/* Trial716 platform candidate: one fixed READ10, with a separately reviewed loader gate.
 * Not installed in production. No block device is exposed.
 */
#include <stdint.h>
#include <stdbool.h>
#include <mach/mach_types.h>
#include <machine/machine_routines.h>
#include <kern/clock.h>
#include <pexpert/pexpert.h>
#include <sys/systm.h>
#include <vm/vm_kern.h>
#include <vm/memory_types.h>
#include "ufs_read_engine.h"
#include "ufs_atomic_lease.h"

extern vm_offset_t ml_io_map(vm_offset_t, vm_size_t);
extern vm_offset_t ml_io_map_unmappable(vm_offset_t, vm_size_t, unsigned int);
extern boolean_t pmap_valid_address(uint64_t);
extern unsigned long gPhysBase, gPhysSize;

#define UR_REG_PA 0x112b0000u
#define UR_REG_BYTES 0x2300u
#define UR_SCRATCH_BYTES ((vm_size_t)(UR_DATA_PA + UR_BLOCK_BYTES - UR_LEASE_PA))
#define UR_EXPECTED_CRC32 0x28a2836cu /* independently validated Android LUN2/LBA1 reference */

struct ur_platform { vm_offset_t regs, table, scratch; };
static struct ur_platform platform;
static bool platform_publish(void *, uint32_t);
/* Do not reset this cached software phase in init: quiesce may already have set STOP. */
static struct ur_atomic_lease lease = { 0, platform_publish, &platform };
static struct ur_workspace workspace;
static struct ur_outcome outcome;

static void platform_barrier(void *unused)
{
    (void)unused;
    __asm__ volatile("dsb sy" ::: "memory");
}

static volatile struct ur_lease *shared(struct ur_platform *p)
{ return (volatile struct ur_lease *)(uintptr_t)p->scratch; }

static struct ur_lease shared_snapshot(struct ur_platform *p)
{
    volatile struct ur_lease *r = shared(p);
    struct ur_lease copy = { r->magic, r->version, r->trial, r->state };
    platform_barrier(NULL);
    return copy;
}

static bool platform_publish(void *context, uint32_t state)
{
    struct ur_platform *p = context;
    if (!p->scratch) return false;
    shared(p)->state = state; /* aligned32-bit DRAM store, not an atomic MMIO operation */
    platform_barrier(NULL);
    return shared(p)->state == state;
}

static bool platform_read_reg(void *context, uint16_t offset, uint32_t *out)
{
    struct ur_platform *p = context;
    if (!p->regs || !out) return false;
    switch (offset) {
    case 0: case 8: case 0x30: case 0x34: case 0x50: case 0x54: case 0x58: case 0x60:
        *out = *(const volatile uint32_t *)(p->regs + offset);
        return true;
    default: return false;
    }
}

static volatile uint32_t *platform_word(struct ur_platform *p, enum ur_memory region, uint32_t off)
{
    vm_offset_t base;
    uint32_t size;
    switch (region) {
    case UR_TABLE: base = p->table; size = 32; break;
    case UR_UCD: base = p->scratch ? p->scratch + (UR_UCD_PA - UR_LEASE_PA) : 0;
        size = UR_UCD_BYTES; break;
    case UR_DATA: base = p->scratch ? p->scratch + (UR_DATA_PA - UR_LEASE_PA) : 0;
        size = UR_BLOCK_BYTES; break;
    default: return NULL;
    }
    if (!base || (off & 3) || off > size - 4) return NULL;
    return (volatile uint32_t *)(base + off);
}

static bool platform_read_word(void *context, enum ur_memory region, uint32_t off, uint32_t *out)
{
    volatile uint32_t *word = platform_word(context, region, off);
    if (!word || !out) return false;
    *out = *word;
    return true;
}

static bool platform_write_word(void *context, enum ur_memory region, uint32_t off, uint32_t value)
{
    volatile uint32_t *word = platform_word(context, region, off);
    uint32_t phase = __atomic_load_n(&lease.phase, __ATOMIC_ACQUIRE);
    if (!word || phase != UR_CLAIMED) return false;
    *word = value;
    return true;
}

static bool platform_claim(void *context)
{
    struct ur_platform *p = context;
    struct ur_lease r = shared_snapshot(p);
    if (!ur_lease_valid(&r) || r.state != UR_UNUSED) return false;
    return ur_atomic_claim(&lease);
}

static bool platform_finish(void *unused, bool restored)
{ (void)unused; return ur_atomic_finish(&lease, restored); }
static bool platform_stopped(void *unused)
{ (void)unused; return ur_atomic_stopped(&lease); }

static bool platform_ring_once(void *context)
{
    struct ur_platform *p = context;
    if (__atomic_load_n(&lease.phase, __ATOMIC_ACQUIRE) != UR_CLAIMED ||
        shared(p)->state != UR_CLAIMED) return false;
    *(volatile uint32_t *)(p->regs + 0x58) = 1u; /* sole controller store: slot0 doorbell */
    platform_barrier(NULL);
    return true;
}

static uint64_t platform_now_us(void *unused)
{
    uint64_t ns;
    (void)unused;
    absolutetime_to_nanoseconds(mach_absolute_time(), &ns);
    return ns / 1000;
}

