#define _GNU_SOURCE
#define US_HOST_TEST 1

#include <assert.h>
#include <string.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <stdlib.h>
#include <stdio.h>

#include "hakios.mt6895_ufs_twoblock.c"

unsigned long gPhysBase = 0x100000000ul;
unsigned long gPhysSize = 0x100000000ul;

static uint32_t regs[VR_REG_BYTES / 4u];
static uint32_t table_page[PAGE_SIZE / 4u];
static uint8_t *scratch;
static uint8_t *ref_window;   /* local reference blocks, never printed */
/* Trial733: the backup window lives far above the stage range, so it gets its own five-block
 * reference laid out exactly as the published snapshot: header first, then the four array blocks.
 * Keeping it separate means the low-LBA tests cannot silently satisfy a high-LBA read. */
static uint8_t *ref_backup;   /* 5 blocks: LBA 31240191, then 31240184..187 */
static bool use_backup_ref;
/* Trial734: the fourth command reads the first block of xnusys, which is not part of the backup
 * window, so it carries its own reference block. */
static uint8_t *ref_nxsb;
static bool use_nxsb_ref;
/* Trial735: the six APFS chain blocks the parser indexes: NXSB first, then the five fetched. */

/*
 * Trial737's blocks, straight from the phone: reference/walk737 holds one file per relative
 * block, each 4096 bytes, each the result of the same dd the driver's reads go through.
 */
#define WALK_DIR "reference/walk738"
static uint8_t walk_pool[64][VR_BLOCK_BYTES];
static uint32_t walk_used;
static bool walk_block_known(uint32_t rel)
{
	char path[256];
	FILE *f;
	snprintf(path, sizeof(path), WALK_DIR "/%u.bin", rel);
	f = fopen(path, "rb");
	if (!f) return false;
	fclose(f);
	return true;
}
static const uint8_t *walk_block(uint32_t rel)
{
	char path[256];
	FILE *f;
	if (walk_used >= 64u) return NULL;
	snprintf(path, sizeof(path), WALK_DIR "/%u.bin", rel);
	f = fopen(path, "rb");
	if (!f) return NULL;
	if (fread(walk_pool[walk_used], 1, VR_BLOCK_BYTES, f) != VR_BLOCK_BYTES) {
		fclose(f);
		return NULL;
	}
	fclose(f);
	return walk_pool[walk_used++];
}

static uint8_t *ref_chain;
static bool use_chain_ref;
static unsigned chain_blocks_loaded;
#include "ufs_vfs_callbacks.h"
static bool overlap_fill;
static unsigned failures;     /* Trial726 cases count their own failures */
static struct {
	uint32_t mode, trial, other, stop_at;
	unsigned maps, reg_reads, rings, data_writes, restore_writes;
	bool pending, completed, bad_end, corrupt_last;
	/* Trial740: the request's address is chosen by the caller, not by a fixed sequence. */
	bool free_address;
	/* Trial750: what the last completed command was, so a write can be told from a read. */
	bool cdb_is_write;
	unsigned dbr_reads, completions;
	/* Trial750: the payload the model saw in the DATA region for that write. */
	uint8_t cdb_write_payload[4096];
	/* Trial739: what the listing reported, so the test can check it without parsing stdout. */
	uint32_t listed;
	uint64_t listed_ids[8];
	uint64_t list_leaf;
	bool partial_last, fail_last_restore, swap_blocks;
	unsigned apfs_final_fault, final_fault_injections;
	uint64_t time_us;
	uint32_t want_lba, cdb_lba, cdb_blocks;
	bool start_lba_present;      /* absent and explicit 0 must be distinguishable */
	uint64_t start_lba_raw;      /* full width, so 2^32+2 cannot wrap to 2 */
	/* Trial726 byte range; absent means the whole-block 725 path. */
	uint32_t vfs_mode;
	bool range_present, off_only, len_only, cap_present;
	uint64_t range_offset, range_length, range_capacity;
} model;

/* The expectation comes from the test case, independently of the placed descriptor. */
static uint32_t want_blocks;
static const char *block_arg;
static const char *mixed_arg;
static unsigned fault_request, stop_request, verified_copies;
static uint8_t initial_data[VR2_TRANSFER_BYTES], previous_copy[VR2_TRANSFER_BYTES];
static struct ur_workspace initial_desc;

/* Every case states its start LBA here; the stub reads these fields. */
static void
set_start(uint32_t lba)
{
	model.want_lba = lba;
	model.start_lba_present = true;
	model.start_lba_raw = lba;
}

static void
clear_start(void)
{
	model.want_lba = VR2_LBA_DEFAULT;   /* what the adapter must fall back to */
	model.start_lba_present = false;
	model.start_lba_raw = 0;
}

static uint32_t *table(void)
{
	return table_page + ((UR_LIST_PA & PAGE_MASK) / 4u);
}

static uint32_t *ucd(void)
{
	return (uint32_t *)(void *)(scratch + (UR_UCD_PA - UR_LEASE_PA));
}

static uint32_t *data(void)
{
	return (uint32_t *)(void *)(scratch + (UR_DATA_PA - UR_LEASE_PA));
}

static struct ur_lease *lease_record(void)
{
	return (struct ur_lease *)(void *)scratch;
}

bool
PE_parse_boot_argn(const char *name, void *out, size_t size)
{
	uint32_t value;
	if (strcmp(name, "mtk_ufs_start_lba") == 0) {
		/* The adapter asks for the full width so it can range-check before narrowing. */
		assert(size == sizeof(uint64_t));
		if (!model.start_lba_present) return false;
		memcpy(out, &model.start_lba_raw, size);
		return true;
	}
	if (strcmp(name, "mtk_ufs_byte_capacity") == 0) {
		assert(size == sizeof(uint64_t));
		if (!model.range_present || !model.cap_present) return false;
		const uint64_t c = model.range_capacity;
		memcpy(out, &c, size);
		return true;
	}
	if (strcmp(name, "mtk_ufs_byte_offset") == 0 ||
	    strcmp(name, "mtk_ufs_byte_length") == 0) {
		/* Also full width, so a value above 32 bits is refused rather than wrapped. */
		assert(size == sizeof(uint64_t));
		const bool is_off = strcmp(name, "mtk_ufs_byte_offset") == 0;
		if (is_off && !(model.range_present || model.off_only)) return false;
		if (!is_off && !(model.range_present || model.len_only)) return false;
		const uint64_t v = is_off ? model.range_offset : model.range_length;
		memcpy(out, &v, size);
		return true;
	}
	assert(size == sizeof(uint32_t));
	if (strcmp(name, "mtk_ufs_twoblock") == 0) value = model.mode;
	else if (strcmp(name, "mtk_ufs_vfs") == 0) value = model.vfs_mode;
	else if (strcmp(name, "mali_trial") == 0) value = model.trial;
	else if (strcmp(name, "mtk_ufs_varread") == 0) value = model.other;
	else return false;
	memcpy(out, &value, size);
	return true;
}

bool PE_parse_boot_arg_str(const char *name, char *out, int size)
{
	assert(size > 0);
	const char *value;
	if (strcmp(name, "mtk_ufs_blocks") == 0) value = block_arg;
	else {
		assert(strcmp(name, "mtk_ufs_mixed") == 0);
		value = mixed_arg;
	}
	if (!value) return false;
	snprintf(out, (size_t)size, "%s", value);
	return true;
}

bool pmap_valid_address(uint64_t address)
{
	return model.bad_end && address == UR_DATA_PA + VR2_TRANSFER_BYTES - 1u;
}

vm_offset_t
ml_io_map_unmappable(vm_offset_t address, vm_size_t length, unsigned int flags)
{
	assert(flags == VM_WIMG_WCOMB);
	model.maps++;
	if (address == UR_LEASE_PA) {
		assert(length == 0x10000u);
		return (vm_offset_t)scratch;
	}
	assert(address == (UR_LIST_PA & ~(uint64_t)PAGE_MASK) && length == PAGE_SIZE);
	return (vm_offset_t)table_page;
}

vm_offset_t ml_io_map(vm_offset_t address, vm_size_t length)
{
	assert(address == VR_REG_PA && length == VR_REG_BYTES);
	model.maps++;
	return (vm_offset_t)regs;
}

