/* Loader-side guard candidate; caller must run it before ANY storage operation on return. */
#ifndef MT6895_UFS_LOADER_GUARD_H
#define MT6895_UFS_LOADER_GUARD_H
#include "ufs_read_contract.h"

/* Loader accesses this DMA block through aligned32-bit loads with MMU off. */
static inline uint32_t ur_guard_block_crc32(const volatile uint32_t *words, unsigned count)
{
    uint32_t crc = UINT32_MAX;
    for (unsigned i = 0; i < count; i++) {
        uint32_t word = words[i];
        for (unsigned byte = 0; byte < 4; byte++) {
            crc ^= (word >> (byte*8)) & 0xff;
            for (unsigned bit = 0; bit < 8; bit++)
                crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320u : 0);
        }
    }
    return ~crc;
}

/* New-boot initialization only, after loader reads and before handing control to XNU.
 * Never call this to clear an outstanding/poisoned lease during return or recovery.
 */
static inline void
ur_guard_initialize(volatile struct ur_lease *r, void (*barrier)(void *), void *context)
{
    r->magic = 0;
    r->version = UR_VERSION;
    r->trial = UR_TRIAL;
    r->state = UR_UNUSED;
    barrier(context);
    r->magic = UR_MAGIC;
    barrier(context);
}

static inline bool
ur_guard_allows_storage(const volatile struct ur_lease *r, void (*barrier)(void *), void *context)
{
    struct ur_lease a = { r->magic, r->version, r->trial, r->state };
    barrier(context);
    struct ur_lease b = { r->magic, r->version, r->trial, r->state };
    if (a.magic != b.magic || a.version != b.version || a.trial != b.trial || a.state != b.state)
        return false;
    return ur_loader_may_use_storage(&b);
}
#endif
