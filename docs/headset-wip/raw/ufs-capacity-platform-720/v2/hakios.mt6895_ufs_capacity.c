/* Trial720: one synchronous guarded RC10 geometry query. No block device, no worker,
 * no cdev/VFS, no dynamic allocation, no mutex. Not installed in production.
 */
#ifdef US_HOST_TEST
#include "ufs_capacity_test_env.h"
#else
#include <stdint.h>
#include <stdbool.h>
#include <mach/mach_types.h>
#include <machine/machine_routines.h>
#include <kern/clock.h>
#include <pexpert/pexpert.h>
#include <sys/systm.h>
#include <vm/vm_kern.h>
#include <vm/memory_types.h>
#endif
/* The frozen capacity engine calls into us_* lease helpers but includes only the read
 * engine and the codec; supply the lease contract first (the same order 719 uses).
 */
#include "support/ufs_service_lease.h"
#include "ufs_capacity/ufs_capacity_engine.h"

#ifdef HAKIOS_UFS
#error The legacy native UFS driver conflicts with the inherited capacity queue
#endif

#ifndef US_HOST_TEST
extern vm_offset_t ml_io_map(vm_offset_t, vm_size_t);
extern vm_offset_t ml_io_map_unmappable(vm_offset_t, vm_size_t, unsigned int);
extern boolean_t pmap_valid_address(uint64_t);
extern unsigned long gPhysBase, gPhysSize;
#endif

#define UC_REG_PA      0x112b0000u
#define UC_REG_BYTES   0x2300u
#define UC_SCRATCH_BYTES ((vm_offset_t)(UR_DATA_PA + UR_BLOCK_BYTES - UR_LEASE_PA))

struct uc_platform { vm_offset_t regs, table, scratch; };
static struct uc_platform platform;
static bool capacity_publish(void *, uint32_t);
/* Statically initialised once. Never reset: quiesce may already have set STOP. */
struct us_lease lease = { 0, capacity_publish, &platform };
static struct ur_workspace workspace;
static struct uc_outcome outcome;

static void capacity_barrier(void *unused)
{
	(void)unused;
#ifdef US_HOST_TEST
	/* Test-only stand-in for dsb sy. The emulator uses this single synchronisation point
	 * to complete a rung command, exactly where a real controller would have finished.
	 * The production branch below is the real barrier and is compiled unchanged.
	 */
	uc_host_barrier();
#else
	__asm__ volatile("dsb sy" ::: "memory");
#endif
}

static volatile struct ur_lease *shared(struct uc_platform *p)
{ return (volatile struct ur_lease *)(uintptr_t)p->scratch; }

static struct ur_lease shared_snapshot(struct uc_platform *p)
{
	volatile struct ur_lease *r = shared(p);
	struct ur_lease copy = { r->magic, r->version, r->trial, r->state };
	capacity_barrier(NULL);
	return copy;
}

static bool capacity_publish(void *context, uint32_t state)
{
	struct uc_platform *p = context;
	if (!p->scratch) return false;
	shared(p)->state = state;
	capacity_barrier(NULL);
	return shared(p)->state == state;
}

static bool capacity_read_reg(void *context, uint16_t offset, uint32_t *out)
{
	struct uc_platform *p = context;
	if (!p->regs || !out) return false;
	switch (offset) {
	case 0: case 8: case 0x30: case 0x34: case 0x50: case 0x54: case 0x58: case 0x60:
		*out = *(const volatile uint32_t *)(p->regs + offset);
		return true;
	default:
		return false;
	}
}

static volatile uint32_t *capacity_word(struct uc_platform *p, enum ur_memory region, uint32_t off)
{
	vm_offset_t base;
	uint32_t size;
	switch (region) {
	case UR_TABLE: base = p->table; size = 32u; break;
	case UR_UCD:   base = p->scratch ? p->scratch + (UR_UCD_PA - UR_LEASE_PA) : 0;
	               size = UR_UCD_BYTES; break;
	case UR_DATA:  base = p->scratch ? p->scratch + (UR_DATA_PA - UR_LEASE_PA) : 0;
	               size = UR_BLOCK_BYTES; break;
	default: return NULL;
	}
	if (!base || (off & 3u) || off > size - 4u) return NULL;
	return (volatile uint32_t *)(base + off);
}

static bool capacity_read_word(void *context, enum ur_memory region, uint32_t off, uint32_t *out)
{
	volatile uint32_t *word = capacity_word(context, region, off);
	if (!word || !out) return false;
	*out = *word;
	return true;
}

static bool capacity_write_word(void *context, enum ur_memory region, uint32_t off, uint32_t value)
{
	volatile uint32_t *word = capacity_word(context, region, off);
	if (!word || us_control(&lease) != US_ACTIVE) return false;
	*word = value;
	return true;
}

static bool capacity_claim(void *context)
{
	struct ur_lease r = shared_snapshot(context);
	return us_control(&lease) == US_ACTIVE && ur_lease_valid(&r) && r.state == UR_CLAIMED;
}

