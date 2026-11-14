#!/usr/bin/env python3
"""Parse an ftrace capture of MT6368 accdet register traffic into a baseline-shaped JSON.

Why this exists: the bare-metal XNU side has never read back a correct value from the
MT6368's accdet block. Android drives the same chip every day, so Android's register
traffic is the ground truth -- if we can record it, we can diff against it instead of
guessing. This script is the half that turns a trace into that ground truth.

Two capture formats are understood, because two things actually work on the device:

  regmap     -- the kernel's own regmap tracepoints
                (/sys/kernel/tracing/events/regmap/regmap_reg_read{,write})
                These carry the real register address and the real value, plus a device
                name. For the MT6368 that name is "0-05" (SPMI bus 0, USID 5), which is
                how we tell it apart from the MT6363 at "0-04".

  kprobe     -- kprobe_events hung off regmap_read / regmap_write / regmap_update_bits_base
                with $arg1/$arg2 already decoded to %x by the probe definition.
                Fallback for a kernel built without the regmap tracepoints.

Output is the same shape as accdet-baseline.json, so accdet-compare.py can diff it
against the stock-Android baseline without a translation step:

    {"sid": 5, "source": "...", "total_registers": N,
     "regions": {"accdet": {"first": .., "last": .., "count": .., "values": {..}},
                 "auddec": {...}, "misc": {...}},
     "sequence": [[reg, value, "r"|"w"], ...]}

The "sequence" key is an extension: the baseline has no ordering, but ordering is the
whole point of a trace. Every downstream consumer may ignore it.

usage:
    accdet-trace-parse.py TRACE [--format auto|regmap|kprobe] [--out FILE]
                                [--baseline FILE] [--quiet]
    accdet-trace-parse.py --selftest

Exit status: 0 ok, 1 nothing usable in the trace, 2 usage error, 3 selftest failure.
"""
import argparse
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_BASELINE = os.path.join(HERE, "accdet-baseline.json")

# MT6368 sits on SPMI bus 0 as USID 5. The kernel names SPMI devices "%d-%02x"
# (drivers/spmi/spmi.c: dev_set_name(&sdev->dev, "%d-%02x", ctrl->nr, sdev->usid)),
# so the regmap that belongs to it is called "0-05". The MT6363 next door is "0-04"
# and it is chatty, which is exactly why filtering on this string matters.
MT6368_SID = 5
MT6368_REGMAP_NAME = "0-05"

# Register windows, copied from what the vendor driver actually touches
# (raw/mt6368-accdet.c: cat_register/dump_register) so a parsed trace lands in the
# same three buckets as accdet-baseline.json.
REGION_ACCDET = (0x2688, 0x26D3)   # ACCDET_RG
REGION_AUDDEC = (0x2508, 0x2537)   # AUDDEC_ANA_RG
# Everything else the driver reads, e.g. 0x1108 (AUXADC_RQST_CH5), 0x11c0, 0x2319..0x2323, 0x8..0x13.
# Kept as "misc" rather than dropped: those are init-time values the baseline captured too.

# --- regmap tracepoint lines -------------------------------------------------------
# The tracepoint prints through TP_printk("%s reg=%x val=%x", name, reg, val), and
# ftrace renders it as "   <name>-<pid> [cpu] .... <ts>: regmap_reg_write: 0-05 reg=26b6 val=ff"
# The task name can contain spaces (kworker/u16:1), so anchor on the event name instead
# of on field positions.
RE_TRACEPOINT = re.compile(
    r"\bregmap_reg_(?P<kind>read|write|read_cache):\s*"
    r"(?P<name>\S+)\s+reg=(?P<reg>[0-9a-fA-F]+)\s+val=(?P<val>[0-9a-fA-F]+)"
)

# --- kprobe lines -------------------------------------------------------------------
# A probe defined as
#     p:acc_r regmap_read reg=%x1 val=%x2
# renders like "  <...>-1234 [000] .... 12.345678: acc_r: (regmap_read+0x0/0x20) reg=26b6 val=ff".
# We accept any probe name; the fields are key=value, which is what we match on.
RE_KPROBE = re.compile(
    r":\s*(?P<event>[A-Za-z_][A-Za-z0-9_]*):\s*(?:\([^)]*\)\s*)?"
    r"(?P<fields>(?:[A-Za-z_][A-Za-z0-9_]*=[0-9a-fA-Fx]+\s*)+)"
)
RE_KV = re.compile(r"([A-Za-z_][A-Za-z0-9_]*)=([0-9a-fA-Fx]+)")

