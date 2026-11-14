#!/usr/bin/env python3
"""mmu-audit.py -- audit the "AS0 table walk rejected by the bus" reasoning chain.

Background. The driver read MMU interrupt value 0x10000 as MMU_BUS_ERROR(0) and built a long
argument on it. Upstream panthor says bits[31:16] of that register are REQ_COMPLETE, not fault
status (panthor_mmu.c:646-650, "Bits 16 to 31 mean REQ_COMPLETE"). Everything that rested on
the old reading has to be re-judged.

What this script checks:

  1. every line of the driver that matches a pattern the chain was written with
  2. every *comment* hit is either audited in findings/mmu-refuted.json or exempted here with
     a stated reason -- comment text is where claims live, so nothing there may go unjudged
  3. audited snippets still match the source (drift detection, so the JSON cannot go stale)
  4. --selftest proves all three checks still fail when they should

Code lines are counted, not audited: a store or a printf does not assert a premise. The few
code lines that *do* encode the disputed semantics are listed in CODE_CLAIMS so they are
visible in the report.

Read-only. Never writes to the driver, never needs root.

Usage:
    ./mmu-audit.py                     # audit the live driver
    ./mmu-audit.py --src PATH          # audit a snapshot instead
    ./mmu-audit.py --selftest          # 9 assertions
    ./mmu-audit.py --list-hits         # print every hit, classified
    ./mmu-audit.py --json-out          # machine-readable report on stdout

Exit codes: 0 clean, 1 drift / unjudged comment, 2 usage or I/O error.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_SRC = "/home/lizi/IOS/hakios-mtk/mtk/mt6895/drivers/gpu/hakios.mali610.c"
DEFAULT_JSON = os.path.join(HERE, "findings", "mmu-refuted.json")

PATTERNS = [
    ("mmu-0x10000", r"0x10000(?![0-9a-fA-F])"),
    ("bus-error", r"BUS[_ ]?ERROR|bus error", re.IGNORECASE),
    ("walk", r"\bwalk\b|\bwalked\b|\bwalker\b|走查|table walk", re.IGNORECASE),
    ("as0", r"\bAS0\b|address space 0", re.IGNORECASE),
    ("as-command", r"\bkAsCommand\b|AS_COMMAND|\bkAsCmdUpdate\b|\bUPDATE\b"),
    ("park", r"\bpark\b|\bparked\b|\bparking\b|\bcollapse", re.IGNORECASE),
    ("req-complete", r"REQ_COMPLETE|REQCOMPLETE|request completions?", re.IGNORECASE),
    ("as-status", r"\bas_ready\b|\bAS_STATUS\b"),
]

# Comment hits that mention a pattern but assert nothing about the AS0 walk. Each needs a
# reason; an entry whose text no longer matches is itself reported, so the list cannot rot.
NARRATIVE_EXEMPT = {
    "walked every neighbouring offset": "CPU reading device registers, not the MMU walking",
    "Unhandled Page fault in AS0 at VA 0x0": "quotes the vendor board's Android log; not our inference",
    "kvtophys() would answer the same thing": "walks the CPU's tables; says nothing about the GPU walker",
    "before the GPU can walk it": "plain statement of purpose",
    "The GPU's walker is kbase's": "XN bit encoding, unrelated to the interrupt latch",
    "both survive a walk": "PTE attribute, not an UPDATE outcome",
    "level-3 table this code builds is walked at 4K": "granule geometry, not a walk outcome",
    "Walk the tables the way the MCU will": "header of the CPU-side walk printer",
    "A leaf whose low two bits are not 11": "descriptor-validity rule, judged at :811/:1122 instead",
    "walk finds against what the section needs": "coverage self-check: CPU walks its own copy",
    "should not walk mfg2..mfg12": "iterating power domains",
    "vendor mux has UPDATE bit20": "topckgen clock mux, a different register",
    "do not walk it through the three values": "'do not enumerate', not table walking",
    "walk the same path": "follow the vendor's init sequence",
    "mali=3 loads the firmware and stops": "level table; names what each level does",
    "AS0 is not among them": "ordering rationale: AS0 needs the firmware table",
    "because AS0's translation table is the one": "ordering rationale",
    "last of the eight": "ordering rationale",
    "went busy between the walk and here": "MFG debug channel, independent of the interrupt bits",
}

# Code lines that encode the disputed semantics rather than merely moving values. Not required
# to be audited, but named so a reader can find them.
CODE_CLAIMS = {
    "as_ready(unsigned as)": "AS_STATUS.bit0 poll: the one independent proof that a command retired",
    "AS_STEP(\"update\"": "the single UPDATE whose retirement sets bit(16+as)",
    "mali: AS%u parked": "per-space latch read after each parked UPDATE: this produced 0x40000..0x800000",
    "just after the clear, AS0 parked as a control": "control run (mali_as0=1); never executed on record",
    "AS0 parked: transtab": "control run readback of the parked AS0",
    "mali: round7 AS%u after UPDATE": "reads fault status, fault address and the latch in one line",
    "mfg_bus_idle(\"before the table walk\")": "MFG AXI channel sampled independently of the latch",
    "mfg_bus_idle(\"after the table walk\")": "same channel after the walk: 0x4 both times",
    "mali: address space 0 is live": "the claim the next step rests on: AS0 configured, not walked",
    "request completions": "decodes bit[31:16] as completions and bit[15:0] as faults: the correct split",
}

VERDICTS = {"void", "rewrite", "holds", "unverified"}
REQUIRED_KEYS = ("line", "snippet", "premise", "verdict", "fix")


def norm(text: str) -> str:
    """Strip a C comment's decoration so a snippet can be compared with a source line."""
    s = text.strip()
    while s[:1] in ("*", "/"):
        s = s[1:].strip()
    return s


