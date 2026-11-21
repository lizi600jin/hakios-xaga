/*
 * Trial749, stage 6 on the phone: write into the window nothing owns, then read it back.
 *
 * The character node, not the block one. A block device's write reaches d_strategy only through
 * the buffer cache, and a raw write(2) on it is not that path; the character node's write goes to
 * physio and therefore straight to the same strategy routine. Measured: with the block node every
 * call returned 0 and the kernel logged nothing at all.
 *
 * The window is blocks 608448..653559 of xnusys -- past the block the APFS container's own
 * superblock says is its last, before the 8 MiB the loader keeps for its log. The whole of it was
 * backed up from the phone before this ran and was all zeros.
 *
 * Three things are checked, in this order:
 *   1. a write inside the container is refused (this is what makes the mount read-only)
 *   2. a write at the start of the window goes out, and a later independent read of the same
 *      block through the read path returns exactly the bytes written
 *   3. a write before the window is refused too
 *
 * The block written carries a trial number, a length and a checksum, so that a reboot and a
 * separate reader can say whether it is still there afterwards.
 */
#include <stdint.h>

#include "mini.h"

struct xnusys_write_arg {
	uint64_t block;
	uint64_t bytes;
	const void *data;
	uint32_t ok;
	uint32_t pad;
};

/* _IOW('x', 4, struct xnusys_write_arg): 32 bytes after alignment. */
#define XNUSYS_WRITE_BLOCK (0x80000000u | (32u << 16) | (0x78u << 8) | 4u)
#define ZONE_FIRST   608448u
#define ZONE_LAST    653559u
#define BLK          4096u

/* Where the sample lives inside the window: a little way in, so the start stays free. */
#define SAMPLE_BLOCK (ZONE_FIRST + 8u)

struct sample {
	uint32_t magic;         /* 'Z','W','7','4' */
	uint32_t trial;
	uint32_t bytes;
	uint32_t fill;
	uint32_t fnv;
	uint32_t pad;
};

static uint32_t
fnv1a(const uint8_t *p, size_t n)
{
	uint32_t h = 2166136261u;
	for (size_t i = 0; i < n; ++i) { h ^= p[i]; h *= 16777619u; }
	return h;
}

static long
pread_at(int fd, uint64_t off, void *buf, size_t len)
{
	long r = mini_syscall(SYS_lseek, fd, (long)off, 0, 0, 0, 0);
	if (r < 0) return r;
	long total = 0;
	while (total < (long)len) {
		long got = mini_syscall(SYS_read, fd, (long)((uint8_t *)buf + total),
		    (long)(len - (size_t)total), 0, 0, 0);
		if (got <= 0) break;
		total += got;
	}
	return total;
}

static long
pwrite_at(int fd, uint64_t off, const void *buf, size_t len)
{
	long r = mini_syscall(SYS_lseek, fd, (long)off, 0, 0, 0, 0);
	if (r < 0) return r;
	long total = 0;
	while (total < (long)len) {
		long put = mini_syscall(SYS_write, fd, (long)((const uint8_t *)buf + total),
		    (long)(len - (size_t)total), 0, 0, 0);
		if (put <= 0) break;
		total += put;
	}
	return total;
}

int
main(int argc, char **argv, char **envp)
{
	(void)argc; (void)argv; (void)envp;

	int failures = 0;
	static uint8_t block[BLK], back[BLK];
	int fd;

	/* ---- 1. a block the container claims must still refuse a write ---- */
	fd = (int)open("/dev/rxnusys", O_RDWR, 0);
	if (fd < 0) {
		printf("xnusys-zonewrite: cannot open the control node (%d)\n", fd);
		return 1;
	}
	for (unsigned i = 0; i < BLK; ++i) block[i] = 0x5a;
	{
		struct xnusys_write_arg a;
		a.block = 1024u;             /* well inside the container */
		a.bytes = BLK;
		a.data = block;
		a.ok = 0; a.pad = 0;
		int rc = (int)mini_syscall(SYS_ioctl, fd, XNUSYS_WRITE_BLOCK, (long)&a, 0, 0, 0);
		printf("xnusys-zonewrite: write inside the container -> rc=%d ok=%u\n", rc, a.ok);
		if (rc == 0 || a.ok) failures++;
	}

	/* ---- 2. write the sample into the window ---- */
	struct sample s;
	s.magic = 0x57345a57u;   /* "ZW4W" little-endian reads Z W 4 W */
	s.trial = 749;
	s.bytes = BLK;
	s.fill = 0x74974974u;
	s.pad = 0;
	for (unsigned i = sizeof(s); i < BLK; ++i) block[i] = (uint8_t)(i * 31u + 7u);
	s.fnv = fnv1a(block, BLK);
	for (unsigned i = 0; i < sizeof(s); ++i) block[i] = ((const uint8_t *)&s)[i];

	{
		struct xnusys_write_arg a;
		a.block = SAMPLE_BLOCK;
		a.bytes = BLK;
		a.data = block;
		a.ok = 0; a.pad = 0;
		int rc = (int)mini_syscall(SYS_ioctl, fd, XNUSYS_WRITE_BLOCK, (long)&a, 0, 0, 0);
		printf("xnusys-zonewrite: write at block %u -> rc=%d ok=%u\n",
		    (unsigned)SAMPLE_BLOCK, rc, a.ok);
		if (rc != 0 || !a.ok) failures++;
	}
	close(fd);

	/* ---- 3. read it back through a fresh open, without the writer in the way ---- */
	fd = (int)open("/dev/rxnusys", O_RDONLY, 0);
	if (fd < 0) { printf("xnusys-zonewrite: reopen failed\n"); return 1; }
	long got = pread_at(fd, (uint64_t)SAMPLE_BLOCK * BLK, back, BLK);
	printf("xnusys-zonewrite: read back %ld bytes\n", got);
	if (got != (long)BLK) {
		failures++;
	} else {
		uint32_t h = fnv1a(back, BLK);
		printf("xnusys-zonewrite: fnv written=%08x read=%08x\n", s.fnv, h);
		if (h != s.fnv) failures++;
		int same = 1;
		for (unsigned i = 0; i < BLK; ++i) if (back[i] != block[i]) { same = 0; break; }
		printf("xnusys-zonewrite: byte-identical: %d\n", same);
		if (!same) failures++;
	}

	/* ---- 4. before the window is refused as well ---- */
	{
		long r = pread_at(fd, (uint64_t)(ZONE_FIRST - 1u) * BLK, back, BLK);
		(void)r;
	}
	close(fd);

	fd = (int)open("/dev/rxnusys", O_RDWR, 0);
	if (fd >= 0) {
		struct xnusys_write_arg a;
		a.block = ZONE_FIRST - 1u;
		a.bytes = BLK;
		a.data = block;
		a.ok = 0; a.pad = 0;
		int rc = (int)mini_syscall(SYS_ioctl, fd, XNUSYS_WRITE_BLOCK, (long)&a, 0, 0, 0);
		printf("xnusys-zonewrite: write just before the window -> rc=%d ok=%u\n", rc, a.ok);
		if (rc == 0 || a.ok) failures++;
		close(fd);
	}

	printf("xnusys-zonewrite: %s\n", failures ? "FAIL" : "PASS");
	return failures ? 1 : 0;
}
