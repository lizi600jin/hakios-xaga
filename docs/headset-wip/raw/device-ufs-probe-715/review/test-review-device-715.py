#!/usr/bin/env python3
"""Synthetic evidence-parser regressions; no device and no runtime-evidence claims."""
import importlib.util
import json
from pathlib import Path
import struct

BASE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('review715', BASE / 'review-device-715.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
ARGS = json.loads((BASE / 'arguments715.json').read_text())['full_arguments']
CASES = []


def fixture(a=None, b=None, result=0, reason=0, incomplete=False, change=None):
    a = list(a if a is not None else [7, 0x30000, 0, 11, 1, 0x60100000, 0, 0, 1])
    b = list(a if b is None else b)
    valid, attempted = ([15, 0], [31, 0]) if incomplete else ([511, 511], [511, 511])
    slots = (b[0] & 31) + 1 if result == 0 else 0
    lines = [ARGS, 'mt6895-ufs: read-only probe; flag samples do not establish IO readiness',
             'mt6895-ufs: base=0x112b0000 result=%s(%d) valid=%#x/%#x attempted=%#x/%#x sdb_slots=%d' %
             (module.NAMES[result], result, *valid, *attempted, slots)]
    for i, offset in enumerate(module.OFFSETS):
        tokens = []
        for sample, values in enumerate((a, b)):
            if valid[sample] & (1 << i):
                tokens.append(hex(values[i]) if values[i] else '0')
            else:
                tokens.append('FAILED' if attempted[sample] & (1 << i) else 'NOT_COLLECTED')
        off = '0000' if offset == 0 else f'{offset:#04x}'
        lines.append('mt6895-ufs: raw %s=%s/%s' % (off, *tokens))
    if result in (1, 6):
        lines.append('mt6895-ufs: flags=' + ('INCOMPLETE' if result == 1 else 'UNTRUSTED_RESPONSE'))
    else:
        lines.append('mt6895-ufs: flags-only dp=%d utrlrdy=%d ucrdy=%d enabled=%d' %
                     (bool(b[3] & 1), bool(b[3] & 2), bool(b[3] & 8), b[4] & 1))
    text = '\n'.join(lines) + '\n'
    if change:
        text = change(text)
    body = text.encode()
    raw = bytearray(4198400)
    struct.pack_into('<IIQQQQQQQQ', raw, 0, 0x474f4c48, 1, len(body), reason, 0, 0, 0, 0, 0, sum(body))
    raw[4096:4096 + len(body)] = body
    return raw, body


def check(name, values, status='PASS', predicate=None):
    raw, body = values
    report = module.review(raw, ARGS, body)
    assert report['status'] == status, (name, report)
    assert predicate is None or predicate(report), (name, report)
    CASES.append(name)


def reject_envelope(name, raw, body):
    try:
        module.review(raw, ARGS, body)
    except (ValueError, struct.error):
        CASES.append(name)
        return
    raise AssertionError(name)


check('C zero-offset format and all nine pairs', fixture(), predicate=lambda r: '0x00' in r['observation']['raw_pairs'])
check('full sum above one byte', fixture(), predicate=lambda r: r['full_sum'] > 255)
check('nonzero return retains capture', fixture(reason=1), 'CAPTURED_ABNORMAL_RETURN')
b = [7, 0x30000, 0x55, 11, 1, 0x60100000, 0, 0, 1]
check('IS-only change excluded', fixture(b=b), predicate=lambda r: r['observation']['is_changed'])
b[7] = 1
check('doorbell change classified', fixture(b=b, result=2), predicate=lambda r: r['observation']['non_is_changed'])
for index, value, result, name in [(4, 0, 3, 'HCE clear'), (3, 10, 4, 'DP clear'),
                                   (3, 9, 5, 'UTRLRDY clear'), (1, 0, 6, 'zero VER'),
                                   (1, 0xffffffff, 6, 'ones VER'), (0, 0xffffffff, 6, 'ones CAP'),
                                   (3, 0xffffffff, 6, 'ones HCS'), (4, 0xffffffff, 6, 'ones HCE'),
                                   (1, 0x12345678, 0, 'unknown nonzero VER')]:
    words = [7, 0x30000, 0, 11, 1, 0x60100000, 0, 0, 1]
    words[index] = value
    check(name, fixture(a=words, result=result))
check('all ones never FLAGS_OBSERVED', fixture(a=[0xffffffff]*9, result=6))
words = [31, 0x30000, 0, 11, 1, 0x60100000, 0, 0, 1]
check('32 slots', fixture(a=words), predicate=lambda r: r['observation']['sdb_slots'] == 32)
check('partial reads never capture PASS', fixture(result=1, incomplete=True), 'INCOMPLETE_OR_REJECTED')
for name, change in [
    ('wrong trial args', lambda s: s.replace('mali_trial=715', 'mali_trial=714')),
    ('duplicate raw offset', lambda s: s + 'mt6895-ufs: raw 0000=0x7/0x7\n'),
    ('extra malformed raw line', lambda s: s + 'mt6895-ufs: raw garbage\n'),
    ('missing raw offset', lambda s: s.replace('mt6895-ufs: raw 0x60=0x1/0x1\n', '')),
    ('duplicate summary', lambda s: s + s.splitlines()[2] + '\n'),
    ('wrong slots', lambda s: s.replace('sdb_slots=8', 'sdb_slots=7')),
    ('wrong flags', lambda s: s.replace('ucrdy=1', 'ucrdy=0')),
    ('symbol/code mismatch', lambda s: s.replace('FLAGS_OBSERVED(0)', 'HCE_CLEAR(0)')),
    ('mask inconsistency', lambda s: s.replace('attempted=0x1ff/0x1ff', 'attempted=0x1fe/0x1ff')),
    ('numeric missing under valid', lambda s: s.replace('raw 0000=0x7/0x7', 'raw 0000=FAILED/0x7')),
    ('word overflow', lambda s: s.replace('raw 0000=0x7/0x7', 'raw 0000=0x100000000/0x100000000')),
    ('bad result precedence', lambda s: s.replace('raw 0x08=0x30000/0x30000', 'raw 0x08=0/0')),
]:
    check(name, fixture(change=change), 'INCOMPLETE_OR_REJECTED')
check('callback garbage cannot impersonate failed sample', fixture(result=1, incomplete=True,
      change=lambda s: s.replace('=FAILED/', '=0xdeadbeef/', 1)), 'INCOMPLETE_OR_REJECTED')
raw, body = fixture()
bad = bytearray(raw); bad[4096] ^= 1
reject_envelope('corrupt body sum', bad, body)
bad = bytearray(raw); struct.pack_into('<I', bad, 4, 2)
reject_envelope('wrong HLOG version', bad, body)
bad = bytearray(raw); struct.pack_into('<Q', bad, 8, 4194305)
reject_envelope('excessive length', bad, body)
reject_envelope('truncated capture', raw[:-1], body)
reject_envelope('saved body mismatch', raw, body + b'x')
report = dict(status='PASS', cases=len(CASES), checks=CASES, device_accessed=False,
              scope='Synthetic parser regressions only, not hardware evidence')
(BASE / 'device-parser-tests-715.json').write_text(json.dumps(report, indent=2) + '\n')
print('PASS', len(CASES), 'host evidence-parser cases')
