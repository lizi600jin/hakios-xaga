#include "ufs_service_model.h"
#include "ufs_service_engine.h"

struct service_model {
    struct model io; /* first member: legacy host callbacks accept this address */
    struct us_lease lease;
    unsigned publishes[4], total_rings, copies;
    unsigned stop_before_publish, stop_after_publish;
    bool publish_fails[4], triggered, stop_drained;
};
struct copy_target {
    struct service_model *service;
    uint8_t bytes[UR_BLOCK_BYTES];
    unsigned stop_at, fail_at;
    bool reenter;
};
static struct ur_workspace saved_service;
static struct ur_outcome out_service, out_other;
static uint32_t expected_crc;
static const struct ur_request request = {2,1,1,UR_BLOCK_BYTES};
static enum us_read_result nested_result;
static struct ur_io service_adapter(struct service_model *s);
static bool deliver(void *, const uint8_t *, size_t);

static bool service_publish(void *ctx, uint32_t state)
{
    struct service_model *s = ctx;
    assert(state >= UR_CLAIMED && state <= UR_POISONED);
    if (s->stop_before_publish == state && !s->triggered) {
        s->triggered = true;
        us_stop(&s->lease);
    }
    s->publishes[state]++;
    s->io.lease.state = state; /* ordinary host wire model; not MMIO */
    if (s->stop_after_publish == state && !s->triggered) {
        s->triggered = true;
        us_stop(&s->lease);
    }
    return !s->publish_fails[state]; /* may fail after a committed publication */
}

static void service_init(struct service_model *s, enum behavior mode)
{
    memset(s,0,sizeof(*s));
    init(&s->io,mode);
    s->lease.publish = service_publish;
    s->lease.context = s;
}

static bool service_claim(void *ctx)
{
    struct service_model *s = ctx;
    return us_control(&s->lease) == US_ACTIVE && ur_lease_valid(&s->io.lease) &&
           s->io.lease.state == UR_CLAIMED && !s->io.deny_claim;
}
static bool service_finish(void *ctx, bool clean)
{
    struct service_model *s = ctx;
    s->io.finishes++;
    if (!clean) { us_poison(&s->lease); return false; }
    if (s->io.behavior == RESTORE_REFUSED) return false;
    if (s->io.behavior == STOP_AT_FINISH) us_stop(&s->lease);
    bool ok = us_mark_drained(&s->lease);
    if (ok && s->stop_drained) us_stop(&s->lease);
    return ok; /* no post-CAS STOP check: DRAINED contains no further engine IO */
}
static bool service_stopped(void *ctx)
{ return us_stopped(&((struct service_model *)ctx)->lease); }
static bool service_write(void *ctx, enum ur_memory kind, uint32_t off, uint32_t value)
{
    struct service_model *s = ctx;
    if (us_control(&s->lease) != US_ACTIVE) return false;
    return write_word(&s->io,kind,off,value);
}
static bool service_ring(void *ctx)
{
    struct service_model *s = ctx;
    if (us_control(&s->lease) != US_ACTIVE || s->io.lease.state != UR_CLAIMED) return false;
    bool ok = ring_once(&s->io);
    s->total_rings++;
    if (s->io.stop) us_stop(&s->lease);
    return ok;
}
static struct ur_io service_adapter(struct service_model *s)
{
    struct ur_io io = adapter(&s->io);
    io.context = s;
    io.claim = service_claim; io.finish = service_finish; io.stopped = service_stopped;
    io.write_word = service_write; io.ring_once = service_ring;
    return io;
}
static bool deliver(void *ctx, const uint8_t *data, size_t n)
{
    struct copy_target *t = ctx;
    struct service_model *s = t->service;
    CHECK(n == UR_BLOCK_BYTES);
    CHECK((us_control(&s->lease) & US_PHASE_MASK) == US_DRAINED);
    s->copies++;
    if (t->reenter) {
        struct ur_io io = service_adapter(s);
        unsigned reads = s->io.mem_reads, writes = s->io.mem_writes, regs = s->io.reg_reads;
        memset(&out_other,0x66,sizeof(out_other));
        nested_result = us_read_fixed(&s->lease,&io,&saved_service,&out_other,&request,
                                     deliver,t,UR_BLOCK_BYTES,expected_crc,12,100);
        CHECK(nested_result == US_BUSY && s->io.mem_reads == reads &&
              s->io.mem_writes == writes && s->io.reg_reads == regs);
        CHECK(((const uint8_t *)&out_other)[0] == 0x66);
    }
    for (unsigned i = 0; i <= n; i++) {
        if (t->stop_at == i+1) us_stop(&s->lease);
        if (t->fail_at == i+1) return false;
        if (i < n) t->bytes[i] = data[i];
    }
    return true;
}

