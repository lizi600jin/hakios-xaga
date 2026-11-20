#!/usr/bin/env python3
"""Replay captured evidence against the archived excerpt and exercise malformed records."""
import argparse
import json
import runpy
from pathlib import Path

HERE = Path(__file__).resolve().parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('baseline', type=Path)
parser.add_argument('capture', type=Path)
args = parser.parse_args()
pre, post = args.baseline.read_bytes(), args.capture.read_bytes()
module = runpy.run_path(str(HERE / 'check-hlog.py'))
verify = module['verify']
frame = runpy.run_path(str(HERE.parent / 'ufs-mixed-length-725/check-hlog.py'))['frame']
body, digest, _, _ = module['TOOLS']['record'](post)
result = verify(pre, post)
hardware = json.loads((HERE / 'package-735.json').read_bytes())['hardware']
for key in ('pre_record_sha256', 'post_record_sha256', 'post_window_sha256', 'body_bytes','body_sha256'):
    assert result[key] == hardware[key]
lines = body.splitlines(keepends=True)
indices = [i for i, l in enumerate(lines) if b'ufs-range-user:' in l or b'ufs-twoblock:' in l]
excerpt = (HERE / 'device-735-ufs.txt').read_bytes().splitlines(keepends=True)
assert [l for l in excerpt if l.startswith((b'[', b'ufs-range-user:', b'mt6895-ufs-twoblock:'))] == [lines[i] for i in indices]

def swapped(a, b):
    data = lines.copy()
    i, j = indices[a], indices[b]
    data[i], data[j] = data[j], data[i]
    return b''.join(data)

negatives = [swapped(0, 1), swapped(8, 9), swapped(2, 10), body+lines[indices[-1]], body+b'junk']
for before, after in [(b'userspace read pid=4', b'userspace read pid=5'),
                      (b'crc=0xba3342fe', b'crc=0x7fdb9635'), (b'bytes=4096 errno=0', b'bytes=4095 errno=0'),
                      (b'slice offset=127959822336 ', b'slice offset=0 '), (b'self=31240191 alternate=1',b'self=1 alternate=31240191'), (b'checksum_ok=1 valid=1',b'checksum_ok=0 valid=1'), (b'request=4 lba=30580480',b'request=4 lba=31240191'), (b'blocks=608448',b'blocks=1'), (b'magic=APSB',b'magic=NXSB'), (b'oid=1028',b'oid=1029'), (b'request=8 lba=30582876',b'request=8 lba=30582875'), (b'stride=128', b'stride=0'), (b'consistent=0',b'consistent=1'),
                      (b'before=1',b'before=0'), (b'used=88',b'used=87'), (b'overlaps=0',b'overlaps=1'), (b'stored=0xce39daa8', b'stored=0xce39daa9'),
                      (b'request=3 lba=31240186', b'request=3 lba=3'), (b'residual=1 rings=8', b'residual=1 rings=2'),
                      (b'state=2 allowed=1', b'state=2 allowed=0'), (b'--- kernel message buffer ---', b'--- console ---')]:
    assert before in body
    negatives.append(body.replace(before, after, 1))
negatives.extend(b''.join(lines[:i]+lines[i+1:]) for i in indices)
pairs = [(post, post), (pre, pre[:len(post)])] + [(pre, frame(b)) for b in negatives]
for i, pair in enumerate(pairs):
    try:
        verify(*pair)
    except ValueError:
        continue
    raise ValueError(f'accepted negative {i}')
print(f'Captured record {digest} and archived excerpt match; rejected {len(pairs)} negatives.')
