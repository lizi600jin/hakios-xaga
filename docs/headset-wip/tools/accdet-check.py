#!/usr/bin/env python3
"""Judge a headset=1 ACCDET dump against the three criteria fixed on 2026-11-15.

Three criteria, all three must pass together:
  shape      distinct >= 12, 0xFF count <= 12, 0x00 count <= 96
  structure  seven mirror register pairs must be equal
  anchor     0x231d must read 0x06 for the baseline to be comparable

The point of the shape test: a dead all-0x00 bus scores 78/130 = 60.0% against the
stock baseline (the baseline itself holds 74 zeroes), so "agreement rate" would pass
a dead bus. These three together do not.

0x231d (AUD_TOP_INT_CON0) is NOT a liveness or reset anchor. Its 0x06 in stock Android
comes from the MFD IRQ layer enabling bit1 (ACCDET) and bit2 (EINT0) when those interrupts
are requested (mtk-spmi-pmiс.c irq_sync_unlock). Nothing in the accdet driver writes it,
and our init does not either. A 0x00 here means "no top-level IRQ enable", not "chip was
reset", and 0xFF is not the only failure signature either.

Therefore this tool no longer scores the anchor. Judge insertion by cur_AB (0x26b6)
moving off 3 under the polling path, and treat the whole-address-map shape as the only
liveness check that has source support.

Usage:
  python3 accdet-check.py <dump.json>            # judge
  python3 accdet-check.py --selftest             # run the built-in cases
"""
import json
import sys


# RETRACTED 2026-11-15: the "seven mirror pairs" criterion was wrong and is removed.
#
# mt6368.h numbers 0x2695..0x269f as ACCDET_CON13..CON23, two bytes per independent
# configuration field. 0x2697 and 0x2699 sit one register apart (0x2698 = CON16) and hold
# separate debounce fields, not the same storage. They only read equal because the
# initialisation happens to program them to the same value.
#
# Evidence: mt6368.h:448-458; review at
# /home/lizi/IOS/hakios-mtk/logs/headset-post700-review-20261115/REVIEW.md section 4.
#
# The list is kept empty so the structure test reports "no pairs configured" instead of
# silently passing. Do not re-add pairs without a source showing they are the same storage.
MIRROR_PAIRS = []

ANCHOR = 0x231D
ANCHOR_STOCK = 0x06

SHAPE_DISTINCT_MIN = 12
SHAPE_FF_MAX = 12
SHAPE_ZERO_MAX = 96


def judge(regs):
    """regs: {int address: int value}. Returns a dict of results."""
    values = list(regs.values())
    distinct = len(set(values))
    ff = sum(1 for v in values if v == 0xFF)
    zero = sum(1 for v in values if v == 0x00)

    shape = {
        "entries": len(values),
        "distinct": distinct,
        "ff_count": ff,
        "zero_count": zero,
        "distinct_ok": distinct >= SHAPE_DISTINCT_MIN,
        "ff_ok": ff <= SHAPE_FF_MAX,
        "zero_ok": zero <= SHAPE_ZERO_MAX,
    }
    shape["pass"] = shape["distinct_ok"] and shape["ff_ok"] and shape["zero_ok"]

    pairs = []
    for lo, hi in MIRROR_PAIRS:
        if lo in regs and hi in regs:
            pairs.append({
                "lo": f"0x{lo:04x}", "hi": f"0x{hi:04x}",
                "lo_value": f"0x{regs[lo]:02x}", "hi_value": f"0x{regs[hi]:02x}",
                "equal": regs[lo] == regs[hi],
            })
    structure = {
        "pairs_checked": len(pairs),
        "pairs_equal": sum(1 for p in pairs if p["equal"]),
        "missing": len(MIRROR_PAIRS) - len(pairs),
        "detail": pairs,
        "note": ("no mirror pairs are configured; the old seven-pair criterion was "
                 "retracted (see the comment above MIRROR_PAIRS)"),
        "pass": None,
    }

    # 0x231d is recorded for information only. It is an IRQ-enable register filled in by the
    # MFD layer, not a liveness or reset anchor, so it does not gate the verdict.
    if ANCHOR in regs:
        v = regs[ANCHOR]
        anchor = {
            "value": f"0x{v:02x}",
            "scored": False,
            "note": ("AUD_TOP_INT_CON0: bit1 ACCDET + bit2 EINT0 enable. Stock reads 0x06 "
                     "because the MFD IRQ layer requested both. Our init does not write it. "
                     "0x00 means the top-level IRQs are not enabled, not that the chip was "
                     "reset."),
        }
    else:
        anchor = {"present": False, "scored": False, "note": "register absent"}

    return {
        "shape": shape,
        "structure": structure,
        "anchor": anchor,
        "all_pass": shape["pass"],   # the only criterion with source support
        "criteria_in_force": ["shape"],
        "retracted_criteria": ["seven mirror pairs (they are independent CON registers)",
                               "anchor 0x231d == 0x06"],
    }


def load(path):
    d = json.load(open(path))
    return {int(k, 16): int(v, 16) for k, v in d["registers"].items()}


def selftest():
    ok = True

    def case(name, regs, want):
        nonlocal ok
        r = judge(regs)
        got = r["all_pass"]
        good = got == want
        ok = ok and good
        print(f"  [{'PASS' if good else 'FAIL'}] {name}: all_pass={got} (wanted {want})")
        if not good:
            print(json.dumps(r, indent=2))

    good = {a: (a & 0xFF) for a in range(0x2680, 0x26D4)}
    good[ANCHOR] = ANCHOR_STOCK
    case("a healthy dump", good, True)

    dead = {a: 0 for a in range(0x2680, 0x26D4)}
    dead[ANCHOR] = ANCHOR_STOCK
    case("all-zero dead bus", dead, False)

    noanswer = {a: 0xFF for a in range(0x2680, 0x26D4)}
    noanswer[ANCHOR] = 0xFF
    case("all-0xFF no answer", noanswer, False)

    varied = {a: (a * 7) & 0xFF for a in range(0x2680, 0x26D4)}
    varied[ANCHOR] = 0x00
    case("varied values, anchor 0x00 (must still pass: anchor is not scored)", varied, True)

    print("selftest:", "PASS" if ok else "FAIL")
    return 0 if ok else 1


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    if sys.argv[1] == "--selftest":
        return selftest()
    regs = load(sys.argv[1])
    r = judge(regs)
    print(json.dumps(r, indent=2))
    return 0 if r["all_pass"] else 1


if __name__ == "__main__":
    sys.exit(main())