static bool capacity_finish(void *unused, bool restored)
{
	bool drained;
	(void)unused;
	if (!restored) { us_poison(&lease); return false; }
	drained = us_mark_drained(&lease);   /* never releases the outer lease */
	/* Publish the DRAINED transition before anyone may observe it. Not an IO callback:
	 * no register, table or data access happens here.
	 */
	capacity_barrier(NULL);
	return drained;
}

static bool capacity_stopped(void *unused)
{ (void)unused; return us_stopped(&lease); }

static bool capacity_ring(void *context)
{
	struct uc_platform *p = context;
	if (us_control(&lease) != US_ACTIVE || shared(p)->state != UR_CLAIMED) return false;
	*(volatile uint32_t *)(p->regs + 0x58) = 1u;   /* sole controller store */
	capacity_barrier(NULL);
	return true;
}

static uint64_t capacity_now_us(void *unused)
{
#ifndef US_HOST_TEST
	uint64_t ns;
	(void)unused;
	absolutetime_to_nanoseconds(mach_absolute_time(), &ns);
	return ns / 1000;
#else
	(void)unused;
	return uc_host_now_us();
#endif
}

static struct ur_io capacity_io(void)
{
	struct ur_io io = { capacity_read_reg, capacity_read_word, capacity_write_word,
	    capacity_claim, capacity_finish, capacity_stopped, capacity_ring,
	    capacity_barrier, capacity_now_us, &platform };
	return io;
}

static bool clean_baseline(const struct ur_io *io)
{
	return ur_idle(io) &&
	    ur_copy_from(io, UR_TABLE, workspace.table, 8) &&
	    ur_copy_from(io, UR_UCD, workspace.ucd, UR_UCD_BYTES/4) &&
	    ur_previous_read(&workspace) &&
	    ur_copy_from(io, UR_DATA, workspace.data, UR_BLOCK_BYTES/4) &&
	    ur_same(io, UR_TABLE, workspace.table, 8) &&
	    ur_same(io, UR_UCD, workspace.ucd, UR_UCD_BYTES/4) &&
	    ur_same(io, UR_DATA, workspace.data, UR_BLOCK_BYTES/4) &&
	    ur_idle(io);
}

void hakios_mt6895_ufs_capacity_init(void);
void hakios_mt6895_ufs_capacity_quiesce(void);

/* Cached software STOP and aligned shared-DRAM publication only.
 * No mutex, no wait, no MMIO, no logger. Other CPUs may not all have stopped.
 */
void hakios_mt6895_ufs_capacity_quiesce(void)
{
	us_stop(&lease);
}

