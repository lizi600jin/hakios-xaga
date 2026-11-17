#!/usr/bin/env python3
"""Synthetic parser checks; no phone or captured runtime bytes are consumed."""
import copy
import importlib.util
import json
from pathlib import Path
import struct

BASE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('review719', BASE/'review-device-719.py')
reviewer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(reviewer)
ARGS = json.loads((BASE/'arguments719.json').read_text())['full_arguments']
PREFIX = 'mt6895-ufs-service: '
tests = []

def packet_words(packet):
    return ' '.join(f'{int.from_bytes(packet[i:i+4], "little"):08x}' for i in range(0, 52, 4))

def valid_packet():
    p = bytearray(52)
    p[0] = 0x21; p[2] = 2; p[11] = 20; p[33] = 18
    return p

def request(index, packet=None, mask=0, engine=5, crc='0x28a2836c'):
    packet = valid_packet() if packet is None else packet
    ok = engine == 5
    parts = [
        f'request={index} result={0 if ok else 5} engine={engine} claimed=1 issued=1 memory_restored=1 delivered={int(ok)} control=0x2 shared=1 polls=116',
        f'request={index} ocs=0 response_valid=0x1fff attempted=0x1fff failures={mask:#x}',
        f'request={index} response_words ' + packet_words(packet),
        f'request={index} diagnostic_crc32={crc} expected=0x28a2836c bytes=4096 accepted={int(ok)}',
    ]
    if engine in (4, 5):
        parts.append(f'request={index} block_crc32={crc} expected=0x28a2836c bytes=4096 accepted={int(ok)}')
    parts.append(f'vfs read={index} error={0 if ok else 5} residual={0 if ok else 4096} crc32={crc} expected=0x28a2836c')
    return [PREFIX + line for line in parts]

def fixture():
    lines = [ARGS,
             PREFIX+'bounded readonly LUN2 LBA1 bytes4096 budget3; guarded handoff required',
             PREFIX+'guard magic=0x554c5345 version=1 trial=719 state=0',
             PREFIX+'opened baseline_verified=1 control=0x2 shared=1',
             PREFIX+'node registered path=/dev/mt6895-ufs-lba1 major=24 mode=0400 snapshot_bytes4096',
             PREFIX+'vnode lookup error=0',
             PREFIX+'write-open error=30 expected=30 requests_before=0 requests_after=0']
    for index in range(1, 4):
        lines += request(index)
    lines += [PREFIX+'vfs eof error=0 residual=4 requests_before=3 requests_after=3',
              PREFIX+'selftest complete requests=3 issued=3 delivered=3 control=0x2 shared=1; idle close deferred to quiesce',
              '', PREFIX+'return-gate magic=0x554c5345 version=1 trial=719 state=2 allowed=1']
    return '\n'.join(lines)+'\n'

def envelope(text, reason=0):
    body = text.encode()
    raw = bytearray(4198400)
    struct.pack_into('<IIQQQQQQQQ', raw, 0, 0x474f4c48, 1, len(body), reason, 0, 0, 0, 0, 0, sum(body))
    raw[4096:4096+len(body)] = body
    return raw, body

def run_case(name, text, status='INCOMPLETE_OR_REJECTED', reason=0):
    raw, body = envelope(text, reason)
    try:
        r = reviewer.review(raw, body, ARGS)
        got = r['status']
    except ValueError:
        got = 'INCOMPLETE_OR_REJECTED'
    assert got == status, (name, got, r if 'r' in locals() else '')
    tests.append(dict(case=name, status=got))
    return r if 'r' in locals() else None

text = fixture()
result = run_case('three VFS reads and actual final audit', text, 'PASS')
assert result['IDLE_RETURN_RELEASE'] == result['LEASE_HELD_BETWEEN'] == 'PASS'
run_case('nonzero return reason', text, 'CAPTURED_ABNORMAL_RETURN', reason=1)
run_case('panic despite normal reason', text.replace(ARGS, ARGS+'\npanic(cpu 0): synthetic'), 'CAPTURED_ABNORMAL_RETURN')
run_case('boot note alone', ARGS+'\n', 'BOOT_NOTE_ONLY', reason=0xfeedface)
run_case('stale boot note', ARGS.replace('mali_trial=719', 'mali_trial=718')+'\n', reason=0xfeedface)

