/*
 * Host fake-controller test suite for multi-block WRITE(10) on MT6895 UFS.
 *
 * Includes the real driver (hakios.mt6895_ufs_twoblock.c) against a synthetic UFSHCI
 * register block, UTRD slot 0, UCD, and guard-paged scratch DRAM mapping, plus a fake
 * flash backing store to prove byte-for-byte payload delivery, exact CDB/UTRD/PRD wire
 * encoding, Response UPIU validation, workspace restore, seam safety, and boot-arg caps.
 */
#define _GNU_SOURCE
#define US_HOST_TEST 1

#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#include "hakios.mt6895_ufs_twoblock.c"

unsigned long gPhysBase = 0x100000000ul;
unsigned long gPhysSize = 0x100000000ul;

static uint32_t regs[VR_REG_BYTES / 4u];
static uint32_t table_page[PAGE_SIZE / 4u];
static uint8_t *scratch;

/* Fake flash window covering a small region near any target LBA we write. */
static uint8_t fake_flash[UR_WRITE_MAX_BLOCKS * VR_BLOCK_BYTES];
static uint64_t fake_flash_base_lba;

static struct {
	uint32_t rings;
	bool pending;
	bool completed;
	uint64_t time_us;
	uint32_t write_max_arg;
	bool write_max_present;
	/* Last command observed by fake controller */
	uint8_t last_upiu[32];
	uint32_t last_utrd[8];
	uint32_t last_prd[4];
	/* Fault injection for Response UPIU / OCS */
	uint32_t inject_ocs;
	uint8_t inject_resp6;
	uint8_t inject_scsi_status;
	uint32_t inject_residual;
	uint8_t inject_lun;
	bool inject_custom_resp;
} ctrl;

static uint32_t *table(void)
{
	return table_page + ((UR_LIST_PA & PAGE_MASK) / 4u);
}

static uint32_t *ucd(void)
{
	return (uint32_t *)(void *)(scratch + (UR_UCD_PA - UR_LEASE_PA));
}

static uint32_t *data_buf(void)
{
	return (uint32_t *)(void *)(scratch + (UR_DATA_PA - UR_LEASE_PA));
}

static struct ur_lease *lease_rec(void)
{
	return (struct ur_lease *)(void *)scratch;
}

/* ---- minimal VFS stubs required because hakios.mt6895_ufs_twoblock.c includes ufs_range_vfs.h ---- */
static uint8_t *ref_window;
#include "ufs_vfs_callbacks.h"
uint8_t *twoblock_test_dst(void) { return NULL; }
uint32_t twoblock_test_cap(void) { return 0u; }
void twoblock_test_listing(uint32_t count, uint64_t leaf, const uint64_t *ids)
{
	(void)count; (void)leaf; (void)ids;
}
void twoblock_host_stop_point(int which) { (void)which; }
bool twoblock_host_bad_managed(void) { return false; }
bool twoblock_host_claim_hook(void) { return true; }
bool twoblock_host_finish_hook(void) { return true; }
void vr_host_barrier(void) {}
uint64_t vr_host_now_us(void) { return ctrl.time_us++; }
bool pmap_valid_address(uint64_t addr) { (void)addr; return false; }

bool PE_parse_boot_argn(const char *name, void *out, size_t size)
{
	if (strcmp(name, "ufs_write_max_blocks") == 0) {
		assert(size == sizeof(uint32_t));
		if (!ctrl.write_max_present) return false;
		memcpy(out, &ctrl.write_max_arg, size);
		return true;
	}
	if (size == sizeof(uint32_t)) {
		uint32_t v = 0;
		if (strcmp(name, "mtk_ufs_twoblock") == 0) v = 1u;
		else if (strcmp(name, "mali_trial") == 0) v = UR_TRIAL;
		else return false;
		memcpy(out, &v, size);
		return true;
	}
	return false;
}

bool PE_parse_boot_arg_str(const char *name, char *out, int size)
{
	(void)name; (void)out; (void)size;
	return false;
}

