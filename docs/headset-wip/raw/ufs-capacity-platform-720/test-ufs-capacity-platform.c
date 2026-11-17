/* Host-only test of the ACTUAL 720 capacity adapter against synthetic mapped memory.
 * Never copied into a kernel tree. No real hardware address is dereferenced.
 *
 * One scenario per process (argv[1]). The cached lease is a process-global and must never
 * be reset between scenarios, so each scenario starts from a genuinely fresh process.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef uintptr_t vm_offset_t;
typedef unsigned long vm_size_t;
typedef int boolean_t;
#define VM_WIMG_WCOMB 0x8u
#define PAGE_SIZE 4096u
#define PAGE_MASK (PAGE_SIZE-1u)

#define EMU_REG_BYTES 0x2300u
#define EMU_SCRATCH   (2u*1024u*1024u)
/* UR_LIST_PA is 0x51266400: page base 0x51266000 + offset 0x400 */
#define LIST_OFF      0x400u

static unsigned checks;
void uc_host_barrier(void);
#define CHECK(x) do { checks++; if(!(x)){ fprintf(stderr,"line%d: %s\n",__LINE__,#x); exit(1);} } while(0)

struct emu {
    uint8_t regs[EMU_REG_BYTES];
    uint8_t scratch[EMU_SCRATCH];
    uint8_t table[4096];
    /* fault injection */
    unsigned tail_corrupt_word;
    unsigned response_partial_words;
    bool map_fail, pmap_valid;
    /* behaviour */
    uint32_t ocs_value;
    uint8_t response[52];
    uint8_t payload[8];
    bool payload_enabled;
    bool leave_prefill;
    bool stop_at_drain;        /* inject an external STOP at clean DRAINED */
    bool stop_fired;
    /* observations */
    unsigned rings, barriers, mem_reads, mem_writes;
    bool whitelist_ok;
    uint8_t planted_table[32], planted_ucd[32], planted_data[4096];
    bool planted;
};
static struct emu emu;

/* explicit presence + value: an invalid 0xffffffff must reach the adapter's reject path,
 * not be mistaken for "boot argument absent" the way an int -1 sentinel would. */
static bool g_present;
static uint32_t g_value;
static bool g_service, g_read, g_probe;

void uc_host_barrier(void);
extern struct us_lease lease;   /* defined by the adapter */

uint64_t uc_host_now_us(void) { return 0; }

static vm_offset_t base_of(uint64_t pa)
{
    if (pa >= 0x60100000ull && pa < 0x60100000ull + EMU_SCRATCH)
        return (vm_offset_t)(uintptr_t)emu.scratch + (pa - 0x60100000ull);
    if (pa >= 0x51266000ull && pa < 0x51267000ull)
        return (vm_offset_t)(uintptr_t)emu.table + (pa - 0x51266000ull);
    if (pa >= 0x112b0000ull && pa < 0x112b0000ull + EMU_REG_BYTES)
        return (vm_offset_t)(uintptr_t)emu.regs + (pa - 0x112b0000ull);
    return 0;
}
vm_offset_t uc_host_ml_io_map(vm_offset_t pa, vm_size_t size)
{ (void)size; return emu.map_fail ? 0 : base_of(pa); }
vm_offset_t uc_host_ml_io_map_unmappable(vm_offset_t pa, vm_size_t size, unsigned int f)
{ (void)size; (void)f; return emu.map_fail ? 0 : base_of(pa); }
boolean_t uc_host_pmap_valid_address(uint64_t pa) { (void)pa; return emu.pmap_valid; }

#define ml_io_map uc_host_ml_io_map
#define ml_io_map_unmappable uc_host_ml_io_map_unmappable
#define pmap_valid_address uc_host_pmap_valid_address

unsigned long gPhysBase = 0x100000000ull, gPhysSize = 0x100000000ull;

int uc_host_pe_parse_boot_argn(const char *name, void *out, size_t size)
{
    bool present = false;
    uint32_t value = 0;
    if (!strcmp(name,"mtk_ufs_capacity")) { present = g_present; value = g_value; }
    else if (!strcmp(name,"mtk_ufs_service")) { present = g_service; value = 1u; }
    else if (!strcmp(name,"mtk_ufs_read"))    { present = g_read;    value = 1u; }
    else if (!strcmp(name,"mtk_ufs_probe"))   { present = g_probe;   value = 1u; }
    if (!present) return 0;
    *(uint32_t *)out = value; (void)size; return 1;
}
#define PE_parse_boot_argn uc_host_pe_parse_boot_argn

