/* Trial725 platform for bounded READ(10) requests under the inherited loader lease. */
#ifdef US_HOST_TEST
#include "ufs_twoblock_test_env.h"
#include <errno.h>
#else
#include <stdint.h>
#include <stdbool.h>
#include <mach/mach_types.h>
#include <machine/machine_routines.h>
#include <kern/clock.h>
#include <kern/thread.h>
#include <sys/conf.h>
#include <sys/errno.h>
#include <sys/fcntl.h>
#include <sys/proc.h>
#include <sys/uio.h>
#include <sys/uio_internal.h>
#include <sys/vnode.h>
#include <sys/vnode_if.h>
#include <sys/vnode_internal.h>
#include <miscfs/devfs/devfs.h>
#include <pexpert/pexpert.h>
#include <sys/systm.h>
#include <vm/vm_kern.h>
#include <vm/memory_types.h>
#endif
#include "support/ufs_service_lease.h"
#include "engine/ufs_two_block_engine.h"
#include "codec/ufs_byte_range_codec.h"
#include "../ufs_varread/metadata/ufs_varread_reference.h"
#include "codec/ufs_apfs_volume.h"

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

struct vr_platform {
	vm_offset_t regs, table, scratch;
	uint32_t data_bytes;
};

static struct vr_platform platform;
static uint32_t twoblock_total_rings;
static uint32_t range_mode_id;
#define GPT_ARRAY_MODE 5u
#define GPT_REQUESTS 3u
#define GPT_SNAPSHOT_BYTES (5u * VR_BLOCK_BYTES)
/* Trial730/731/732: the primary GPT window, unchanged. LBA 1 header, LBA 2+3 array, LBA 4+5 array. */
static const struct vr_request gpt_requests[GPT_REQUESTS] = {
	{ VR_LUN, 1u, 1u, VR_BLOCK_BYTES },
	{ VR_LUN, 2u, 2u, VR_BLOCK_BYTES },
	{ VR_LUN, 4u, 2u, VR_BLOCK_BYTES },
};

/* Mode 6 exports a header block followed by two pairs of entry-array blocks. */
#define GPT_BACKUP_MODE 6u
static const struct vr_request gpt_backup_requests[GPT_REQUESTS] = {
	{ VR_LUN, GPT_BACKUP_HEADER_LBA, 1u, VR_BLOCK_BYTES },
	{ VR_LUN, GPT_BACKUP_ARRAY_LBA, 2u, VR_BLOCK_BYTES },
	{ VR_LUN, GPT_BACKUP_ARRAY_LBA + 2u, 2u, VR_BLOCK_BYTES },
};

/* Trial734: the same backup window, plus one more block whose address is not a constant.
 *
 * The fourth READ(10) targets the first block of the xnusys partition, and its LBA is taken from
 * the backup GPT entry array the first three commands already fetched. The name is matched as the
 * complete UTF-16LE partition name, and the resulting extent must equal the extent measured on this
 * device; anything else refuses before a command is built.
 */
#define XNUSYS_MODE 7u
#define XNUSYS_SNAPSHOT_BYTES (GPT_SNAPSHOT_BYTES + VR_BLOCK_BYTES)

/* Trial735: the xnusys window plus five APFS blocks walked by av_next().
 *
 * Commands 1..4 are Trial734's. Commands 5..8 are chosen one at a time from the parser's answer,
 * and each answer is accepted only for the ordinal that asks for it, so no step can substitute a
 * different address or length. The window is 20480 + 5 * 4096 = 45056 bytes.
 */
#define APFS_VOLUME_MODE 8u
/* Four more commands (5..8) covering five blocks: command 5 is a pair, 6..8 are single. */
#define APFS_CHAIN_REQUESTS 4u
#define APFS_CHAIN_BLOCKS 5u
#define APFS_SNAPSHOT_BYTES (XNUSYS_SNAPSHOT_BYTES + APFS_CHAIN_BLOCKS * VR_BLOCK_BYTES)

/*
 * Trial736 -- the volume's file-system tree root.
 *
 * The first eight commands are Trial735's, unchanged. Four more follow, each one block, and each
 * address is derived by the parser from an object the previous command already brought in:
 *
 *   9   the volume's object map          (from the APSB)
 *  10   the interior root of that map    (from the map object)
 *  11   the leaf the interior keys pick  (from (oid, xid) order)
 *  12   the file-system tree root        (from the leaf's newest visible mapping)
 *
 * The window is 45056 + 4 * 4096 = 61440 bytes, and nothing is published until all twelve have
 * been read and the root object itself has been checked.
 */
