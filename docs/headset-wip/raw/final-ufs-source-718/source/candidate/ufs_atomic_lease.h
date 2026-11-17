/* Atomic SOFTWARE phase lives in ordinary cached kernel memory. Shared-state publication
 * is delegated to an audited aligned-store + barrier adapter; never atomics on Device MMIO.
 * This is still a host candidate, with no platform mapping or loader integration.
 */
#ifndef MT6895_UFS_ATOMIC_LEASE_H
#define MT6895_UFS_ATOMIC_LEASE_H
#include "ufs_read_contract.h"
#define UR_STOP_BIT 0x100u
#define UR_PHASE_MASK 3u
struct ur_atomic_lease {
    uint32_t phase;
    bool (*publish)(void *, uint32_t);
    void *context;
};

static inline bool ur_atomic_stopped(struct ur_atomic_lease *lease)
{ return (__atomic_load_n(&lease->phase, __ATOMIC_ACQUIRE) & UR_STOP_BIT) != 0; }

static inline void ur_atomic_poison(struct ur_atomic_lease *lease)
{
    (void)__atomic_fetch_or(&lease->phase, UR_STOP_BIT | UR_POISONED, __ATOMIC_ACQ_REL);
    (void)lease->publish(lease->context, UR_POISONED);
}

static inline bool ur_atomic_claim(struct ur_atomic_lease *lease)
{
    uint32_t expected = UR_UNUSED;
    if (!__atomic_compare_exchange_n(&lease->phase, &expected, UR_CLAIMED, false,
                                     __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) return false;
    if (!lease->publish(lease->context, UR_CLAIMED) || ur_atomic_stopped(lease)) {
        ur_atomic_poison(lease);
        return false;
    }
    return true;
}

static inline bool ur_atomic_finish(struct ur_atomic_lease *lease, bool restored)
{
    if (!restored) { ur_atomic_poison(lease); return false; }
    uint32_t expected = UR_CLAIMED;
    if (!__atomic_compare_exchange_n(&lease->phase, &expected, UR_RESTORED, false,
                                     __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) return false;
    if (!lease->publish(lease->context, UR_RESTORED)) {
        ur_atomic_poison(lease);
        return false;
    }
    return true;
}

/* Callable without locks/waits/MMIO. Provider publication writes only the shared DRAM state.
 * STOP before claim denies future claim; STOP after claim poisons; STOP after restored is safe.
 */
static inline void ur_atomic_stop(struct ur_atomic_lease *lease)
{
    uint32_t old = __atomic_fetch_or(&lease->phase, UR_STOP_BIT, __ATOMIC_ACQ_REL);
    if ((old & UR_PHASE_MASK) == UR_CLAIMED)
        (void)lease->publish(lease->context, UR_POISONED);
}
#endif