static int g_verbose;
#define printf(...) do { if (g_verbose) fprintf(stdout, __VA_ARGS__); } while (0)

#include "support/ufs_service_lease.h"
#include "ufs_capacity/ufs_capacity_engine.h"

void uc_host_barrier(void)
{
    uint32_t *tbl = (uint32_t *)(emu.table + LIST_OFF);
    emu.barriers++;
    /* Inject an external STOP exactly at the clean DRAINED transition. */
    if (emu.stop_at_drain && !emu.stop_fired &&
        (us_control(&lease) & US_PHASE_MASK) == US_DRAINED) {
        emu.stop_fired = true;
        us_stop(&lease);
    }
    /* The adapter has just rung if slot 0 holds the capacity UTRD and UTRLDBR reads 1. */
    if (emu.rings == 0 && (tbl[0] & 0xfe000000u) == 0x14000000u &&
        *(volatile uint32_t *)(emu.regs + 0x58) == 1u) {
        struct uc_command wire;
        /* Capture what the adapter actually wrote for the whitelist assertion. */
        memcpy(wire.utrd, tbl, 32);
        memcpy(wire.upiu, emu.scratch + (0x60101000u - 0x60100000u), 32);
        memcpy(wire.prd, emu.scratch + (0x60101000u - 0x60100000u) + 1024, 16);
        emu.whitelist_ok = uc_command_allowed(&wire);
        emu.rings++;

        /* Complete: install the modelled response/DATA and clear the doorbell. */
        {
            uintptr_t ucd = (uintptr_t)emu.scratch + (0x60101000u - 0x60100000u);
            uintptr_t data = (uintptr_t)emu.scratch + (0x6010e000u - 0x60100000u);
            uint32_t *resp = (uint32_t *)(ucd + 512);
            memset(resp, 0, 52);
            memcpy(resp, emu.response, (size_t)emu.response_partial_words * 4u);
            if (emu.payload_enabled) memcpy((void *)data, emu.payload, 8);
            if (emu.tail_corrupt_word) {
                uint32_t *tail = (uint32_t *)(data + 8);
                tail[emu.tail_corrupt_word - 1u] ^= 1u;
            }
            tbl[2] = emu.ocs_value;
            *(volatile uint32_t *)(emu.regs + 0x58) = 0u;
        }
    }
}

static struct ur_lease *shared_record(void)
{ return (struct ur_lease *)(emu.scratch + (0x60100000u - 0x60100000u)); }

#include "hakios.mt6895_ufs_capacity.c"
#undef printf

static uint32_t held(void) { return us_control(&lease); }
static uint32_t phase(void) { return held() & US_PHASE_MASK; }
static uint32_t shared_state(void) { return shared_record()->state; }

static void fresh_lease(void)
{
    struct ur_lease *r = shared_record();
    r->magic = UR_MAGIC; r->version = UR_VERSION; r->trial = UR_TRIAL; r->state = UR_UNUSED;
}

static void healthy(void)
{
    uint32_t *r = (uint32_t *)emu.regs;
    uint32_t *tbl = (uint32_t *)(emu.table + LIST_OFF);
    uint32_t *ucd = (uint32_t *)(emu.scratch + (0x60101000u - 0x60100000u));

    memset(&emu, 0, sizeof(emu));
    emu.response_partial_words = 13;
    r = (uint32_t *)emu.regs;
    r[0]      = 0x3587071fu;
    r[8/4]    = 0x200u;
    r[0x30/4] = 0x10fu;
    r[0x34/4] = 1u;
    r[0x50/4] = (uint32_t)UR_LIST_PA;
    r[0x54/4] = 0u;
    r[0x58/4] = 0u;
    r[0x60/4] = 1u;
    emu.response[0] = 0x21; emu.response[2] = 2;

    /* Inherited READ(10) descriptor, planted at the ACTUAL slot the adapter will map. */
    memset(tbl, 0, 32); memset(ucd, 0, 32);
    tbl[0] = 0x14000000u; tbl[2] = 0u; tbl[4] = 0x60101000u;
    tbl[5] = 0u; tbl[6] = 0x00800080u; tbl[7] = 0x01000001u;
    ucd[0] = 0x00024001u;    /* UPIU 01 40 02 00 */
    ucd[3] = 0x00100000u;    /* bytes 12..15 BE = 4096 */
    ucd[4] = 0x00000028u;    /* c[16] = 0x28 */
    ucd[5] = 0x00000000u;    /* c[23] = 0 */
    ucd[6] = 0x00000001u;    /* c[24] = 1 => blocks 1 */

    fresh_lease();
}

