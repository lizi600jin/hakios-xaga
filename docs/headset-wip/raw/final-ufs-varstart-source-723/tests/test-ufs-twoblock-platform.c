#define _GNU_SOURCE
#define US_HOST_TEST 1

#include <assert.h>
#include <string.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <stdio.h>

#include "hakios.mt6895_ufs_twoblock.c"

unsigned long gPhysBase = 0x100000000ul;
unsigned long gPhysSize = 0x100000000ul;

static uint32_t regs[VR_REG_BYTES / 4u];
static uint32_t table_page[PAGE_SIZE / 4u];
static uint8_t *scratch;
static uint8_t *ref_window;   /* local reference blocks, never printed */
static struct {
	uint32_t mode, trial, other, stop_at;
	unsigned maps, reg_reads, rings, data_writes, restore_writes;
	bool pending, completed, bad_end, corrupt_second;
	bool partial_second, fail_second_restore, swap_blocks;
	uint64_t time_us;
	uint32_t want_lba, cdb_lba, cdb_blocks;
	bool start_lba_present;      /* absent and explicit 0 must be distinguishable */
	uint64_t start_lba_raw;      /* full width, so 2^32+2 cannot wrap to 2 */
} model;

static unsigned failures;

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

static void
report_case(const char *name, uint32_t want, uint32_t cdb, unsigned rings)
{
	bool ok = (cdb == want) && (rings == 1);
	printf("  case %-36s start=%u cdb=%u rings=%u %s\n",
	    name, want, cdb, rings, ok ? "PASS" : "FAIL");
	if (!ok) failures++;
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
	assert(size == sizeof(uint32_t));
	if (strcmp(name, "mtk_ufs_twoblock") == 0) value = model.mode;
	else if (strcmp(name, "mali_trial") == 0) value = model.trial;
	else if (strcmp(name, "mtk_ufs_varread") == 0) value = model.other;
	else return false;
	memcpy(out, &value, size);
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
	assert(upiu[0] == 1 && upiu[1] == 0x40 && upiu[2] == 2 && upiu[16] == 0x28);
	model.cdb_lba = ur_be32(upiu + 18);
	model.cdb_blocks = (uint32_t)upiu[24];
	assert(model.cdb_lba == model.want_lba);
	assert(model.cdb_blocks == 2u && upiu[23] == 0);
	assert(ur_be32(upiu + 12) == 8192);
	assert((table()[7] & 0xffffu) == 1u && ucd()[256] == UR_DATA_PA && ucd()[259] == 8191);

	/* Serve the real reference blocks at the start LBA the CDB carried. The window is read
	 * from a local file so no reference bytes live in this source; the path is given by
	 * UFS_REF_WINDOW. If it is absent the model keeps a synthetic pattern and the CRC check
	 * is expected to fail, which is itself a valid negative case. */
	for (uint32_t blk = 0; blk < 2u; blk++) {
		uint32_t lba = model.cdb_lba + blk;
		uint32_t *dst = data() + blk * VRQ_BLOCK_WORDS;
		const uint8_t *src = ref_window ? ref_window + (size_t)lba * VR_BLOCK_BYTES : NULL;
		if (src) {
			memcpy(dst, src, VR_BLOCK_BYTES);
		} else {
			for (uint32_t i = 0; i < VRQ_BLOCK_WORDS; i++)
				dst[i] = varread_reference_crc(lba) ^ (0x9e3779b9u * (i + 1u));
		}
	}
	if (model.swap_blocks) {
		for (uint32_t i = 0; i < VRQ_BLOCK_WORDS; i++) {
			uint32_t t = data()[i];
			data()[i] = data()[VRQ_BLOCK_WORDS + i];
			data()[VRQ_BLOCK_WORDS + i] = t;
		}
	}
	if (model.corrupt_second) data()[VRQ_BLOCK_WORDS] ^= 1u;

	ucd()[512 / 4] = 0x00020021u;
	table()[2] = 0;
	regs[0x58 / 4] = 0;
	model.pending = false;
	model.completed = true;
}

bool vr_host_reg_hook(uint16_t offset)
{
	model.reg_reads++;
	if (offset == 0x58 && model.pending) model_complete();
	return true;
}

bool vr_host_ring_hook(void)
{
	assert(regs[0x58 / 4] == 1 && model.rings == 0);
	model.rings++;
	model.pending = true;
	return true;
}

bool vr_host_read_word_hook(int region, uint32_t offset)
{
	if (region == VRQ_DATA && model.completed && model.partial_second && offset == 4096u + 80u) {
		model.partial_second = false;
		return false;
	}
	return true;
}

