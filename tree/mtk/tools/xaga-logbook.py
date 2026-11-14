#!/usr/bin/env python3
"""Decode a captured HLOG window; refuse incomplete or corrupt records."""

import argparse
from pathlib import Path
import struct
import sys

HEADER = struct.Struct('<IIQQQQQQQQ')
BLOCK = 4096
MAX_LOG = 4 << 20
REASONS = {
    0: 'end (the kernel handed back)',
    1: 'early exception',
    2: 'checkpoint',
    0x52534355: 'rescued kernel log (chronological, from retained DRAM)',
    0xFEEDFACE: 'boot note (written before XNU was entered)',
}


def decode(data):
    if len(data) < BLOCK:
        raise ValueError('truncated HLOG header block')
    fields = HEADER.unpack_from(data)
    magic, version, length = fields[:3]
    if magic != 0x474F4C48:
        raise ValueError(f'no HLOG header: magic 0x{magic:08x}')
    if version != 1:
        raise ValueError(f'unsupported HLOG version {version}')
    if length == 0 or length > MAX_LOG:
        raise ValueError(f'invalid HLOG length {length}')
    if len(data) < BLOCK + length:
        raise ValueError(f'truncated HLOG body: need {length}, have {len(data) - BLOCK}')
    body = data[BLOCK:BLOCK + length]
    if sum(body) != fields[-1]:
        raise ValueError('HLOG checksum mismatch; body is not a valid saved record')
    return fields, body


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('window', type=Path)
    parser.add_argument('--body', type=Path, help='also save the validated log bytes')
    args = parser.parse_args()
    try:
        fields, body = decode(args.window.read_bytes())
    except (OSError, ValueError) as error:
        print(f'logbook: {error}', file=sys.stderr)
        return 1
    _, version, length, reason, code, elr, far, ticks, where, checksum = fields
    if args.body:
        args.body.write_bytes(body)
    print(f'magic    HLOG v{version}')
    print(f'len      {length} bytes')
    print(f'reason   0x{reason:x}  {REASONS.get(reason, "probe/unknown")}')
    print(f'code     0x{code:x}')
    print(f'elr      0x{elr:x}  far 0x{far:x}')
    print(f'ticks    {ticks}  (where 0x{where:x})')
    print(f'sum      0x{checksum:x}')
    print('checksum ok')
    print('---- log ----')
    sys.stdout.write(body.decode('utf-8', 'replace'))
    print('\n---- end ----')
    return 0


if __name__ == '__main__':
    sys.exit(main())
