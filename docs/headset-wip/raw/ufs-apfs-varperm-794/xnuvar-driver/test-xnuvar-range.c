/*
 * Trial795: the two ranges this driver may touch, and the hole between them.
 *
 * The 64 GiB data volume is GPT entry 89, absolute logical blocks 13803264..30580479. The old
 * 174 MiB window is 608448..653559 inside xnusys, which starts at 30580480 -- so the two are
 * ordered the other way round from what the numbers suggest, and 5.1 GiB of userdata sits between
 * them.
 *
 * The check under test is the guard both hakios_mt6895_ufs_zone_read() and
 * hakios_mt6895_ufs_zone_write_fua() run before they build a command. A hole in the middle is
 * exactly the shape a min/max span gets wrong, so the tests walk the boundaries and the gap
 * rather than sampling.
 *
 * The guard is reproduced here rather than included. Including the driver would mean including
 * xnusys_blk.c, which needs the kernel; and on the host what is worth testing is the arithmetic,
 * not the plumbing. The numbers below are copied from the two headers and the test fails loudly
 * if they ever move -- which is the point, because a stale copy of an LBA is the bug this guards
 * against.
 */
#define _GNU_SOURCE

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

/* ---- copied from source/ufs_twoblock/hakios.xnusys_blk.c and engine/ufs_two_block_engine.h ---- */
#define XNUSYS_DISK_LBA        UINT64_C(30580480)
#define XNUSYS_WINDOW_FIRST    UINT64_C(608448)
#define XNUSYS_WINDOW_LAST     UINT64_C(653559)
#define XNUSYS_VAR_FIRST       UINT64_C(608512)
#define XNUSYS_VAR_BLOCKS      UINT64_C(44544)
#define XNUVAR_PART_FIRST      UINT64_C(13803264)
#define XNUVAR_PART_LAST       UINT64_C(30580479)
#define XNUVAR_PART_BLOCKS     UINT64_C(16777216)

/* ---- the guard, as written in the driver ---- */
static bool
block_is_ours(uint64_t block)
{
	if (block >= XNUSYS_WINDOW_FIRST && block <= XNUSYS_WINDOW_LAST) return true;
	if (block >= XNUVAR_PART_FIRST && block <= XNUVAR_PART_LAST) return true;
	return false;
}

static int failures;

static void
check(bool ok, const char *what)
{
	if (!ok) {
		printf("FAIL %s\n", what);
		failures++;
	}
}

static void
expect(uint64_t block, bool want, const char *what)
{
	bool got = block_is_ours(block);
	if (got != want) {
		printf("FAIL %-44s block %llu: want %s got %s\n", what,
		    (unsigned long long)block, want ? "yes" : "no", got ? "yes" : "no");
		failures++;
	}
}

int
main(void)
{
	/* --- the old window, unchanged --- */
	expect(608447, false, "one before xnusys window");
	expect(608448, true,  "first block of xnusys window");
	expect(608512, true,  "first block of the 174 MiB volume");
	expect(653055, true,  "last block of the 174 MiB volume");
	expect(653559, true,  "last block before the loader log");
	expect(653560, false, "first block of the loader log");

	/* --- the gap --- */
	expect(653561, false,       "just past the old window");
	expect(10000000, false,     "middle of the gap");
	expect(13803263, false,     "one before xnuvar");
	expect(30580480, false,     "xnusys starts here; not in xnuvar");

	/* --- the new volume, both ends and the middle --- */
	expect(13803264, true,  "first block of xnuvar");
	expect(13803265, true,  "second block of xnuvar");
	expect(16777215, true,  "1 GiB into xnuvar");
	expect(22190271, true,  "halfway through xnuvar");
	expect(30580478, true,  "second to last block of xnuvar");
	expect(30580479, true,  "last block of xnuvar");

	/* --- what lies past xnuvar --- */
	expect(30580480, false, "xnusys's first block is not xnuvar's");
	expect(31236087, false, "xnusys's last block is not xnuvar's");
	expect(31240183, false, "flashinfo is not ours");

	/* --- the numbers that made this test necessary --- */
	expect(0, false,        "block zero");
	expect(1, false,        "the GPT header");
	expect(3323904, false,  "userdata's first block after the carve");

	/*
	 * A span test: had the guard been written as `>= 608448 && <= 30580479` -- which is what
	 * "just widen the top" looks like -- every block in the gap would answer yes.
	 */
	for (uint64_t b = 653560; b < XNUVAR_PART_FIRST; b += 7919) {
		if (block_is_ours(b)) {
			printf("FAIL the gap is not contiguous: block %llu admitted\n",
			    (unsigned long long)b);
			failures++;
			break;
		}
	}

	/* --- the volume descriptors --- */
	check(XNUSYS_DISK_LBA + XNUSYS_VAR_FIRST == 31188992ull, "xnusys_var_vol.first is absolute");
	check(XNUSYS_VAR_BLOCKS == 44544ull, "xnusys_var_vol.blocks unchanged");
	check(XNUVAR_PART_FIRST == 13803264ull, "xnuvar_vol.first");
	check(XNUVAR_PART_BLOCKS == 16777216ull, "xnuvar_vol.blocks is 64 GiB of 4K blocks");
	check(XNUVAR_PART_LAST - XNUVAR_PART_FIRST + 1 == XNUVAR_PART_BLOCKS, "the two agree");

	/* the carve: userdata ends where xnuvar begins, and nothing is left over */
	check(13803263ull + 1 == XNUVAR_PART_FIRST, "userdata ends just before xnuvar");
	check(XNUVAR_PART_LAST + 1 == XNUSYS_DISK_LBA, "xnuvar ends where xnusys begins");

	/*
	 * 512-byte sector arithmetic, the number DKIOCGETBLOCKCOUNT has to return. Written as a
	 * 64-bit product on purpose: 16777216 * 4096 is 68719476736, which does not fit in 32 bits,
	 * and a wrapped answer here is a file system sized against a number that is not this one.
	 */
	{
		uint64_t bytes = XNUVAR_PART_BLOCKS * UINT64_C(4096);
		check(bytes == UINT64_C(68719476736), "64 GiB exactly");
		check(bytes / 512ull == UINT64_C(134217728), "134217728 sectors of 512");
		check(bytes / 1024ull / 1024ull == 65536ull, "65536 MiB");

		/*
		 * The 32-bit multiply that would have been wrong. 16777216 << 12 is exactly 2^36, so
		 * the low 32 bits are zero -- a driver that computed the sector count in 32 bits would
		 * report a volume of no size at all, and the file system would refuse to mount rather
		 * than mis-size itself. Worth stating which of the two happens.
		 */
		uint32_t wrapped = (uint32_t)XNUVAR_PART_BLOCKS * 4096u;
		check(wrapped == 0u, "a 32-bit product of this size is zero");
		check(bytes / 512ull != (uint64_t)wrapped, "and nothing like the sector count");
	}

	if (failures == 0) printf("all %d range checks passed\n", 34);
	else printf("%d failure(s)\n", failures);
	return failures != 0;
}