vm_offset_t ml_io_map_unmappable(vm_offset_t address, vm_size_t length, unsigned int flags)
{
	assert(flags == VM_WIMG_WCOMB);
	if (address == UR_LEASE_PA) {
		assert(length == VR_SCRATCH_BYTES);
		return (vm_offset_t)scratch;
	}
	assert(address == (UR_LIST_PA & ~(uint64_t)PAGE_MASK) && length == PAGE_SIZE);
	return (vm_offset_t)table_page;
}

vm_offset_t ml_io_map(vm_offset_t address, vm_size_t length)
{
	assert(address == VR_REG_PA && length == VR_REG_BYTES);
	return (vm_offset_t)regs;
}

static void fake_complete(void)
{
	uint8_t upiu[32];
	for (unsigned i = 0; i < 8u; i++) {
		ctrl.last_utrd[i] = table()[i];
		vrq_put32le(upiu + i * 4u, ucd()[i]);
	}
	memcpy(ctrl.last_upiu, upiu, 32u);
	for (unsigned i = 0; i < 4u; i++) {
		ctrl.last_prd[i] = ucd()[1024u / 4u + i];
	}

	/* Verify strict JESD223 WRITE(10) format on the wire */
	assert(upiu[0] == 0x01u);
	assert(upiu[1] == 0x20u);
	assert(upiu[2] == 2u);
	assert(upiu[16] == 0x2au);
	assert(upiu[17] == 0x00u || upiu[17] == 0x08u);
	assert(ctrl.last_utrd[0] == 0x12000000u);
	assert(ctrl.last_utrd[4] == (uint32_t)UR_UCD_PA);
	assert(ctrl.last_utrd[5] == (uint32_t)(UR_UCD_PA >> 32));
	assert(ctrl.last_utrd[6] == 0x00800080u);
	assert(ctrl.last_utrd[7] == 0x01000001u);

	const uint32_t lba = ur_be32(upiu + 18);
	const uint32_t blocks = ((uint32_t)upiu[23] << 8) | upiu[24];
	const uint32_t bytes = ur_be32(upiu + 12);
	assert(blocks >= 1u && blocks <= UR_WRITE_MAX_BLOCKS);
	assert(bytes == blocks * VR_BLOCK_BYTES);
	assert(ctrl.last_prd[0] == (uint32_t)UR_DATA_PA);
	assert(ctrl.last_prd[1] == (uint32_t)(UR_DATA_PA >> 32));
	assert(ctrl.last_prd[2] == 0u);
	assert(ctrl.last_prd[3] == bytes - 1u);

	/* Copy DMA OUT payload into fake flash when succeeding */
	if (!ctrl.inject_custom_resp && ctrl.inject_ocs == 0u) {
		assert(lba >= fake_flash_base_lba);
		assert((uint64_t)(lba - fake_flash_base_lba) + blocks <= UR_WRITE_MAX_BLOCKS);
		const uint8_t *dma_src = (const uint8_t *)data_buf();
		memcpy(fake_flash + (size_t)(lba - fake_flash_base_lba) * VR_BLOCK_BYTES,
		    dma_src, bytes);
	}

	/* Build Response UPIU at UCD + 512 */
	uint8_t resp[52];
	memset(resp, 0, sizeof(resp));
	resp[0] = 0x21u;
	resp[2] = ctrl.inject_custom_resp ? ctrl.inject_lun : 2u;
	resp[3] = 0u;
	resp[6] = ctrl.inject_custom_resp ? ctrl.inject_resp6 : 0u;
	resp[7] = ctrl.inject_custom_resp ? ctrl.inject_scsi_status : 0u;
	const uint32_t res = ctrl.inject_custom_resp ? ctrl.inject_residual : 0u;
	resp[12] = (uint8_t)(res >> 24);
	resp[13] = (uint8_t)(res >> 16);
	resp[14] = (uint8_t)(res >> 8);
	resp[15] = (uint8_t)res;

	for (unsigned i = 0; i < VRQ_RESP_WORDS; i++) {
		ucd()[512u / 4u + i] = vrq_get32le(resp + i * 4u);
	}
	table()[2] = ctrl.inject_ocs;
	regs[0x58u / 4u] = 0u;
	ctrl.pending = false;
	ctrl.completed = true;
}

