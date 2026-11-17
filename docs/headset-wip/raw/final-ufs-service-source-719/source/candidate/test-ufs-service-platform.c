#define _DEFAULT_SOURCE
#define US_HOST_TEST 1
#include "hakios.mt6895_ufs_service.c"

struct cdevsw cdevsw[256];
unsigned long gPhysBase=UINT64_C(0x100000000),gPhysSize=UINT64_C(0x100000000);
static uint32_t fake_regs[US_REG_BYTES/4];
static uint8_t fake_page[PAGE_SIZE] __attribute__((aligned(PAGE_SIZE)));
static uint8_t fake_scratch[US_SCRATCH_BYTES] __attribute__((aligned(PAGE_SIZE)));
static uint8_t reference[UR_BLOCK_BYTES];
static unsigned maps,commands,allocations,readers,write_opens,terminates;
static bool reg_fail,thread_fail,stop_ring,bad_response;
static uint32_t mode=1,read_mode,probe_mode;
static bool mode_present=true;
static void (*worker)(void *,wait_result_t);
static struct vnode fake_node;
static uint64_t ticks;
static bool dma_seen;
static unsigned checks;
#define CHECK(x) do { assert(x);checks++; } while (0)
int us_eno(void) { return ENODEV; }
void lck_mtx_lock(int *p) { assert(*p==0);*p=1; }
void lck_mtx_unlock(int *p) { assert(*p==1);*p=0; }
void *kalloc_data(size_t n,int flags) { (void)flags;allocations++;return malloc(n); }
void kfree_data(void *p,size_t n) { (void)n;assert(allocations);allocations--;free(p); }
int64_t uio_offset(struct uio *u) { return u->offset; }
int64_t uio_resid(struct uio *u) { return u->residual; }
int uiomove(caddr_t p,int n,struct uio *u)
{
    assert(n>=0 && n<=u->residual);memcpy(u->base+u->moved,p,(size_t)n);
    u->moved+=(size_t)n;u->offset+=n;u->residual-=n;return 0;
}
int cdevsw_add(int start,const struct cdevsw *s)
{ assert(start==-24);if(reg_fail)return -1;cdevsw[24]=*s;return 24; }
int cdevsw_remove(int index,const struct cdevsw *s)
{ assert(index==24 && cdevsw[index].d_read==s->d_read);memset(&cdevsw[index],0,sizeof(*s));return index; }
void *devfs_make_node(dev_t dev,int kind,int uid,int gid,int perms,const char *name,...)
{
    assert(dev==makedev(24,0) && kind==DEVFS_CHAR && uid==0 && gid==0 && perms==0400);
    assert(!strcmp(name,"mt6895-ufs-lba1"));fake_node.device=dev;return &fake_node;
}
void delay_for_interval(uint32_t n,uint32_t unit) { ticks+=(uint64_t)n*unit; }
uint64_t mach_absolute_time(void) { return ticks+=1000; }
void absolutetime_to_nanoseconds(uint64_t t,uint64_t *out) { *out=t; }
bool PE_parse_boot_argn(const char *name,void *out,size_t size)
{
    assert(size==4);
    if(!strcmp(name,"mtk_ufs_service")){*(uint32_t *)out=mode;return mode_present;}
    if(!strcmp(name,"mtk_ufs_read")){*(uint32_t *)out=read_mode;return true;}
    if(!strcmp(name,"mtk_ufs_probe")){*(uint32_t *)out=probe_mode;return true;}
    assert(!"unexpected argument");return false;
}
kern_return_t kernel_thread_start(void (*f)(void *,wait_result_t),void *arg,thread_t *out)
{ assert(!arg);if(thread_fail)return 1;worker=f;*out=1;return KERN_SUCCESS; }
void thread_deallocate(thread_t t) { assert(t==1); }
thread_t current_thread(void) { return 1; }
kern_return_t thread_terminate(thread_t t) { assert(t==1);terminates++;return 0; }
vfs_context_t vfs_context_kernel(void) { return &fake_node; }
void *vfs_context_ucred(vfs_context_t c) { return c; }
struct proc *vfs_context_proc(vfs_context_t c) { (void)c;return NULL; }
int vnode_lookup(const char *p,int flags,vnode_t *out,vfs_context_t c)
{ (void)c;assert(!strcmp(p,US_NODE_PATH)&&flags==VNODE_LOOKUP_NOFOLLOW);*out=&fake_node;return 0; }
int vnode_vtype(vnode_t n) { assert(n==&fake_node);return VCHR; }
dev_t vnode_specrdev(vnode_t n) { return n->device; }
int VNOP_OPEN(vnode_t n,int flags,vfs_context_t c)
{ (void)c;if(flags&FWRITE)write_opens++;return cdevsw[24].d_open(n->device,flags,0,NULL); }
int VNOP_CLOSE(vnode_t n,int flags,vfs_context_t c)
{ (void)c;return cdevsw[24].d_close(n->device,flags,0,NULL); }
void vnode_put(vnode_t n) { assert(n==&fake_node); }
int vn_rdwr(int rw,vnode_t node,caddr_t buf,int len,off_t offset,int seg,int flags,
            void *cred,int *residual,struct proc *proc)
{
    (void)flags;(void)cred;(void)proc;assert(rw==UIO_READ&&seg==UIO_SYSSPACE);
    struct uio u={offset,len,(uint8_t *)buf,0};readers++;
    int error=cdevsw[24].d_read(node->device,&u,0);*residual=(int)u.residual;return error;
}
vm_offset_t ml_io_map(vm_offset_t pa,vm_size_t n)
{ assert(pa==US_REG_PA&&n==US_REG_BYTES);maps++;return (vm_offset_t)fake_regs; }
vm_offset_t ml_io_map_unmappable(vm_offset_t pa,vm_size_t n,unsigned int attr)
{
    assert(attr==VM_WIMG_WCOMB);maps++;
    if(pa==UR_LEASE_PA){assert(n==US_SCRATCH_BYTES);return (vm_offset_t)fake_scratch;}
    assert(pa==(UR_LIST_PA&~(uint64_t)PAGE_MASK)&&n==PAGE_SIZE);return (vm_offset_t)fake_page;
}
boolean_t pmap_valid_address(uint64_t pa) { (void)pa;return false; }
static uint32_t *fake_table(void) { return (uint32_t *)(fake_page+(UR_LIST_PA&PAGE_MASK)); }
static uint32_t *fake_ucd(void) { return (uint32_t *)(fake_scratch+(UR_UCD_PA-UR_LEASE_PA)); }
void us_host_barrier(void)
{
    if(fake_regs[0x58/4]!=1 || dma_seen)return;
    dma_seen=true;
    commands++;assert(commands<=US_MAX_REQUESTS);
    if(stop_ring){us_stop(&lease);return;}
    uint8_t response[UR_RESPONSE_BYTES]={0};response[0]=0x21;response[2]=2;
    response[11]=20;response[33]=18;
    if (bad_response) response[7]=2;
    for(unsigned i=0;i<UR_RESPONSE_WORDS;i++)fake_ucd()[128+i]=ur_le32(response+i*4);
    memcpy(fake_scratch+(UR_DATA_PA-UR_LEASE_PA),reference,UR_BLOCK_BYTES);
    fake_table()[2]=0;fake_regs[0x58/4]=0;dma_seen=false;
}
static void initial_hardware(void)
{
    struct ur_command cmd;const struct ur_request req={2,1,1,UR_BLOCK_BYTES};
    assert(ur_make_read(&req,&cmd));memcpy(fake_table(),cmd.utrd,32);fake_table()[2]=0;
    for(unsigned i=0;i<8;i++)fake_ucd()[i]=ur_le32(cmd.upiu+i*4);
    memcpy(fake_ucd()+256,cmd.prd,16);
    memcpy(fake_scratch+(UR_DATA_PA-UR_LEASE_PA),reference,UR_BLOCK_BYTES);
    struct ur_lease r={UR_MAGIC,UR_VERSION,UR_TRIAL,UR_UNUSED};memcpy(fake_scratch,&r,sizeof(r));
    fake_regs[0]=0x3587071f;fake_regs[8/4]=0x200;fake_regs[0x30/4]=0x10f;
    fake_regs[0x34/4]=1;fake_regs[0x50/4]=(uint32_t)UR_LIST_PA;fake_regs[0x60/4]=1;
}
int main(int argc,char **argv)
{
    CHECK(argc==3);FILE *f=fopen(argv[2],"rb");CHECK(f!=NULL);
    CHECK(fread(reference,1,sizeof(reference),f)==sizeof(reference));CHECK(fgetc(f)==EOF);fclose(f);
    CHECK(ur_crc32(reference,sizeof(reference))==US_REFERENCE_CRC);initial_hardware();
    const char *scenario=argv[1];
    if(!strcmp(scenario,"absent"))mode_present=false;
    else if(!strcmp(scenario,"zero"))mode=0;
    else if(!strcmp(scenario,"two"))mode=2;
    else if(!strcmp(scenario,"max"))mode=UINT32_MAX;
    else if(!strcmp(scenario,"old-read"))read_mode=1;
    else if(!strcmp(scenario,"old-probe"))probe_mode=1;
    else if(!strcmp(scenario,"stopped"))hakios_mt6895_ufs_service_quiesce();
    else if(!strcmp(scenario,"overlap")){gPhysBase=UR_LIST_PA;gPhysSize=0x200000;}
    else if(!strcmp(scenario,"stale"))((struct ur_lease *)fake_scratch)->state=UR_RESTORED;
    else if(!strcmp(scenario,"bad-cap"))fake_regs[0]=UINT32_MAX;
    else if(!strcmp(scenario,"registration"))reg_fail=true;
    else if(!strcmp(scenario,"thread"))thread_fail=true;
    else if(!strcmp(scenario,"ring-stop"))stop_ring=true;
    else if(!strcmp(scenario,"bad-response"))bad_response=true;
    else CHECK(!strcmp(scenario,"budget")||!strcmp(scenario,"worker"));
    hakios_mt6895_ufs_service_init();
    bool healthy=!strcmp(scenario,"budget")||!strcmp(scenario,"worker")||stop_ring||bad_response;
    if(healthy){
        CHECK(maps==3&&ready==1&&worker!=NULL&&shared_snapshot(&platform).state==UR_CLAIMED);
        if(!strcmp(scenario,"worker")){
            worker(NULL,0);CHECK(commands==3&&readers==4&&write_opens==1&&terminates==1);
            CHECK(requests==3&&issued==3&&delivered==3&&us_control(&lease)==US_IDLE);
        }else{
            uint8_t a[UR_BLOCK_BYTES],b[UR_BLOCK_BYTES];dev_t dev=makedev(24,0);
            CHECK(service_dev_open(dev,FWRITE,0,NULL)==EROFS);
            struct uio u={0,4096,a,0};CHECK(service_dev_write(dev,&u,0)==EROFS&&commands==0);
            u=(struct uio){4096,4,a,0};CHECK(service_dev_read(dev,&u,0)==0&&u.residual==4&&commands==0);
            u=(struct uio){-1,4,a,0};CHECK(service_dev_read(dev,&u,0)==EINVAL&&commands==0);
            u=(struct uio){4097,4,a,0};CHECK(service_dev_read(dev,&u,0)==EINVAL&&commands==0);
            u=(struct uio){0,4096,a,0};int error=service_dev_read(dev,&u,0);
            if(stop_ring){CHECK(error!=0&&commands==1&&(us_control(&lease)&US_POISON_BIT));goto finish;}
            if(bad_response){
                CHECK(error==EIO&&commands==1&&u.residual==4096&&delivered==0);
                CHECK(records[0].diagnostic_valid && records[0].diagnostic_crc==US_REFERENCE_CRC);
                CHECK(records[0].response_valid==UR_RESPONSE_VALID_MASK && records[0].response_failures==16);
                uint32_t reported=0;service_report_records(&reported);CHECK(reported==1);goto finish;
            }
            CHECK(error==0&&u.residual==0&&!memcmp(a,reference,4096));
            u=(struct uio){7,17,b,0};CHECK(service_dev_read(dev,&u,0)==0&&!memcmp(b,reference+7,17));
            u=(struct uio){0,4096,b,0};CHECK(service_dev_read(dev,&u,0)==0&&!memcmp(a,reference,4096));
            CHECK(commands==3&&requests==3&&issued==3&&delivered==3);
            u=(struct uio){0,4096,b,0};CHECK(service_dev_read(dev,&u,0)==EAGAIN&&commands==3&&u.residual==4096);
        }
    }else{
        CHECK(commands==0&&requests==0);
        if(!strcmp(scenario,"registration")||!strcmp(scenario,"thread"))CHECK(shared_snapshot(&platform).state==UR_RESTORED);
        if(!strcmp(scenario,"absent")||!strcmp(scenario,"zero")||!strcmp(scenario,"two")||
           !strcmp(scenario,"max")||read_mode||probe_mode||!strcmp(scenario,"stopped")||
           !strcmp(scenario,"overlap"))CHECK(maps==0);
    }
finish:
    hakios_mt6895_ufs_service_quiesce();CHECK(allocations==0);
    if(platform.scratch){
        struct ur_lease r=shared_snapshot(&platform);
        CHECK(stop_ring?!ur_loader_may_use_storage(&r):ur_loader_may_use_storage(&r));
    }
    printf("PASS %u actual-platform-host checks scenario=%s commands=%u; shim, no hardware\n",checks,scenario,commands);
    return 0;
}
