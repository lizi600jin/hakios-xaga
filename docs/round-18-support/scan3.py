import struct, json, re
from capstone import *
d=open('mali_csffw.bin','rb').read()
info=json.load(open('fw-sections.json')); secs=info['secs']
def va2off(va):
    for s in secs:
        vs,ve=s['va']; ds,de=s['data']
        if vs<=va<ve and de>ds and (va-vs)<(de-ds): return ds+(va-vs)
    return None
s1=[s for s in secs if s['va'][0]==0x1000000][0]
code=d[s1['data'][0]:s1['data'][1]]
md=Cs(CS_ARCH_ARM, CS_MODE_THUMB|CS_MODE_MCLASS)
insns=list(md.disasm(code, 0x1000000))
print('sec1 insns disassembled:', len(insns), 'code bytes', len(code))
regs={}
found={}
for i,ins in enumerate(insns):
    m=ins.mnemonic
    if m=='movw':
        r=re.match(r'(\w+),\s*#(-?0x[0-9a-fA-F]+|-?\d+)', ins.op_str)
        if r: regs[r.group(1)]=(int(r.group(2),0)&0xffff, ins.address, 1)
    elif m=='movt':
        r=re.match(r'(\w+),\s*#(-?0x[0-9a-fA-F]+|-?\d+)', ins.op_str)
        if r and r.group(1) in regs:
            base,at,ok=regs[r.group(1)]
            v=(int(r.group(2),0)&0xffff)<<16 | base
            regs.pop(r.group(1))
            found.setdefault(v,[]).append((at,ins.address))
def cls(v):
    if v < 0x1000: return 'SEC0 vec'
    if 0x1000000<=v<0x1010000: return 'SEC1 code'
    if 0x2000000<=v<0x2040000: return 'SEC2-4 data'
    if 0x2800000<=v<0x2840000: return 'SEC5 ro'
    if 0x3000000<=v<0x3040000: return 'SEC6-8 PROT'
    if 0x4000000<=v<0x4094000: return 'SEC9 shared'
    if 0xe0000000<=v<0xe0010000: return 'PPB'
    return 'OTHER'
from collections import Counter
b=Counter()
for v,srcs in found.items(): b[cls(v)]+=1
print('\n=== movw/movt built addresses by class ===')
for k,c in b.most_common(): print('  %-14s %d' % (k,c))
print('\n=== OTHER (non-VA, i.e. real SoC/reg addresses) ===')
for v in sorted(found):
    if cls(v) in ('OTHER','PPB'):
        print('  %#010x   from %s' % (v, ','.join('%#x'%a for a,_ in found[v][:3])))
