/*
 * Trial774: read the data window through /dev/ufs_xnusys -- the loader's channel.
 *
 * Every previous attempt went through the driver that talks to the controller itself, and each one
 * ended with the lease poisoned. This one uses the other driver: the kernel writes a request into a
 * shared page and the loader reads the flash, so the kernel never touches the controller and never
 * enters the lease state machine at all.
 *
 * If this works it is not a workaround. It is the arrangement the project already uses for its own
 * data volume (var_store), and the one whose writes are proved on the flash because the loader
 * flushes and reads every block back before answering.
 */
#include <stdint.h>

#include "mini.h"

/* The window in flash blocks, and the offset within the partition that reaches it. */
#define WINDOW_FIRST_BLOCK 608512u
#define WINDOW_LAST_BLOCK  653055u
#define BLOCK 4096u

static uint32_t fnv1a(const uint8_t *p, size_t n)
{
	uint32_t h = 2166136261u;
	for (size_t i = 0; i < n; ++i) { h ^= p[i]; h *= 16777619u; }
	return h;
}

static long pread_at(int fd, uint64_t off, void *buf, size_t len)
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

	static uint8_t blk[BLOCK];
	int fd, failures = 0;
	long got;

	printf("xnusys-ufs: opening /dev/ufs_xnusys\n");
	fd = (int)open("/dev/ufs_xnusys", O_RDONLY, 0);
	if (fd < 0) {
		printf("xnusys-ufs: no such device (%d)\n", fd);
		return 1;
	}

	/* The window's first block, which holds the HFS Plus volume header at offset 1024. */
	got = pread_at(fd, (uint64_t)WINDOW_FIRST_BLOCK * BLOCK, blk, sizeof(blk));
	printf("xnusys-ufs: read block %u -> %ld bytes, fnv=%08x\n",
	    WINDOW_FIRST_BLOCK, got, fnv1a(blk, sizeof(blk)));
	printf("xnusys-ufs: header at 1024: %02x %02x %02x %02x\n",
	    blk[1024], blk[1025], blk[1026], blk[1027]);
	if (got != (long)sizeof(blk)) failures++;
	if (!(blk[1024] == 0x48 && blk[1025] == 0x2b)) failures++;

	/* The last block of the window, to prove the top of the range reads too. */
	got = pread_at(fd, (uint64_t)WINDOW_LAST_BLOCK * BLOCK, blk, sizeof(blk));
	printf("xnusys-ufs: read block %u -> %ld bytes, fnv=%08x\n",
	    WINDOW_LAST_BLOCK, got, fnv1a(blk, sizeof(blk)));
	if (got != (long)sizeof(blk)) failures++;

	close(fd);
	printf("xnusys-ufs: %s\n", failures ? "FAIL" : "PASS");
	return failures ? 1 : 0;
}
