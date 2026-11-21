/* Host-tested protocol/codec candidate only. Not wired to XNU or the loader.
 * State transitions below are pure planning logic, NOT atomic shared-memory operations.
 * Integration must supply serialization, mapping/barriers and a loader gate before ALL UFS IO.
 */
#ifndef MT6895_UFS_READ_CONTRACT_H
#define MT6895_UFS_READ_CONTRACT_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define UR_TRIAL 740u
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

/* ------------------------------------------------------------------ Trial749: WRITE(10) */

/*
 * The write contract, and it is deliberately as narrow as the read one.
 *
 * A read can only be wrong; a write can be wrong and *stay* wrong. So the command that may be
 * sent is fixed here, not built from a caller's arguments: WRITE(10), one block, at an LBA the
 * caller names but inside a window this header defines. The window is the part of xnusys the
 * APFS container does not claim -- the container's own superblock says it ends at block 608448,
 * and the loader reserves the last 8 MiB of the partition for its log, so 608448..653559 is
 * nobody's. A write outside it is refused before a byte leaves the CPU.
 */
/*
 * The zone is named in **partition-relative blocks**, because that is how a reader of the APFS
 * container thinks of it. A caller converts to the absolute LBA the controller wants
 * (XNUSYS_LBA + block, in ufs_two_block_engine.h) and gets the check done a second time there.
 */
#define UR_ZONE_FIRST_BLOCK UINT64_C(608448)    /* first block the container does not claim */
#define UR_ZONE_LAST_BLOCK  UINT64_C(653559)    /* last block before the loader's 8 MiB log */

static inline bool
ur_zone_contains_block(uint64_t block)
{
    return block >= UR_ZONE_FIRST_BLOCK && block <= UR_ZONE_LAST_BLOCK;
}

/*
 * Build the one WRITE(10) this contract allows: LUN 2 (as every command on this port), one
 * 4096-byte block, absolute `lba`. The zone check is the engine's, where the partition base is
 * known; this builds what it is told to and nothing else. The caller's payload is in UR_DATA.
 */
static inline bool
ur_make_write_flags(uint64_t lba, bool fua, struct ur_command *c)
{
    if (!c) return false;
    for (size_t i = 0; i < sizeof(*c); i++) ((uint8_t *)c)[i] = 0;
    c->upiu[0] = 0x01;                      /* command UPIU */
    /*
     * 0x20 is UPIU_FLAG_WRITE (JESD223: the direction lives in the same byte as a read's 0x40).
     * The first version wrote 0x00, which is neither direction.
     */
    c->upiu[1] = 0x20;
    c->upiu[2] = 2;                         /* LUN, fixed */
    c->upiu[14] = 0x10;                     /* expected data length 0x00001000, big-endian */
    c->upiu[16] = 0x2a;                     /* CDB[0] WRITE(10) */
    /*
     * CDB[1]: bit3 is FUA. With the device in write-back mode (Android reports
     * /sys/block/sda/queue/write_cache = "write back", fua = 1) an ordinary write may still be
     * sitting in the device's volatile cache when it reports done, so a write that has to survive
     * a power cut asks for FUA. The plain form stays the default: FUA is a per-command promise,
     * not a mode.
     */
    c->upiu[17] = fua ? 0x08u : 0x00u;
    c->upiu[18] = (uint8_t)(lba >> 24);     /* CDB[2..5] LBA, big-endian */
    c->upiu[19] = (uint8_t)(lba >> 16);
    c->upiu[20] = (uint8_t)(lba >> 8);
    c->upiu[21] = (uint8_t)lba;
    c->upiu[22] = 0x00;                     /* CDB[6] group */
    c->upiu[23] = 0x00;                     /* CDB[7..8] transfer length, one block */
    c->upiu[24] = 0x01;
    c->upiu[25] = 0x00;                     /* CDB[9] control */
    /*
     * Command type UFS (1 << 28) with data direction OUT (1 << 25).
     *
     * This started as 0x0a000000, which is the read path's (2 << 25) plus a bit that does not
     * belong -- a "write" carrying a device-to-host direction field. The controller answered
     * OCS 7 (INVALID_PRD_TABLE), which is what a direction it cannot reconcile with the command
     * looks like from here.
     */
    c->utrd[0] = 0x12000000;
    c->utrd[2] = 0x0f;                      /* invalid OCS before submission */
    c->utrd[4] = (uint32_t)UR_UCD_PA;
    c->utrd[5] = (uint32_t)(UR_UCD_PA >> 32);
    c->utrd[6] = 0x00800080;                /* response offset512, length512, in dwords */
    c->utrd[7] = 0x01000001;                /* PRDT offset1024 in dwords, one entry */
    c->prd[0] = (uint32_t)UR_DATA_PA;
    c->prd[1] = (uint32_t)(UR_DATA_PA >> 32);
    c->prd[3] = UR_BLOCK_BYTES - 1;
    return true;
}