def is_comment(text: str) -> bool:
    s = text.strip()
    return s.startswith("*") or s.startswith("/*") or s.startswith("//")


def paragraph(lines, line):
    """The C-comment paragraph around `line`: the claim a reader takes as one unit.

    A verdict written for one sentence should cover the rest of its paragraph, otherwise every
    wrapped line would need its own entry and the JSON would grow faster than the arguments.
    A paragraph is bounded by a bare ' * ', by blank lines, and by the comment's own '/*' '*/'.
    """
    def usable(i):
        if i < 1 or i > len(lines):
            return False
        t = lines[i - 1].strip()
        return is_comment(t) and t not in ("*", "*/", "/*") and t != ""

    start = end = line
    while usable(start - 1) and not lines[start - 2].strip().startswith("/*"):
        start -= 1
    while usable(end + 1) and not lines[end].strip().endswith("*/"):
        end += 1
    return start, end


def code_claim(text: str):
    """The reason a code line is worth naming, or None. Keyed by text for the same reason as
    the exemptions: a concurrent edit shifts line numbers, not sentences."""
    for frag, reason in CODE_CLAIMS.items():
        if frag in text:
            return reason
    return None


def exempt_reason(text: str):
    """Exemptions are keyed by a distinctive fragment, so moving a line cannot move the excuse
    onto a different claim."""
    body = norm(text)
    for frag, reason in NARRATIVE_EXEMPT.items():
        if frag in body:
            return reason
    return None


def scan(path: str, patterns=PATTERNS):
    """Return every (lineno, pattern_name, line_text) hit, first pattern wins, in file order."""
    with open(path, "r", encoding="utf-8", errors="replace") as fh:
        lines = fh.readlines()
    compiled = []
    for entry in patterns:
        name, rx = entry[0], entry[1]
        flags = entry[2] if len(entry) > 2 else 0
        compiled.append((name, re.compile(rx, flags)))
    hits = []
    for idx, raw in enumerate(lines, start=1):
        for name, rx in compiled:
            if rx.search(raw):
                hits.append((idx, name, raw.rstrip("\n")))
                break
    return hits, lines


def load_findings(path):
    with open(path, "r", encoding="utf-8") as fh:
        data = json.load(fh)
    if not isinstance(data, list):
        raise ValueError("findings file must be a JSON array")
    for i, ent in enumerate(data):
        missing = [k for k in REQUIRED_KEYS if k not in ent]
        if missing:
            raise ValueError("entry %d is missing %s" % (i, ", ".join(missing)))
        if ent["verdict"] not in VERDICTS:
            raise ValueError("entry %d has verdict %r, expected one of %s"
                             % (i, ent["verdict"], sorted(VERDICTS)))
        if ent.get("end_line", ent["line"]) < ent["line"]:
            raise ValueError("entry %d has end_line before line" % i)
    return data