static void
model_complete(void)
{
	uint8_t upiu[32];
	for (unsigned i = 0; i < 8; i++) vrq_put32le(upiu + i * 4u, ucd()[i]);
	/*
	 * Trial750: the model now recognises a write as well as a read.
	 *
	 * The two differ in exactly three places -- the data direction byte, the opcode and the UTRD's
	 * direction bits -- and a stub that only knew 0x28 would abort on the first WRITE(10) rather
	 * than describe it. Both shapes are asserted strictly; anything else is still a failure.
	 */
	if (upiu[16] == 0x2a) {
		assert(upiu[0] == 1 && upiu[1] == 0x00 && upiu[2] == 2);
		assert(upiu[23] == 0 && upiu[24] == 1);              /* one block */
		model.cdb_is_write = true;
	} else {
		assert(upiu[0] == 1 && upiu[1] == 0x40 && upiu[2] == 2 && upiu[16] == 0x28);
		model.cdb_is_write = false;
	}
	if (model.cdb_is_write) {
		for (unsigned i = 0; i < 4096 / 4; i++)
			vrq_put32le(model.cdb_write_payload + i * 4u, data()[i]);
	}
	model.cdb_lba = ur_be32(upiu + 18);
	model.cdb_blocks = ((uint32_t)upiu[23] << 8) | upiu[24];
	/*
	 * Trial750, and it goes first: a write's address can fall inside the ranges the chain
	 * branches below accept (608456 is between 2399 and 655607), so recognising it afterwards
	 * meant the write was checked against a captured chain block it was never meant to match.
	 */
	if (model.cdb_is_write) {
		assert(model.cdb_blocks == 1u);
		assert(model.cdb_lba >= XNUSYS_LBA + 608448u);
		assert(model.cdb_lba <= XNUSYS_LBA + 653559u);
		return;
	}
	/* The backup window is driven by a fixed table, so the expected LBA/block pair comes from the
	 * table position rather than from the boot-argument geometry. */
	/* rings counts completed doorbells, so the chain command being prepared is rings-3:
	 * after the fourth command rings == 4, which is chain step 0. */
	/* Chain commands are identified by their address, not by a ring counter: the xnusys block and
	 * the first chain step are both prepared when rings == 4. */
	if (use_chain_ref && (model.vfs_mode == APFS_PATH_MODE ||
	    model.vfs_mode == APFS_PATH_MODE_LIST) &&
	    model.cdb_lba >= XNUSYS_LBA + 2399u && model.cdb_lba <= XNUSYS_LBA + 655607u) {
		/*
		 * Trial737. The walk names its own blocks, so the stub cannot list them; what it can do
		 * is insist the engine allowed the address and that a real block exists there. The
		 * blocks come from reference/walk737, which holds what this phone actually returned.
		 */
		assert(model.cdb_blocks == 1u);
		assert(walk_block_known(model.cdb_lba - XNUSYS_LBA));
	} else if (use_chain_ref && (model.vfs_mode == APFS_FSTREE_MODE ||
	    model.vfs_mode == APFS_PATH_MODE ||
	    model.vfs_mode == APFS_PATH_MODE_LIST) &&
	    model.cdb_lba >= XNUSYS_LBA + 2397u && model.cdb_lba <= XNUSYS_LBA + 2510u) {
		/* Trial736: rel 2397/2398/2399/2510 follow the six from Trial735. */
		static const struct { uint32_t rel, blocks; } step[] = {
			{ 2397u, 1u }, { 2398u, 1u }, { 2399u, 1u }, { 2510u, 1u },
		};
		bool matched = false;
		for (unsigned i = 0; i < 4u; ++i) {
			if (model.cdb_lba == XNUSYS_LBA + step[i].rel) {
				matched = model.cdb_blocks == step[i].blocks;
				break;
			}
		}
		if (!matched)
			fprintf(stderr, "  FSTREE lba=%u blocks=%u not a 736 step\n",
			    model.cdb_lba, model.cdb_blocks);
		assert(matched);
	} else if (use_chain_ref && model.cdb_lba != XNUSYS_LBA &&
	    model.cdb_lba >= XNUSYS_LBA + 1u && model.cdb_lba <= XNUSYS_LBA + 2396u) {
		static const struct { uint32_t rel, blocks; } step[] = {
			{ 1u, 2u }, { 2394u, 1u }, { 2395u, 1u }, { 2396u, 1u },
		};
		bool matched = false;
		for (unsigned i = 0; i < 4u; ++i) {
			if (model.cdb_lba == XNUSYS_LBA + step[i].rel) {
				matched = model.cdb_blocks == step[i].blocks;
				break;
			}
		}
		if (!matched)
			fprintf(stderr, "  CHAIN lba=%u blocks=%u not a chain step\n",
			    model.cdb_lba, model.cdb_blocks);
		assert(matched);
	} else if (model.free_address && model.cdb_is_write) {
		/*
		 * Trial750. A write names its block too, but the zone it may land in is empty -- there is
		 * no captured block to compare against, which is the point of a zone nothing owns. What
		 * still holds: one block, inside the partition, and inside the zone this contract allows.
		 */
		assert(model.cdb_blocks == 1u);
		assert(model.cdb_lba >= XNUSYS_LBA + 608448u);
		assert(model.cdb_lba <= XNUSYS_LBA + 653559u);
		assert(model.start_lba_present);
	} else if (model.free_address) {
		/* Trial740. A mounting file system reads whatever its own structures name, so the
		 * harness cannot predict the address. What it can still require: whole blocks within one
		 * command, inside the partition, and a block that was actually captured from this phone. */
		assert(model.cdb_blocks >= 1u && model.cdb_blocks <= 2u);
		assert(model.cdb_lba >= XNUSYS_LBA);
		assert(walk_block_known(model.cdb_lba - XNUSYS_LBA));
	} else if (use_nxsb_ref && model.cdb_lba == XNUSYS_LBA) {
		/* Fourth command: the resolved xnusys block, one block only. */
		assert(model.cdb_blocks == 1u);
	} else if (use_backup_ref) {
		const uint32_t expect_lba = model.rings == 1u ? GPT_BACKUP_HEADER_LBA
		    : (model.rings == 2u ? GPT_BACKUP_ARRAY_LBA : GPT_BACKUP_ARRAY_LBA + 2u);
		const uint32_t expect_blocks = model.rings == 1u ? 1u : 2u;
		if (!(model.cdb_lba == expect_lba && model.cdb_blocks == expect_blocks))
			fprintf(stderr, "  STUB rings=%u got lba=%u blocks=%u want lba=%u blocks=%u nxsb=%u\n",
			    model.rings, model.cdb_lba, model.cdb_blocks, expect_lba, expect_blocks,
			    (unsigned)use_nxsb_ref);
		assert(model.cdb_lba == expect_lba && model.cdb_blocks == expect_blocks);
	} else {
		assert(model.cdb_lba == model.want_lba);
		assert(model.cdb_blocks == want_blocks);
	}
	const uint32_t bytes = model.cdb_blocks * VR_BLOCK_BYTES;
	assert(ur_be32(upiu + 12) == bytes);
	assert((table()[7] & 0xffffu) == 1u && ucd()[256] == UR_DATA_PA && ucd()[259] == bytes - 1u);

	/* Resolve the source for this LBA. The backup window is the only place above the stage
	 * range, and it must come from its own reference, not from the low window. */
	const uint8_t *src = NULL;
	if (use_chain_ref && (model.vfs_mode == APFS_PATH_MODE ||
	    model.vfs_mode == APFS_PATH_MODE_LIST) &&
	    model.cdb_lba >= XNUSYS_LBA + 2399u && model.cdb_lba <= XNUSYS_LBA + 655607u) {
		/*
		 * Trial737. The walk names its own blocks, so the stub cannot list them; what it can do
		 * is insist the engine allowed the address and that a real block exists there. The
		 * blocks come from reference/walk737, which holds what this phone actually returned.
		 */
		assert(model.cdb_blocks == 1u);
		assert(walk_block_known(model.cdb_lba - XNUSYS_LBA));
		src = walk_block(model.cdb_lba - XNUSYS_LBA);
		assert(src != NULL);
	} else if (use_chain_ref && (model.vfs_mode == APFS_FSTREE_MODE ||
	    model.vfs_mode == APFS_PATH_MODE ||
	    model.vfs_mode == APFS_PATH_MODE_LIST) &&
	    model.cdb_lba >= XNUSYS_LBA + 2397u && model.cdb_lba <= XNUSYS_LBA + 2510u) {
		/* Trial736: rel 2397/2398/2399/2510 follow the six from Trial735. */
		static const struct { uint32_t rel, blocks; } step[] = {
			{ 2397u, 1u }, { 2398u, 1u }, { 2399u, 1u }, { 2510u, 1u },
		};
		bool matched = false;
		for (unsigned i = 0; i < 4u; ++i) {
			if (model.cdb_lba == XNUSYS_LBA + step[i].rel) {
				matched = model.cdb_blocks == step[i].blocks;
				break;
			}
		}
		if (!matched)
			fprintf(stderr, "  FSTREE lba=%u blocks=%u not a 736 step\n",
			    model.cdb_lba, model.cdb_blocks);
		assert(matched);
		{
			const uint32_t rel736 = model.cdb_lba - XNUSYS_LBA;
			const uint32_t idx736 = rel736 == 2397u ? 6u :
			    (rel736 == 2398u ? 7u : (rel736 == 2399u ? 8u : 9u));
			src = ref_chain + (size_t)idx736 * VR_BLOCK_BYTES;
		}
	} else if (use_chain_ref && model.cdb_lba != XNUSYS_LBA &&
	    model.cdb_lba >= XNUSYS_LBA + 1u && model.cdb_lba <= XNUSYS_LBA + 2396u) {
		/* ref_chain is [NXSB, rel1, rel2, 2394, 2395, 2396], the parser's own indexing. */
		const uint32_t rel = model.cdb_lba - XNUSYS_LBA;
		const uint32_t idx = (rel == 1u || rel == 2u) ? rel :
		    (rel == 2394u ? 3u : (rel == 2395u ? 4u : 5u));
		src = ref_chain + (size_t)idx * VR_BLOCK_BYTES;
	} else if (model.free_address) {
		/* Trial740: the block comes from the captured set, by the address the caller chose. */
		src = walk_block(model.cdb_lba - XNUSYS_LBA);
		assert(src != NULL);
	} else if (use_nxsb_ref && model.cdb_lba == XNUSYS_LBA) {
		src = ref_nxsb;
	} else if (use_backup_ref) {
		assert(model.cdb_lba >= GPT_BACKUP_ARRAY_LBA);
		assert(model.cdb_lba + model.cdb_blocks <= GPT_BACKUP_ARRAY_LBA + 4u ||
		    model.cdb_lba == GPT_BACKUP_HEADER_LBA);
		if (model.cdb_lba == GPT_BACKUP_HEADER_LBA) {
			src = ref_backup;
		} else {
			/* The array blocks follow the header block in the snapshot reference. */
			src = ref_backup + VR_BLOCK_BYTES +
			    (size_t)(model.cdb_lba - GPT_BACKUP_ARRAY_LBA) * VR_BLOCK_BYTES;
		}
	} else {
		assert(model.cdb_lba + model.cdb_blocks <= 16u);
		src = ref_window + (size_t)model.cdb_lba * VR_BLOCK_BYTES;
	}
	assert(src);
	memcpy(data(), src, bytes);
	if (model.swap_blocks) {
		for (uint32_t i = 0; i < VRQ_BLOCK_WORDS; i++) {
			uint32_t t = data()[i];
			data()[i] = data()[VRQ_BLOCK_WORDS + i];
			data()[VRQ_BLOCK_WORDS + i] = t;
		}
	}
	if (model.corrupt_last && (!fault_request || model.rings == fault_request)) data()[bytes / 4u - 1u] ^= 1u;

	ucd()[512 / 4] = 0x00020021u;
	table()[2] = 0;
	regs[0x58 / 4] = 0;
	model.pending = false;
	model.completed = true;
}

bool vr_host_reg_hook(uint16_t offset)
{
	model.reg_reads++;
	if (offset == 0x58) {
		model.dbr_reads++;
		if (model.pending) { model.completions++; model_complete(); }
	}
	return true;
}

bool vr_host_ring_hook(void)
{
	/* The fixed-window modes may ring up to three times; everything else stays single-command. */
	const uint32_t ring_cap = (model.vfs_mode == APFS_PATH_MODE_STAGE2 ||
	    model.vfs_mode == APFS_PATH_MODE_STAGE3 ||
	    model.vfs_mode == APFS_PATH_MODE_LIST) ? 64u :
	    model.vfs_mode == APFS_FSTREE_MODE ? 12u :
	    model.vfs_mode == APFS_VOLUME_MODE ? 8u :
	    model.vfs_mode == XNUSYS_MODE ? 4u :
	    ((mixed_arg || model.vfs_mode == 5u || model.vfs_mode == GPT_BACKUP_MODE) ? 3u : 1u);
	assert(regs[0x58 / 4] == 1 && model.rings < ring_cap);
	model.rings++;
	model.pending = true;
	return true;
}

bool vr_host_read_word_hook(int region, uint32_t offset)
{
	/* The command in flight decides the data span; the backup table's second and third commands
	 * move two blocks even though the boot-argument seed names one. */
	/* rings counts completed doorbells, so the command being prepared is rings+1: the first moves
	 * one block, the second and third move two each. */
	/* This hook runs while the command is being placed, before the CDB has been decoded, so the
	 * span comes from what the mode can move: chain steps are one or two blocks, everything else
	 * follows the seed. */
	const uint32_t data_blocks = model.vfs_mode == APFS_VOLUME_MODE ? 2u :
	    model.vfs_mode == XNUSYS_MODE ? 2u :
	    (use_backup_ref && model.rings >= 1u ? 2u : want_blocks);
	if (region == VRQ_DATA) assert(offset < data_blocks * VR_BLOCK_BYTES);
	if (region == VRQ_DATA && model.completed && model.partial_last &&
	    offset == (use_backup_ref && model.rings >= 1u ? 2u : want_blocks) * VR_BLOCK_BYTES - 4u) {
		model.partial_last = false;
		return false;
	}
	return true;
}

bool vr_host_write_hook(int region, uint32_t offset)
{
	/* Same span rule as the read hook: the backup table's later commands move two blocks. */
	const uint32_t write_blocks = model.vfs_mode == APFS_VOLUME_MODE ? 2u :
	    model.vfs_mode == XNUSYS_MODE ? 2u :
	    (use_backup_ref && model.rings >= 1u ? 2u : want_blocks);
	if (region == VRQ_DATA) {
		assert(offset < write_blocks * VR_BLOCK_BYTES);
		model.data_writes++;
		if (model.completed) {
			if (model.fail_last_restore && offset == write_blocks * VR_BLOCK_BYTES - 4u) {
				model.fail_last_restore = false;
				return false;
			}
			model.restore_writes++;
		}
	}
	return true;
}

bool twoblock_host_bad_managed(void) { return false; }
bool twoblock_host_claim_hook(void) { return true; }
bool twoblock_host_finish_hook(void) { return true; }
void vr_host_barrier(void) {}
uint64_t vr_host_now_us(void) { return model.time_us++; }

void twoblock_host_stop_point(int which)
{
	/* Inject after the transport CRC and restore checks, so only the final APFS parser can
	 * reject this object. Faults 2 and 3 carry valid APFS checksums but wrong identity/magic. */
	if (which == 3 && model.vfs_mode == APFS_VOLUME_MODE && model.rings == 8u &&
	    model.apfs_final_fault) {
		uint8_t *p = twoblock_stage;
		assert(!memcmp(p, ref_chain + 5u * VR_BLOCK_BYTES, VR_BLOCK_BYTES));
		if (model.apfs_final_fault == 1u) {
			p[0] ^= 1u;
		} else {
			p[model.apfs_final_fault == 2u ? 8u : 32u] ^= 1u;
			uint64_t a = 0u, b = 0u, mod = UINT32_MAX;
			for (unsigned i = 8u; i < VR_BLOCK_BYTES; i += 4u) {
				a = (a + av_u32(p + i)) % mod;
				b = (b + a) % mod;
			}
			uint64_t lo = mod - (a + b) % mod;
			uint64_t checksum = ((mod - (a + lo) % mod) << 32) | lo;
			for (unsigned i = 0; i < 8u; ++i) p[i] = (uint8_t)(checksum >> (8u * i));
			assert(av_object(p, 13u, 0u));
		}
		model.final_fault_injections++;
	}
	if (overlap_fill && which == 1) {
		uint8_t b = 0xa5;
		struct uio other = {0, 1, &b, 0, true};
		assert(range_dev_read(fake_node.device, &other, 0) == EBUSY);
		assert(b == 0xa5 && !other.moved && range_rings() == 1);
		overlap_fill = false;
	}
	if (model.stop_at == (uint32_t)which && (!stop_request || stop_request == model.rings))
		hakios_mt6895_ufs_twoblock_quiesce();
	if (which == 3 && model.vfs_mode == 5) {
		assert(!memcmp(table(), initial_desc.table, sizeof(initial_desc.table)));
		assert(!memcmp(ucd(), initial_desc.ucd, sizeof(initial_desc.ucd)));
		assert(!memcmp(data(), initial_data, sizeof(initial_data)));
		assert(!memcmp(twoblock_copy, ref_window + model.want_lba * VR_BLOCK_BYTES,
		    want_blocks * VR_BLOCK_BYTES));
		model.completed = false;
		model.want_lba = model.rings == 1u ? 2u : 4u;
		want_blocks = 2;
	}
	if (which == 3 && mixed_arg && strcmp(mixed_arg, "1") == 0) {
		const uint32_t bytes = want_blocks * VR_BLOCK_BYTES;
		assert(model.rings == verified_copies + 1);
		assert(memcmp(table(), initial_desc.table, sizeof(initial_desc.table)) == 0);
		assert(memcmp(ucd(), initial_desc.ucd, sizeof(initial_desc.ucd)) == 0);
		assert(memcmp(data(), initial_data, sizeof(initial_data)) == 0);
		assert(memcmp(twoblock_copy, ref_window + model.want_lba * VR_BLOCK_BYTES, bytes) == 0);
		assert(memcmp(twoblock_copy + bytes, previous_copy + bytes, sizeof(previous_copy) - bytes) == 0);
		memcpy(previous_copy, twoblock_copy, sizeof(previous_copy));
		verified_copies++;
		model.completed = false;
		want_blocks = model.rings == 1u ? 2u : 1u;
		model.want_lba = model.rings == 1u ? 2u : 3u;
	}
}