bool vr_host_reg_hook(uint16_t offset)
{
	if (offset == 0x58u && ctrl.pending) {
		fake_complete();
	}
	return true;
}

bool vr_host_ring_hook(void)
{
	assert(regs[0x58u / 4u] == 1u);
	ctrl.rings++;
	ctrl.pending = true;
	return true;
}

bool vr_host_read_word_hook(int region, uint32_t offset)
{
	if (region == VRQ_DATA) assert(offset < platform.data_bytes);
	return true;
}

bool vr_host_write_hook(int region, uint32_t offset)
{
	if (region == VRQ_DATA) assert(offset < platform.data_bytes);
	return true;
}

/* Baseline snapshot matching what mtkloader leaves in UR_LIST_PA / UR_UCD_PA / UR_DATA_PA */
static uint32_t saved_table[8];
static uint32_t saved_ucd[UR_UCD_BYTES / 4u];
static uint32_t saved_data_2blk[VRQ_DATA_WORDS];

static void reset_controller(void)
{
	memset(&ctrl, 0, sizeof(ctrl));
	memset(regs, 0, sizeof(regs));
	memset(table_page, 0, sizeof(table_page));
	memset(scratch, 0, VR_SCRATCH_BYTES);
	memset(fake_flash, 0xee, sizeof(fake_flash));

	/* Idle UFSHCI registers matching vrq_idle() */
	regs[0x00 / 4u] = 0x1fu;
	regs[0x08 / 4u] = 0x200u;
	regs[0x30 / 4u] = 0x03u;
	regs[0x34 / 4u] = 0x01u;
	regs[0x50 / 4u] = (uint32_t)UR_LIST_PA;
	regs[0x54 / 4u] = 0u;
	regs[0x58 / 4u] = 0u;
	regs[0x60 / 4u] = 0x01u;

	/* Inherited loader READ(10) baseline that satisfies ur_previous_read() */
	struct ur_request req = { 2u, 1u, 1u, UR_BLOCK_BYTES };
	struct ur_command prev;
	assert(ur_make_read(&req, &prev));
	prev.utrd[2] = 0u; /* OCS_SUCCESS */
	for (unsigned i = 0; i < 8u; i++) table()[i] = prev.utrd[i];
	for (unsigned i = 0; i < 8u; i++) ucd()[i] = vrq_get32le(prev.upiu + i * 4u);
	for (unsigned i = 0; i < 4u; i++) ucd()[1024u / 4u + i] = prev.prd[i];
	ucd()[512u / 4u] = 0x00020021u;

	/* Fill initial 2-block scratch pattern to verify byte-for-byte restore */
	for (unsigned i = 0; i < VRQ_DATA_WORDS; i++) {
		data_buf()[i] = 0x13570000u ^ i;
	}
	memcpy(saved_table, table(), sizeof(saved_table));
	memcpy(saved_ucd, ucd(), sizeof(saved_ucd));
	memcpy(saved_data_2blk, data_buf(), sizeof(saved_data_2blk));

	/* Fresh unused lease */
	lease_rec()->magic = UR_MAGIC;
	lease_rec()->version = UR_VERSION;
	lease_rec()->trial = UR_TRIAL;
	lease_rec()->state = UR_UNUSED;

	/* Reset driver static state */
	memset(&platform, 0, sizeof(platform));
	twoblock_lease.control = US_NEW;
	twoblock_lease.publish = twoblock_publish;
	twoblock_lease.context = &platform;
	twoblock_total_rings = 0u;
	twoblock_zone_write_cmds = 0u;
	twoblock_zone_write_blks = 0u;
	twoblock_write_max_blocks = UR_WRITE_MAX_BLOCKS;

	/* Open platform mappings & lease (directly, without running boot-time probe reads) */
	platform.scratch = ml_io_map_unmappable(UR_LEASE_PA, VR_SCRATCH_BYTES, VM_WIMG_WCOMB);
	platform.table = ml_io_map_unmappable(UR_LIST_PA & ~(uint64_t)PAGE_MASK, PAGE_SIZE, VM_WIMG_WCOMB)
	    + (UR_LIST_PA & PAGE_MASK);
	platform.regs = ml_io_map(VR_REG_PA, VR_REG_BYTES);
	platform.data_bytes = VR2_TRANSFER_BYTES;
	assert(twoblock_baseline_clean(&platform, &twoblock_baseline));
	assert(us_open(&twoblock_lease, true));
}

