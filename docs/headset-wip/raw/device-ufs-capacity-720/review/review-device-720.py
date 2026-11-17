#!/usr/bin/env python3
"""Proposed read-only720 runtime parser; no hardware result exists yet."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct

BASE=Path(__file__).resolve().parent
P=r'mt6895-ufs-capacity: '
N=r'(?:0[xX][0-9a-fA-F]+|[0-9]+)'
LIMITS=['One fixed RC10/LUN2 query only; no whole-disk block device or filesystem',
        'Android block-layer capacity is a separate comparison, not a substitute for query data',
        '0xffffffff is unresolved/saturated, not proof of absent medium; no automatic RC16',
        'Tail canary is a bounded observation, not a proof against all DMA corruption',
        'Image identity and stock recovery require separate evidence',
        'Body includes a loader-generated audit after kernel text']
def need(value,message):
    if not value: raise ValueError(message)
def number(value): return int(value,16 if value.lower().startswith('0x') else 10)
def sha(data): return hashlib.sha256(data).hexdigest()
def single(text,pattern,marker,required=False):
    found=list(re.finditer(P+pattern+r'[ \t]*(?=\r?$)',text,re.M))
    need(len(found)==len(list(re.finditer(P+marker,text))) and len(found)<=1 and (found or not required),
         'missing/repeated/malformed '+marker)
    return found[0] if found else None
def response(packet):
    fields=dict(transaction_type=packet[0]&0x3f,lun=packet[2],tag=packet[3],response=packet[6],
        status=packet[7]&0x3f,segment=int.from_bytes(packet[10:12],'big'),residual=int.from_bytes(packet[12:16],'big'),
        sense=int.from_bytes(packet[32:34],'big'),ehs=packet[8])
    bad=[fields['transaction_type']!=0x21,fields['lun']!=2,fields['tag']!=0,fields['response']!=0,
         fields['status']!=0,fields['segment'] not in (0,20),fields['residual']!=0,
         fields['segment']==20 and fields['sense']>18,fields['ehs']!=0]
    return fields,sum(1<<i for i,b in enumerate(bad) if b)
def words(row,group,count):
    data=row[group].split()
    need(len(data)==count and all(re.fullmatch('[0-9a-fA-F]{8}',s) for s in data),'raw word width/encoding')
    return [int(s,16) for s in data]

def inspect(text,full_args):
    need(len(full_args.encode())==232 and full_args in text,'exact720 runtime arguments absent')
    banner=single(text,r'single RC10 lun2 data_bytes8 scratch_bytes4096; guarded handoff required','single RC10 ',True)
    guard=single(text,r'guard magic=('+N+r') version=(\d+) trial=(\d+) state=(\d+)','guard magic=')
    if guard: need([number(x) for x in guard.groups()]==[0x554c5345,1,720,0],'not a fresh720 guard')
    opened=single(text,r'opened baseline_verified=1 control=('+N+r') shared=(\d+)','opened ')
    if opened:
        need(guard and [number(x) for x in opened.groups()]==[2,1],'not clean held baseline')
        need(banner.start()<guard.start()<opened.start(),'initialization report order')
    audit=single(text,r'return-gate magic=('+N+r') version=(\d+) trial=(\d+) state=(\d+) allowed=([01])','return-gate ',True)
    magic,version,trial,state,allowed=[number(x) for x in audit.groups()]
    need([magic,version,trial]==[0x554c5345,1,720] and state in (0,2) and allowed==1,
         'persisted HLOG contradicts return gate')
    need(text.endswith(audit[0]+'\n'),'return audit is not final HLOG span')
    if opened: need(state==2,'opened lease returned as UNUSED')
    result=single(text,r'result=(\d+) claimed=([01]) issued=([01]) memory_restored=([01]) query_valid=([01]) polls=(\d+)','result=')
    final=single(text,r'final control=('+N+r') shared=(\d+) idle_close_deferred=([01])','final ')
    ocs=single(text,r'ocs=('+N+r') response_valid=('+N+r') attempted=('+N+r') failures=('+N+r')','ocs=')
    packet_row=single(text,r'response_words([^\r\n]*)','response_words')
    raw_row=single(text,r'raw valid=('+N+r') attempted=('+N+r') changed=('+N+r')([^\r\n]*)','raw valid=')
    tail=single(text,r'tail complete=([01]) ok=([01]) checked_words=(\d+) first_bad=('+N+r')','tail ')
    geometry=single(text,r'decode=(\d+) last_lba=('+N+r') block_bytes=(\d+) blocks=(\d+) capacity_bytes=(\d+) android_match=([01])',
                    r'decode=\d+ last_lba=')
    unavailable=single(text,r'decode=(\d+); geometry unavailable',r'decode=\d+;')
    need(text.count('mt6895-ufs-capacity: decode=')==int(bool(geometry))+int(bool(unavailable)),'malformed/repeated decode')
    base=dict(QUERY_CAPTURE='PASS',QUERY_DATA_ACCEPTED='NOT_PROVEN',CAPACITY_OBSERVATION='NOT_PROVEN',
              ANDROID_COMPARISON='UNRESOLVED',LEASE_RETURN='PASS' if opened and state==2 else 'NOT_BORROWED_OR_UNPROVEN',
              LEASE_HELD_UNTIL_IDLE='NOT_PROVEN',IDLE_RETURN_RELEASE='NOT_PROVEN',
              return_gate=dict(magic=magic,version=version,trial=trial,state=state,allowed=allowed))
    if not result:
        need(not any([final,ocs,packet_row,raw_row,tail,geometry,unavailable]),'outcome fields without a result')
        return base
    code,claimed,issued,restored,valid,polls=[int(x) for x in result.groups()]
    need(code<=7 and polls<=500000 and (not issued or claimed) and (not restored or claimed),'query flags/range')
    need(opened is not None and raw_row and tail and final and bool(geometry)!=bool(unavailable),'query evidence incomplete')
    need(not claimed or (restored and state==2),'persisted HLOG after uncertain borrowed queue')
    control,shared,deferred=[number(x) for x in final.groups()]
    need(control<=0x7ff and (control&0xff)<=4 and shared<=3,'final control range')
    need(not control&0x200,'persisted HLOG after cached poison')
    need(shared in (1,2) and state==2,'borrowed final lease not returned')
    if shared==2: need(control&0x500==0x500 and control&0xff in (2,4),'release without clean STOP')
    item=dict(result_code=code,claimed=claimed,issued=issued,memory_restored=restored,query_valid=valid,polls=polls,
              final_control=control,final_shared=shared,idle_close_deferred=deferred)
    rvalid,attempted,changed=[number(x) for x in raw_row.groups()[:3]]
    need(0<=rvalid<=3 and 0<=attempted<=3 and changed&~rvalid==0 and
         rvalid&(rvalid+1)==0 and attempted in (rvalid,rvalid|(rvalid+1)),'raw masks')
    raw_words=words(raw_row,4,2) if rvalid==3 else []
    if rvalid!=3: need(not raw_row[4].strip(),'words exposed from partial payload')
    raw=b''.join(w.to_bytes(4,'little') for w in raw_words)
    if raw_words: need(changed==sum(1<<i for i,w in enumerate(raw_words) if w!=0xa5a5a5a5),'raw changed mask contradicts payload')
    complete,ok,count,bad=[number(x) for x in tail.groups()]
    need(count<=1022 and (not complete or count==1022) and (not ok or complete),'tail counts/flags')
    need(bad==0xffffffff or (bad%4==0 and 8<=bad<=4092 and bad<8+count*4),'tail mismatch offset')
    if complete: need(ok==(bad==0xffffffff),'tail complete contradiction')
    observed_ocs=None;mask=0;response_fields=None;response_words=[];evaluable=False
    if ocs:
        observed_ocs,pvalid,pattempted,mask=[number(x) for x in ocs.groups()]
        need(issued and observed_ocs<=0x0a and pvalid<=0x1fff and pattempted<=0x1fff and mask<=0x1ff,'OCS/response bounds')
        need(pvalid&~pattempted==0 and pvalid&(pvalid+1)==0 and pattempted in (pvalid,pvalid|(pvalid+1)),'response masks')
        need(bool(packet_row)==(pvalid==0x1fff),'response word availability')
        if packet_row:
            response_words=words(packet_row,1,13)
            if observed_ocs==0:
                response_fields,computed=response(b''.join(w.to_bytes(4,'little') for w in response_words))
                need(mask==computed,'response predicate mismatch');evaluable=True
        if not evaluable: need(mask==0,'unevaluated failure mask nonzero')
    else: need(not packet_row and rvalid==attempted==changed==count==0,'payload without completion observation')
    if rvalid or count: need(evaluable,'payload/tail without full response/OCS0')
    item.update(sampled_ocs=observed_ocs,response_evaluated=evaluable,response_failures=mask,
                response_words=response_words,response_fields=response_fields,
                raw_valid=rvalid,raw_attempted=attempted,raw_changed=changed,raw_words=raw_words,
                tail_complete=complete,tail_ok=ok,tail_checked_words=count,tail_first_bad=bad)
    decoded=int(geometry[1] if geometry else unavailable[1]);need(decoded<=3,'decode enum')
    if valid:
        need(code==5 and claimed==issued==restored==1 and polls>0 and evaluable and mask==0 and
             rvalid==attempted==changed==3 and complete==ok==1,'accepted query lacks completion/raw/tail/restore evidence')
        last=int.from_bytes(raw[:4],'big');block=int.from_bytes(raw[4:],'big')
        expected_decode=2 if last==0xffffffff else 3 if block==0 else 0
        need(decoded==expected_decode,'decode status contradicts raw payload')
        base['QUERY_DATA_ACCEPTED']='PASS'
        if decoded==0:
            need(geometry is not None,'decoded geometry missing')
            dlast,dblock,blocks,capacity,matches=[number(x) for x in geometry.groups()[1:]]
            expected_match=block==4096 and ((last+1)*block)==127959826432
            need([dlast,dblock,blocks,capacity,matches]==[last,block,last+1,(last+1)*block,int(expected_match)],'geometry/raw/arithmetic/comparison contradiction')
            base.update(CAPACITY_OBSERVATION='PASS',ANDROID_COMPARISON='MATCH' if matches else 'DIFFERENT')
            item['geometry']=dict(last_lba=last,logical_block_bytes=block,blocks=blocks,capacity_bytes=capacity)
        else:
            need(unavailable is not None,'unresolved query fabricated geometry')
            base['CAPACITY_OBSERVATION']='UNRESOLVED' if decoded==2 else 'INVALID_ZERO_BLOCK_BYTES'
    else:
        need(geometry is None,'unaccepted query exposes accepted geometry')
        if code==7: need(rvalid==3 and changed!=3,'DATA_UNCHANGED contradicts raw mask')
        if code==4: need(complete and not ok,'TAIL_CHANGED lacks tail mismatch')
    item['decode']=decoded
    held=control==2 and shared==1 and deferred==1
    if valid: need(held,'accepted query lacks clean held idle end state')
    base.update(outcome=item,LEASE_HELD_UNTIL_IDLE='PASS' if held else 'NOT_PROVEN',
                IDLE_RETURN_RELEASE='PASS' if held and state==2 else 'NOT_PROVEN')
    order=[opened.start(),result.start()]+([ocs.start()] if ocs else [])+([packet_row.start()] if packet_row else [])
    order += [raw_row.start(),tail.start(),(geometry or unavailable).start(),final.start(),audit.start()]
    need(order==sorted(order) and len(set(order))==len(order),'query report chronology')
    return base

def review(raw,saved_body,full_args):
    need(len(raw)==4198400,'raw capture size')
    h=struct.unpack_from('<IIQQQQQQQQ',raw)
    need(h[:2]==(0x474f4c48,1) and 0<h[2]<=4194304,'HLOG identity/body bound')
    body=raw[4096:4096+h[2]];need(body==saved_body and sum(body)==h[9],'body/full integer sum')
    text=body.decode(errors='replace')
    report=dict(raw_bytes=len(raw),raw_sha256=sha(raw),body_bytes=len(body),body_sha256=sha(body),
                reason=h[3],code=h[4],full_sum=h[9],full_sum_verified=True,NORMAL_RETURN='UNVERIFIED',
                QUERY_CAPTURE='INCOMPLETE',QUERY_DATA_ACCEPTED='NOT_PROVEN',CAPACITY_OBSERVATION='NOT_PROVEN',
                LEASE_RETURN='UNVERIFIED',IDLE_RETURN_RELEASE='NOT_PROVEN',limits=LIMITS)
    if h[3]==0xfeedface:
        need('mali_trial=720' in text and 'mtk_ufs_capacity=1' in text,'stale/non720 boot note')
        report.update(status='BOOT_NOTE_ONLY',NORMAL_RETURN='NOT_CAPTURED');return report
    try:
        report.update(inspect(text,full_args))
        error_context=[s for s in text.splitlines() if re.search(r'panic\(cpu|Kernel panic|\bdata abort\b|\bDATA ABORT\b|stop timed out',s)][:40]
        report['error_context']=error_context
        report['NORMAL_RETURN']='PASS' if h[3]==0 and not error_context else 'ABNORMAL'
        report['status']=('CAPTURED_ABNORMAL_RETURN' if report['NORMAL_RETURN']!='PASS' else
                          'PASS' if report['QUERY_DATA_ACCEPTED']==report['CAPACITY_OBSERVATION']==report['IDLE_RETURN_RELEASE']=='PASS' else 'DIAGNOSTIC_ONLY')
    except ValueError as error: report.update(status='INCOMPLETE_OR_REJECTED',capture_error=str(error))
    return report

if __name__=='__main__':
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('raw',type=Path)
    ap.add_argument('--body',type=Path,required=True);ap.add_argument('--output',type=Path,required=True);args=ap.parse_args()
    need(not args.output.exists(),'output exists; choose a new review path')
    try:
        full=json.loads((BASE/'arguments720.json').read_text())['full_arguments']
        report=review(args.raw.read_bytes(),args.body.read_bytes(),full)
    except (OSError,ValueError,KeyError,struct.error) as error: report=dict(status='INCOMPLETE_OR_REJECTED',error=str(error),limits=LIMITS)
    args.output.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
    raise SystemExit(0 if report['status']=='PASS' else 1)
