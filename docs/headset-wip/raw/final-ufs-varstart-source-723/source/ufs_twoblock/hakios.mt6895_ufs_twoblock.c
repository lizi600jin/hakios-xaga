/* Trial722 platform for one two-block READ(10) under the inherited loader lease. */
#ifdef US_HOST_TEST
#include "ufs_twoblock_test_env.h"
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
#include "support/ufs_service_lease.h"
#include "engine/ufs_two_block_engine.h"
#include "../ufs_varread/metadata/ufs_varread_reference.h"

#ifdef US_HOST_TEST
extern void twoblock_host_stop_point(int which);
extern bool twoblock_host_bad_managed(void);
extern bool twoblock_host_claim_hook(void);
extern bool twoblock_host_finish_hook(void);
extern bool twoblock_host_restore_read_hook(void);
#endif

#ifdef HAKIOS_UFS
#error The legacy native UFS driver conflicts with the inherited two-block queue
#endif

#ifndef US_HOST_TEST
extern vm_offset_t ml_io_map(vm_offset_t, vm_size_t);
extern vm_offset_t ml_io_map_unmappable(vm_offset_t, vm_size_t, unsigned int);
extern boolean_t pmap_valid_address(uint64_t);
extern unsigned long gPhysBase, gPhysSize;
#endif

#define VR_REG_PA        0x112b0000u
#define VR_REG_BYTES     0x2300u
#define VR_SCRATCH_BYTES ((vm_offset_t)(UR_DATA_PA + VR2_TRANSFER_BYTES - UR_LEASE_PA))

#define VR2_LBA_DEFAULT 1u

_Static_assert(VR2_BLOCKS == 2u && VR2_TRANSFER_BYTES == 8192u, "two-block transfer");
_Static_assert(VR_SCRATCH_BYTES == 0x10000u, "full scratch span");

struct vr_platform { vm_offset_t regs, table, scratch; };

static struct vr_platform platform;
static bool twoblock_publish(void *, uint32_t);
static struct us_lease twoblock_lease = { 0, twoblock_publish, &platform };
static struct vrq_workspace twoblock_workspace;
static struct vrq_workspace twoblock_baseline;
static struct vrq_outcome twoblock_outcome;
static struct vrq_counters twoblock_counters;
static uint8_t twoblock_copy[VR2_TRANSFER_BYTES];


static void twoblock_barrier(void *unused)
{
	(void)unused;
#ifdef US_HOST_TEST
	vr_host_barrier();
#else
	__asm__ volatile("dsb sy" ::: "memory");
#endif
}

static volatile struct ur_lease *shared(struct vr_platform *p)
{ return (volatile struct ur_lease *)(uintptr_t)p->scratch; }

static struct ur_lease shared_snapshot(struct vr_platform *p)
{
	volatile struct ur_lease *r = shared(p);
	struct ur_lease copy = { r->magic, r->version, r->trial, r->state };
	twoblock_barrier(NULL);
	return copy;
}

#ifdef US_HOST_TEST
/* Host observation only. In the kernel build this symbol and its update do not exist, so
 * quiesce stays exactly: cached atomic control + aligned shared-DRAM publication + barrier.
 */
volatile uint32_t twoblock_published_state;
#endif

static bool twoblock_publish(void *context, uint32_t state)
{
	struct vr_platform *p = context;
	if (!p->scratch) return false;
	shared(p)->state = state;
	twoblock_barrier(NULL);
#ifdef US_HOST_TEST
	twoblock_published_state = shared(p)->state;
#endif
	return shared(p)->state == state;
}

static bool twoblock_read_reg(void *context, uint16_t offset, uint32_t *out)
{
	struct vr_platform *p = context;
	if (!p->regs || !out) return false;
	/* The SAME eight-offset whitelist runs in both builds. Nothing here is test-only. */
	switch (offset) {
	case 0: case 8: case 0x30: case 0x34: case 0x50: case 0x54: case 0x58: case 0x60:
		break;
	default:
		return false;      /* exactly these eight offsets, nothing else */
	}
#ifdef US_HOST_TEST
	/* Test-only: lets the synthetic controller complete a command or inject a read fault.
	 * It only mutates the EMULATED backing memory (or returns false); the volatile read below
	 * is the same statement the kernel build executes.
	 */
	if (!vr_host_reg_hook(offset)) return false;
#endif
	*out = *(const volatile uint32_t *)(p->regs + offset);
	return true;
}