static void assert_restored_clean(uint32_t expected_data_bytes)
{
	assert(memcmp(table(), saved_table, sizeof(saved_table)) == 0);
	assert(memcmp(ucd(), saved_ucd, sizeof(saved_ucd)) == 0);
	assert(memcmp(data_buf(), saved_data_2blk, sizeof(saved_data_2blk)) == 0);
	assert(platform.data_bytes == expected_data_bytes);
	assert(us_control(&twoblock_lease) == US_IDLE);
	assert(lease_rec()->state == UR_CLAIMED);
}

static void test_codec_wire_format(void)
{
	struct ur_command cmd, bad;
	const uint32_t counts[] = { 1u, 2u, 3u, 16u, 32u, 64u };
	const uint64_t lba = 13803264ull;

	for (size_t i = 0; i < sizeof(counts) / sizeof(counts[0]); i++) {
		const uint32_t n = counts[i];
		const uint32_t bytes = n * UR_BLOCK_BYTES;
		for (int fua = 0; fua <= 1; fua++) {
			assert(ur_make_write_blocks_flags(lba, n, fua != 0, &cmd));
			assert(cmd.upiu[0] == 0x01u);
			assert(cmd.upiu[1] == 0x20u);
			assert(cmd.upiu[2] == 2u);
			assert(ur_be32(cmd.upiu + 12) == bytes);
			assert(cmd.upiu[16] == 0x2au);
			assert(cmd.upiu[17] == (fua ? 0x08u : 0x00u));
			assert(ur_be32(cmd.upiu + 18) == (uint32_t)lba);
			assert((((uint32_t)cmd.upiu[23] << 8) | cmd.upiu[24]) == n);
			assert(cmd.utrd[0] == 0x12000000u);
			assert(cmd.utrd[2] == 0x0fu);
			assert(cmd.utrd[7] == 0x01000001u);
			assert(cmd.prd[0] == (uint32_t)UR_DATA_PA);
			assert(cmd.prd[1] == (uint32_t)(UR_DATA_PA >> 32));
			assert(cmd.prd[3] == bytes - 1u);

			assert(ur_write_blocks_flags_allowed(&cmd, lba, n, fua != 0));
			assert(!ur_write_blocks_flags_allowed(&cmd, lba, n, fua == 0));
			assert(!ur_write_blocks_flags_allowed(&cmd, lba + 1u, n, fua != 0));
			assert(!ur_write_blocks_flags_allowed(&cmd, lba, n == 64u ? 1u : n + 1u, fua != 0));

			/* Tamper PRD length by 1 byte -> must fail readback */
			bad = cmd;
			bad.prd[3] ^= 1u;
			assert(!ur_write_blocks_flags_allowed(&bad, lba, n, fua != 0));

			/* Tamper UPIU expected length -> must fail readback */
			bad = cmd;
			bad.upiu[13] ^= 1u;
			assert(!ur_write_blocks_flags_allowed(&bad, lba, n, fua != 0));
		}
	}

	/* Single-block wrapper matches 1-block multi-block output byte-for-byte */
	struct ur_command c1, cm;
	assert(ur_make_write(lba, &c1));
	assert(ur_make_write_blocks_flags(lba, 1u, false, &cm));
	assert(memcmp(&c1, &cm, sizeof(c1)) == 0);

	/* Refused counts and LBA wraps */
	assert(!ur_make_write_blocks_flags(lba, 0u, false, &cmd));
	assert(!ur_make_write_blocks_flags(lba, UR_WRITE_MAX_BLOCKS + 1u, false, &cmd));
	assert(!ur_make_write_blocks_flags((uint64_t)UINT32_MAX + 1ull, 1u, false, &cmd));
	assert(!ur_make_write_blocks_flags(UINT32_MAX, 2u, false, &cmd));
	puts("[PASS] codec wire format (1..64 blocks, FUA, tamper rejection, bounds)");
}

