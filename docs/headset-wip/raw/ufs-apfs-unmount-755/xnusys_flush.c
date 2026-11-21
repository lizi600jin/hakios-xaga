/*
 * Trial754: a plain write, a real SYNCHRONIZE CACHE(10), then proof the volume is still whole.
 *
 * This is the combination an unmount needs. FUA covers one write; a flush covers everything the
 * device is still holding, which is what a caller has to be able to say before walking away.
 *
 * The block is new (608458) and its contents were generated on the host with a nonce nobody has
 * written before, so an old sample cannot pass for this round's.
 */
#include <stdint.h>

#include "mini.h"
#include "flush_block.h"

struct xnusys_write_arg {
	uint64_t block;
	uint64_t bytes;
	const void *data;
	uint32_t ok;
	uint32_t fua;
};

#define XNUSYS_WRITE_BLOCK (0x80000000u | (32u << 16) | (0x78u << 8) | 4u)
/* _IOWR('x', 5, struct { uint32_t done; uint32_t pad; }): 8 bytes, both directions. */
#define XNUSYS_FLUSH       (0xC0000000u | (8u << 16) | (0x78u << 8) | 5u)

struct xnusys_flush_arg { uint32_t done; uint32_t pad; };
#define TARGET_BLOCK 608458u

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
	if (mini_syscall(SYS_lseek, fd, (long)off, 0, 0, 0, 0) < 0) return -1;
	long total = 0;
	while (total < (long)len) {
		long got = mini_syscall(SYS_read, fd, (long)((uint8_t *)buf + total),
		    (long)(len - (size_t)total), 0, 0, 0);
		if (got <= 0) break;
		total += got;
	}
	return total;
}

int
main(int argc, char **argv, char **envp)
{
	(void)argc; (void)argv; (void)envp;

	static uint8_t want[4096], back[4096];
	int fd, failures = 0, rc;
	long got;

	for (unsigned i = 0; i < sizeof(want); ++i) want[i] = flush_block[i];
	printf("xnusys-flush: block %u, fnv=%08x\n", (unsigned)TARGET_BLOCK,
	    fnv1a(want, sizeof(want)));

	fd = (int)open("/dev/rxnusys", O_RDWR, 0);
	if (fd < 0) { printf("xnusys-flush: no control node (%d)\n", fd); return 1; }

	/* 1. a plain write: no FUA, so the durability has to come from the flush. */
	{
		struct xnusys_write_arg a;
		a.block = TARGET_BLOCK; a.bytes = sizeof(want); a.data = want;
		a.ok = 0; a.fua = 0;
		rc = (int)mini_syscall(SYS_ioctl, fd, XNUSYS_WRITE_BLOCK, (long)&a, 0, 0, 0);
		printf("xnusys-flush: plain write -> rc=%d ok=%u\n", rc, a.ok);
		if (rc != 0) failures++;
	}

	/* 2. the flush that is supposed to make it real. */
	{
		struct xnusys_flush_arg fa;
		fa.done = 0; fa.pad = 0;
		rc = (int)mini_syscall(SYS_ioctl, fd, XNUSYS_FLUSH, (long)&fa, 0, 0, 0);
		printf("xnusys-flush: synchronize cache -> rc=%d done=%u\n", rc, fa.done);
	}
	if (rc != 0) failures++;
	close(fd);

	/* 3. read it back through a fresh open. */
	fd = (int)open("/dev/rxnusys", O_RDONLY, 0);
	if (fd < 0) { printf("xnusys-flush: reopen failed\n"); return 1; }
	got = pread_at(fd, (uint64_t)TARGET_BLOCK * 4096u, back, sizeof(back));
	close(fd);
	printf("xnusys-flush: read back %ld bytes, fnv=%08x\n", got, fnv1a(back, sizeof(back)));
	if (got != (long)sizeof(back)) {
		failures++;
	} else {
		int same = 1;
		for (unsigned i = 0; i < sizeof(want); ++i) if (want[i] != back[i]) { same = 0; break; }
		printf("xnusys-flush: byte-identical: %d\n", same);
		if (!same) failures++;
		printf("xnusys-flush: nonce=");
		for (unsigned i = 0; i < 8; ++i) printf("%02x", back[i]);
		printf("\n");
	}

	printf("xnusys-flush: %s\n", failures ? "FAIL" : "PASS");
	return failures ? 1 : 0;
}
