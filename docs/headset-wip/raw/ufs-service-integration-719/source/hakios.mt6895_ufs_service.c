/* Trial719: bounded three-request read-only snapshot service, not a whole-disk provider. */
#ifdef US_HOST_TEST
#include "ufs_service_test_env.h"
#else
#include <stdint.h>
#include <stdbool.h>
#include <mach/mach_types.h>
#include <machine/machine_routines.h>
#include <kern/clock.h>
#include <kern/kalloc.h>
#include <kern/locks.h>
#include <kern/thread.h>
#include <pexpert/pexpert.h>
#include <sys/conf.h>
#include <sys/errno.h>
#include <sys/fcntl.h>
#include <sys/param.h>
#include <sys/proc.h>
#include <sys/systm.h>
#include <sys/uio.h>
#include <sys/uio_internal.h>
#include <sys/vnode.h>
#include <sys/vnode_if.h>
#include <sys/vnode_internal.h>
#include <miscfs/devfs/devfs.h>
#include <vm/vm_kern.h>
#include <vm/memory_types.h>
#endif
#include "ufs_service_engine.h"
#include "ufs_service_frontend.h"

#ifdef HAKIOS_UFS
#error The legacy native UFS driver conflicts with the inherited service queue
#endif

extern vm_offset_t ml_io_map(vm_offset_t, vm_size_t);
extern vm_offset_t ml_io_map_unmappable(vm_offset_t, vm_size_t, unsigned int);
extern boolean_t pmap_valid_address(uint64_t);
extern unsigned long gPhysBase, gPhysSize;
extern kern_return_t thread_terminate(thread_t);
#define US_REG_PA 0x112b0000u
#define US_REG_BYTES 0x2300u
#define US_SCRATCH_BYTES ((vm_size_t)(UR_DATA_PA + UR_BLOCK_BYTES - UR_LEASE_PA))
#define US_REFERENCE_CRC 0x28a2836cu
#define US_MAX_REQUESTS 3u
#define US_NODE_PATH "/dev/mt6895-ufs-lba1"

struct us_platform { vm_offset_t regs, table, scratch; };
static struct us_platform platform;
static bool service_publish(void *, uint32_t);
/* Never reset this cached control in init; quiesce may already have set STOP. */
static struct us_lease lease = {0,service_publish,&platform};
static struct ur_workspace workspace;
static struct ur_outcome outcome;
static LCK_GRP_DECLARE(service_group,"mt6895-ufs-service");
static LCK_MTX_DECLARE(service_lock,&service_group);
static uint32_t ready, requests, issued, delivered;
static int service_major = -1;
struct service_record {
    uint32_t ready, result, engine, claimed, issued, restored, delivered, control, shared, polls;
    uint32_t ocs, response_valid, response_attempted, response_failures, crc;
    uint32_t response[UR_RESPONSE_WORDS], diagnostic_valid, diagnostic_crc;
};
static struct service_record records[US_MAX_REQUESTS];