static void
reset(void)
{
	memset(&model, 0, sizeof(model));
	vfs_request_used = 0;
	snapshot_state = SNAP_EMPTY;
	range_mode_id = 0;
	overlap_fill = false;
	twoblock_total_rings = 0;
	range_ready = 0;
	worker = NULL;
	clear_start();
	want_blocks = VR2_BLOCKS;
	block_arg = NULL;
	mixed_arg = NULL;
	fault_request = stop_request = verified_copies = 0;
	model.listed = 0u;
	model.free_address = false;
	model.list_leaf = 0u;
	memset(model.listed_ids, 0, sizeof(model.listed_ids));
	memset(twoblock_copy, 0xa5, sizeof(twoblock_copy));
	memset(&platform, 0, sizeof(platform));
	memset(&twoblock_outcome, 0, sizeof(twoblock_outcome));
	memset(&twoblock_counters, 0, sizeof(twoblock_counters));
	twoblock_lease = (struct us_lease){ 0, twoblock_publish, &platform };
	memset(regs, 0, sizeof(regs));
	memset(table_page, 0, sizeof(table_page));
	memset(scratch, 0, VR_SCRATCH_BYTES);
	model.mode = 1;
	model.trial = UR_TRIAL;

	regs[0] = 0x3587071fu;
	regs[8 / 4] = 0x200;
	regs[0x30 / 4] = 3;
	regs[0x34 / 4] = 1;
	regs[0x50 / 4] = (uint32_t)UR_LIST_PA;
	regs[0x60 / 4] = 1;
	*lease_record() = (struct ur_lease){ UR_MAGIC, UR_VERSION, UR_TRIAL, UR_UNUSED };
	struct vr_request predecessor = { VR_LUN, 1, 1, VR_BLOCK_BYTES };
	struct vr_limits limits = vr_runtime_limits();
	struct vr_command command;
	assert(vr_make_read(&predecessor, &limits, &command));
	memcpy(table(), command.utrd, sizeof(command.utrd));
	table()[2] = 0;
	for (unsigned i = 0; i < 8; i++) ucd()[i] = vrq_get32le(command.upiu + 4u * i);
	memcpy(ucd() + 256, command.prd, sizeof(command.prd));
	for (unsigned i = 0; i < VRQ_DATA_WORDS; i++) data()[i] = 0x13428c00u ^ i;
}

static void
test_success(void)
{
	const uint32_t bytes = want_blocks * VR_BLOCK_BYTES;
	const uint32_t words = bytes / 4u;
	uint8_t before_payload[VR2_TRANSFER_BYTES];
	struct ur_workspace before;
	memcpy(before_payload, data(), sizeof(before_payload));
	memcpy(before.table, table(), sizeof(before.table));
	memcpy(before.ucd, ucd(), sizeof(before.ucd));

	hakios_mt6895_ufs_twoblock_init();
	assert(model.maps == 3 && model.rings == 1 && model.restore_writes == words);
	assert(twoblock_outcome.result == VRQ_DELIVERED && twoblock_outcome.data_words_valid == words);
	assert(twoblock_outcome.data_words_attempted == words && twoblock_outcome.readback_restored);
	assert(us_control(&twoblock_lease) == US_IDLE && lease_record()->state == UR_CLAIMED);
	assert(memcmp(table(), before.table, sizeof(before.table)) == 0);
	assert(memcmp(ucd(), before.ucd, sizeof(before.ucd)) == 0);
	assert(memcmp(data(), before_payload, sizeof(before_payload)) == 0);
	assert(memcmp(twoblock_copy, ref_window + model.want_lba * VR_BLOCK_BYTES, bytes) == 0);
	for (uint32_t i = bytes; i < sizeof(twoblock_copy); i++) assert(twoblock_copy[i] == 0xa5);
	assert(twoblock_word(&platform, VRQ_DATA, bytes - 4u) == data() + words - 1u);
	assert(twoblock_word(&platform, VRQ_DATA, bytes) == NULL);
	assert(twoblock_word(&platform, VRQ_DATA, bytes - 2u) == NULL);

	const unsigned reads = model.reg_reads;
	hakios_mt6895_ufs_twoblock_init();
	hakios_mt6895_ufs_twoblock_quiesce();
	assert(model.maps == 3 && model.reg_reads == reads && model.rings == 1);
	assert(lease_record()->state == UR_RESTORED);
}

static void test_refused(void)
{
	hakios_mt6895_ufs_twoblock_init();
	assert(model.maps == 0 && model.reg_reads == 0 && model.rings == 0 && model.data_writes == 0);
	assert(lease_record()->state == UR_UNUSED);
}

static void request(uint32_t start, uint32_t blocks)
{
	reset();
	set_start(start);
	want_blocks = blocks;
	block_arg = blocks == 1u ? "1" : "2";
}

static void mixed_request(void)
{
	request(3, 1);
	mixed_arg = "1";
	memcpy(initial_data, data(), sizeof(initial_data));
	memcpy(initial_desc.table, table(), sizeof(initial_desc.table));
	memcpy(initial_desc.ucd, ucd(), sizeof(initial_desc.ucd));
	memcpy(previous_copy, twoblock_copy, sizeof(previous_copy));
}

static void test_sequence(void)
{
	mixed_request();
	puts("BEGIN mixed 725");
	hakios_mt6895_ufs_twoblock_init();
	puts("END mixed 725");
	assert(model.maps == 3 && model.rings == 3 && verified_copies == 3);
	assert(model.restore_writes == 4096 && us_control(&twoblock_lease) == US_IDLE);
	assert(lease_record()->state == UR_CLAIMED);
	hakios_mt6895_ufs_twoblock_init();
	hakios_mt6895_ufs_twoblock_quiesce();
	assert(model.rings == 3 && lease_record()->state == UR_RESTORED);

	for (unsigned n = 1; n <= 3; n++) {
		mixed_request();
		model.corrupt_last = true;
		fault_request = n;
		hakios_mt6895_ufs_twoblock_init();
		assert(model.rings == n && verified_copies == n - 1 && lease_record()->state == UR_POISONED);
		mixed_request();
		model.stop_at = 3;
		stop_request = n;
		hakios_mt6895_ufs_twoblock_init();
		assert(model.rings == n && lease_record()->state == UR_RESTORED);
	}
	request(2, 1);
	mixed_arg = "1";
	test_refused();
	request(3, 2);
	mixed_arg = "1";
	test_refused();
	request(3, 1);
	mixed_arg = "10";
	test_refused();
}


/* ---------------- Trial726: byte range ---------------- */

/* The caller's own buffer, with sentinels either side so a write past the range is visible. */
#define CALLER_PAD 8u
static uint8_t caller_buf[VRB_WINDOW_BYTES + 2 * CALLER_PAD];
static uint32_t caller_cap;
/* Taken from the entry point's own report, which names the byte count it actually handed over. */
static uint32_t delivered;

/* The adapter exports the count it reported; the test never assumes it equals the request. */
extern uint32_t twoblock_test_delivered;
/* The adapter takes its destination from here, so the caller buffer is the test's own. */
uint8_t *twoblock_test_dst(void) { return caller_buf + CALLER_PAD; }
uint32_t twoblock_test_cap(void) { return caller_cap; }

static void
caller_reset(void)
{
	memset(caller_buf, 0x5A, sizeof(caller_buf));
	delivered = 0;
}

/* Trial739: the driver reports what it listed so the test can check it. */
void
twoblock_test_listing(uint32_t count, uint64_t leaf, const uint64_t *ids)
{
	model.listed = count;
	model.list_leaf = leaf;
	for (uint32_t i = 0; i < 8u && ids; ++i) model.listed_ids[i] = ids[i];
}

static void
range_settings(uint64_t off, uint64_t len, uint32_t cap)
{
	reset();
	model.range_present = true;
	model.range_offset = off;
	model.range_length = len;
	model.range_capacity = cap;   /* what the command line declares */
	model.cap_present = true;
	caller_cap = cap;             /* what the buffer actually has; a case may shrink it */
	caller_reset();
	model.want_lba = (uint32_t)(off / VR_BLOCK_BYTES);
	{
		const uint32_t in_off = (uint32_t)(off % VR_BLOCK_BYTES);
		const uint64_t first = (len < VR_BLOCK_BYTES - in_off)
		    ? len : (uint64_t)(VR_BLOCK_BYTES - in_off);
		want_blocks = (len > first) ? 2u : 1u;
	}
}

/* Positive case: the edge is taken from the caller's buffer, and the CRC is computed over exactly
 * the number of bytes the entry point reports having delivered. */
static void
test_range_success(const char *name, uint64_t off, uint64_t len)
{
	range_settings(off, len, (uint32_t)len);
	const uint32_t first = (uint32_t)(off / VR_BLOCK_BYTES);
	const uint32_t in_off = (uint32_t)(off % VR_BLOCK_BYTES);
	const uint32_t first_bytes = (uint32_t)((len < VR_BLOCK_BYTES - in_off)
	    ? len : VR_BLOCK_BYTES - in_off);
	const uint32_t blocks = (len > first_bytes) ? 2u : 1u;
	model.cdb_lba = first;
	model.cdb_blocks = blocks;
	model.rings = 0;
	hakios_mt6895_ufs_twoblock_init();
	delivered = twoblock_test_delivered;

	bool ok = model.rings == 1 && model.cdb_lba == first && model.cdb_blocks == blocks;
	ok = ok && delivered == len;
	/* Compare the caller's buffer with the frozen window, byte for byte. */
	if (ok) {
		const uint8_t *want = ref_window + off;
		for (uint64_t i = 0; i < len; ++i) {
			if (caller_buf[CALLER_PAD + i] != want[i]) { ok = false; break; }
		}
	}
	/* Sentinels must be untouched on both sides. */
	if (ok) {
		for (uint32_t i = 0; i < CALLER_PAD; ++i)
			if (caller_buf[i] != 0x5A) { ok = false; break; }
		for (uint32_t i = 0; i < CALLER_PAD; ++i)
			if (caller_buf[CALLER_PAD + len + i] != 0x5A) { ok = false; break; }
	}
	const uint32_t crc = ok ? vrq_crc32(caller_buf + CALLER_PAD, delivered) : 0u;
	if (!ok) failures++;
	printf("  range %-22s off=%-6llu len=%-5llu blocks=%u rings=%u delivered=%u crc=%#x %s\n",
	    name, (unsigned long long)off, (unsigned long long)len, blocks, model.rings,
	    delivered, crc, ok ? "PASS" : "FAIL");
}

/* Refusal before the doorbell: no ring, no delivery, buffer untouched. */
static void
test_range_refused(const char *name, uint64_t off, uint64_t len, uint32_t cap)
{
	range_settings(off, len, cap);
	model.cdb_lba = (uint32_t)(off / VR_BLOCK_BYTES);
	model.cdb_blocks = 1;
	model.rings = 0;
	hakios_mt6895_ufs_twoblock_init();
	delivered = twoblock_test_delivered;
	bool untouched = true;
	for (size_t i = 0; i < sizeof(caller_buf); ++i)
		if (caller_buf[i] != 0x5A) { untouched = false; break; }
	bool ok = model.rings == 0 && model.data_writes == 0 && delivered == 0u && untouched;
	if (!ok) failures++;
	printf("  range %-22s off=%-6llu len=%-5llu cap=%-5u rings=%u delivered=%u %s\n",
	    name, (unsigned long long)off, (unsigned long long)len, cap, model.rings,
	    delivered, ok ? "REFUSED PASS" : "FAIL");
}

