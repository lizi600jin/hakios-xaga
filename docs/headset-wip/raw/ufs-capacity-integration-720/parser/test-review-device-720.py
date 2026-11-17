#!/usr/bin/env python3
"""Synthetic720 logs; no device capture or disk data consumed."""
from pathlib import Path
import importlib.util
import json
import struct
BASE=Path(__file__).resolve().parent
s=importlib.util.spec_from_file_location('parser720',BASE/'review-device-720.py')
p=importlib.util.module_from_spec(s);s.loader.exec_module(p)
ARGS=json.loads((BASE/'arguments720.json').read_text())['full_arguments']
P='mt6895-ufs-capacity: '
cases=[]
def words(data):
    return ' '.join(f'{int.from_bytes(data[i:i+4],"little"):08x}' for i in range(0,len(data),4))
def fixture(last=31240191,block=4096,result=5,valid=1,status=0,tail_bad=False):
    packet=bytearray(52);packet[0]=0x21;packet[2]=2;packet[7]=status
    raw=last.to_bytes(4,'big')+block.to_bytes(4,'big')
    decode=(2 if last==0xffffffff else 3 if block==0 else 0) if valid else 1
    changed=sum(1<<i for i in range(2) if int.from_bytes(raw[i*4:i*4+4],'little')!=0xa5a5a5a5)
    lines=[ARGS,P+'single RC10 lun2 data_bytes8 scratch_bytes4096; guarded handoff required',
        P+'guard magic=0x554c5345 version=1 trial=720 state=0',
        P+'opened baseline_verified=1 control=0x2 shared=1',
        P+f'result={result} claimed=1 issued=1 memory_restored=1 query_valid={valid} polls=116',
        P+f'ocs=0 response_valid=0x1fff attempted=0x1fff failures={16 if status else 0:#x}',
        P+'response_words '+words(packet),
        P+f'raw valid=3 attempted=3 changed={changed} '+words(raw),
        P+f'tail complete=1 ok={int(not tail_bad)} checked_words=1022 first_bad={"0x8" if tail_bad else "0xffffffff"}']
    if valid and decode==0:
        match=block==4096 and (last+1)*block==127959826432
        lines.append(P+f'decode=0 last_lba={last:#x} block_bytes={block} blocks={last+1} capacity_bytes={(last+1)*block} android_match={int(match)}')
    else: lines.append(P+f'decode={decode}; geometry unavailable')
    lines += [P+'final control=0x2 shared=1 idle_close_deferred=1','',P+'return-gate magic=0x554c5345 version=1 trial=720 state=2 allowed=1']
    return '\n'.join(lines)+'\n'
def wrap(text,reason=0):
    body=text.encode();raw=bytearray(4198400)
    struct.pack_into('<IIQQQQQQQQ',raw,0,0x474f4c48,1,len(body),reason,0,0,0,0,0,sum(body))
    raw[4096:4096+len(body)]=body;return raw,body
def case(name,text,status='INCOMPLETE_OR_REJECTED',reason=0):
    try: report=p.review(*wrap(text,reason),ARGS);got=report['status']
    except ValueError: report={};got='INCOMPLETE_OR_REJECTED'
    assert got==status,(name,got,report)
    cases.append(dict(case=name,status=got));return report
text=fixture()
case('accepted reference geometry',text,'PASS')
r=case('different512 geometry is observed not IO failure',fixture(12345,512),'PASS')
assert r['ANDROID_COMPARISON']=='DIFFERENT' and r['CAPACITY_OBSERVATION']=='PASS'
for last,block in [(0,4096),(1,520),(0xfffffffe,0xffffffff)]:
    case(f'valid geometry {last}/{block}',fixture(last,block),'PASS')