static enum us_read_result run_read(struct service_model *s, struct copy_target *target)
{
    struct ur_io io = service_adapter(s);
    target->service = s;
    return us_read_fixed(&s->lease,&io,&saved_service,&out_service,&request,
                         deliver,target,UR_BLOCK_BYTES,expected_crc,12,100);
}
static void reset_counts(struct service_model *s)
{
    s->io.reg_reads = s->io.mem_reads = s->io.mem_writes = s->io.rings = 0;
    s->io.barriers = s->io.finishes = s->io.writes_after_ring = 0;
    s->io.ticks = 0;
}

int main(void)
{
    static struct service_model s;
    static struct copy_target first, second;
    uint8_t oracle[UR_BLOCK_BYTES];
    for (unsigned i=0;i<UR_BLOCK_BYTES/4;i++) ur_bytes(0x04030201u ^ i,oracle+i*4);
    expected_crc = ur_crc32(oracle,sizeof(oracle));

    service_init(&s,SUCCESS);
    CHECK(!us_open(&s.lease,false) && !s.publishes[UR_CLAIMED]);
    CHECK(!us_admit(&s.lease));
    us_stop(&s.lease);
    CHECK(ur_loader_may_use_storage(&s.io.lease) && us_stopped(&s.lease));
    CHECK(!us_open(&s.lease,true) && !s.publishes[UR_CLAIMED]);

    service_init(&s,SUCCESS);
    CHECK(us_open(&s.lease,true));
    CHECK(us_control(&s.lease)==US_IDLE && s.io.lease.state==UR_CLAIMED);
    CHECK(!ur_loader_may_use_storage(&s.io.lease));
    memset(&first,0,sizeof(first)); first.reenter=true;
    CHECK(run_read(&s,&first)==US_READ_OK);
    CHECK(!memcmp(first.bytes,oracle,sizeof(oracle)) && memory_restored(&s.io));
    unsigned reads=s.io.mem_reads, regs=s.io.reg_reads, writes=s.io.mem_writes;
    first.reenter=false;
    for (unsigned i=0;i<2;i++) {
        reset_counts(&s); memset(&second,0,sizeof(second));
        CHECK(run_read(&s,&second)==US_READ_OK);
        CHECK(!memcmp(first.bytes,oracle,sizeof(oracle)) && !memcmp(second.bytes,oracle,sizeof(oracle)));
        CHECK(memory_restored(&s.io) && s.io.lease.state==UR_CLAIMED);
    }
    CHECK(s.total_rings==3 && s.publishes[UR_CLAIMED]==1 && !s.publishes[UR_RESTORED]);
    reset_counts(&s);s.io.behavior=WRONG_DATA;
    CHECK(run_read(&s,&second)==US_COMMAND_ERROR);
    CHECK(!memcmp(first.bytes,oracle,sizeof(oracle)) && memory_restored(&s.io));
    us_stop(&s.lease); us_stop(&s.lease);
    CHECK(s.publishes[UR_RESTORED]==1 && ur_loader_may_use_storage(&s.io.lease));
    reset_counts(&s);
    CHECK(run_read(&s,&second)==US_STOPPED && !s.io.mem_reads && !s.io.mem_writes && !s.io.reg_reads);

    for (unsigned before=0;before<2;before++) {
        service_init(&s,SUCCESS);
        if (before) s.stop_before_publish=UR_CLAIMED; else s.stop_after_publish=UR_CLAIMED;
        CHECK(!us_open(&s.lease,true));
        CHECK(us_control(&s.lease)&US_POISON_BIT);
        CHECK(!ur_loader_may_use_storage(&s.io.lease));
    }
    service_init(&s,SUCCESS);s.publish_fails[UR_CLAIMED]=true;
    CHECK(!us_open(&s.lease,true) && s.io.lease.state==UR_POISONED);
    service_init(&s,SUCCESS);CHECK(us_open(&s.lease,true));
    s.publish_fails[UR_RESTORED]=true;us_stop(&s.lease);us_stop(&s.lease);
    CHECK(s.publishes[UR_RESTORED]==1 && s.io.lease.state==UR_POISONED);
    for (unsigned before=0;before<2;before++) {
        service_init(&s,SUCCESS);CHECK(us_open(&s.lease,true));
        if (before) s.stop_before_publish=UR_RESTORED; else s.stop_after_publish=UR_RESTORED;
        us_stop(&s.lease);
        CHECK(s.publishes[UR_RESTORED]==1 && ur_loader_may_use_storage(&s.io.lease));
    }
    service_init(&s,SUCCESS);CHECK(us_open(&s.lease,true));CHECK(us_admit(&s.lease));
    us_stop(&s.lease);
    CHECK(!us_mark_drained(&s.lease) && !ur_loader_may_use_storage(&s.io.lease));

    service_init(&s,SUCCESS);CHECK(us_open(&s.lease,true));s.stop_drained=true;
    memset(&second,0,sizeof(second));CHECK(run_read(&s,&second)==US_STOPPED);
    CHECK(out_service.restored && !s.copies && ur_loader_may_use_storage(&s.io.lease));
    const unsigned copy_points[]={1,2,2049,4096,4097};
    for (unsigned i=0;i<sizeof(copy_points)/sizeof(copy_points[0]);i++) {
        service_init(&s,SUCCESS);CHECK(us_open(&s.lease,true));memset(&second,0,sizeof(second));
        second.stop_at=copy_points[i];CHECK(run_read(&s,&second)==US_STOPPED);
        CHECK(memory_restored(&s.io) && ur_loader_may_use_storage(&s.io.lease));
        service_init(&s,SUCCESS);CHECK(us_open(&s.lease,true));memset(&second,0,sizeof(second));
        second.fail_at=copy_points[i];CHECK(run_read(&s,&second)==US_COPY_ERROR);
        CHECK(memory_restored(&s.io) && us_control(&s.lease)==US_IDLE);
    }
    memset(&second,0,sizeof(second));
    for (unsigned mode=NO_COMPLETION;mode<=EXTENDED_HEADER;mode++) {
        service_init(&s,(enum behavior)mode);CHECK(us_open(&s.lease,true));
        CHECK(run_read(&s,&second)!=US_READ_OK);
        us_stop(&s.lease);
        CHECK(out_service.restored ? ur_loader_may_use_storage(&s.io.lease) :
                                    !ur_loader_may_use_storage(&s.io.lease));
    }
    for (unsigned kind=0;kind<3;kind++) {
        unsigned limit=kind==0?reads:kind==1?regs:writes;
        for (unsigned i=1;i<=limit;i++) {
            service_init(&s,SUCCESS);CHECK(us_open(&s.lease,true));
            if (kind==0) s.io.fail_read=i; else if(kind==1) s.io.fail_reg=i; else s.io.fail_write=i;
            CHECK(run_read(&s,&second)!=US_READ_OK && !s.copies);
            CHECK(!out_service.restored || memory_restored(&s.io));
            us_stop(&s.lease);
            CHECK(out_service.restored ? ur_loader_may_use_storage(&s.io.lease) :
                                        !ur_loader_may_use_storage(&s.io.lease));
        }
    }
    service_init(&s,SUCCESS);CHECK(us_open(&s.lease,true));
    const struct ur_request bad_requests[] = {
        {0,1,1,4096},{2,0,1,4096},{2,2,1,4096},{2,1,0,4096},
        {2,1,2,4096},{2,1,1,512},{2,UINT32_MAX,1,4096}
    };
    for (unsigned i=0;i<sizeof(bad_requests)/sizeof(bad_requests[0]);i++) {
        struct ur_io io=service_adapter(&s);
        memset(&out_service,0x66,sizeof(out_service));
        CHECK(us_read_fixed(&s.lease,&io,&saved_service,&out_service,&bad_requests[i],
                            deliver,&second,4096,expected_crc,12,100)==US_BAD_REQUEST);
        CHECK(!s.io.mem_reads && !s.io.mem_writes && !s.io.reg_reads && us_control(&s.lease)==US_IDLE);
        CHECK(((const uint8_t *)&out_service)[0]==0x66);
    }
    us_stop(&s.lease);
    printf("PASS %u service lease/engine/copy/stop checks; reads=%u regs=%u writes=%u; host only\n",
           checks,reads,regs,writes);
    return 0;
}