# Field names various probe definitions might use. First match wins.
REG_KEYS = ("reg", "addr", "r")
VAL_KEYS = ("val", "value", "v", "ret")
NAME_KEYS = ("name", "dev", "map")


def _region_of(reg):
    if REGION_ACCDET[0] <= reg <= REGION_ACCDET[1]:
        return "accdet"
    if REGION_AUDDEC[0] <= reg <= REGION_AUDDEC[1]:
        return "auddec"
    return "misc"


def _num(tok):
    """Parse a hex/decimal token from a trace field. ftrace prints these without 0x."""
    if tok is None:
        return None
    t = tok.strip()
    if t.lower().startswith("0x"):
        t = t[2:]
    try:
        return int(t, 16)
    except ValueError:
        return None


def _pick(fields, keys):
    for k in keys:
        if k in fields:
            return fields[k]
    return None


class Stats(object):
    def __init__(self):
        self.lines = 0
        self.matched = 0
        self.kept = 0
        self.dropped_other_device = 0
        self.dropped_unparsed = 0
        self.dropped_wide = 0
        self.names_seen = {}

    def as_dict(self):
        return {
            "lines": self.lines,
            "matched": self.matched,
            "kept": self.kept,
            "dropped_other_device": self.dropped_other_device,
            "dropped_unparsed": self.dropped_unparsed,
            "dropped_wide": self.dropped_wide,
            "names_seen": dict(sorted(self.names_seen.items(), key=lambda kv: -kv[1])),
        }


def parse_text(text, fmt="auto", sid=MT6368_SID, want_name=MT6368_REGMAP_NAME):
    """Yield (reg, value, 'r'|'w') in trace order, plus a Stats object.

    Filtering rules, and why each exists:

    * A regmap tracepoint names its map. If the name is present and it is not ours, drop
      the line and count it -- a silent drop would make "MT6363 wasn't noisy" look true.
    * A kprobe on regmap_read/regmap_write carries no map name unless the probe was
      written to fetch $arg0 and dereference it, which is fragile. So without a name we
      keep the line and rely on the register window below. That is a real limitation and
      the stats say how many lines were kept on that basis ("names_seen" lacking ours).
    * Values wider than 8 bits cannot be a PMIC register byte. regmap_printk prints
      unsigned ints; a 16-bit value at a 0x26xx address would mean we grabbed a pointer,
      not a value. Drop and count rather than poison the JSON.
    """
    stats = Stats()
    out = []
    for raw in text.splitlines():
        stats.lines += 1
        line = raw.rstrip("\n")

        m = RE_TRACEPOINT.search(line)
        if m:
            kind = m.group("kind")
            name = m.group("name")
            reg = _num(m.group("reg"))
            val = _num(m.group("val"))
            if reg is None or val is None:
                stats.dropped_unparsed += 1
                continue
            stats.matched += 1
            stats.names_seen[name] = stats.names_seen.get(name, 0) + 1
            if want_name is not None and name != want_name:
                stats.dropped_other_device += 1
                continue
            if val > 0xFF:
                stats.dropped_wide += 1
                continue
            # read_cache means the value came from the software cache, not the wire.
            # It is still a true statement about the driver's view, and during bring-up
            # that is what we are comparing against, so keep it but mark nothing.
            out.append((reg, val, "w" if kind == "write" else "r"))
            stats.kept += 1
            continue

        if fmt == "regmap":
            # Format was pinned to regmap and this line is not one, so it is noise.
            continue

        m = RE_KPROBE.search(line)
        if not m:
            continue
        fields = {}
        for k, v in RE_KV.findall(m.group("fields")):
            fields[k] = v
        reg = _num(_pick(fields, REG_KEYS))
        val = _num(_pick(fields, VAL_KEYS))
        name = _pick(fields, NAME_KEYS)
        if reg is None or val is None:
            stats.dropped_unparsed += 1
            continue
        stats.matched += 1
        if name is not None:
            stats.names_seen[name] = stats.names_seen.get(name, 0) + 1
            if want_name is not None and name != want_name:
                stats.dropped_other_device += 1
                continue
        if val > 0xFF:
            stats.dropped_wide += 1
            continue
        ev = m.group("event")
        # Probe naming convention from the shell script: *_w = write, *_r = read,
        # *_u = update_bits (which is a read then a write; the read is the pre-image,
        # so it is recorded as a read and the post-image write follows from the write probe).
        rw = "w" if ev.endswith(("_w", "write", "wr")) else "r"
        out.append((reg, val, rw))
        stats.kept += 1
    return out, stats