bool vr_host_write_hook(int region, uint32_t offset)
{
	if (region == VRQ_DATA) {
		model.data_writes++;
		if (model.completed) {
			if (model.fail_second_restore && offset == 4096u) {
				model.fail_second_restore = false;
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
	if (model.stop_at == (uint32_t)which) hakios_mt6895_ufs_twoblock_quiesce();
}

static void
reset(void)
{
	uint32_t keep_lba = model.want_lba;
	bool keep_present = model.start_lba_present;
	uint64_t keep_raw = model.start_lba_raw;
	memset(&model, 0, sizeof(model));
	model.want_lba = keep_lba;
	model.start_lba_present = keep_present;
	model.start_lba_raw = keep_raw;
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
	reset();
	/* Snapshot the data area after reset and before the adapter runs. */
	static uint8_t before_payload[VR2_TRANSFER_BYTES];
	memcpy(before_payload, data(), VR2_TRANSFER_BYTES);
	hakios_mt6895_ufs_twoblock_init();
	assert(model.maps == 3 && model.rings == 1 && model.restore_writes == 2048);
	assert(twoblock_outcome.result == VRQ_DELIVERED && twoblock_outcome.data_words_valid == 2048);
	assert(us_control(&twoblock_lease) == US_IDLE && lease_record()->state == UR_CLAIMED);
	/* Independent evidence: the whole 8192 B must come back byte-for-byte. */
	assert(memcmp(data(), before_payload, VR2_TRANSFER_BYTES) == 0);
	assert(twoblock_outcome.readback_restored);
	assert(twoblock_outcome.restored);
	assert(twoblock_word(&platform, VRQ_DATA, 8188) == data() + 2047);
	assert(twoblock_word(&platform, VRQ_DATA, 8192) == NULL);
	assert(twoblock_word(&platform, VRQ_DATA, 8190) == NULL);

	const unsigned reads = model.reg_reads;
	hakios_mt6895_ufs_twoblock_init();
	hakios_mt6895_ufs_twoblock_quiesce();
	assert(model.maps == 3 && model.reg_reads == reads && model.rings == 1);
	assert(lease_record()->state == UR_RESTORED);
}

int
main(int argc, char **argv)
{
	{
		const char *path = getenv("UFS_REF_WINDOW");
		FILE *f = path ? fopen(path, "rb") : NULL;
		if (f) {
			ref_window = malloc(65536);
			if (ref_window && fread(ref_window, 1, 65536, f) != 65536) ref_window = NULL;
			fclose(f);
		}
	}
	scratch = mmap(NULL, VR_SCRATCH_BYTES + PAGE_SIZE, PROT_READ | PROT_WRITE,
	    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	assert((void *)scratch != MAP_FAILED);
	assert(mprotect(scratch + VR_SCRATCH_BYTES, PAGE_SIZE, PROT_NONE) == 0);

	puts("SYNTHETIC TRIAL722 HOST FORMAT FIXTURE");
	set_start(1);
	test_success();
	report_case("lba=1 (R22 regression)", 1, model.cdb_lba, model.rings);
	{
		uint32_t starts[] = { 2u, 14u };
		for (unsigned i = 0; i < 2; i++) {
			set_start(starts[i]);
			test_success();
			report_case(starts[i] == 2u ? "lba=2 (window moves to 2,3)"
			                            : "lba=14 (last legal window)",
			    starts[i], model.cdb_lba, model.rings);
		}
	}
	{
		uint32_t bad[] = { 15u, 0xffffffffu };
		for (unsigned i = 0; i < 2; i++) {
			reset();
			set_start(bad[i]);
			hakios_mt6895_ufs_twoblock_init();
			bool ok = (model.rings == 0 && model.data_writes == 0);
			printf("  case %-36s start=%u rings=%u writes=%u %s\n",
			    i == 0 ? "lba=15 (window would pass the end)" : "lba=UINT32_MAX",
			    bad[i], model.rings, model.data_writes, ok ? "REFUSED PASS" : "FAIL");
			if (!ok) failures++;
		}
	}
	{	/* absent argument: the default of 1 must be used */
		reset();
		clear_start();
		memset(&twoblock_outcome, 0, sizeof(twoblock_outcome));
		hakios_mt6895_ufs_twoblock_init();
		printf("  case absent (default 1): cdb=%u rings=%u %s\n",
		    model.cdb_lba, model.rings,
		    (model.cdb_lba == 1 && model.rings == 1) ? "PASS" : "FAIL");
		if (!(model.cdb_lba == 1 && model.rings == 1)) failures++;
	}
	{	/* explicit 0 is a value, refused for lacking a reference */
		reset();
		model.start_lba_present = true;
		model.start_lba_raw = 0;
		memset(&twoblock_outcome, 0, sizeof(twoblock_outcome));
		hakios_mt6895_ufs_twoblock_init();
		bool ok = (model.rings == 0 && model.data_writes == 0);
		printf("  case explicit 0 (no reference): rings=%u writes=%u %s\n",
		    model.rings, model.data_writes, ok ? "REFUSED PASS" : "FAIL");
		if (!ok) failures++;
	}
	{	/* 2^32+2 must not wrap to a valid LBA 2 */
		reset();
		model.start_lba_present = true;
		model.start_lba_raw = 4294967298ull;
		memset(&twoblock_outcome, 0, sizeof(twoblock_outcome));
		hakios_mt6895_ufs_twoblock_init();
		bool ok = (model.rings == 0 && model.data_writes == 0);
		printf("  case 2^32+2 (no wrap to 2): rings=%u writes=%u %s\n",
		    model.rings, model.data_writes, ok ? "REFUSED PASS" : "FAIL");
		if (!ok) failures++;
	}
	{	/* start=3 is inside the geometry but block LBA 4 has no frozen reference */
		reset();
		set_start(3);
		memset(&twoblock_outcome, 0, sizeof(twoblock_outcome));
		hakios_mt6895_ufs_twoblock_init();
		bool refused = (model.rings == 0 && model.data_writes == 0);
		printf("  case start=3 (lba 4 has no reference): rings=%u writes=%u %s\n",
		    model.rings, model.data_writes, refused ? "REFUSED PASS" : "FAIL");
		if (!refused) failures++;
	}
	{	/* start=2 with the two blocks swapped: the CRC check must notice */
		reset();
		set_start(2);
		model.swap_blocks = true;
		memset(&twoblock_outcome, 0, sizeof(twoblock_outcome));
		hakios_mt6895_ufs_twoblock_init();
		/* The adapter reports the refusal through the lease, not through outcome.result. */
		bool poisoned = (us_control(&twoblock_lease) & 0x200u) != 0 ||
		                lease_record()->state == UR_POISONED;
		bool caught = poisoned || twoblock_outcome.result != VRQ_DELIVERED;
		printf("  case start=2 swapped blocks: ctl=%#x state=%u %s\n",
		    us_control(&twoblock_lease), lease_record()->state,
		    caught ? "REJECTED PASS" : "MISSED FAIL");
		if (!caught) failures++;
	}
	set_start(1);
	if (failures) { printf("  %u case(s) FAILED\n", failures); return 1; }
	test_success();
	if (argc != 2 || strcmp(argv[1], "--fixture") != 0) {
		reset();
		model.trial = 721;
		hakios_mt6895_ufs_twoblock_init();
		assert(model.maps == 0 && model.rings == 0);
		reset();
		model.other = 1;
		hakios_mt6895_ufs_twoblock_init();
		assert(model.maps == 0 && model.rings == 0);
		reset();
		model.bad_end = true;
		hakios_mt6895_ufs_twoblock_init();
		assert(model.maps == 0 && model.rings == 0);
		reset();
		lease_record()->trial = 721;
		hakios_mt6895_ufs_twoblock_init();
		assert(model.maps == 1 && model.reg_reads == 0 && model.rings == 0);

		reset();
		model.corrupt_second = true;
		hakios_mt6895_ufs_twoblock_init();
		assert(model.rings == 1 && model.restore_writes == 2048);
		assert(twoblock_outcome.result == VRQ_DELIVERED && lease_record()->state == UR_POISONED);
		reset();
		model.partial_second = true;
		hakios_mt6895_ufs_twoblock_init();
		assert(model.rings == 1 && !model.partial_second && model.restore_writes == 2048);
		assert(twoblock_outcome.data_words_valid == 1044 && lease_record()->state == UR_POISONED);
		reset();
		model.fail_second_restore = true;
		hakios_mt6895_ufs_twoblock_init();
		assert(model.rings == 1 && !model.fail_second_restore && model.restore_writes == 1024);
		assert(twoblock_outcome.result == VRQ_UNCERTAIN && lease_record()->state == UR_POISONED);
		reset();
		model.stop_at = 3;
		hakios_mt6895_ufs_twoblock_init();
		assert(model.rings == 1 && lease_record()->state == UR_RESTORED);
		assert((us_control(&twoblock_lease) & US_POISON_BIT) == 0);
		puts("Platform PASS: one ring, 8 KiB guards and restoration, both CRCs, and fail-closed paths");
	}
	assert(munmap(scratch, VR_SCRATCH_BYTES + PAGE_SIZE) == 0);
	return 0;
}
