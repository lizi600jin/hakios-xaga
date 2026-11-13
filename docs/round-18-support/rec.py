import struct, json, re, sys
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
md=Cs(CS_ARCH_ARM, CS_MODE_THUMB|CS_MODE_MCLASS)
mi=Cs(CS_ARCH_ARM, CS_MODE_THUMB|CS_MODE_MCLASS); mi.detail=True

visited=set(); pending=[0x10018ec]; addrs={}; srcs={}
import collections
srcs=collections.defaultdict(list)
insn_count=0
while pending:
    a=pending.pop()
    if a in visited: continue
    o=va2off(a)
    if o is None: continue
    # follow until terminator
    for ins in md.disasm(d[o:o+0x400], a):
        if ins.address in visited: break
        visited.add(ins.address); insn_count+=1
        m=ins.mnemonic; ops=ins.op_str
        # literal loads
        if '[pc' in ops:
            mm=re.search(r'#(-?0x[0-9a-f]+|-?\d+)', ops)
            if mm:
                la=((ins.address+4)&~3)+int(mm.group(1),0)
                v=read32(la)
                if v is not None and v!=0:
                    addrs[v]=addrs.get(v,0)+1; srcs[v].append(ins.address)
        # direct branches
        if m in ('bl','b','b.w','bl.w') and ops.startswith('#'):
            try: t=int(ops[1:],16)
            except: t=None
            if t is not None and t!=ins.address:
                if m.startswith('bl'): pending.append(t)
                else: pending.append(t)
        if m in ('bx','pop','b') and not (m=='b' and ops.startswith('#')):
            break
        if m.startswith('b') and m not in ('bic','bfc','bfi','bl','blx') and ops.startswith('#') and m!='b':
            break
def cls(v):
    if v < 0x1000: return 'SEC0 vec'
    if 0x1000000<=v<0x1010000: return 'SEC1 code'
    if 0x2000000<=v<0x2040000: return 'SEC2-4 data'
    if 0x2800000<=v<0x2840000: return 'SEC5 ro'
    if 0x3000000<=v<0x3040000: return 'SEC6-8 PROT'
    if 0x4000000<=v<0x4094000: return 'SEC9 shared'
    if 0x9000000<=v<0x9100000: return '?'
    if 0xe0000000<=v<0xe0010000: return 'PPB'
    return 'OTHER/SoC'
b=collections.Counter()
for v,c in addrs.items(): b[cls(v)]+=1
print('reachable insns:', insn_count, 'distinct literal targets:', len(addrs))
for k,c in b.most_common(): print('  %-14s %d' % (k,c))
print('\n--- PROT / shared / OTHER literals ---')
for v in sorted(addrs):
    k=cls(v)
    if k in ('SEC6-8 PROT','SEC9 shared','OTHER/SoC','SEC0 vec','SEC5 ro'):
        print('  %#010x  x%-3d %-10s from %s' % (v,addrs[v],k,','.join('%#x'%x for x in srcs[v][:4])))
json.dump({'addrs':{hex(k):v for k,v in addrs.items()}}, open('reach-addrs.json','w'), indent=1)
