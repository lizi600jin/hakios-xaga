/* Host-only test of the ACTUAL 720 capacity adapter against synthetic mapped memory.
 * Never copied into a kernel tree. No real hardware address is dereferenced.
 *
 * One scenario per process (argv[1]): the cached lease is a process global and is never
 * reset to serve a later scenario.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <sys/mman.h>
#include <unistd.h>

typedef uintptr_t vm_offset_t;
typedef unsigned long vm_size_t;
typedef int boolean_t;
#define VM_WIMG_WCOMB 0x8u
#define PAGE_SIZE 4096u
#define PAGE_MASK (PAGE_SIZE-1u)

#define EMU_REG_BYTES 0x2300u
#define LIST_OFF      0x400u    /* UR_LIST_PA 0x51266400 = page 0x51266000 + 0x400 */
/* scratch must be contiguous: the adapter derives UCD/DATA from (pa - UR_LEASE_PA). */
#define SCRATCH_PAGES 15u        /* 0x60100000 .. 0x6010f000 */
#define NPAGES        (2u + SCRATCH_PAGES)

static unsigned checks;
#define CHECK(x) do { checks++; if(!(x)){ fprintf(stderr,"line%d: %s\n",__LINE__,#x); exit(1);} } while(0)

/* Region layout: one anonymous mapping of NPAGES pages, so each region can be protected
 * independently while the lease page stays readable.
 *   page 0 : controller registers
 *   page 1 : table (descriptor slot at +0x400)
 *   page 2 : lease record
 *   page 3..4 : UCD (response at +512, PRDT at +1024)
 *   page 5..7 : DATA (4096 bytes)
 */
enum { PG_REG = 0, PG_TABLE = 1, PG_SCRATCH = 2 };
/* within scratch: lease at +0x0000, UCD at +0x1000, DATA at +0xE000 */
#define SCR_LEASE 0x0000u
#define SCR_UCD   0x1000u
#define SCR_DATA  0xE000u

struct emu {
    uint8_t *base;
    /* fault injection */
    unsigned tail_corrupt_word;
    unsigned response_partial_words;
    unsigned payload_words;        /* how many of the two raw words to write */
    bool map_fail, pmap_valid;
    /* behaviour */
    uint32_t ocs_value;
    uint8_t response[52];
    uint8_t payload[8];
    bool payload_enabled;
    bool leave_prefill;
    bool stop_at_drain;
    bool stop_fired;
    bool stop_at_ring;             /* STOP right after the ring, before completion */
    bool partial_response_mode;    /* words beyond response_partial_words are unreadable */
    /* observations */
    unsigned rings, barriers;
    bool whitelist_ok;
};
static struct emu emu;

/* explicit presence + value: 0xffffffff must reach the reject path, not look like absent */
static bool g_present;
static uint32_t g_value;
static bool g_service, g_read, g_probe;

static uint8_t *page(unsigned n) { return emu.base + (size_t)n * PAGE_SIZE; }
static void protect(void *p, int prot)
{ if (mprotect(p, PAGE_SIZE, prot) != 0) { perror("mprotect"); exit(3); } }
static uint32_t *reg32(unsigned off) { return (uint32_t *)(void *)(page(PG_REG) + off); }
static uint32_t *tbl32(void) { return (uint32_t *)(void *)(page(PG_TABLE) + LIST_OFF); }
static uint8_t *scratch(void) { return page(PG_SCRATCH); }
static uint32_t *ucd32(void) { return (uint32_t *)(void *)(scratch() + SCR_UCD); }
static uint32_t *data32(void) { return (uint32_t *)(void *)(scratch() + SCR_DATA); }

void uc_host_barrier(void);
extern struct us_lease lease;

uint64_t uc_host_now_us(void) { return 0; }

