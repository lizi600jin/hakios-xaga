import struct, json
d=open('mali_csffw.bin','rb').read()
info=json.load(open('fw-sections.json')); secs=info['secs']
def va2off(va):
    for s in secs:
        vs,ve=s['va']; ds,de=s['data']
        if vs<=va<ve and de>ds and (va-vs)<(de-ds): return ds+(va-vs)
    return None
print('0x03800000 covered by a section?', va2off(0x03800000) is not None)
for s in secs:
    if s['va'][0] <= 0x03800000 < s['va'][1]: print('  -> in section', hex(s['va'][0]), hex(s['va'][1]))
print('0x0302bc78 covered?', va2off(0x302bc78) is not None)
print('0x0202c79c covered?', va2off(0x202c79c) is not None)
print()
print('GPU_STATUS bit7 (PROTM_ACTIVE) per 692 log: line245 "protected 0"')
print('=> cbz r4 at 0x1001932 taken -> 0x1001982 -> b.w 0x1002aec')
print('=> 0x1001934 (ldrb 0x0302bc78) and the 0x03800000->0x02000000 copy are NOT executed')