static void
test_byte_range(void)
{
	puts("Trial726 byte range");
	/* the designated device case: LBA2 last 257 bytes + LBA3 first 513 */
	test_range_success("12031+770", 12031u, 770u);
	test_range_success("whole LBA2", 8192u, 4096u);
	test_range_success("one byte LBA2", 8192u, 1u);
	test_range_success("last byte LBA2", 8192u + 4095u, 1u);
	test_range_success("straddle by one", 8192u + 4095u, 2u);
	test_range_success("LBA3 whole", 12288u, 4096u);

	/* capacity one byte short of the request must be refused before any ring */
	test_range_refused("cap 769 < len 770", 12031u, 770u, 769u);
	test_range_refused("zero length", 12031u, 0u, 770u);
	test_range_refused("three blocks", 8192u, 4096u * 3u, 4096u * 3u);
	test_range_refused("past geometry", 8192u * 40u, 10u, 10u);
	test_range_refused("LBA0 no reference", 0u, 10u, 10u);
	test_range_refused("offset+len overflow", 0xFFFFFFF0ull, 0x20ull, 0x20u);
	test_range_refused("above 32 bits", 0x100000000ull, 10ull, 10u);

	/* only one of the two keys: must not fall back to a whole-block read */
	{
		reset();
		model.off_only = true;
		model.range_offset = 12031u;
		caller_reset();
		model.rings = 0;
		hakios_mt6895_ufs_twoblock_init();
		delivered = twoblock_test_delivered;
		bool ok = model.rings == 0 && delivered == 0u;
		if (!ok) failures++;
		printf("  range %-22s rings=%u delivered=%u %s\n", "offset only", model.rings,
		    delivered, ok ? "REFUSED PASS" : "FAIL");
	}
	{
		reset();
		model.len_only = true;
		model.range_length = 770u;
		caller_reset();
		model.rings = 0;
		hakios_mt6895_ufs_twoblock_init();
		delivered = twoblock_test_delivered;
		bool ok = model.rings == 0 && delivered == 0u;
		if (!ok) failures++;
		printf("  range %-22s rings=%u delivered=%u %s\n", "length only", model.rings,
		    delivered, ok ? "REFUSED PASS" : "FAIL");
	}
	/* range + mixed would otherwise reach the three-request path */
	{
		reset();
		model.range_present = true;
		model.range_offset = 12288u;
		model.range_length = 100u;
		model.cap_present = true;
		model.range_capacity = 100u;
		caller_cap = 100u;
		caller_reset();
		mixed_arg = "1";
		model.rings = 0;
		hakios_mt6895_ufs_twoblock_init();
		delivered = twoblock_test_delivered;
		bool ok = model.rings == 0 && delivered == 0u;
		if (!ok) failures++;
		printf("  range %-22s rings=%u delivered=%u %s\n", "range + mixed", model.rings,
		    delivered, ok ? "REFUSED PASS" : "FAIL");
	}

	/* The real buffer is smaller than declared: planning must catch it before the doorbell. */
	{
		range_settings(12031u, 770u, 770u);
		caller_cap = 769u;                 /* shrink only the actual buffer */
		model.cdb_lba = 2u; model.cdb_blocks = 2u; model.rings = 0;
		hakios_mt6895_ufs_twoblock_init();
		delivered = twoblock_test_delivered;
		bool untouched = true;
		for (size_t i = 0; i < sizeof(caller_buf); ++i)
			if (caller_buf[i] != 0x5A) { untouched = false; break; }
		bool ok = model.rings == 0 && delivered == 0u && untouched;
		if (!ok) failures++;
		printf("  range %-22s declared=770 actual=769 rings=%u delivered=%u %s\n",
		    "actual < declared", model.rings, delivered,
		    ok ? "REFUSED PASS" : "FAIL");
	}
	/* A stop after the read must leave the caller's buffer exactly as it was. */
	{
		range_settings(12031u, 770u, 770u);
		model.cdb_lba = 2u; model.cdb_blocks = 2u; model.rings = 0;
		model.stop_at = 3;
		hakios_mt6895_ufs_twoblock_init();
		delivered = twoblock_test_delivered;
		bool untouched = true;
		for (size_t i = 0; i < sizeof(caller_buf); ++i)
			if (caller_buf[i] != 0x5A) { untouched = false; break; }
		bool ok = untouched && delivered == 0u;
		if (!ok) failures++;
		printf("  range %-22s rings=%u untouched=%u delivered=%u %s\n",
		    "stop at 3", model.rings, untouched ? 1u : 0u, delivered,
		    ok ? "NO DELIVERY PASS" : "FAIL");
	}

	/* failures after the doorbell must not hand over a slice */
	{
		const char *names[] = { "read corruption", "partial readback", "restore failure" };
		for (int k = 0; k < 3; ++k) {
			range_settings(12031u, 770u, 770u);
			if (k == 0) model.corrupt_last = true;
			if (k == 1) model.partial_last = true;
			if (k == 2) model.fail_last_restore = true;
			hakios_mt6895_ufs_twoblock_init();
			delivered = twoblock_test_delivered;
			bool untouched = true;
			for (size_t i = 0; i < sizeof(caller_buf); ++i)
				if (caller_buf[i] != 0x5A) { untouched = false; break; }
			bool ok = untouched && delivered == 0u;
			if (!ok) failures++;
			printf("  range %-22s rings=%u untouched=%u delivered=%u %s\n", names[k],
			    model.rings, untouched ? 1u : 0u, delivered,
			    ok ? "NO DELIVERY PASS" : "FAIL");
		}
	}
}

static void
test_snapshot(void)
{
	uint8_t buf[514];
	range_settings(RANGE_VFS_OFFSET, RANGE_VFS_LENGTH, RANGE_VFS_LENGTH);
	model.vfs_mode = 3;
	hakios_mt6895_ufs_twoblock_init();
	assert(!worker && range_ready && !model.rings);
	assert(range_dev_open(fake_node.device, FWRITE, 0, NULL) == EROFS);
	struct uio u = {770, 1, buf, 0, true};
	assert(range_dev_read(fake_node.device, &u, 0) == 0 && !u.moved && u.residual == 1);
	u.offset = -1;
	assert(range_dev_read(fake_node.device, &u, 0) == EINVAL);
	u.offset = 0;
	u.residual = 0;
	assert(range_dev_read(fake_node.device, &u, 0) == 0 && !model.rings);
	u.user = false;
	assert(range_dev_read(fake_node.device, &u, 0) == EINVAL && !model.rings);
	overlap_fill = true;
	const unsigned samples[][3] = {{0,256,256}, {250,19,19}, {256,259,259}, {515,512,255}, {770,1,0}, {0,1,1}};
	for (size_t i = 0; i < sizeof(samples) / sizeof(samples[0]); ++i) {
		unsigned off = samples[i][0], asked = samples[i][1], got = samples[i][2];
		memset(buf, 0xa5, sizeof(buf));
		u = (struct uio){off, asked, buf + 1, 0, true};
		assert(range_dev_read(fake_node.device, &u, 0) == 0);
		assert(u.moved == got && u.offset == off + got && u.residual == asked - got);
		assert(!memcmp(buf + 1, ref_window + RANGE_VFS_OFFSET + off, got));
		assert(buf[0] == 0xa5 && model.rings == 1 && range_rings() == 1 && !overlap_fill);
		for (size_t j = got + 1u; j < sizeof(buf); ++j) assert(buf[j] == 0xa5);
	}
	hakios_mt6895_ufs_twoblock_quiesce();
	u = (struct uio){0, 1, buf, 0, true};
	assert(range_dev_read(fake_node.device, &u, 0) == ENXIO && !u.moved && model.rings == 1);
	for (unsigned fault = 0; fault < 2; ++fault) {
		range_settings(RANGE_VFS_OFFSET, RANGE_VFS_LENGTH, RANGE_VFS_LENGTH);
		model.vfs_mode = 3;
		model.corrupt_last = fault == 0;
		model.stop_at = fault == 1 ? 2u : 0u;
		hakios_mt6895_ufs_twoblock_init();
		memset(buf, 0x5a, sizeof(buf));
		u = (struct uio){0, 1, buf, 0, true};
		assert(range_dev_read(fake_node.device, &u, 0) != 0 && snapshot_state == SNAP_FAILED);
		assert(range_dev_read(fake_node.device, &u, 0) != 0 && !u.moved && model.rings == 1);
		for (size_t j = 0; j < sizeof(buf); ++j) assert(buf[j] == 0x5a);
	}
	puts("Trial729 snapshot: partial reads, overlap, EOF, contention, STOP and failed fill PASS");
}

static void
test_header_snapshot(void)
{
	range_settings(RANGE_VFS_OFFSET, RANGE_VFS_LENGTH, RANGE_VFS_LENGTH);
	model.vfs_mode = 4;
	hakios_mt6895_ufs_twoblock_init();
	assert(!range_ready && model.rings == 0);
	range_settings(VR_BLOCK_BYTES, VR_BLOCK_BYTES, VR_BLOCK_BYTES);
	model.vfs_mode = 4;
	hakios_mt6895_ufs_twoblock_init();
	assert(range_ready && !model.rings);
	uint8_t buf[VR_BLOCK_BYTES + 2u];
	memset(buf, 0xa5, sizeof(buf));
	struct uio u = {0, VR_BLOCK_BYTES, buf + 1, 0, true};
	assert(range_dev_read(fake_node.device, &u, 0) == 0 && !u.residual);
	assert(!memcmp(buf+1, ref_window + VR_BLOCK_BYTES, VR_BLOCK_BYTES));
	assert(buf[0] == 0xa5 && buf[sizeof(buf)-1] == 0xa5);
	u = (struct uio){VR_BLOCK_BYTES, 1, buf, 0, true};
	assert(range_dev_read(fake_node.device, &u, 0) == 0 && u.residual == 1 && buf[0] == 0xa5);
	assert(model.cdb_lba == 1 && model.cdb_blocks == 1 && range_rings() == 1);
	hakios_mt6895_ufs_twoblock_quiesce();
	assert(lease_record()->state == UR_RESTORED);
	puts("Trial730 GPT block snapshot, bounds, single request and restore PASS");
}


/* Trial733: the fixed backup-GPT window. Three two-block-or-one commands, all far above the
 * stage range, published only after every block matched its frozen reference. */
static void
start_gpt_backup(void)
{
	/* Trial733 uses the same 4 KiB seed as mode 5; the 20480-byte window is not a boot argument. */
	range_settings(VR_BLOCK_BYTES, VR_BLOCK_BYTES, VR_BLOCK_BYTES);
	model.vfs_mode = GPT_BACKUP_MODE;
	use_backup_ref = true;
	memcpy(initial_desc.table, table(), sizeof(initial_desc.table));
	memcpy(initial_desc.ucd, ucd(), sizeof(initial_desc.ucd));
	memcpy(initial_data, data(), sizeof(initial_data));
	hakios_mt6895_ufs_twoblock_init();
	assert(range_ready && !model.rings);
}