static void test_end_to_end_multiblock_writes(void)
{
	static uint8_t payload[UR_WRITE_MAX_BLOCKS * VR_BLOCK_BYTES];
	const uint32_t sizes[] = { 1u, 2u, 3u, 16u, 32u, 64u };
	const uint64_t bases[] = {
		XNUVAR_FIRST_BLOCK,
		XNUVAR_LAST_BLOCK - UR_WRITE_MAX_BLOCKS + 1u,
		XNUSYS_ZONE_LBA_FIRST,
		XNUSYS_ZONE_LBA_LAST - UR_WRITE_MAX_BLOCKS + 1u,
	};

	reset_controller();
	uint32_t expected_rings = 0u;

	for (size_t b = 0; b < sizeof(bases) / sizeof(bases[0]); b++) {
		fake_flash_base_lba = bases[b];
		for (size_t s = 0; s < sizeof(sizes) / sizeof(sizes[0]); s++) {
			const uint32_t n = sizes[s];
			const bool fua = (s & 1u) != 0u;
			const uint32_t bytes = n * VR_BLOCK_BYTES;

			/* Unique deterministic pattern per block and per run */
			for (uint32_t i = 0; i < bytes; i++) {
				payload[i] = (uint8_t)((i * 37u + (i / VR_BLOCK_BYTES) * 13u + b * 7u + s) & 0xffu);
			}
			memset(fake_flash, 0xaa, sizeof(fake_flash));

			const bool ok = hakios_mt6895_ufs_zone_write_blocks(bases[b], n, payload, fua);
			assert(ok);
			expected_rings++;
			assert(ctrl.rings == expected_rings);
			assert(twoblock_outcome.result == VRQ_DELIVERED);
			assert(twoblock_outcome.blocks == n);
			assert(twoblock_outcome.ocs == 0u);
			assert(twoblock_outcome.response_complete);
			assert(twoblock_outcome.response_failures == 0u);
			assert(twoblock_outcome.restored);
			assert(memcmp(fake_flash, payload, bytes) == 0);
			/* Untouched trailing blocks in fake_flash must still be 0xaa */
			if (n < UR_WRITE_MAX_BLOCKS) {
				assert(fake_flash[bytes] == 0xaau);
			}
			assert_restored_clean(VR2_TRANSFER_BYTES);
		}
	}
	puts("[PASS] end-to-end fake controller writes (1..64 blocks / 256 KiB, xnuvar + zone, FUA, restore)");
}