def entry_for(line, findings, lines=None):
    """The audited entry owning `line`. With `lines`, the entry's paragraph counts as covered."""
    for ent in findings:
        lo, hi = ent["line"], ent.get("end_line", ent["line"])
        if lo <= line <= hi:
            return ent
        if lines is not None and lo <= line <= paragraph(lines, lo)[1]:
            return ent
    return None


def check_drift(findings, lines):
    """An audited entry whose snippet is no longer at its line has either moved or gone.

    Returns (moved, gone). moved is [(entry, new_line)] when the same text is still somewhere
    in the file -- a concurrent edit shifted the line numbers -- and gone is [entry] when the
    text is nowhere, which means the line was rewritten and the verdict needs re-checking.
    """
    flat = [norm(l) for l in lines]
    # Search in a space where line breaks are spaces, because a snippet spans lines and was
    # recorded joined by spaces. Offsets are mapped back to line numbers below.
    starts, pos, joined = [], 0, []
    for f in flat:
        starts.append(pos)
        joined.append(f)
        pos += len(f) + 1
    joined = " ".join(joined)
    moved, gone = [], []
    for ent in findings:
        end = ent.get("end_line", ent["line"])
        window = " ".join(norm(lines[i]) for i in range(ent["line"] - 1, min(end, len(lines))))
        if norm(ent["snippet"]) in window:
            continue
        hit = joined.find(norm(ent["snippet"]))
        if hit < 0:
            gone.append(ent)
            continue
        lo, hi = 0, len(starts) - 1
        while lo < hi:
            mid = (lo + hi + 1) // 2
            if starts[mid] <= hit:
                lo = mid
            else:
                hi = mid - 1
        moved.append((ent, lo + 1))
    return moved, gone


def audit(src, json_path):
    hits, lines = scan(src)
    findings = load_findings(json_path)

    moved, gone = check_drift(findings, lines)

    # Relocate the audited entries onto the file as it is now, so a concurrent edit that only
    # shifted line numbers does not turn every verdict into a false gap.
    reloc = {id(ent): nl for ent, nl in moved}
    current = []
    for ent in findings:
        ent2 = dict(ent)
        if id(ent) in reloc:
            delta = reloc[id(ent)] - ent["line"]
            ent2["line"] = reloc[id(ent)]
            if "end_line" in ent2:
                ent2["end_line"] = ent2["end_line"] + delta
        current.append(ent2)

    unjudged, exempt, code = [], [], []
    for ln, name, text in hits:
        if entry_for(ln, current, lines) is not None:
            continue
        if is_comment(text):
            if exempt_reason(text):
                exempt.append((ln, name, text))
            else:
                unjudged.append((ln, name, text))
        else:
            code.append((ln, name, text))

    by_pattern = {}
    for _, name, _ in hits:
        by_pattern[name] = by_pattern.get(name, 0) + 1

    return {
        "src": src, "json": json_path, "lines_total": len(lines),
        "hits": hits, "hits_total": len(hits), "by_pattern": by_pattern,
        "audited": len(findings),
        "by_verdict": {v: sum(1 for e in findings if e["verdict"] == v) for v in VERDICTS},
        "accidentally_right": sum(1 for e in findings if e.get("accidentally_right")),
        "unjudged": unjudged, "exempt": exempt, "code": code,
        "unused_exempt": sorted(frag for frag in NARRATIVE_EXEMPT
                                if not any(frag in norm(t) for _, _, t in hits)),
        "current": current,
        "moved": moved, "gone": gone,
    }


