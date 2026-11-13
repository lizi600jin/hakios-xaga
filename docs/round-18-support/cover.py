import struct, json
from capstone import *
d=open('mali_csffw.bin','rb').read()
info=json.load(open('fw-sections.json')); secs=info['secs']
s1=[s for s in secs if s['va'][0]==0x1000000][0]
ds,de=s1['data']; code=d[ds:de]
md=Cs(CS_ARCH_ARM, CS_MODE_THUMB|CS_MODE_MCLASS)
insns=list(md.disasm(code, 0x1000000))
print('count',len(insns),'first %#x last %#x' % (insns[0].address, insns[-1].address))
print('expected end %#x' % (0x1000000+len(code)))
print('stopped at', hex(insns[-1].address+insns[-1].size))
print('bytes at stop:', code[insns[-1].address+insns[-1].size-0x1000000:][:16].hex())
