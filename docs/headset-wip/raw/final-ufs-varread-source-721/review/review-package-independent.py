#!/usr/bin/env python3
"""Read-only strict trial721 package review; Android v4 layout follows AOSP:
https://source.android.com/docs/core/architecture/bootloader/boot-image-header
"""
import argparse
import gzip
import hashlib
import json
from pathlib import Path
import re
import struct

ROOT=Path('/home/lizi/IOS/hakios-mtk')
R20=ROOT/'logs/ufs-capacity-20261117'
def need(condition,message):
    if not condition: raise ValueError(message)
def sha(data): return hashlib.sha256(data).hexdigest()

def xnup(data):
    need(len(data)>=32,'short XNUP')
    need(struct.unpack_from('<II',data)==(0x50554e58,len(data)-8),'XNUP identity/size')
    offset=8; result={}
    for expected in ['kernel','ramdisk','kernel.ver']:
        need(offset+24<=len(data),'short XNUP entry')
        name,length,reserved=struct.unpack_from('<16sII',data,offset)
        need(name==expected.encode().ljust(16,b'\0') and reserved==0 and length>0,'XNUP name/order/reserved/size')
        offset+=24; end=offset+length; padded=offset+((length+15)&~15)
        need(padded<=len(data),'XNUP entry bounds')
        need(data[end:padded]==bytes(padded-end),'XNUP nonzero padding')
        result[expected]=data[offset:end];offset=padded
    need(data[offset:]==bytes(24),'XNUP terminator/trailing data')
    return result

def boot(data,strict_end=False):
    need(len(data)>=4096 and data[:8]==b'ANDROID!','Android identity/header bound')
    need(struct.unpack_from('<I',data,40)[0]==4,'Android header version')
    need(struct.unpack_from('<I',data,20)[0]==1584,'Android v4 header size')
    kernel_size,ramdisk_size=struct.unpack_from('<II',data,8)
    signature_size=struct.unpack_from('<I',data,1580)[0]
    result={'header':data[:4096]}; offset=4096
    for name,size in [('kernel',kernel_size),('ramdisk',ramdisk_size),('sig',signature_size)]:
        end=offset+size;padded=offset+((size+4095)&~4095)
        need(padded<=len(data),'truncated Android '+name)
        result[name]=data[offset:end]
        if strict_end: need(data[end:padded]==bytes(padded-end),'nonzero Android '+name+' padding')
        offset=padded
    need(not strict_end or offset==len(data),'unexpected Android trailer')
    result['end']=offset
    return result

def argument_values(text):
    values={}; positional=[]
    for token in text.split():
        if '=' not in token: positional.append(token);continue
        key,value=token.split('=',1)
        need(key not in values,'duplicate behavior argument '+key)
        values[key]=value
    return values,positional

