import struct, json, re
from capstone import *
d=open('mali_csffw.bin','rb').read()
info=json.load(open('fw-sections.json')); secs=info['secs']
def va2off(va):
    for s in secs:
        vs,ve=s['va']; ds,de=s['data']
        if vs<=va<ve and de>ds and (va-vs)<(de-ds): return ds+(va-vs)
    return None
def read32(va):
    o=va2off(va); return None if o is None else struct.unpack_from('<I',d,o)[0]
# section 1 full code range
s1=[s for s in secs if s['va'][0]==0x1000000][0]
code=d[s1['data'][0]:s1['data'][0]+ (s1['data'][1]-s1['data'][0])]
base=0x1000000
md=Cs(CS_ARCH_ARM, CS_MODE_THUMB|CS_MODE_MCLASS); md.detail=True
from collections import Counter, defaultdict
addrs=Counter(); srcs=defaultdict(list)
# linear sweep with resync
for ins in md.disasm(code, base):
    if '[pc' in ins.op_str:
        m=re.search(r'#(-?0x[0-9a-f]+|-?\d+)', ins.op_str)
        if m:
            la=((ins.address+4)&~3)+int(m.group(1),0)
            v=read32(la)
            if v is not None and v!=0:
                addrs[v]+=1; srcs[v].append(('lit',ins.address))
def cls(v):
    if v < 0x1000: return 'VEC/sec0'
    if 0x1000000<=v<0x1010000: return 'SEC1 code'
    if 0x2000000<=v<0x2040000: return 'SEC2-4 data'
    if 0x2800000<=v<0x2840000: return 'SEC5 ro'
    if 0x3000000<=v<0x3040000: return 'SEC6-8 PROT'
    if 0x4000000<=v<0x4094000: return 'SEC9 shared'
    if 0x40000000<=v<0x40040000: return 'APERTURE 0x4000_xxxx'
    if 0xE0000000<=v<0xE0010000: return 'PPB'
    return 'OTHER'
buckets=Counter()
for v,c in addrs.items():
    buckets[cls(v)]+=c
print('=== literal-derived addresses by class (all of sec1, linear sweep) ===')
for k,c in buckets.most_common(): print('  %-20s %d' % (k,c))
print()
print('=== APERTURE / PPB / OTHER distinct addresses ===')
for v in sorted(addrs):
    k=cls(v)
    if k in ('APERTURE 0x4000_xxxx','PPB','OTHER'):
        print('  %#010x  x%-3d %-20s %s' % (v, addrs[v], k, ','.join('%#x'%a for t,a in srcs[v][:4])))