def to_baseline(seq, source, sid=MT6368_SID):
    """Fold a (reg, value, rw) sequence into the accdet-baseline.json shape.

    Last-value-wins per register: the trace is a history, the baseline is a state. The
    last thing the driver wrote (or read) is the closest analogue to the single dump the
    baseline was taken from. The full history stays in "sequence".
    """
    regions = {}
    for reg, val, _rw in seq:
        regions.setdefault(_region_of(reg), {})["0x%04x" % reg] = val

    out_regions = {}
    for name in ("accdet", "auddec", "misc"):
        vals = regions.get(name)
        if not vals:
            continue
        addrs = sorted(int(k, 16) for k in vals)
        out_regions[name] = {
            "first": addrs[0],
            "last": addrs[-1],
            "count": len(addrs),
            "values": {k: vals[k] for k in sorted(vals, key=lambda x: int(x, 16))},
        }

    return {
        "sid": sid,
        "source": source,
        "total_registers": sum(r["count"] for r in out_regions.values()),
        "regions": out_regions,
        "sequence": [[r, v, w] for r, v, w in seq],
    }


def diff_against(parsed, baseline):
    """Compare a parsed trace against the baseline. Values only; ordering is not comparable."""
    report = {"only_in_trace": [], "only_in_baseline": [], "differ": [], "same": 0}
    bvals = {}
    for _rname, r in baseline.get("regions", {}).items():
        for k, v in r.get("values", {}).items():
            bvals[int(k, 16)] = v
    tvals = {}
    for _rname, r in parsed.get("regions", {}).items():
        for k, v in r.get("values", {}).items():
            tvals[int(k, 16)] = v

    for a in sorted(set(tvals) | set(bvals)):
        if a not in bvals:
            report["only_in_trace"].append((a, tvals[a]))
        elif a not in tvals:
            report["only_in_baseline"].append((a, bvals[a]))
        elif tvals[a] != bvals[a]:
            report["differ"].append((a, bvals[a], tvals[a]))
        else:
            report["same"] += 1
    return report


# ---------------------------------------------------------------------------------
# selftest: everything below runs on synthetic trace text, no device required.
# ---------------------------------------------------------------------------------

FAKE_REGMAP = """\
# tracer: nop
#
# entries-in-buffer/entries-written: 42/42   #P:8
#
#                              _-----=> irqs-off
#                             / _----=> need-resched
#                            | / _---=> hardirq/softirq
#                            || / _--=> preempt-depth
#                            ||| /     delay
#           TASK-PID     CPU#  ||||   TIMESTAMP  FUNCTION
#              | |         |   ||||       |         |
     kworker/u16:1-812   [003] ....  1478.232024: regmap_reg_write: 0-05 reg=26a9 val=0
     kworker/u16:1-812   [003] ....  1478.232050: regmap_reg_read: 0-05 reg=26b6 val=ff
     kworker/u16:1-812   [003] ....  1478.232100: regmap_reg_write: 0-05 reg=26aa val=1
       mt6368-codec-801  [001] ....  1478.232200: regmap_reg_read: 0-05 reg=26b7 val=0
     kworker/u16:1-812   [003] ....  1478.232300: regmap_reg_write: 0-05 reg=2510 val=15
       mt6368-codec-801  [001] ....  1478.232400: regmap_reg_read: 0-04 reg=1c2a val=3
     kworker/u16:1-812   [003] ....  1478.232500: regmap_reg_write: 0-04 reg=1c2b val=3
       mt6368-codec-801  [001] ....  1478.232600: regmap_reg_read: 0-05 reg=1108 val=0
     kworker/u16:1-812   [003] ....  1478.232700: regmap_reg_read: 0-05 reg=2688 val=1234
     kworker/u16:2-813   [002] ....  1478.232800: sched_switch: prev_comm=x next_comm=y
"""

FAKE_KPROBE = """\
# tracer: nop
#
     kworker/u16:1-812   [003] ....  1478.232024: accdet_w: (regmap_write+0x0/0x20) reg=26a9 val=0
     kworker/u16:1-812   [003] ....  1478.232050: accdet_r: (regmap_read+0x0/0x20) reg=26b6 val=ff
     kworker/u16:1-812   [003] ....  1478.232100: accdet_w: (regmap_write+0x0/0x20) reg=26aa val=1
     kworker/u16:1-812   [003] ....  1478.232200: accdet_u: (regmap_update_bits_base+0x0/0x20) reg=26b6 val=3f
     kworker/u16:1-812   [003] ....  1478.232300: accdet_r: (regmap_read+0x0/0x20) reg=26b6 val=ff
     kworker/u16:1-812   [003] ....  1478.232400: accdet_r: (regmap_read+0x0/0x20) reg=1108 val=0
"""


