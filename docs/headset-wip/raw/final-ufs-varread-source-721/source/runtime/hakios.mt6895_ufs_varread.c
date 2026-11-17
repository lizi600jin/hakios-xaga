/* Trial721-C: bounded variable-LBA READ(10) probe, KERNEL-runtime candidate.
 * One synchronous init sequence, four fixed LBA reads, no node, no VFS, no block layer,
 * no userland consumer. Not installed in production.
 */
#ifdef US_HOST_TEST
#include "ufs_varread_test_env.h"
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
#include "ufs_variable_read_engine.h"
#include "metadata/ufs_varread_reference.h"

#ifdef US_HOST_TEST
extern void varread_host_stop_point(int which);
extern bool varread_host_bad_managed(void);
extern bool varread_host_claim_hook(void);
extern bool varread_host_finish_hook(void);
extern bool varread_host_restore_read_hook(void);
#endif

#ifdef HAKIOS_UFS
#error The legacy native UFS driver conflicts with the inherited variable-read queue
#endif

#ifndef US_HOST_TEST
extern vm_offset_t ml_io_map(vm_offset_t, vm_size_t);
extern vm_offset_t ml_io_map_unmappable(vm_offset_t, vm_size_t, unsigned int);
extern boolean_t pmap_valid_address(uint64_t);
extern unsigned long gPhysBase, gPhysSize;
#endif

#define VR_REG_PA        0x112b0000u
#define VR_REG_BYTES     0x2300u
#define VR_SCRATCH_BYTES ((vm_offset_t)(UR_DATA_PA + UR_BLOCK_BYTES - UR_LEASE_PA))

/* Predefined sequence and global budget. No retry: a failure stops the run. */
#define VR_BUDGET        4u
static const uint32_t vr_sequence[VR_BUDGET] = { 1u, 2u, 15u, 1u };

struct vr_platform { vm_offset_t regs, table, scratch; };
struct vr_request_record {
    uint32_t request_lba;          /* the LBA this request was built for */
    uint32_t admitted;             /* engine claim succeeded (us_admit IDLE->ACTIVE) */
    uint32_t readback_ok;
    uint32_t issued;
    uint32_t response_valid, response_failures, ocs;
    uint32_t data_words_valid, data_words_attempted;
    uint32_t restored, copied;
    uint32_t reference_present, crc, reference_crc, match;
    uint32_t result;
    uint32_t outer_control, shared_state;
	uint8_t cached[VR_BLOCK_BYTES];     /* this request's own delivered copy */
};

static struct vr_platform platform;
static bool varread_publish(void *, uint32_t);
/* Statically initialised once, never reset: quiesce may already have set STOP. */
struct us_lease vr_lease = { 0, varread_publish, &platform };
static struct ur_workspace vr_workspace;      /* engine scratch, persistent */
static struct ur_workspace vr_base_snapshot;  /* baseline evidence, persistent */
static struct vrq_outcome vr_outcome;
static struct vrq_counters vr_counters;
static struct vr_request_record vr_records[VR_BUDGET];
/* Supplied by the platform build: the reference CRC for an LBA, or 0 if unavailable.
 * This provider never reads the reference payload itself.
 */
extern uint32_t varread_reference_crc(uint32_t lba);
/* Cached delivery buffer: a private copy of each accepted block. */


static void varread_barrier(void *unused)
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
	varread_barrier(NULL);
	return copy;
}

#ifdef US_HOST_TEST
/* Host observation only. In the kernel build this symbol and its update do not exist, so
 * quiesce stays exactly: cached atomic control + aligned shared-DRAM publication + barrier.
 */
volatile uint32_t varread_published_state;
#endif

static bool varread_publish(void *context, uint32_t state)
{
	struct vr_platform *p = context;
	if (!p->scratch) return false;
	shared(p)->state = state;
	varread_barrier(NULL);
#ifdef US_HOST_TEST
	varread_published_state = shared(p)->state;
#endif
	return shared(p)->state == state;
}

static bool varread_read_reg(void *context, uint16_t offset, uint32_t *out)
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