def render(rep, verbose=False):
    out = []
    out.append("source   : %s (%d lines)" % (rep["src"], rep["lines_total"]))
    out.append("findings : %s (%d audited)" % (rep["json"], rep["audited"]))
    out.append("")
    out.append("pattern                                    hits")
    out.append("-----------------------------------------------")
    for entry in PATTERNS:
        out.append("%-42s %4d" % (entry[0], rep["by_pattern"].get(entry[0], 0)))
    out.append("%-42s %4d" % ("TOTAL lines matching any pattern", rep["hits_total"]))
    out.append("")
    out.append("verdicts : void %d / rewrite %d / holds %d / unverified %d"
               % (rep["by_verdict"]["void"], rep["by_verdict"]["rewrite"],
                  rep["by_verdict"]["holds"], rep["by_verdict"]["unverified"]))
    out.append("           of which flagged 'accidentally right': %d"
               % rep["accidentally_right"])
    out.append("")
    out.append("comment hits: %d audited / %d exempt-with-reason / %d UNJUDGED"
               % (rep["audited"], len(rep["exempt"]), len(rep["unjudged"])))
    named = sum(1 for _, _, t in rep["code"] if code_claim(t))
    out.append("code hits   : %d (values and stores; %d of them named in CODE_CLAIMS)"
               % (len(rep["code"]), named))
    out.append("")
    if rep["unjudged"]:
        out.append("UNJUDGED comment hits (%d) -- asserts something, not in the JSON,"
                   " not exempt:" % len(rep["unjudged"]))
        for ln, name, text in rep["unjudged"]:
            out.append("  %5d  %-12s %s" % (ln, name, norm(text)[:92].strip()))
    else:
        out.append("UNJUDGED comment hits: none")
    out.append("")
    if rep["unused_exempt"]:
        out.append("UNUSED EXEMPTIONS (%d) -- no hit matches these any more, drop them:"
                   % len(rep["unused_exempt"]))
        for frag in rep["unused_exempt"]:
            out.append("  %r" % frag)
        out.append("")
    if rep["moved"]:
        out.append("LINES MOVED (%d) -- same text, new line number (concurrent edit;"
                   " verdicts unchanged):" % len(rep["moved"]))
        for ent, nl in rep["moved"]:
            out.append("  %5d -> %5d" % (ent["line"], nl))
        out.append("")
    if rep["gone"]:
        out.append("SNIPPETS GONE (%d) -- the audit no longer describes this file,"
                   " re-check these by hand:" % len(rep["gone"]))
        for ent in rep["gone"]:
            out.append("  %5d  %r" % (ent["line"], norm(ent["snippet"])[:70]))
    elif not rep["moved"]:
        out.append("SNIPPETS: all %d still at their audited line" % rep["audited"])
    if verbose:
        out.append("")
        out.append("every hit, classified:")
        for ln, name, text in rep["hits"]:
            if entry_for(ln, []) is not None:
                continue
            if is_comment(text):
                cls = "exempt" if ln in NARRATIVE_EXEMPT else (
                    "audit" if ln not in [u[0] for u in rep["unjudged"]] else "UNJUDGED")
            else:
                cls = "code"
            out.append("  %5d  %-12s %-9s %s" % (ln, name, cls, norm(text)[:84].strip()))
    return "\n".join(out)


# --------------------------------------------------------------------- selftest

FIXTURE_SRC = """\
#define kJobIntRaw 0x1000
#define MALI_TOPCKGEN_PHYS      0x10000000ULL
#define MALI_CLK_UPDATE         0x0004u
	kAsCommand = 0x18, kAsStatus = 0x28,
/*
 * AS0 is handed an AArch64 table and its UPDATE raises MMU_BUS_ERROR(0);
 */
/*
 * mmu irq 0x10000 -- a walk the bus refused, while fault status stayed zero.
 */
/*
 * parked, mmu irq 0x100000
 */
	mwr(asReg(as, kAsCommand), cmd);
printf("mali: mmu irq %#x (page faults %#x, request completions %#x)\\n", 0, 0, 0);
/*
 * some other claim about a walk nobody has judged yet
 */
"""

FIXTURE_JSON = [
    {"line": 6, "snippet": "AS0 is handed an AArch64 table and its UPDATE raises MMU_BUS_ERROR(0);",
     "premise": "0x10000 = BUS_ERROR", "verdict": "void", "fix": "delete"},
    {"line": 9, "snippet": "mmu irq 0x10000 -- a walk the bus refused, while fault status stayed zero.",
     "premise": "0x10000 = BUS_ERROR", "verdict": "void", "fix": "delete"},
    {"line": 12, "snippet": "parked, mmu irq 0x100000",
     "premise": "AS4 latch is silent", "verdict": "void", "fix": "bit20 is AS4 REQ_COMPLETE"},
]