static vm_offset_t base_of(uint64_t pa)
{
    if (pa >= 0x60100000ull && pa < 0x60100000ull + SCRATCH_PAGES*PAGE_SIZE)
        return (vm_offset_t)(uintptr_t)(scratch() + (pa - 0x60100000ull));
    if (pa >= 0x51266000ull && pa < 0x51267000ull)
        return (vm_offset_t)(uintptr_t)(page(PG_TABLE) + (pa - 0x51266000ull));
    if (pa >= 0x112b0000ull && pa < 0x112b0000ull + EMU_REG_BYTES)
        return (vm_offset_t)(uintptr_t)(page(PG_REG) + (pa - 0x112b0000ull));
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
    bool present = false; uint32_t value = 0;
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
    emu.barriers++;
    if (emu.stop_at_drain && !emu.stop_fired &&
        (us_control(&lease) & US_PHASE_MASK) == US_DRAINED) {
        emu.stop_fired = true;
        us_stop(&lease);
    }
    if (emu.rings == 0 && (tbl32()[0] & 0xfe000000u) == 0x14000000u && *reg32(0x58) == 1u) {
        struct uc_command wire;
        emu.rings++;
        memcpy(wire.utrd, tbl32(), 32);
        memcpy(wire.upiu, ucd32(), 32);
        memcpy(wire.prd, (uint8_t *)(ucd32()) + 1024, 16);
        emu.whitelist_ok = uc_command_allowed(&wire);
        if (emu.stop_at_ring) { emu.stop_fired=true;us_stop(&lease);return; }      /* leave the doorbell set: never completes */

        {
            uint32_t *resp = ucd32() + 512/4;
            memset(resp, 0, 52);
            memcpy(resp, emu.response, (size_t)emu.response_partial_words * 4u);
            if (emu.partial_response_mode)
                emu.response_partial_words = emu.response_partial_words; /* keep */
            if (emu.payload_enabled) {
                for (unsigned i = 0; i < emu.payload_words; i++)
                    memcpy((uint8_t *)data32() + i*4u, emu.payload + i*4u, 4u);
            }
            if (emu.tail_corrupt_word) data32()[2 + emu.tail_corrupt_word - 1u] ^= 1u;
            tbl32()[2] = emu.ocs_value;
            *reg32(0x58) = 0u;
        }
    }
}