static void report_metadata(const struct ur_io *io)
{
    uint32_t values[8] = {0}, mask = 0;
    for (unsigned i = 0; i < 8; i++) {
        if (!io->read_word(io->context, UR_TABLE, i*4, &values[i])) break;
        mask |= 1u << i;
    }
    printf("mt6895-ufs-read: inherited utrd valid=%#x", mask);
    if (mask == 0xff)
        for (unsigned i = 0; i < 8; i++) printf(" %08x", values[i]);
    printf("\n");
    if (mask != 0xff || values[4] != (uint32_t)UR_UCD_PA || values[5] != 0) return;
    mask = 0;
    for (unsigned i = 0; i < 8; i++) {
        if (!io->read_word(io->context, UR_UCD, i*4, &values[i])) break;
        mask |= 1u << i;
    }
    printf("mt6895-ufs-read: inherited upiu valid=%#x", mask);
    if (mask == 0xff)
        for (unsigned i = 0; i < 8; i++) printf(" %08x", values[i]);
    printf("\n");
}

void hakios_mt6895_ufs_read_init(void);
void hakios_mt6895_ufs_read_quiesce(void);

void hakios_mt6895_ufs_read_quiesce(void)
{
    /* Atomic software phase + shared DRAM publication only; no controller IO, waits or print. */
    ur_atomic_stop(&lease);
}

void hakios_mt6895_ufs_read_init(void)
{
    uint32_t mode = 0;
    struct ur_lease record;
    struct ur_io io = { platform_read_reg, platform_read_word, platform_write_word,
        platform_claim, platform_finish, platform_stopped, platform_ring_once,
        platform_barrier, platform_now_us, &platform };
    static const char *names[] = { "REFUSED", "NO_LEASE", "PREPARE_FAILED", "COMPLETED_ERROR",
        "REFERENCE_MISMATCH", "READ_OK", "UNCERTAIN" };

    if (!PE_parse_boot_argn("mtk_ufs_read", &mode, sizeof(mode)) || mode == 0) return;
    if (mode != 1) {
        printf("mt6895-ufs-read: mtk_ufs_read must be0 or1; no access\n");
        return;
    }
    if (ur_atomic_stopped(&lease)) return;
    printf("mt6895-ufs-read: single READ10 lun2 lba1 bytes4096; guarded ownership required\n");
    if (!ur_outside_vm(UR_LIST_PA, 32, gPhysBase, gPhysSize) ||
        !ur_outside_vm(UR_LEASE_PA, UR_SCRATCH_BYTES, gPhysBase, gPhysSize) ||
        pmap_valid_address(UR_LIST_PA) || pmap_valid_address(UR_LIST_PA + 31) ||
        pmap_valid_address(UR_LEASE_PA) || pmap_valid_address(UR_DATA_PA + UR_BLOCK_BYTES - 1)) {
        printf("mt6895-ufs-read: fixed scratch overlaps managed memory; no mapping\n");
        return;
    }
    platform.scratch = ml_io_map_unmappable(UR_LEASE_PA, UR_SCRATCH_BYTES, VM_WIMG_WCOMB);
    if (!platform.scratch) {
        printf("mt6895-ufs-read: scratch mapping failed; no request\n");
        return;
    }
    record = shared_snapshot(&platform);
    printf("mt6895-ufs-read: guard magic=%#x version=%u trial=%u state=%u\n",
           record.magic, record.version, record.trial, record.state);
    if (!ur_lease_valid(&record) || record.state != UR_UNUSED) {
        printf("mt6895-ufs-read: fresh loader guard absent; no request\n");
        return;
    }
    platform.table = ml_io_map_unmappable(UR_LIST_PA & ~(uint64_t)PAGE_MASK,
                                        PAGE_SIZE, VM_WIMG_WCOMB);
    if (!platform.table) return;
    platform.table += UR_LIST_PA & PAGE_MASK;
    platform.regs = ml_io_map(UR_REG_PA, UR_REG_BYTES);
    if (!platform.regs || ur_atomic_stopped(&lease)) return;
    if (!ur_idle(&io)) {
        printf("mt6895-ufs-read: inherited controller predicates failed; no request\n");
        return;
    }
    report_metadata(&io);
    ur_run(&io, &workspace, &outcome, UR_EXPECTED_CRC32, 500000u, 1000000u);
    printf("mt6895-ufs-read: result=%s(%u) claimed=%u issued=%u restored=%u data_valid=%u polls=%u\n",
           names[outcome.result], outcome.result, outcome.claimed, outcome.issued,
           outcome.restored, outcome.data_valid, outcome.polls);
    if (outcome.issued && outcome.result >= UR_COMPLETED_ERROR && outcome.result <= UR_READ_OK)
        printf("mt6895-ufs-read: sampled ocs=%#x\n", outcome.ocs);
    if (outcome.result == UR_READ_OK || outcome.result == UR_REFERENCE_MISMATCH)
        printf("mt6895-ufs-read: block crc32=%#x expected=%#x bytes=%u\n",
               outcome.crc32, UR_EXPECTED_CRC32, UR_BLOCK_BYTES);
    record = shared_snapshot(&platform);
    printf("mt6895-ufs-read: final shared state=%u loader_io_allowed=%u\n",
           record.state, ur_loader_may_use_storage(&record));
}
