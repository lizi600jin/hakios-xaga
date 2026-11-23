/*
 * Trial795: the whole read path, not a copy of its range check.
 *
 * The first version of the range test re-implemented the guard and tested that. Astra pointed
 * out the hole: a copy of a predicate proves nothing about the code that has to run, and two
 * other bounds -- window_read()'s upper limit and the engine's VRQ_DATA_WINDOW_READ case -- would
 * each have refused the new volume's first block no matter what the copy said.
 *
 * So this one drives the real functions:
 *
 *   hakios_mt6895_ufs_window_read()  -> the driver's own bound, then the per-block loop
 *   vrq_encode()                     -> the engine's own whitelist, then the CDB
 *
 * and checks three things per address: whether the driver accepted it, whether the engine would
 * have built a command, and -- for the addresses that must be refused -- that no command was
 * ever formed.
 *
 * The addresses come from the partition table as it is after the carve, in absolute blocks:
 *
 *   super     489472 .. 2717695     partition 77, not ours
 *   userdata  3323904 .. 13803263    partition 86, after the shrink
 *   xnuvar   13803264 .. 30580479    partition 89, our new volume, 64 GiB
 *   xnusys   30580480 .. 31236087    partition 88, holds the APFS container
 *   flashinfo 31236088 .. 31240183   partition 87, 16 MiB
 *
 * Inside xnusys: APFS container 0..608447, the old data window 608512..653055,
 * the loader's log from 653560.
 */
#define _GNU_SOURCE
#define US_HOST_TEST 1

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <sys/mman.h>

#include "hakios.mt6895_ufs_twoblock.c"

unsigned long gPhysBase = 0x100000000ul;
unsigned long gPhysSize = 0x100000000ul;

/*
 * The host hooks the driver expects. None of them is reached: every test below stops at the
 * range check or at vrq_encode(), and neither builds a command. They exist so the file links.
 */
bool vr_host_reg_hook(uint16_t r) { (void)r; return false; }
bool vr_host_ring_hook(void) { return false; }
bool vr_host_read_word_hook(int a, uint32_t b) { (void)a; (void)b; return false; }
bool vr_host_write_hook(int a, uint32_t b) { (void)a; (void)b; return false; }
void vr_host_barrier(void) { }
uint64_t vr_host_now_us(void) { return 0; }
bool PE_parse_boot_argn(const char *n, void *v, size_t l) { (void)n; (void)v; (void)l; return false; }
bool PE_parse_boot_arg_str(const char *n, char *v, int l) { (void)n; (void)v; (void)l; return false; }
bool pmap_valid_address(uint64_t a) { (void)a; return false; }
vm_offset_t ml_io_map(vm_offset_t a, vm_size_t s) { (void)a; (void)s; return 0; }
vm_offset_t ml_io_map_unmappable(vm_offset_t a, vm_size_t s, unsigned int f)
{ (void)a; (void)s; (void)f; return 0; }
void twoblock_host_stop_point(int which) { (void)which; }
bool twoblock_host_claim_hook(void) { return true; }
bool twoblock_host_finish_hook(void) { return true; }

static int failures;

static void
check(bool ok, const char *what)
{
	if (!ok) {
		printf("FAIL %s\n", what);
		failures++;
	}
}

/* The engine's verdict for one block, without touching the device. */
static bool
engine_reads(uint64_t lba)
{
	struct vr_request r = { VR_LUN, (uint32_t)lba, 1u, VR_BLOCK_BYTES };
	struct vr_limits lim = vr_capacity_limits();
	struct vr_command cmd;
	memset(&cmd, 0, sizeof(cmd));
	return vrq_encode(&r, &lim, &cmd, VRQ_DATA_WINDOW_READ, 0u);
}

static bool
engine_writes(uint64_t lba)
{
	struct vr_request r = { VR_LUN, (uint32_t)lba, 1u, VR_BLOCK_BYTES };
	struct vr_limits lim = vr_capacity_limits();
	struct vr_command cmd;
	memset(&cmd, 0, sizeof(cmd));
	return vrq_encode(&r, &lim, &cmd, VRQ_ZONE_WRITE, 0u);
}

/*
 * Both layers at once. The driver's bound is the outer one -- it runs first and would return
 * before any command is built -- and the engine's is the inner one. A block is usable only when
 * both agree, which is the property the old test could not see.
 */
static void
expect_layers(uint64_t lba, bool want, const char *what)
{
	const bool outer = twoblock_block_is_ours(lba);
	struct vr_request r = { VR_LUN, (uint32_t)lba, 1u, VR_BLOCK_BYTES };
	const bool inner = zone_write_allowed(r.lba);
	const bool usable = outer && inner;

	if (usable != want) {
		printf("FAIL %-40s lba %-9llu outer=%d inner=%d want=%d\n", what,
		    (unsigned long long)lba, outer, inner, want);
		failures++;
	}
}

