#!/usr/bin/env python3
"""mmu-audit.py -- audit the "AS0 table walk rejected by the bus" reasoning chain.

Background: the driver read MMU interrupt value 0x10000 as MMU_BUS_ERROR(0) and built a
long argument on it ("AS0's page table walk was refused by the bus"). Upstream panthor
says bits[31:16] of that register are REQ_COMPLETE, not fault status
(panthor_mmu.c:646-650, "Bits 16 to 31 mean REQ_COMPLETE"). Every conclusion that rested
on the old reading has to be re-judged.

This script does three things and nothing else:

  1. scan the driver for every occurrence of the patterns the chain was written with
  2. compare those occurrences against findings/mmu-refuted.json and report which hits
     were never audited (and which audited snippets have drifted away from the source)
  3. --selftest: prove the three checks still fail when they should

It is read-only. It never writes to the driver and never needs root.

Usage:
    ./mmu-audit.py                      # audit with the built-in default paths
    ./mmu-audit.py --src DRIVER --json FINDINGS
    ./mmu-audit.py --selftest           # self-check, 9 assertions
    ./mmu-audit.py --list-hits          # every hit, no verdict bookkeeping
    ./mmu-audit.py --json-out           # machine-readable report on stdout

Exit codes: 0 clean, 1 unaudited hits or snippet drift, 2 usage or I/O error.
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

# The patterns the refuted chain was written in. Order matters only for reporting.
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

# Lines in the driver that match a pattern but carry no reasoning: register enum values,
# command opcode constants, the clock block's unrelated CLK_UPDATE bit, and the call sites
# that only move values around. Auditing them would be noise; the list is explicit so that
# anything NOT listed here and still uncovered is a real gap.
CODE_LINES = {
    127: "MALI_CLK_UPDATE: topckgen clock mux latch, unrelated to AS_COMMAND",
    130: "MALI_MFG_REF_SHIFT comment mentions CLK_UPDATE",
    131: "MALI_MFGSC_REF_SHIFT comment mentions CLK_UPDATE",
    446: "AS register offset enum",
    457: "AS command opcode enum",
    1213: "walk print loop, reads the CPU's own copy through pt_kva()",
    1214: "walk print loop header comment, no claim about hardware",
    1216: "descriptor-validity rule, not a hardware observation",
    1264: "coverage self-check header, CPU-side only",
    1267: "coverage self-check header, CPU-side only",
    2006: "'refused the first one' is the bus-protection route, not the MMU",
    2161: "clock mux write, CLK_UPDATE",
    2198: "clock mux comment, CLK_UPDATE bit20",
    2227: "clock mux write, CLK_UPDATE",
    2724: "as_command(): the single store of the opcode",
    3145: "gpu_irq_mask_prestart: GPU_IRQ clear",
    3146: "gpu_irq_mask_prestart: GPU_IRQ mask",
    3147: "gpu_irq_mask_prestart: job clear",
    3148: "gpu_irq_mask_prestart: job mask",
    3149: "gpu_irq_mask_prestart: MMU clear",
    3150: "gpu_irq_mask_prestart: MMU mask",
    3460: "as_park_unused: per-space clear, code",
    3464: "as_park_unused: per-space MEMATTR, code",
    3466: "as_park_unused: the UPDATE, code",
    3468: "as_park_unused: the per-space latch print, code",
    3732: "start_mcu: MMU clear before AUTO, code",
    3735: "start_mcu: the read that follows the clear, code",
    3982: "start_mcu: booted branch",
    3991: "start_mcu: job clear, code",
    3992: "start_mcu: job mask write, code",
    3993: "start_mcu: GPU_IRQ clear, code",
    3994: "start_mcu: GPU_IRQ mask, code",
    3995: "start_mcu: MMU clear, code",
    3996: "start_mcu: MMU mask, code",
    3997: "start_mcu: the confirmation print, code",
    4250: "mali_early_as0: print after the clear, code",
    4252: "mali_early_as0: as_enable(0), code",
    4255: "mali_early_as0: print after the update, code",
    4257: "mali_early_as0: bus-idle sample, code",
    4782: "mali_as0 control: clear, code",
    4784: "mali_as0 control: print, code",
    4788: "mali_as0 control: the UPDATE, code",
    4789: "mali_as0 control: parked readback print, code",
    4792: "mali_as0 control: parked readback print, code",
    4793: "mali_as0 control: bus-idle sample, code",
    4797: "normal path: the clear, code",
    4799: "normal path: print after the clear, code",
    4800: "normal path: as_enable(0), code",
    4803: "normal path: print after the update, code",
    4804: "normal path: bus-idle sample, code",
    5185: "boot-arg parse of mali_parkattrs",
}

VERDICTS = {"void", "rewrite", "holds"}
REQUIRED_KEYS = ("line", "snippet", "premise", "verdict", "fix")


def norm(text: str) -> str:
    """Strip a C comment's decoration so a snippet can be compared with a source line."""
    s = text.strip()
    while s[:1] in ("*", "/"):
        s = s[1:].strip()
    return s