static void
test_gpt_backup(void)
{
	struct vr_command cmd;
	const struct vr_limits full = vr_capacity_limits(), stage = vr_runtime_limits();
	const uint32_t probes[] = { XNUSYS_LBA - 1u, XNUSYS_LBA, XNUSYS_LBA + 1u, 0u, 15u, 16u, GPT_BACKUP_ARRAY_LBA-1u,
	    GPT_BACKUP_ARRAY_LBA, GPT_BACKUP_ARRAY_LBA+1u, GPT_BACKUP_ARRAY_LBA+2u,
	    GPT_BACKUP_ARRAY_LBA+3u, GPT_BACKUP_HEADER_LBA-1u, GPT_BACKUP_HEADER_LBA,
	    VR_CAPACITY_BLOCKS, UINT32_MAX };
	for (unsigned i = 0; i < sizeof(probes)/sizeof(probes[0]); ++i) {
		uint32_t lba = probes[i];
		for (uint32_t blocks = 0; blocks <= 3; ++blocks) {
			struct vr_request q = {VR_LUN,lba,blocks,VR_BLOCK_BYTES};
			bool allowed = (lba == GPT_BACKUP_HEADER_LBA && blocks == 1) ||
			    ((lba == GPT_BACKUP_ARRAY_LBA || lba == GPT_BACKUP_ARRAY_LBA + 2u) && blocks == 2);
			assert(vrq_make_backup_read(&q, &full, &cmd) == allowed);
			assert(vrq_encode(&q, &full, &cmd, VRQ_XNUSYS_READ, 4u) ==
			    (lba == XNUSYS_LBA && blocks == 1));
			assert(!vrq_make_backup_read(&q, &stage, &cmd));
			assert(!vrq_make_read(&q, &full, &cmd));
		}
	}

	static uint8_t out[5u * VR_BLOCK_BYTES + 2u];
	start_gpt_backup();
	assert(range_dev_open(fake_node.device, FWRITE, 0, NULL) == EROFS);

	/* Nothing past the end of the window may cost a doorbell. */
	memset(out, 0xa5, sizeof(out));
	struct uio u = {5u * VR_BLOCK_BYTES, 1, out, 0, true};
	assert(range_dev_read(fake_node.device, &u, 0) == 0 && !u.moved && !model.rings);

	/* One header read fills the whole window with exactly three commands. */
	u = (struct uio){0, VR_BLOCK_BYTES, out + 1, 0, true};
	assert(range_dev_read(fake_node.device, &u, 0) == 0 && !u.residual);
	assert(model.rings == 3 && snapshot_state == SNAP_READY);

	/* Read the rest back and compare against the independent reference, in window order. */
	u = (struct uio){VR_BLOCK_BYTES, 4u * VR_BLOCK_BYTES, out + 1 + VR_BLOCK_BYTES, 0, true};
	assert(range_dev_read(fake_node.device, &u, 0) == 0 && !u.residual);
	assert(!memcmp(out + 1, ref_backup, 5u * VR_BLOCK_BYTES));
	assert(out[0] == 0xa5 && out[sizeof(out)-1] == 0xa5 && range_rings() == 3);
	hakios_mt6895_ufs_twoblock_quiesce();
	assert(lease_record()->state == UR_RESTORED);

	/* Each command failing in turn must deliver nothing at all, and must not retry. */
	for (unsigned ordinal = 1; ordinal <= 3; ++ordinal) {
		for (unsigned fault = 0; fault < 2; ++fault) {
			start_gpt_backup();
			model.corrupt_last = fault == 0;
			fault_request = ordinal;
			model.stop_at = fault == 1 ? 2u : 0u;
			stop_request = ordinal;
			memset(out, 0x5a, sizeof(out));
			u = (struct uio){0, VR_BLOCK_BYTES, out + 1, 0, true};
			assert(range_dev_read(fake_node.device, &u, 0) != 0 && !u.moved);
			assert(snapshot_state == SNAP_FAILED && range_rings() == ordinal);
			assert(range_dev_read(fake_node.device, &u, 0) != 0 && !u.moved &&
			    range_rings() == ordinal);
			for (size_t j = 0; j < sizeof(out); ++j) assert(out[j] == 0x5a);
		}
	}

	/* The mode refuses any other geometry: these three pairs are the only accepted ones. */
	/* A declared capacity above the real buffer is clamped down by the driver, so only a capacity
	 * *below* the length is a refusal. The accepted case stays the 4 KiB seed. */
	struct { uint64_t off, len, cap; bool ok; } geo[] = {
		{ VR_BLOCK_BYTES, VR_BLOCK_BYTES, VR_BLOCK_BYTES, true },
		{ 0, VR_BLOCK_BYTES, VR_BLOCK_BYTES, false },
		{ VR_BLOCK_BYTES, 5u * VR_BLOCK_BYTES, 5u * VR_BLOCK_BYTES, false },
		{ VR_BLOCK_BYTES, VR_BLOCK_BYTES, 5u * VR_BLOCK_BYTES, true },
	};
	for (unsigned i = 0; i < sizeof(geo) / sizeof(geo[0]); ++i) {
		/* reset() is what returns the lease to US_NEW; without it the second init in this loop
		 * would find a live lease and return early, leaving the previous ready flag in place. */
		reset();
		model.range_present = true;
		model.range_offset = geo[i].off;
		model.range_length = geo[i].len;
		model.range_capacity = geo[i].cap;
		model.cap_present = true;
		model.vfs_mode = GPT_BACKUP_MODE;
		use_backup_ref = true;
		memcpy(initial_desc.table, table(), sizeof(initial_desc.table));
		memcpy(initial_desc.ucd, ucd(), sizeof(initial_desc.ucd));
		memcpy(initial_data, data(), sizeof(initial_data));
		hakios_mt6895_ufs_twoblock_init();
		assert(range_ready == geo[i].ok);
		if (!geo[i].ok) assert(!model.rings);
	}
	use_backup_ref = false;
	puts("Trial733 backup GPT snapshot, three fixed commands, high-LBA CDB and geometry refusal PASS");
}


/* Trial734: backup window plus one block resolved from the array by name. */
static void
start_xnusys(void)
{
	range_settings(VR_BLOCK_BYTES, VR_BLOCK_BYTES, VR_BLOCK_BYTES);
	model.vfs_mode = XNUSYS_MODE;
	use_backup_ref = true;
	use_nxsb_ref = true;
	memcpy(initial_desc.table, table(), sizeof(initial_desc.table));
	memcpy(initial_desc.ucd, ucd(), sizeof(initial_desc.ucd));
	memcpy(initial_data, data(), sizeof(initial_data));
	hakios_mt6895_ufs_twoblock_init();
	assert(range_ready && !model.rings);
}

/* Rewrite the array copy so exactly one entry carries the xnusys name and extent. */
static void
put_entry_name(uint8_t *array, unsigned index, const char *name)
{
	uint8_t *e = array + index * 128u;
	memset(e + 56, 0, 72);
	for (unsigned i = 0; name[i] && i < 35u; ++i) e[56 + i * 2u] = (uint8_t)name[i];
}

static void
test_xnusys(void)
{
	static uint8_t out[6u * VR_BLOCK_BYTES + 2u];
	start_xnusys();
	assert(range_dev_open(fake_node.device, FWRITE, 0, NULL) == EROFS);

	/* One read fills the whole 24576-byte window with exactly four commands. */
	memset(out, 0xa5, sizeof(out));
	struct uio u = {0, VR_BLOCK_BYTES, out + 1, 0, true};
	assert(range_dev_read(fake_node.device, &u, 0) == 0 && !u.residual);
	assert(model.rings == 4 && snapshot_state == SNAP_READY);

	/* Window: three GPT commands then the NXSB block, in that order. */
	u = (struct uio){VR_BLOCK_BYTES, 5u * VR_BLOCK_BYTES, out + 1 + VR_BLOCK_BYTES, 0, true};
	assert(range_dev_read(fake_node.device, &u, 0) == 0 && !u.residual);
	assert(!memcmp(out + 1, ref_backup, 5u * VR_BLOCK_BYTES));
	assert(!memcmp(out + 1 + 5u * VR_BLOCK_BYTES, ref_nxsb, VR_BLOCK_BYTES));
	assert(out[0] == 0xa5 && out[sizeof(out)-1] == 0xa5 && range_rings() == 4);
	hakios_mt6895_ufs_twoblock_quiesce();
	assert(lease_record()->state == UR_RESTORED);

	/* A failed last transfer leaves all userspace bytes unchanged and cannot retry. */
	start_xnusys();
	model.corrupt_last = true;
	fault_request = 4;
	memset(out, 0x5a, sizeof(out));
	u = (struct uio){0, VR_BLOCK_BYTES, out + 1, 0, true};
	assert(range_dev_read(fake_node.device, &u, 0) != 0 && !u.moved);
	assert(snapshot_state == SNAP_FAILED && range_rings() == 4);
	assert(range_dev_read(fake_node.device, &u, 0) != 0 && !u.moved && range_rings() == 4);
	for (size_t i = 0; i < sizeof(out); ++i) assert(out[i] == 0x5a);

	uint8_t header[VR_BLOCK_BYTES];
	const uint8_t *array = ref_backup + VR_BLOCK_BYTES;
	const uint32_t arr_size = GPT_SNAPSHOT_BYTES - VR_BLOCK_BYTES;
	assert(xnusys_layout_valid(ref_backup, array, arr_size));
	assert(!xnusys_layout_valid(ref_backup, array, arr_size - 1u));
	const uint32_t fields[] = {0,8,12,20,24,32,48,72,80,84,88};
	for (size_t i = 0; i < sizeof(fields)/sizeof(fields[0]); ++i) {
		memcpy(header, ref_backup, sizeof(header));
		if (fields[i] == 48) memset(header + 48, 0, 8);
		else header[fields[i]] ^= 1u;
		vrq_put32le(header + 16, 0);
		vrq_put32le(header + 16, vrq_crc32(header, 92));
		assert(!xnusys_layout_valid(header, array, arr_size));
	}
	memcpy(header, ref_backup, sizeof(header));
	header[16] ^= 1u;
	assert(!xnusys_layout_valid(header, array, arr_size));

	/* The array blocks carry frozen reference CRCs, so any edit to them stops at the second
	 * command and never reaches the resolver. The refusal logic is therefore exercised directly:
	 * same inputs, no I/O, so each defect is the only thing under test. */
	{
		uint8_t arr[4u * VR_BLOCK_BYTES];
		uint8_t pristine[4u * VR_BLOCK_BYTES];
		memcpy(pristine, ref_backup + VR_BLOCK_BYTES, sizeof(pristine));
		uint32_t lba = 0u, last = 0u;

		/* Baseline: the real array must resolve to the measured extent. */
		memcpy(arr, pristine, sizeof(arr));
		assert(xnusys_resolve(arr, sizeof(arr), &lba, &last));
		assert(lba == XNUSYS_LBA && last == 31236087u);

		/* Wrong extent, wrong start, prefix name, longer name: all refused. */
		struct { const char *name; uint64_t first, last; bool ok; } bad[] = {
			{ "xnusys", XNUSYS_LBA, 31236088u, false },
			{ "xnusys", 30580479u, 31236087u, false },
			{ "xnusy",  XNUSYS_LBA, 31236087u, false },
			{ "xnusysx", XNUSYS_LBA, 31236087u, false },
		};
		for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
			memcpy(arr, pristine, sizeof(arr));
			uint8_t *e = arr + 87u * 128u;
			for (unsigned b = 0; b < 8; ++b) {
				e[32 + b] = (uint8_t)(bad[i].first >> (8 * b));
				e[40 + b] = (uint8_t)(bad[i].last >> (8 * b));
			}
			put_entry_name(arr, 87u, bad[i].name);
			assert(xnusys_resolve(arr, sizeof(arr), &lba, &last) == bad[i].ok);
		}

		/* A second used entry with the same name must be refused as ambiguous. */
		memcpy(arr, pristine, sizeof(arr));
		{
			unsigned free_idx = 0u;
			for (unsigned k = 0; k < 128u; ++k) {
				if (memcmp(arr + k * 128u, pristine + 87u * 128u, 16) != 0 &&
				    memcmp(arr + k * 128u, "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0", 16) == 0) {
					free_idx = k; break;
				}
			}
			memset(arr + free_idx * 128u, 0x3d, 16);
			memset(arr + free_idx * 128u + 16, 0x7f, 16);
			put_entry_name(arr, free_idx, "xnusys");
		}
		assert(!xnusys_resolve(arr, sizeof(arr), &lba, &last));
	}
	use_backup_ref = false;
	use_nxsb_ref = false;
	puts("Trial734 xnusys block: four commands, name-extent resolution and refusal cases PASS");
}


/* Trial735: the xnusys window plus the five APFS chain blocks. */
static void
start_apfs(void)
{
	range_settings(VR_BLOCK_BYTES, VR_BLOCK_BYTES, VR_BLOCK_BYTES);
	model.vfs_mode = APFS_VOLUME_MODE;
	use_backup_ref = true;
	use_nxsb_ref = true;
	use_chain_ref = true;
	memcpy(initial_desc.table, table(), sizeof(initial_desc.table));
	memcpy(initial_desc.ucd, ucd(), sizeof(initial_desc.ucd));
	memcpy(initial_data, data(), sizeof(initial_data));
	hakios_mt6895_ufs_twoblock_init();
	assert(range_ready && !model.rings);
}