def selftest():
    checks = []

    def ok(name, cond, detail=""):
        checks.append((name, bool(cond), detail))

    with tempfile.TemporaryDirectory() as td:
        src = os.path.join(td, "fixture.c")
        with open(src, "w", encoding="utf-8") as fh:
            fh.write(FIXTURE_SRC)
        jsp = os.path.join(td, "fixture.json")
        with open(jsp, "w", encoding="utf-8") as fh:
            json.dump(FIXTURE_JSON, fh)

        hits, lines = scan(src)
        rep = audit(src, jsp)

        # 1. The lookahead. Line 2 carries 0x10000000ULL and line 12 carries 0x100000; only
        #    line 9 holds a real 0x10000. Confusing 0x100000 with 0x10000 is the exact mistake
        #    that once put AS4's REQ_COMPLETE (bit20) next to AS0's (bit16) in one argument.
        got = [ln for ln, n, _ in hits if n == "mmu-0x10000"]
        ok("regex: 0x10000 matches bit16 only, not 0x10000000 or 0x100000", got == [9], repr(got))

        # 2. The same probe as a negative, so a pattern that over-matches cannot pass both.
        ok("regex: MALI_TOPCKGEN_PHYS line is not reported at all", 2 not in [ln for ln, _, _ in hits],
           repr([ln for ln, _, _ in hits if ln == 2]))

        # 3. Comment decoration must not defeat snippet matching.
        ok("normalize: leading ' * ' stripped before comparison",
           norm(" * AS0 is handed") == "AS0 is handed", repr(norm(" * AS0 is handed")))

        # 4. Comments are claims and get audited; stores and printf are code and are counted.
        claim = next(l for l in lines if "AS0 is handed" in l)
        store = next(l for l in lines if "kAsCommand" in l and "mwr" in l)
        ok("classify: comment is a claim, mwr() is code",
           is_comment(claim) and not is_comment(store))

        # 5. Audited snippets must still be where the JSON says they are.
        ok("drift: clean source reports nothing", not rep["moved"] and not rep["gone"],
           repr(([e["line"] for e, _ in rep["moved"]], [e["line"] for e in rep["gone"]])))

        gone = [dict(FIXTURE_JSON[0], snippet="text that is not in the file")]
        m, g = check_drift(gone, lines)
        ok("drift: a vanished snippet is reported as gone", not m and len(g) == 1)

        # A concurrent edit that shifts every line by one must be reported as moved to the
        # right place, not as an error: the verdicts still hold, only the numbers changed.
        shifted = [""] + lines
        m, g = check_drift(FIXTURE_JSON, shifted)
        ok("drift: a one-line shift is re-located, not lost",
           not g and [nl for _, nl in m] == [7, 10, 13], repr([nl for _, nl in m]))

        # 6. end_line covers a multi-line block and stops at its end.
        ranged = [dict(FIXTURE_JSON[1], line=9, end_line=13)]
        ok("coverage: end_line covers inner lines and stops at the end",
           entry_for(12, ranged) is not None and entry_for(14, ranged) is None)

        # 7. An un-audited comment that asserts something is reported, not silently dropped.
        ok("gap: unjudged comment is reported",
           any(ln == 17 for ln, _, _ in rep["unjudged"]),
           repr([ln for ln, _, _ in rep["unjudged"]]))

        # 8. An exemption is keyed by a text fragment. A shifting edit must not be able to move
        #    someone's excuse onto a different claim, so key it by words, not by line number.
        saved = dict(NARRATIVE_EXEMPT)
        NARRATIVE_EXEMPT["some other claim about a walk"] = "selftest: excused on purpose"
        try:
            ok("gap: exempted comment is not reported",
               17 not in [ln for ln, _, _ in audit(src, jsp)["unjudged"]])
        finally:
            NARRATIVE_EXEMPT.clear()
            NARRATIVE_EXEMPT.update(saved)

        # 8b. An exemption matching nothing in the file is reported, so the list cannot rot.
        #     The dict is replaced wholesale here because the fixture is tiny: with the real
        #     list loaded, every real-driver phrase would look unused.
        saved = dict(NARRATIVE_EXEMPT)
        NARRATIVE_EXEMPT.clear()
        NARRATIVE_EXEMPT["some other claim about a walk"] = "selftest: live"
        NARRATIVE_EXEMPT["a phrase that appears nowhere"] = "selftest: rot"
        try:
            rot = audit(src, jsp)["unused_exempt"]
            ok("gap: unused exemption reported", rot == ["a phrase that appears nowhere"], repr(rot))
        finally:
            NARRATIVE_EXEMPT.clear()
            NARRATIVE_EXEMPT.update(saved)



        # 9. Code hits are never reported as unjudged claims.
        ok("gap: code hits stay out of the unjudged list",
           all(not is_comment(t) for _, _, t in rep["code"]) and
           not any(ln == 14 for ln, _, _ in rep["unjudged"]))

        # 9b. CODE_CLAIMS keys are text fragments that must still match a code hit; a fragment
        #     matching nothing means the list has rotted and the report overstates its coverage.
        saved3 = dict(CODE_CLAIMS)
        CODE_CLAIMS.clear()
        CODE_CLAIMS["mwr(asReg(as, kAsCommand), cmd)"] = "selftest: live"
        try:
            named = [ln for ln, _, t in audit(src, jsp)["code"] if code_claim(t)]
            ok("code claims: fragment matches a code hit", named == [14], repr(named))
        finally:
            CODE_CLAIMS.clear()
            CODE_CLAIMS.update(saved3)

        # 10. Verdict vocabulary is enforced; an invented verdict must raise, not be counted.
        bad = os.path.join(td, "bad.json")
        with open(bad, "w", encoding="utf-8") as fh:
            json.dump([dict(FIXTURE_JSON[0], verdict="bogus")], fh)
        try:
            load_findings(bad)
            rejected = False
        except ValueError:
            rejected = True
        ok("schema: unknown verdict rejected", rejected)

        # 11. Required keys are enforced.
        missing = os.path.join(td, "missing.json")
        with open(missing, "w", encoding="utf-8") as fh:
            json.dump([{"line": 1, "snippet": "x", "verdict": "void"}], fh)
        try:
            load_findings(missing)
            rejected = False
        except ValueError:
            rejected = True
        ok("schema: missing required key rejected", rejected)

    passed = sum(1 for _, cond, _ in checks if cond)
    for name, cond, detail in checks:
        print("%s  %s%s" % ("PASS" if cond else "FAIL", name,
                            ("   [" + detail + "]") if detail and not cond else ""))
    print("")
    print("selftest: %d/%d passed" % (passed, len(checks)))
    return 0 if passed == len(checks) else 1


