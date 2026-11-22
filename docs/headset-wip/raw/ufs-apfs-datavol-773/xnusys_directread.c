/*
 * Trial764: read the data window's first block through an ioctl, with no file system in the way.
 *
 * Five attempts at making the device node readable all ended in the loader's guard, and two of
 * them were plumbing bugs rather than driver bugs. This asks the driver the one question that
 * separates the two: can it fetch flash block 608512 at all?
 *
 * If the answer is yes and the bytes start 48 2b (an HFS Plus volume header), then the reading
 * works and whatever is broken is in the way user space reaches it. If the answer is no, the
 * problem is below, in the driver, and the node was never the issue.
 */
#include <stdint.h>

#include "mini.h"

struct xnusys_read_block_arg {
	uint64_t block;
	uint64_t bytes;
	void    *data;
	uint32_t ok;
	uint32_t pad;
};

#define XNUSYS_READ_BLOCK (0xC0000000u | (32u << 16) | (0x78u << 8) | 6u)
#define FIRST_WINDOW_BLOCK 608512u
#define LAST_WINDOW_BLOCK  653055u

static uint32_t
fnv1a(const uint8_t *p, size_t n)
{
	uint32_t h = 2166136261u;
	for (size_t i = 0; i < n; ++i) { h ^= p[i]; h *= 16777619u; }
	return h;
}

int
main(int argc, char **argv, char **envp)
{
	(void)argc; (void)argv; (void)envp;

	static uint8_t blk[4096];
	int fd, failures = 0, rc;

	fd = (int)open("/dev/rxnusys", O_RDONLY, 0);
	if (fd < 0) {
		printf("xnusys-direct: no control node (%d)\n", fd);
		return 1;
	}

	/* 1. the first block of the window: this is where the HFS Plus volume header lives. */
	{
		struct xnusys_read_block_arg a;
		a.block = FIRST_WINDOW_BLOCK; a.bytes = 4096; a.data = blk; a.ok = 0; a.pad = 0;
		rc = (int)mini_syscall(SYS_ioctl, fd, XNUSYS_READ_BLOCK, (long)&a, 0, 0, 0);
		printf("xnusys-direct: read block %u -> rc=%d ok=%u\n",
		    (unsigned)FIRST_WINDOW_BLOCK, rc, a.ok);
		if (rc != 0) failures++;
		printf("xnusys-direct: header at 1024: %02x %02x %02x %02x\n",
		    blk[1024], blk[1025], blk[1026], blk[1027]);
		printf("xnusys-direct: fnv=%08x\n", fnv1a(blk, sizeof(blk)));
		if (!(blk[1024] == 0x48 && blk[1025] == 0x2b)) failures++;
	}

	/* 2. the last block of the window: proves the top of the range reads too. */
	{
		struct xnusys_read_block_arg a;
		a.block = LAST_WINDOW_BLOCK; a.bytes = 4096; a.data = blk; a.ok = 0; a.pad = 0;
		rc = (int)mini_syscall(SYS_ioctl, fd, XNUSYS_READ_BLOCK, (long)&a, 0, 0, 0);
		printf("xnusys-direct: read block %u -> rc=%d ok=%u fnv=%08x\n",
		    (unsigned)LAST_WINDOW_BLOCK, rc, a.ok, fnv1a(blk, sizeof(blk)));
		if (rc != 0) failures++;
	}

	/* 3. one block outside the window must be refused, and refusing must not hurt. */
	{
		struct xnusys_read_block_arg a;
		a.block = 608448; a.bytes = 4096; a.data = blk; a.ok = 0; a.pad = 0;   /* in the APFS container */
		rc = (int)mini_syscall(SYS_ioctl, fd, XNUSYS_READ_BLOCK, (long)&a, 0, 0, 0);
		printf("xnusys-direct: read block 608448 (container) -> rc=%d ok=%u\n", rc, a.ok);
		if (rc == 0 || a.ok != 0) failures++;
	}

	/* 4. and the device still answers afterwards. */
	{
		struct xnusys_read_block_arg a;
		a.block = FIRST_WINDOW_BLOCK; a.bytes = 4096; a.data = blk; a.ok = 0; a.pad = 0;
		rc = (int)mini_syscall(SYS_ioctl, fd, XNUSYS_READ_BLOCK, (long)&a, 0, 0, 0);
		printf("xnusys-direct: read again after the refusal -> rc=%d ok=%u\n", rc, a.ok);
		if (rc != 0) failures++;
	}

	close(fd);
	printf("xnusys-direct: %s\n", failures ? "FAIL" : "PASS");
	return failures ? 1 : 0;
}