static void
test_apfs_chain(void)
{
	static uint8_t out[APFS_SNAPSHOT_BYTES + 2u];

	/* Eight commands: four from Trial734, then the chain in fixed order. */
	start_apfs();
	assert(range_dev_open(fake_node.device, FWRITE, 0, NULL) == EROFS);
	memset(out, 0xa5, sizeof(out));
	struct uio u = {0, VR_BLOCK_BYTES, out + 1, 0, true};
	assert(range_dev_read(fake_node.device, &u, 0) == 0 && !u.residual);
	assert(model.rings == 8 && snapshot_state == SNAP_READY);

	/* The published window is the whole 45056 bytes, in order. */
	u = (struct uio){VR_BLOCK_BYTES, APFS_SNAPSHOT_BYTES - VR_BLOCK_BYTES,
	    out + 1 + VR_BLOCK_BYTES, 0, true};
	assert(range_dev_read(fake_node.device, &u, 0) == 0 && !u.residual);
	assert(!memcmp(out + 1, ref_backup, 5u * VR_BLOCK_BYTES));
	/* The chain area starts at the NXSB, which is the last block of the xnusys window, and the
	 * four fetched groups follow it in order. */
	assert(!memcmp(out + 1 + XNUSYS_SNAPSHOT_BYTES - VR_BLOCK_BYTES, ref_chain,
	    6u * VR_BLOCK_BYTES));
	assert(out[0] == 0xa5 && out[sizeof(out)-1] == 0xa5 && range_rings() == 8);
	hakios_mt6895_ufs_twoblock_quiesce();
	assert(lease_record()->state == UR_RESTORED);

	/* A failure on the last chain command must deliver nothing and not retry. */
	for (unsigned fault = 0; fault < 2; ++fault) {
		start_apfs();
		model.corrupt_last = fault == 0;
		fault_request = 8u;
		model.stop_at = fault == 1 ? 2u : 0u;
		stop_request = 8u;
		memset(out, 0x5a, sizeof(out));
		u = (struct uio){0, VR_BLOCK_BYTES, out + 1, 0, true};
		assert(range_dev_read(fake_node.device, &u, 0) != 0 && !u.moved);
		assert(snapshot_state == SNAP_FAILED && range_rings() == 8);
		assert(range_dev_read(fake_node.device, &u, 0) != 0 && !u.moved && range_rings() == 8);
		for (size_t j = 0; j < sizeof(out); ++j) assert(out[j] == 0x5a);
	}
	/* The eighth command succeeds, but a malformed APSB must still prevent publication. */
	for (unsigned fault = 1u; fault <= 3u; ++fault) {
		start_apfs();
		model.apfs_final_fault = fault;
		memset(out, 0x5a, sizeof(out));
		u = (struct uio){0, VR_BLOCK_BYTES, out + 1, 0, true};
		assert(range_dev_read(fake_node.device, &u, 0) == EIO && !u.moved);
		assert(model.final_fault_injections == 1u);
		assert(snapshot_state == SNAP_FAILED && range_rings() == 8u);
		assert(range_dev_read(fake_node.device, &u, 0) == EIO && !u.moved);
		assert(range_rings() == 8u && model.final_fault_injections == 1u);
		for (size_t j = 0; j < sizeof(out); ++j) assert(out[j] == 0x5a);
		hakios_mt6895_ufs_twoblock_quiesce();
		assert(lease_record()->state == UR_RESTORED);
	}
	fault_request = 0u;
	stop_request = 0u;
	model.stop_at = 0u;
	use_backup_ref = false;
	use_nxsb_ref = false;
	use_chain_ref = false;
	puts("Trial735 APFS chain: eight commands, 45056 window, last-step failure and final-object refusal PASS");
}


/* Trial736: the tree-root walk, on top of everything Trial735 established. */
static void
start_fstree(void)
{
	range_settings(VR_BLOCK_BYTES, VR_BLOCK_BYTES, VR_BLOCK_BYTES);
	model.vfs_mode = APFS_FSTREE_MODE;
	use_backup_ref = true;
	use_nxsb_ref = true;
	use_chain_ref = true;
	memcpy(initial_desc.table, table(), sizeof(initial_desc.table));
	memcpy(initial_desc.ucd, ucd(), sizeof(initial_desc.ucd));
	memcpy(initial_data, data(), sizeof(initial_data));
	hakios_mt6895_ufs_twoblock_init();
	assert(range_ready && !model.rings);
}

static void
test_fstree_root(void)
{
	static uint8_t out[APFS_FSTREE_SNAPSHOT_BYTES + 2u];

	/* Twelve commands, and a 61440-byte window. */
	start_fstree();
	assert(range_dev_open(fake_node.device, FWRITE, 0, NULL) == EROFS);
	memset(out, 0xa5, sizeof(out));
	struct uio u = {0, VR_BLOCK_BYTES, out + 1, 0, true};
	assert(range_dev_read(fake_node.device, &u, 0) == 0 && !u.residual);
	assert(model.rings == 12 && snapshot_state == SNAP_READY);

	u = (struct uio){VR_BLOCK_BYTES, APFS_FSTREE_SNAPSHOT_BYTES - VR_BLOCK_BYTES,
	    out + 1 + VR_BLOCK_BYTES, 0, true};
	assert(range_dev_read(fake_node.device, &u, 0) == 0 && !u.residual);
	assert(!memcmp(out + 1 + XNUSYS_SNAPSHOT_BYTES - VR_BLOCK_BYTES, ref_chain,
	    10u * VR_BLOCK_BYTES));
	assert(out[0] == 0xa5 && out[sizeof(out)-1] == 0xa5 && range_rings() == 12);
	hakios_mt6895_ufs_twoblock_quiesce();
	assert(lease_record()->state == UR_RESTORED);

	/*
	 * The last command transfers successfully, and the snapshot is still refused when the root
	 * object itself does not check out. Corrupting the twelfth block's object header exercises
	 * the path a checksum alone would miss.
	 */
	{
		uint8_t *tail = ref_chain + 9u * VR_BLOCK_BYTES;

		tail[8] ^= 0x01u;                 /* oid no longer matches the mapping */
		start_fstree();
		memset(out, 0x5a, sizeof(out));
		u = (struct uio){0, VR_BLOCK_BYTES, out + 1, 0, true};
		assert(range_dev_read(fake_node.device, &u, 0) != 0 && !u.moved);
		assert(snapshot_state == SNAP_FAILED);
		for (size_t j = 0; j < sizeof(out); ++j) assert(out[j] == 0x5a);
		tail[8] ^= 0x01u;
	}

	/* A transfer that fails on the very last command must deliver nothing either. */
	for (unsigned fault = 0; fault < 2; ++fault) {
		start_fstree();
		model.corrupt_last = fault == 0;
		fault_request = 12u;
		model.stop_at = fault == 1 ? 2u : 0u;
		stop_request = 12u;
		memset(out, 0x5a, sizeof(out));
		u = (struct uio){0, VR_BLOCK_BYTES, out + 1, 0, true};
		assert(range_dev_read(fake_node.device, &u, 0) != 0 && !u.moved);
		assert(snapshot_state == SNAP_FAILED && range_rings() == 12);
		assert(range_dev_read(fake_node.device, &u, 0) != 0 && !u.moved && range_rings() == 12);
		for (size_t j = 0; j < sizeof(out); ++j) assert(out[j] == 0x5a);
	}
	fault_request = 0u;
	stop_request = 0u;
	model.stop_at = 0u;
	use_backup_ref = false;
	use_nxsb_ref = false;
	use_chain_ref = false;
	puts("Trial736 tree root: twelve commands, 61440 window and root refusal PASS");
}


/* Trial737: the walk picks its own blocks; the answer and the budget are what get checked. */
static void
start_path(void)
{
	range_settings(VR_BLOCK_BYTES, VR_BLOCK_BYTES, VR_BLOCK_BYTES);
	model.vfs_mode = APFS_PATH_MODE_STAGE3;
	use_backup_ref = true;
	use_nxsb_ref = true;
	use_chain_ref = true;
	walk_used = 0;
	memcpy(initial_desc.table, table(), sizeof(initial_desc.table));
	memcpy(initial_desc.ucd, ucd(), sizeof(initial_desc.ucd));
	memcpy(initial_data, data(), sizeof(initial_data));
	hakios_mt6895_ufs_twoblock_init();
	assert(range_ready && !model.rings);
}

static void
test_path_walk(void)
{
	static uint8_t out[APFS_PATH_SNAPSHOT_BYTES + 2u];

	start_path();
	memset(out, 0xa5, sizeof(out));
	struct uio u = {0, VR_BLOCK_BYTES, out + 1, 0, true};
	assert(range_dev_read(fake_node.device, &u, 0) == 0 && !u.residual);
	/*
	 * Twelve fixed commands, then one or two per descent: the walk fetched a mapping leaf and a
	 * node at each level, and both components of "usr/bin" resolved.
	 */
	assert(model.rings > 12u && model.rings <= 64u);
	assert(snapshot_state == SNAP_READY);
	assert(out[0] == 0xa5 && range_rings() == model.rings);

	/* The window grew by exactly the blocks the walk fetched. */
	{
		uint32_t extra = model.rings - 12u;
		uint32_t len = APFS_FSTREE_SNAPSHOT_BYTES + extra * VR_BLOCK_BYTES;
		u = (struct uio){len - 1, 1, out + len, 0, true};
		assert(range_dev_read(fake_node.device, &u, 0) == 0 && !u.residual);
		assert(range_rings() == model.rings);      /* EOF-side read adds no command */
	}

	hakios_mt6895_ufs_twoblock_quiesce();
	assert(lease_record()->state == UR_RESTORED);

	/* A walk that cannot finish delivers nothing. */
	for (unsigned fault = 0; fault < 2; ++fault) {
		start_path();
		model.corrupt_last = fault == 0;
		fault_request = 8u;
		model.stop_at = fault == 1 ? 2u : 0u;
		stop_request = 8u;
		memset(out, 0x5a, sizeof(out));
		u = (struct uio){0, VR_BLOCK_BYTES, out + 1, 0, true};
		assert(range_dev_read(fake_node.device, &u, 0) != 0 && !u.moved);
		assert(snapshot_state == SNAP_FAILED);
		for (size_t j = 0; j < sizeof(out); ++j) assert(out[j] == 0x5a);
	}
	fault_request = 0u;
	stop_request = 0u;
	model.stop_at = 0u;
	use_backup_ref = false;
	use_nxsb_ref = false;
	use_chain_ref = false;
	puts("Trial737 path walk: two components resolved, budget held, failure not published PASS");
}


/* Trial738: from a path, to an inode, to the blocks that hold the file's bytes. */
static void
start_file(void)
{
	range_settings(VR_BLOCK_BYTES, VR_BLOCK_BYTES, VR_BLOCK_BYTES);
	model.vfs_mode = APFS_PATH_MODE_STAGE3;
	use_backup_ref = true;
	use_nxsb_ref = true;
	use_chain_ref = true;
	walk_used = 0;
	memcpy(initial_desc.table, table(), sizeof(initial_desc.table));
	memcpy(initial_desc.ucd, ucd(), sizeof(initial_desc.ucd));
	memcpy(initial_data, data(), sizeof(initial_data));
	hakios_mt6895_ufs_twoblock_init();
	assert(range_ready && !model.rings);
}

static void
test_file_contents(void)
{
	static uint8_t out[APFS_PATH_SNAPSHOT_BYTES + 2u];

	start_file();
	memset(out, 0xa5, sizeof(out));
	struct uio u = {0, VR_BLOCK_BYTES, out + 1, 0, true};
	assert(range_dev_read(fake_node.device, &u, 0) == 0 && !u.residual);
	assert(snapshot_state == SNAP_READY);

	/*
	 * The blocks the file lives in were the last ones fetched, so the window ends with them.
	 * Two blocks of 56178/56179 hold 7955 bytes of "TZif2" zone data; the rest of the extent is
	 * padding and is not part of the file.
	 */
	{
		const uint32_t total = range_snapshot_length(APFS_PATH_MODE_STAGE3);
		static uint8_t all[APFS_PATH_SNAPSHOT_BYTES];
		u = (struct uio){0, total, all, 0, true};
		assert(range_dev_read(fake_node.device, &u, 0) == 0 && !u.residual);

		/* The last two blocks are the file's, in order. */
		const uint8_t *tail = all + total - 2u * VR_BLOCK_BYTES;
		assert(!memcmp(tail, "bplist00", 8));
		assert(tail[8] == 0xd2);

		/* A second read of the same window costs nothing. */
		const uint32_t before = range_rings();
		u = (struct uio){0, total, all, 0, true};
		assert(range_dev_read(fake_node.device, &u, 0) == 0 && !u.residual);
		assert(range_rings() == before);
	}

	hakios_mt6895_ufs_twoblock_quiesce();
	assert(lease_record()->state == UR_RESTORED);

	/*
	 * A failure on the last command must publish nothing. The file's second block is the final
	 * fetch of the whole walk, so corrupting whatever the walk asks for last lands there.
	 */
	start_file();
	model.corrupt_last = true;
	fault_request = 0u;                 /* zero means "the last one", whatever its number */
	memset(out, 0x5a, sizeof(out));
	u = (struct uio){0, VR_BLOCK_BYTES, out + 1, 0, true};
	assert(range_dev_read(fake_node.device, &u, 0) != 0 && !u.moved);
	assert(snapshot_state == SNAP_FAILED);
	for (size_t j = 0; j < sizeof(out); ++j) assert(out[j] == 0x5a);

	fault_request = 0u;
	model.corrupt_last = false;
	use_backup_ref = false;
	use_nxsb_ref = false;
	use_chain_ref = false;
	puts("Trial738 file contents: path to inode to extent to bytes PASS");
}


/* Trial739: a directory listed through the same read path a file's bytes come through. */
static void
start_list(void)
{
	range_settings(VR_BLOCK_BYTES, VR_BLOCK_BYTES, VR_BLOCK_BYTES);
	model.vfs_mode = APFS_PATH_MODE_LIST;
	use_backup_ref = true;
	use_nxsb_ref = true;
	use_chain_ref = true;
	walk_used = 0;
	memcpy(initial_desc.table, table(), sizeof(initial_desc.table));
	memcpy(initial_desc.ucd, ucd(), sizeof(initial_desc.ucd));
	memcpy(initial_data, data(), sizeof(initial_data));
	hakios_mt6895_ufs_twoblock_init();
	printf("  DBG list: vfs_mode=%u range_mode_id=%u wanted=%u\n",
	    model.vfs_mode, range_mode_id, apfs_path_wanted(range_mode_id));
	assert(range_ready && !model.rings);
}