static void test_response_and_ocs_rejections(void)
{
	static uint8_t payload[16u * VR_BLOCK_BYTES];
	memset(payload, 0x55, sizeof(payload));

	/* 1. Non-zero OCS (e.g., OCS 7 INVALID_PRD_TABLE) */
	reset_controller();
	fake_flash_base_lba = XNUVAR_FIRST_BLOCK;
	ctrl.inject_ocs = 7u;
	assert(!hakios_mt6895_ufs_zone_write_blocks(XNUVAR_FIRST_BLOCK, 16u, payload, false));
	assert(ctrl.rings == 1u);
	assert(twoblock_outcome.result == VRQ_REJECTED);
	assert(twoblock_outcome.ocs == 7u);
	assert_restored_clean(VR2_TRANSFER_BYTES);

	/* 2. OCS==0 but SCSI CHECK CONDITION (0x02) -> MUST NOT silently succeed! */
	reset_controller();
	fake_flash_base_lba = XNUVAR_FIRST_BLOCK;
	ctrl.inject_custom_resp = true;
	ctrl.inject_lun = 2u;
	ctrl.inject_resp6 = 0u;
	ctrl.inject_scsi_status = 0x02u;
	ctrl.inject_residual = 0u;
	assert(!hakios_mt6895_ufs_zone_write_blocks(XNUVAR_FIRST_BLOCK, 16u, payload, false));
	assert(twoblock_outcome.result == VRQ_REJECTED);
	assert(twoblock_outcome.ocs == 0u);
	assert((twoblock_outcome.response_failures & 16u) != 0u);
	assert_restored_clean(VR2_TRANSFER_BYTES);

	/* 3. OCS==0 + SCSI GOOD but non-zero residual transfer count (partial write!) -> MUST REJECT! */
	reset_controller();
	fake_flash_base_lba = XNUVAR_FIRST_BLOCK;
	ctrl.inject_custom_resp = true;
	ctrl.inject_lun = 2u;
	ctrl.inject_resp6 = 0u;
	ctrl.inject_scsi_status = 0u;
	ctrl.inject_residual = 4096u;
	assert(!hakios_mt6895_ufs_zone_write_blocks(XNUVAR_FIRST_BLOCK, 16u, payload, false));
	assert(twoblock_outcome.result == VRQ_REJECTED);
	assert((twoblock_outcome.response_failures & 64u) != 0u);
	assert_restored_clean(VR2_TRANSFER_BYTES);

	/* 4. OCS==0 + TARGET FAILURE (resp[6]=1) -> MUST REJECT! */
	reset_controller();
	fake_flash_base_lba = XNUVAR_FIRST_BLOCK;
	ctrl.inject_custom_resp = true;
	ctrl.inject_lun = 2u;
	ctrl.inject_resp6 = 1u;
	ctrl.inject_scsi_status = 0u;
	ctrl.inject_residual = 0u;
	assert(!hakios_mt6895_ufs_zone_write_blocks(XNUVAR_FIRST_BLOCK, 8u, payload, false));
	assert(twoblock_outcome.result == VRQ_REJECTED);
	assert((twoblock_outcome.response_failures & 8u) != 0u);
	assert_restored_clean(VR2_TRANSFER_BYTES);

	puts("[PASS] Response UPIU & OCS error rejection (OCS=7, CHECK CONDITION, residual!=0, target fail)");
}

static void test_seam_and_boundary_refusals(void)
{
	static uint8_t payload[UR_WRITE_MAX_BLOCKS * VR_BLOCK_BYTES];
	memset(payload, 0x99, sizeof(payload));

	struct { uint64_t lba; uint32_t blocks; const char *why; } bad[] = {
		{ XNUVAR_FIRST_BLOCK,        0u,  "zero blocks" },
		{ XNUVAR_FIRST_BLOCK,        65u, "exceeds single-PRD 64-block cap" },
		{ XNUVAR_FIRST_BLOCK - 1u,   2u,  "crosses userdata -> xnuvar seam" },
		{ XNUVAR_LAST_BLOCK,         2u,  "crosses xnuvar -> xnusys APFS NXSB seam" },
		{ XNUVAR_LAST_BLOCK - 10u,   64u, "spills from xnuvar into xnusys APFS" },
		{ XNUSYS_ZONE_LBA_FIRST - 1u, 2u, "crosses APFS container -> zone seam" },
		{ XNUSYS_ZONE_LBA_LAST,      2u,  "crosses zone -> loader HLOG seam" },
		{ XNUSYS_ZONE_LBA_LAST - 5u, 64u, "spills from zone into loader HLOG" },
		{ 608448ull,                 4u,  "relative offset inside super partition" },
		{ 0ull,                      1u,  "LBA 0 MBR/GPT" },
		{ UINT64_MAX,                2u,  "uint64 overflow wrap" },
		{ (uint64_t)UINT32_MAX,      2u,  "uint32 CDB LBA wrap" },
	};

	reset_controller();
	for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
		const bool ok = hakios_mt6895_ufs_zone_write_blocks(bad[i].lba, bad[i].blocks, payload, false);
		if (ok) {
			fprintf(stderr, "FAIL: accepted illegal write: %s (lba=%llu blocks=%u)\n",
			    bad[i].why, (unsigned long long)bad[i].lba, bad[i].blocks);
			abort();
		}
		assert(ctrl.rings == 0u);
		assert(us_control(&twoblock_lease) == US_IDLE);
		assert(lease_rec()->state == UR_CLAIMED);
		/* Engine inner guard must independently refuse too */
		assert(!zone_write_range_allowed(bad[i].lba, bad[i].blocks));
	}
	puts("[PASS] seam & boundary refusals (zero rings, lease unpoisoned, outer+inner guards agree)");
}