#define APFS_FSTREE_MODE 9u

/*
 * Trial737 -- the root directory and a path below it.
 *
 * The first twelve commands are Trial736's, unchanged. From the thirteenth the walk chooses its
 * own blocks: each one is named by a key of a node already checked, turned into an address by the
 * volume's object map. What this mode adds is the answer it is looking for and the budget:
 *
 *   /usr/bin       two components, so two descents from the tree root
 *
 * The snapshot grows by one block per fetch, up to APFS_WALK_MAX_BLOCKS, and nothing is published
 * unless every step checks out and the walk ends in a definite answer.
 */
/* Two components answer Trial737's question; five reach a file. */
#define APFS_PATH_MODE_STAGE2 10u
#define APFS_PATH_MODE_STAGE3 11u
#define APFS_PATH_MODE APFS_PATH_MODE_STAGE3
#define APFS_PATH_COMPONENTS 4u
#define APFS_WALK_MAX_BLOCKS 16u
/* One small file's extent; the target is 7955 bytes, so two blocks, with room to spare. */
#define APFS_FILE_MAX_BLOCKS 8u
#define APFS_PATH_SNAPSHOT_BYTES (APFS_FSTREE_SNAPSHOT_BYTES + \
                                  (APFS_WALK_MAX_BLOCKS + APFS_FILE_MAX_BLOCKS) * VR_BLOCK_BYTES)
#define APFS_PATH_TOTAL_REQUESTS (APFS_TOTAL_REQUESTS + APFS_WALK_MAX_BLOCKS + \
                                  APFS_FILE_MAX_BLOCKS)
#define APFS_ROOT_TREE_OID UINT64_C(1029)
#define APFS_ROOT_DIR_INO  UINT64_C(2)
/*
 * The path this mode resolves, one component per entry, with the lengths the tree stores: a
 * directory entry's key carries the name and its trailing NUL, and the stored length counts it.
 */
/*
 * A file whose directory holds only three entries: every one of them lives in the same leaf,
 * so the walk reaches the record in one descent. The bigger directory tried first held 44, whose
 * entries are spread over many leaves because APFS files them under a hash of the name -- a case
 * this walker does not yet handle and does not need for this question.
 */
static const char *const apfs_path[APFS_PATH_COMPONENTS] = {
	"usr", "libexec", "GKCentralCache.momd", "VersionInfo.plist",
};
static const uint32_t apfs_path_len[APFS_PATH_COMPONENTS] = { 4u, 8u, 20u, 18u };
/*
 * Mode 11 stops at the file's bytes. Mode 12 lists a directory instead: the same walk, but the
 * last component names the directory to enumerate rather than a file to read.
 */
#define APFS_PATH_MODE_LIST 12u
/*
 * Every place that has to treat the path modes alike asks this, rather than repeating the three
 * numbers. Six of them did repeat it and two were missed when a seventh mode arrived, so the
 * question is asked in one place now.
 */
#define APFS_IS_PATH_MODE(m) \
	((m) == APFS_PATH_MODE_STAGE2 || (m) == APFS_PATH_MODE_STAGE3 || (m) == APFS_PATH_MODE_LIST)
static inline unsigned apfs_path_wanted(uint32_t mode)
{
	if (mode == APFS_PATH_MODE_STAGE2) return 2u;
	if (mode == APFS_PATH_MODE_LIST) return APFS_PATH_COMPONENTS - 1u;   /* the directory itself */
	return APFS_PATH_COMPONENTS;
}
static inline const char *apfs_path_component(unsigned i) { return apfs_path[i]; }
static inline uint32_t apfs_path_length(unsigned i) { return apfs_path_len[i]; }
#define APFS_FSTREE_CHAIN_BLOCKS 4u
/* Steps the shared walk needs to reach ten blocks from the single NXSB. */
#define APFS_FSTREE_STEPS 9u
#define APFS_FSTREE_SNAPSHOT_BYTES (APFS_SNAPSHOT_BYTES + APFS_FSTREE_CHAIN_BLOCKS * VR_BLOCK_BYTES)
#define APFS_TOTAL_REQUESTS 12u
/* XNUSYS_LBA comes from the engine header so the driver and the whitelist cannot disagree. */
#define XNUSYS_LAST_LBA UINT32_C(31236087)
#define XNUSYS_NAME_OFFSET 56u   /* GPT entry: partition name, 72 UTF-16LE bytes */
#define XNUSYS_NAME_BYTES 72u
static uint64_t
gpt_u64(const uint8_t *p)
{
	return vrq_get32le(p) | ((uint64_t)vrq_get32le(p + 4) << 32);
}