replacements = [
    ('runtime argument cookie', 'mali_trial=719', 'mali_trial=718'),
    ('guard cookie', 'version=1 trial=719 state=0', 'version=1 trial=718 state=0'),
    ('guard initially restored', 'version=1 trial=719 state=0', 'version=1 trial=719 state=2'),
    ('initial control active', 'baseline_verified=1 control=0x2', 'baseline_verified=1 control=0x3'),
    ('wrong node', '/dev/mt6895-ufs-lba1', '/dev/not-the-service'),
    ('wrong node mode', 'mode=0400', 'mode=0600'),
    ('major below allocation range', 'major=24', 'major=23'),
    ('major outside cdevsw', 'major=24', 'major=64'),
    ('lookup failed but success asserted', 'vnode lookup error=0', 'vnode lookup error=2'),
    ('write accepted', 'write-open error=30 expected=30', 'write-open error=0 expected=30'),
    ('write submitted a command', 'requests_before=0 requests_after=0', 'requests_before=0 requests_after=1'),
    ('request result wrong', 'request=1 result=0', 'request=1 result=5'),
    ('request engine wrong', 'request=1 result=0 engine=5', 'request=1 result=0 engine=3'),
    ('request not claimed', 'claimed=1 issued=1', 'claimed=0 issued=1'),
    ('request not issued', 'claimed=1 issued=1', 'claimed=1 issued=0'),
    ('request memory unrestored', 'memory_restored=1', 'memory_restored=0'),
    ('request not delivered', 'delivered=1 control=', 'delivered=0 control='),
    ('request still drained', 'delivered=1 control=0x2', 'delivered=1 control=0x4'),
    ('request released early', 'control=0x2 shared=1 polls=', 'control=0x2 shared=2 polls='),
    ('zero polls', 'polls=116', 'polls=0'),
    ('poll bound exceeded', 'polls=116', 'polls=500001'),
    ('OCS nonzero', 'request=1 ocs=0 ', 'request=1 ocs=1 '),
    ('reserved OCS', 'request=1 ocs=0 ', 'request=1 ocs=0xb '),
    ('partial valid mask', 'response_valid=0x1fff', 'response_valid=0xfff'),
    ('partial attempted mask', 'attempted=0x1fff', 'attempted=0xfff'),
    ('nonprefix valid mask', 'response_valid=0x1fff', 'response_valid=0x1ffd'),
    ('unmatched failure mask', 'failures=0', 'failures=0x20'),
    ('CRC mismatch accepted', 'diagnostic_crc32=0x28a2836c', 'diagnostic_crc32=0x28a2836d'),
    ('block CRC mismatch accepted', 'block_crc32=0x28a2836c', 'block_crc32=0x28a2836d'),
    ('CRC wider than32 bits', 'diagnostic_crc32=0x28a2836c', 'diagnostic_crc32=0x128a2836c'),
    ('CRC marked unaccepted', 'bytes=4096 accepted=1', 'bytes=4096 accepted=0'),
    ('reference wrong', 'expected=0x28a2836c', 'expected=0x28a2836d'),
    ('VFS error', 'vfs read=1 error=0', 'vfs read=1 error=5'),
    ('VFS partial', 'vfs read=1 error=0 residual=0', 'vfs read=1 error=0 residual=4'),
    ('VFS wrong CRC', 'residual=0 crc32=0x28a2836c', 'residual=0 crc32=0x28a2836d'),
    ('EOF consumed data', 'vfs eof error=0 residual=4', 'vfs eof error=0 residual=0'),
    ('EOF submitted command', 'requests_before=3 requests_after=3', 'requests_before=3 requests_after=4'),
    ('complete request count', 'complete requests=3', 'complete requests=2'),
    ('complete issue count', 'complete requests=3 issued=3', 'complete requests=3 issued=2'),
    ('complete delivery count', 'requests=3 issued=3 delivered=3', 'requests=3 issued=3 delivered=2'),
    ('complete control not idle', 'delivered=3 control=0x2', 'delivered=3 control=0x4'),
    ('complete lease already released', 'control=0x2 shared=1; idle', 'control=0x2 shared=2; idle'),
    ('return gate claimed', 'trial=719 state=2 allowed=1', 'trial=719 state=1 allowed=1'),
    ('return gate poisoned', 'trial=719 state=2 allowed=1', 'trial=719 state=3 allowed=1'),
    ('return gate unused after claim', 'trial=719 state=2 allowed=1', 'trial=719 state=0 allowed=1'),
    ('return gate denied', 'trial=719 state=2 allowed=1', 'trial=719 state=2 allowed=0'),
    ('return gate cookie', 'trial=719 state=2 allowed=1', 'trial=718 state=2 allowed=1'),
]
for name, before, after in replacements:
    assert before in text, name
    run_case(name, text.replace(before, after, 1))