static void service_barrier(void *unused)
{
    (void)unused;
#ifdef US_HOST_TEST
    us_host_barrier();
#else
    __asm__ volatile("dsb sy" ::: "memory");
#endif
}
static volatile struct ur_lease *shared(struct us_platform *p)
{ return (volatile struct ur_lease *)(uintptr_t)p->scratch; }
static struct ur_lease shared_snapshot(struct us_platform *p)
{
    volatile struct ur_lease *r=shared(p);
    struct ur_lease result={r->magic,r->version,r->trial,r->state};
    service_barrier(NULL);return result;
}
static bool service_publish(void *context,uint32_t state)
{
    struct us_platform *p=context;
    if (!p->scratch) return false;
    shared(p)->state=state;
    service_barrier(NULL);
    return shared(p)->state==state;
}
static bool service_read_reg(void *context,uint16_t offset,uint32_t *out)
{
    struct us_platform *p=context;
    if (!p->regs || !out) return false;
    switch(offset) {
    case 0:case 8:case 0x30:case 0x34:case 0x50:case 0x54:case 0x58:case 0x60:
        *out=*(const volatile uint32_t *)(p->regs+offset);return true;
    default:return false;
    }
}
static volatile uint32_t *service_word(struct us_platform *p,enum ur_memory region,uint32_t off)
{
    vm_offset_t base;uint32_t bytes;
    switch(region) {
    case UR_TABLE:base=p->table;bytes=32;break;
    case UR_UCD:base=p->scratch?p->scratch+(UR_UCD_PA-UR_LEASE_PA):0;bytes=UR_UCD_BYTES;break;
    case UR_DATA:base=p->scratch?p->scratch+(UR_DATA_PA-UR_LEASE_PA):0;bytes=UR_BLOCK_BYTES;break;
    default:return NULL;
    }
    if (!base || (off&3) || off>bytes-4) return NULL;
    return (volatile uint32_t *)(base+off);
}
static bool service_read_word(void *context,enum ur_memory region,uint32_t off,uint32_t *out)
{
    volatile uint32_t *word=service_word(context,region,off);
    if (!word || !out) return false;
    *out=*word;return true;
}
static bool service_write_word(void *context,enum ur_memory region,uint32_t off,uint32_t value)
{
    volatile uint32_t *word=service_word(context,region,off);
    if (!word || us_control(&lease)!=US_ACTIVE) return false;
    *word=value;return true;
}
static bool service_claim(void *context)
{
    struct ur_lease r=shared_snapshot(context);
    return us_control(&lease)==US_ACTIVE && ur_lease_valid(&r) && r.state==UR_CLAIMED;
}
static bool service_finish(void *unused,bool restored)
{
    (void)unused;
    if (!restored) { us_poison(&lease);return false; }
    return us_mark_drained(&lease);
}
static bool service_stopped(void *unused)
{ (void)unused;return us_stopped(&lease); }
static bool service_ring(void *context)
{
    struct us_platform *p=context;
    if (us_control(&lease)!=US_ACTIVE || shared(p)->state!=UR_CLAIMED) return false;
    *(volatile uint32_t *)(p->regs+0x58)=1u; /* sole controller store, one per admitted READ */
    service_barrier(NULL);return true;
}
static uint64_t service_now_us(void *unused)
{
    uint64_t ns;(void)unused;
    absolutetime_to_nanoseconds(mach_absolute_time(),&ns);return ns/1000;
}
static struct ur_io service_io(void)
{
    return (struct ur_io){service_read_reg,service_read_word,service_write_word,service_claim,
        service_finish,service_stopped,service_ring,service_barrier,service_now_us,&platform};
}
static bool service_deliver(void *destination,const uint8_t *data,size_t bytes)
{
    if (!destination || bytes!=UR_BLOCK_BYTES) return false;
    memcpy(destination,data,bytes);return true; /* cached private kernel buffer, never uiomove */
}
static bool clean_baseline(const struct ur_io *io)
{
    return ur_idle(io) && ur_copy_from(io,UR_TABLE,workspace.table,8) &&
        ur_copy_from(io,UR_UCD,workspace.ucd,UR_UCD_BYTES/4) && ur_previous_read(&workspace) &&
        ur_copy_from(io,UR_DATA,workspace.data,UR_BLOCK_BYTES/4) &&
        ur_same(io,UR_TABLE,workspace.table,8) && ur_same(io,UR_UCD,workspace.ucd,UR_UCD_BYTES/4) &&
        ur_same(io,UR_DATA,workspace.data,UR_BLOCK_BYTES/4) && ur_idle(io);
}

