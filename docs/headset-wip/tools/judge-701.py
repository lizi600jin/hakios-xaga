#!/usr/bin/env python3
"""Judge a trial 701 log (headset=2, headphone plugged in before boot).

The criteria were fixed before the run, in
/home/lizi/IOS/hakios-mtk/logs/headset-post700-review-20261115/TRIAL-701-CRITERIA.md

The chain is C -> B, with A as supporting state evidence only:

  A  state response   a sample line reporting AB 0 or 1 (700 read AB=3 with nothing plugged)
  C  insertion accepted   "headset: EINT0 plug in; awaiting ACCDET classification"
  B  type classified  "headset: cable headset with mic (AB 1, detector 1)"      (four-pole)
                      "headset: cable headphone without mic (AB 0, detector 2)" (three-pole)

A pass needs C, a B whose type matches the physical headphone, a visible
"mode 2 initialised", and no failure/rollback afterwards.

Deliberate non-criteria, all of which would cause false failures:
  - AB jumping 3 -> 0/1 within one run. The headphone is plugged before boot, so the
    first sample can already read 0/1. 699/700 are the unplugged references.
  - sync=1 in the first sample. The sample prints before the EINT is handled.
  - IRQ!=0 in the first sample. first_sample && EINT0==0 has a startup_plug path.
  - a sample line showing IRQ bit0 or bit2. Only AB/EINT0/FSM changes trigger a print.
  - a fixed A->C or C->A order, or C after "mode 2 initialised". C can come first.
  - "exactly every 50 ms" or "must classify within 6 s".
  - repeated samples. No change means no print; that is not the poll stopping.

Usage:
  python3 judge-701.py <log> --type 3pole|4pole [--json]
  python3 judge-701.py --selftest
"""
import json
import re
import sys

C_LINE = "headset: EINT0 plug in; awaiting ACCDET classification"
B_WITH_MIC = "headset: cable headset with mic"
B_NO_MIC = "headset: cable headphone without mic"
INIT_OK = "headset: mode 2 initialised"

FAILURE_PATTERNS = [
    ("polling failed", "headset: polling failed"),
    ("initialisation failed", "headset: initialisation failed"),
    ("configuration restore", "restoring configuration"),
    ("restore failed", "headset: restore failed"),
    ("PMIF not idle", "headset: PMIF not idle"),
    ("IRQ clear timeout", "headset: IRQ clear timeout"),
    ("verify failed", "headset: verify failed"),
    ("unexpected EINT0 state", "headset: unexpected EINT0 state"),
]

SAMPLE_RE = re.compile(r"headset: sample IRQ (0x[0-9a-fA-F]+) AB (\d) EINT0 (\d) FSM (\d) sync (\d)")
TS_RE = re.compile(r"^\[ *([0-9]+\.[0-9]+)\]")


def lines_of(path):
    out = []
    with open(path, "r", errors="replace") as f:
        for i, raw in enumerate(f, 1):
            m = TS_RE.match(raw)
            out.append((i, float(m.group(1)) if m else None, raw.rstrip("\n")))
    return out


def judge(rows, headphone_type):
    """rows: [(line_no, timestamp, text)]. headphone_type: '3pole' or '4pole'.

    Returns (verdict_dict, exit_ok).
    """
    samples = []
    c_lines, b_mic, b_nomic, init_ok = [], [], [], []
    failures = []

    for no, ts, text in rows:
        m = SAMPLE_RE.search(text)
        if m:
            samples.append({
                "line": no, "ts": ts,
                "irq": m.group(1), "ab": int(m.group(2)),
                "eint0": int(m.group(3)), "fsm": int(m.group(4)),
                "sync": int(m.group(5)),
            })
        if C_LINE in text:
            c_lines.append({"line": no, "ts": ts})
        if B_WITH_MIC in text:
            ab = re.search(r"AB (\d), detector (\d)", text)
            b_mic.append({"line": no, "ts": ts,
                          "ab": int(ab.group(1)) if ab else None,
                          "detector": int(ab.group(2)) if ab else None})
        if B_NO_MIC in text:
            ab = re.search(r"AB (\d), detector (\d)", text)
            b_nomic.append({"line": no, "ts": ts,
                            "ab": int(ab.group(1)) if ab else None,
                            "detector": int(ab.group(2)) if ab else None})
        if INIT_OK in text:
            init_ok.append({"line": no, "ts": ts})
        for label, needle in FAILURE_PATTERNS:
            if needle in text:
                failures.append({"line": no, "ts": ts, "kind": label, "text": text.strip()})

    # A: supporting evidence only
    a_hits = [s for s in samples if s["ab"] in (0, 1)]

    # B: the type that matches the physical headphone
    expected = b_mic if headphone_type == "4pole" else b_nomic
    wrong = b_nomic if headphone_type == "4pole" else b_mic

    # failure/rollback after the point of acceptance (C) matters most
    c_ts = c_lines[0]["ts"] if c_lines else None
    failures_after_c = [f for f in failures
                        if c_ts is not None and f["ts"] is not None and f["ts"] >= c_ts]

    # an unexpected plug-out with the headphone left in
    plug_out = [r for r in rows if "headset: EINT0 plug out" in r[2]]

    passed = bool(c_lines and expected and init_ok and not failures_after_c and not plug_out)

    if not c_lines and not expected:
        verdict = "no insertion observed: neither C nor B appeared; AB stayed at 3"
    elif c_lines and not expected and not wrong:
        verdict = "C only: insertion accepted, classification not completed"
    elif wrong and not expected:
        verdict = ("B of the wrong type: classified as %s but the headphone is %s"
                   % ("3pole" if headphone_type == "4pole" else "4pole", headphone_type))
    elif passed:
        verdict = "PASS: insertion accepted (C) and classified (B) as %s" % headphone_type
    elif failures_after_c:
        verdict = "classified but a failure followed; this run does not pass"
    elif plug_out:
        verdict = "classified but an unexpected plug-out appeared while the headphone stayed in"
    else:
        verdict = "insufficient evidence"

    result = {
        "headphone_type_expected": headphone_type,
        "samples_seen": len(samples),
        "A_state_response": {
            "found": bool(a_hits),
            "detail": a_hits[:5],
            "role": "supporting only",
        },
        "C_insertion_accepted": {"found": bool(c_lines), "detail": c_lines[:3]},
        "B_classified": {
            "expected_kind": expected[:3] and ("with_mic" if headphone_type == "4pole" else "no_mic"),
            "found": bool(expected), "detail": expected[:3],
            "wrong_type_seen": wrong[:3],
        },
        "init_ok": {"found": bool(init_ok), "detail": init_ok[:2]},
        "failures": failures,
        "failures_after_C": failures_after_c,
        "unexpected_plug_out": plug_out[:3],
        "verdict": verdict,
        "passed": passed,
        "not_required": [
            "AB jump 3->0/1 within the run",
            "sync=1 in the first sample",
            "IRQ!=0 in the first sample",
            "a sample showing IRQ bit0/bit2",
            "fixed A->C or C->A order, or C after init",
            "exact 50 ms cadence or 6 s classification",
            "repeated samples while nothing changes",
        ],
    }
    return result, passed


