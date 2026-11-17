#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "ufs_read_engine.h"

enum behavior { SUCCESS, NO_COMPLETION, RING_AMBIGUOUS, SCSI_ERROR, BAD_TAG, BAD_OCS,
                NO_FRESH_OCS, SHORT_RESPONSE, WRONG_DATA, RESTORE_REFUSED, STOP_AFTER_RING,
                STOP_AT_FINISH, READBACK_CORRUPTED };
struct model {
    uint32_t table[8], ucd[UR_UCD_BYTES/4], data[UR_BLOCK_BYTES/4];
    struct ur_workspace original;
    struct ur_lease lease;
    enum behavior behavior;
    unsigned reg_reads, mem_reads, mem_writes, rings, barriers, finishes;
    unsigned fail_reg, fail_read, fail_write;
    unsigned writes_after_ring, corrupt_reads, write_before_claim;
    uint64_t ticks;
    uint32_t dbr;
    bool stop, deny_claim, restoring;
};
static unsigned checks;
#define CHECK(x) do { assert(x); checks++; } while (0)

static uint32_t *region(struct model *m, enum ur_memory kind, uint32_t off)
{
    assert(off % 4 == 0);
    if (kind == UR_TABLE) { assert(off < 32); return &m->table[off/4]; }
    if (kind == UR_UCD) { assert(off < UR_UCD_BYTES); return &m->ucd[off/4]; }
    assert(kind == UR_DATA && off < UR_BLOCK_BYTES);
    return &m->data[off/4];
}

static bool read_reg(void *ctx, uint16_t off, uint32_t *value)
{
    struct model *m = ctx;
    if (++m->reg_reads == m->fail_reg) return false;
    switch (off) {
    case 0: *value = 0x3587071f; break;
    case 8: *value = 0x200; break;
    case 0x30: *value = 0x10f; break;
    case 0x34: *value = 1; break;
    case 0x50: *value = (uint32_t)UR_LIST_PA; break;
    case 0x54: *value = 0; break;
    case 0x58: *value = m->dbr; break;
    case 0x60: *value = 1; break;
    default: assert(!"unexpected register access"); return false;
    }
    return true;
}

static bool read_word(void *ctx, enum ur_memory kind, uint32_t off, uint32_t *out)
{
    struct model *m = ctx;
    if (++m->mem_reads == m->fail_read) { *out = 0xdeadbeef; return false; }
    *out = *region(m, kind, off);
    if (m->behavior == READBACK_CORRUPTED && m->lease.state == UR_CLAIMED &&
        !m->rings && !m->corrupt_reads && kind == UR_TABLE && off == 0) {
        *out ^= 1u << 25;
        m->corrupt_reads++;
    }
    return true;
}

static bool write_word(void *ctx, enum ur_memory kind, uint32_t off, uint32_t value)
{
    struct model *m = ctx;
    m->mem_writes++;
    if (m->lease.state != UR_CLAIMED) m->write_before_claim++;
    if (m->rings) m->writes_after_ring++;
    *region(m, kind, off) = value;
    if (m->mem_writes == m->fail_write) return false; /* failure can follow a committed write */
    return true;
}

static bool claim(void *ctx)
{
    struct model *m = ctx;
    return !m->deny_claim && !m->stop && ur_lease_step(&m->lease, UR_CLAIM);
}
static bool finish(void *ctx, bool clean)
{
    struct model *m = ctx;
    m->finishes++;
    if (m->behavior == STOP_AT_FINISH) {
        m->stop = true; ur_lease_step(&m->lease, UR_STOP);
    }
    if (clean && m->behavior == RESTORE_REFUSED) return false;
    return ur_lease_step(&m->lease, clean ? UR_RESTORE_CONFIRMED : UR_UNCERTAIN);
}
static bool stopped(void *ctx) { return ((struct model *)ctx)->stop; }
static void barrier(void *ctx) { ((struct model *)ctx)->barriers++; }
static uint64_t now_us(void *ctx) { return ((struct model *)ctx)->ticks++; }