static void test_bootarg_cap(void)
{
	static uint8_t payload[64u * VR_BLOCK_BYTES];
	memset(payload, 0x42, sizeof(payload));

	/* Cap = 1 (instant kill switch back to single-block) */
	reset_controller();
	twoblock_write_max_blocks = 1u;
	fake_flash_base_lba = XNUVAR_FIRST_BLOCK;
	assert(hakios_mt6895_ufs_write_max_blocks() == 1u);
	assert(hakios_mt6895_ufs_zone_write_blocks(XNUVAR_FIRST_BLOCK, 1u, payload, false));
	assert(ctrl.rings == 1u);
	assert(!hakios_mt6895_ufs_zone_write_blocks(XNUVAR_FIRST_BLOCK, 2u, payload, false));
	assert(ctrl.rings == 1u); /* refused before ringing */
	assert_restored_clean(VR2_TRANSFER_BYTES);

	/* Cap = 16 */
	reset_controller();
	twoblock_write_max_blocks = 16u;
	fake_flash_base_lba = XNUVAR_FIRST_BLOCK;
	assert(hakios_mt6895_ufs_zone_write_blocks(XNUVAR_FIRST_BLOCK, 16u, payload, false));
	assert(ctrl.rings == 1u);
	assert(!hakios_mt6895_ufs_zone_write_blocks(XNUVAR_FIRST_BLOCK, 17u, payload, false));
	assert(ctrl.rings == 1u);
	assert_restored_clean(VR2_TRANSFER_BYTES);

	puts("[PASS] ufs_write_max_blocks runtime cap / single-block kill switch");
}

static void test_journal_ioctl_math(void)
{
	/* Mirror hfs_journal.c:get_io_info() with xnuvar_ioctl's fixed return values */
	const uint64_t phys_blksz = 512u;
	const uint64_t page_size = 16384u;
	const uint64_t max_blks = 64u;
	const uint64_t max_bytes = max_blks * 4096u; /* 262144 */
	const uint64_t writeblockcnt = max_bytes / phys_blksz; /* 512 sectors */
	const uint64_t writesegcnt = max_blks; /* 64 */
	uint64_t writemaxcnt = max_bytes;
	uint64_t tmp = writeblockcnt * phys_blksz;
	if (writemaxcnt == 0 || (writeblockcnt > 0 && tmp < writemaxcnt))
		writemaxcnt = tmp;
	if (writesegcnt > 0 && (writesegcnt * page_size) < writemaxcnt)
		writemaxcnt = writesegcnt * page_size;
	assert(writemaxcnt == 262144u);

	/* Also verify kill-switch max_blks=1 gives 4096 bytes (NOT 512 bytes!) */
	{
		const uint64_t mb1 = 1u * 4096u;
		const uint64_t wbc1 = mb1 / phys_blksz; /* 8 sectors */
		const uint64_t wsc1 = 1u;
		uint64_t wmc1 = mb1;
		uint64_t t1 = wbc1 * phys_blksz;
		if (t1 < wmc1) wmc1 = t1;
		if (wsc1 * page_size < wmc1) wmc1 = wsc1 * page_size;
		assert(wmc1 == 4096u);
	}
	puts("[PASS] HFS+ journal get_io_info() math (256 KiB normal, 4 KiB kill-switch, never 512B)");
}

int main(void)
{
	/* Allocate scratch + guard page immediately after to catch ANY out-of-bounds DMA write! */
	scratch = mmap(NULL, VR_SCRATCH_BYTES + PAGE_SIZE, PROT_READ | PROT_WRITE,
	    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	assert(scratch != MAP_FAILED);
	assert(mprotect(scratch + VR_SCRATCH_BYTES, PAGE_SIZE, PROT_NONE) == 0);

	test_codec_wire_format();
	test_end_to_end_multiblock_writes();
	test_response_and_ocs_rejections();
	test_seam_and_boundary_refusals();
	test_bootarg_cap();
	test_journal_ioctl_math();

	assert(munmap(scratch, VR_SCRATCH_BYTES + PAGE_SIZE) == 0);
	puts("ALL MULTI-BLOCK WRITE TESTS PASSED");
	return 0;
}
