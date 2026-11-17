/* Pure host codec proposal720, not wired into any kernel/loader or hardware adapter.
 * This constructs exactly one RC10 data-IN command. It cannot issue or complete IO.
 * A caller must independently establish accepted UPIU/OCS, DMA visibility and full restore
 * before treating an eight-byte payload as a runtime geometry observation.
 */
#ifndef MT6895_UFS_CAPACITY_CODEC_H
#define MT6895_UFS_CAPACITY_CODEC_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define UC_DATA_BYTES 8u
#define UC_UCD_PA UINT64_C(0x60101000)
#define UC_DATA_PA UINT64_C(0x6010e000)
#define UC_ANDROID_BLOCK_BYTES 4096u
#define UC_ANDROID_CAPACITY_BYTES UINT64_C(127959826432)

struct uc_command { uint32_t utrd[8]; uint8_t upiu[32]; uint32_t prd[4]; };
_Static_assert(sizeof(struct uc_command) == 80, "capacity command wire size");
_Static_assert(offsetof(struct uc_command, upiu) == 32, "UPIU placement");
_Static_assert(offsetof(struct uc_command, prd) == 64, "PRDT placement");

/* LUN2 only, tag0, all ten CDB bytes zero except opcode0x25; PMI/LBA remain0.
 * Expected transfer8 and PRDT DBC7. Inherited queue/controller addresses are never written
 * by this codec. A future adapter must assert these scratch addresses match its contract.
 */
static inline bool uc_make_capacity(uint32_t lun, struct uc_command *command)
{
    if (!command || lun != 2) return false;
    for (size_t i=0; i<sizeof(*command); i++) ((uint8_t *)command)[i]=0;
    command->utrd[0]=0x14000000;
    command->utrd[2]=0x0f;
    command->utrd[4]=(uint32_t)UC_UCD_PA;
    command->utrd[5]=(uint32_t)(UC_UCD_PA>>32);
    command->utrd[6]=0x00800080;
    command->utrd[7]=0x01000001;
    command->upiu[0]=0x01;
    command->upiu[1]=0x40;
    command->upiu[2]=2;
    command->upiu[15]=UC_DATA_BYTES;
    command->upiu[16]=0x25;
    command->prd[0]=(uint32_t)UC_DATA_PA;
    command->prd[1]=(uint32_t)(UC_DATA_PA>>32);
    command->prd[3]=UC_DATA_BYTES-1;
    return true;
}

/* Full eighty-byte equality, including reserved/CDB/UPIU/PRDT bytes. Never accept WRITE,
 * READ10, RC16, another LUN, a larger transfer, or a different scratch/response layout.
 */
static inline bool uc_command_allowed(const struct uc_command *actual)
{
    struct uc_command expected;
    if (!actual || !uc_make_capacity(2,&expected)) return false;
    for (size_t i=0; i<sizeof(expected); i++)
        if (((const uint8_t *)actual)[i] != ((const uint8_t *)&expected)[i]) return false;
    return true;
}

enum uc_decode_result { UC_DECODE_OK, UC_BAD_ARGUMENT, UC_RC10_SATURATED, UC_ZERO_BLOCK_BYTES };
struct uc_geometry {
    uint32_t last_lba, logical_block_bytes;
    uint64_t block_count, capacity_bytes;
};

static inline uint32_t uc_be32(const uint8_t *bytes)
{
    return ((uint32_t)bytes[0]<<24) | ((uint32_t)bytes[1]<<16) |
           ((uint32_t)bytes[2]<<8) | bytes[3];
}

/* Decode observations separately from Android comparison. Any nonzero block length can
 * be recorded, including512; that alone does not authorize the4096-byte xaga READ path.
 * RC10's all-ones lastLBA is unresolved/saturated, not a no-medium diagnosis. No fallback,
 * follow-up RC16 or retry exists here. Non-OK leaves the caller's output untouched.
 */
static inline enum uc_decode_result
uc_decode_capacity(const uint8_t *data, size_t bytes, struct uc_geometry *out)
{
    if (!data || !out || bytes != UC_DATA_BYTES) return UC_BAD_ARGUMENT;
    uint32_t last=uc_be32(data), block_bytes=uc_be32(data+4);
    if (last == UINT32_MAX) return UC_RC10_SATURATED;
    if (!block_bytes) return UC_ZERO_BLOCK_BYTES;
    uint64_t count=(uint64_t)last+1;
    /* last<=UINT32_MAX-1, block_bytes<=UINT32_MAX, so product fits uint64_t. */
    *out=(struct uc_geometry){last,block_bytes,count,count*block_bytes};
    return UC_DECODE_OK;
}

/* A comparison with Android's independently recorded block-layer observation, not an IO
 * completion predicate and not a reason to replace differing device-reported geometry.
 */
static inline bool uc_matches_android(const struct uc_geometry *geometry)
{
    return geometry && geometry->logical_block_bytes == UC_ANDROID_BLOCK_BYTES &&
           geometry->block_count == (uint64_t)geometry->last_lba+1 &&
           geometry->block_count == UC_ANDROID_CAPACITY_BYTES/UC_ANDROID_BLOCK_BYTES &&
           geometry->capacity_bytes == UC_ANDROID_CAPACITY_BYTES;
}
#endif