/* The plain write: no FUA. Kept as the name most callers use. */
static inline bool
ur_make_write(uint64_t lba, struct ur_command *c)
{
    return ur_make_write_flags(lba, false, c);
}

/* The same strict check the read side makes: what is about to be rung must be exactly this.
 * Both forms are allowed, and each is compared against its own construction, so a command cannot
 * quietly acquire FUA it did not ask for, or lose the FUA it did.
 */
static inline bool
ur_write_allowed(const struct ur_command *actual, uint64_t lba)
{
    struct ur_command expected;
    if (!actual) return false;
    if (ur_make_write_flags(lba, false, &expected)) {
        if (memcmp(actual, &expected, sizeof(expected)) == 0) return true;
    }
    if (!ur_make_write_flags(lba, true, &expected)) return false;
    for (size_t i = 0; i < sizeof(expected); i++)
        if (((const uint8_t *)actual)[i] != ((const uint8_t *)&expected)[i]) return false;
    return true;
}

/* ------------------------------------------------------------------ Trial754: SYNCHRONIZE CACHE */

/*
 * SYNCHRONIZE CACHE(10), the command that makes the device put everything it is holding into
 * non-volatile storage. FUA covers one write; this covers all of them, which is what an unmount
 * needs before it can say the volume is safe to walk away from.
 *
 * It moves no data, and every field that describes data is zero: the UPIU's direction byte, the
 * expected transfer length, the UTRD's data-direction bits, and the PRDT entry count. Getting that
 * wrong is how a no-data command turns into a scatter-gather request the device rejects.
 *
 * LBA 0 with a block count of 0 is the whole logical unit, which is the form Linux uses in
 * sd_sync_cache() for a full flush.
 */
static inline bool
ur_make_flush(struct ur_command *c)
{
    if (!c) return false;
    for (size_t i = 0; i < sizeof(*c); i++) ((uint8_t *)c)[i] = 0;
    c->upiu[0] = 0x01;                      /* command UPIU */
    c->upiu[1] = 0x00;                      /* no data in either direction */
    c->upiu[2] = 2;                         /* LUN, fixed as everywhere else here */
    c->upiu[14] = 0x00;                     /* expected data transfer length 0 */
    c->upiu[15] = 0x00;
    c->upiu[16] = 0x35;                     /* CDB[0] SYNCHRONIZE CACHE(10) */
    c->upiu[17] = 0x00;                     /* CDB[1]: no IMMED, so completion means done */
    c->upiu[18] = 0; c->upiu[19] = 0;       /* CDB[2..5] LBA 0 */
    c->upiu[20] = 0; c->upiu[21] = 0;
    c->upiu[22] = 0x00;                     /* CDB[6] group */
    c->upiu[23] = 0x00;                     /* CDB[7..8] block count 0 = the whole unit */
    c->upiu[24] = 0x00;
    c->upiu[25] = 0x00;                     /* CDB[9] control */
    c->utrd[0] = 0x10000000;                /* command type UFS, no data transfer */
    c->utrd[2] = 0x0f;                      /* invalid OCS before submission */
    c->utrd[4] = (uint32_t)UR_UCD_PA;
    c->utrd[5] = (uint32_t)(UR_UCD_PA >> 32);
    c->utrd[6] = 0x00800080;                /* response offset512, length512, in dwords */
    /*
     * DW7 is prd_table_length[15:0] | prd_table_offset[31:16], and the length is a COUNT OF
     * ENTRIES, not bytes. A no-data command has zero entries and keeps the same table offset:
     * 0x01010000 would be one entry at offset 0x100, which the device rejects with OCS 1
     * (invalid command) because it is being told to scatter-gather data for a command that
     * moves none.
     */
    c->utrd[7] = 0x01000000;                /* PRDT: zero entries at dword offset 0x100 */
    return true;
}

/* Exactly that command, and nothing that happens to look like it. */
static inline bool
ur_flush_allowed(const struct ur_command *actual)
{
    struct ur_command expected;
    if (!actual || !ur_make_flush(&expected)) return false;
    for (size_t i = 0; i < sizeof(expected); i++)
        if (((const uint8_t *)actual)[i] != ((const uint8_t *)&expected)[i]) return false;
    return true;
}

#endif
