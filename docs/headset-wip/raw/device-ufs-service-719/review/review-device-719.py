#!/usr/bin/env python3
"""Read-only review of trial719 HLOG, including the loader-appended return audit."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct

BASE = Path(__file__).resolve().parent
N = r'(?:0[xX][0-9a-fA-F]+|[0-9]+)'
P = r'mt6895-ufs-service: '
CRC = 0x28a2836c
LIMITS = [
    'CRC32 agreement only; not full SHA256 or byte-for-byte verification',
    'Three kernel VFS reads of one fixed LUN2/LBA1 snapshot; no userland consumer proven',
    'No whole-disk block service, filesystem, throughput or link-speed claim',
    'HLOG body includes a loader-generated audit after the kernel log',
    'Image identity and stock recovery need separate evidence',
    'Known competing drivers are excluded; global exclusive ownership remains a prerequisite',
]

def need(value, message):
    if not value:
        raise ValueError(message)

def number(value):
    return int(value, 16 if value.lower().startswith('0x') else 10)

def sha(data):
    return hashlib.sha256(data).hexdigest()

def rows(text, pattern, marker):
    found = list(re.finditer(P + pattern + r'[ \t]*(?=\r?$)', text, re.M))
    need(len(found) == len(list(re.finditer(P + marker, text))), 'malformed ' + marker)
    return found

def single(text, pattern, marker, required=False):
    found = rows(text, pattern, marker)
    need(len(found) <= 1 and (found or not required), 'missing/repeated ' + marker)
    return found[0] if found else None

def response_fields(packet):
    fields = dict(transaction_type=packet[0] & 0x3f, lun=packet[2], tag=packet[3],
                  response=packet[6], scsi_status=packet[7] & 0x3f,
                  data_segment_length=int.from_bytes(packet[10:12], 'big'),
                  residual=int.from_bytes(packet[12:16], 'big'),
                  sense_length=int.from_bytes(packet[32:34], 'big'), ehs_length=packet[8])
    rejected = [fields['transaction_type'] != 0x21, fields['lun'] != 2, fields['tag'] != 0,
                fields['response'] != 0, fields['scsi_status'] != 0,
                fields['data_segment_length'] not in (0, 20), fields['residual'] != 0,
                fields['data_segment_length'] == 20 and fields['sense_length'] > 18,
                fields['ehs_length'] != 0]
    return fields, sum(1 << bit for bit, bad in enumerate(rejected) if bad)

def request_records(text):
    summaries = rows(text, r'request=(\d+) result=(\d+) engine=(\d+) claimed=([01]) issued=([01])'
                     r' memory_restored=([01]) delivered=([01]) control=(' + N + r') shared=(\d+) polls=(\d+)',
                     r'request=[^ \r\n]+ result=')
    patterns = {
        'ocs': (r'request=(\d+) ocs=(' + N + r') response_valid=(' + N + r') attempted=(' + N + r') failures=(' + N + r')', 'ocs='),
        'words': (r'request=(\d+) response_words([^\r\n]*)', 'response_words'),
        'diagnostic': (r'request=(\d+) diagnostic_crc32=(' + N + r') expected=(' + N + r') bytes=(\d+) accepted=([01])', 'diagnostic_crc32='),
        'block': (r'request=(\d+) block_crc32=(' + N + r') expected=(' + N + r') bytes=(\d+) accepted=([01])', 'block_crc32='),
    }
    grouped = {}
    ids = [int(m[1]) for m in summaries]
    need(ids == list(range(1, len(ids) + 1)) and len(ids) <= 3, 'request IDs/budget/order')
    for kind, (pattern, marker) in patterns.items():
        values = rows(text, pattern, r'request=[^ \r\n]+ ' + marker)
        grouped[kind] = {}
        for m in values:
            index = int(m[1])
            need(index in ids and index not in grouped[kind], 'orphan/repeated request ' + kind)
            grouped[kind][index] = m
    result = []
    for m in summaries:
        index, service, engine, claimed, issued, restored, delivered, control, shared, polls = [number(x) for x in m.groups()]
        need(service <= 7 and engine <= 6 and control <= 0x7ff and control & 0xff <= 4 and shared <= 3,
             'request enum/control range')
        need(polls <= 500000 and (not issued or claimed) and (not restored or claimed), 'request flags/poll bound')
        need(delivered == (service == 0), 'delivery/result disagreement')
        item = dict(id=index, service_result=service, engine_result=engine, claimed=claimed,
                    issued=issued, memory_restored=restored, delivered=delivered,
                    control=control, shared=shared, polls=polls)
        ocs_row = grouped['ocs'].get(index)
        need(bool(ocs_row) == bool(issued and engine in (3, 4, 5)), 'completion observation availability')
        valid = attempted = failures = 0
        ocs = None
        packet = None
        positions = [m.start()]
        if ocs_row:
            ocs, valid, attempted, failures = [number(x) for x in ocs_row.groups()[1:]]
            need(ocs <= 0x0a and valid <= 0x1fff and attempted <= 0x1fff and failures <= 0x1ff,
                 'completion/mask bounds')
            need(valid & ~attempted == 0 and valid & (valid + 1) == 0 and
                 attempted in (valid, valid | (valid + 1)), 'inconsistent response masks')
            positions.append(ocs_row.start())
        words_row = grouped['words'].get(index)
        need(bool(words_row) == (valid == 0x1fff), 'response word availability')
        if words_row:
            words = words_row[2].split()
            need(len(words) == 13 and all(re.fullmatch('[0-9a-fA-F]{8}', w) for w in words), 'response width/word encoding')
            words = [int(w, 16) for w in words]
            packet = b''.join(w.to_bytes(4, 'little') for w in words)
            item['response_words'] = words
            positions.append(words_row.start())
        evaluated = packet is not None and ocs == 0
        if evaluated:
            fields, mask = response_fields(packet)
            need(mask == failures, 'response mask contradicts raw fields')
            item['response_fields'] = fields
        else:
            need(failures == 0, 'unevaluated response mask is nonzero')
        item.update(ocs=ocs, response_valid=valid, response_attempted=attempted,
                    response_failures=failures, response_evaluated=evaluated)
        diagnostic = grouped['diagnostic'].get(index)
        block = grouped['block'].get(index)
        for kind, row in [('diagnostic', diagnostic), ('block', block)]:
            if row:
                crc, expected, size, accepted = [number(x) for x in row.groups()[1:]]
                need(crc <= 0xffffffff and expected == CRC and size == 4096 and accepted == delivered,
                     kind + ' CRC/reference/acceptance')
                need(evaluated and claimed == issued == 1, kind + ' without known completion')
                item[kind] = dict(crc32=crc, expected_crc32=expected, bytes=size, accepted=accepted)
                positions.append(row.start())
        need(bool(block) == (engine in (4, 5)), 'block comparison availability')
        if block:
            need(restored == 1 and ocs == failures == 0 and diagnostic is not None,
                 'block result lacks restored accepted response')
            need(item['block']['crc32'] == item['diagnostic']['crc32'] and
                 (engine == 5) == (item['block']['crc32'] == CRC), 'engine/CRC contradiction')
        if diagnostic and failures:
            need(engine == 3 and delivered == 0, 'rejected response marked accepted')
        if service == 0:
            need(engine == 5 and claimed == issued == restored == delivered == 1 and
                 control == 2 and shared == 1 and polls > 0, 'successful request lacks held clean lease')
        need(positions == sorted(positions) and len(set(positions)) == len(positions), 'request observation order')
        item['_start'], item['_end'] = positions[0], positions[-1]
        result.append(item)
    return result

def inspect(text, full_args):
    need(len(full_args.encode()) == 231 and full_args in text, 'exact719 runtime arguments absent')
    banner = single(text, r'bounded readonly LUN2 LBA1 bytes4096 budget3; guarded handoff required',
                    'bounded readonly ', True)
    guard = single(text, r'guard magic=(' + N + r') version=(\d+) trial=(\d+) state=(\d+)', 'guard magic=')
    if guard:
        need([number(x) for x in guard.groups()] == [0x554c5345, 1, 719, 0], 'initial guard is not fresh719')
    opened = single(text, r'opened baseline_verified=1 control=(' + N + r') shared=(\d+)', 'opened ')
    if opened:
        need(guard is not None and [number(x) for x in opened.groups()] == [2, 1], 'baseline/open not clean and held')
        need(banner.start() < guard.start() < opened.start(), 'initialization report order')
    node = single(text, r'node registered path=(\S+) major=(\d+) mode=0400 snapshot_bytes4096', 'node registered ')
    if node:
        need(opened is not None and node[1] == '/dev/mt6895-ufs-lba1' and 24 <= int(node[2]) < 64,
             'node identity/permissions/major')
        need(opened.start() < node.start(), 'node before open')
    lookup = single(text, r'vnode lookup error=(-?\d+)', 'vnode lookup ')
    write = single(text, r'write-open error=(-?\d+) expected=(\d+) requests_before=(\d+) requests_after=(\d+)', 'write-open ')
    eof = single(text, r'vfs eof error=(-?\d+) residual=(-?\d+) requests_before=(\d+) requests_after=(\d+)', 'vfs eof ')
    complete = single(text, r'selftest complete requests=(\d+) issued=(\d+) delivered=(\d+) control=(' + N +
                      r') shared=(\d+); idle close deferred to quiesce', 'selftest complete ')
    failed = single(text, r'selftest failed error=(-?\d+); close service without retry', 'selftest failed ')
    need(not (failed and complete), 'failure and success summaries coexist')
    audit = single(text, r'return-gate magic=(' + N + r') version=(\d+) trial=(\d+) state=(\d+) allowed=([01])',
                   'return-gate ', True)
    magic, version, trial, state, allowed = [number(x) for x in audit.groups()]
    need((magic, version, trial) == (0x554c5345, 1, 719) and allowed == 1 and state in (0, 2),
         'persisted runtime HLOG contradicts loader gate')
    need(text.endswith(audit[0] + '\n'), 'actual return audit is not the final HLOG span')
    if opened:
        need(state == 2, 'opened service returned as unused')
    records = request_records(text)
    need(not records or opened is not None, 'request without opened baseline')
    need(all(not r['claimed'] or (r['memory_restored'] and state == 2) for r in records),
         'persisted return after unconfirmed restored request')
    vfs = rows(text, r'vfs read=(\d+) error=(-?\d+) residual=(-?\d+) crc32=(' + N + r') expected=(' + N + r')', 'vfs read=')
    need([int(m[1]) for m in vfs] == list(range(1, len(vfs) + 1)) and len(vfs) <= 3, 'VFS read IDs/budget')
    vfs_values = []
    for m in vfs:
        index, error, residual = [int(x) for x in m.groups()[:3]]
        crc, expected = [number(x) for x in m.groups()[3:]]
        need(0 <= residual <= 4096 and 0 <= crc <= 0xffffffff and expected == CRC, 'VFS residue/CRC range/reference')
        if error == residual == 0:
            need(index <= len(records) and records[index-1]['service_result'] == 0 and crc == CRC,
                 'successful VFS copy contradicts service/CRC')
        vfs_values.append(dict(id=index, error=error, residual=residual, crc32=crc, expected_crc32=expected))
    successful = (len(records) == len(vfs_values) == 3 and
                  all(r['service_result'] == 0 for r in records) and
                  all(v['error'] == v['residual'] == 0 and v['crc32'] == CRC for v in vfs_values))
    if complete:
        need([number(x) for x in complete.groups()] == [3, 3, 3, 2, 1] and successful,
             'complete summary contradicts three clean held requests')
    successful = len(records) == len(vfs) == 3 and all(r['service_result'] == 0 for r in records) and all(
        r['error'] == r['residual'] == 0 and r['crc32'] == CRC for r in vfs_values)
    if complete:
        need(successful and [number(x) for x in complete.groups()] == [3, 3, 3, 2, 1], 'selftest total/state contradicts requests')
    write_ok = write is not None and [int(x) for x in write.groups()] == [30, 30, 0, 0]
    eof_ok = eof is not None and [int(x) for x in eof.groups()] == [0, 4, 3, 3]
    if successful:
        need(node is not None and lookup is not None and int(lookup[1]) == 0 and write_ok and eof_ok and complete is not None,
             'three reads lack node/lookup/write refusal/EOF/idle evidence')
        positions = [node.start(), lookup.start(), write.start()]
        for r, v in zip(records, vfs):
            positions += [r['_start'], r['_end'], v.start()]
        positions += [eof.start(), complete.start(), audit.start()]
        need(positions == sorted(positions) and len(set(positions)) == len(positions), 'VFS/service/return chronology')
    for item in records:
        item.pop('_start'); item.pop('_end')
    return dict(READ_SERVICE_CAPTURE='PASS', VFS_READ_SERVICE='PASS' if successful else 'NOT_PROVEN',
                READ_BLOCK_COMPARE='PASS' if successful else 'NOT_PROVEN',
                WRITE_OPEN_REJECTED='PASS' if write_ok else 'NOT_PROVEN',
                EOF_WITHOUT_COMMAND='PASS' if eof_ok else 'NOT_PROVEN',
                LEASE_HELD_BETWEEN='PASS' if successful and complete else 'NOT_PROVEN',
                LEASE_RETURN='PASS' if state == 2 and opened else 'NOT_BORROWED_OR_UNPROVEN',
                IDLE_RETURN_RELEASE='PASS' if successful and complete and state == 2 else 'NOT_PROVEN',
                requests=records, vfs_reads=vfs_values, initial_guard_observed=bool(guard),
                node_major=int(node[2]) if node else None,
                return_gate=dict(magic=magic, version=version, trial=trial, state=state, allowed=allowed),
                selftest_failure=int(failed[1]) if failed else None)

def review(raw, saved_body, full_args):
    need(len(raw) == 4198400, 'raw capture size')
    h = struct.unpack_from('<IIQQQQQQQQ', raw)
    need(h[:2] == (0x474f4c48, 1) and 0 < h[2] <= 4194304, 'HLOG identity/body bounds')
    body = raw[4096:4096+h[2]]
    need(body == saved_body and sum(body) == h[9], 'body identity/full integer sum mismatch')
    text = body.decode(errors='replace')
    report = dict(raw_bytes=len(raw), raw_sha256=sha(raw), body_bytes=len(body), body_sha256=sha(body),
                  reason=h[3], code=h[4], full_sum=h[9], full_sum_verified=True,
                  NORMAL_RETURN='UNVERIFIED', READ_SERVICE_CAPTURE='INCOMPLETE', READ_BLOCK_COMPARE='NOT_PROVEN',
                  VFS_READ_SERVICE='NOT_PROVEN', LEASE_RETURN='UNVERIFIED', IDLE_RETURN_RELEASE='NOT_PROVEN',
                  limits=LIMITS)
    if h[3] == 0xfeedface:
        need('mali_trial=719' in text and 'mtk_ufs_service=1' in text, 'stale/non719 boot note')
        report.update(status='BOOT_NOTE_ONLY', NORMAL_RETURN='NOT_CAPTURED')
        return report
    report['error_context'] = [line for line in text.splitlines() if re.search(
        r'panic\(cpu|Kernel panic|\bdata abort\b|\bDATA ABORT\b|shutdown was NOT|stop timed out', line)][:40]
    try:
        parsed = inspect(text, full_args)
        report.update(parsed)
        report['NORMAL_RETURN'] = 'PASS' if h[3] == 0 and not report['error_context'] else 'ABNORMAL'
        report['status'] = ('CAPTURED_ABNORMAL_RETURN' if report['NORMAL_RETURN'] != 'PASS' else
                            'PASS' if parsed['VFS_READ_SERVICE'] == parsed['IDLE_RETURN_RELEASE'] == 'PASS' else 'DIAGNOSTIC_ONLY')
    except ValueError as error:
        report.update(status='INCOMPLETE_OR_REJECTED', capture_error=str(error))
    return report

if __name__ == '__main__':
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('raw', type=Path)
    ap.add_argument('--body', type=Path, required=True)
    ap.add_argument('--output', type=Path, required=True)
    args = ap.parse_args()
    need(not args.output.exists(), 'output exists; use a new review path')
    try:
        full_args = json.loads((BASE/'arguments719.json').read_text())['full_arguments']
        report = review(args.raw.read_bytes(), args.body.read_bytes(), full_args)
    except (OSError, ValueError, struct.error, KeyError) as error:
        report = dict(status='INCOMPLETE_OR_REJECTED', error=str(error), limits=LIMITS)
    args.output.write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report, indent=2))
    raise SystemExit(0 if report['status'] == 'PASS' else 1)