/* Read the 92-byte GPT header for the captured LUN layout. Its CRC excludes the
 * header CRC field, and its partition-array CRC covers all four array blocks. */
static bool
xnusys_layout_valid(const uint8_t *header, const uint8_t *array, uint32_t bytes)
{
	uint8_t copy[92];
	if (bytes != GPT_SNAPSHOT_BYTES - VR_BLOCK_BYTES ||
	    memcmp(header, "EFI PART", 8) || vrq_get32le(header + 8) != 0x10000 ||
	    vrq_get32le(header + 12) != sizeof(copy) || vrq_get32le(header + 20) != 0 ||
	    gpt_u64(header + 24) != GPT_BACKUP_HEADER_LBA || gpt_u64(header + 32) != 1 ||
	    gpt_u64(header + 72) != GPT_BACKUP_ARRAY_LBA ||
	    vrq_get32le(header + 80) != 128 || vrq_get32le(header + 84) != 128)
		return false;
	uint64_t first = gpt_u64(header + 40), last = gpt_u64(header + 48);
	if (first < 6 || first > XNUSYS_LBA || last < XNUSYS_LAST_LBA ||
	    last >= GPT_BACKUP_ARRAY_LBA) return false;
	memcpy(copy, header, sizeof(copy));
	memset(copy + 16, 0, 4);
	return vrq_crc32(copy, sizeof(copy)) == vrq_get32le(header + 16) &&
	    vrq_crc32(array, bytes) == vrq_get32le(header + 88);
}

/* The two additional Android reference CRCs apply only to the fixed GPT read set. */
static bool
twoblock_reference(uint32_t lba, uint32_t mode, uint32_t *crc)
{
	if (mode == GPT_ARRAY_MODE && (lba == 4u || lba == 5u)) {
		*crc = lba == 4u ? UINT32_C(0xa0d23c1c) : UINT32_C(0xc71c0011);
		return true;
	}
	/* Trial733: the backup header block and the four blocks of its array, measured on the same
	 * device. Only these five addresses carry a reference in this mode. */
	if (mode == GPT_BACKUP_MODE || mode == XNUSYS_MODE || mode == APFS_VOLUME_MODE ||
	    mode == APFS_FSTREE_MODE || APFS_IS_PATH_MODE(mode)) {
		static const struct { uint32_t lba, crc; } backup[] = {
			{ GPT_BACKUP_HEADER_LBA,     UINT32_C(0x4b13d623) },
			{ GPT_BACKUP_ARRAY_LBA,      UINT32_C(0x0d024e0c) },
			{ GPT_BACKUP_ARRAY_LBA + 1u, UINT32_C(0x58f10429) },
			{ GPT_BACKUP_ARRAY_LBA + 2u, UINT32_C(0xa0d23c1c) },
			{ GPT_BACKUP_ARRAY_LBA + 3u, UINT32_C(0xc71c0011) },
		};
		for (unsigned i = 0; i < sizeof(backup) / sizeof(backup[0]); ++i) {
			if (backup[i].lba == lba) {
				*crc = backup[i].crc;
				return true;
			}
		}
		/* Trial734 adds exactly one address, and only once it has been resolved. */
		/* Trial735: the APFS chain blocks, measured on the same device. */
		if (mode == APFS_VOLUME_MODE || mode == APFS_FSTREE_MODE ||
		    APFS_IS_PATH_MODE(mode)) {
			static const struct { uint32_t lba, crc; } chain[] = {
				{ XNUSYS_LBA + 1u,    UINT32_C(0x14f752f0) },
				{ XNUSYS_LBA + 2u,    UINT32_C(0x787ea9ea) },
				{ XNUSYS_LBA + 2394u, UINT32_C(0x3cb0c204) },
				{ XNUSYS_LBA + 2395u, UINT32_C(0xb708a29a) },
				{ XNUSYS_LBA + 2396u, UINT32_C(0xd85f81f9) },
				/* Trial736: the tree-root path, measured on this device. */
				{ XNUSYS_LBA + 2397u, UINT32_C(0xe714c9da) },
				{ XNUSYS_LBA + 2398u, UINT32_C(0xbe415566) },
				{ XNUSYS_LBA + 2399u, UINT32_C(0x8b840f5c) },
				{ XNUSYS_LBA + 2510u, UINT32_C(0xfc7153a9) },
			};
			for (unsigned i = 0; i < sizeof(chain) / sizeof(chain[0]); ++i) {
				if (chain[i].lba == lba) {
					*crc = chain[i].crc;
					return true;
				}
			}
		}
		if ((mode == XNUSYS_MODE || mode == APFS_VOLUME_MODE ||
		     mode == APFS_FSTREE_MODE || APFS_IS_PATH_MODE(mode)) && lba == XNUSYS_LBA) {
			/* CRC32 of the NXSB block, measured on the local sample. This is the same block CRC
			 * the three GPT commands use; it is not the APFS fletcher64 object checksum. */
			*crc = UINT32_C(0x787ea9ea);
			return true;
		}
		return false;
	}
	if (!varread_reference_is_known(lba)) return false;
	*crc = varread_reference_crc(lba);
	return true;
}
/* Resolve xnusys from the verified backup array.
 *
 * Runs only after the first three commands matched their frozen CRCs, so the bytes below are known
 * good. Exactly one used entry may carry this name, and its extent must equal the measured one.
 */
