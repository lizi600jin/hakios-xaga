/*
 * Trial786 host test: the read-modify-write the data volume's write path now does.
 *
 * The driver used to refuse any write that was not a whole 4K block, silently, and HFS Plus
 * writes its volume headers in 512-byte pieces. What replaces it is this: for each block a
 * request touches, read it, lay the caller's bytes over the part they cover, write it back.
 *
 * The loop below is the same arithmetic, run against a byte array. If it is right, any offset and
 * any length leave the window byte-for-byte what a memcpy into it would have made it -- which is
 * the only property the file system above depends on.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#define BLK 4096u
#define NBLK 64u
#define WINDOW (NBLK * BLK)

static uint8_t sim[WINDOW];
static uint8_t mirror[WINDOW];

static int pass, fail;

/* The driver's partial-write path, in the shape it is written in hakios.xnusys_blk.c. */
static int rmw_write(uint64_t offset, uint64_t len, const uint8_t *src)
{
	static uint8_t bounce[BLK];
	uint64_t first = offset / BLK;
	uint64_t lastblk = (offset + len + BLK - 1u) / BLK;
	uint64_t nblk = lastblk - first;

	if (len == 0u || nblk == 0u || first + nblk > NBLK) return -1;

	if ((offset % BLK) == 0u && (len % BLK) == 0u) {
		for (uint64_t i = 0; i < nblk; i++) {
			memcpy(sim + (first + i) * BLK, src + i * BLK, BLK);
		}
		return 0;
	}

	for (uint64_t i = 0; i < nblk; i++) {
		uint64_t blk = first + i;
		uint64_t blk_start = blk * BLK;
		uint64_t copy_from = (offset > blk_start) ? offset : blk_start;
		uint64_t copy_end = blk_start + BLK;
		uint64_t req_end = offset + len;
		uint64_t n, within;

		if (copy_end > req_end) copy_end = req_end;
		n = copy_end - copy_from;
		within = copy_from - blk_start;

		if (within == 0u && n == BLK) {
			memcpy(sim + blk_start, src + (copy_from - offset), BLK);
		} else {
			memcpy(bounce, sim + blk_start, BLK);          /* the read */
			memcpy(bounce + within, src + (copy_from - offset), n);
			memcpy(sim + blk_start, bounce, BLK);          /* the write back */
		}
	}
	return 0;
}

static void reset(void)
{
	uint32_t s = 12345;
	for (unsigned i = 0; i < WINDOW; i++) {
		s = s * 1103515245u + 12345u;
		sim[i] = (uint8_t)(s >> 16);
		mirror[i] = sim[i];
	}
}

static void check(const char *what, uint64_t off, uint64_t len)
{
	static uint8_t src[WINDOW];
	uint32_t s = (uint32_t)(off * 31 + len);
	int bad;

	for (uint64_t i = 0; i < len; i++) {
		s = s * 1103515245u + 12345u;
		src[i] = (uint8_t)(s >> 16);
	}
	reset();
	if (rmw_write(off, len, src) != 0) {
		printf("  FAIL %-40s rmw refused\n", what);
		fail++;
		return;
	}
	memcpy(mirror + off, src, len);

	bad = memcmp(sim, mirror, WINDOW) != 0;
	if (bad) { printf("  FAIL %-40s window differs\n", what); fail++; }
	else     { printf("  PASS %-40s off=%llu len=%llu\n", what, (unsigned long long)off, (unsigned long long)len); pass++; }
}

int
main(void)
{
	printf("Trial786 read-modify-write test\n\n");

	printf("-- the shapes HFS Plus actually asks for --\n");
	check("volume header, 512 at 1024", 1024, 512);
	check("alternate header, 512 at end", WINDOW - 1024 + 512, 512);
	check("one whole block", 2 * BLK, BLK);
	check("several whole blocks", 3 * BLK, 5 * BLK);

	printf("\n-- the edges of a block --\n");
	check("crosses one boundary", BLK - 1, 2);
	check("starts at block end", BLK, 1);
	check("ends at block start", BLK - 1, 1);
	check("spans three blocks", BLK + 100, 3 * BLK);
	check("one byte", 7777, 1);

	printf("\n-- random offsets and lengths --\n");
	{
		uint32_t s = 999;
		for (int i = 0; i < 200; i++) {
			uint64_t off, len;
			char name[64];

			s = s * 1103515245u + 12345u; off = (s >> 8) % (WINDOW - 9000);
			s = s * 1103515245u + 12345u; len = 1 + (s >> 8) % 8192;
			snprintf(name, sizeof(name), "random #%d", i);
			check(name, off, len);
		}
	}

	printf("\n-- what must still be refused --\n");
	{
		reset();
		if (rmw_write(WINDOW - 100, 200, sim) != 0) { printf("  PASS past the end refused\n"); pass++; }
		else { printf("  FAIL past the end accepted\n"); fail++; }
		reset();
		if (rmw_write(0, 0, sim) != 0) { printf("  PASS zero length refused\n"); pass++; }
		else { printf("  FAIL zero length accepted\n"); fail++; }
	}

	printf("\n%d PASS, %d FAIL\n", pass, fail);
	return fail ? 1 : 0;
}