static void geometry_payload(uint32_t last, uint32_t block)
{
    emu.payload_enabled = true;
    emu.payload[0]=(uint8_t)(last>>24);  emu.payload[1]=(uint8_t)(last>>16);
    emu.payload[2]=(uint8_t)(last>>8);   emu.payload[3]=(uint8_t)last;
    emu.payload[4]=(uint8_t)(block>>24); emu.payload[5]=(uint8_t)(block>>16);
    emu.payload[6]=(uint8_t)(block>>8);  emu.payload[7]=(uint8_t)block;
}

static void run(uint32_t value, bool present)
{
    g_present = present; g_value = value;
    hakios_mt6895_ufs_capacity_init();
}

int main(int argc, char **argv)
{
    const char *name = argc > 1 ? argv[1] : "reference";
    g_verbose = getenv("UC_VERBOSE") != NULL;

    if (!strcmp(name, "absent")) {
        healthy(); g_present = false;
        hakios_mt6895_ufs_capacity_init();
        CHECK(phase() == US_NEW && emu.rings == 0 && platform.scratch == 0);
    } else if (!strcmp(name, "zero")) {
        healthy(); run(0u, true);
        CHECK(phase() == US_NEW && emu.rings == 0);
    } else if (!strcmp(name, "invalid")) {
        healthy(); run(2u, true);
        CHECK(phase() == US_NEW && emu.rings == 0);
    } else if (!strcmp(name, "max")) {
        healthy(); run(0xffffffffu, true);   /* must be rejected as invalid, not absent */
        CHECK(phase() == US_NEW && emu.rings == 0 && platform.scratch == 0);
    } else if (!strcmp(name, "compete")) {
        healthy(); g_service = true; run(1u, true);
        CHECK(phase() == US_NEW && emu.rings == 0 && platform.scratch == 0);
    } else if (!strcmp(name, "compete_read")) {
        healthy(); g_read = true; run(1u, true);
        CHECK(phase() == US_NEW && emu.rings == 0 && platform.scratch == 0);
    } else if (!strcmp(name, "compete_probe")) {
        healthy(); g_probe = true; run(1u, true);
        CHECK(phase() == US_NEW && emu.rings == 0 && platform.scratch == 0);
    } else if (!strcmp(name, "pmap")) {
        healthy(); emu.pmap_valid = true; run(1u, true);
        CHECK(phase() == US_NEW && platform.scratch == 0 && emu.rings == 0);
    } else if (!strcmp(name, "mapfail")) {
        healthy(); emu.map_fail = true; run(1u, true);
        CHECK(phase() == US_NEW && emu.rings == 0);
    } else if (!strcmp(name, "stale")) {
        healthy(); shared_record()->state = UR_CLAIMED; run(1u, true);
        CHECK(phase() == US_NEW && emu.rings == 0);
    } else if (!strcmp(name, "badflags")) {
        healthy(); ((uint32_t *)emu.regs)[0x30/4] = 0x100u; run(1u, true);
        CHECK(emu.rings == 0 && phase() != US_ACTIVE);
    } else if (!strcmp(name, "reference")) {
        healthy(); geometry_payload(31240191u, 4096u); run(1u, true);
        CHECK(emu.rings == 1);
        CHECK(emu.whitelist_ok);                 /* exact 80-byte RC10 on the wire */
        CHECK(outcome.query_valid == 1);
        CHECK(outcome.decode == UC_DECODE_OK);
        CHECK(outcome.geometry.last_lba == 31240191u);
        CHECK(outcome.geometry.logical_block_bytes == 4096u);
        CHECK(outcome.geometry.block_count == 31240192ull);
        CHECK(outcome.geometry.capacity_bytes == 127959826432ull);
        CHECK(outcome.android_reference_matches == 1);
        CHECK(outcome.restored == 1);
        CHECK(outcome.tail_ok == 1 && outcome.tail_complete == 1);
        CHECK(outcome.tail_checked_words == 1022u);
        CHECK(phase() == US_IDLE && shared_state() == UR_CLAIMED);
        /* repeated init in the same process must not issue another command */
        hakios_mt6895_ufs_capacity_init();
        CHECK(emu.rings == 1 && phase() == US_IDLE);
        /* clean quiesce: IDLE -> shared RESTORED, no MMIO/writes */
        {
            unsigned mr = emu.mem_reads, mw = emu.mem_writes;
            hakios_mt6895_ufs_capacity_quiesce();
            CHECK(shared_state() == UR_RESTORED);
            CHECK(phase() == US_IDLE);
            CHECK(emu.mem_reads == mr && emu.mem_writes == mw);
        }
    } else if (!strcmp(name, "geometry512")) {
        healthy(); geometry_payload(31240191u, 512u); run(1u, true);
        CHECK(outcome.query_valid == 1 && outcome.decode == UC_DECODE_OK);
        CHECK(outcome.geometry.logical_block_bytes == 512u);
        CHECK(outcome.android_reference_matches == 0);
        CHECK(phase() == US_IDLE && shared_state() == UR_CLAIMED);
    } else if (!strcmp(name, "saturated")) {
        healthy(); geometry_payload(0xffffffffu, 4096u); run(1u, true);
        CHECK(outcome.query_valid == 1);
        CHECK(outcome.decode == UC_RC10_SATURATED);
        CHECK(outcome.android_reference_matches == 0);
        CHECK(phase() == US_IDLE && shared_state() == UR_CLAIMED);
    } else if (!strcmp(name, "zeroblock")) {
        healthy(); geometry_payload(31240191u, 0u); run(1u, true);
        CHECK(outcome.query_valid == 1 && outcome.decode == UC_ZERO_BLOCK_BYTES);
        CHECK(phase() == US_IDLE && shared_state() == UR_CLAIMED);
    } else if (!strcmp(name, "unchanged")) {
        healthy(); emu.leave_prefill = true; run(1u, true);
        CHECK(outcome.result == UC_QUERY_DATA_UNCHANGED);
        CHECK(outcome.query_valid == 0 && outcome.decode == UC_BAD_ARGUMENT);
        CHECK(phase() == US_IDLE && shared_state() == UR_CLAIMED);
    } else if (!strcmp(name, "tail")) {
        healthy(); geometry_payload(31240191u, 4096u);
        emu.tail_corrupt_word = 7u; run(1u, true);
        CHECK(outcome.result == UC_QUERY_TAIL_CHANGED);
        CHECK(outcome.tail_ok == 0 && outcome.tail_first_bad != UINT32_MAX);
        CHECK(outcome.query_valid == 0);
    } else if (!strcmp(name, "rejected")) {
        healthy(); geometry_payload(31240191u, 4096u);
        emu.response[0] = 0x00; run(1u, true);
        CHECK(outcome.response_failures != 0);
        CHECK(outcome.query_valid == 0);
        CHECK(phase() == US_IDLE && shared_state() == UR_CLAIMED);
    } else if (!strcmp(name, "reserved_ocs")) {
        healthy(); geometry_payload(31240191u, 4096u);
        emu.ocs_value = 0x0bu; run(1u, true);
        CHECK(outcome.result == UC_QUERY_UNCERTAIN);
        CHECK(held() & US_POISON_BIT);
        CHECK(shared_state() == UR_POISONED);
    } else if (!strcmp(name, "stop_at_drain")) {
        healthy(); geometry_payload(31240191u, 4096u);
        emu.stop_at_drain = true; run(1u, true);
        /* STOP at clean DRAINED: RESTORED is published, never POISONED. */
        CHECK(phase() == US_DRAINED);
        CHECK(held() & US_STOP_BIT);
        CHECK(held() & US_RELEASED_BIT);
        CHECK(!(held() & US_POISON_BIT));
        CHECK(shared_state() == UR_RESTORED);
        CHECK(outcome.query_valid == 0);
        hakios_mt6895_ufs_capacity_quiesce();
        CHECK(!(held() & US_POISON_BIT));
        CHECK(shared_state() == UR_RESTORED);
    } else {
        fprintf(stderr, "unknown scenario %s\n", name);
        return 2;
    }

    fprintf(stdout, "%s: PASS %u checks (rings=%u whitelist=%d shared=%u ctrl=%#x)\n",
            name, checks, emu.rings, (int)emu.whitelist_ok,
            (unsigned)shared_state(), held());
    return 0;
}