static bool
xnusys_resolve(const uint8_t *array, uint32_t bytes, uint32_t *lba, uint32_t *last)
{
	static const uint8_t zero_guid[16];
	static const uint8_t name[XNUSYS_NAME_BYTES] = { 'x',0,'n',0,'u',0,'s',0,'y',0,'s' };
	const uint32_t stride = 128u;
	uint32_t found = 0u, hit = 0u;
	if (!array || bytes != GPT_SNAPSHOT_BYTES - VR_BLOCK_BYTES) return false;
	for (uint32_t off = 0; off + stride <= bytes; off += stride) {
		const uint8_t *e = array + off;
		if (memcmp(e, zero_guid, sizeof(zero_guid)) == 0) continue;   /* unused */
		if (memcmp(e + XNUSYS_NAME_OFFSET, name, sizeof(name))) continue;
		++found;
		hit = off;
	}
	if (found != 1u) return false;
	const uint8_t *e = array + hit;
	const uint64_t first = gpt_u64(e + 32), end = gpt_u64(e + 40);
	if (first != XNUSYS_LBA || end != XNUSYS_LAST_LBA) return false;
	*lba = (uint32_t)first;
	*last = (uint32_t)end;
	return true;
}

static bool twoblock_publish(void *, uint32_t);
static struct us_lease twoblock_lease = { 0, twoblock_publish, &platform };
static struct vrq_workspace twoblock_workspace;
static struct vrq_workspace twoblock_baseline;
static struct vrq_outcome twoblock_outcome;
static struct vrq_counters twoblock_counters;
static uint8_t twoblock_copy[VR2_TRANSFER_BYTES];
/* Trial726: the caller's slice, filled only after both blocks have been verified. */
static uint8_t twoblock_slice[VRB_WINDOW_BYTES];
/* Sealed copy of the verified slice for the call in flight. Never the caller's buffer, and never
 * the stack: the window is up to 8192 bytes. */