static int service_copy_block(uint8_t *destination)
{
    const struct ur_request request={2,1,1,UR_BLOCK_BYTES};
    struct ur_io io=service_io();
    enum us_read_result result;
    int error;
    lck_mtx_lock(&service_lock);
    if (us_stopped(&lease)) { lck_mtx_unlock(&service_lock);return ENXIO; }
    if (requests>=US_MAX_REQUESTS) { lck_mtx_unlock(&service_lock);return EAGAIN; }
    uint32_t index=requests++; /* global budget includes failed attempts; never reset */
    memset(&outcome,0,sizeof(outcome));
    result=us_read_fixed(&lease,&io,&workspace,&outcome,&request,service_deliver,destination,
                         UR_BLOCK_BYTES,US_REFERENCE_CRC,500000u,1000000u);
    if (outcome.issued) issued++;
    if (result==US_READ_OK) delivered++;
    error=result==US_READ_OK?0:result==US_STOPPED?ENXIO:result==US_BUSY?EBUSY:EIO;
    struct service_record *record=&records[index];
    record->result=result;record->engine=outcome.result;record->claimed=outcome.claimed;
    record->issued=outcome.issued;record->restored=outcome.restored;record->delivered=result==US_READ_OK;
    record->control=us_control(&lease);record->shared=shared_snapshot(&platform).state;
    record->polls=outcome.polls;record->ocs=outcome.ocs;record->response_valid=outcome.response_valid;
    record->response_attempted=outcome.response_attempted;record->response_failures=outcome.response_failures;
    record->crc=outcome.crc32;
    record->diagnostic_valid=outcome.diagnostic_valid;record->diagnostic_crc=outcome.diagnostic_crc32;
    for (unsigned i=0;i<UR_RESPONSE_WORDS;i++) record->response[i]=ur_le32(outcome.response+i*4);
    __atomic_store_n(&record->ready,1,__ATOMIC_RELEASE);
    lck_mtx_unlock(&service_lock);
    return error;
}
static uint32_t service_request_count(void)
{
    lck_mtx_lock(&service_lock);uint32_t n=requests;lck_mtx_unlock(&service_lock);return n;
}
static void service_report_records(uint32_t *reported)
{
    while (*reported<US_MAX_REQUESTS && !us_stopped(&lease)) {
        struct service_record *r=&records[*reported];
        if (!__atomic_load_n(&r->ready,__ATOMIC_ACQUIRE)) break;
        uint32_t id=++*reported;
        printf("mt6895-ufs-service: request=%u result=%u engine=%u claimed=%u issued=%u memory_restored=%u delivered=%u control=%#x shared=%u polls=%u\n",
               id,r->result,r->engine,r->claimed,r->issued,r->restored,r->delivered,r->control,r->shared,r->polls);
        if (r->issued && r->engine>=UR_COMPLETED_ERROR && r->engine<=UR_READ_OK)
            printf("mt6895-ufs-service: request=%u ocs=%#x response_valid=%#x attempted=%#x failures=%#x\n",
                   id,r->ocs,r->response_valid,r->response_attempted,r->response_failures);
        if (r->response_valid==UR_RESPONSE_VALID_MASK) {
            printf("mt6895-ufs-service: request=%u response_words",id);
            for (unsigned i=0;i<UR_RESPONSE_WORDS;i++) printf(" %08x",r->response[i]);
            printf("\n");
        }
        if (r->diagnostic_valid)
            printf("mt6895-ufs-service: request=%u diagnostic_crc32=%#x expected=%#x bytes=%u accepted=%u\n",
                   id,r->diagnostic_crc,US_REFERENCE_CRC,UR_BLOCK_BYTES,r->delivered);
        if (r->engine==UR_READ_OK || r->engine==UR_REFERENCE_MISMATCH)
            printf("mt6895-ufs-service: request=%u block_crc32=%#x expected=%#x bytes=%u accepted=%u\n",
                   id,r->crc,US_REFERENCE_CRC,UR_BLOCK_BYTES,r->delivered);
    }
}
static int service_dev_open(dev_t dev,int flags,__unused int devtype,__unused struct proc *p)
{
    if (flags&FWRITE) return EROFS;
    return minor(dev)==0 && __atomic_load_n(&ready,__ATOMIC_ACQUIRE) && !us_stopped(&lease)?0:ENXIO;
}
static int service_dev_close(__unused dev_t dev,__unused int flags,__unused int devtype,__unused struct proc *p)
{ return 0; }
static int service_dev_write(__unused dev_t dev,__unused struct uio *uio,__unused int flags)
{ return EROFS; }
static int service_dev_read(dev_t dev,struct uio *uio,__unused int flags)
{
    struct us_read_plan plan;
    enum us_plan_result action=us_plan_read(uio_offset(uio),uio_resid(uio),&plan);
    if (minor(dev)!=0 || !__atomic_load_n(&ready,__ATOMIC_ACQUIRE)) return ENXIO;
    if (action==US_PLAN_INVALID) return EINVAL;
    if (action==US_PLAN_EOF) return 0;
    if (us_stopped(&lease)) return ENXIO;
    uint8_t *copy=kalloc_data(UR_BLOCK_BYTES,Z_WAITOK);
    if (!copy) return ENOMEM;
    int error=service_copy_block(copy);
    if (!error) error=uiomove((caddr_t)(copy+plan.offset),(int)plan.bytes,uio);
    kfree_data(copy,UR_BLOCK_BYTES);
    return error;
}
static const struct cdevsw service_cdevsw={
    .d_open=service_dev_open,.d_close=service_dev_close,.d_read=service_dev_read,.d_write=service_dev_write,
    .d_ioctl=eno_ioctl,.d_stop=eno_stop,.d_reset=eno_reset,.d_ttys=NULL,.d_select=eno_select,
    .d_mmap=eno_mmap,.d_strategy=eno_strat,.d_reserved_1=eno_getc,.d_reserved_2=eno_putc,.d_type=0
};

