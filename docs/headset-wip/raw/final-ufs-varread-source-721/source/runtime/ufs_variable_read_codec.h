/* Host-only bounded variable-LBA READ(10) codec, trial721-A v2.
 * Pure construction and comparison. No IO, no MMIO, no lease, no ring, no callback.
 *
 * v2 fixes a contract gap in v1: v1 compared the request against a caller-supplied
 * limits value, so moving BOTH to lun3 / blocks2 / block_bytes512 was accepted. The
 * protocol invariants (LUN2, exactly one block, 4096 bytes) are now hard constants that
 * no caller can relax; only the LBA range is parameterised.
 */
#ifndef MT6895_UFS_VARIABLE_READ_CODEC_H
#define MT6895_UFS_VARIABLE_READ_CODEC_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* Fixed protocol invariants. Not negotiable, not derived from any caller argument. */
#define VR_LUN             2u
#define VR_BLOCKS          1u
#define VR_BLOCK_BYTES     4096u
#define VR_UCD_PA          UINT64_C(0x60101000)
#define VR_DATA_PA         UINT64_C(0x6010e000)

/* Observed geometry of this LUN, used only as an upper bound on the LBA. It is NOT proof
 * that the controller answered correctly, and it does not establish ownership.
 */
#define VR_CAPACITY_BLOCKS UINT32_C(31240192)

struct vr_request { uint32_t lun, lba, blocks, logical_block_bytes; };
struct vr_command { uint32_t utrd[8]; uint8_t upiu[32]; uint32_t prd[4]; };
_Static_assert(sizeof(struct vr_command) == 80, "command layout size");

/* Only the LBA range is policy. LUN, block count and block length are deliberately absent:
 * they are fixed invariants, so no limits value can authorise a two-block or 512-byte command.
 */
struct vr_limits { uint32_t lba_max_exclusive; };

static inline struct vr_limits vr_runtime_limits(void)
{
    struct vr_limits l = { 16u };                 /* stage policy: LBA 0..15 */
    return l;
}

static inline struct vr_limits vr_capacity_limits(void)
{
    struct vr_limits l = { VR_CAPACITY_BLOCKS };  /* tail bound from observed geometry */
    return l;
}

/* Cheap upper bound on any LBA this codec will ever encode for one block. */
#define VR_LBA_HARD_MAX VR_CAPACITY_BLOCKS

/* Runs BEFORE any construction, IO, claim or mapping; touches nothing when it refuses.
 * Both the request and the limits are checked: a request is only valid if it satisfies the
 * fixed invariants itself AND falls inside the caller's LBA range.
 */
static inline bool
vr_request_valid(const struct vr_request *r, const struct vr_limits *lim)
{
    if (!r || !lim) return false;
    /* Fixed invariants, independent of lim. */
    if (r->lun != VR_LUN) return false;
    if (r->blocks != VR_BLOCKS) return false;
    if (r->logical_block_bytes != VR_BLOCK_BYTES) return false;
    /* Policy: LBA range. */
    if (lim->lba_max_exclusive == 0u) return false;
    if (lim->lba_max_exclusive > VR_LBA_HARD_MAX) return false;  /* never exceed capacity */
    if (r->lba >= lim->lba_max_exclusive) return false;
    /* Tail bound for a single block: the block must exist. */
    if (r->lba >= VR_CAPACITY_BLOCKS) return false;
    return true;
}

/* Fill the READ(10) CDB. The command descriptor block starts at upiu[16]:
 *   CDB[0]     upiu[16]  opcode 0x28
 *   CDB[1]     upiu[17]  flags (RDPROTECT bit7, DPO bit4, FUA bit3, FUA_NV bit1, PMI bit0)
 *   CDB[2..5]  upiu[18..21]  LBA, four bytes big-endian
 *   CDB[6]     upiu[22]  group number
 *   CDB[7..8]  upiu[23..24] transfer length, big-endian, one block
 *   CDB[9]     upiu[25]  control
 * 719 wrote only upiu[21] because its LBA was the literal 1.
 */