static void reply(struct model *m)
{
    uint8_t response[32] = {0};
    response[0] = 0x21; response[2] = 2;
    if (m->behavior == SCSI_ERROR) response[7] = 2;
    if (m->behavior == BAD_TAG) response[3] = 1;
    if (m->behavior == SHORT_RESPONSE) response[15] = 1;
    for (unsigned i = 0; i < 8; i++) m->ucd[128+i] = ur_le32(response+i*4);
    for (unsigned i = 0; i < UR_BLOCK_BYTES/4; i++) m->data[i] = 0x04030201u ^ i;
    if (m->behavior == WRONG_DATA) m->data[10] ^= 1;
    m->table[2] = m->behavior == BAD_OCS ? 5 : 0;
    if (m->behavior == NO_FRESH_OCS) m->table[2] = 0x0f;
    m->dbr = 0;
}

static bool ring_once(void *ctx)
{
    struct model *m = ctx;
    struct ur_command actual;
    memcpy(actual.utrd, m->table, 32);
    for (unsigned i = 0; i < 8; i++) ur_bytes(m->ucd[i], actual.upiu+i*4);
    memcpy(actual.prd, m->ucd+256, 16);
    assert(ur_command_allowed(&actual));
    assert(m->lease.state == UR_CLAIMED && !m->stop && m->rings == 0);
    m->rings++;
    m->dbr = 1;
    if (m->behavior == RING_AMBIGUOUS) return false;
    if (m->behavior == NO_COMPLETION) return true;
    if (m->behavior == STOP_AFTER_RING) {
        m->stop = true; ur_lease_step(&m->lease, UR_STOP); return true;
    }
    reply(m);
    return true;
}

static struct ur_io adapter(struct model *m)
{
    return (struct ur_io){ read_reg, read_word, write_word, claim, finish, stopped,
                          ring_once, barrier, now_us, m };
}

static void init(struct model *m, enum behavior behavior)
{
    memset(m, 0, sizeof(*m));
    struct ur_command c;
    const struct ur_request request = { 2, 1, 1, 4096 };
    assert(ur_make_read(&request, &c));
    m->behavior = behavior;
    m->lease = (struct ur_lease){ UR_MAGIC, UR_VERSION, UR_TRIAL, UR_UNUSED };
    for (unsigned i = 0; i < UR_UCD_BYTES/4; i++) m->ucd[i] = 0x11110000u + i;
    for (unsigned i = 0; i < UR_BLOCK_BYTES/4; i++) m->data[i] = 0x22220000u + i;
    memcpy(m->table, c.utrd, 32); m->table[2] = 0;
    for (unsigned i = 0; i < 8; i++) m->ucd[i] = ur_le32(c.upiu+i*4);
    memcpy(m->ucd+256, c.prd, 16);
    memcpy(m->original.table, m->table, sizeof(m->table));
    memcpy(m->original.ucd, m->ucd, sizeof(m->ucd));
    memcpy(m->original.data, m->data, sizeof(m->data));
}

static bool memory_restored(const struct model *m)
{
    return !memcmp(m->table, m->original.table, sizeof(m->table)) &&
           !memcmp(m->ucd, m->original.ucd, sizeof(m->ucd)) &&
           !memcmp(m->data, m->original.data, sizeof(m->data));
}

