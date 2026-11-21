/*
 * Trial761: can the data-volume device read its own first block, and nothing else.
 *
 * Trial760 ended with the loader's guard refusing ownership back, which is what it does when a
 * request was admitted and never restored. A block with no measured reference made the runner
 * refuse after the admission, and refusing there skipped the restore. This asks the one question
 * that would have found it: read block 0 of /dev/xnusys_var and print what came back.
 *
 * Nothing is mounted and nothing is written. If the guard appears again, the window device is
 * still wrong; if the HFS Plus signature appears, the read path is right and the mount can be
 * tried next.
 */
#include <stdint.h>

#include "mini.h"

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

	static uint8_t b[4096];
	int fd, failures = 0;

	fd = (int)open("/dev/xnusys_var", O_RDONLY, 0);
	if (fd < 0) {
		printf("xnusys-varread: no /dev/xnusys_var (%d)\n", fd);
		return 1;
	}

	/* First ask the device how big it says it is: that proves the ioctl path works and tells us
	 * whether an open of the block node reached this driver at all. */
	{
		uint32_t bsize = 0;
		uint64_t bcount = 0;
		long r1 = mini_syscall(SYS_ioctl, fd, 0x40046418u, (long)&bsize, 0, 0, 0);  /* DKIOCGETBLOCKSIZE */
		long r2 = mini_syscall(SYS_ioctl, fd, 0x40086419u, (long)&bcount, 0, 0, 0); /* DKIOCGETBLOCKCOUNT */
		printf("xnusys-varread: DKIOCGETBLOCKSIZE -> %ld size=%u\n", r1, bsize);
		printf("xnusys-varread: DKIOCGETBLOCKCOUNT -> %ld count=%llu\n", r2,
		    (unsigned long long)bcount);
	}

	/* Block 0, which is flash block 608512 -- the HFS Plus volume header block. */
	long got = mini_syscall(SYS_read, fd, (long)b, 4096, 0, 0, 0);
	close(fd);

	printf("xnusys-varread: read %ld bytes, fnv=%08x\n", got, fnv1a(b, 4096));

	/* The volume header sits at offset 1024 of block 0 and starts with 0x482b, big endian. */
	printf("xnusys-varread: bytes at 1024: %02x %02x %02x %02x\n",
	    b[1024], b[1025], b[1026], b[1027]);
	if (got != 4096) failures++;
	if (!(b[1024] == 0x48 && b[1025] == 0x2b)) failures++;

	/* And the last block of the window, to prove the top of the range works too. */
	fd = (int)open("/dev/xnusys_var", O_RDONLY, 0);
	if (fd >= 0) {
		if (mini_syscall(SYS_lseek, fd, (long)(44543u * 4096u), 0, 0, 0, 0) >= 0) {
			long n = mini_syscall(SYS_read, fd, (long)b, 4096, 0, 0, 0);
			printf("xnusys-varread: last window block read %ld bytes\n", n);
			if (n != 4096) failures++;
		}
		close(fd);
	}

	printf("xnusys-varread: %s\n", failures ? "FAIL" : "PASS");
	return failures ? 1 : 0;
}
