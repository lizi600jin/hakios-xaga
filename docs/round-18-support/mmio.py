import struct, json, re, collections
from capstone import *
d=open('mali_csffw.bin','rb').read()
info=json.load(open('fw-sections.json')); secs=info['secs']
s1=[s for s in secs if s['va'][0]==0x1000000][0]
code=d[s1['data'][0]:s1['data'][1]]; base=0x1000000
md=Cs(CS_ARCH_ARM, CS_MODE_THUMB|CS_MODE_MCLASS)
md.skipdata=True
regs={}; writes=[]; reads=[]
for ins in md.disasm(code, base):
    m=ins.mnemonic; ops=ins.op_str
    mm=re.match(r'(\w+),\s*#(-?0x[0-9a-fA-F]+|-?\d+)\s*$', ops)
    if m=='movw' and mm:
        regs[mm.group(1)]=[int(mm.group(2),0)&0xffff, ins.address]
        continue
    if m=='movt' and mm and mm.group(1) in regs:
        r=mm.group(1); b0,at=regs.pop(r)
        v=(int(mm.group(2),0)&0xffff)<<16|b0
        regs.setdefault('__full_'+r,[v,at])
        regs[r]=[v,at]
        continue
    # record uses of a fully built base
    for r,(v,at) in list(regs.items()):
        if not r.startswith('__full_'): continue
        rr=r[7:]
        if re.search(r'\b%s\b' % rr, ops):
            if m.startswith('str'):
                writes.append((v, m, ins.address, at))
            elif m.startswith('ldr') and '[' in ops:
                reads.append((v, m, ins.address, at))
            regs.pop(r, None)
def cls(v):
    if 0x40000000<=v<0x40001000: return 'MCU periph 0x40000xxx'
    if 0x40003000<=v<0x40003100: return 'GPU regs via MCU (0x3034=GPU_STATUS)'
    if 0x40020000<=v<0x40030000: return 'MTK MCU periph 0x4002xxxx'
    if 0xe0000000<=v<0xe0010000: return 'PPB'
    return 'other'
print('=== MMIO WRITES by the firmware (movw/movt base) ===')
c=collections.Counter()
for v,m,a,at in writes: c[cls(v)]+=1
for k,n in c.most_common(): print('  %-40s %d' % (k,n))
print()
seen=set()
for v,m,a,at in sorted(writes):
    key=(v,m)
    if key in seen: continue
    seen.add(key)
    print('  write %#010x  (%s @%#x, base built @%#x)  [%s]' % (v,m,a,at,cls(v)))
print()
print('=== MMIO READS ===')
seen=set()
for v,m,a,at in sorted(reads):
    key=(v,m)
    if key in seen: continue
    seen.add(key); print('  read  %#010x  (%s @%#x)  [%s]' % (v,m,a,cls(v)))