void hakios_mt6895_ufs_capacity_init(void)
{
	uint32_t mode = 0, old_service = 0, old_read = 0, old_probe = 0, phase;
	struct ur_lease initial;
	struct ur_io io;

	if (!PE_parse_boot_argn("mtk_ufs_capacity", &mode, sizeof(mode)) || !mode) return;
	(void)PE_parse_boot_argn("mtk_ufs_service", &old_service, sizeof(old_service));
	(void)PE_parse_boot_argn("mtk_ufs_read", &old_read, sizeof(old_read));
	(void)PE_parse_boot_argn("mtk_ufs_probe", &old_probe, sizeof(old_probe));
	if (mode != 1 || old_service || old_read || old_probe) {
		printf("mt6895-ufs-capacity: invalid or competing mode; no access\n");
		return;
	}
	/* A repeated init after any non-NEW phase must not touch the controller. */
	if (us_control(&lease) != US_NEW) return;

	printf("mt6895-ufs-capacity: single RC10 lun2 data_bytes8 scratch_bytes4096; "
	       "guarded handoff required\n");

	if (!ur_outside_vm(UR_LIST_PA, 32, gPhysBase, gPhysSize) ||
	    !ur_outside_vm(UR_LEASE_PA, UC_SCRATCH_BYTES, gPhysBase, gPhysSize) ||
	    pmap_valid_address(UR_LIST_PA) || pmap_valid_address(UR_LIST_PA + 31) ||
	    pmap_valid_address(UR_LEASE_PA) ||
	    pmap_valid_address(UR_DATA_PA + UR_BLOCK_BYTES - 1)) {
		printf("mt6895-ufs-capacity: scratch overlaps managed memory; no mapping\n");
		return;
	}
	platform.scratch = ml_io_map_unmappable(UR_LEASE_PA, UC_SCRATCH_BYTES, VM_WIMG_WCOMB);
	if (!platform.scratch) {
		printf("mt6895-ufs-capacity: scratch mapping failed; no query\n");
		return;
	}
	initial = shared_snapshot(&platform);
	printf("mt6895-ufs-capacity: guard magic=%#x version=%u trial=%u state=%u\n",
	       initial.magic, initial.version, initial.trial, initial.state);
	if (!ur_lease_valid(&initial) || initial.state != UR_UNUSED || us_stopped(&lease)) return;

	platform.table = ml_io_map_unmappable(UR_LIST_PA & ~(uint64_t)PAGE_MASK, PAGE_SIZE,
	                                     VM_WIMG_WCOMB);
	if (!platform.table) return;
	platform.table += UR_LIST_PA & PAGE_MASK;
	platform.regs = ml_io_map(UC_REG_PA, UC_REG_BYTES);
	if (!platform.regs || us_stopped(&lease)) return;

	io = capacity_io();
	if (!clean_baseline(&io) || !us_open(&lease, true)) {
		printf("mt6895-ufs-capacity: baseline/open refused; no query\n");
		return;
	}
	printf("mt6895-ufs-capacity: opened baseline_verified=1 control=%#x shared=%u\n",
	       us_control(&lease), shared_snapshot(&platform).state);

	/* Admit exactly once: IDLE -> ACTIVE. io.claim only ever verifies this cached ACTIVE
	 * together with the shared CLAIMED record; it never publishes CLAIMED again.
	 */
	if (!us_admit(&lease)) {
		printf("mt6895-ufs-capacity: admission refused; lease held at idle\n");
		return;
	}
	uc_run(&io, &workspace, &outcome, 500000u, 1000000u);

	/* Reportability requires the engine's restore certification AND a clean DRAINED.
	 * A STOP that arrives while the lease is already DRAINED is a CLEAN close: us_stop has
	 * published RESTORED, so poisoning here would violate that invariant. In that case we
	 * suppress delivery and return without further hardware IO or a second publication.
	 */
	phase = us_control(&lease) & US_PHASE_MASK;
	if (!outcome.restored || phase != US_DRAINED) {
		if (us_control(&lease) & US_RELEASED_BIT) {
			/* STOP already closed this lease cleanly; never poison a released lease. */
			outcome.query_valid = 0;
			printf("mt6895-ufs-capacity: stop released the lease before completion; "
			       "no result\n");
		} else {
			us_poison(&lease);
			outcome.query_valid = 0;
			printf("mt6895-ufs-capacity: uncertain after query; lease poisoned\n");
		}
	} else if (!us_copy_done(&lease)) {
		/* STOP won the DRAINED->IDLE CAS. That is a clean no-delivery path: no cleanup,
		 * no poison, no hardware access is needed or allowed here.
		 */
		outcome.query_valid = 0;
		printf("mt6895-ufs-capacity: stop closed the service before result delivery\n");
	}

	printf("mt6895-ufs-capacity: result=%u claimed=%u issued=%u memory_restored=%u "
	       "query_valid=%u polls=%u\n",
	       (unsigned)outcome.result, (unsigned)outcome.claimed, (unsigned)outcome.issued,
	       (unsigned)outcome.restored, (unsigned)outcome.query_valid, outcome.polls);

	if (outcome.response_attempted) {
		printf("mt6895-ufs-capacity: ocs=%#x response_valid=%#x attempted=%#x failures=%#x\n",
		       outcome.ocs, outcome.response_valid, outcome.response_attempted,
		       outcome.response_failures);
		if (outcome.response_valid == (uint32_t)0x1fff) {
			printf("mt6895-ufs-capacity: response_words");
			for (unsigned i = 0; i < 13; i++)
				printf(" %08x", (unsigned)ur_le32(outcome.response + i*4));
			printf("\n");
		}
	}

	/* Emitted for EVERY collected outcome, including partial responses, nonzero OCS and
	 * prepare failures. The masks state what was actually collected, so an all-zero mask is
	 * an explicit "uncollected", never a fabricated value. Only the two raw words themselves
	 * stay behind the valid==3 guard.
	 */
	printf("mt6895-ufs-capacity: raw valid=%#x attempted=%#x changed=%#x",
	       outcome.raw_words_valid, outcome.raw_attempted, outcome.raw_words_changed);
	if (outcome.raw_words_valid == 3u)
		printf(" %08x %08x", (unsigned)ur_le32(outcome.raw),
		       (unsigned)ur_le32(outcome.raw + 4));
	printf("\n");
	printf("mt6895-ufs-capacity: tail complete=%u ok=%u checked_words=%u first_bad=%#x\n",
	       (unsigned)outcome.tail_complete, (unsigned)outcome.tail_ok,
	       outcome.tail_checked_words, outcome.tail_first_bad);

	if (outcome.query_valid && outcome.decode == UC_DECODE_OK) {
		printf("mt6895-ufs-capacity: decode=%u last_lba=%#x block_bytes=%u blocks=%llu "
		       "capacity_bytes=%llu android_match=%u\n",
		       (unsigned)outcome.decode, outcome.geometry.last_lba,
		       outcome.geometry.logical_block_bytes,
		       (unsigned long long)outcome.geometry.block_count,
		       (unsigned long long)outcome.geometry.capacity_bytes,
		       (unsigned)outcome.android_reference_matches);
	} else {
		printf("mt6895-ufs-capacity: decode=%u; geometry unavailable\n",
		       (unsigned)outcome.decode);
	}

	printf("mt6895-ufs-capacity: final control=%#x shared=%u idle_close_deferred=%u\n",
	       us_control(&lease), shared_snapshot(&platform).state,
	       (unsigned)(us_control(&lease) == US_IDLE));
}
