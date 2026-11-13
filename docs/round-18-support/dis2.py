import struct, json, sys
from capstone import *
d=open('mali_csffw.bin','rb').read()
info=json.load(open('fw-sections.json')); secs=info['secs']
def va2off(va):
    for s in secs:
        vs,ve=s['va']; ds,de=s['data']
        if vs<=va<ve and de>ds and (va-vs)<(de-ds):
            return ds+(va-vs)
    return None
def read32(va):
    o=va2off(va)
    return None if o is None else struct.unpack_from('<I',d,o)[0]
md=Cs(CS_ARCH_ARM, CS_MODE_THUMB|CS_MODE_MCLASS); md.detail=True
def run(start, n=0x60, label=''):
    print('=== %s @ %#x ===' % (label,start))
    o=va2off(start)
    if o is None: print('  unmapped'); return
    for ins in md.disasm(d[o:o+n], start):
        extra=''
        if '[pc' in ins.op_str:
            import re
            m=re.search(r'#(-?0x[0-9a-f]+|-?\d+)', ins.op_str)
            if m:
                imm=int(m.group(1),0)
                la=((ins.address+4)&~3)+imm
                v=read32(la)
                extra='   ; %#x = %s' % (la, ('%#010x'%v) if v is not None else '??')
        print('%08x  %-8s %-24s%s' % (ins.address, ins.mnemonic, ins.op_str, extra))
        if ins.mnemonic.startswith('b') and ins.mnemonic not in ('bic','bfc','bfi') and ins.address>start: break
for a,l in [(0x10019d8,'fn A'),(0x10019e2,'fn B'),(0x1001a2a,'fn C'),(0x1003126,'fn D')]:
    run(a, 0x80, l); print()