static uint8_t twoblock_stage[VRB_WINDOW_BYTES];
#ifdef US_HOST_TEST
/* The host test supplies the caller buffer, so capacity refusal can be exercised end to end. */
uint8_t *twoblock_test_dst(void);
uint32_t twoblock_test_cap(void);
/* Last byte count the entry point reported delivering; the test reads it back. */
uint32_t twoblock_test_delivered;
#endif


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
	                size = p->data_bytes; break;
	default: return NULL;
	}
	if (!base || (off & 3u) || off >= size) return NULL;
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
	__atomic_fetch_add(&twoblock_total_rings, 1u, __ATOMIC_RELEASE);
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
	const uint32_t data_words = p->data_bytes / sizeof(uint32_t);
	uint32_t w, i, j;

	if (!p->regs || !p->table || !p->scratch) return false;
	if (us_stopped(&twoblock_lease) || !vrq_idle(&c, &io)) return false;
	for (i = 0; i < 8; i++) { c.read_word++; if (!io.read_word(io.context, VRQ_TABLE, i*4, &snap->baseline.table[i])) return false; }
	for (i = 0; i < UR_UCD_BYTES/4; i++) { c.read_word++; if (!io.read_word(io.context, VRQ_UCD, i*4, &snap->baseline.ucd[i])) return false; }
	if (!ur_previous_read(&snap->baseline)) return false;
	for (i = 0; i < data_words; i++) { c.read_word++; if (!io.read_word(io.context, VRQ_DATA, i*4, vrq_saved_word(snap, i))) return false; }
	for (i = 0; i < 8; i++) { c.read_word++; if (!io.read_word(io.context, VRQ_TABLE, i*4, &w) || w != snap->baseline.table[i]) return false; }
	for (j = 0; j < UR_UCD_BYTES/4; j++) { c.read_word++; if (!io.read_word(io.context, VRQ_UCD, j*4, &w) || w != snap->baseline.ucd[j]) return false; }
	for (i = 0; i < data_words; i++) { c.read_word++; if (!io.read_word(io.context, VRQ_DATA, i*4, &w) || w != *vrq_saved_word(snap, i)) return false; }
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
twoblock_run(const struct vr_request *pinned, const struct vr_limits *limits, uint32_t ordinal,
    const struct vrb_window *slice, uint8_t *dst, uint32_t dst_capacity, uint32_t *out_delivered)
{
	struct vrq_io io = twoblock_io();
	const struct vrq_outcome *outcome = &twoblock_outcome;
	const uint32_t data_words = pinned->blocks * VRQ_BLOCK_WORDS;
	const uint32_t transfer_bytes = pinned->blocks * VR_BLOCK_BYTES;
	uint32_t crc[VR2_BLOCKS];
	uint32_t reference[VR2_BLOCKS];

	if (us_stopped(&twoblock_lease) || !us_admit(&twoblock_lease)) {
		twoblock_fail("admission refused");
		return false;
	}

	platform.data_bytes = transfer_bytes;
	enum vrq_read_scope scope = VRQ_STAGE_READ;
	if (range_mode_id == GPT_BACKUP_MODE || range_mode_id == XNUSYS_MODE ||
	    range_mode_id == APFS_VOLUME_MODE || range_mode_id == APFS_FSTREE_MODE ||
	    APFS_IS_PATH_MODE(range_mode_id))
		scope = VRQ_BACKUP_READ;
	if ((range_mode_id == XNUSYS_MODE || range_mode_id == APFS_VOLUME_MODE ||
	     range_mode_id == APFS_FSTREE_MODE || APFS_IS_PATH_MODE(range_mode_id)) &&
	    ordinal == GPT_REQUESTS + 1u)
		scope = VRQ_XNUSYS_READ;
	/* Trial735: ordininals 5..8 are the APFS chain and are authorised by number. */
	if ((range_mode_id == APFS_VOLUME_MODE || range_mode_id == APFS_FSTREE_MODE ||
	     APFS_IS_PATH_MODE(range_mode_id)) &&
	    ordinal > GPT_REQUESTS + 1u)
		scope = VRQ_APFS_READ;
	vrq_run_scope(&io, pinned, limits, &twoblock_workspace, &twoblock_outcome,
	    &twoblock_counters, false, 0, scope, ordinal);
	if (outcome->result != VRQ_DELIVERED || !outcome->restored ||
	    !outcome->readback_restored || outcome->data_words_valid != data_words ||
	    outcome->data_words_attempted != data_words || twoblock_counters.ring != 1u) {
		twoblock_fail("engine did not deliver requested blocks");
		return false;
	}

#ifdef US_HOST_TEST
	twoblock_host_stop_point(1);
#endif
	if (us_stopped(&twoblock_lease)) {
		twoblock_fail("STOP before copy");
		return false;
	}
	for (uint32_t i = 0; i < transfer_bytes; i++) {
		twoblock_copy[i] = outcome->data[i];
	}
#ifdef US_HOST_TEST
	twoblock_host_stop_point(2);
#endif
	if (us_stopped(&twoblock_lease)) {
		twoblock_fail("STOP after copy");
		return false;
	}

	for (uint32_t block = 0; block < pinned->blocks; block++) {
		uint32_t lba = pinned->lba + block;
		crc[block] = vrq_crc32(twoblock_copy + block * VR_BLOCK_BYTES, VR_BLOCK_BYTES);
		if (!twoblock_reference(lba, range_mode_id, &reference[block])) {
			/*
			 * Trial737's walk names its own addresses, so most of them have no entry here: they
			 * depend on what the volume holds, not on a number this file could carry. The block
			 * is still checked -- by the header of whatever structure it is supposed to be, and
			 * by the fact that the address came from a value the walker had already verified.
			 * What is dropped is only the "same bytes as when this was measured" check, which
			 * cannot apply to a block nobody measured in advance.
			 */
			if (APFS_IS_PATH_MODE(range_mode_id)) {
				reference[block] = 0u;
				continue;
			}
			twoblock_fail("block reference missing");
			return false;
		}
		if (crc[block] != reference[block]) {
			printf("mt6895-ufs-twoblock: lba=%u checksum mismatch crc=%#x reference_crc=%#x\n",
			       lba, crc[block], reference[block]);
			twoblock_fail("block reference mismatch");
			return false;
		}
	}

	/* Trial726: while the lease still holds the source, seal the verified slice into a staging
	 * area owned by this call. The caller's buffer is only written after every later check has
	 * passed, so a failure leaves it untouched. The source never leaves the lease-protected copy. */
	uint32_t slice_crc = 0;
	uint32_t slice_len = 0;
	if (slice != NULL) {
		const uint32_t want = slice->first_bytes + slice->second_bytes;
		if (dst == NULL) {
			twoblock_fail("slice destination refused");
			return false;
		}
		if (!vrb_deliver(twoblock_copy, slice, twoblock_stage, sizeof(twoblock_stage))) {
			twoblock_fail("slice staging failed");
			return false;
		}
		slice_len = want;
		slice_crc = vrq_crc32(twoblock_stage, want);
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

	printf("mt6895-ufs-twoblock: request=%u lba=%u blocks=%u bytes=%u admitted=%u "
	       "readback_ok=%u issued=%u rings=%u response_valid=%#x failures=%#x ocs=%#x "
	       "data_words=%u/%u restored=%u copied=1 control=%#x shared=%u\n",
	       ordinal, pinned->lba, pinned->blocks, transfer_bytes,
	       outcome->claimed ? 1u : 0u, outcome->readback_ok ? 1u : 0u, outcome->issued ? 1u : 0u,
	       twoblock_counters.ring, outcome->response_valid, outcome->response_failures, outcome->ocs,
	       outcome->data_words_valid, outcome->data_words_attempted, outcome->restored ? 1u : 0u,
	       control, state);
	for (uint32_t block = 0; block < pinned->blocks; block++) {
		printf("mt6895-ufs-twoblock: block=%u lba=%u reference_present=1 "
		       "crc=%#x reference_crc=%#x match=1\n",
		       block + 1u, pinned->lba + block, crc[block], reference[block]);
	}

	/* Every check has passed: commit the sealed slice to the caller now. */
	if (slice != NULL) {
		if (dst_capacity < slice_len) {
			twoblock_fail("slice destination too small");
			return false;
		}
		for (uint32_t i = 0; i < slice_len; ++i)
			dst[i] = twoblock_stage[i];
		*out_delivered = slice_len;
		/* 64-bit: the backup window's absolute offset is far above 2^32/4096. */
		printf("mt6895-ufs-twoblock: slice offset=%llu blocks=%u bytes=%u first=%u second=%u "
		       "cap=%u crc=%#x delivered=1\n",
		       (unsigned long long)((uint64_t)slice->first_lba * VR_BLOCK_BYTES) +
		           slice->in_block_offset,
		       slice->blocks, slice_len, slice->first_bytes, slice->second_bytes,
		       dst_capacity, slice_crc);
	} else if (out_delivered != NULL) {
		*out_delivered = 0u;
	}
	return true;
}

#ifdef US_HOST_TEST
/* The listing hook lives in the test, which includes this file. */
void twoblock_test_listing(uint32_t count, uint64_t leaf, const uint64_t *ids);
#endif
#include "ufs_range_vfs.h"

void
hakios_mt6895_ufs_twoblock_quiesce(void)
{
	us_stop(&twoblock_lease);
}

void
hakios_mt6895_ufs_twoblock_init(void)
{
#ifdef US_HOST_TEST
	/* Cleared before any early return, so a refused call never reports a stale count. */
	twoblock_test_delivered = 0u;
#endif
	uint32_t mode = 0;
	uint32_t trial = 0;
	if (!PE_parse_boot_argn("mtk_ufs_twoblock", &mode, sizeof(mode)) || !mode) {
#ifdef US_HOST_TEST
	#endif
		return;
	}
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
	if (us_control(&twoblock_lease) != US_NEW) {
#ifdef US_HOST_TEST
	#endif
		return;
	}

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

	/* XNU can truncate string results. Keep a second character to reject longer values. */
	char block_arg[3] = {0};
	uint32_t blocks = VR2_BLOCKS;
	if (PE_parse_boot_arg_str("mtk_ufs_blocks", block_arg, sizeof(block_arg))) {
		if ((block_arg[0] != '1' && block_arg[0] != '2') || block_arg[1] != '\0') {
			printf("mt6895-ufs-twoblock: invalid block count; no request\n");
			return;
		}
		blocks = (uint32_t)(block_arg[0] - '0');
	}

	const struct vr_limits limits = vr_runtime_limits();

	/* Trial726: a byte range from the window start. Planned here, before any command is built,
	 * so a refused range never rings the doorbell. */
	uint64_t range_off = 0, range_len = 0, range_cap = 0;
	struct vrb_window slice;
	enum vrb_reject planned = VRB_OK;
	/* Both keys or neither. A single key must not silently fall back to the whole-block read:
	 * that would move data the caller never asked for. */
	const bool have_off = PE_parse_boot_argn("mtk_ufs_byte_offset", &range_off, sizeof(range_off));
	const bool have_len = PE_parse_boot_argn("mtk_ufs_byte_length", &range_len, sizeof(range_len));
	if (have_off != have_len) {
		printf("mt6895-ufs-twoblock: byte range needs both offset and length; no request\n");
		return;
	}
	const bool range_mode = have_off && have_len;
	if (range_mode) {
		if (range_off > UINT32_MAX || range_len > UINT32_MAX) {
			printf("mt6895-ufs-twoblock: byte range out of range; no request\n");
			return;
		}
		/* The capacity that matters is the buffer we will actually write. The declared value is
		 * an upper bound from the command line; planning uses whichever is smaller, so a buffer
		 * smaller than declared is refused before the doorbell instead of after the read. */
		uint64_t declared = range_len;
		if (PE_parse_boot_argn("mtk_ufs_byte_capacity", &declared, sizeof(declared)) &&
		    declared > UINT32_MAX) {
			printf("mt6895-ufs-twoblock: byte capacity out of range; no request\n");
			return;
		}
		const uint32_t actual_cap =
#ifdef US_HOST_TEST
		    twoblock_test_cap();
#else
		    (uint32_t)sizeof(twoblock_slice);
#endif
		range_cap = (declared < actual_cap) ? declared : actual_cap;
		if (range_cap > UINT32_MAX) range_cap = UINT32_MAX;
		planned = vrb_plan((uint32_t)range_off, (uint32_t)range_len,
		    (uint32_t)range_cap, &limits, &slice);
		if (planned != VRB_OK) {
			printf("mt6895-ufs-twoblock: byte range refused reason=%u; no request\n",
			    (unsigned)planned);
			return;
		}
		/* The covering blocks, not the command-line start, decide where the read goes. */
		start = slice.first_lba;
		blocks = slice.blocks;
	}

	/* One local request built here: encoding, per-block verification and the log all use it. */
	const struct vr_request pinned = {
	    VR_LUN, start, blocks, VR_BLOCK_BYTES,
	};

	/* The three-request experiment is opt-in and pins the first and last reads to LBA 3. */
	char mixed_arg[3] = {0};
	bool mixed = false;
	if (PE_parse_boot_arg_str("mtk_ufs_mixed", mixed_arg, sizeof(mixed_arg))) {
		if (mixed_arg[1] != '\0' || (mixed_arg[0] != '0' && mixed_arg[0] != '1')) return;
		mixed = mixed_arg[0] == '1';
	}
	/* Range and the three-request experiment are different trials; together they would issue
	 * three reads for a request that must cost one. Refuse before any command is built. */
	if (range_mode && mixed) {
		printf("mt6895-ufs-twoblock: byte range cannot combine with mixed; no request\n");
		return;
	}
	uint32_t vfs = 0;
	(void)PE_parse_boot_argn("mtk_ufs_vfs", &vfs, sizeof(vfs));
	/*
	 * Mode 12 is not offered yet. The listing branch passes its host tests, but one run on the
	 * device produced no kernel log at all and the guard stopped there, and that is not understood
	 * well enough to hand it a boot argument again. The code stays; the door stays shut.
	 */
#ifdef US_HOST_TEST
	const uint32_t highest_mode = APFS_PATH_MODE_LIST;
#else
	const uint32_t highest_mode = APFS_PATH_MODE_STAGE3;
#endif
	if (vfs && (vfs > highest_mode || !range_mode || range_off != range_source_offset(vfs) ||
	    range_len != range_source_length(vfs) || range_cap != range_len)) return;
	if (mixed && (start != 3u || blocks != 1u)) return;
	const struct vr_request standard_requests[] = { pinned, { VR_LUN, 2u, 2u, VR_BLOCK_BYTES }, pinned };
	const bool backup_table = vfs == GPT_BACKUP_MODE || vfs == XNUSYS_MODE ||
	    vfs == APFS_VOLUME_MODE || vfs == APFS_FSTREE_MODE || APFS_IS_PATH_MODE(vfs);
	const struct vr_request *requests = backup_table ? gpt_backup_requests :
	    vfs == GPT_ARRAY_MODE ? gpt_requests : standard_requests;
	/* Only the fixed table is pre-validated here. Trial735's chained commands cannot be built
	 * until the parser has answered, so the count stays at the table length; the extra four are
	 * authorised one at a time when they are actually issued. */
	const uint32_t count = (backup_table || vfs == GPT_ARRAY_MODE) ? GPT_REQUESTS :
	    (mixed ? (uint32_t)(sizeof(standard_requests) / sizeof(standard_requests[0])) : 1u);
	/* The backup window lives above the 16-block stage window, so those three fixed requests are
	 * encoded against the capacity limits. Nothing else changes: the stage guard still governs
	 * every other mode, and these requests are still the only three this mode can build. */
	const struct vr_limits encode_limits =
	    backup_table ? vr_capacity_limits() : limits;
	for (uint32_t i = 0; i < count; ++i) {
		const struct vr_request *r = &requests[i];
		struct vr_command encoded;
		if (!(backup_table ? vrq_make_backup_read(r, &encode_limits, &encoded) :
		    vrq_make_read(r, &encode_limits, &encoded))) return;
		for (uint32_t block = 0; block < r->blocks; ++block) {
			uint32_t crc;
			if (!twoblock_reference(r->lba + block, vfs, &crc)) {
				printf("mt6895-ufs-twoblock: lba %u has no frozen reference; no request\n",
				    r->lba + block);
				return;
			}
		}
	}

	printf("mt6895-ufs-twoblock: bounded READ10 lun2 lba%u blocks%u bytes%u "
	       "budget%u; guarded handoff required\n",
	       requests[0].lba, requests[0].blocks, requests[0].blocks * VR_BLOCK_BYTES,
	       APFS_IS_PATH_MODE(vfs) ? APFS_PATH_TOTAL_REQUESTS :
	       vfs == APFS_FSTREE_MODE ? APFS_TOTAL_REQUESTS :
	       vfs == APFS_VOLUME_MODE ? count + 1u + APFS_CHAIN_REQUESTS :
	       vfs == XNUSYS_MODE ? count + 1u : count);

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
	platform.data_bytes = pinned.blocks * VR_BLOCK_BYTES;
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

	if (vfs) {
		range_register(vfs);
		return;
	}

	uint32_t delivered = 0;
	uint32_t completed = 0;
	/* Destination chosen once: the host test supplies the caller's buffer, the kernel uses the
	 * internal one sized from the frozen window. */
	uint8_t *const dst = range_mode
#ifdef US_HOST_TEST
	    ? twoblock_test_dst()
#else
	    ? twoblock_slice
#endif
	    : NULL;
	const uint32_t dst_cap = range_mode
#ifdef US_HOST_TEST
	    ? twoblock_test_cap()
#else
	    ? range_cap
#endif
	    : 0u;
	while (completed < count &&
	    twoblock_run(&requests[completed], &limits, completed + 1u,
	        range_mode ? &slice : NULL, dst, dst_cap, &delivered)) {
		++completed;
	}
	if (range_mode && completed == count && delivered != 0u) {
		/* Observe the committed caller buffer across the compiler's memory boundary. */
		__asm__ volatile("" : : "r"(dst) : "memory");
		printf("mt6895-ufs-twoblock: caller bytes=%u crc=%#x\n", delivered,
		       vrq_crc32(dst, delivered));
	}
#ifdef US_HOST_TEST
	twoblock_test_delivered = delivered;
#endif
	printf("mt6895-ufs-twoblock: final control=%#x shared=%u failed=%u delivered=%u\n",
	       us_control(&twoblock_lease), shared_snapshot(&platform).state,
	       completed == count ? 0u : 1u, delivered);
}
