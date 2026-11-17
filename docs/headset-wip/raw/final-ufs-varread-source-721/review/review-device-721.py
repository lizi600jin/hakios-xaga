#!/usr/bin/env python3
"""Read-only trial721 HLOG verifier. Synthetic fixtures never become hardware PASS."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct

PREFIX='mt6895-ufs-varread: '
NUMBER=r'(0[xX][0-9a-fA-F]+|[0-9]+)'
EXPECTED_LBAS=[1,2,15,1]
EXPECTED_CRC={1:0x28a2836c,2:0x0d024e0c,15:0xc71c0011}
SYNTHETIC_MARKER='SYNTHETIC TRIAL721 HOST FORMAT FIXTURE'
LIMITS=[
    'Four bounded kernel READ10 requests; no variable userland API, whole-disk block layer or filesystem claim',
    'LBA binding relies on the frozen source and exact-request readback; this is not an independent wire trace',
    'Reference comparisons are CRC32 observations, never byte identity or unique LBA identification',
    'Reported full CPU sampling does not establish every physical DMA write or global controller exclusivity',
    'Image identity and restored-stock state require separate frozen-package and recovery evidence',
    'Command count uses the reviewed single-init budget and single-ring engine, not issued booleans alone',
]

def need(condition,message):
    if not condition: raise ValueError(message)
def number(value): return int(value,16 if value.lower().startswith('0x') else 10)
def sha(data): return hashlib.sha256(data).hexdigest()
def one(lines,pattern,label):
    matches=[re.fullmatch(pattern,line) for line in lines]
    matches=[match for match in matches if match]
    need(len(matches)==1,'missing/repeated/malformed '+label)
    return matches[0]

def inspect(text,full_arguments):
    need(0<len(full_arguments.encode('ascii'))<608,'command line exceeds frozen loader bound')
    need(not any(c in full_arguments for c in '\0\r\n'),'command line control characters')
    values={}
    for token in full_arguments.split():
        if '=' not in token: continue
        key,value=token.split('=',1)
        need(key not in values,'duplicate boot-argument key '+key)
        values[key]=value
    required={'mali_trial':'721','mtk_ufs_varread':'1','mali':'0','headset':'0','cpus':'4','loader_return_after':'150'}
    need(all(values.get(key)==value for key,value in required.items()),'incorrect trial/mode/profile arguments')
    need(all(values.get(key,'0')=='0' for key in ['mtk_ufs_probe','mtk_ufs_read','mtk_ufs_service','mtk_ufs_capacity']),
         'competing UFS mode in frozen arguments')
    normalized=text.replace('\r\n','\n')
    arglines=re.findall(r'^.*\bboot-args = ([^\n]*)$',normalized,re.M)
    need(arglines==[full_arguments],'exact sole observed command line absent or extended')
    lines=[line[len(PREFIX):] for line in normalized.splitlines() if line.startswith(PREFIX)]
    need(len(lines)==9,'unexpected/missing driver report span')
    need(lines[0]=='bounded variable READ10 lun2 lba0..15 bytes4096 budget4; guarded handoff required','wrong trial banner')
    guard=one(lines,r'guard magic='+NUMBER+r' version=(\d+) trial=(\d+) state=(\d+)','guard')
    need([number(value) for value in guard.groups()]==[0x554c5345,1,721,0],'not fresh trial721 guard')
    opened=one(lines,r'opened control='+NUMBER+r' shared=(\d+)','opened')
    need([number(value) for value in opened.groups()]==[2,1],'opened lease is not held IDLE')
    need(lines[1]==guard[0] and lines[2]==opened[0],'initialization report order')
    fields=['request','lba','admitted','readback_ok','issued','response_valid','failures','ocs',
            'data_words_valid','data_words_attempted','restored','copied','reference_present','crc','reference_crc','match','control','shared']
    pattern=(r'request=(\d+) lba=(\d+) admitted=([01]) readback_ok=([01]) issued=([01]) '
             r'response_valid='+NUMBER+r' failures='+NUMBER+r' ocs='+NUMBER+
             r' data_words=(\d+)/(\d+) restored=([01]) copied=([01]) reference_present=([01]) '
             r'crc='+NUMBER+r' reference_crc='+NUMBER+r' match=([01]) control='+NUMBER+r' shared=(\d+)')
    records=[]
    for index,line in enumerate(lines[3:7]):
        match=re.fullmatch(pattern,line)
        need(match is not None,'malformed/incomplete request record')
        record=dict(zip(fields,[number(value) for value in match.groups()]))
        lba=EXPECTED_LBAS[index]
        expected={'request':index+1,'lba':lba,'admitted':1,'readback_ok':1,'issued':1,
                  'response_valid':0x1fff,'failures':0,'ocs':0,'data_words_valid':1024,
                  'data_words_attempted':1024,'restored':1,'copied':1,'reference_present':1,
                  'crc':EXPECTED_CRC[lba],'reference_crc':EXPECTED_CRC[lba],'match':1,
                  'control':4,'shared':1}
        need(record==expected,'request order/completion/restore/copy/reference contradiction')
        records.append(record)
    final=one(lines,r'final control='+NUMBER+r' shared=(\d+) failed=([01])','final')
    need(lines[7]==final[0] and [number(value) for value in final.groups()]==[2,1,0],
         'final lease is not clean held IDLE after all four reads')
    audit=one(lines,r'return-gate magic='+NUMBER+r' version=(\d+) trial=(\d+) state=(\d+) allowed=([01])','return gate')
    need(lines[8]==audit[0] and [number(value) for value in audit.groups()]==[0x554c5345,1,721,2,1],
         'return gate did not observe restored trial721 lease')
    need(normalized.endswith(PREFIX+audit[0]+'\n'),'return audit is not the final HLOG span')
    errors=[line for line in normalized.splitlines() if re.search(r'panic\(cpu|Kernel panic|\bdata abort\b|\bDATA ABORT\b|stop timed out',line)]
    need(not errors,'fatal error context in HLOG')
    return {'records':records,'final_control':2,'final_shared':1,
            'return_gate':{'magic':0x554c5345,'version':1,'trial':721,'state':2,'allowed':1},
            'record_control_sampling':'DRAINED snapshot before copy_done; final control2 is later IDLE',
            'error_context':errors}

def review(raw,saved_body,full_arguments):
    report={'status':'INCOMPLETE_OR_REJECTED','trial':721,'limits':LIMITS}
    try:
        need(len(raw)==4198400,'raw capture size')
        header=struct.unpack_from('<IIQQQQQQQQ',raw)
        need(header[:2]==(0x474f4c48,1) and 0<header[2]<=4194304,'HLOG identity/body bound')
        body=raw[4096:4096+header[2]]
        need(body==saved_body and sum(body)==header[9],'body identity/full integer sum')
        report.update(raw_bytes=len(raw),raw_sha256=sha(raw),body_bytes=len(body),body_sha256=sha(body),
                      reason=header[3],code=header[4],full_sum=header[9],full_sum_verified=True)
        text=body.decode(errors='replace')
        if header[3]==0xfeedface:
            need('mali_trial=721' in text and 'mtk_ufs_varread=1' in text,'stale/non721 boot note')
            report.update(status='BOOT_NOTE_ONLY',NORMAL_RETURN='NOT_CAPTURED');return report
        need(header[3]==header[4]==0,'abnormal reason/code')
        checked=inspect(text,full_arguments)
        if SYNTHETIC_MARKER in text:
            report.update(status='FORMAT_PASS_ONLY',evidence_kind='SYNTHETIC_HOST_FIXTURE',
                          NORMAL_RETURN='NOT_HARDWARE_EVIDENCE',validated_format=checked)
        else:
            report.update(checked,status='PASS',NORMAL_RETURN='PASS',RUNTIME_READS='PASS',
                          REFERENCE_CHECKSUMS='MATCH',LEASE_HELD_THROUGH_REQUESTS='PASS',LEASE_RETURN='PASS')
    except (ValueError,UnicodeError,struct.error) as error:
        report['error']=str(error)
    return report

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('raw',type=Path)
    parser.add_argument('--body',type=Path,required=True)
    parser.add_argument('--arguments',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    need(not args.output.exists(),'output exists; use a fresh review path')
    try:
        full=json.loads(args.arguments.read_text())['full_arguments']
        result=review(args.raw.read_bytes(),args.body.read_bytes(),full)
    except (OSError,KeyError,ValueError) as error:
        result={'status':'INCOMPLETE_OR_REJECTED','error':str(error),'limits':LIMITS}
    args.output.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result,indent=2))
    raise SystemExit(0 if result['status']=='PASS' else 1)
