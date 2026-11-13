import struct, json, re, collections
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
s1=[s for s in secs if s['va'][0]==0x1000000][0]
ds,de=s1['data']; code=d[ds:de]; base=0x1000000
md=Cs(CS_ARCH_ARM, CS_MODE_THUMB|CS_MODE_MCLASS)
md.skipdata=True
lits=collections.defaultdict(list); nins=0
for ins in md.disasm(code, base):
    nins+=1
    if '[pc' in ins.op_str:
        m=re.search(r'#(-?0x[0-9a-f]+|-?\d+)', ins.op_str)
        if m:
            la=((ins.address+4)&~3)+int(m.group(1),0)
            v=read32(la)
            if v: lits[v].append(ins.address)
print('decoded', nins, 'insns (with skipdata) over', len(code), 'bytes')
def cls(v):
    if v<0x1000: return 'SEC0 vec'
    if 0x1000000<=v<0x1010000: return 'SEC1 code'
    if 0x2000000<=v<0x2040000: return 'SEC2-4 data'
    if 0x2800000<=v<0x2840000: return 'SEC5 ro'
    if 0x3000000<=v<0x3040000: return 'SEC6-8 PROT'
    if 0x4000000<=v<0x4094000: return 'SEC9 shared'
    if 0xe0000000<=v<0xe0010000: return 'PPB'
    if 0x40000000<=v<0x40048000: return 'GPU-APERTURE'
    return 'OTHER'
c=collections.Counter()
for v in lits: c[cls(v)]+=len(lits[v])
print('\n=== literal-load targets by class (count of load sites) ===')
for k,n in c.most_common(): print('  %-14s %d sites / %d distinct' % (k,n,len([v for v in lits if cls(v)==k])))
print('\n=== PROT literals (load sites) ===')
for v in sorted(lits):
    if cls(v)=='SEC6-8 PROT':
        print('  %#010x  sites: %s' % (v, ','.join('%#x'%a for a in lits[v])))
print('\n=== GPU-APERTURE literals ===')
for v in sorted(lits):
    if cls(v)=='GPU-APERTURE':
        print('  %#010x  sites: %s' % (v, ','.join('%#x'%a for a in lits[v][:6])))
print('\n=== SEC9 shared (distinct, first 25) ===')
sh=[v for v in sorted(lits) if cls(v)=='SEC9 shared']
print('  distinct', len(sh), 'total sites', sum(len(lits[v]) for v in sh))
for v in sh[:25]: print('   %#010x x%d' % (v, len(lits[v])))