static volatile uint32_t *twoblock_word(struct vr_platform *p, int region, uint32_t off)
{
	vm_offset_t base; uint32_t size;
	switch (region) {
	case VRQ_TABLE: base = p->table; size = 32u; break;
	case VRQ_UCD:   base = p->scratch ? p->scratch + (UR_UCD_PA - UR_LEASE_PA) : 0;
	                size = UR_UCD_BYTES; break;
	case VRQ_DATA:  base = p->scratch ? p->scratch + (UR_DATA_PA - UR_LEASE_PA) : 0;
	                size = VR2_TRANSFER_BYTES; break;
	default: return NULL;
	}
	if (!base || (off & 3u) || off > size - 4u) return NULL;
	return (volatile uint32_t *)(base + off);
}

static bool twoblock_read_word(void *context, int region, uint32_t off, uint32_t *out)
{
	volatile uint32_t *w = twoblock_word(context, region, off);
	if (!w || !out) return false;          /* region alignment and range check, shared */
#ifdef US_HOST_TEST
	/* Test-only: refuses a chosen sampling read; the load below is the shared statement. */
	if (!vr_host_read_word_hook(region, off)) return false;
#endif
	*out = *w;
	return true;
}

static bool twoblock_write_word(void *context, int region, uint32_t off, uint32_t value)
{
	volatile uint32_t *w = twoblock_word(context, region, off);
	if (!w) return false;                              /* region alignment and range, shared */
	if (us_control(&twoblock_lease) != US_ACTIVE) return false;   /* shared */
#ifdef US_HOST_TEST
	/* Test-only fault point: only refuses the write, the store below is shared. */
	if (!vr_host_write_hook(region, off)) return false;
#endif
	*w = value;
	return true;
}

/* The claim callback VERIFIES, it does not admit. The wrapper already moved the cached lease
 * IDLE->ACTIVE through us_admit, so a CAS here could return false while leaving the lease
 * ACTIVE — an ambiguous state that later quiesce could mistake for a clean baseline.
 */
static bool twoblock_claim(void *context)
{
	struct ur_lease r = shared_snapshot(context);
#ifdef US_HOST_TEST
	if (!twoblock_host_claim_hook()) return false;
#endif
	return us_control(&twoblock_lease) == US_ACTIVE && ur_lease_valid(&r) && r.state == UR_CLAIMED;
}

static bool twoblock_finish(void *unused, bool restored)
{
	(void)unused;
#ifdef US_HOST_TEST
	if (restored && !twoblock_host_finish_hook()) return false;
#endif
	if (!restored) { us_poison(&twoblock_lease); return false; }
	return us_mark_drained(&twoblock_lease);   /* never releases the outer lease */
}

static bool twoblock_stopped(void *unused)
{ (void)unused; return us_stopped(&twoblock_lease); }

static bool twoblock_ring(void *context)
{
	struct vr_platform *p = context;
	if (us_control(&twoblock_lease) != US_ACTIVE || shared(p)->state != UR_CLAIMED) return false;
	*(volatile uint32_t *)(p->regs + 0x58) = 1u;   /* the only controller store */
	twoblock_barrier(NULL);
#ifdef US_HOST_TEST
	/* Test-only: a ring that reports failure AFTER the posted store is ambiguous. */
	if (!vr_host_ring_hook()) return false;
#endif
	return true;
}

static uint64_t twoblock_now_us(void *unused)
{
#ifndef US_HOST_TEST
	uint64_t ns; (void)unused;
	absolutetime_to_nanoseconds(mach_absolute_time(), &ns);
	return ns / 1000u;
#else
	(void)unused;
	return vr_host_now_us();
#endif
}

