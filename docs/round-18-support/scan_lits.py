import struct, json
d=open('mali_csffw.bin','rb').read()
info=json.load(open('fw-sections.json'))
secs=info['secs']
def off2va(o):
    for i,s in enumerate(secs):
        vs,ve=s['va']; ds,de=s['data']
        if de>ds and ds<=o<de and o-ds< (ve-vs):
            return i, vs+(o-ds)
    return None,None
hits=[]
for o in range(0, len(d)-3, 4):
    v=struct.unpack_from('<I',d,o)[0]
    if 0x03000000 <= v < 0x03040000:
        si,va=off2va(o)
        hits.append((o,si,va,v))
print('literal words pointing into 0x03000000-0x03040000:', len(hits))
for h in hits[:60]:
    print('  fileoff %#08x sec%s va %s -> %#010x' % (h[0], h[1], ('%#x'%h[2]) if h[2] is not None else '-', h[3]))
print('--- shared 0x04000000-0x04094000 ---')
hits2=[]
for o in range(0, len(d)-3, 4):
    v=struct.unpack_from('<I',d,o)[0]
    if 0x04000000 <= v < 0x04094000:
        si,va=off2va(o)
        hits2.append((o,si,va,v))
print('count', len(hits2))
for h in hits2[:40]:
    print('  fileoff %#08x sec%s va %s -> %#010x' % (h[0], h[1], ('%#x'%h[2]) if h[2] is not None else '-', h[3]))
print('--- any 0x02xxxxxx or 0x03xxxxxx (broad) top-of-range counts ---')
from collections import Counter
c=Counter()
for o in range(0,len(d)-3,4):
    v=struct.unpack_from('<I',d,o)[0]
    if 0x02000000<=v<0x05000000: c[v>>16]+=1
for k in sorted(c): print('  %#06x0000  %d' % (k,c[k]))