def selftest():
    ok = True

    def rows(*texts):
        out = []
        for i, t in enumerate(texts, 1):
            mm = re.match(r"\[ *([0-9.]+)\]\s?(.*)", t)
            if mm:
                out.append((i, float(mm.group(1)), mm.group(2)))
            else:
                out.append((i, None, t))
        return out

    def case(name, r, want_type, want_pass):
        nonlocal ok
        res, got = judge(r, want_type)
        good = got == want_pass
        ok = ok and good
        print(f"  [{'PASS' if good else 'FAIL'}] {name}: passed={got} (wanted {want_pass})")
        if not good:
            print(json.dumps(res, indent=2))
        else:
            print(f"          verdict: {res['verdict']}")

    # a four-pole headphone plugged before boot, classified, nothing after
    case("4pole full pass", rows(
        "[20.0] headset: sample IRQ 0x00 AB 1 EINT0 0 FSM 0 sync 0",
        "[20.1] headset: EINT0 plug in; awaiting ACCDET classification",
        "[20.2] headset: mode 2 initialised, polling every 50 ms; PCM playback not implemented",
        "[20.3] headset: cable headset with mic (AB 1, detector 1)",
    ), "4pole", True)

    # C appeared but never classified
    case("C only", rows(
        "[20.0] headset: sample IRQ 0x00 AB 0 EINT0 0 FSM 0 sync 0",
        "[20.1] headset: EINT0 plug in; awaiting ACCDET classification",
        "[20.2] headset: mode 2 initialised, polling every 50 ms; PCM playback not implemented",
    ), "3pole", False)

    # classified, then a polling failure
    case("classified then polling failed", rows(
        "[20.1] headset: EINT0 plug in; awaiting ACCDET classification",
        "[20.2] headset: mode 2 initialised, polling every 50 ms; PCM playback not implemented",
        "[20.3] headset: cable headphone without mic (AB 0, detector 2)",
        "[21.0] headset: polling failed; restoring configuration and stopping",
    ), "3pole", False)

    # wrong type for the physical headphone
    case("wrong type", rows(
        "[20.1] headset: EINT0 plug in; awaiting ACCDET classification",
        "[20.2] headset: mode 2 initialised, polling every 50 ms; PCM playback not implemented",
        "[20.3] headset: cable headphone without mic (AB 0, detector 2)",
    ), "4pole", False)

    # nothing plugged: the 700 shape (AB stays 3, no C, no B)
    case("nothing plugged", rows(
        "[20.0] headset: sample IRQ 0x00 AB 3 EINT0 3 FSM 0 sync 0",
        "[20.1] headset: mode 2 initialised, polling every 50 ms; PCM playback not implemented",
    ), "3pole", False)

    print("selftest:", "PASS" if ok else "FAIL")
    return 0 if ok else 1


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    if sys.argv[1] == "--selftest":
        return selftest()
    path = sys.argv[1]
    htype = None
    as_json = "--json" in sys.argv[2:]
    if "--type" in sys.argv:
        htype = sys.argv[sys.argv.index("--type") + 1]
    if htype not in ("3pole", "4pole"):
        print("give --type 3pole or --type 4pole (which headphone was plugged in)")
        return 2
    res, ok = judge(lines_of(path), htype)
    print(json.dumps(res, indent=2) if as_json else json.dumps(res, indent=2))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
