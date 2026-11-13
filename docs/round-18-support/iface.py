import struct, json
d=open('mali_csffw.bin','rb').read()
off=20; out=[]
while off+4<=len(d):
    eh=struct.unpack_from('<I',d,off)[0]
    typ=eh&0xff; esz=(eh>>8)&0xff
    if esz<4 or esz&3 or off+esz>len(d): print('stop@',off,hex(eh)); break
    words=[struct.unpack_from('<I',d,off+4+4*i)[0] for i in range((esz-4)//4)]
    out.append((off,typ,esz,eh,words))
    off+=esz
for o,t,e,eh,w in out:
    print('off %#06x type %d esize %-3d flags %#x words %s' % (o,t,e,eh>>24, ' '.join('%#x'%x for x in w)))