int main(void)
{
    static struct model m;
    static struct ur_workspace saved;
    static struct ur_outcome out;
    uint8_t oracle[UR_BLOCK_BYTES];
    for (unsigned i = 0; i < UR_BLOCK_BYTES/4; i++) ur_bytes(0x04030201u ^ i, oracle+i*4);
    uint32_t crc = ur_crc32(oracle, sizeof(oracle));
    CHECK(ur_crc32((const uint8_t *)"123456789", 9) == 0xcbf43926u);
    init(&m, SUCCESS); struct ur_io io = adapter(&m);
    ur_run(&io, &saved, &out, crc, 12, 100);
    CHECK(out.result == UR_READ_OK && out.data_valid && out.restored);
    CHECK(!memcmp(out.block, oracle, sizeof(oracle)));
    CHECK(memory_restored(&m) && ur_loader_may_use_storage(&m.lease));
    CHECK(m.rings == 1 && !m.write_before_claim);
    unsigned reads = m.mem_reads, regs = m.reg_reads, writes = m.mem_writes;

    for (unsigned mode = NO_COMPLETION; mode <= READBACK_CORRUPTED; mode++) {
        init(&m, (enum behavior)mode); io = adapter(&m);
        ur_run(&io, &saved, &out, crc, 12, 100);
        CHECK(!out.data_valid && out.result != UR_READ_OK && m.rings <= 1);
        CHECK(!m.write_before_claim);
        if (mode == NO_COMPLETION || mode == RING_AMBIGUOUS || mode == NO_FRESH_OCS ||
            mode == STOP_AFTER_RING || mode == STOP_AT_FINISH || mode == RESTORE_REFUSED) {
            CHECK(out.result == UR_UNCERTAIN_RESULT && !ur_loader_may_use_storage(&m.lease));
            if (mode != STOP_AT_FINISH && mode != RESTORE_REFUSED) CHECK(m.writes_after_ring == 0);
        } else {
            CHECK(memory_restored(&m) && out.restored && ur_loader_may_use_storage(&m.lease));
        }
    }
    init(&m, SUCCESS); m.deny_claim = true; io = adapter(&m);
    ur_run(&io, &saved, &out, crc, 12, 100);
    CHECK(out.result == UR_NO_LEASE && !m.mem_writes && !m.rings);
    CHECK(memory_restored(&m) && ur_loader_may_use_storage(&m.lease));
    init(&m, SUCCESS); m.stop = true; io = adapter(&m);
    ur_run(&io, &saved, &out, crc, 12, 100);
    CHECK(out.result == UR_REFUSED && !m.mem_reads && !m.reg_reads && !m.mem_writes);
    init(&m, SUCCESS); m.ucd[4] ^= 0x02; io = adapter(&m); /* old CDB opcode changes */
    ur_run(&io, &saved, &out, crc, 12, 100);
    CHECK(out.result == UR_REFUSED && !m.mem_writes && !m.rings);

    for (unsigned i = 1; i <= reads; i++) {
        init(&m, SUCCESS); m.fail_read = i; io = adapter(&m);
        ur_run(&io, &saved, &out, crc, 12, 100);
        CHECK(m.rings <= 1 && !m.write_before_claim && !out.data_valid);
        CHECK(!out.restored || memory_restored(&m));
        CHECK(out.restored || !out.claimed || !ur_loader_may_use_storage(&m.lease));
    }
    for (unsigned i = 1; i <= regs; i++) {
        init(&m, SUCCESS); m.fail_reg = i; io = adapter(&m);
        ur_run(&io, &saved, &out, crc, 12, 100);
        CHECK(m.rings <= 1 && !m.write_before_claim && !out.data_valid);
        CHECK(!out.restored || memory_restored(&m));
        CHECK(out.restored || !out.claimed || !ur_loader_may_use_storage(&m.lease));
    }
    for (unsigned i = 1; i <= writes; i++) {
        init(&m, SUCCESS); m.fail_write = i; io = adapter(&m);
        ur_run(&io, &saved, &out, crc, 12, 100);
        CHECK(m.rings <= 1 && !m.write_before_claim && !out.data_valid);
        CHECK(!out.restored || memory_restored(&m));
        CHECK(out.restored || !ur_loader_may_use_storage(&m.lease));
    }
    printf("PASS %u bounded-engine checks; fault positions reads=%u regs=%u writes=%u; no hardware IO\n",
           checks, reads, regs, writes);
    return 0;
}
