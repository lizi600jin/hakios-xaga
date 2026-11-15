#!/usr/bin/env python3
"""Judge a headset=1 ACCDET dump against the three criteria fixed on 2026-11-15.

Three criteria, all three must pass together:
  shape      distinct >= 12, 0xFF count <= 12, 0x00 count <= 96
  structure  seven mirror register pairs must be equal
  anchor     0x231d must read 0x06 for the baseline to be comparable

The point of the shape test: a dead all-0x00 bus scores 78/130 = 60.0% against the
stock baseline (the baseline itself holds 74 zeroes), so "agreement rate" would pass
a dead bus. These three together do not.

0x231d values other than 0x06 are not automatically a channel failure:
  0x00  the chip was reset (baseline void, channel may be fine)
  0xFF  no answer from the slave (wrong SID, PMIF arbitration not up, MPU refusal)
  else  address alignment or bit order is wrong -- do not write any register yet

Usage:
  python3 accdet-check.py <dump.json>            # judge
  python3 accdet-check.py --selftest             # run the built-in cases
"""
import json
import sys


# The seven mirror pairs fixed in HEADSETLESS-TEST-PLAN.md: registers that are wired to
# the same storage, so a correct read returns the same byte twice. A mismatch means the
# read lost a byte or landed on the wrong address.
MIRROR_PAIRS = [
    (0x2697, 0x2699),
    (0x2699, 0x269B),
    (0x26A3, 0x26A4),
    (0x26C4, 0x26C6),
    (0x26C6, 0x26C8),
    (0x26C8, 0x26CA),
    (0x26C5, 0x26C7),
]

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
    }
    structure["pass"] = (structure["missing"] == 0
                         and structure["pairs_equal"] == len(MIRROR_PAIRS))

    if ANCHOR not in regs:
        anchor = {"present": False, "pass": False, "verdict": "anchor register absent"}
    else:
        v = regs[ANCHOR]
        if v == ANCHOR_STOCK:
            verdict = "channel up, chip still holds its pre-shutdown state; baseline comparable"
            ok = True
        elif v == 0x00:
            verdict = ("chip was reset: baseline void, but the channel is not necessarily broken "
                       "(0xFF is what a dead channel reads)")
            ok = False
        elif v == 0xFF:
            verdict = "no answer from the slave: wrong SID, PMIF arbitration not up, or MPU refusal"
            ok = False
        else:
            verdict = "unexpected value: address alignment or bit order wrong; write nothing yet"
            ok = False
        anchor = {"present": True, "value": f"0x{v:02x}",
                  "expected": f"0x{ANCHOR_STOCK:02x}", "pass": ok, "verdict": verdict}

    return {
        "shape": shape,
        "structure": structure,
        "anchor": anchor,
        "all_pass": shape["pass"] and structure["pass"] and anchor["pass"],
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
    for lo, hi in MIRROR_PAIRS:
        good[hi] = good[lo]
    case("a healthy dump", good, True)

    dead = {a: 0 for a in range(0x2680, 0x26D4)}
    dead[ANCHOR] = ANCHOR_STOCK
    case("all-zero dead bus", dead, False)

    noanswer = {a: 0xFF for a in range(0x2680, 0x26D4)}
    noanswer[ANCHOR] = 0xFF
    case("all-0xFF no answer", noanswer, False)

    varied = {a: (a * 7) & 0xFF for a in range(0x2680, 0x26D4)}
    for lo, hi in MIRROR_PAIRS:
        varied[hi] = varied[lo]
    varied[ANCHOR] = 0x00
    case("varied but anchor reset", varied, False)

    broken = dict(good)
    broken[0x2697] ^= 0xFF
    case("a broken mirror pair", broken, False)

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
