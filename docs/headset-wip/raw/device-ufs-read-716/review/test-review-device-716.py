#!/usr/bin/env python3
"""Synthetic tests for716 evidence parsing; never accesses a phone."""
import importlib.util
import json
from pathlib import Path
import struct

BASE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('read716', BASE/'review-device-716.py')
module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
ARGS = json.loads((BASE/'arguments716.json').read_text())['full_arguments']
CASES = []

def wrap(text, reason=0):
    body = text.encode(); raw = bytearray(4198400)
    struct.pack_into('<IIQQQQQQQQ', raw, 0, 0x474f4c48, 1, len(body), reason, 0, 0, 0, 0, 0, sum(body))
    raw[4096:4096+len(body)] = body
    return raw, body

def fixture(code=5, reason=0, change=None):
    upiu = bytearray(32)
    upiu[:4] = bytes([1,0x40,2,0]);upiu[14] = 0x10;upiu[16] = 0x28;upiu[21] = upiu[24] = 1
    table = [0x14000000,0,0,0,0x60101000,0,0x00800080,0x01000001]
    claimed = issued = restored = valid = 1;state = 2
    if code in (0,1): claimed = issued = restored = valid = 0;state = 0
    if code == 2: issued = valid = 0
    if code in (3,4): valid = 0
    lines = [ARGS,
        'mt6895-ufs-read: single READ10 lun2 lba1 bytes4096; guarded ownership required',
        'mt6895-ufs-read: guard magic=0x554c5345 version=1 trial=716 state=0',
        'mt6895-ufs-read: inherited utrd valid=0xff '+' '.join(f'{x:08x}' for x in table),
        'mt6895-ufs-read: inherited upiu valid=0xff '+' '.join(f'{x:08x}' for x in struct.unpack('<8I',upiu)),
        f'mt6895-ufs-read: result={module.NAMES[code]}({code}) claimed={claimed} issued={issued} restored={restored} data_valid={valid} polls=1']
    if code in (3,4,5): lines.append('mt6895-ufs-read: sampled ocs='+('0x5' if code==3 else '0'))
    if code in (4,5): lines.append('mt6895-ufs-read: block crc32='+('0x28a2836d' if code==4 else '0x28a2836c')+' expected=0x28a2836c bytes=4096')
    lines.append(f'mt6895-ufs-read: final shared state={state} loader_io_allowed=1')
    text = '\n'.join(lines)+'\n'
    return wrap(change(text) if change else text, reason)

def check(name, values, status, predicate=None):
    raw, body = values
    result = module.review(raw, body, ARGS)
    assert result['status'] == status, (name,result)
    assert predicate is None or predicate(result), (name,result)
    CASES.append(name)

check('complete successful read and clean lease', fixture(), 'PASS',
      lambda x:x['READ_BLOCK_COMPARE']==x['LEASE_RETURN']==x['NORMAL_RETURN']=='PASS')
for code in range(5): check('diagnostic enum'+str(code),fixture(code=code),'DIAGNOSTIC_ONLY')
check('nonzero runtime reason',fixture(reason=1),'CAPTURED_ABNORMAL_RETURN')
check('boot note never a runtime result',wrap('boot args: mali_trial=716 mtk_ufs_read=1\n',0xfeedface),
      'BOOT_NOTE_ONLY',lambda x:x['NORMAL_RETURN']=='NOT_CAPTURED' and x['READ_BLOCK_COMPARE']=='NOT_PROVEN')
for name,change in [
    ('wrong runtime trial',lambda x:x.replace('mali_trial=716','mali_trial=715')),
    ('duplicate guard',lambda x:x+'mt6895-ufs-read: guard magic=0x554c5345 version=1 trial=716 state=0\n'),
    ('malformed extra guard',lambda x:x+'mt6895-ufs-read: guard magic=oops\n'),
    ('wrong guard magic',lambda x:x.replace('magic=0x554c5345','magic=0x0')),
    ('wrong guard trial',lambda x:x.replace('trial=716 state=0','trial=717 state=0')),
    ('not fresh guard',lambda x:x.replace('trial=716 state=0','trial=716 state=2')),
    ('incomplete raw metadata',lambda x:x.replace('utrd valid=0xff','utrd valid=0x7f')),
    ('extra malformed metadata',lambda x:x+'mt6895-ufs-read: inherited invalid\n'),
    ('wrong predecessor direction',lambda x:x.replace('14000000','12000000')),
    ('wrong predecessor opcode',lambda x:x.replace('00000028','0000002a')),
    ('wrong result enum',lambda x:x.replace('READ_OK(5)','READ_OK(4)')),
    ('duplicate result',lambda x:x+x.splitlines()[5]+'\n'),
    ('read without claim',lambda x:x.replace('claimed=1','claimed=0')),
    ('read without issuance',lambda x:x.replace('issued=1','issued=0')),
    ('read without restore',lambda x:x.replace('restored=1','restored=0')),
    ('read without valid data',lambda x:x.replace('data_valid=1','data_valid=0')),
    ('zero read polls',lambda x:x.replace('polls=1','polls=0')),
    ('excess read polls',lambda x:x.replace('polls=1','polls=500001')),
    ('reserved OCS',lambda x:x.replace('sampled ocs=0','sampled ocs=0xb')),
    ('unsuccessful OCS',lambda x:x.replace('sampled ocs=0','sampled ocs=5')),
    ('missing OCS',lambda x:x.replace('mt6895-ufs-read: sampled ocs=0\n','')),
    ('wrong CRC',lambda x:x.replace('block crc32=0x28a2836c','block crc32=0x28a2836d')),
    ('changed comparison oracle',lambda x:x.replace('expected=0x28a2836c','expected=0x28a2836d')),
    ('wrong block length',lambda x:x.replace('bytes=4096','bytes=512')),
    ('missing CRC',lambda x:x.replace('mt6895-ufs-read: block crc32=0x28a2836c expected=0x28a2836c bytes=4096\n','')),
    ('poisoned lease persisted contradiction',lambda x:x.replace('final shared state=2','final shared state=3')),
    ('claimed lease persisted contradiction',lambda x:x.replace('final shared state=2','final shared state=1')),
    ('denied lease persisted contradiction',lambda x:x.replace('loader_io_allowed=1','loader_io_allowed=0')),
]: check(name,fixture(change=change),'INCOMPLETE_OR_REJECTED')

raw,body = fixture()
for name,mutate in [
    ('wrong envelope size',lambda b:b[:-1]),
    ('corrupt body',lambda b:b[:4096]+bytes([b[4096]^1])+b[4097:]),
    ('low-byte checksum rejected',lambda b:b[:64]+struct.pack('<Q',sum(body)&255)+b[72:]),
]:
    try: module.review(mutate(raw),body,ARGS)
    except (ValueError,struct.error): CASES.append(name)
    else: raise AssertionError(name)
report=dict(status='PASS',cases=len(CASES),checks=CASES,scope='Synthetic evidence parser tests only; no device')
(BASE/'device-parser-tests-716.json').write_text(json.dumps(report,indent=2)+'\n')
print('PASS',len(CASES),'716 host evidence-parser cases')