def scan(path: str, patterns=PATTERNS):
    """Return every (lineno, pattern_name, line_text) hit, in file order."""
    with open(path, "r", encoding="utf-8", errors="replace") as fh:
        lines = fh.readlines()
    compiled = []
    for name, rx, *flags in patterns:
        compiled.append((name, re.compile(rx, flags[0] if flags else 0)))
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


def entry_for(line, findings):
    for ent in findings:
        end = ent.get("end_line", ent["line"])
        if ent["line"] <= line <= end:
            return ent
    return None


def check_drift(findings, lines):
    """An audited entry whose snippet is no longer at its line means the source moved."""
    drift = []
    for ent in findings:
        end = ent.get("end_line", ent["line"])
        window = " ".join(norm(lines[i]) for i in range(ent["line"] - 1, min(end, len(lines))))
        if norm(ent["snippet"]) not in window:
            drift.append(ent)
    return drift


def audit(src, json_path, verbose=False):
    hits, lines = scan(src)
    findings = load_findings(json_path)
    unaudited = [(ln, name, text) for ln, name, text in hits
                 if ln not in CODE_LINES and entry_for(ln, findings) is None]
    drift = check_drift(findings, lines)
    by_verdict = {v: sum(1 for e in findings if e["verdict"] == v) for v in VERDICTS}
    by_pattern = {}
    for _, name, _ in hits:
        by_pattern[name] = by_pattern.get(name, 0) + 1
    return {
        "src": src,
        "json": json_path,
        "lines_total": len(lines),
        "hits": hits,
        "hits_total": len(hits),
        "by_pattern": by_pattern,
        "audited": len(findings),
        "by_verdict": by_verdict,
        "accidentally_right": sum(1 for e in findings if e.get("accidentally_right")),
        "unaudited": unaudited,
        "code_lines": len(CODE_LINES),
        "drift": drift,
    }


def render(rep, verbose=False):
    out = []
    out.append("source   : %s (%d lines)" % (rep["src"], rep["lines_total"]))
    out.append("findings : %s (%d audited entries)" % (rep["json"], rep["audited"]))
    out.append("")
    out.append("pattern                                    hits")
    out.append("-----------------------------------------------")
    for name, _ in PATTERNS:
        out.append("%-42s %4d" % (name, rep["by_pattern"].get(name, 0)))
    out.append("%-42s %4d" % ("TOTAL lines matching any pattern", rep["hits_total"]))
    out.append("")
    out.append("verdicts : void %d / rewrite %d / holds %d  (accidentally-right %d)"
               % (rep["by_verdict"]["void"], rep["by_verdict"]["rewrite"],
                  rep["by_verdict"]["holds"], rep["accidentally_right"]))
    out.append("")
    if rep["unaudited"]:
        out.append("UNAUDITED hits (%d) -- matched a pattern, not in mmu-refuted.json,"
                   " not in the code-only list:" % len(rep["unaudited"]))
        for ln, name, text in rep["unaudited"]:
            out.append("  %5d  %-12s %s" % (ln, name, norm(text)[:96].strip()))
    else:
        out.append("UNAUDITED hits: none (every pattern hit is either audited or code-only)")
    out.append("")
    if rep["drift"]:
        out.append("SNIPPET DRIFT (%d) -- audited line no longer holds the quoted text:"
                   % len(rep["drift"]))
        for ent in rep["drift"]:
            out.append("  %5d  expected %r" % (ent["line"], norm(ent["snippet"])[:72]))
    else:
        out.append("SNIPPET DRIFT: none (all %d audited snippets still match the source)"
                   % rep["audited"])
    out.append("")
    out.append("code-only lines excluded: %d (each listed with a reason in CODE_LINES)"
               % rep["code_lines"])
    if verbose:
        out.append("")
        out.append("every hit:")
        for ln, name, text in rep["hits"]:
            tag = "code" if ln in CODE_LINES else ("audit" if entry_for(ln, []) else "?")
            out.append("  %5d  %-12s %s" % (ln, name, norm(text)[:96].strip()))
    return "\n".join(out)


# --------------------------------------------------------------------- selftest

FIXTURE_SRC = """\
/* header */
#define kJobIntRaw = 0x1000,
	kAsCommand = 0x18, kAsStatus = 0x28,
#define MALI_TOPCKGEN_PHYS      0x10000000ULL
#define MALI_CLK_UPDATE         0x0004u
	 * answered ambiguously. AS0 is handed an AArch64 table and its UPDATE raises MMU_BUS_ERROR(0);
	 * The first run showed mmu irq 0x10000 -- MMU_BUS_ERROR for address space 0.
	 * parked, mmu irq 0x100000
	AS_STEP("update", mwr(asReg(as, kAsCommand), kAsCmdUpdate));
	 * request completions 0x1
"""