static struct vrq_io twoblock_io(void)
{
	struct vrq_io io = { twoblock_read_reg, twoblock_read_word, twoblock_write_word,
	    twoblock_claim, twoblock_finish, twoblock_stopped, twoblock_ring,
	    twoblock_barrier, twoblock_now_us, &platform };
	return io;
}

/* Clean-baseline evidence. us_open's clean_baseline argument is exactly this result: idle,
 * snapshot, the inherited READ(10) baseline, a second sample of ALL THREE areas, idle again.
 * It is never a constant.
 */
static bool twoblock_baseline_clean(struct vr_platform *p, struct vrq_workspace *snap)
{
	struct vrq_io io = twoblock_io();
	struct vrq_counters c = { 0 };
	uint32_t w, i, j;

	if (!p->regs || !p->table || !p->scratch) return false;
	if (us_stopped(&twoblock_lease) || !vrq_idle(&c, &io)) return false;
	for (i = 0; i < 8; i++) { c.read_word++; if (!io.read_word(io.context, VRQ_TABLE, i*4, &snap->baseline.table[i])) return false; }
	for (i = 0; i < UR_UCD_BYTES/4; i++) { c.read_word++; if (!io.read_word(io.context, VRQ_UCD, i*4, &snap->baseline.ucd[i])) return false; }
	if (!ur_previous_read(&snap->baseline)) return false;
	for (i = 0; i < VRQ_DATA_WORDS; i++) { c.read_word++; if (!io.read_word(io.context, VRQ_DATA, i*4, vrq_saved_word(snap, i))) return false; }
	for (i = 0; i < 8; i++) { c.read_word++; if (!io.read_word(io.context, VRQ_TABLE, i*4, &w) || w != snap->baseline.table[i]) return false; }
	for (j = 0; j < UR_UCD_BYTES/4; j++) { c.read_word++; if (!io.read_word(io.context, VRQ_UCD, j*4, &w) || w != snap->baseline.ucd[j]) return false; }
	for (i = 0; i < VRQ_DATA_WORDS; i++) { c.read_word++; if (!io.read_word(io.context, VRQ_DATA, i*4, &w) || w != *vrq_saved_word(snap, i)) return false; }
	return !us_stopped(&twoblock_lease) && vrq_idle(&c, &io);
}

void hakios_mt6895_ufs_twoblock_init(void);
void hakios_mt6895_ufs_twoblock_quiesce(void);

static void
twoblock_fail(const char *reason)
{
	uint32_t control = us_control(&twoblock_lease);
	if ((control & US_STOP_BIT) && (control & US_RELEASED_BIT) &&
	    !(control & US_POISON_BIT) &&
	    ((control & US_PHASE_MASK) == US_DRAINED || (control & US_PHASE_MASK) == US_IDLE)) {
		printf("mt6895-ufs-twoblock: %s after clean release\n", reason);
		return;
	}
	printf("mt6895-ufs-twoblock: %s; poisoning\n", reason);
	us_poison(&twoblock_lease);
}