static void service_selftest(void *unused,wait_result_t result)
{
    vnode_t node=NULL;
    uint8_t *copy=NULL;
    vfs_context_t ctx=vfs_context_kernel();
    bool opened=false;
    int error=0;
    uint32_t reported=0;
    (void)unused;(void)result;
    delay_for_interval(2000,NSEC_PER_MSEC);
    if (us_stopped(&lease)) goto done;
    /* The registration API recommends dynamic allocation starting at24. Refuse observed replacement. */
    if (service_major<0 || cdevsw[service_major].d_read!=service_dev_read) {
        printf("mt6895-ufs-service: selftest dispatch replaced; no request\n");goto failed;
    }
    error=vnode_lookup(US_NODE_PATH,VNODE_LOOKUP_NOFOLLOW,&node,ctx);
    printf("mt6895-ufs-service: vnode lookup error=%d\n",error);
    if (error) goto failed;
    if (vnode_vtype(node)!=VCHR || vnode_specrdev(node)!=makedev(service_major,0)) {
        error=ENXIO;goto failed;
    }
    uint32_t before=service_request_count();
    error=VNOP_OPEN(node,FWRITE,ctx);
    printf("mt6895-ufs-service: write-open error=%d expected=%d requests_before=%u requests_after=%u\n",
           error,EROFS,before,service_request_count());
    if (!error) (void)VNOP_CLOSE(node,FWRITE,ctx);
    if (error!=EROFS) goto failed;
    error=VNOP_OPEN(node,FREAD,ctx);
    if (error) goto failed;
    opened=true;
    copy=kalloc_data(UR_BLOCK_BYTES,Z_WAITOK);
    if (!copy) goto failed;
    for (uint32_t i=0;i<US_MAX_REQUESTS;i++) {
        int residual=0;
        if (us_stopped(&lease)) goto done;
        if (cdevsw[service_major].d_read!=service_dev_read) goto failed;
        memset(copy,0x66,UR_BLOCK_BYTES);
        error=vn_rdwr(UIO_READ,node,(caddr_t)copy,UR_BLOCK_BYTES,0,UIO_SYSSPACE,IO_NOCACHE,
                      vfs_context_ucred(ctx),&residual,vfs_context_proc(ctx));
        if (us_stopped(&lease)) goto done;
        service_report_records(&reported);
        uint32_t crc=ur_crc32(copy,UR_BLOCK_BYTES);
        printf("mt6895-ufs-service: vfs read=%u error=%d residual=%d crc32=%#x expected=%#x\n",
               i+1,error,residual,crc,US_REFERENCE_CRC);
        if (error || residual || crc!=US_REFERENCE_CRC) { if (!error) error=EIO;goto failed; }
        if (i+1<US_MAX_REQUESTS) delay_for_interval(2000,NSEC_PER_MSEC);
    }
    {
        int residual=0;uint32_t count=service_request_count();
        error=vn_rdwr(UIO_READ,node,(caddr_t)copy,4,UR_BLOCK_BYTES,UIO_SYSSPACE,IO_NOCACHE,
                      vfs_context_ucred(ctx),&residual,vfs_context_proc(ctx));
        if (us_stopped(&lease)) goto done;
        printf("mt6895-ufs-service: vfs eof error=%d residual=%d requests_before=%u requests_after=%u\n",
               error,residual,count,service_request_count());
        if (error || residual!=4 || count!=service_request_count()) goto failed;
    }
    lck_mtx_lock(&service_lock);
    if (!us_stopped(&lease))
        printf("mt6895-ufs-service: selftest complete requests=%u issued=%u delivered=%u control=%#x shared=%u; idle close deferred to quiesce\n",
               requests,issued,delivered,us_control(&lease),shared_snapshot(&platform).state);
    lck_mtx_unlock(&service_lock);
    goto done;
failed:
    printf("mt6895-ufs-service: selftest failed error=%d; close service without retry\n",error);
    us_stop(&lease);
done:
    if (copy) kfree_data(copy,UR_BLOCK_BYTES);
    if (opened) (void)VNOP_CLOSE(node,FREAD,ctx);
    if (node) vnode_put(node);
    thread_terminate(current_thread());
}

