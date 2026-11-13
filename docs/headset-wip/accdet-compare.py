#!/usr/bin/env python3
"""Compare a boot's SPMI register dump from XNU with the stock Android baseline.

The kernel driver (hakios.mt6895_headset.c, boot-arg headset=1) reads the MT6368's
accdet and AUDDEC_ANA register blocks over SPMI and prints them. This tool takes that
output and the baseline taken from Android (accdet-baseline.json) and says, register by
register, whether they agree.

What agreement means: the XNU kernel is reaching the same chip, on the same SPMI slave,
at the same addresses, and the chip is in the state Android left it in. A mismatch is
either a wrong address, a wrong slave, a dropped transfer, or a register that really did
change -- and the three look different in the report.

usage: accdet-compare.py <boot log>  [--baseline FILE] [--verbose]
       accdet-compare.py --self-test

The driver's line format is one of:

    headset: accdet 0x2688 = 0x00
    headset: 0x2688=0x00
    headset: read 0x2688 0x00

(all three are accepted; the colon after "headset" is what marks the line).
"""
import argparse
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_BASELINE = os.path.join(HERE, "accdet-baseline.json")

# "headset: ... 0x2688 = 0x00" / "0x2688=0x00" / "read 0x2688 0x00"
FORMS = (
    re.compile(r"0x([0-9a-fA-F]{3,4})\s*=\s*0x([0-9a-fA-F]{1,2})"),
    re.compile(r"0x([0-9a-fA-F]{3,4})\s+0x([0-9a-fA-F]{1,2})\b"),
)
MARKER = re.compile(r"headset\s*:", re.IGNORECASE)


def load_baseline(path):
    with open(path, encoding="utf-8") as f:
        return json.load(f)


def baseline_values(base):
    out = {}
    for region in base["regions"].values():
        for reg, value in region["values"].items():
            out[int(reg, 16)] = value
    return out


def parse_log(path):
    """{register: value} from the driver's lines, plus how many lines carried a marker."""
    seen, marked = {}, 0
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            if not MARKER.search(line):
                continue
            marked += 1
            for form in FORMS:
                for m in form.finditer(line):
                    seen[int(m.group(1), 16)] = int(m.group(2), 16)
    return seen, marked


def compare(want, got):
    """(agreed, differed, missing, extra) -- as sorted lists of (register, expected, actual)."""
    agreed, differed, missing, extra = [], [], [], []
    for reg in sorted(want):
        if reg not in got:
            missing.append((reg, want[reg], None))
        elif got[reg] == want[reg]:
            agreed.append((reg, want[reg], got[reg]))
        else:
            differed.append((reg, want[reg], got[reg]))
    for reg in sorted(got):
        if reg not in want:
            extra.append((reg, None, got[reg]))
    return agreed, differed, missing, extra


def region_of(reg, base):
    for name, region in base["regions"].items():
        if name == "misc":
            continue
        if "first" in region and region["first"] <= reg <= region["last"]:
            return name
    return "misc"


