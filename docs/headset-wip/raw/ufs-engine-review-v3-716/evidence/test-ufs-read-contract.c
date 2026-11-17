#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "ufs_read_contract.h"

static unsigned cases;
#define CHECK(x) do { assert(x); cases++; } while (0)

static struct ur_lease fresh(void)
{
    return (struct ur_lease){ UR_MAGIC, UR_VERSION, UR_TRIAL, UR_UNUSED };
}

int main(void)
{
    struct ur_lease r = fresh();
    CHECK(ur_loader_may_use_storage(&r));
    CHECK(ur_lease_step(&r, UR_CLAIM));
    CHECK(!ur_loader_may_use_storage(&r));
    CHECK(ur_lease_step(&r, UR_RESTORE_CONFIRMED));
    CHECK(ur_loader_may_use_storage(&r));
    CHECK(!ur_lease_step(&r, UR_CLAIM)); /* only one command in this trial */

    r = fresh();
    CHECK(ur_lease_step(&r, UR_CLAIM));
    CHECK(ur_lease_step(&r, UR_UNCERTAIN));
    CHECK(!ur_loader_may_use_storage(&r));
    CHECK(!ur_lease_step(&r, UR_RESTORE_CONFIRMED)); /* timeout cannot become clean */
    CHECK(!ur_lease_step(&r, UR_CLAIM));
    CHECK(ur_lease_step(&r, UR_STOP));
    CHECK(!ur_loader_may_use_storage(&r));

    r = fresh();
    CHECK(ur_lease_step(&r, UR_STOP));
    CHECK(ur_loader_may_use_storage(&r)); /* no descriptor was borrowed */
    CHECK(ur_lease_step(&r, UR_CLAIM));
    CHECK(ur_lease_step(&r, UR_STOP));
    CHECK(!ur_lease_step(&r, UR_RESTORE_CONFIRMED)); /* stop wins before cleanup evidence */
    CHECK(!ur_loader_may_use_storage(&r));
    r = fresh();
    CHECK(ur_lease_step(&r, UR_CLAIM));
    CHECK(ur_lease_step(&r, UR_RESTORE_CONFIRMED));
    CHECK(ur_lease_step(&r, UR_STOP));
    CHECK(ur_loader_may_use_storage(&r)); /* cleanup finished before stop */

    for (unsigned state = 0; state <= UR_POISONED; state++) {
        for (unsigned event = 0; event <= UR_STOP; event++) {
            r = fresh(); r.state = state;
            struct ur_lease old = r;
            bool ok = ur_lease_step(&r, (enum ur_event)event);
            CHECK(ok || memcmp(&r, &old, sizeof(r)) == 0);
            CHECK(state != UR_POISONED || !ur_loader_may_use_storage(&r));
        }
    }
    for (unsigned word = 0; word < 4; word++) {
        uint32_t words[4];
        r = fresh(); memcpy(words, &r, sizeof(words));
        words[word] ^= 0x80000000u;
        memcpy(&r, words, sizeof(r));
        CHECK(!ur_loader_may_use_storage(&r));
        CHECK(!ur_lease_step(&r, UR_CLAIM));
    }
    r = fresh();
    CHECK(!ur_lease_step(&r, (enum ur_event)99));
    CHECK(ur_outside_vm(UR_LIST_PA, 32, UINT64_C(0x100000000), UINT64_C(0x100000000)));
    CHECK(ur_outside_vm(UR_UCD_PA, UR_UCD_BYTES, UINT64_C(0x100000000), UINT64_C(0x100000000)));
    CHECK(ur_outside_vm(UR_DATA_PA, UR_BLOCK_BYTES, UINT64_C(0x100000000), UINT64_C(0x100000000)));
    CHECK(!ur_outside_vm(UINT64_C(0xfffffff0), 32, UINT64_C(0x100000000), UINT64_C(0x100000000)));
    CHECK(!ur_outside_vm(UINT64_MAX - 8, 16, 0x1000, 0x1000));
    CHECK(!ur_outside_vm(0, 4, UINT64_MAX - 8, 16));
    CHECK(!ur_outside_vm(0, 0, 0x1000, 0x1000));
    CHECK(!ur_outside_vm(0, 16, 0x1000, 0));

    const struct ur_request good = { 2, 1, 1, 4096 };
    struct ur_command c;
    CHECK(ur_make_read(&good, &c));
    CHECK(ur_command_allowed(&c));
    CHECK(c.upiu[16] == 0x28 && c.upiu[1] == 0x40 && c.upiu[2] == 2);
    CHECK(c.upiu[12] == 0 && c.upiu[13] == 0 && c.upiu[14] == 0x10 && c.upiu[15] == 0);
    CHECK(c.utrd[4] == 0x60101000 && c.prd[0] == 0x6010e000 && c.prd[3] == 4095);
    for (size_t byte = 0; byte < sizeof(c); byte++) {
        for (unsigned bit = 0; bit < 8; bit++) {
            struct ur_command bad = c;
            ((uint8_t *)&bad)[byte] ^= (uint8_t)(1u << bit);
            CHECK(!ur_command_allowed(&bad)); /* direction/CDB/DMA/length corruption */
        }
    }
    for (unsigned field = 0; field < 4; field++) {
        uint32_t words[4] = { good.lun, good.lba, good.blocks, good.logical_block_bytes };
        words[field] = 0;
        struct ur_request bad = { words[0], words[1], words[2], words[3] };
        struct ur_command out;
        memset(&out, 0xa5, sizeof(out));
        struct ur_command original = out;
        CHECK(!ur_make_read(&bad, &out));
        CHECK(memcmp(&out, &original, sizeof(out)) == 0); /* reject before output mutation */
    }
    for (unsigned lun = 0; lun <= 255; lun++) {
        struct ur_request request = good; request.lun = lun;
        CHECK(ur_make_read(&request, &c) == (lun == 2));
    }
    CHECK(!ur_command_allowed(NULL));
    CHECK(!ur_loader_may_use_storage(NULL));
    printf("PASS %u lease/readonly-command/range checks; no IO performed\n", cases);
    return 0;
}