void hakios_mt6895_ufs_service_init(void);
void hakios_mt6895_ufs_service_quiesce(void);
void hakios_mt6895_ufs_service_quiesce(void)
{ us_stop(&lease); } /* only cached atomic control and aligned shared-DRAM publication */

void hakios_mt6895_ufs_service_init(void)
{
    uint32_t mode=0,old_read=0,old_probe=0;
    if (!PE_parse_boot_argn("mtk_ufs_service",&mode,sizeof(mode)) || !mode) return;
    (void)PE_parse_boot_argn("mtk_ufs_read",&old_read,sizeof(old_read));
    (void)PE_parse_boot_argn("mtk_ufs_probe",&old_probe,sizeof(old_probe));
    if (mode!=1 || old_read || old_probe) {
        printf("mt6895-ufs-service: invalid or competing mode; no access\n");return;
    }
    if (us_control(&lease)!=US_NEW) return;
    printf("mt6895-ufs-service: bounded readonly LUN2 LBA1 bytes4096 budget3; guarded handoff required\n");
    if (!ur_outside_vm(UR_LIST_PA,32,gPhysBase,gPhysSize) ||
        !ur_outside_vm(UR_LEASE_PA,US_SCRATCH_BYTES,gPhysBase,gPhysSize) ||
        pmap_valid_address(UR_LIST_PA) || pmap_valid_address(UR_LIST_PA+31) ||
        pmap_valid_address(UR_LEASE_PA) || pmap_valid_address(UR_DATA_PA+UR_BLOCK_BYTES-1)) {
        printf("mt6895-ufs-service: scratch overlaps managed memory; no mapping\n");return;
    }
    platform.scratch=ml_io_map_unmappable(UR_LEASE_PA,US_SCRATCH_BYTES,VM_WIMG_WCOMB);
    if (!platform.scratch) return;
    struct ur_lease initial=shared_snapshot(&platform);
    printf("mt6895-ufs-service: guard magic=%#x version=%u trial=%u state=%u\n",
           initial.magic,initial.version,initial.trial,initial.state);
    if (!ur_lease_valid(&initial) || initial.state!=UR_UNUSED || us_stopped(&lease)) return;
    platform.table=ml_io_map_unmappable(UR_LIST_PA&~(uint64_t)PAGE_MASK,PAGE_SIZE,VM_WIMG_WCOMB);
    if (!platform.table) return;
    platform.table+=UR_LIST_PA&PAGE_MASK;
    platform.regs=ml_io_map(US_REG_PA,US_REG_BYTES);
    if (!platform.regs || us_stopped(&lease)) return;
    struct ur_io io=service_io();
    if (!clean_baseline(&io) || !us_open(&lease,true)) {
        printf("mt6895-ufs-service: baseline/open refused; no request\n");return;
    }
    printf("mt6895-ufs-service: opened baseline_verified=1 control=%#x shared=%u\n",
           us_control(&lease),shared_snapshot(&platform).state);
    service_major=cdevsw_add(-24,&service_cdevsw);
    if (service_major<0 || devfs_make_node(makedev(service_major,0),DEVFS_CHAR,UID_ROOT,GID_WHEEL,
                                         0400,"mt6895-ufs-lba1")==NULL) {
        if (service_major>=0) (void)cdevsw_remove(service_major,&service_cdevsw);
        printf("mt6895-ufs-service: node registration failed; close without requests\n");
        us_stop(&lease);return;
    }
    __atomic_store_n(&ready,1,__ATOMIC_RELEASE);
    printf("mt6895-ufs-service: node registered path=%s major=%d mode=0400 snapshot_bytes4096\n",
           US_NODE_PATH,service_major);
    thread_t thread;
    if (kernel_thread_start(service_selftest,NULL,&thread)!=KERN_SUCCESS) {
        printf("mt6895-ufs-service: selftest thread failed; close without requests\n");
        us_stop(&lease);return;
    }
    thread_deallocate(thread);
}
