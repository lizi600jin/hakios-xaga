from unicorn import *
from unicorn.arm_const import *
import json,struct
d=open('mali_csffw.bin','rb').read()
info=json.load(open('fw-sections.json')); secs=info['secs']
mu=Uc(UC_ARCH_ARM, UC_MODE_THUMB|UC_MODE_MCLASS)
mu.mem_map(0x1000000, 0x20000)
mu.mem_write(0x1000000, d[0xe94:0xe94+63560])
mu.mem_map(0xE0000000, 0x100000)
mu.mem_map(0x20000000, 0x100000)
mu.reg_write(UC_ARM_REG_SP, 0x2003f000)
mu.reg_write(UC_ARM_REG_R0, 0)
writes=[]
def hp(uc,access,addr,size,value,user):
    if access==UC_MEM_WRITE: writes.append((addr,value,uc.reg_read(UC_ARM_REG_PC)))
def hu(uc,access,addr,size,value,user):
    print('UNMAPPED %s addr=%#x size=%d value=%#x pc=%#x' % ('W' if access==UC_MEM_WRITE else 'R', addr,size,value,uc.reg_read(UC_ARM_REG_PC)))
    return False
mu.hook_add(UC_HOOK_MEM_WRITE, hp)
mu.hook_add(UC_HOOK_MEM_WRITE_UNMAPPED, hu)
mu.hook_add(UC_HOOK_MEM_READ_UNMAPPED, hu)
try:
    mu.emu_start(0x1003126|1, 0x100328e, count=5000)
except UcError as e:
    print('stop:', e, 'pc=%#x' % mu.reg_read(UC_ARM_REG_PC))
print('=== PPB writes ===')
for addr,val,pc in writes:
    if addr==0xE000ED9C: print('  PC %#x RBAR <- %#010x (base %#010x valid %d region %d)'%(pc,val,val&0xffffffe0,(val>>4)&1,val&0xf))
    elif addr==0xE000EDA0:
        s=((val>>1)&0x1f); n=1<<(s+1)
        print('  PC %#x RASR <- %#010x EN=%d SIZE=%s AP=%s XN=%d' % (pc,val,val&1,
            ('%dMB'%(n>>20)) if n>=1<<20 else ('%dKB'%(n>>10)), bin((val>>24)&7),(val>>28)&1))
    elif addr==0xE000ED94: print('  PC %#x MPU_CTRL <- %#x'%(pc,val))
