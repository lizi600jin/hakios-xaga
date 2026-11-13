import json,struct
d=open('mali_csffw.bin','rb').read()
info=json.load(open('fw-sections.json')); secs=info['secs']
def va2off(va):
    for s in secs:
        vs,ve=s['va']; ds,de=s['data']
        if vs<=va<ve and de>ds and (va-vs)<(de-ds): return ds+(va-vs)
    return None
def r32(va):
    o=va2off(va); return None if o is None else struct.unpack_from('<I',d,o)[0]

print('=== 向量表 @VA 0 (sec0, 2836 bytes 文件数据) ===')
names={0:'Initial SP',1:'Reset',2:'NMI',3:'HardFault',4:'MemManage',5:'BusFault',6:'UsageFault',
       11:'SVCall',12:'DebugMon',14:'PendSV',15:'SysTick'}
for i in range(16):
    v=r32(i*4)
    if v is None: break
    print('  [%2d] %-12s %#010x' % (i, names.get(i,'IRQ%d'%(i-16)), v))
print()
print('前 16 个外设 IRQ 向量:')
for i in range(16,32):
    v=r32(i*4)
    if v is None: break
    print('  [%2d] IRQ%-3d %#010x' % (i,i-16,v))
