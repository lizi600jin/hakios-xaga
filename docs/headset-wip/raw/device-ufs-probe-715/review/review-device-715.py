#!/usr/bin/env python3
"""Audit a captured715 HLOG on the host; never open a device or modify raw evidence."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct

BASE = Path(__file__).resolve().parent
OFFSETS = (0, 8, 0x20, 0x30, 0x34, 0x50, 0x54, 0x58, 0x60)
NAMES = ('FLAGS_OBSERVED', 'READ_FAILED', 'CHANGING_WORDS', 'HCE_CLEAR',
         'DP_CLEAR', 'UTRLRDY_CLEAR', 'UNTRUSTED_RESPONSE')
NUM = r'(?:0[xX][0-9a-fA-F]+|[0-9]+)'
TOKEN = NUM + r'|FAILED|NOT_COLLECTED'


def require(condition, message):
    if not condition:
        raise ValueError(message)


def number(text):
    return int(text, 16 if text.lower().startswith('0x') else 10)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def envelope(raw):
    require(len(raw) == 4198400, 'raw capture size is not4198400')
    h = struct.unpack_from('<IIQQQQQQQQ', raw)
    require(h[:2] == (0x474f4c48, 1), 'HLOG magic/version')
    require(0 < h[2] <= 4194304, 'HLOG body length out of bounds')
    body = raw[4096:4096 + h[2]]
    require(len(body) == h[2] and sum(body) == h[9], 'full64-bit body sum mismatch')
    return body, dict(raw_bytes=len(raw), raw_sha256=digest(raw),
                     body_bytes=len(body), body_sha256=digest(body),
                     reason=h[3], code=h[4], elr=h[5], far=h[6],
                     full_sum=h[9], full_sum_verified=True)


def observation(text, arguments):
    require(len(arguments.encode()) == 229 and arguments in text, 'exact715 runtime arguments absent')
    require(text.count('mt6895-ufs: read-only probe; flag samples do not establish IO readiness') == 1,
            'read-only banner missing or repeated')
    pattern = (r'mt6895-ufs: base=(' + NUM + r') result=([A-Z_]+)\(([0-9]+)\)'
               r' valid=(' + NUM + r')/(' + NUM + r') attempted=(' + NUM + r')/('
               + NUM + r') sdb_slots=([0-9]+)')
    summaries = list(re.finditer(pattern, text))
    require(len(summaries) == 1 and text.count('mt6895-ufs: base=') == 1,
            'expected exactly one complete summary')
    m = summaries[0]
    base, name, result = number(m[1]), m[2], int(m[3])
    valid, attempted = [number(m[4]), number(m[5])], [number(m[6]), number(m[7])]
    slots = int(m[8])
    require(base == 0x112b0000, 'wrong physical base')
    require(0 <= result < len(NAMES) and name == NAMES[result], 'result name/code mismatch')
    require(all(0 <= x <= 0x1ff for x in valid + attempted), 'mask outside nine rows')
    require(all(v & ~a == 0 for v, a in zip(valid, attempted)), 'valid bits not attempted')
    require(text.count('mt6895-ufs: raw ') == 9, 'expected exactly nine raw report lines')
    rows = {}
    for m in re.finditer(r'mt6895-ufs: raw (' + NUM + r')=(' + TOKEN + r')/(' + TOKEN + r')', text):
        offset = number(m[1])
        require(offset in OFFSETS and offset not in rows, 'unexpected or duplicate raw offset')
        rows[offset] = [m[2], m[3]]
    require(set(rows) == set(OFFSETS), 'missing raw offsets')
    values = [[], []]
    for row, offset in enumerate(OFFSETS):
        for sample in range(2):
            token = rows[offset][sample]
            if valid[sample] & (1 << row):
                require(token not in ('FAILED', 'NOT_COLLECTED'), 'valid row lacks numeric value')
                value = number(token)
                require(0 <= value <= 0xffffffff, 'raw word outside32 bits')
                values[sample].append(value)
            else:
                expected = 'FAILED' if attempted[sample] & (1 << row) else 'NOT_COLLECTED'
                require(token == expected, 'invalid row is not the correct failure marker')
                values[sample].append(None)
    complete = valid == attempted == [0x1ff, 0x1ff]
    a, b = values
    if not complete:
        expected_result = 1
    elif any(a[i] != b[i] for i in range(9) if OFFSETS[i] != 0x20):
        expected_result = 2
    elif b[0] == 0xffffffff or b[1] in (0, 0xffffffff) or b[3] == 0xffffffff or b[4] == 0xffffffff:
        expected_result = 6
    elif not b[4] & 1:
        expected_result = 3
    elif not b[3] & 1:
        expected_result = 4
    elif not b[3] & 2:
        expected_result = 5
    else:
        expected_result = 0
    require(result == expected_result, 'result disagrees with captured words and frozen C precedence')
    require(slots == ((b[0] & 31) + 1 if result == 0 else 0), 'sdb_slots decode mismatch')
    flag_lines = re.findall(r'mt6895-ufs: (flags(?:-only)?[^\r\n]*)', text)
    require(len(flag_lines) == 1, 'flags report missing or repeated')
    if result in (1, 6):
        expected_flags = 'flags=INCOMPLETE' if result == 1 else 'flags=UNTRUSTED_RESPONSE'
    else:
        expected_flags = ('flags-only dp=%d utrlrdy=%d ucrdy=%d enabled=%d' %
                          (bool(b[3] & 1), bool(b[3] & 2), bool(b[3] & 8), b[4] & 1))
    require(flag_lines[0].strip() == expected_flags, 'flags disagree with second sample')
    return dict(capture_complete=complete, base=base, result=name, result_code=result,
                valid=valid, attempted=attempted, sdb_slots=slots,
                raw_pairs={f'0x{k:02x}': rows[k] for k in OFFSETS},
                decoded_samples=values, flags=expected_flags,
                is_changed=complete and a[2] != b[2],
                non_is_changed=complete and any(a[i] != b[i] for i in range(9) if i != 2))


def review(raw, arguments, expected_body=None):
    body, report = envelope(raw)
    if expected_body is not None:
        require(body == expected_body, 'saved body differs from exact HLOG body')
    text = body.decode(errors='replace')
    report['NORMAL_RETURN'] = 'PASS' if report['reason'] == 0 else 'ABNORMAL'
    report['runtime_args_bytes'] = len(arguments.encode())
    try:
        report['observation'] = observation(text, arguments)
        report['UFS_SNAPSHOT_CAPTURE'] = 'PASS' if report['observation']['capture_complete'] else 'INCOMPLETE'
    except ValueError as error:
        report['UFS_SNAPSHOT_CAPTURE'] = 'INCOMPLETE_OR_REJECTED'
        report['capture_error'] = str(error)
    report['error_context'] = [line for line in text.splitlines() if re.search(
        r'panic\(cpu|Kernel panic|\bdata abort\b|\bDATA ABORT\b|shutdown was NOT|stop timed out', line)][:40]
    if report['UFS_SNAPSHOT_CAPTURE'] != 'PASS':
        report['status'] = 'INCOMPLETE_OR_REJECTED'
    elif report['reason'] != 0:
        report['status'] = 'CAPTURED_ABNORMAL_RETURN'
    else:
        report['status'] = 'PASS'
    report['limits'] = ['Flag observations do not establish queue ownership, DMA or storage IO',
                        'No link-speed/gear/lane or physical GPU result',
                        'Stock recovery and image flash identity need separate evidence',
                        'A successful observed recovery does not prove the713 incident solved']
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('raw', type=Path)
    parser.add_argument('--body', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    try:
        parameters = json.loads((BASE / 'arguments715.json').read_text())
        report = review(args.raw.read_bytes(), parameters['full_arguments'], args.body.read_bytes())
    except (OSError, ValueError, KeyError, struct.error) as error:
        report = dict(status='INCOMPLETE_OR_REJECTED', error=str(error))
    args.output.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))
    raise SystemExit(0 if report['status'] == 'PASS' else 1)
