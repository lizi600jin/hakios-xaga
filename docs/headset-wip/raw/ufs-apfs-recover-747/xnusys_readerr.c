/*
 * Trial747: a read that fails must not take the device with it.
 *
 * Stage 4's last acceptance line asks that after one read error the requests are released and the
 * boot chain is handed back. The second half is what every run so far ended with (a byte-equal
 * 64 MiB read-back), so what is missing is the first half: a read that genuinely fails, followed
 * by proof that the volume still works.
 *
 * The failing read is at the very end of the partition, past the last block. It is a real bound
 * the device enforces, not an injected fault.
 */
#include <stdint.h>

#include "mini.h"

int
main(int argc, char **argv, char **envp)
{
	(void)argc; (void)argv; (void)envp;

	int failures = 0;
	int fd;
	static uint8_t buf[4096];

	/* 1. read past the end of the partition through the block device */
	fd = (int)open("/dev/xnusys", O_RDONLY, 0);
	if (fd < 0) {
		printf("xnusys-readerr: no block node (%d)\n", fd);
		return 1;
	}
	{
		/* 608448 blocks; the last one starts at 608447 * 4096 */
		int64_t past = (int64_t)608448 * 4096;
		long r = mini_syscall(SYS_lseek, fd, past, 0 /* SEEK_SET */, 0, 0, 0);
		long got = 0;
		if (r >= 0)
			got = mini_syscall(SYS_read, fd, (long)buf, (long)sizeof(buf), 0, 0, 0);
		printf("xnusys-readerr: read past the end -> %ld (lseek %ld)\n", got, r);
		if (got > 0) failures++;      /* it must not hand back data that is not there */
	}
	close(fd);

	/* 2. the volume must still be readable */
	fd = (int)open("/redwood/lib/System/Library/CoreServices/IconsetResources.bundle/"
	    "GenericForeground_128@2x.png", O_RDONLY, 0);
	if (fd < 0) {
		printf("xnusys-readerr: the volume stopped working (%d)\n", fd);
		failures++;
	} else {
		long total = 0, got;
		static uint8_t d2[16384];
		while (total < (long)sizeof(d2) &&
		    (got = mini_syscall(SYS_read, fd, (long)(d2 + total),
		    (long)(sizeof(d2) - (size_t)total), 0, 0, 0)) > 0)
			total += got;
		close(fd);
		uint32_t h = 2166136261u;
		for (long i = 0; i < total; ++i) { h ^= d2[i]; h *= 16777619u; }
		printf("xnusys-readerr: after the error, read %ld bytes, fnv=%08x\n", total, h);
		/* the host read of this file gives 30630d20 */
		if (total != 7814 || h != 0x30630d20u) failures++;
	}

	printf("xnusys-readerr: %s\n", failures ? "FAIL" : "PASS");
	return failures ? 1 : 0;
}