def _selftest():
    fails = []
    total = [0]

    def ck(name, cond, detail=""):
        total[0] += 1
        if cond:
            sys.stdout.write("ok   %s\n" % name)
        else:
            sys.stdout.write("FAIL %s %s\n" % (name, detail))
            fails.append(name)

    # 1. regmap format: the MT6368 reads/writes survive, in order.
    #    The fixture has 7 lines naming 0-05; one of them (0x2688 val=1234) carries a
    #    16-bit "value" and is dropped as a pointer, so 6 make it through.
    seq, st = parse_text(FAKE_REGMAP, fmt="auto")
    ck("regmap: keeps exactly the MT6368 lines", st.kept == 6, "got %d" % st.kept)
    ck("regmap: order preserved", seq[0] == (0x26A9, 0x00, "w") and seq[1] == (0x26B6, 0xFF, "r"),
       "got %r" % (seq[:2],))

    # 2. The MT6363 (0-04) must be dropped -- that is the whole point of the name filter.
    ck("regmap: drops 0-04 (MT6363)", st.dropped_other_device == 2,
       "got %d" % st.dropped_other_device)
    ck("regmap: 0-04 registers never reach output",
       all(r not in (0x1C2A, 0x1C2B) for r, _v, _w in seq))

    # 3. A 16-bit "value" is a pointer, not a register byte; it must not poison the JSON.
    ck("regmap: drops >8-bit values", st.dropped_wide == 1, "got %d" % st.dropped_wide)
    ck("regmap: 0x2688 absent from output", all(r != 0x2688 for r, _v, _w in seq))

    # 4. Non-regmap lines (sched_switch, the header) are not mistaken for register traffic.
    #    9 lines carry a regmap_reg_* event; the sched_switch and the 12 header lines do not.
    ck("regmap: ignores non-regmap lines", st.matched == 9, "got %d" % st.matched)

    # 5. Region bucketing matches the baseline's three buckets.
    bl = to_baseline(seq, "selftest")
    ck("bucket: accdet window 0x2688-0x26d3", "0x26a9" in bl["regions"]["accdet"]["values"])
    ck("bucket: auddec window 0x2508-0x2537", "0x2510" in bl["regions"]["auddec"]["values"])
    ck("bucket: misc catches 0x1108", "0x1108" in bl["regions"]["misc"]["values"])
    ck("bucket: total matches sum", bl["total_registers"] == 6, "got %d" % bl["total_registers"])

    # 6. kprobe format parses too, and _w/_r/_u suffixes decide direction.
    seq2, st2 = parse_text(FAKE_KPROBE, fmt="kprobe")
    ck("kprobe: parses six probes", st2.kept == 6, "got %d" % st2.kept)
    ck("kprobe: _w is a write", seq2[0] == (0x26A9, 0x00, "w"), "got %r" % (seq2[0],))
    ck("kprobe: _r is a read", seq2[1] == (0x26B6, 0xFF, "r"), "got %r" % (seq2[1],))
    ck("kprobe: _u keeps the pre-image as a read", seq2[3] == (0x26B6, 0x3F, "r"), "got %r" % (seq2[3],))

    # 7. Last-value-wins: the second read of 0x26b6 overwrites the first in the state view,
    #    but both stay in the sequence. Getting this backwards would hide the whole point
    #    of a trace (that a register changed).
    ck("fold: last value wins", bl["regions"]["accdet"]["values"]["0x26b6"] == 0xFF)
    ck("fold: sequence keeps every sample", len(bl["sequence"]) == 6, "got %d" % len(bl["sequence"]))
    bl2 = to_baseline(seq2, "selftest2")
    ck("fold: kprobe last read wins", bl2["regions"]["accdet"]["values"]["0x26b6"] == 0xFF)

    # 8. Diffing against a baseline reports the three kinds of disagreement separately.
    fake_baseline = {"regions": {"accdet": {"values": {"0x26b6": 0xFF, "0x26a9": 0x01, "0x26ff": 0x00}}}}
    rep = diff_against(bl, fake_baseline)
    ck("diff: same value counted", rep["same"] >= 1, "got %d" % rep["same"])
    ck("diff: differing value reported", any(a == 0x26A9 for a, _b, _t in rep["differ"]),
       "got %r" % (rep["differ"],))
    ck("diff: baseline-only reported", any(a == 0x26FF for a, _v in rep["only_in_baseline"]))
    ck("diff: trace-only reported", any(a == 0x2510 for a, _v in rep["only_in_trace"]))

    # 9. Empty/garbage input fails cleanly instead of emitting a bogus all-zero baseline.
    seq3, st3 = parse_text("nothing here\nnot a trace\n", fmt="auto")
    ck("empty: no sequence", seq3 == [])
    ck("empty: stats still honest", st3.lines == 2 and st3.kept == 0)

    sys.stdout.write("\n%d checks, %d failed\n" % (total[0], len(fails)))
    if fails:
        sys.stdout.write("FAILED: %s\n" % ", ".join(fails))
        return 3
    sys.stdout.write("selftest: all assertions passed\n")
    return 0