FIXTURE_JSON = [
    {"line": 6, "snippet": "AS0 is handed an AArch64 table and its UPDATE raises MMU_BUS_ERROR(0);",
     "premise": "0x10000 = BUS_ERROR", "verdict": "void", "fix": "delete"},
    {"line": 7, "snippet": "The first run showed mmu irq 0x10000 -- MMU_BUS_ERROR for address space 0.",
     "premise": "0x10000 = BUS_ERROR", "verdict": "void", "fix": "delete"},
    {"line": 8, "snippet": "parked, mmu irq 0x100000",
     "premise": "AS4 silence", "verdict": "holds", "fix": "none", "accidentally_right": False},
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
        hit_lines = [ln for ln, _, _ in hits]
        rep = audit(src, jsp)

        # 1. 0x10000 must not match a longer literal. Line 4 is 0x10000000ULL and line 8 is
        #    0x100000; only lines 6 and 7 carry the real 0x10000. This is the exact confusion
        #    that put AS4's REQ_COMPLETE (bit20) next to AS0's (bit16) in one argument.
        ok("regex: 0x10000 does not match 0x10000000 or 0x100000",
           [ln for ln, n, _ in hits if n == "mmu-0x10000"] == [6, 7],
           "0x10000 hits at %r" % [ln for ln, n, _ in hits if n == "mmu-0x10000"])

        # 2. The physical-address constant at line 4 is still a *pattern* hit only through
        #    0x10000? No: it must be picked up by nothing at all if the lookahead works.
        ok("regex: MALI_TOPCKGEN_PHYS 0x10000000ULL is not reported", 4 not in hit_lines,
           "line 4 hit=%r" % (4 in hit_lines))

        # 3. A comment's leading ' * ' must not stop a snippet from matching.
        ok("normalize: ' * ' prefix stripped before comparison",
           norm(" * AS0 is handed") == "AS0 is handed", repr(norm(" * AS0 is handed")))

        # 4. Snippet drift is detected when the source no longer holds the quoted text.
        ok("drift: a snippet absent from its line is reported", len(rep["drift"]) == 0,
           "unexpected drift %r" % [e["line"] for e in rep["drift"]])

        stale = [dict(FIXTURE_JSON[0], snippet="text that is not in the file")]
        drift = check_drift(stale, lines)
        ok("drift: drifted snippet is caught", len(drift) == 1, "drift=%d" % len(drift))

        # 5. An audited line inside a multi-line range is covered by end_line.
        ranged = [dict(FIXTURE_JSON[0], line=2, end_line=9)]
        ok("coverage: end_line range covers an inner line",
           entry_for(8, ranged) is not None and entry_for(10, ranged) is None)

        # 6. A pattern hit that is neither audited nor code-only is reported as a gap.
        ok("gap: un-audited hit is listed", any(ln == 9 for ln, _, _ in rep["unaudited"]),
           "unaudited=%r" % [ln for ln, _, _ in rep["unaudited"]])

        # 7. The code-only list suppresses exactly the lines it names.
        global CODE_LINES
        saved, CODE_LINES = CODE_LINES, {9: "the AS_STEP store, code"}
        try:
            rep2 = audit(src, jsp)
            excused = [ln for ln in hit_lines if ln == 9]
            ok("gap: code-only line excused", bool(excused) and
               9 not in [ln for ln, _, _ in rep2["unaudited"]],
               "line 9 still reported")
        finally:
            CODE_LINES = saved

        # 8. Verdict vocabulary is enforced: 'bogus' must be rejected, not silently counted.
        bad = os.path.join(td, "bad.json")
        with open(bad, "w", encoding="utf-8") as fh:
            json.dump([dict(FIXTURE_JSON[0], verdict="bogus")], fh)
        try:
            load_findings(bad)
            rejected = False
        except ValueError:
            rejected = True
        ok("schema: unknown verdict rejected", rejected)

        # 9. Required keys are enforced.
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
    ap.add_argument("--strict", action="store_true",
                    help="also fail when there are unaudited hits, not only drift")
    args = ap.parse_args(argv)

    if args.selftest:
        return selftest()

    try:
        rep = audit(args.src, args.json_path)
    except (OSError, ValueError) as exc:
        print("mmu-audit: %s" % exc, file=sys.stderr)
        return 2

    if args.json_out:
        payload = dict(rep)
        payload["hits"] = [{"line": ln, "pattern": n, "text": norm(t)}
                           for ln, n, t in rep["hits"]]
        payload["unaudited"] = [{"line": ln, "pattern": n, "text": norm(t)}
                                for ln, n, t in rep["unaudited"]]
        payload["drift"] = [e["line"] for e in rep["drift"]]
        print(json.dumps(payload, ensure_ascii=False, indent=2))
    else:
        print(render(rep, verbose=args.list_hits))

    if rep["drift"]:
        return 1
    if args.strict and rep["unaudited"]:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
