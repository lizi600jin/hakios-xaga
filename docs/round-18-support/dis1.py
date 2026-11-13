import struct, json
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

md=Cs(CS_ARCH_ARM, CS_MODE_THUMB | CS_MODE_MCLASS)
md.detail=True
start=0x10018ec
off=va2off(start)
code=d[off:off+0x400]
print('=== linear disasm from entry 0x%x ===' % start)
for ins in md.disasm(code, start):
    txt='%08x  %-8s %s' % (ins.address, ins.mnemonic, ins.op_str)
    extra=''
    if ins.mnemonic in ('ldr','ldr.w') and '[pc' in ins.op_str:
        # compute literal addr
        try:
            imm=int(ins.op_str.split('#')[-1].rstrip(']'),0)
        except Exception:
            imm=None
        if imm is not None:
            la=(ins.address+4)&~3
            la=la+imm if imm>=0 else la+imm
            v=read32(la)
            extra='   ; lit @%#x = %s' % (la, ('%#010x'%v) if v is not None else '??')
    print(txt+extra)