static volatile uint32_t *varread_word(struct vr_platform *p, int region, uint32_t off)
{
	vm_offset_t base; uint32_t size;
	switch (region) {
	case VRQ_TABLE: base = p->table; size = 32u; break;
	case VRQ_UCD:   base = p->scratch ? p->scratch + (UR_UCD_PA - UR_LEASE_PA) : 0;
	                size = UR_UCD_BYTES; break;
	case VRQ_DATA:  base = p->scratch ? p->scratch + (UR_DATA_PA - UR_LEASE_PA) : 0;
	                size = UR_BLOCK_BYTES; break;
	default: return NULL;
	}
	if (!base || (off & 3u) || off > size - 4u) return NULL;
	return (volatile uint32_t *)(base + off);
}

static bool varread_read_word(void *context, int region, uint32_t off, uint32_t *out)
{
	volatile uint32_t *w = varread_word(context, region, off);
	if (!w || !out) return false;          /* region alignment and range check, shared */
#ifdef US_HOST_TEST
	/* Test-only: refuses a chosen sampling read; the load below is the shared statement. */
	if (!vr_host_read_word_hook(region, off)) return false;
#endif
	*out = *w;
	return true;
}

static bool varread_write_word(void *context, int region, uint32_t off, uint32_t value)
{
	volatile uint32_t *w = varread_word(context, region, off);
	if (!w) return false;                              /* region alignment and range, shared */
	if (us_control(&vr_lease) != US_ACTIVE) return false;   /* shared */
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
static bool varread_claim(void *context)
{
	struct ur_lease r = shared_snapshot(context);
#ifdef US_HOST_TEST
	if (!varread_host_claim_hook()) return false;
#endif
	return us_control(&vr_lease) == US_ACTIVE && ur_lease_valid(&r) && r.state == UR_CLAIMED;
}

static bool varread_finish(void *unused, bool restored)
{
	(void)unused;
#ifdef US_HOST_TEST
	if (restored && !varread_host_finish_hook()) return false;
#endif
	if (!restored) { us_poison(&vr_lease); return false; }
	return us_mark_drained(&vr_lease);   /* never releases the outer lease */
}

static bool varread_stopped(void *unused)
{ (void)unused; return us_stopped(&vr_lease); }

static bool varread_ring(void *context)
{
	struct vr_platform *p = context;
	if (us_control(&vr_lease) != US_ACTIVE || shared(p)->state != UR_CLAIMED) return false;
	*(volatile uint32_t *)(p->regs + 0x58) = 1u;   /* the only controller store */
	varread_barrier(NULL);
#ifdef US_HOST_TEST
	/* Test-only: a ring that reports failure AFTER the posted store is ambiguous. */
	if (!vr_host_ring_hook()) return false;
#endif
	return true;
}

static uint64_t varread_now_us(void *unused)
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

static struct vrq_io varread_io(void)
{
	struct vrq_io io = { varread_read_reg, varread_read_word, varread_write_word,
	    varread_claim, varread_finish, varread_stopped, varread_ring,
	    varread_barrier, varread_now_us, &platform };
	return io;
}

/* Clean-baseline evidence. us_open's clean_baseline argument is exactly this result: idle,
 * snapshot, the inherited READ(10) baseline, a second sample of ALL THREE areas, idle again.
 * It is never a constant.
 */
static bool varread_baseline_clean(struct vr_platform *p, struct ur_workspace *snap)
{
	struct vrq_io io = varread_io();
	struct vrq_counters c = { 0 };
	uint32_t w, i, j;

	if (!p->regs || !p->table || !p->scratch) return false;
	if (us_stopped(&vr_lease) || !vrq_idle(&c, &io)) return false;
	for (i = 0; i < 8; i++) { c.read_word++; if (!io.read_word(io.context, VRQ_TABLE, i*4, &snap->table[i])) return false; }
	for (i = 0; i < UR_UCD_BYTES/4; i++) { c.read_word++; if (!io.read_word(io.context, VRQ_UCD, i*4, &snap->ucd[i])) return false; }
	if (!ur_previous_read(snap)) return false;
	for (i = 0; i < VRQ_DATA_WORDS; i++) { c.read_word++; if (!io.read_word(io.context, VRQ_DATA, i*4, &snap->data[i])) return false; }
	for (i = 0; i < 8; i++) { c.read_word++; if (!io.read_word(io.context, VRQ_TABLE, i*4, &w) || w != snap->table[i]) return false; }
	for (j = 0; j < UR_UCD_BYTES/4; j++) { c.read_word++; if (!io.read_word(io.context, VRQ_UCD, j*4, &w) || w != snap->ucd[j]) return false; }
	for (i = 0; i < VRQ_DATA_WORDS; i++) { c.read_word++; if (!io.read_word(io.context, VRQ_DATA, i*4, &w) || w != snap->data[i]) return false; }
	return !us_stopped(&vr_lease) && vrq_idle(&c, &io);
}

/* Reference CRCs are pure metadata constants. The reference PAYLOAD is never read, stored or
 * printed. varread_reference_is_known() is the presence signal; a checksum value is never
 * overloaded to mean "absent".
 */

void hakios_mt6895_ufs_varread_init(void);
void hakios_mt6895_ufs_varread_quiesce(void);

/* One failure handler. A cleanly released lease (cached STOP+RELEASED, no POISON, phase
 * DRAINED or IDLE) is already a proven clean return: stop, never re-poison it. Anything else
 * — ACTIVE, un-restored, unknown baseline — stays fail-closed and is poisoned.
 */
static bool varread_fail(const char *why, uint32_t lba)
{
	uint32_t c = us_control(&vr_lease);
	if ((c & US_STOP_BIT) && (c & US_RELEASED_BIT) && !(c & US_POISON_BIT) &&
	    ((c & US_PHASE_MASK) == US_DRAINED || (c & US_PHASE_MASK) == US_IDLE)) {
		printf("mt6895-ufs-varread: lba=%u %s after a clean release; not re-poisoning\n",
		       lba, why);
		return true;
	}
	printf("mt6895-ufs-varread: lba=%u %s; poisoning\n", lba, why);
	us_poison(&vr_lease);
	return false;
}

/* Cached software STOP plus aligned shared-DRAM publication only. No mutex, wait, logger,
 * thread or MMIO; other CPUs may not have stopped.
 */
void hakios_mt6895_ufs_varread_quiesce(void)
{
	us_stop(&vr_lease);
}

void hakios_mt6895_ufs_varread_init(void)
{
	uint32_t mode = 0, old_service = 0, old_read = 0, old_probe = 0, old_capacity = 0;
	struct ur_lease initial;
	struct vrq_io io;
	struct vr_limits lim;
	bool failed = false;
	uint32_t i;

	if (!PE_parse_boot_argn("mtk_ufs_varread", &mode, sizeof(mode)) || !mode) return;
	(void)PE_parse_boot_argn("mtk_ufs_service", &old_service, sizeof(old_service));
	(void)PE_parse_boot_argn("mtk_ufs_read", &old_read, sizeof(old_read));
	(void)PE_parse_boot_argn("mtk_ufs_probe", &old_probe, sizeof(old_probe));
	(void)PE_parse_boot_argn("mtk_ufs_capacity", &old_capacity, sizeof(old_capacity));
	if (mode != 1 || old_service || old_read || old_probe || old_capacity) {
		printf("mt6895-ufs-varread: invalid or competing mode; no access\n");
		return;
	}
	if (us_control(&vr_lease) != US_NEW) return;   /* repeated init: no IO at all */

	printf("mt6895-ufs-varread: bounded variable READ10 lun2 lba0..15 bytes4096 budget4; "
	       "guarded handoff required\n");

	/* Fail closed before any mapping if the scratch areas are not outside managed memory. */
#ifdef US_HOST_TEST
	if (varread_host_bad_managed()) {
		printf("mt6895-ufs-varread: scratch overlaps managed memory; no mapping\n");
		return;
	}
#endif
	if (!ur_outside_vm(UR_LIST_PA, 32, gPhysBase, gPhysSize) ||
	    !ur_outside_vm(UR_LEASE_PA, VR_SCRATCH_BYTES, gPhysBase, gPhysSize) ||
	    pmap_valid_address(UR_LIST_PA) || pmap_valid_address(UR_LIST_PA + 31) ||
	    pmap_valid_address(UR_LEASE_PA) ||
	    pmap_valid_address(UR_DATA_PA + UR_BLOCK_BYTES - 1)) {
		printf("mt6895-ufs-varread: scratch overlaps managed memory; no mapping\n");
		return;
	}
	platform.scratch = ml_io_map_unmappable(UR_LEASE_PA, VR_SCRATCH_BYTES, VM_WIMG_WCOMB);
	if (!platform.scratch) { printf("mt6895-ufs-varread: scratch mapping failed\n"); return; }
	initial = shared_snapshot(&platform);
	printf("mt6895-ufs-varread: guard magic=%#x version=%u trial=%u state=%u\n",
	       initial.magic, initial.version, initial.trial, initial.state);
	if (!ur_lease_valid(&initial) || initial.state != UR_UNUSED || us_stopped(&vr_lease)) return;

	platform.table = ml_io_map_unmappable(UR_LIST_PA & ~(uint64_t)PAGE_MASK, PAGE_SIZE,
	                                     VM_WIMG_WCOMB);
	if (!platform.table) return;
	platform.table += UR_LIST_PA & PAGE_MASK;
	platform.regs = ml_io_map(VR_REG_PA, VR_REG_BYTES);
	if (!platform.regs || us_stopped(&vr_lease)) return;

	/* The codec must refuse before any IO, and clean_baseline must be EVIDENCE, not a constant. */
	/* Codec self-check BEFORE any IO: the runtime window must be a strict subset of the
	 * capacity window, and a legal request must be accepted while an illegal one is not.
	 */
	lim = vr_runtime_limits();
	{
		struct vr_limits cap = vr_capacity_limits();
		struct vr_request probe = { VR_LUN, 0u, VR_BLOCKS, VR_BLOCK_BYTES };
		struct vr_request bad   = { 3u, 0u, VR_BLOCKS, VR_BLOCK_BYTES };
		if (lim.lba_max_exclusive == 0u ||
		    lim.lba_max_exclusive > cap.lba_max_exclusive ||
		    VR_BLOCKS != 1u || VR_BLOCK_BYTES != 4096u || VR_LUN != 2u ||
		    !vr_request_valid(&probe, &lim) || vr_request_valid(&bad, &lim)) {
			printf("mt6895-ufs-varread: codec self-check refused; no access\n");
			return;
		}
	}
	if (!varread_baseline_clean(&platform, &vr_base_snapshot)) {
		printf("mt6895-ufs-varread: baseline not clean; refusing to publish CLAIMED\n");
		return;
	}
	if (!us_open(&vr_lease, true)) {
		printf("mt6895-ufs-varread: open refused; no request\n");
		return;
	}
	printf("mt6895-ufs-varread: opened control=%#x shared=%u\n",
	       us_control(&vr_lease), shared_snapshot(&platform).state);

	io = varread_io();
	for (i = 0; i < VR_BUDGET; i++) {
		struct vr_request req;
		struct vr_request_record *rec = &vr_records[i];
		uint32_t outer, sh, crc;

		rec->request_lba = vr_sequence[i];
		rec->result = (uint32_t)VRQ_REFUSED;

		if (us_stopped(&vr_lease)) { failed = !varread_fail("STOP before admit", rec->request_lba); break; }

		/* The wrapper performs the admission; the engine only verifies it. */
		if (!us_admit(&vr_lease)) {
			failed = !varread_fail("admit refused", rec->request_lba);
			break;
		}

		req.lun = VR_LUN;
		req.lba = vr_sequence[i];
		req.blocks = VR_BLOCKS;
		req.logical_block_bytes = VR_BLOCK_BYTES;
		vrq_run(&io, &req, &lim, &vr_workspace, &vr_outcome, &vr_counters, false, 0u);

		rec->result = (uint32_t)vr_outcome.result;
		rec->admitted = vr_outcome.claimed ? 1u : 0u;
		rec->readback_ok = vr_outcome.readback_ok ? 1u : 0u;
		rec->issued = vr_outcome.issued ? 1u : 0u;
		rec->ocs = vr_outcome.ocs;
		rec->response_valid = vr_outcome.response_valid;
		rec->response_failures = vr_outcome.response_failures;
		rec->data_words_valid = vr_outcome.data_words_valid;
		rec->data_words_attempted = vr_outcome.data_words_attempted;
		rec->restored = vr_outcome.restored ? 1u : 0u;

		/* Any failure that is not a proven clean drain must fail closed: the lease is still
		 * ACTIVE, so quiesce must not be able to release it as a clean baseline.
		 */
		if (vr_outcome.result != VRQ_DELIVERED) {
			failed = !varread_fail("engine did not deliver", rec->request_lba);
			break;
		}

		/* STOP is checked on both sides of the copy. */
#ifdef US_HOST_TEST
		varread_host_stop_point(1);
#endif
		if (us_stopped(&vr_lease)) { failed = !varread_fail("STOP before copy", rec->request_lba); break; }
		for (uint32_t k = 0; k < VR_BLOCK_BYTES; k++) rec->cached[k] = vr_outcome.data[k];
		rec->copied = 1u;
#ifdef US_HOST_TEST
		varread_host_stop_point(2);
#endif
		if (us_stopped(&vr_lease)) {
			rec->copied = 0u;
			failed = !varread_fail("STOP after copy", rec->request_lba);
			break;
		}

		/* Reference label: a checksum comparison only, valid for a full accepted block. */
		crc = vrq_crc32(rec->cached, VR_BLOCK_BYTES);
		rec->crc = crc;
		rec->reference_crc = varread_reference_crc(req.lba);
		rec->reference_present = varread_reference_is_known(req.lba) ? 1u : 0u;
		rec->match = (rec->reference_present && crc == rec->reference_crc) ? 1u : 0u;

		outer = us_control(&vr_lease);
		sh = shared_snapshot(&platform).state;
		rec->outer_control = outer;
		rec->shared_state = sh;

		/* A reference mismatch stops the run: it is never silently skipped. */
		if (!rec->match) {
			printf("mt6895-ufs-varread: request=%u lba=%u reference mismatch crc=%#x ref=%#x\n",
			       i + 1u, rec->request_lba, rec->crc, rec->reference_crc);
			failed = !varread_fail("reference mismatch", rec->request_lba);
			break;
		}

		/* No host-only bypass here: the fault is injected with a real cached STOP, so the
		 * FAILURE comes from the REAL us_copy_done CAS, exactly as in production.
		 */
#ifdef US_HOST_TEST
		varread_host_stop_point(3);       /* inside the copy_done window */
#endif
		if (!us_copy_done(&vr_lease)) {
			failed = !varread_fail("copy_done refused", rec->request_lba);
			break;
		}

		printf("mt6895-ufs-varread: request=%u lba=%u admitted=%u readback_ok=%u issued=%u "
		       "response_valid=%#x failures=%#x ocs=%#x data_words=%u/%u restored=%u "
		       "copied=%u reference_present=%u crc=%#x reference_crc=%#x match=%u "
		       "control=%#x shared=%u\n",
		       i + 1u, rec->request_lba, rec->admitted, rec->readback_ok, rec->issued,
		       rec->response_valid, rec->response_failures, rec->ocs,
		       rec->data_words_valid, rec->data_words_attempted, rec->restored,
		       rec->copied, rec->reference_present, rec->crc, rec->reference_crc,
		       rec->match, rec->outer_control, rec->shared_state);
	}

	printf("mt6895-ufs-varread: final control=%#x shared=%u failed=%u\n",
	       us_control(&vr_lease), shared_snapshot(&platform).state, failed ? 1u : 0u);
}