static bool
twoblock_run(const struct vr_request *pinned, const struct vr_limits *limits)
{
	struct vrq_io io = twoblock_io();
	const struct vrq_outcome *outcome = &twoblock_outcome;
	uint32_t crc[VR2_BLOCKS];
	uint32_t reference[VR2_BLOCKS];

	if (us_stopped(&twoblock_lease) || !us_admit(&twoblock_lease)) {
		twoblock_fail("admission refused");
		return false;
	}

	vrq_run(&io, pinned, limits, &twoblock_workspace, &twoblock_outcome,
	    &twoblock_counters, false, 0);
	if (outcome->result != VRQ_DELIVERED || !outcome->restored ||
	    !outcome->readback_restored || outcome->data_words_valid != VRQ_DATA_WORDS ||
	    outcome->data_words_attempted != VRQ_DATA_WORDS || twoblock_counters.ring != 1u) {
		twoblock_fail("engine did not deliver both blocks");
		return false;
	}

#ifdef US_HOST_TEST
	twoblock_host_stop_point(1);
#endif
	if (us_stopped(&twoblock_lease)) {
		twoblock_fail("STOP before copy");
		return false;
	}
	for (uint32_t i = 0; i < VR2_TRANSFER_BYTES; i++) {
		twoblock_copy[i] = outcome->data[i];
	}
#ifdef US_HOST_TEST
	twoblock_host_stop_point(2);
#endif
	if (us_stopped(&twoblock_lease)) {
		twoblock_fail("STOP after copy");
		return false;
	}

	for (uint32_t block = 0; block < VR2_BLOCKS; block++) {
		uint32_t lba = pinned->lba + block;
		crc[block] = vrq_crc32(twoblock_copy + block * VR_BLOCK_BYTES, VR_BLOCK_BYTES);
		if (!varread_reference_is_known(lba)) {
			twoblock_fail("block reference missing");
			return false;
		}
		reference[block] = varread_reference_crc(lba);
		if (crc[block] != reference[block]) {
			printf("mt6895-ufs-twoblock: lba=%u checksum mismatch crc=%#x reference_crc=%#x\n",
			       lba, crc[block], reference[block]);
			twoblock_fail("block reference mismatch");
			return false;
		}
	}

	const uint32_t control = us_control(&twoblock_lease);
	const uint32_t state = shared_snapshot(&platform).state;
#ifdef US_HOST_TEST
	twoblock_host_stop_point(3);
#endif
	if (!us_copy_done(&twoblock_lease)) {
		twoblock_fail("copy_done refused");
		return false;
	}

	printf("mt6895-ufs-twoblock: request=1 lba=%u blocks=%u bytes=%u admitted=%u "
	       "readback_ok=%u issued=%u rings=%u response_valid=%#x failures=%#x ocs=%#x "
	       "data_words=%u/%u restored=%u copied=1 control=%#x shared=%u\n",
	       pinned->lba, pinned->blocks, VR2_TRANSFER_BYTES,
	       outcome->claimed ? 1u : 0u, outcome->readback_ok ? 1u : 0u, outcome->issued ? 1u : 0u,
	       twoblock_counters.ring, outcome->response_valid, outcome->response_failures, outcome->ocs,
	       outcome->data_words_valid, outcome->data_words_attempted, outcome->restored ? 1u : 0u,
	       control, state);
	for (uint32_t block = 0; block < VR2_BLOCKS; block++) {
		printf("mt6895-ufs-twoblock: block=%u lba=%u reference_present=1 "
		       "crc=%#x reference_crc=%#x match=1\n",
		       block + 1u, pinned->lba + block, crc[block], reference[block]);
	}
	return true;
}

void
hakios_mt6895_ufs_twoblock_quiesce(void)
{
	us_stop(&twoblock_lease);
}

