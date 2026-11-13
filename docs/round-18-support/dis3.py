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
md=Cs(CS_ARCH_ARM, CS_MODE_THUMB|CS_MODE_MCLASS)
def run(start,n=0x40,label=''):
    print('=== %s @ %#x ===' % (label,start)); o=va2off(start)
    if o is None: print(' unmapped'); return
    for ins in md.disasm(d[o:o+n], start):
        e=''
        if '[pc' in ins.op_str:
            m=re.search(r'#(-?0x[0-9a-f]+|-?\d+)', ins.op_str)
            if m:
                la=((ins.address+4)&~3)+int(m.group(1),0); v=read32(la)
                if v is not None: e='   ; %#x = %#010x' % (la,v)
        print('%08x  %-8s %-28s%s' % (ins.address,ins.mnemonic,ins.op_str,e))
        if ins.mnemonic in ('bx','pop','b') and ins.address>start: break
        if ins.mnemonic=='bl': break
run(0x1000ea0,0x60,'0x40022104 writer')
print()
run(0x10005e0,0x50,'0x40034000 user')
