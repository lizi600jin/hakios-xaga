import struct, json, re
from capstone import *
d=open('mali_csffw.bin','rb').read()
info=json.load(open('fw-sections.json')); secs=info['secs']
def va2off(va):
    for s in secs:
        vs,ve=s['va']; ds,de=s['data']
        if vs<=va<ve and de>ds and (va-vs)<(de-ds): return ds+(va-vs)
    return None
def r32(va):
    o=va2off(va); return None if o is None else struct.unpack_from('<I',d,o)[0]
md=Cs(CS_ARCH_ARM, CS_MODE_THUMB|CS_MODE_MCLASS)
for start in (0x100b3e0, 0x1001bf0, 0x1001c00):
    o=va2off(start)
    print('=== @%#x ===' % start)
    for ins in md.disasm(d[o:o+0x50], start):
        e=''
        if '[pc' in ins.op_str:
            m=re.search(r'#(-?0x[0-9a-f]+|-?\d+)', ins.op_str)
            if m:
                la=((ins.address+4)&~3)+int(m.group(1),0); v=r32(la)
                if v is not None: e='   ; %#x = %#010x' % (la,v)
        print('%08x  %-8s %-24s%s' % (ins.address,ins.mnemonic,ins.op_str,e))
        if ins.mnemonic in ('bx','pop') : break