def main(argv):
    ap = argparse.ArgumentParser(
        add_help=True,
        description="Parse an ftrace capture of MT6368 accdet register traffic.",
    )
    ap.add_argument("trace", nargs="?", help="trace file (use - for stdin)")
    ap.add_argument("--format", default="auto", choices=("auto", "regmap", "kprobe"))
    ap.add_argument("--out", help="write JSON here (default: stdout)")
    ap.add_argument("--baseline", default=DEFAULT_BASELINE,
                    help="baseline to diff against (default: %s)" % DEFAULT_BASELINE)
    ap.add_argument("--no-diff", action="store_true", help="skip the baseline comparison")
    ap.add_argument("--quiet", action="store_true", help="suppress the stats block on stderr")
    ap.add_argument("--selftest", action="store_true", help="run built-in assertions and exit")
    args = ap.parse_args(argv)

    if args.selftest:
        return _selftest()

    if not args.trace:
        ap.print_usage(sys.stderr)
        sys.stderr.write("error: no trace file given (or --selftest)\n")
        return 2

    if args.trace == "-":
        text = sys.stdin.read()
        src = "<stdin>"
    else:
        try:
            with open(args.trace, "r", errors="replace") as fh:
                text = fh.read()
        except OSError as e:
            sys.stderr.write("error: cannot read %s: %s\n" % (args.trace, e))
            return 2
        src = args.trace

    seq, stats = parse_text(text, fmt=args.format)
    if not args.quiet:
        sys.stderr.write("parsed %s\n" % src)
        for k, v in stats.as_dict().items():
            if k == "names_seen":
                if v:
                    sys.stderr.write("  names seen: %s\n" % ", ".join(
                        "%s(%d)" % (n, c) for n, c in v.items()))
            else:
                sys.stderr.write("  %-24s %d\n" % (k, v))

    if not seq:
        sys.stderr.write("error: no MT6368 register accesses found in %s\n" % src)
        sys.stderr.write("       check --format, and that the trace actually contains the "
                         "regmap tracepoints or the kprobe events\n")
        return 1

    parsed = to_baseline(seq, "parsed from %s" % os.path.basename(src))

    if not args.no_diff and os.path.exists(args.baseline):
        try:
            with open(args.baseline, "r") as fh:
                base = json.load(fh)
        except (OSError, ValueError) as e:
            sys.stderr.write("warning: cannot read baseline %s: %s\n" % (args.baseline, e))
            base = None
        if base is not None:
            rep = diff_against(parsed, base)
            parsed["diff_vs_baseline"] = {
                "baseline": os.path.basename(args.baseline),
                "same": rep["same"],
                "differ": [{"reg": "0x%04x" % a, "baseline": b, "trace": t}
                           for a, b, t in rep["differ"]],
                "only_in_trace": [{"reg": "0x%04x" % a, "trace": t}
                                  for a, t in rep["only_in_trace"]],
                "only_in_baseline": [{"reg": "0x%04x" % a, "baseline": b}
                                     for a, b in rep["only_in_baseline"]],
            }
            if not args.quiet:
                sys.stderr.write("  vs baseline: %d same, %d differ, %d trace-only, %d baseline-only\n"
                                 % (rep["same"], len(rep["differ"]),
                                    len(rep["only_in_trace"]), len(rep["only_in_baseline"])))

    blob = json.dumps(parsed, indent=2, sort_keys=False) + "\n"
    if args.out:
        try:
            with open(args.out, "w") as fh:
                fh.write(blob)
        except OSError as e:
            sys.stderr.write("error: cannot write %s: %s\n" % (args.out, e))
            return 2
        if not args.quiet:
            sys.stderr.write("wrote %s (%d registers)\n" % (args.out, parsed["total_registers"]))
    else:
        sys.stdout.write(blob)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