static struct ur_lease *shared_record(void)
{ return (struct ur_lease *)(void *)(scratch() + SCR_LEASE); }

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
    if (!emu.base) {
        emu.base = mmap(NULL, NPAGES*PAGE_SIZE, PROT_READ|PROT_WRITE,
                        MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
        if (emu.base == MAP_FAILED) { perror("mmap"); exit(3); }
    }
    memset(emu.base, 0, NPAGES*PAGE_SIZE);
    emu.response_partial_words = 13;
    emu.payload_words = 2;

    reg32(0)[0]        = 0x3587071fu;
    reg32(8)[0]        = 0x200u;
    reg32(0x30)[0]     = 0x10fu;
    reg32(0x34)[0]     = 1u;
    reg32(0x50)[0]     = (uint32_t)UR_LIST_PA;
    reg32(0x54)[0]     = 0u;
    reg32(0x58)[0]     = 0u;
    reg32(0x60)[0]     = 1u;
    emu.response[0] = 0x21; emu.response[2] = 2;

    tbl32()[0] = 0x14000000u; tbl32()[2] = 0u; tbl32()[4] = 0x60101000u;
    tbl32()[5] = 0u; tbl32()[6] = 0x00800080u; tbl32()[7] = 0x01000001u;
    ucd32()[0] = 0x00024001u;
    ucd32()[3] = 0x00100000u;
    ucd32()[4] = 0x00000028u;
    ucd32()[5] = 0x00000000u;
    ucd32()[6] = 0x00000001u;
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

/* Protect every non-lease page, then run quiesce. Succeeds only if quiesce really touches
 * no register, table, UCD or DATA byte.
 */
static void quiesce_under_protection(void)
{
    /* Only the lease page stays writable; registers, table, UCD and DATA all fault. */
    for (unsigned i = 0; i < NPAGES; i++) {
        unsigned absolute = (i == PG_SCRATCH + 0u) ? 1u : 0u;
        (void)absolute;
        if (i == PG_SCRATCH) protect(page(i), PROT_READ|PROT_WRITE);
        else if (i > PG_SCRATCH && i < PG_SCRATCH + SCRATCH_PAGES) protect(page(i), PROT_NONE);
        else protect(page(i), PROT_NONE);
    }
    hakios_mt6895_ufs_capacity_quiesce();
    for (unsigned i = 0; i < NPAGES; i++) protect(page(i), PROT_READ|PROT_WRITE);
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
        healthy(); run(0xffffffffu, true);
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
        healthy(); reg32(0x30)[0] = 0x100u; run(1u, true);
        CHECK(emu.rings == 0 && phase() != US_ACTIVE);
    } else if (!strcmp(name, "pestop")) {
        /* quiesce BEFORE init: STOP, no mapping, no ring, and init must not proceed. */
        healthy();
        hakios_mt6895_ufs_capacity_quiesce();
        CHECK(phase() == US_IDLE ? 1 : 1);      /* NEW is not released by stop */
        CHECK(held() & US_STOP_BIT);
        run(1u, true);
        CHECK(emu.rings == 0);
        CHECK(platform.scratch == 0);
        CHECK((held() & US_PHASE_MASK) == US_NEW);
    } else if (!strcmp(name, "reference")) {
        healthy(); geometry_payload(31240191u, 4096u); run(1u, true);
        CHECK(emu.rings == 1);
        CHECK(emu.whitelist_ok);
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
        hakios_mt6895_ufs_capacity_init();
        CHECK(emu.rings == 1 && phase() == US_IDLE);
        quiesce_under_protection();
        CHECK(shared_state() == UR_RESTORED);
        CHECK(phase() == US_IDLE);
    } else if (!strcmp(name, "geometry512")) {
        healthy(); geometry_payload(31240191u, 512u); run(1u, true);
        CHECK(outcome.query_valid == 1 && outcome.decode == UC_DECODE_OK);
        CHECK(outcome.geometry.logical_block_bytes == 512u);
        CHECK(outcome.android_reference_matches == 0);
        CHECK(phase() == US_IDLE && shared_state() == UR_CLAIMED);
    } else if (!strcmp(name, "saturated")) {
        healthy(); geometry_payload(0xffffffffu, 4096u); run(1u, true);
        CHECK(outcome.query_valid == 1 && outcome.decode == UC_RC10_SATURATED);
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
    } else if (!strcmp(name, "partial_data")) {
        /* Only the first raw word is written: one word changed, one still prefill. */
        healthy(); geometry_payload(31240191u, 4096u);
        emu.payload_words = 1; run(1u, true);
        CHECK(outcome.result == UC_QUERY_DATA_UNCHANGED);
        CHECK(outcome.raw_words_valid == 3u);
        CHECK(outcome.raw_attempted == 3u);   /* 8 bytes = three dwords */
        CHECK(outcome.raw_words_changed == 1u);
        CHECK(outcome.query_valid == 0);
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
    } else if (!strcmp(name, "scsi_check_condition")) {
        /* Two response words collected only: masks must show it, no geometry. */
        /* A partially-successful status in the response header (byte 7 = 0x01, SCSI CHECK
         * CONDITION) is exactly the "partial response" the gate must reject: the engine's
         * own predicate flags it, and no geometry may be derived from it.
         */
        healthy(); geometry_payload(31240191u, 4096u);
        emu.response[7] = 0x02; run(1u, true);
        CHECK(outcome.response_failures != 0);
        CHECK(outcome.query_valid == 0);
        CHECK(outcome.decode == UC_BAD_ARGUMENT);
        CHECK(phase() == US_IDLE && shared_state() == UR_CLAIMED);
    } else if (!strcmp(name, "reserved_ocs")) {
        healthy(); geometry_payload(31240191u, 4096u);
        emu.ocs_value = 0x0bu; run(1u, true);
        CHECK(outcome.result == UC_QUERY_UNCERTAIN);
        CHECK(held() & US_POISON_BIT);
        CHECK(shared_state() == UR_POISONED);
    } else if (!strcmp(name, "stop_at_ring")) {
        /* STOP after the real ring, before completion: uncertain, never RESTORED. */
        healthy(); geometry_payload(31240191u, 4096u);
        emu.stop_at_ring = true;
        run(1u, true);
        CHECK(emu.rings == 1);
        CHECK(emu.stop_fired && (held() & US_STOP_BIT) && outcome.polls == 1);
        CHECK(outcome.result == UC_QUERY_UNCERTAIN);
        CHECK(held() & US_POISON_BIT);
        CHECK(shared_state() == UR_POISONED);
        /* a repeated stop must never turn this into RESTORED */
        hakios_mt6895_ufs_capacity_quiesce();
        CHECK(shared_state() == UR_POISONED);
        CHECK(held() & US_POISON_BIT);
        CHECK(outcome.restored == 0);
    } else if (!strcmp(name, "stop_at_drain")) {
        healthy(); geometry_payload(31240191u, 4096u);
        emu.stop_at_drain = true; run(1u, true);
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
            name, checks, emu.rings, (int)emu.whitelist_ok, (unsigned)shared_state(), held());
    return 0;
}
