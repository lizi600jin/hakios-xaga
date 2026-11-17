#!/usr/bin/env python3
"""Host-only independent audit of718 capture; raw/body files are never modified."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct

BASE = Path(__file__).resolve().parent
NUMBER = r'(?:0[xX][0-9a-fA-F]+|[0-9]+)'
NAMES = ('REFUSED', 'NO_LEASE', 'PREPARE_FAILED', 'COMPLETED_ERROR',
         'REFERENCE_MISMATCH', 'READ_OK', 'UNCERTAIN')

def need(value, message):
    if not value: raise ValueError(message)

def number(value): return int(value, 16 if value.lower().startswith('0x') else 10)
def sha(data): return hashlib.sha256(data).hexdigest()

def unique(pattern, text, description):
    rows = list(re.finditer(pattern, text))
    need(len(rows) == 1, description + ' missing/repeated')
    return rows[0]

def inspect_base(text, full_args):
    need(len(full_args.encode()) == 228 and full_args in text, 'exact718 runtime arguments absent')
    unique(r'mt6895-ufs-read: single READ10 lun2 lba1 bytes4096; guarded ownership required',
           text, 'read banner')
    need(text.count('mt6895-ufs-read: guard magic=') == 1, 'malformed/repeated guard report')
    guard = unique(r'mt6895-ufs-read: guard magic=('+NUMBER+r') version=(\d+) trial=(\d+) state=(\d+)',
                   text, 'initial guard')
    need([number(v) for v in guard.groups()] == [0x554c5345, 1, 718, 0], 'guard not fresh/expected')
    need(text.count('mt6895-ufs-read: inherited ') == 2, 'malformed/repeated inherited metadata')
    raw = {}
    for m in re.finditer(r'mt6895-ufs-read: inherited (utrd|upiu) valid=('+NUMBER+r')([^\r\n]*)', text):
        kind, mask, tail = m[1], number(m[2]), m[3]
        need(kind not in raw, 'duplicate inherited metadata')
        words = tail.split()
        need(mask == 0xff and len(words) == 8 and all(re.fullmatch('[0-9a-fA-F]{8}', w) for w in words),
             'inherited metadata incomplete')
        raw[kind] = [int(w, 16) for w in words]
    need(set(raw) == {'utrd', 'upiu'}, 'inherited metadata missing')
    need(text.count('mt6895-ufs-read: result=') == 1, 'malformed/repeated result')
    m = unique(r'mt6895-ufs-read: result=([A-Z_]+)\((\d+)\) claimed=([01]) issued=([01])'
               r' restored=([01]) data_valid=([01]) polls=(\d+)', text, 'result summary')
    name, code = m[1], int(m[2])
    need(0 <= code < len(NAMES) and name == NAMES[code], 'result enum mismatch')
    claimed, issued, restored, data_valid, polls = [int(x) for x in m.groups()[2:]]
    need(0 <= polls <= 500000, 'poll count outside configured bound')
    need(not issued or claimed, 'issued without lease')
    need(not restored or claimed, 'restore without prior claim')
    need(text.count('mt6895-ufs-read: final shared state=') == 1, 'malformed/repeated final guard')
    final = unique(r'mt6895-ufs-read: final shared state=(\d+) loader_io_allowed=([01])', text, 'final guard')
    state, allowed = int(final[1]), int(final[2])
    # Any persisted runtime HLOG already means the loader used storage after this record.
    need(allowed == 1 and state in (0, 2), 'persisted HLOG contradicts the return guard')
    need((state == 2 and claimed == restored == 1) or (state == 0 and claimed == restored == 0),
         'claimed/restored flags contradict shared state')
    upiu = b''.join(w.to_bytes(4, 'little') for w in raw['upiu'])
    table = raw['utrd']
    previous_matches = ((table[0] & 0xfe000000) == 0x14000000 and table[2] & 0xff == 0 and
        table[4:6] == [0x60101000, 0] and table[6] == 0x00800080 and table[7] == 0x01000001 and
        upiu[:4] == bytes([1, 0x40, 2, 0]) and upiu[16] == 0x28 and
        int.from_bytes(upiu[12:16], 'big') == 4096 and int.from_bytes(upiu[18:22], 'big') == 1 and
        int.from_bytes(upiu[23:25], 'big') == 1)
    ocss = list(re.finditer(r'mt6895-ufs-read: sampled ocs=('+NUMBER+r')', text))
    checksums = list(re.finditer(r'mt6895-ufs-read: block crc32=('+NUMBER+r') expected=('
                               +NUMBER+r') bytes=(\d+)', text))
    need(text.count('mt6895-ufs-read: sampled ocs=') == len(ocss), 'malformed OCS report')
    need(text.count('mt6895-ufs-read: block crc32=') == len(checksums), 'malformed CRC report')
    ocs = None
    if issued and code in (3, 4, 5):
        need(len(ocss) == 1, 'sampled OCS missing/repeated')
        ocs = number(ocss[0][1]); need(0 <= ocs <= 0x0a, 'untrusted OCS')
    else: need(not ocss, 'OCS exposed outside collected completion')
    crc = None
    if code in (4, 5):
        need(len(checksums) == 1, 'block comparison missing/repeated')
        crc, expected, size = [number(v) for v in checksums[0].groups()]
        need(expected == 0x28a2836c and size == 4096, 'wrong comparison reference/length')
        need(ocs == 0 and claimed == issued == restored == 1, 'comparison without completed/restored request')
        need((code == 5) == (crc == expected), 'comparison result disagrees with CRC')
    else: need(not checksums, 'block CRC exposed without complete data')
    if code == 5:
        need(data_valid == 1 and polls > 0 and previous_matches, 'READ_OK lacks required evidence')
    else: need(data_valid == 0, 'non-success marked valid data')
    return dict(result=name, result_code=code, claimed=claimed, issued=issued, restored=restored,
                data_valid=data_valid, polls=polls, inherited_words=raw,
                inherited_predecessor_matches=previous_matches, sampled_ocs=ocs,
                block_crc32=crc, final_shared_state=state, loader_io_allowed=allowed,
                READ_EXPERIMENT_CAPTURE='PASS', READ_BLOCK_COMPARE='PASS' if code == 5 else 'NOT_PROVEN',
                LEASE_RETURN='PASS' if claimed and restored else 'NOT_BORROWED')

def inspect(text, full_args):
    result = inspect_base(text, full_args)
    need(text.count('mt6895-ufs-read: response valid=') == 1, 'missing/repeated response observation')
    row = unique(r'mt6895-ufs-read: response valid=('+NUMBER+r') attempted=('+NUMBER+
                 r') failures=('+NUMBER+r')([^\r\n]*)', text, 'response observation')
    valid, attempted, failures = [number(v) for v in row.groups()[:3]]
    need(0 <= valid <= 0x1fff and 0 <= attempted <= 0x1fff and 0 <= failures <= 0x1ff,
         'response masks outside bounds')
    need(valid & ~attempted == 0 and valid & (valid+1) == 0, 'nonsequential response masks')
    need(attempted in (valid, valid | (valid+1)) and attempted <= 0x1fff, 'inconsistent attempt mask')
    words = row[4].split()
    packet = None
    if valid == 0x1fff:
        need(len(words) == 13 and all(re.fullmatch('[0-9a-fA-F]{8}', w) for w in words),
             'complete response lacks thirteen raw words')
        words = [int(w,16) for w in words]
        packet = b''.join(w.to_bytes(4,'little') for w in words)
    else: need(not words, 'raw words exposed from incomplete response')
    evaluable = packet is not None and result['sampled_ocs'] == 0
    fields = {}
    if evaluable:
        fields = dict(transaction_type=packet[0]&0x3f, lun=packet[2], tag=packet[3],
                      response=packet[6], scsi_status=packet[7]&0x3f,
                      data_segment_length=int.from_bytes(packet[10:12],'big'),
                      residual=int.from_bytes(packet[12:16],'big'),
                      sense_length=int.from_bytes(packet[32:34],'big'),ehs_length=packet[8])
        rejected = dict(transaction_type=fields['transaction_type']!=0x21,lun=fields['lun']!=2,
                        tag=fields['tag']!=0,response=fields['response']!=0,
                        scsi_status=fields['scsi_status']!=0,
                        data_segment_length=fields['data_segment_length'] not in (0,20),
                        residual=fields['residual']!=0,
                        sense_length=fields['data_segment_length']==20 and fields['sense_length']>18,
                        ehs_length=fields['ehs_length']!=0)
        mask = sum(1 << i for i,key in enumerate(rejected) if rejected[key])
        need(mask == failures, 'logged failure mask contradicts raw response')
    else: need(failures == 0, 'failure mask without complete OCS0 response')
    rows = list(re.finditer(r'mt6895-ufs-read: diagnostic-only data crc32=('+NUMBER+
        r') expected=('+NUMBER+r') bytes=(\d+) accepted=([01])',text))
    need(text.count('mt6895-ufs-read: diagnostic-only data crc32=') == len(rows) and len(rows) <= 1,
         'malformed/repeated diagnostic CRC')
    diagnostic = None
    if rows:
        crc, expected, size, accepted = [number(v) for v in rows[0].groups()]
        need(evaluable and result['issued'] == 1, 'diagnostic data without known completion/response')
        need(0 <= crc <= 0x1fffffffff and expected == 0x28a2836c and size == 4096,
             'diagnostic reference/size/range')
        need(accepted == result['data_valid'], 'diagnostic acceptance contradicts result')
        if failures: need(result['result_code'] == 3 and accepted == 0, 'rejected response accepted as data')
        else:
            need(result['result_code'] in (4,5) and crc == result['block_crc32'],
                 'accepted response diagnostic contradicts block comparison')
        diagnostic = dict(crc32=crc,expected_crc32=expected,bytes=size,accepted=accepted,
                          reference_crc_matches=crc==expected)
    if result['result_code'] in (4,5):
        need(evaluable and failures == 0 and diagnostic is not None,
             'block result lacks accepted response/diagnostic capture')
    if valid: need(result['issued'] and result['result_code'] in (3,4,5), 'response outside completion')
    result.update(response_attempted=attempted,response_valid=valid,response_words=words,
                  response_failure_mask=failures,response_predicate_evaluated=evaluable,
                  response_fields=fields,
                  response_rejected_fields=[key for i,key in enumerate(fields) if failures & (1 << i)],
                  diagnostic_data=diagnostic,
                  DIAGNOSTIC_RESPONSE_CAPTURE='PASS' if evaluable else 'PARTIAL_OR_UNAVAILABLE')
    return result

def review(raw, saved_body, full_args):
    need(len(raw) == 4198400, 'raw capture size')
    h = struct.unpack_from('<IIQQQQQQQQ', raw)
    need(h[:2] == (0x474f4c48, 1) and 0 < h[2] <= 4194304, 'HLOG identity/body bounds')
    body = raw[4096:4096+h[2]]
    need(body == saved_body and sum(body) == h[9], 'body identity/full64-bit sum mismatch')
    text = body.decode(errors='replace')
    result = dict(raw_bytes=len(raw), raw_sha256=sha(raw), body_bytes=len(body), body_sha256=sha(body),
                  reason=h[3], code=h[4], full_sum=h[9], full_sum_verified=True,
                  NORMAL_RETURN='UNVERIFIED', READ_EXPERIMENT_CAPTURE='INCOMPLETE',
                  READ_BLOCK_COMPARE='NOT_PROVEN', LEASE_RETURN='UNVERIFIED')
    if h[3] == 0xfeedface:
        need('mali_trial=718' in text and 'mtk_ufs_read=1' in text, 'stale/non718 boot note')
        result.update(status='BOOT_NOTE_ONLY', NORMAL_RETURN='NOT_CAPTURED')
        result['limits'] = ['Boot note cannot distinguish predecessor-read halt, kernel failure or denied return',
                            'No runtime read or clean lease return is established']
        return result
    try:
        parsed = inspect(text, full_args)
        result.update(parsed)
        result['NORMAL_RETURN'] = 'PASS' if h[3] == 0 else 'ABNORMAL'
        result['status'] = ('PASS' if h[3] == 0 and parsed['READ_BLOCK_COMPARE'] == 'PASS' else
                            'DIAGNOSTIC_ONLY' if h[3] == 0 else 'CAPTURED_ABNORMAL_RETURN')
    except ValueError as error:
        result.update(status='INCOMPLETE_OR_REJECTED', capture_error=str(error))
    result['error_context'] = [s for s in text.splitlines() if re.search(
        r'panic\(cpu|Kernel panic|\bdata abort\b|\bDATA ABORT\b|shutdown was NOT|stop timed out', s)][:40]
    result['limits'] = ['CRC32 agreement only, not full SHA256 or mathematical byte equality',
                        'No persistent block device/filesystem/throughput or713 fix established',
                        'Image identity and stock recovery require separate evidence']
    return result

if __name__ == '__main__':
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('raw', type=Path); ap.add_argument('--body', type=Path, required=True)
    ap.add_argument('--output', type=Path, required=True); args = ap.parse_args()
    try:
        full = json.loads((BASE/'arguments718.json').read_text())['full_arguments']
        result = review(args.raw.read_bytes(), args.body.read_bytes(), full)
    except (OSError, ValueError, struct.error, KeyError) as error:
        result = dict(status='INCOMPLETE_OR_REJECTED', error=str(error))
    args.output.write_text(json.dumps(result, indent=2)+'\n')
    print(json.dumps(result, indent=2))
    raise SystemExit(0 if result['status'] == 'PASS' else 1)