r=case('RC10 saturated remains unresolved',fixture(0xffffffff,4096),'DIAGNOSTIC_ONLY')
assert r['QUERY_DATA_ACCEPTED']=='PASS' and r['CAPACITY_OBSERVATION']=='UNRESOLVED'
case('zero-block separate from query completion',fixture(31240191,0),'DIAGNOSTIC_ONLY')
case('no fresh payload',fixture(0xa5a5a5a5,0xa5a5a5a5,result=7,valid=0),'DIAGNOSTIC_ONLY')
case('partially unchanged payload',fixture(31240191,0xa5a5a5a5,result=7,valid=0),'DIAGNOSTIC_ONLY')
case('tail changed but restored',fixture(result=4,valid=0,tail_bad=True),'DIAGNOSTIC_ONLY')
case('SCSI rejected despite raw payload',fixture(result=3,valid=0,status=2),'DIAGNOSTIC_ONLY')
case('abnormal reason',text,'CAPTURED_ABNORMAL_RETURN',reason=1)
case('panic with reason zero',text.replace(ARGS,ARGS+'\npanic(cpu 0): synthetic'),'CAPTURED_ABNORMAL_RETURN')
case('boot note only',ARGS+'\n','BOOT_NOTE_ONLY',reason=0xfeedface)
case('stale boot note',ARGS.replace('trial=720','trial=719')+'\n',reason=0xfeedface)
changes=[
 ('runtime cookie','mali_trial=720','mali_trial=719'),('guard cookie','version=1 trial=720 state=0','version=1 trial=719 state=0'),
 ('not fresh','trial=720 state=0','trial=720 state=2'),('opened state','baseline_verified=1 control=0x2 shared=1','baseline_verified=1 control=0x4 shared=1'),
 ('wrong result','result=5','result=3'),('no claim','claimed=1','claimed=0'),('not issued','issued=1','issued=0'),
 ('not restored','memory_restored=1','memory_restored=0'),('over poll bound','polls=116','polls=500001'),('no poll','polls=116','polls=0'),
 ('bad OCS','ocs=0 ','ocs=5 '),('reserved OCS','ocs=0 ','ocs=0xb '),('partial response','response_valid=0x1fff','response_valid=0xfff'),
 ('nonprefix response','response_valid=0x1fff','response_valid=0x1ffd'),('bad attempts','attempted=0x1fff','attempted=0xfff'),
 ('unequal failure mask','failures=0','failures=0x20'),('partial raw','raw valid=3','raw valid=1'),
 ('bad raw attempt','raw valid=3 attempted=3','raw valid=3 attempted=1'),('false raw freshness','changed=3','changed=1'),
 ('partial tail','tail complete=1','tail complete=0'),('tail not OK','tail complete=1 ok=1','tail complete=1 ok=0'),
 ('tail count short','checked_words=1022','checked_words=1021'),('tail count excessive','checked_words=1022','checked_words=1023'),
 ('tail mismatch hidden','first_bad=0xffffffff','first_bad=0x8'),('tail bad alignment','first_bad=0xffffffff','first_bad=0x9'),
 ('wrong last','last_lba=0x1dcafff','last_lba=0x1dcbfff'),('wrong block','block_bytes=4096','block_bytes=512'),
 ('wrong count','blocks=31240192','blocks=31240191'),('wrong capacity','capacity_bytes=127959826432','capacity_bytes=127959826433'),
 ('wrong comparison','android_match=1','android_match=0'),('wrong decode','decode=0 last','decode=2 last'),
 ('final still active','final control=0x2','final control=0x3'),('early lease release','shared=1 idle_close_deferred=1','shared=2 idle_close_deferred=1'),
 ('cached poison','final control=0x2','final control=0x302'),('not deferred','idle_close_deferred=1','idle_close_deferred=0'),
 ('gate claimed','trial=720 state=2 allowed=1','trial=720 state=1 allowed=1'),('gate poisoned','trial=720 state=2 allowed=1','trial=720 state=3 allowed=1'),
 ('gate unused','trial=720 state=2 allowed=1','trial=720 state=0 allowed=1'),('gate denied','state=2 allowed=1','state=2 allowed=0'),
 ('gate stale','trial=720 state=2 allowed=1','trial=719 state=2 allowed=1'),
]
for name,before,after in changes:
    assert before in text,name;case(name,text.replace(before,after,1))
for marker in ['single RC10 ','guard magic=','opened ','result=','ocs=','response_words','raw valid=','tail ', 'decode=', 'final ', 'return-gate ']:
    line=next(s for s in text.splitlines(keepends=True) if P+marker in s)
    case('missing '+marker,text.replace(line,'',1))
    case('repeated '+marker,text.replace(line,line+line,1))
case('audit not final',text+'extra\n')
line=next(s for s in text.splitlines(keepends=True) if 'response_words' in s)
case('excess packet word',text.replace(line,line.rstrip()+' 00000000\n'))
line=next(s for s in text.splitlines(keepends=True) if 'raw valid=' in s)
case('excess raw word',text.replace(line,line.rstrip()+' 00000000\n'))
partial=fixture(result=3,valid=0)
partial='\n'.join(s for s in partial.splitlines() if 'response_words' not in s)+'\n'
partial=partial.replace('response_valid=0x1fff attempted=0x1fff','response_valid=0xff attempted=0x1ff')
partial=partial.replace('raw valid=3 attempted=3 changed=3 ffafdc01 00100000','raw valid=0 attempted=0 changed=0')
partial=partial.replace('tail complete=1 ok=1 checked_words=1022','tail complete=0 ok=0 checked_words=0')
case('partial response failures0 is not acceptance',partial,'DIAGNOSTIC_ONLY')
early='\n'.join(text.splitlines()[:3])+'\n'+P+'baseline/open refused; no request\n\n'+text.split('\n\n')[-1].replace('state=2 allowed=1','state=0 allowed=1')
case('baseline refusal did not borrow',early,'DIAGNOSTIC_ONLY')
for name,change in [('short',lambda a,b:(a[:-1],b)),('magic',lambda a,b:(b'BAD!'+a[4:],b)),
                    ('body',lambda a,b:(a,b+b'!')),('sum',lambda a,b:(a[:64]+struct.pack('<Q',sum(b)+1)+a[72:],b))]:
    a,b=change(*wrap(text))
    try: p.review(a,b,ARGS)
    except ValueError: cases.append(dict(case='envelope '+name,status='REJECTED'))
    else: raise AssertionError(name)
out=BASE/'device-parser-tests-720.json';assert not out.exists()
out.write_text(json.dumps(dict(status='PASS',cases=len(cases),tests=cases,limits=['Synthetic only; no runtime720 IO']),indent=2)+'\n')
print(json.dumps(dict(status='PASS',cases=len(cases))))
