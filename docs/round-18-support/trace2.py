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
md=Cs(CS_ARCH_ARM, CS_MODE_THUMB|CS_MODE_MCLASS); md.detail=True
work=[0x1002aec]; seen=set(); steps=0
while work and steps<40:
    a=work.pop(0)
    if a in seen: continue
    seen.add(a); steps+=1
    o=va2off(a)
    if o is None: print('=== %#x UNMAPPED' % a); continue
    print('=== block %#x ===' % a)
    for ins in md.disasm(d[o:o+0x120], a):
        extra=''
        if '[pc' in ins.op_str:
            m=re.search(r'#(-?0x[0-9a-f]+|-?\d+)', ins.op_str)
            if m:
                la=((ins.address+4)&~3)+int(m.group(1),0)
                v=read32(la)
                if v is not None:
                    tag=''
                    if 0x03000000<=v<0x03040000: tag='  <== PROT'
                    elif 0x04000000<=v<0x04094000: tag='  <== SHARED'
                    elif 0x02000000<=v<0x02040000: tag='  <== RW'
                    extra='   ; %#x = %#010x%s' % (la, v, tag)
        print('%08x  %-8s %-26s%s' % (ins.address, ins.mnemonic, ins.op_str, extra))
        if ins.mnemonic=='bl' and ins.op_str.startswith('#'):
            work.append(int(ins.op_str[1:],16))
        if ins.mnemonic in ('bx','b','pop'):
            if ins.mnemonic=='b' and ins.op_str.startswith('#'):
                t=int(ins.op_str[1:],16)
                if t!=ins.address+ins.size: work.insert(0,int(ins.op_str[1:],16))
            break