def report(base, want, got, verbose):
    agreed, differed, missing, extra = compare(want, got)
    total = len(want)

    print("baseline: %d registers (%s)" % (total, base.get("source", "?")))
    print("read:     %d registers" % len(got))
    print()

    if differed:
        print("DIFFERED (%d) -- same address, different value:" % len(differed))
        for reg, w, g in differed:
            print("  0x%04x  %-8s  android 0x%02x  xnu 0x%02x   (xor 0x%02x)"
                  % (reg, region_of(reg, base), w, g, w ^ g))
        print()

    if missing:
        # A whole block missing is one fact, not fifty: group them.
        blocks = {}
        for reg, w, _ in missing:
            blocks.setdefault(region_of(reg, base), []).append(reg)
        print("MISSING (%d) -- in the baseline, not in the log:" % len(missing))
        for name, regs in sorted(blocks.items()):
            first, last = min(regs), max(regs)
            if len(regs) == last - first + 1:
                print("  %-8s 0x%04x..0x%04x  (%d contiguous)" % (name, first, last, len(regs)))
            else:
                print("  %-8s %d registers: %s" % (name, len(regs),
                      " ".join("0x%04x" % r for r in regs[:12]) + (" ..." if len(regs) > 12 else "")))
        print()

    if extra:
        print("EXTRA (%d) -- in the log, not in the baseline:" % len(extra))
        for reg, _, g in extra[:20]:
            print("  0x%04x  0x%02x" % (reg, g))
        if len(extra) > 20:
            print("  ... and %d more" % (len(extra) - 20))
        print()

    if verbose:
        print("AGREED (%d):" % len(agreed))
        for reg, w, _ in agreed:
            print("  0x%04x  %-8s 0x%02x" % (reg, region_of(reg, base), w))
        print()

    print("=" * 60)
    print("agreed   %3d / %d  (%.1f%%)" % (len(agreed), total, 100.0 * len(agreed) / total if total else 0))
    print("differed %3d" % len(differed))
    print("missing  %3d" % len(missing))
    print("extra    %3d" % len(extra))
    print()

    # The verdict. A wrong slave or a wrong channel shows up as "everything missing";
    # a wrong address shows up as a block of registers that disagree in a pattern;
    # a dropped transfer shows up as one register.
    if not got:
        print("VERDICT: the driver printed no register at all. Either headset=1 was not")
        print("         passed, or the SPMI transaction never completed.")
        return 2

    # A dead bus does not read as zero disagreement: it reads as one value everywhere, and
    # that value is often 0x00, which the baseline happens to hold almost half of. So the
    # first question is not how much agrees but whether the read varied at all. Measured:
    # an all-zero read scores 60% against this baseline and would otherwise pass.
    #
    # Only a read of many registers can be judged this way: one register that happens to
    # read 0x00 is a normal, honest answer, not a dead bus.
    distinct = set(got.values())
    if len(got) >= 8 and len(distinct) == 1:
        only = distinct.pop()
        print("VERDICT: every one of the %d registers read back 0x%02x. A working transfer returns" % (len(got), only))
        print("         the chip's own values, which differ from register to register. This is")
        print("         what a channel that never carried a command, or a bus stuck low, looks like.")
        print("         %d of the baseline's %d registers hold 0x%02x too, which is why the"
              % (sum(1 for v in want.values() if v == only), total, only))
        print("         percentage below looks like progress and is not.")
        return 2
    if len(got) >= 16 and len(distinct) == 2:
        print("NOTE: only two distinct values came back (%s), over %d registers. Worth"
              % (" ".join("0x%02x" % v for v in sorted(distinct)), len(got)))
        print("      looking at before trusting the percentages.")
        print()

    if len(missing) == total:
        print("VERDICT: nothing matched. The read did not reach this chip -- wrong slave,")
        print("         wrong software channel, or the arbiter was never brought up.")
        return 2
    if not differed and not missing:
        print("VERDICT: every register the baseline has, the kernel read back the same.")
        print("         The SPMI read path, the slave number and the register addresses are right.")
        return 0
    if len(agreed) > total * 0.8:
        print("VERDICT: %d of %d registers agree. The addressing is right; what differs is"
              % (len(agreed), total))
        print("         either state the chip was left in, or values our own init wrote.")
        return 0
    print("VERDICT: %d of %d agree -- too few to call the addressing right."
          % (len(agreed), total))
    return 1