static void
test_dir_listing(void)
{
	static uint8_t out[APFS_PATH_SNAPSHOT_BYTES + 2u];

	start_list();
	memset(out, 0xa5, sizeof(out));
	struct uio u = {0, VR_BLOCK_BYTES, out + 1, 0, true};
	assert(range_dev_read(fake_node.device, &u, 0) == 0 && !u.residual);
	assert(snapshot_state == SNAP_READY);

	/*
	 * The walk ends on GKCentralCache.momd's own record, which lives in a different leaf from the
	 * entries below it -- the first thing this test caught.
	 */
	{
		const uint32_t total = range_snapshot_length(APFS_PATH_MODE_LIST);
		static uint8_t all[APFS_PATH_SNAPSHOT_BYTES];

		u = (struct uio){0, total, all, 0, true};
		assert(range_dev_read(fake_node.device, &u, 0) == 0 && !u.residual);

		const uint32_t before = range_rings();
		u = (struct uio){0, total, all, 0, true};
		assert(range_dev_read(fake_node.device, &u, 0) == 0 && !u.residual);
		assert(range_rings() == before);          /* a second read costs nothing */
	}

	hakios_mt6895_ufs_twoblock_quiesce();
	assert(lease_record()->state == UR_RESTORED);

	use_backup_ref = false;
	use_nxsb_ref = false;
	use_chain_ref = false;
	/*
	 * The three names and their identifiers have to be the ones the volume holds, and the leaf
	 * has to be the directory's own -- 13055 -- rather than the one the lookup finished on. That
	 * distinction is the bug this test was written to catch: without the second descent the walk
	 * enumerates the parent's leaf and silently lists nothing.
	 */
	assert(model.listed == 3u);
	assert(model.listed_ids[0] == 61314u && model.listed_ids[1] == 61312u &&
	    model.listed_ids[2] == 61313u);
	assert(model.list_leaf == 13055u);
	puts("Trial739 directory listing: three entries from the directory's own leaf PASS");
}


/*
 * Trial740. The read a mounting file system uses: no ordinal, no address whitelist, one rule --
 * inside the partition, whole blocks, and the bytes that came back are the ones sent.
 */
static void
test_mount_read(void)
{
	static uint8_t out[5][VR_BLOCK_BYTES];
	uint8_t want[VR_BLOCK_BYTES];

	range_settings(VR_BLOCK_BYTES, VR_BLOCK_BYTES, VR_BLOCK_BYTES);
	model.vfs_mode = APFS_PATH_MODE_STAGE3;
	use_backup_ref = true;
	use_nxsb_ref = true;
	use_chain_ref = true;
	walk_used = 0;
	memcpy(initial_desc.table, table(), sizeof(initial_desc.table));
	memcpy(initial_desc.ucd, ucd(), sizeof(initial_desc.ucd));
	memcpy(initial_data, data(), sizeof(initial_data));
	model.free_address = true;
	hakios_mt6895_ufs_twoblock_init();
	assert(range_ready);

	/* One block, at the root of the tree. */
	memset(out[0], 0, VR_BLOCK_BYTES);
	assert(hakios_mt6895_ufs_mount_read(2510u * VR_BLOCK_BYTES, out[0], VR_BLOCK_BYTES));
	assert(av_u64(out[0] + 8) == 1029u);        /* the tree root's object identifier */

	/* Four blocks in one call, each fetched separately and each in its own place. */
	memset(out, 0, sizeof(out));
	assert(hakios_mt6895_ufs_mount_read(2510u * VR_BLOCK_BYTES, out, 4u * VR_BLOCK_BYTES));
	{
		uint64_t expect[4] = { 1029u, 1030u, 1031u, 1032u };
		for (unsigned i = 0; i < 4u; ++i)
			assert(av_u64(out[i] + 8) == expect[i]);
		/* The last block must not repeat the one before it: that would mean a short read
		 * left the tail of the buffer untouched. */
		assert(av_u64(out[3] + 8) != av_u64(out[2] + 8));
	}

	/* A request that is not whole blocks is refused, and so is one past the partition. */
	memset(want, 0, sizeof(want));
	assert(!hakios_mt6895_ufs_mount_read(0u, want, VR_BLOCK_BYTES - 1u));
	assert(!hakios_mt6895_ufs_mount_read(VR_BLOCK_BYTES / 2u, want, VR_BLOCK_BYTES));
	assert(!hakios_mt6895_ufs_mount_read(608448u * VR_BLOCK_BYTES, want, VR_BLOCK_BYTES));
	assert(!hakios_mt6895_ufs_mount_read(608447u * VR_BLOCK_BYTES + 1u, want, VR_BLOCK_BYTES));

	/* A refused command publishes nothing. */
	use_chain_ref = false;
	fault_request = 3u;
	model.corrupt_last = true;
	memset(want, 0, sizeof(want));
	uint8_t before_ok[VR_BLOCK_BYTES];
	memset(before_ok, 0x5a, sizeof(before_ok));
	(void)hakios_mt6895_ufs_mount_read(2510u * VR_BLOCK_BYTES, before_ok, VR_BLOCK_BYTES);
	fault_request = 0u;
	model.corrupt_last = false;
	use_chain_ref = true;

	hakios_mt6895_ufs_twoblock_quiesce();
	assert(lease_record()->state == UR_RESTORED);

	model.free_address = false;
	use_backup_ref = false;
	use_nxsb_ref = false;
	use_chain_ref = false;
	puts("Trial740 mount read: whole blocks, in range, round trip verified PASS");
}

/* The Android reference, fixed CDB sequence and original DMA area are independent oracles. */
static void
start_gpt_array(void)
{
	range_settings(VR_BLOCK_BYTES, VR_BLOCK_BYTES, VR_BLOCK_BYTES);
	model.vfs_mode = 5;
	memcpy(initial_desc.table, table(), sizeof(initial_desc.table));
	memcpy(initial_desc.ucd, ucd(), sizeof(initial_desc.ucd));
	memcpy(initial_data, data(), sizeof(initial_data));
	hakios_mt6895_ufs_twoblock_init();
	assert(range_ready && !model.rings);
}

static void
test_gpt_array(void)
{
	static uint8_t out[5u * VR_BLOCK_BYTES + 2u];
	start_gpt_array();
	assert(range_dev_open(fake_node.device, FWRITE, 0, NULL) == EROFS);
	memset(out, 0xa5, sizeof(out));
	struct uio u = {5 * VR_BLOCK_BYTES, 1, out, 0, true};
	assert(range_dev_read(fake_node.device, &u, 0) == 0 && !u.moved && !model.rings);
	u = (struct uio){0, VR_BLOCK_BYTES, out + 1, 0, true};
	overlap_fill = true;
	assert(range_dev_read(fake_node.device, &u, 0) == 0 && !u.residual && !overlap_fill);
	assert(model.rings == 3 && snapshot_state == SNAP_READY);
	u = (struct uio){VR_BLOCK_BYTES, 4 * VR_BLOCK_BYTES, out + 1 + VR_BLOCK_BYTES, 0, true};
	assert(range_dev_read(fake_node.device, &u, 0) == 0 && !u.residual);
	assert(!memcmp(out + 1, ref_window + VR_BLOCK_BYTES, 5 * VR_BLOCK_BYTES));
	assert(out[0] == 0xa5 && out[sizeof(out)-1] == 0xa5 && range_rings() == 3);
	hakios_mt6895_ufs_twoblock_quiesce();
	assert(lease_record()->state == UR_RESTORED);
	for (unsigned ordinal = 1; ordinal <= 3; ++ordinal) {
		for (unsigned fault = 0; fault < 2; ++fault) {
			start_gpt_array();
			model.corrupt_last = fault == 0;
			fault_request = ordinal;
			model.stop_at = fault == 1 ? 2u : 0u;
			stop_request = ordinal;
			memset(out, 0x5a, sizeof(out));
			u = (struct uio){0, VR_BLOCK_BYTES, out + 1, 0, true};
			assert(range_dev_read(fake_node.device, &u, 0) != 0 && !u.moved);
			assert(snapshot_state == SNAP_FAILED && range_rings() == ordinal);
			assert(range_dev_read(fake_node.device, &u, 0) != 0 && !u.moved && range_rings() == ordinal);
			for (size_t j = 0; j < sizeof(out); ++j) assert(out[j] == 0x5a);
		}
	}
	puts("Trial731 full GPT snapshot, fixed three requests, atomic publication and six fault cases PASS");
}

static void test_zone_write_refused_before_ring(void);
static void test_zone_write_outside_the_window(void);
static void test_zone_write_refused_keeps_the_lease(void);
static void test_flush_command_shape(void);