void
hakios_mt6895_ufs_twoblock_init(void)
{
	uint32_t mode = 0;
	uint32_t trial = 0;
	if (!PE_parse_boot_argn("mtk_ufs_twoblock", &mode, sizeof(mode)) || !mode) return;
	static const char *const competing[] = {
		"mtk_ufs_varread", "mtk_ufs_service", "mtk_ufs_read", "mtk_ufs_probe",
		"mtk_ufs_capacity",
	};
	bool conflict = false;
	for (size_t i = 0; i < sizeof(competing) / sizeof(competing[0]); i++) {
		uint32_t value = 0;
		(void)PE_parse_boot_argn(competing[i], &value, sizeof(value));
		conflict |= value != 0;
	}
	if (mode != 1 || conflict ||
	    !PE_parse_boot_argn("mali_trial", &trial, sizeof(trial)) || trial != UR_TRIAL) {
		printf("mt6895-ufs-twoblock: invalid trial or competing mode; no access\n");
		return;
	}
	if (us_control(&twoblock_lease) != US_NEW) return;

	/* The window start comes from the command line. Absent means 1; an explicit 0 is a value,
	 * not a missing argument, so it is kept and refused later for lacking a reference. */
	uint64_t start_wide = 0;
	uint32_t start;
	if (PE_parse_boot_argn("mtk_ufs_start_lba", &start_wide, sizeof(start_wide))) {
		/* Range is decided before narrowing, so a value above 32 bits cannot wrap into a
		 * valid-looking LBA. */
		if (start_wide > UINT32_MAX) {
			printf("mt6895-ufs-twoblock: start lba out of range; no request\n");
			return;
		}
		start = (uint32_t)start_wide;
	} else {
		start = VR2_LBA_DEFAULT;
	}

	/* One local request built here: encoding, per-block verification and the log all use it. */
	const struct vr_request pinned = {
	    VR_LUN, start, VR2_BLOCKS, VR_BLOCK_BYTES,
	};
	const struct vr_limits limits = vr_runtime_limits();

	/* Refuse before any command: every block must sit inside the geometry, and every block
	 * must have a frozen reference to check against. */
	if (start > limits.lba_max_exclusive ||
	    limits.lba_max_exclusive - start < VR2_BLOCKS) {
		printf("mt6895-ufs-twoblock: start lba %u outside geometry; no request\n", start);
		return;
	}
	for (uint32_t block = 0; block < VR2_BLOCKS; block++) {
		if (!varread_reference_is_known(start + block)) {
			printf("mt6895-ufs-twoblock: lba %u has no frozen reference; no request\n",
			    start + block);
			return;
		}
	}

	struct vr_command encoded;
	if (!vr2_make_read(&pinned, &limits, &encoded)) return;

	printf("mt6895-ufs-twoblock: bounded two-block READ10 lun2 lba%u blocks%u bytes%u "
	       "budget1; guarded handoff required\n",
	       pinned.lba, pinned.blocks, VR2_TRANSFER_BYTES);

#ifdef US_HOST_TEST
	if (twoblock_host_bad_managed()) {
		printf("mt6895-ufs-twoblock: scratch overlaps managed memory; no mapping\n");
		return;
	}
#endif
	if (!ur_outside_vm(UR_LIST_PA, 32, gPhysBase, gPhysSize) ||
	    !ur_outside_vm(UR_LEASE_PA, VR_SCRATCH_BYTES, gPhysBase, gPhysSize) ||
	    pmap_valid_address(UR_LIST_PA) || pmap_valid_address(UR_LIST_PA + 31) ||
	    pmap_valid_address(UR_LEASE_PA) ||
	    pmap_valid_address(UR_DATA_PA + VR2_TRANSFER_BYTES - 1)) {
		printf("mt6895-ufs-twoblock: scratch overlaps managed memory; no mapping\n");
		return;
	}
	platform.scratch = ml_io_map_unmappable(UR_LEASE_PA, VR_SCRATCH_BYTES, VM_WIMG_WCOMB);
	if (!platform.scratch) return;

	const struct ur_lease initial = shared_snapshot(&platform);
	printf("mt6895-ufs-twoblock: guard magic=%#x version=%u trial=%u state=%u\n",
	       initial.magic, initial.version, initial.trial, initial.state);
	if (!ur_lease_valid(&initial) || initial.state != UR_UNUSED ||
	    us_stopped(&twoblock_lease)) return;

	platform.table = ml_io_map_unmappable(UR_LIST_PA & ~(uint64_t)PAGE_MASK, PAGE_SIZE,
	                                      VM_WIMG_WCOMB);
	if (!platform.table) return;
	platform.table += UR_LIST_PA & PAGE_MASK;
	platform.regs = ml_io_map(VR_REG_PA, VR_REG_BYTES);
	if (!platform.regs || us_stopped(&twoblock_lease)) return;

	if (!twoblock_baseline_clean(&platform, &twoblock_baseline)) {
		printf("mt6895-ufs-twoblock: baseline not clean; refusing to publish CLAIMED\n");
		return;
	}
	if (!us_open(&twoblock_lease, true)) {
		printf("mt6895-ufs-twoblock: open refused; no request\n");
		return;
	}
	printf("mt6895-ufs-twoblock: opened control=%#x shared=%u\n",
	       us_control(&twoblock_lease), shared_snapshot(&platform).state);

	bool success = twoblock_run(&pinned, &limits);
	printf("mt6895-ufs-twoblock: final control=%#x shared=%u failed=%u\n",
	       us_control(&twoblock_lease), shared_snapshot(&platform).state, success ? 0u : 1u);
}
