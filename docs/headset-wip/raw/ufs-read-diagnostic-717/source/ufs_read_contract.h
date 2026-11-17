/* Host-tested protocol/codec candidate only. Not wired to XNU or the loader.
 * State transitions below are pure planning logic, NOT atomic shared-memory operations.
 * Integration must supply serialization, mapping/barriers and a loader gate before ALL UFS IO.
 */
#ifndef MT6895_UFS_READ_CONTRACT_H
#define MT6895_UFS_READ_CONTRACT_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define UR_TRIAL 717u
#define UR_MAGIC 0x554c5345u
#define UR_VERSION 1u
#define UR_LIST_PA UINT64_C(0x51266400)
#define UR_LEASE_PA UINT64_C(0x60100000)
#define UR_UCD_PA UINT64_C(0x60101000)
#define UR_UCD_BYTES 1664u
#define UR_DATA_PA UINT64_C(0x6010e000)
#define UR_BLOCK_BYTES 4096u

enum ur_state { UR_UNUSED, UR_CLAIMED, UR_RESTORED, UR_POISONED };
enum ur_event { UR_CLAIM, UR_RESTORE_CONFIRMED, UR_UNCERTAIN, UR_STOP };
struct ur_lease { uint32_t magic, version, trial, state; };
_Static_assert(sizeof(struct ur_lease) == 16, "lease wire size");

static inline bool
ur_lease_valid(const struct ur_lease *r)
{
    return r && r->magic == UR_MAGIC && r->version == UR_VERSION &&
           r->trial == UR_TRIAL && r->state <= UR_POISONED;
}

static inline bool
ur_loader_may_use_storage(const struct ur_lease *r)
{
    return ur_lease_valid(r) && (r->state == UR_UNUSED || r->state == UR_RESTORED);
}

/* UR_RESTORE_CONFIRMED is evidence supplied by integration, not proved by this function.
 * It must include no outstanding owned DMA, unchanged controller/list, restored bytes and
 * readback, and exclusion of concurrent stop. Never generate it from a timeout or OCS alone.
 */
static inline bool
ur_lease_step(struct ur_lease *r, enum ur_event event)
{
    if (!ur_lease_valid(r)) return false;
    switch (event) {
    case UR_CLAIM:
        if (r->state != UR_UNUSED) return false;
        r->state = UR_CLAIMED;
        return true;
    case UR_RESTORE_CONFIRMED:
        if (r->state != UR_CLAIMED) return false;
        r->state = UR_RESTORED;
        return true;
    case UR_UNCERTAIN:
        if (r->state != UR_CLAIMED) return false;
        r->state = UR_POISONED;
        return true;
    case UR_STOP:
        if (r->state == UR_CLAIMED) r->state = UR_POISONED;
        return true;
    }
    return false;
}

/* Necessary VM separation check only; it is NOT physical ownership or mapping permission. */
static inline bool
ur_outside_vm(uint64_t start, uint64_t size, uint64_t vm_start, uint64_t vm_size)
{
    if (!size || !vm_size || start > UINT64_MAX - size || vm_start > UINT64_MAX - vm_size)
        return false;
    return start + size <= vm_start || start >= vm_start + vm_size;
}

struct ur_request { uint32_t lun, lba, blocks, logical_block_bytes; };
struct ur_command { uint32_t utrd[8]; uint8_t upiu[32]; uint32_t prd[4]; };
_Static_assert(sizeof(struct ur_command) == 80, "command layout size");

/* Exactly one READ(10), LUN2, LBA1, one verified4096-byte logical block.
 * LUN/block geometry must come from independently established device evidence.
 * No WRITE, QUERY, task management, reset, clock or queue-base programming is constructed.
 */
static inline bool
ur_make_read(const struct ur_request *r, struct ur_command *c)
{
    if (!r || !c || r->lun != 2 || r->lba != 1 || r->blocks != 1 ||
        r->logical_block_bytes != UR_BLOCK_BYTES) return false;
    for (size_t i = 0; i < sizeof(*c); i++) ((uint8_t *)c)[i] = 0;
    c->upiu[0] = 0x01;
    c->upiu[1] = 0x40;
    c->upiu[2] = 2;
    c->upiu[14] = 0x10; /* expected data length0x00001000, big-endian */
    c->upiu[16] = 0x28;
    c->upiu[21] = 1;
    c->upiu[24] = 1;
    c->utrd[0] = 0x14000000; /* UFS command, data in; no host-to-device direction */
    c->utrd[2] = 0x0f;       /* invalid OCS before submission */
    c->utrd[4] = (uint32_t)UR_UCD_PA;
    c->utrd[5] = (uint32_t)(UR_UCD_PA >> 32);
    c->utrd[6] = 0x00800080; /* response offset512, length512, in dwords */
    c->utrd[7] = 0x01000001; /* PRDT offset1024 in dwords, one entry */
    c->prd[0] = (uint32_t)UR_DATA_PA;
    c->prd[1] = (uint32_t)(UR_DATA_PA >> 32);
    c->prd[3] = UR_BLOCK_BYTES - 1;
    return true;
}

/* Validate the actual bytes read back before ringing, against the sole allowed command.
 * This strict whitelist is a codec check, not proof of exclusive controller ownership.
 */
static inline bool
ur_command_allowed(const struct ur_command *actual)
{
    const struct ur_request request = { 2, 1, 1, UR_BLOCK_BYTES };
    struct ur_command expected;
    if (!actual || !ur_make_read(&request, &expected)) return false;
    for (size_t i = 0; i < sizeof(expected); i++)
        if (((const uint8_t *)actual)[i] != ((const uint8_t *)&expected)[i]) return false;
    return true;
}

#endif
