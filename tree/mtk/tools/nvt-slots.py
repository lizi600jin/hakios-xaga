#!/usr/bin/env python3
"""Pull the finger reports out of a logbook and show which bytes carry what.

The driver prints, for every report it accepts, a line of the shape

    nvt: slots 1:2/956,923 all 0a 3b 39 cb 10 01 ff ...

The point of dumping all sixty five bytes is that the vendor formula
    x = (p[1] << 4) | (p[3] >> 4)     y = (p[2] << 4) | (p[3] & 0x0f)
gives a horizontal axis that mirrors sensibly and a vertical axis whose spread
is a quarter of the panel's. One of the two has to be wrong, and the way to
tell which is to look at every byte across a set of presses at known places
rather than reading the same two offsets again.

Usage: python3 mtk/tools/nvt-slots.py xaga-logbook.bin
"""

import re
import struct
import sys

HEADER = 72
LINE = re.compile(r"nvt: slots(.*?) all((?: [0-9a-f]{2})+)")


def reports(path):
    raw = open(path, "rb").read()
    magic, version, length = struct.unpack("<IIQ", raw[:16])

    if magic != 0x474f4c48:
        sys.exit("%s: no logbook header (magic %#x)" % (path, magic))
    body = raw[HEADER:HEADER + length].decode("utf-8", "replace")
    found = []

    for line in body.split("\n"):
        match = LINE.search(line)

        if match is None:
            continue
        slots = match.group(1).strip()
        data = bytes(int(byte, 16) for byte in match.group(2).split())
        found.append((slots, data))
    return found


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    found = reports(sys.argv[1])

    print("%d reports" % len(found))

    if not found:
        return

    width = len(found[0][1])
    print("\nper byte across the reports (min..max, distinct):")
    for at in range(width):
        column = [data[at] for _, data in found]
        print("  [%2d] %3d..%3d  %2d distinct   %s"
              % (at, min(column), max(column), len(set(column)),
                 " ".join("%02x" % value for value in column[:16])))

    print("\nslots as the driver decoded them:")
    for slots, _ in found:
        print("  %s" % slots)


if __name__ == "__main__":
    main()