def main(argv=None):
    ap = argparse.ArgumentParser(description="audit the refuted AS0-walk reasoning chain")
    ap.add_argument("--src", default=DEFAULT_SRC)
    ap.add_argument("--json", dest="json_path", default=DEFAULT_JSON)
    ap.add_argument("--selftest", action="store_true")
    ap.add_argument("--list-hits", action="store_true")
    ap.add_argument("--json-out", action="store_true")
    args = ap.parse_args(argv)

    if args.selftest:
        return selftest()

    try:
        rep = audit(args.src, args.json_path)
    except (OSError, ValueError) as exc:
        print("mmu-audit: %s" % exc, file=sys.stderr)
        return 2

    if args.json_out:
        payload = {k: v for k, v in rep.items() if k not in ("hits", "unjudged", "exempt", "code")}
        payload["moved"] = [{"from": e["line"], "to": nl} for e, nl in rep["moved"]]
        payload["gone"] = [e["line"] for e in rep["gone"]]
        payload["hits"] = [{"line": ln, "pattern": n, "text": norm(t)} for ln, n, t in rep["hits"]]
        payload["unjudged"] = [{"line": ln, "pattern": n, "text": norm(t)}
                               for ln, n, t in rep["unjudged"]]
        print(json.dumps(payload, ensure_ascii=False, indent=2))
    else:
        print(render(rep, verbose=args.list_hits))

    return 1 if (rep["gone"] or rep["unjudged"] or rep["unused_exempt"]) else 0


if __name__ == "__main__":
    sys.exit(main())
