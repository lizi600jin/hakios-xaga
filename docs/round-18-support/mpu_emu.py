"""Symbolically execute fn D @0x1003126 to get the real MPU region table."""
import json,struct
from capstone import *
d=open('mali_csffw.bin','rb').read()
info=json.load(open('fw-sections.json')); secs=info['secs']
def va2off(va):
    for s in secs:
        vs,ve=s['va']; ds,de=s['data']
        if vs<=va<ve and de>ds and (va-vs)<(de-ds): return ds+(va-vs)
    return None
md=Cs(CS_ARCH_ARM, CS_MODE_THUMB|CS_MODE_MCLASS)
o=va2off(0x1003126)
insns=list(md.disasm(d[o:o+0x170], 0x1003126))
R={'r0':0,'r1':0,'r2':0,'r3':0,'r4':0,'r5':0,'r6':0,'r7':0,'r8':0,'sb':0,'sl':0,'fp':0,'ip':0,'lr':0,'sp':0}
M=0xffffffff
regions=[]
def rb(v):
    return dict(base=v&0xffffffe0, valid=(v>>4)&1, region=v&0xf)
def rasr(v):
    size=((v>>1)&0x1f)
    return dict(en=v&1, bytes=1<<(size+1), AP=(v>>24)&7, XN=(v>>28)&1,
                TEX=(v>>19)&7, C=(v>>17)&1, B=(v>>18)&1, S=(v>>16)&1)
def clz(x):
    x&=M
    return 32 if x==0 else 32-x.bit_length()
last_rbar=None
for ins in insns:
    m,ops=ins.mnemonic,ins.op_str
    def parse_op(t):
        t=t.strip()
        if t in R: return ('r',t)
        if t.startswith('#'):
            return ('i',int(t[1:],0)&M)
        if t.startswith('['):
            return ('m',t)
        return ('?',t)
    if m=='movw' and ops.count(',')==1:
        r,i=ops.split(','); i=i.strip()
        if i.startswith('#'):
            R[r.strip()]=(int(i[1:],0)&0xffff)|(R.get(r.strip(),0)&0xffff0000)
        continue
    if m=='movt' and ops.count(',')==1:
        r,i=ops.split(','); i=i.strip()
        if i.startswith('#'):
            R[r.strip()]=((int(i[1:],0)&0xffff)<<16)|(R.get(r.strip(),0)&0xffff)
        continue
    if m=='movs' and ops.count(',')==1:
        r,i=ops.split(','); R[r.strip()]=int(i.strip()[1:],0)&M; continue
    if m=='mov' and ops.count(',')==1:
        a,b=[x.strip() for x in ops.split(',')]
        R[a]=R.get(b,0) if b in R else int(b[1:],0)&M; continue
    if m=='adds' and ops.count(',')==1:
        r,i=ops.split(','); R[r.strip()]=(R[r.strip()]+int(i.strip()[1:],0))&M; continue
    if m=='add' and ops.count(',')==1:
        r,i=ops.split(','); R[r.strip()]=(R[r.strip()]+int(i.strip()[1:],0))&M; continue
    if m.startswith('add.w') or m=='add':
        parts=[x.strip() for x in ops.split(',')]
        if len(parts)==2 and parts[1].startswith('#'):
            R[parts[0]]=(R.get(parts[0],0)+int(parts[1][1:],0))&M
        elif len(parts)==3:
            a,b,c=parts
            v=R.get(b,0) if b in R else int(b[1:],0)&M
            if c.startswith('#'): v=(v+int(c[1:],0))&M
            else:
                sh,amt=c.split(','); amt=int(amt.strip().lstrip('#'),0)
                vv=R.get(sh,0)
                v=(vv+(vv<<amt))&M if 'lsl' in c else v
            R[a]=v&M
        continue
    if m.startswith('sub.w') or m=='sub':
        parts=[x.strip() for x in ops.split(',')]
        a,b,c=parts
        v=R.get(b,0) if b in R else int(b[1:],0)&M
        if c.startswith('#'): v=(v-int(c[1:],0))&M
        else:
            sh,amt=c.split(','); amt=int(amt.strip().lstrip('#'),0)
            v=(v-(R.get(sh,0)<<amt))&M
        R[a]=v; continue
    if m=='clz':
        a,b=[x.strip() for x in ops.split(',')]; R[a]=clz(R.get(b,0)); continue
    if m=='and' or m=='ands':
        parts=[x.strip() for x in ops.split(',')]
        if len(parts)==2: R[parts[0]]=R[parts[0]]&int(parts[1][1:],0)&M
        continue
    if m.startswith('orr'):
        parts=[x.strip() for x in ops.split(',')]
        if len(parts)==2: R[parts[0]]=(R[parts[0]]|int(parts[1][1:],0))&M
        continue
    if m=='str' and ops.startswith('r') and '[r1' in ops:
        val=int(ops.split(',')[0].strip()[1:])
        off=0
        if '#' in ops:
            off=int(ops.split('#')[1].rstrip(']'),0)
        v=R.get('r%d'%val,0)
        if off==0: last_rbar=rb(v); print('  RBAR = %#010x -> base %#010x valid %d region %d' % (v,last_rbar['base'],last_rbar['valid'],last_rbar['region']))
        elif off==4:
            r=rasr(v)
            regions.append((last_rbar,r,v))
            print('  RASR = %#010x -> EN=%d SIZE=%s AP=%s XN=%d TEX=%d C=%d B=%d S=%d' %
                  (v,r['en'],('%dKB'%(r['bytes']//1024)) if r['bytes']>=1024 else ('%dB'%r['bytes']),
                   bin(r['AP']),r['XN'],r['TEX'],r['C'],r['B'],r['S']))
        elif off==-8:
            print('  MPU_CTRL (0xE000ED94) = %#x  (ENABLE=%d PRIVDEFENA=%d)' % (v,v&1,(v>>1)&1))
    if m in ('beq','bne'):
        continue
    if m in ('dsb','isb','push','pop','cmp','ite','itttt','b','cbz'):
        if m.startswith('pop'): break
        continue
print()
print('=== final region table (RBAR base / size / AP / XN) ===')
for b,r,v in regions:
    print('  region %-2d base %#010x size %-9s AP=%s XN=%d' % (b['region'],b['base'],
        ('%dKB'%(r['bytes']//1024)) if r['bytes']>=1024 else ('%dB'%r['bytes']), bin(r['AP']),r['XN']))