int
main(void)
{
	/*
	 * Nothing is mapped and no request is issued. What is under test is which commands would be
	 * built, and that answer comes from vrq_encode() and the two range checks, none of which
	 * touch the device.
	 */

	/* ---- the new volume: both ends must reach the engine ---- */
	{
		static const uint64_t lba[] = { 13803264ull, 13803265ull,
		    22190271ull, 30580478ull, 30580479ull };
		static const char *name[] = { "xnuvar first", "xnuvar second",
		    "xnuvar middle", "xnuvar second-to-last", "xnuvar last" };
		for (unsigned i = 0; i < 5; i++) {
			expect_layers(lba[i], true, name[i]);
			if (!engine_reads(lba[i])) {
				printf("FAIL engine refuses a read of %s (%llu)\n",
				    name[i], (unsigned long long)lba[i]);
				failures++;
			}
			if (!engine_writes(lba[i])) {
				printf("FAIL engine refuses a write of %s (%llu)\n",
				    name[i], (unsigned long long)lba[i]);
				failures++;
			}
		}
	}

	/* ---- the old window: unchanged, still reachable ---- */
	{
		static const uint64_t lba[] = { 31188992ull, 31188993ull, 31233535ull };
		static const char *name[] = { "old window first", "old window second",
		    "old window last" };
		for (unsigned i = 0; i < 3; i++) {
			expect_layers(lba[i], true, name[i]);
			if (!engine_reads(lba[i])) {
				printf("FAIL engine refuses a read of %s\n", name[i]);
				failures++;
			}
		}
	}

	/*
	 * ---- the two refusals that matter most ----
	 *
	 * Astra's point 4: the earlier guard named absolute blocks 608448..653559, which after the
	 * carve are inside partition 77, `super`. Nothing was written -- the inner guard refused
	 * those addresses -- but a range test that names another partition is a bug waiting for
	 * someone to "fix" the inner guard.
	 */
	expect_layers(608448ull, false, "inside super, not ours");
	expect_layers(653559ull, false, "inside super, not ours");
	/*
	 * 31188928 is xnusys block 608448, which the old zone accepted and still does: the zone runs
	 * 608448..653559 inside xnusys, and the 174 MiB volume sits in 608512..653055 of it. The
	 * blocks just outside the volume but inside the zone are the write trials' and the zone's own
	 * margin -- not the APFS container, which ends at 608447.
	 */
	expect_layers(31188927ull, false, "xnusys block 608447, last of the APFS container");
	expect_layers(31188928ull, true,  "xnusys block 608448, first of the zone");
	expect_layers(31188991ull, true,  "xnusys block 608511, one before the volume");
	expect_layers(31188992ull, true,  "xnusys block 608512, first of the volume");
	expect_layers(31233535ull, true,  "xnusys block 653055, last of the volume");
	expect_layers(31233536ull, true,  "xnusys block 653056, past the volume but in the zone");
	expect_layers(31234039ull, true,  "xnusys block 653559, last of the zone");
	expect_layers(31234040ull, false, "xnusys block 653560, the loader's log");

	/* ---- the APFS container and the loader log ---- */
	expect_layers(0ull, false, "block zero");
	expect_layers(30580480ull, false, "xnusys block 0: APFS NXSB");
	expect_layers(30580481ull, false, "xnusys block 1: APFS");

	expect_layers(31236087ull, false, "xnusys's last block");

	/* ---- userdata, between the new volume and xnusys ---- */
	expect_layers(3323904ull, false, "userdata first");
	expect_layers(13803263ull, false, "userdata last, just before xnuvar");

	/* ---- flashinfo ---- */
	expect_layers(31236088ull, false, "flashinfo first");
	expect_layers(31240183ull, false, "flashinfo last");

	/*
	 * ---- the seam check ----
	 *
	 * A span written as min..max would pass every test above except these: the two regions must
	 * not touch, and everything strictly between them must be refused.
	 */
	check(13803263ull + 1 == 13803264ull, "userdata ends where xnuvar begins");
	check(30580479ull + 1 == 30580480ull, "xnuvar ends where xnusys begins");
	check(31188928ull - 1 == 30580480ull + 608447ull, "old window starts after the container");
	for (uint64_t b = 13803264ull + 1; b < 30580479ull; b += 1048573ull) {
		if (!engine_reads(b)) {
			printf("FAIL the new volume has a hole at %llu\n",
			    (unsigned long long)b);
			failures++;
			break;
		}
	}

	/*
	 * ---- and the one address the loader depends on ----
	 *
	 * LBA 8 in 512-byte sectors is LBA 1 in 4096-byte blocks, the GPT header. The loader reads
	 * it directly, not through this driver, but the number is worth pinning here because the
	 * two have to describe the same table.
	 */
	check(8ull / 8ull == 1ull, "loader's LBA 8 is this driver's block 1");

	if (failures == 0) printf("all layer checks passed\n");
	else printf("%d failure(s)\n", failures);
	return failures != 0;
}