static inline bool
vr_make_read(const struct vr_request *r, const struct vr_limits *lim, struct vr_command *c)
{
    if (!c) return false;
    if (!vr_request_valid(r, lim)) return false;
    for (size_t i = 0; i < sizeof(*c); i++) ((uint8_t *)c)[i] = 0;
    c->upiu[0] = 0x01;                          /* command UPIU */
    c->upiu[1] = 0x40;                          /* data-in */
    c->upiu[2] = (uint8_t)VR_LUN;               /* fixed, not from the request */
    c->upiu[14] = (uint8_t)(VR_BLOCK_BYTES >> 8); /* expected data length, big-endian */
    c->upiu[15] = (uint8_t)(VR_BLOCK_BYTES);
    c->upiu[16] = 0x28;                         /* CDB[0] READ(10) */
    c->upiu[17] = 0x00;                         /* CDB[1] no protection, DPO, FUA or PMI */
    c->upiu[18] = (uint8_t)(r->lba >> 24);      /* CDB[2..5] LBA, big-endian */
    c->upiu[19] = (uint8_t)(r->lba >> 16);
    c->upiu[20] = (uint8_t)(r->lba >> 8);
    c->upiu[21] = (uint8_t)(r->lba);
    c->upiu[22] = 0x00;                         /* CDB[6] group */
    c->upiu[23] = (uint8_t)(VR_BLOCKS >> 8);    /* CDB[7..8] transfer length, fixed */
    c->upiu[24] = (uint8_t)(VR_BLOCKS);
    c->upiu[25] = 0x00;                         /* CDB[9] control */
    c->utrd[0] = 0x14000000u;                   /* UFS command, data in */
    c->utrd[2] = 0x0fu;                         /* invalid OCS before submission */
    c->utrd[4] = (uint32_t)VR_UCD_PA;
    c->utrd[5] = (uint32_t)(VR_UCD_PA >> 32);
    c->utrd[6] = 0x00800080u;                   /* response offset512, length512, dwords */
    c->utrd[7] = 0x01000001u;                   /* PRDT offset1024, one entry */
    c->prd[0] = (uint32_t)VR_DATA_PA;
    c->prd[1] = (uint32_t)(VR_DATA_PA >> 32);
    c->prd[3] = VR_BLOCK_BYTES - 1u;
    return true;
}

/* Compare the bytes actually read back against the command expected for THIS request.
 * Matching some other allowed LBA is not sufficient, so the expected command is rebuilt
 * from the caller's request and compared in full.
 */
static inline bool
vr_command_allowed(const struct vr_command *actual, const struct vr_request *r,
                   const struct vr_limits *lim)
{
    struct vr_command expected;
    if (!actual) return false;
    if (!vr_make_read(r, lim, &expected)) return false;
    for (size_t i = 0; i < sizeof(expected); i++)
        if (((const uint8_t *)actual)[i] != ((const uint8_t *)&expected)[i]) return false;
    return true;
}

/* Recover the LBA a command would issue, and reject any malformed descriptor block. */
static inline bool
vr_command_lba(const struct vr_command *c, uint32_t *lba)
{
    if (!c || !lba) return false;
    if (c->upiu[16] != 0x28) return false;
    if (c->upiu[17] != 0x00) return false;   /* no flags, so the LBA field is the whole address */
    if (c->upiu[22] != 0x00) return false;
    if (c->upiu[25] != 0x00) return false;
    if (c->upiu[23] != 0x00 || c->upiu[24] != VR_BLOCKS) return false;
    *lba = ((uint32_t)c->upiu[18] << 24) | ((uint32_t)c->upiu[19] << 16) |
           ((uint32_t)c->upiu[20] << 8) | (uint32_t)c->upiu[21];
    return true;
}

#endif
