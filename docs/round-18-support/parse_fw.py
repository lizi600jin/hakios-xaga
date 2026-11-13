import struct, json
d=open('mali_csffw.bin','rb').read()
magic=struct.unpack_from('<I',d,0)[0]
tbl_end=struct.unpack_from('<I',d,16)[0]
print('tbl_end',tbl_end,'len',len(d))
off=20; secs=[]; others=[]
while off+4<=len(d):
    eh=struct.unpack_from('<I',d,off)[0]
    typ=eh&0xff; esz=(eh>>8)&0xff
    if esz<4 or esz&3 or off+esz>len(d): print('bad@',off,hex(eh)); break
    if typ==0:
        flags,va_s,va_e,ds,de=struct.unpack_from('<IIIII',d,off+4)
        secs.append(dict(off=off,flags=flags,va=(va_s,va_e),data=(ds,de),esize=esz))
    else:
        others.append(dict(off=off,type=typ,esize=esz,raw=d[off:off+esz].hex()))
    off+=esz
for i,s in enumerate(secs):
    print('sec%-2d va %#011x-%#011x flags %#x datalen %d  dstart %#x' % (i,s['va'][0],s['va'][1],s['flags'],s['data'][1]-s['data'][0],s['data'][0]))
print('other entries:',len(others))
for o in others[:20]: print('  ',o['type'],o['esize'],o['raw'][:40])
json.dump(dict(secs=secs,others=others),open('fw-sections.json','w'),indent=1)