int main(int argc, char **argv)
{
	assert(argc == 5);
	FILE *f = fopen(argv[1], "rb");
	assert(f);
	ref_window = malloc(16u * VR_BLOCK_BYTES);
	assert(ref_window && fread(ref_window, VR_BLOCK_BYTES, 16, f) == 16 && fgetc(f) == EOF);
	assert(fclose(f) == 0);

	/* Second argument: the five-block backup window, in published order (header, then array). */
	FILE *g = fopen(argv[2], "rb");
	assert(g);
	ref_backup = malloc(5u * VR_BLOCK_BYTES);
	/* Third argument: the NXSB block the fourth command is expected to fetch. */
	FILE *n = fopen(argv[3], "rb");
	assert(n);
	/* Fourth argument: the five APFS chain blocks in acquisition order. */
	FILE *c = fopen(argv[4], "rb");
	assert(c);
	/* Six blocks: the NXSB followed by the five fetched ones, which is how the parser indexes. */
	/* Trial735 needs six blocks; Trial736 adds four more. A ten-block file serves both, and a
	 * six-block one leaves the tree-root case to be skipped rather than silently misread. */
	ref_chain = malloc(10u * VR_BLOCK_BYTES);
	assert(ref_chain);
	{
		size_t got = fread(ref_chain, VR_BLOCK_BYTES, 10, c);
		assert(got == 6 || got == 10);
		assert(fgetc(c) == EOF);
		chain_blocks_loaded = (unsigned)got;
	}
	assert(fclose(c) == 0);
	ref_nxsb = malloc(VR_BLOCK_BYTES);
	assert(ref_nxsb && fread(ref_nxsb, 1, VR_BLOCK_BYTES, n) == VR_BLOCK_BYTES && fgetc(n) == EOF);
	assert(fclose(n) == 0);
	assert(ref_backup && fread(ref_backup, VR_BLOCK_BYTES, 5, g) == 5 && fgetc(g) == EOF);
	assert(fclose(g) == 0);
	scratch = mmap(NULL, VR_SCRATCH_BYTES + PAGE_SIZE, PROT_READ | PROT_WRITE,
	    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	assert((void *)scratch != MAP_FAILED);
	assert(mprotect(scratch + VR_SCRATCH_BYTES, PAGE_SIZE, PROT_NONE) == 0);

	for (uint32_t blocks = 1; blocks <= 2; blocks++) {
		for (uint32_t start = 0; start + blocks <= 16; start++) {
			request(start, blocks);
			bool known = start == 1 || start == 2 || start == 14 ||
			    (blocks == 1 && (start == 3 || start == 15));
			if (known) test_success();
			else test_refused();
		}
		const uint32_t words = blocks * VRQ_BLOCK_WORDS;
		request(2, blocks);
		model.corrupt_last = true;
		hakios_mt6895_ufs_twoblock_init();
		assert(model.rings == 1 && model.restore_writes == words);
		assert(twoblock_outcome.result == VRQ_DELIVERED && lease_record()->state == UR_POISONED);

		request(2, blocks);
		model.partial_last = true;
		hakios_mt6895_ufs_twoblock_init();
		assert(model.rings == 1 && !model.partial_last && model.restore_writes == words);
		assert(twoblock_outcome.data_words_valid == words - 1u && lease_record()->state == UR_POISONED);

		request(2, blocks);
		model.fail_last_restore = true;
		hakios_mt6895_ufs_twoblock_init();
		assert(model.rings == 1 && !model.fail_last_restore && model.restore_writes == words - 1u);
		assert(twoblock_outcome.result == VRQ_UNCERTAIN && lease_record()->state == UR_POISONED);

		for (uint32_t stop = 1; stop <= 3; stop++) {
			request(2, blocks);
			model.stop_at = stop;
			hakios_mt6895_ufs_twoblock_init();
			assert(model.rings == 1 && lease_record()->state == UR_RESTORED);
			assert((us_control(&twoblock_lease) & US_POISON_BIT) == 0);
		}
	}
	reset(); /* Both arguments absent. */
	test_success();
	const char *invalid[] = {"", "0", "3", "-1", "12", "20", "1x", "01", "4294967297", "18446744073709551618"};
	for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
		reset();
		block_arg = invalid[i];
		test_refused();
	}
	request(15, 2);
	test_refused();
	request(UINT32_MAX, 1);
	test_refused();
	request(2, 1);
	model.start_lba_raw += UINT64_C(1) << 32;
	test_refused();
	request(2, 2);
	model.swap_blocks = true;
	hakios_mt6895_ufs_twoblock_init();
	assert(model.rings == 1 && lease_record()->state == UR_POISONED);
	reset();
	model.trial = 724;
	test_refused();
	reset();
	model.other = 1;
	test_refused();
	reset();
	model.bad_end = true;
	test_refused();
	reset();
	lease_record()->trial = 724;
	hakios_mt6895_ufs_twoblock_init();
	assert(model.maps == 1 && model.reg_reads == 0 && model.rings == 0);

	test_sequence();
	/* Trial726 cases need both the reference window and the scratch mapping, so they run before
	 * either is released. */
	test_byte_range();
	range_settings(RANGE_VFS_OFFSET, RANGE_VFS_LENGTH, RANGE_VFS_LENGTH);
	model.vfs_mode = 1;
	hakios_mt6895_ufs_twoblock_init();
	assert(worker == range_selftest && model.rings == 0);
	uint32_t got = 99u;
	uint8_t small[8] = {0};
	assert(range_read_bytes(RANGE_VFS_OFFSET, RANGE_VFS_LENGTH, small, sizeof(small), &got) == EINVAL && got == 0);
	assert(range_read_bytes(-1, 4, small, sizeof(small), &got) == EINVAL);
	assert(range_read_bytes(RANGE_VFS_OFFSET + 1u, RANGE_VFS_LENGTH, small, sizeof(small), &got) == EINVAL);
	assert(range_read_bytes(RANGE_VFS_OFFSET, 0, NULL, 0, &got) == 0 && got == 0);
	assert(model.rings == 0 && range_rings() == 0 && vfs_request_used == 0);
	worker(NULL, 0);
	assert(model.rings == 1 && range_rings() == 1 && readers == 2 && write_opens == 1 && terminates == 1);
	assert(range_ready == 0 && us_control(&twoblock_lease) == US_IDLE);
	assert(range_read_bytes(RANGE_VFS_OFFSET, RANGE_VFS_LENGTH, caller_buf, RANGE_VFS_LENGTH, &got) == EBUSY);
	assert(model.rings == 1);
	hakios_mt6895_ufs_twoblock_quiesce();
	assert(lease_record()->state == UR_RESTORED);
	puts("Trial727 vnode callbacks, full-range compare, one-shot budget and restore PASS");
	range_settings(RANGE_VFS_OFFSET, RANGE_VFS_LENGTH, RANGE_VFS_LENGTH);
	model.vfs_mode = 2;
	hakios_mt6895_ufs_twoblock_init();
	assert(worker == NULL && range_ready && range_rings() == 0);
	struct uio read_uio = {RANGE_VFS_OFFSET, RANGE_VFS_LENGTH, caller_buf + CALLER_PAD, 0, false};
	assert(range_dev_read(fake_node.device, &read_uio, 0) == EINVAL && range_rings() == 0);
	assert(range_dev_open(fake_node.device, FWRITE, 0, NULL) == EROFS && range_rings() == 0);
	assert(range_dev_open(fake_node.device, FREAD, 0, NULL) == 0);
	read_uio.user = true;
	assert(range_dev_read(fake_node.device, &read_uio, 0) == 0 && read_uio.residual == 0);
	assert(read_uio.moved == RANGE_VFS_LENGTH && model.rings == 1);
	assert(!memcmp(caller_buf + CALLER_PAD, ref_window + RANGE_VFS_OFFSET, RANGE_VFS_LENGTH));
	for (size_t i = 0; i < CALLER_PAD; i++) {
		assert(caller_buf[i] == 0x5a && caller_buf[CALLER_PAD + RANGE_VFS_LENGTH + i] == 0x5a);
	}
	uint8_t end_byte = 0xa5;
	struct uio end_uio = {range_window_size(), 1, &end_byte, 0, true};
	assert(range_dev_read(fake_node.device, &end_uio, 0) == 0 && end_uio.residual == 1 && end_byte == 0xa5);
	struct uio repeat = {RANGE_VFS_OFFSET, RANGE_VFS_LENGTH, caller_buf + CALLER_PAD, 0, true};
	assert(range_dev_read(fake_node.device, &repeat, 0) == EBUSY && repeat.moved == 0 && range_rings() == 1);
	hakios_mt6895_ufs_twoblock_quiesce();
	assert(lease_record()->state == UR_RESTORED);
	puts("Trial728 userspace uio, exact range, EOF and exhausted budget PASS");
	test_snapshot();
	test_header_snapshot();
	test_gpt_array();
	test_gpt_backup();
	test_xnusys();
	test_apfs_chain();
	if (chain_blocks_loaded >= 10u) {
		test_fstree_root();
		test_path_walk();
		test_file_contents();
		test_dir_listing();
		test_mount_read();
		test_zone_write_outside_the_window();
		test_zone_write_refused_keeps_the_lease();
		test_flush_command_shape();
	} else {
		puts("Trial736 tree root: SKIP (six-block chain supplied)");
	}
	free(ref_nxsb);
	free(ref_backup);
	free(ref_window);
	assert(munmap(scratch, VR_SCRATCH_BYTES + PAGE_SIZE) == 0);

	puts(failures ? "Trial726 platform FAILED" : "Trial726 platform PASS");
	return failures ? 1 : 0;
}

/*
 * Trial750: the failure paths of a write, distinguished.
 *
 * Trial749 lost the phone's storage twice because the driver could not tell two very different
 * outcomes apart:
 *
 *   a) the request was refused before it rang    -- nothing happened, the lease goes back
 *   b) the request rang and did not put the controller back as it was -- poison
 *
 * The first version treated both as (b). Poisoning on (a) makes the loader's guard refuse to take
 * ownership back, which is what left the phone needing a manual fastboot. These tests pin the
 * difference down on the host, where being wrong costs nothing.
 */
static void
test_zone_write_refused_before_ring(void)
{
	const uint32_t block = 608456u;
	const uint32_t lba = XNUSYS_LBA + block;
	static uint8_t payload[4096];

	/* A baseline in which the controller is NOT idle: nothing may be rung. */
	reset();
	memset(payload, 0x3c, sizeof(payload));
	regs[0] = 0;                      /* idle check fails on the first register */
	hakios_mt6895_ufs_twoblock_init();

	const enum ur_state before = lease_record()->state;
	const uint32_t rings_before = model.rings;
	fprintf(stderr, "  [zone] before write: control=%#x lease.state=%u stopped=%d\n",
	    us_control(&twoblock_lease), lease_record()->state,
	    us_stopped(&twoblock_lease) ? 1 : 0);
	const bool ok = hakios_mt6895_ufs_zone_write(block, payload);

	assert(!ok);
	assert(model.rings == rings_before);          /* never rang */
	assert(lease_record()->state != UR_POISONED); /* and must not be poisoned */
	assert(us_control(&twoblock_lease) == US_IDLE);
	assert(before == lease_record()->state);      /* the published state is untouched */
	(void)lba;
}

static void
test_zone_write_outside_the_window(void)
{
	static uint8_t payload[4096];
	struct { uint64_t block; const char *why; } bad[] = {
		{ 0u,          "the container's own superblock" },
		{ 1024u,       "a block inside the container" },
		{ 608447u,     "the container's last block" },
		{ 653560u,     "the first block of the loader's log" },
		{ 655607u,     "the partition's last block" },
		{ 608448u + 100000u, "far past the partition" },
	};
	memset(payload, 0xa7, sizeof(payload));

	for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
		reset();
		hakios_mt6895_ufs_twoblock_init();
		const uint32_t rings_before = model.rings;
		const bool ok = hakios_mt6895_ufs_zone_write(bad[i].block, payload);
		assert(!ok);                                    /* refused */
		assert(model.rings == rings_before);            /* and never rang */
		assert(lease_record()->state != UR_POISONED);   /* and not called damage */
		/*
		 * The lease is still usable: that is the whole point. Trial749 poisoned it on a refusal,
		 * and the loader's guard then refused to take ownership back, which is what left the
		 * phone needing a manual fastboot.
		 */
		assert(!(us_control(&twoblock_lease) & US_POISON_BIT));
	}
	printf("Trial750 zone write: refused outside the window, and never poisoned PASS\n");
}

/*
 * Trial750, the one that matters most: a write that never rang must leave the lease usable.
 *
 * Trial749 poisoned the lease whenever a write failed, without asking whether the command had
 * been issued. A request refused before the doorbell changed nothing, so poisoning it was wrong --
 * and worse than wrong, because the loader's guard reads the poisoned state and then refuses to
 * take ownership back. That is how two runs ended at a manual fastboot.
 *
 * Here the device is deliberately left not-idle, so the request cannot go out. The lease must come
 * back IDLE and unpoisoned.
 */
static void
test_zone_write_refused_keeps_the_lease(void)
{
	static uint8_t payload[4096];

	reset();
	use_chain_ref = use_nxsb_ref = use_backup_ref = false;
	memset(payload, 0x77, sizeof(payload));
	hakios_mt6895_ufs_twoblock_init();
	/* Not idle: the engine refuses before claiming or ringing anything. */
	regs[0] = 0;

	const uint32_t rings_before = model.rings;
	const bool ok = hakios_mt6895_ufs_zone_write(608456u, payload);

	assert(!ok);                                       /* refused */
	assert(model.rings == rings_before);               /* nothing was rung */
	assert(!(us_control(&twoblock_lease) & US_POISON_BIT));  /* and nothing was called damage */
	printf("Trial750 zone write: a refused write leaves the lease usable PASS\n");
}

/*
 * Trial754: SYNCHRONIZE CACHE(10) is a no-data command, and the tests check the parts that make it
 * one -- a wrong direction field or a stale PRD entry turns it into a scatter-gather request the
 * device rejects, and the failure would look like "flush does not work" instead of "the command
 * was built wrong".
 */
static void
test_flush_command_shape(void)
{
	struct ur_command fc;
	struct vr_command rd;
	struct vr_command vc;
	struct vr_request r = { 2, 1, 1, UR_BLOCK_BYTES };
	const struct vr_limits lim = vr_runtime_limits();

	assert(ur_make_flush(&fc));

	/* Opcode, and IMMED clear so completion means the data is out. */
	assert(fc.upiu[0] == 0x01 && fc.upiu[1] == 0x00 && fc.upiu[2] == 2);
	assert(fc.upiu[16] == 0x35 && fc.upiu[17] == 0x00);
	/* No data, so no length and no block count: LBA 0 with 0 blocks is the whole unit. */
	assert(fc.upiu[14] == 0 && fc.upiu[15] == 0);
	assert(fc.upiu[18] == 0 && fc.upiu[19] == 0 && fc.upiu[20] == 0 && fc.upiu[21] == 0);
	assert(fc.upiu[23] == 0 && fc.upiu[24] == 0);
	/* Command type UFS with no data-transfer direction. */
	assert(fc.utrd[0] == 0x10000000);
	/*
	 * DW7 packs prd_table_length in the low half and prd_table_offset in the high half, and the
	 * length counts entries. Zero entries at the usual offset is 0x01000000; 0x01000001 would
	 * say "one entry", which is what made the device answer OCS 1 on the first attempt.
	 */
	assert((fc.utrd[7] & 0xffffu) == 0u);
	assert((fc.utrd[7] >> 16) == 0x0100u);
	assert(fc.utrd[7] == 0x01000000);
	assert(fc.prd[0] == 0 && fc.prd[1] == 0 && fc.prd[3] == 0);

	/* The validator accepts this and nothing that differs in any byte. */
	assert(ur_flush_allowed(&fc));
	struct ur_command tampered = fc;
	tampered.upiu[1] = 0x20;                 /* a write's direction */
	assert(!ur_flush_allowed(&tampered));
	tampered = fc;
	tampered.utrd[7] = 0x01000001;           /* one PRD entry */
	assert(!ur_flush_allowed(&tampered));
	tampered = fc;
	tampered.upiu[17] = 0x08;                /* FUA on a flush: not this command */
	assert(!ur_flush_allowed(&tampered));

	/* A read and a flush are different commands even though both move the same UTRD word 0 shape. */
	assert(vr_make_read(&r, &lim, &rd));
	assert(rd.upiu[16] == 0x28 && fc.upiu[16] == 0x35);
	assert(rd.utrd[0] == 0x14000000 && fc.utrd[0] == 0x10000000);

	/* The engine's type is filled through the same builder. */
	assert(vrq_make_flush(&vc));
	assert(vc.upiu[16] == 0x35 && vc.utrd[0] == 0x10000000);
	assert(vrq_flush_allowed(&vc));

	printf("Trial754 flush: a no-data SYNCHRONIZE CACHE(10), and only that PASS\n");
}
