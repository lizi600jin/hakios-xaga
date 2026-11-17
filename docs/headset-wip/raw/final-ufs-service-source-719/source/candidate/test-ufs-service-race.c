#define _GNU_SOURCE
#include <assert.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <string.h>
#include "ufs_service_lease.h"

struct race {
    struct us_lease lease;
    pthread_barrier_t start, middle;
    uint32_t wire, published[4];
    unsigned mode;
};
static unsigned checks;
#define CHECK(x) do { assert(x); checks++; } while (0)

static bool publish(void *ctx, uint32_t state)
{
    struct race *r=ctx;
    assert(state>=UR_CLAIMED && state<=UR_POISONED);
    /* Fake shared record is ordinary HOST cached memory, not a model of MMIO atomics. */
    __atomic_fetch_add(&r->published[state],1,__ATOMIC_RELAXED);
    sched_yield();
    __atomic_store_n(&r->wire,state,__ATOMIC_RELEASE);
    sched_yield();
    return !(r->mode==2 && state==UR_RESTORED); /* false AFTER a committed store */
}
static void *request_thread(void *ctx)
{
    struct race *r=ctx;
    pthread_barrier_wait(&r->start);
    if (r->mode==2) { us_stop(&r->lease); return NULL; }
    if (r->mode==1 && !us_open(&r->lease,true)) return NULL;
    if (!us_admit(&r->lease)) return NULL;
    if (r->mode==3) {
        pthread_barrier_wait(&r->middle);pthread_barrier_wait(&r->middle);
    }
    sched_yield();
    /* This test grants a synthetic restored-memory proof, not hardware evidence. */
    if (!us_mark_drained(&r->lease)) { us_poison(&r->lease); return NULL; }
    if (r->mode==4) {
        pthread_barrier_wait(&r->middle);pthread_barrier_wait(&r->middle);
    }
    sched_yield();
    (void)us_copy_done(&r->lease); /* no hardware after DRAINED, even if STOP wins */
    return NULL;
}
static void *stop_thread(void *ctx)
{
    struct race *r=ctx;
    pthread_barrier_wait(&r->start);
    if (r->mode==3 || r->mode==4) pthread_barrier_wait(&r->middle);
    sched_yield();
    us_stop(&r->lease);
    if (r->mode==3 || r->mode==4) pthread_barrier_wait(&r->middle);
    return NULL;
}
int main(void)
{
    unsigned allowed=0, denied=0;
    for (unsigned mode=0;mode<5;mode++) {
        for (unsigned i=0;i<500;i++) {
            struct race r;memset(&r,0,sizeof(r));r.mode=mode;
            r.lease.publish=publish;r.lease.context=&r;
            CHECK(pthread_barrier_init(&r.start,NULL,2)==0);
            CHECK(pthread_barrier_init(&r.middle,NULL,2)==0);
            if (mode!=1) CHECK(us_open(&r.lease,true));
            pthread_t a,b;
            CHECK(pthread_create(&a,NULL,request_thread,&r)==0);
            CHECK(pthread_create(&b,NULL,stop_thread,&r)==0);
            CHECK(pthread_join(a,NULL)==0 && pthread_join(b,NULL)==0);
            CHECK(pthread_barrier_destroy(&r.start)==0);
            CHECK(pthread_barrier_destroy(&r.middle)==0);
            uint32_t control=us_control(&r.lease),wire=__atomic_load_n(&r.wire,__ATOMIC_ACQUIRE);
            CHECK(control&US_STOP_BIT);
            CHECK(r.published[UR_CLAIMED]<=1 && r.published[UR_RESTORED]<=1);
            struct ur_lease observed={UR_MAGIC,UR_VERSION,UR_TRIAL,wire};
            if (ur_loader_may_use_storage(&observed)) {
                CHECK(!(control&US_POISON_BIT));
                CHECK((wire==UR_UNUSED && (control&US_PHASE_MASK)==US_NEW) ||
                      (wire==UR_RESTORED && ((control&US_PHASE_MASK)==US_IDLE ||
                                            (control&US_PHASE_MASK)==US_DRAINED)));
                allowed++;
            } else {
                CHECK(wire==UR_POISONED && (control&US_POISON_BIT));denied++;
            }
            if (mode==2) CHECK(wire==UR_POISONED && r.published[UR_RESTORED]==1);
            if (mode==3) CHECK(wire==UR_POISONED && (control&US_PHASE_MASK)==US_ACTIVE);
            if (mode==4) CHECK(wire==UR_RESTORED && (control&US_PHASE_MASK)==US_DRAINED);
            CHECK(!us_open(&r.lease,true) && !us_admit(&r.lease) && !us_copy_done(&r.lease));
        }
    }
    printf("PASS %u cached-lease race checks; schedules=2500 allowed=%u denied=%u; host only\n",
           checks,allowed,denied);
    return 0;
}
