#!/usr/bin/env python3
"""Exercise the Trial725 log validator with a captured sequence in a synthetic HLOG."""
import argparse
import json
import runpy
import struct
from pathlib import Path

HERE = Path(__file__).resolve().parent


def frame(body):
    head = bytearray(4096)
    struct.pack_into('<IIQQQ', head, 0, 0x474f4c48, 1, len(body), 0, 0)
    struct.pack_into('<Q', head, 64, sum(body))
    return bytes(head) + body.ljust(4194304, b'\0')


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('lines', type=Path)
    lines = ap.parse_args().lines.read_bytes().splitlines()
    assert len(lines) == 12
    args = (HERE/'arguments725.json').read_bytes()
    head = b'boot-args = ' + json.loads(args)['full_arguments'].encode('ascii') + b'\n'
    body = head + b'\n'.join(lines) + b'\n'
    pre = frame(b'previous HLOG\n')
    raw = frame(body)
    verify = runpy.run_path(str(HERE/'parse-hlog-725.py'))['verify']
    assert verify(raw, body, pre, args)['status'] == 'FORMAT_PASS_ONLY'
    negatives = [
        (raw, body, raw, args), (raw, body[:-1], pre, args),
        (raw, body, pre, args+b' '), (raw[:4096], body, pre, args),
    ]
    texts = [head + b'\n'.join(lines[:i] + lines[i+1:]) for i in range(len(lines))]
    texts.extend([body+lines[3], head+b'\n'.join(lines[:3]+lines[5:8]+lines[3:5]+lines[8:]),
                  body.replace(b'rings=1', b'rings=2'), body.replace(b'crc=0x58f10429', b'crc=0x58f10428'),
                  body.replace(b'state=2 allowed=1', b'state=0 allowed=1'),
                  body.replace(b'data_words=2048/2048', b'data_words=1024/1024'),
                  body.replace(b'mtk_ufs_mixed=1', b'mtk_ufs_mixed=0')])
    negatives.extend((frame(t), t, pre, args) for t in texts)
    for n, inputs in enumerate(negatives):
        try:
            verify(*inputs)
        except ValueError:
            continue
        raise RuntimeError(f'invalid fixture {n} was accepted')
    print(f'HLOG checks passed: 1 accepted, {len(negatives)} rejected')


if __name__ == '__main__':
    main()
