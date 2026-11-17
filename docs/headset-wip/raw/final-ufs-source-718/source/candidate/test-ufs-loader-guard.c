#include <assert.h>
#include <stdio.h>
#include "ufs_loader_guard.h"

struct model { struct ur_lease shared; unsigned barriers, storage_calls, jump_calls; bool corrupt; };
static unsigned checks;
#define CHECK(x) do { assert(x); checks++; } while (0)
static void barrier(void *context)
{
    struct model *m = context;
    m->barriers++;
    if (m->corrupt) m->shared.state = UR_CLAIMED;
}
/* This models the required call placement, not the actual patched loader source. */
static bool return_to_loader(struct model *m)
{
    if (!ur_guard_allows_storage(&m->shared, barrier, m)) return false;
    m->storage_calls++;
    m->jump_calls++;
    return true;
}
int main(void)
{
    struct model m = {0};
    CHECK(!return_to_loader(&m));
    CHECK(m.storage_calls == 0 && m.jump_calls == 0);
    uint32_t words[1] = { 0x34333231u }; /* ASCII1234, independent known CRC */
    CHECK(ur_guard_block_crc32(words, 1) == 0x9be3e0a3u);
    ur_guard_initialize(&m.shared, barrier, &m);
    CHECK(m.shared.magic == UR_MAGIC && m.shared.state == UR_UNUSED);
    CHECK(return_to_loader(&m));
    for (unsigned state = UR_UNUSED; state <= UR_POISONED; state++) {
        m.storage_calls = m.jump_calls = 0; m.shared.state = state;
        bool allowed = return_to_loader(&m);
        CHECK(allowed == (state == UR_UNUSED || state == UR_RESTORED));
        CHECK(m.storage_calls == (unsigned)allowed && m.jump_calls == (unsigned)allowed);
    }
    for (unsigned word = 0; word < 4; word++) {
        ur_guard_initialize(&m.shared, barrier, &m);
        switch (word) {
        case 0: m.shared.magic ^= 1; break;
        case 1: m.shared.version++; break;
        case 2: m.shared.trial++; break;
        case 3: m.shared.state = 99; break;
        }
        m.storage_calls = m.jump_calls = 0;
        CHECK(!return_to_loader(&m));
        CHECK(m.storage_calls == 0 && m.jump_calls == 0);
    }
    ur_guard_initialize(&m.shared, barrier, &m);
    m.corrupt = true; m.storage_calls = m.jump_calls = 0;
    CHECK(!return_to_loader(&m));
    CHECK(m.storage_calls == 0 && m.jump_calls == 0);
    printf("PASS %u loader gate checks; source placement audited separately\n", checks);
    return 0;
}