def review(kernel_path,loader_path,payload_path,image_path,arguments_path,kernel_review_path):
    kernel_review=json.loads(kernel_review_path.read_text())
    need(kernel_review['status']=='PASS','kernel review not PASS')
    kernel=kernel_path.read_bytes()
    need(sha(kernel)==kernel_review['kernel_sha256'],'new kernel differs from independent frozen review')
    need(sha(kernel)!='179efc781d17b41721a2d9cd5e83fb23d9b279f44c253aff27ddc00604bca19f','copied720 kernel')
    need(b'mt6895-ufs-varread: ' in kernel,'new provider strings absent from kernel')
    previous_payload=(R20/'trial720/payload.bin').read_bytes()
    need(sha(previous_payload)=='d0b202c08e41bec468ad9e10737b3723d41be07071d6ccf0b1fbfdf0c5ba7487','frozen720 payload changed')
    previous=xnup(previous_payload)
    payload=payload_path.read_bytes(); entries=xnup(payload)
    need(entries['kernel']==kernel,'nested kernel identity')
    need(entries['ramdisk']==previous['ramdisk'] and entries['kernel.ver']==previous['kernel.ver'],
         'ramdisk/version changed from frozen baseline')

    stock_data=(ROOT/'backup/boot_a_stock_real.img').read_bytes()
    need(sha(stock_data)=='c5f8e7bde8e9f1d824d0c7915f7d701adce8142656a82c20e10ccb75e15e01fc','stock identity')
    stock=boot(stock_data)
    image_data=image_path.read_bytes()
    need(len(image_data)<67108864,'boot image exceeds partition')
    image=boot(image_data,True)
    expected_header=bytearray(stock['header'])
    struct.pack_into('<I',expected_header,8,len(image['kernel']))
    need(image['header']==expected_header,'unexpected Android header change')
    need(image['ramdisk']==stock['ramdisk'] and image['sig']==stock['sig'],'Android ramdisk/signature changed')
    linux=gzip.decompress(stock['kernel'])
    need(sha(linux)=='12d255cdd09fb5b9fb5cf9608e6b060a5b2dabc109fd4344b071a3dbdc0cf391','Android Image identity')
    blob=gzip.decompress(image['kernel'])
    loader=loader_path.read_bytes()
    need(0x40<=len(loader)<=0x40000 and loader[0x38:0x3c]==b'ARM\x64','loader architecture/slot bound')
    payload_offset=0x40000+((len(linux)+15)&~15)
    need(blob[0x40000:0x40000+len(linux)]==linux,'Android Image changed inside chain')
    need(blob[payload_offset:]==payload,'payload offset/content/trailer')
    need(blob[len(loader):0x40000]==bytes(0x40000-len(loader)),'loader slot padding')
    need(blob[0x40000+len(linux):payload_offset]==bytes(payload_offset-0x40000-len(linux)),'Android alignment padding')
    patched=bytearray(loader)
    total=max(0x40000+struct.unpack_from('<Q',linux,0x10)[0],payload_offset+len(payload))
    struct.pack_into('<Q',patched,0x10,(total+0xfffff)&~0xfffff)
    struct.pack_into('<Q',patched,0x28,len(linux))
    struct.pack_into('<Q',patched,0x30,payload_offset)
    need(blob[:len(loader)]==patched,'loader differs beyond three documented header fields')

    args=json.loads(arguments_path.read_text())
    base=args['base_arguments']; need(not any(c in base for c in '\0\r\n'),'base command-line control byte')
    need(loader.count(base.encode('ascii')+b'\0')==1,'exact terminated base arguments not unique in loader')
    values,positional=argument_values(base)
    previous_args=json.loads((R20/'arguments720.json').read_text())
    baseline,previous_positional=argument_values(previous_args['base_arguments'])
    selectors={'mtk_ufs_varread','mtk_ufs_capacity','mtk_ufs_service','mtk_ufs_read','mtk_ufs_probe'}
    need(positional==previous_positional,'positional boot arguments changed')
    need(set(values)<=set(baseline)|selectors,'unexpected boot controls')
    for key,value in baseline.items():
        if key not in selectors and key!='mali_trial': need(values.get(key)==value,'baseline argument changed: '+key)
    need(values.get('mali_trial')=='721' and values.get('mtk_ufs_varread')=='1','trial/mode mismatch')
    need(all(values.get(key,'0')=='0' for key in selectors-{'mtk_ufs_varread'}),'competing mode enabled')
    version=entries['kernel.ver']
    need(version.endswith(b'\0') and version.count(b'\0')==1,'kernel.ver termination')
    version=re.split(b'[ \0]',version,maxsplit=1)[0]
    full=base.encode()+b' hakios_kver='+version
    need(len(full)<608 and len(full)+1<=608,'runtime command-line overflow')
    need(full.decode()==args['full_arguments'],'full argument metadata mismatch')
    if 'base_bytes' in args: need(args['base_bytes']==len(base.encode()),'base byte metadata')
    if 'full_bytes' in args: need(args['full_bytes']==len(full),'full byte metadata')
    if 'nul_inclusive_bytes' in args: need(args['nul_inclusive_bytes']==len(full)+1,'NUL byte metadata')
    need(args.get('device_execution_authorized') is False,'host-only hold absent')
    return {'status':'PASS','scope':'Read-only host structural/provenance verification; no hardware result',
            'image':str(image_path),'image_bytes':len(image_data),'image_sha256':sha(image_data),
            'image_md5':hashlib.md5(image_data).hexdigest(),'kernel_sha256':sha(kernel),
            'loader_bytes':len(loader),'loader_sha256':sha(loader),'payload_bytes':len(payload),'payload_sha256':sha(payload),
            'entries':{name:{'bytes':len(data),'sha256':sha(data)} for name,data in entries.items()},
            'chain_blob_bytes':len(blob),'base_args_bytes':len(base.encode()),'full_args_bytes':len(full),
            'nul_inclusive_bytes':len(full)+1,'full_arguments':full.decode()}

if __name__=='__main__':
    ap=argparse.ArgumentParser(description=__doc__)
    for option in ['kernel','loader','payload','image','arguments','kernel-review','output']:
        ap.add_argument('--'+option,type=Path,required=True)
    args=ap.parse_args()
    need(not args.output.exists(),'output exists; choose a new review path')
    try:
        report=review(args.kernel,args.loader,args.payload,args.image,args.arguments,args.kernel_review)
    except (OSError,ValueError,KeyError,struct.error) as error:
        report={'status':'REJECTED','error':str(error)}
    args.output.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))
    raise SystemExit(0 if report['status']=='PASS' else 1)