def self_test():
    """Prove the parser and the comparison on made-up logs, so a real run is not the first test."""
    import tempfile
    # Eight registers, so that "most of them agree" and "hardly any agree" are different
    # outcomes under the 80% rule the verdict uses; four would not separate the two.
    accdet_vals = {"0x%04x" % r: r & 0xff for r in range(0x2688, 0x268e)}
    base = {"source": "synthetic", "regions": {
        "accdet": {"first": 0x2688, "last": 0x268d, "values": accdet_vals},
        "auddec": {"first": 0x2508, "last": 0x2509,
                   "values": {"0x2508": 7, "0x2509": 9}},
        "misc": {"values": {"0x2319": 0}},
    }}
    want = baseline_values(base)
    assert len(want) == 9, want  # six in accdet, two in auddec, one in misc

    def clean(**override):
        """One log line per baseline register, with any register replaced."""
        lines = []
        for reg in sorted(want):
            value = override.get("r%04x" % reg, want[reg])
            lines.append("headset: 0x%04x = 0x%02x" % (reg, value))
        return lines

    cases = [
        ("all three line forms are read", [
            "headset: accdet 0x2688 = 0x%02x" % want[0x2688],
            "headset: 0x2689=0x%02x" % want[0x2689],
            "headset: read 0x2508 0x%02x" % want[0x2508],
            "headset: 0x2319 = 0x%02x" % want[0x2319],
            "headset: 0x268a = 0x%02x" % want[0x268a],
            "headset: 0x268b = 0x%02x" % want[0x268b],
            "headset: 0x268c = 0x%02x" % want[0x268c],
            "headset: 0x268d = 0x%02x" % want[0x268d],
            "headset: 0x2509 = 0x%02x" % want[0x2509],
        ], 9, "every register the baseline has"),
        ("a line without the marker is ignored", [
            "mali: 0x2688 = 0x99",
            "headset: 0x2688 = 0x%02x" % want[0x2688],
        ], 1, "too few to call the addressing right"),
        ("nothing printed", [], 0, "printed no register"),
        ("one register differs, the rest agree", clean(r2688=0x01), 9, "what differs is"),
        ("every register differs", [
            "headset: 0x%04x = 0x%02x" % (reg, want[reg] ^ 0xff) for reg in sorted(want)
        ], 9, "too few to call the addressing right"),
        ("half the registers missing", clean()[:4], 4, "too few to call the addressing right"),
        ("the whole block missing", [
            "headset: 0x2508 = 0x%02x" % want[0x2508],
        ], 1, "MISSING"),
        # A bus that answers zero everywhere scores 60% against the real baseline, because
        # almost half of the baseline's registers are zero. It has to be called dead by its
        # shape, not by its score, or a dead read passes.
        ("a dead bus reads zero everywhere", [
            "headset: 0x%04x = 0x00" % reg for reg in sorted(want)
        ], 9, "every one of the 9 registers read back 0x00"),
        # ... but one register reading 0x00 is an honest answer, not a dead bus.
        ("a single register that reads zero", [
            "headset: 0x2319 = 0x00"
        ], 1, "too few to call the addressing right"),
    ]

    failures = 0
    for name, lines, expect_count, expect_text in cases:
        with tempfile.NamedTemporaryFile("w", suffix=".log", delete=False) as f:
            f.write("\n".join(lines) + "\n")
            path = f.name
        try:
            got, _marked = parse_log(path)
            if len(got) != expect_count:
                print("FAIL %-40s read %d registers, wanted %d" % (name, len(got), expect_count))
                failures += 1
                continue
            import io
            from contextlib import redirect_stdout
            buf = io.StringIO()
            with redirect_stdout(buf):
                report(base, want, got, verbose=False)
            if expect_text not in buf.getvalue():
                print("FAIL %-40s verdict lacks %r" % (name, expect_text))
                failures += 1
                continue
            print("ok   %s" % name)
        finally:
            os.unlink(path)

    if failures:
        print("\n%d self-test(s) failed" % failures)
        return 1
    print("\nall self-tests passed")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("log", nargs="?", help="the boot log the driver's lines are in")
    ap.add_argument("--baseline", default=DEFAULT_BASELINE, help="the Android baseline (default: %(default)s)")
    ap.add_argument("--verbose", action="store_true", help="also list the registers that agree")
    ap.add_argument("--self-test", action="store_true", help="check the parser and the comparison themselves")
    args = ap.parse_args()

    if args.self_test:
        return self_test()
    if not args.log:
        ap.error("a boot log is needed (or --self-test)")

    base = load_baseline(args.baseline)
    got, marked = parse_log(args.log)
    if marked == 0:
        print("no line in %s carries the driver's 'headset:' marker" % args.log)
        return 2
    return report(base, baseline_values(base), got, args.verbose)


if __name__ == "__main__":
    sys.exit(main())
