/* Cached software lease for a serialized service. Shared DRAM publication is a provider.
 * IDLE/DRAINED clean proofs must be established by integration, not inferred from this codec.
 */
#ifndef MT6895_UFS_SERVICE_LEASE_H
#define MT6895_UFS_SERVICE_LEASE_H
#include "ufs_read_contract.h"

enum us_phase { US_NEW, US_OPENING, US_IDLE, US_ACTIVE, US_DRAINED };
#define US_PHASE_MASK 0xffu
#define US_STOP_BIT 0x100u
#define US_POISON_BIT 0x200u
#define US_RELEASED_BIT 0x400u
struct us_lease {
    uint32_t control;
    bool (*publish)(void *, uint32_t);
    void *context;
};

static inline uint32_t us_control(struct us_lease *s)
{ return __atomic_load_n(&s->control, __ATOMIC_ACQUIRE); }
static inline bool us_stopped(struct us_lease *s)
{ return (us_control(s) & US_STOP_BIT) != 0; }

static inline void us_poison(struct us_lease *s)
{
    (void)__atomic_fetch_or(&s->control, US_STOP_BIT | US_POISON_BIT, __ATOMIC_ACQ_REL);
    (void)s->publish(s->context, UR_POISONED);
}

/* Provider must verify the inherited baseline before calling this. No state reset exists. */
static inline bool us_open(struct us_lease *s, bool clean_baseline)
{
    uint32_t expected = US_NEW;
    if (!s || !s->publish || !clean_baseline ||
        !__atomic_compare_exchange_n(&s->control, &expected, US_OPENING, false,
                                    __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) return false;
    if (!s->publish(s->context, UR_CLAIMED)) { us_poison(s); return false; }
    expected = US_OPENING;
    if (!__atomic_compare_exchange_n(&s->control, &expected, US_IDLE, false,
                                    __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
        us_poison(s);
        return false;
    }
    return true;
}

static inline bool us_admit(struct us_lease *s)
{
    uint32_t expected = US_IDLE;
    return __atomic_compare_exchange_n(&s->control, &expected, US_ACTIVE, false,
                                       __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
}

/* Called only after full memory restore/readback and idle verification; no later engine IO. */
static inline bool us_mark_drained(struct us_lease *s)
{
    uint32_t expected = US_ACTIVE;
    return __atomic_compare_exchange_n(&s->control, &expected, US_DRAINED, false,
                                       __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
}

/* Copy to caller-owned cached memory has finished. A STOP failure needs no hardware cleanup. */
static inline bool us_copy_done(struct us_lease *s)
{
    uint32_t expected = US_DRAINED;
    return __atomic_compare_exchange_n(&s->control, &expected, US_IDLE, false,
                                       __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
}

static inline void us_stop(struct us_lease *s)
{
    uint32_t old = __atomic_fetch_or(&s->control, US_STOP_BIT, __ATOMIC_ACQ_REL);
    uint32_t phase = old & US_PHASE_MASK;
    if (old & (US_POISON_BIT | US_RELEASED_BIT)) return;
    if (phase != US_NEW && phase != US_IDLE && phase != US_DRAINED) {
        us_poison(s);
        return;
    }
    old = __atomic_fetch_or(&s->control, US_RELEASED_BIT, __ATOMIC_ACQ_REL);
    if (old & US_RELEASED_BIT) return; /* exactly one clean publisher */
    if (phase != US_NEW && !s->publish(s->context, UR_RESTORED)) us_poison(s);
}

#endif
