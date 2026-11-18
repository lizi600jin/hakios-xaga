#!/usr/bin/env python3
"""Check the frozen three-request Trial725 transcript and both HLOG captures."""
import argparse
import hashlib
import json
import re
import struct
from pathlib import Path

WINDOW = 4096 + 4194304
PREFIX = 'mt6895-ufs-twoblock: '
TRIAL = 725
REQUESTS = ((3, 1), (2, 2), (3, 1))
CRC = {2: 0x0d024e0c, 3: 0x58f10429}
ARGS_SHA = 'c42fa0096af01c5013d5c21eee91bc3fdead44c8406dd757aade97bf508b85a1'
STAMP = re.compile(r'(?:\[ *[0-9]+\.[0-9]{6}\]: )?' + re.escape(PREFIX) + r'(.*)')


def sha(data):
    return hashlib.sha256(data).hexdigest()


def require(test, reason):
    if not test:
        raise ValueError(reason)


def record(window):
    require(len(window) >= WINDOW, 'short HLOG window')
    magic, version, n, reason, code = struct.unpack_from('<IIQQQ', window)
    require((magic, version) == (0x474f4c48, 1) and 0 < n <= 4194304, 'invalid HLOG header')
    body = window[4096:4096+n]
    require(sum(body) == struct.unpack_from('<Q', window, 64)[0], 'HLOG checksum')
    return body, sha(window[:72] + body), reason, code


def expected_lines():
    lines = ['bounded READ10 lun2 lba3 blocks1 bytes4096 budget3; guarded handoff required',
             'guard magic=0x554c5345 version=1 trial=725 state=0', 'opened control=0x2 shared=1']
    for i, (start, blocks) in enumerate(REQUESTS, 1):
        words = blocks * 1024
        lines.append(f'request={i} lba={start} blocks={blocks} bytes={blocks * 4096} admitted=1 '
                     f'readback_ok=1 issued=1 rings=1 response_valid=0x1fff failures=0 ocs=0 '
                     f'data_words={words}/{words} restored=1 copied=1 control=0x4 shared=1')
        for block in range(blocks):
            lba = start + block
            crc = CRC[lba]
            lines.append(f'block={block+1} lba={lba} reference_present=1 crc={crc:#x} reference_crc={crc:#x} match=1')
    return lines + ['final control=0x2 shared=1 failed=0',
                    'return-gate magic=0x554c5345 version=1 trial=725 state=2 allowed=1']


def verify(raw, supplied_body, pre, argfile):
    require(len(raw) == WINDOW, 'post window length')
    old_body, old_digest, _, _ = record(pre)
    body, digest, reason, code = record(raw)
    require(reason == 0 and code == 0, 'abnormal return')
    require(body == supplied_body and digest != old_digest and body != old_body, 'stale or mismatched body')
    require(sha(argfile) == ARGS_SHA, 'unexpected arguments file')
    full = json.loads(argfile)['full_arguments'].encode('ascii')
    boot_args = re.findall(rb'^(?:com\.apple\.xpc\.launchd\|[^\n]*?<Notice>: |[ \t]*)boot-args = ([^\r\n]*)\r?$',
                           body, re.MULTILINE)
    require(boot_args == [full], 'boot-args mismatch')
    lines = []
    for line in body.splitlines():
        if PREFIX.encode() in line:
            match = STAMP.fullmatch(line.decode('ascii'))
            require(match is not None, 'unanchored driver record')
            lines.append(match[1])
    expected = expected_lines()
    require(len(lines) == len(expected), 'driver line count')
    for i, (got, want) in enumerate(zip(lines, expected), 1):
        require(got == want, f'driver line {i} differs from frozen sequence')
    return {'status':'FORMAT_PASS_ONLY', 'pre_record_sha256':old_digest, 'post_record_sha256':digest,
            'raw_sha256':sha(raw), 'body_bytes':len(body), 'body_sha256':sha(body), 'arguments_sha256':sha(argfile),
            'header_reason':reason, 'header_code':code, 'driver_lines':lines}


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('raw', type=Path)
    for key in ('body', 'pre', 'arguments', 'output'):
        ap.add_argument('--' + key, required=True, type=Path)
    a = ap.parse_args()
    try:
        result = verify(a.raw.read_bytes(), a.body.read_bytes(), a.pre.read_bytes(), a.arguments.read_bytes())
        rc = 0
    except (ValueError, OSError, KeyError, struct.error) as error:
        result, rc = {'status':'REJECTED', 'error':str(error)}, 1
    with a.output.open('x') as out:
        json.dump(result, out, indent=2)
        out.write('\n')
    print(result['status'], result.get('error',''))
    return rc


if __name__ == '__main__':
    raise SystemExit(main())