for marker in ['guard magic=', 'opened baseline_verified=', 'node registered ', 'vnode lookup ', 'write-open ',
               'request=1 result=', 'request=1 ocs=', 'request=1 response_words', 'request=1 diagnostic_crc32=',
               'request=1 block_crc32=', 'vfs read=1 ', 'vfs eof ', 'selftest complete ', 'return-gate ']:
    line = next(s for s in text.splitlines(keepends=True) if PREFIX+marker in s)
    run_case('missing '+marker, text.replace(line, '', 1))
    run_case('duplicate '+marker, text.replace(line, line+line, 1))

run_case('audit not last', text+'extra\n')
run_case('extra request above budget', text.replace(PREFIX+'vfs eof ', '\n'.join(request(4))+'\n'+PREFIX+'vfs eof '))
run_case('orphan diagnostic', text.replace('request=1 diagnostic_', 'request=4 diagnostic_', 1))
run_case('unexpected raw response word', text.replace(packet_words(valid_packet()), packet_words(valid_packet())+' 00000000', 1))
run_case('bad raw response word', text.replace(packet_words(valid_packet()), 'zzzzzzzz '+packet_words(valid_packet())[9:], 1))
run_case('success and failure summary', text.replace('\n\n'+PREFIX+'return-gate', '\n'+PREFIX+'selftest failed error=5; close service without retry\n\n'+PREFIX+'return-gate'))

for offset, value in [(0, 0x20), (2, 3), (3, 1), (6, 1), (7, 2), (11, 22), (15, 1), (33, 19), (8, 1)]:
    packet = valid_packet(); packet[offset] = value
    run_case(f'raw response field rejected byte{offset}', text.replace(packet_words(valid_packet()), packet_words(packet), 1))

for segment, sense in [(0, 0), (20, 0), (20, 1), (20, 18)]:
    packet = valid_packet(); packet[11] = segment; packet[33] = sense
    run_case(f'legal segment{sense}/{segment}', text.replace(packet_words(valid_packet()), packet_words(packet)), 'PASS')

# Diagnostic checksum agreement must not turn a protocol-rejected or partial service into PASS.
prelude = '\n'.join(text.splitlines()[:7])+'\n'
packet = valid_packet(); packet[7] = 2
failure = prelude+'\n'.join(request(1, packet, mask=16, engine=3))+'\n'+PREFIX+'selftest failed error=5; close service without retry\n\n'+text.split('\n\n')[-1]
r = run_case('status reject with matching diagnostic CRC', failure, 'DIAGNOSTIC_ONLY')
assert r['requests'][0]['diagnostic']['crc32'] == reviewer.CRC and r['READ_BLOCK_COMPARE'] == 'NOT_PROVEN'
failure = prelude+'\n'.join(request(1))+'\n'+PREFIX+'selftest failed error=5; close service without retry\n\n'+text.split('\n\n')[-1]
run_case('one clean read is not three', failure, 'DIAGNOSTIC_ONLY')
base_lines = '\n'.join(text.splitlines()[:4])+'\n'
run_case('registration failed after clean open', base_lines+PREFIX+'node registration failed; close without requests\n\n'+text.split('\n\n')[-1], 'DIAGNOSTIC_ONLY')
base_lines = '\n'.join(text.splitlines()[:3])+'\n'
unused_audit = text.split('\n\n')[-1].replace('state=2 allowed=1', 'state=0 allowed=1')
run_case('baseline refused untouched guard', base_lines+PREFIX+'baseline/open refused; no request\n\n'+unused_audit, 'DIAGNOSTIC_ONLY')

for name, mutate in [
    ('raw short', lambda a,b: (a[:-1], b)),
    ('wrong raw magic', lambda a,b: (bytearray(b'XXXX')+a[4:], b)),
    ('body differs', lambda a,b: (a, b+b'!')),
    ('full sum differs', lambda a,b: (a[:64]+struct.pack('<Q', sum(b)+1)+a[72:], b)),
    ('body length exceeds bound', lambda a,b: (a[:8]+struct.pack('<Q',4194305)+a[16:], b)),
]:
    a, b = mutate(*envelope(text))
    try:
        reviewer.review(a, b, ARGS)
    except ValueError:
        tests.append(dict(case=name, status='ENVELOPE_REJECTED'))
    else:
        raise AssertionError(name)

report = dict(status='PASS', cases=len(tests), tests=tests,
              limits=['Synthetic log/envelope tests only; not hardware or VFS evidence'])
out = BASE/'device-parser-tests-719.json'
assert not out.exists(), out
out.write_text(json.dumps(report, indent=2)+'\n')
print(json.dumps(dict(status='PASS', cases=len(tests))))
