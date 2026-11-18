#ifndef MT6895_UFS_TWO_BLOCK_CODEC_H
#define MT6895_UFS_TWO_BLOCK_CODEC_H

#if defined(KERNEL) && !defined(US_HOST_TEST)
#include <libkern/libkern.h>
#else
#include <string.h>
#endif

#include "../../ufs_varread/ufs_variable_read_codec.h"

#define VR2_BLOCKS 2u
#define VR2_TRANSFER_BYTES (VR2_BLOCKS * VR_BLOCK_BYTES)

/* Host candidate for one READ(10) with a contiguous two-block PRD. */
static inline bool
vr2_make_read(const struct vr_request *request, const struct vr_limits *limits,
    struct vr_command *command)
{
    if (!request || !limits || request->blocks != VR2_BLOCKS ||
        limits->lba_max_exclusive < VR2_BLOCKS ||
        request->lba > limits->lba_max_exclusive - VR2_BLOCKS) {
        return false;
    }

    /* The frozen builder validates the LUN, block size, and capacity ceiling. */
    struct vr_request first_block = *request;
    first_block.blocks = VR_BLOCKS;
    if (!vr_make_read(&first_block, limits, command)) {
        return false;
    }

    const uint32_t bytes = VR2_TRANSFER_BYTES;
    command->upiu[12] = (uint8_t)(bytes >> 24);
    command->upiu[13] = (uint8_t)(bytes >> 16);
    command->upiu[14] = (uint8_t)(bytes >> 8);
    command->upiu[15] = (uint8_t)bytes;
    command->upiu[23] = (uint8_t)(VR2_BLOCKS >> 8);
    command->upiu[24] = (uint8_t)VR2_BLOCKS;
    command->prd[3] = bytes - 1u;
    return true;
}

static inline bool
vr2_command_allowed(const struct vr_command *actual,
    const struct vr_request *request, const struct vr_limits *limits)
{
    struct vr_command expected;
    return actual && vr2_make_read(request, limits, &expected) &&
        memcmp(actual, &expected, sizeof(expected)) == 0;
}

#endif
