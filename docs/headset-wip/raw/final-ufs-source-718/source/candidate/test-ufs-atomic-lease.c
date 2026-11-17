#include <stdio.h>
#include <assert.h>
#include "ufs_atomic_lease.h"

struct model {
    struct ur_atomic_lease phase;
    struct ur_lease shared;
    unsigned stop_before_store, fail_state, calls;
    bool stop_once, store_on_fail;
};
static unsigned checks;
#define CHECK(x) do { assert(x); checks++; } while (0)

static bool publish(void *context, uint32_t value)
{
    struct model *m = context;
    m->calls++;
    if (!m->stop_once && m->stop_before_store == value + 1) {
        m->stop_once = true;
        ur_atomic_stop(&m->phase); /* scheduling point AFTER CAS, BEFORE shared store */
    }
    if (m->fail_state == value + 1) {
        if (m->store_on_fail) m->shared.state = value;
        return false;
    }
    m->shared.state = value;
    return true;
}

static void init(struct model *m)
{
    *m = (struct model){ 0 };
    m->phase.publish = publish;
    m->phase.context = m;
    m->shared = (struct ur_lease){ UR_MAGIC, UR_VERSION, UR_TRIAL, UR_UNUSED };
}

int main(void)
{
    struct model m;
    init(&m);
    CHECK(ur_atomic_claim(&m.phase));
    CHECK(!ur_loader_may_use_storage(&m.shared));
    CHECK(ur_atomic_finish(&m.phase, true));
    CHECK(ur_loader_may_use_storage(&m.shared));
    CHECK(!ur_atomic_claim(&m.phase));
    ur_atomic_stop(&m.phase);
    CHECK(ur_loader_may_use_storage(&m.shared));

    init(&m); ur_atomic_stop(&m.phase);
    CHECK(!ur_atomic_claim(&m.phase));
    CHECK(ur_loader_may_use_storage(&m.shared));
    CHECK(!ur_atomic_finish(&m.phase, true));

    init(&m); CHECK(ur_atomic_claim(&m.phase));
    ur_atomic_stop(&m.phase);
    CHECK(!ur_atomic_finish(&m.phase, true));
    CHECK(!ur_loader_may_use_storage(&m.shared));
    CHECK(!ur_atomic_claim(&m.phase));

    init(&m); m.stop_before_store = UR_CLAIMED + 1;
    CHECK(!ur_atomic_claim(&m.phase));
    CHECK(ur_atomic_stopped(&m.phase));
    CHECK(!ur_loader_may_use_storage(&m.shared));
    CHECK(!ur_atomic_finish(&m.phase, true));

    init(&m); CHECK(ur_atomic_claim(&m.phase));
    m.stop_before_store = UR_RESTORED + 1;
    CHECK(ur_atomic_finish(&m.phase, true));
    CHECK(ur_atomic_stopped(&m.phase));
    CHECK(ur_loader_may_use_storage(&m.shared)); /* CAS clean only AFTER actual restoration */

    for (unsigned committed = 0; committed < 2; committed++) {
        init(&m); m.fail_state = UR_CLAIMED + 1; m.store_on_fail = committed;
        CHECK(!ur_atomic_claim(&m.phase));
        CHECK(!ur_loader_may_use_storage(&m.shared));
        CHECK(!ur_atomic_finish(&m.phase, true));
        init(&m); CHECK(ur_atomic_claim(&m.phase));
        m.fail_state = UR_RESTORED + 1; m.store_on_fail = committed;
        CHECK(!ur_atomic_finish(&m.phase, true));
        CHECK(!ur_loader_may_use_storage(&m.shared));
        CHECK(!ur_atomic_claim(&m.phase));
    }
    init(&m); CHECK(ur_atomic_claim(&m.phase));
    CHECK(!ur_atomic_finish(&m.phase, false));
    CHECK(!ur_atomic_finish(&m.phase, true));
    CHECK(!ur_loader_may_use_storage(&m.shared));
    CHECK(!ur_atomic_claim(&m.phase));
    printf("PASS %u atomic phase/publication/interleaving checks; no platform integration\n", checks);
    return 0;
}
