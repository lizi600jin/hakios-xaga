#include <assert.h>
#include <stdio.h>

#include "ufs_two_block_codec.h"

static void
reject(const struct vr_request *request, const struct vr_limits *limits)
{
    struct vr_command command;
    memset(&command, 0xa5, sizeof(command));
    const struct vr_command before = command;
    assert(!vr2_make_read(request, limits, &command));
    assert(memcmp(&command, &before, sizeof(command)) == 0);
    assert(!vr2_command_allowed(&command, request, limits));
}

static void
test_ranges(void)
{
    const struct vr_limits runtime = vr_runtime_limits();
    const struct vr_limits capacity = vr_capacity_limits();
    struct vr_request request = { 2, 0, 2, 4096 };
    struct vr_command command;

    for (uint32_t lba = 0; lba <= 16; lba++) {
        request.lba = lba;
        if (lba <= 14) {
            assert(vr2_make_read(&request, &runtime, &command));
            assert(vr2_command_allowed(&command, &request, &runtime));
        } else {
            reject(&request, &runtime);
        }
    }

    request.lba = 31240190;
    assert(vr2_make_read(&request, &capacity, &command));
    static const uint8_t tail_lba[] = { 0x01, 0xdc, 0xaf, 0xfe };
    assert(memcmp(command.upiu + 18, tail_lba, sizeof(tail_lba)) == 0);
    request.lba++;
    reject(&request, &capacity);
    request.lba = UINT32_MAX;
    reject(&request, &capacity);

    request.lba = 0;
    static const struct vr_limits bad_limits[] = { { 0 }, { 1 }, { UINT32_MAX } };
    for (size_t i = 0; i < sizeof(bad_limits) / sizeof(bad_limits[0]); i++) {
        reject(&request, &bad_limits[i]);
    }
    reject(NULL, &runtime);
    reject(&request, NULL);
    assert(!vr2_make_read(&request, &runtime, NULL));

    static const uint32_t bad_counts[] = { 0, 1, 3, UINT32_MAX };
    for (size_t i = 0; i < sizeof(bad_counts) / sizeof(bad_counts[0]); i++) {
        request.blocks = bad_counts[i];
        reject(&request, &runtime);
    }
    request.blocks = 2;
    request.lun = 3;
    reject(&request, &runtime);
    request.lun = 2;
    request.logical_block_bytes = 512;
    reject(&request, &runtime);
    request.logical_block_bytes = 8192;
    reject(&request, &runtime);
}

static unsigned
test_descriptor(void)
{
    const struct vr_limits limits = vr_runtime_limits();
    struct vr_request request = { 2, 1, 2, 4096 };
    const struct vr_command golden = {
        .utrd = { 0x14000000, 0, 0x0f, 0, 0x60101000, 0, 0x00800080, 0x01000001 },
        .upiu = { [0] = 0x01, [1] = 0x40, [2] = 2, [14] = 0x20,
                  [16] = 0x28, [21] = 1, [24] = 2 },
        .prd = { 0x6010e000, 0, 0, 0x1fff },
    };
    struct vr_command actual;
    assert(vr2_make_read(&request, &limits, &actual));
    assert(memcmp(&actual, &golden, sizeof(golden)) == 0);
    assert(vr2_command_allowed(&actual, &request, &limits));
    assert(!vr2_command_allowed(NULL, &request, &limits));

    request.lba = 2;
    assert(!vr2_command_allowed(&actual, &request, &limits));
    request.lba = 1;

    unsigned rejected = 0;
    uint8_t *bytes = (uint8_t *)&actual;
    for (size_t i = 0; i < sizeof(actual); i++) {
        const uint8_t expected = bytes[i];
        for (unsigned value = 0; value <= UINT8_MAX; value++) {
            if (value == expected) {
                continue;
            }
            bytes[i] = (uint8_t)value;
            assert(!vr2_command_allowed(&actual, &request, &limits));
            rejected++;
        }
        bytes[i] = expected;
    }

    assert(!vr_make_read(&request, &limits, &actual));
    request.blocks = 1;
    assert(vr_make_read(&request, &limits, &actual));
    assert(actual.upiu[14] == 0x10 && actual.upiu[24] == 1 && actual.prd[3] == 0xfff);
    assert(!vr2_command_allowed(&actual, &request, &limits));
    return rejected;
}

int
main(void)
{
    test_ranges();
    const unsigned rejected = test_descriptor();
    printf("Two-block codec: golden descriptor and bounds PASS, %u mutations rejected\n", rejected);
    return 0;
}
