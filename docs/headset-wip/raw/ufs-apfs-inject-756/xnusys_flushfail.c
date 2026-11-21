/*
 * Trial756: what happens when the flush the unmount needs fails.
 *
 * The device is told, once, at boot, to refuse the next flush. Then:
 *
 *   1. the unmount of a mounted volume must come back with an error
 *   2. the volume must still be mounted and its files still readable -- a refused unmount that
 *      quietly released anything would be worse than one that went ahead
 *   3. without rebooting, the next unmount must work, because the fault was one shot
 *
 * Step 2 is the one that cannot be skipped. Checking only the return code would accept a program
 * whose ioctl never reached the kernel, which is exactly the mistake made in Trial755.
 */
#include <stdint.h>

#include "mini.h"

struct xnusys_mount_arg { char from[64]; char at[192]; };

#define XNUSYS_MOUNT    (0x80000000u | (256u << 16) | (0x78u << 8) | 1u)
#define XNUSYS_UNMOUNT  (0x80000000u | (256u << 16) | (0x78u << 8) | 2u)

static void put(char *dst, const char *src, unsigned cap)
{
	unsigned i = 0;
	while (src[i] && i < cap - 1u) { dst[i] = src[i]; ++i; }
	dst[i] = '\0';
}

static int
do_unmount(const char *at)
{
	struct xnusys_mount_arg a;
	int fd, rc;
	for (unsigned i = 0; i < sizeof(a); ++i) ((char *)&a)[i] = 0;
	put(a.from, "/dev/xnusys", sizeof(a.from));
	put(a.at, at, sizeof(a.at));
	fd = (int)open("/dev/rxnusys", O_RDONLY, 0);
	if (fd < 0) return -1000;
	rc = (int)mini_syscall(SYS_ioctl, fd, XNUSYS_UNMOUNT, (long)&a, 0, 0, 0);
	close(fd);
	return rc;
}

/* Read the volume's file and say whether it came back whole. */
static int
volume_file_ok(void)
{
	const char *p =
	    "/redwood/lib/System/Library/CoreServices/IconsetResources.bundle/"
	    "GenericForeground_128@2x.png";
	static uint8_t b[8192];
	int fd = (int)open(p, O_RDONLY, 0);
	long total = 0, got;

	if (fd < 0) return 0;
	while (total < (long)sizeof(b) &&
	    (got = mini_syscall(SYS_read, fd, (long)(b + total),
	    (long)(sizeof(b) - (size_t)total), 0, 0, 0)) > 0)
		total += got;
	close(fd);
	if (total != 7814) return -1;
	uint32_t h = 2166136261u;
	for (long i = 0; i < total; ++i) { h ^= b[i]; h *= 16777619u; }
	return h == 0x30630d20u ? 1 : -2;
}

int
main(int argc, char **argv, char **envp)
{
	(void)argc; (void)argv; (void)envp;

	int failures = 0, rc, v;

	/* Before anything: the volume has to be mounted and readable for this test to mean much. */
	v = volume_file_ok();
	printf("xnusys-flushfail: the volume's file before -> %d\n", v);
	if (v != 1) { printf("xnusys-flushfail: not mounted or not readable, nothing to test\n"); return 1; }

	/* 1. the unmount whose flush will be refused. */
	rc = do_unmount("/redwood/lib");
	printf("xnusys-flushfail: unmount with the flush refused -> %d\n", rc);
	if (rc == 0) failures++;

	/* 2. and the volume is still there, whole. This is the part that matters. */
	v = volume_file_ok();
	printf("xnusys-flushfail: the volume's file after the refusal -> %d\n", v);
	if (v != 1) failures++;

	/* 3. the fault was one shot: try again without rebooting. */
	rc = do_unmount("/redwood/lib");
	printf("xnusys-flushfail: the second unmount -> %d\n", rc);
	if (rc != 0) failures++;

	/* The mount point is a ramdisk directory again, so the volume's file is gone. */
	{
		int fd = (int)open("/redwood/lib/System/Library/CoreServices/IconsetResources.bundle/"
		    "GenericForeground_128@2x.png", O_RDONLY, 0);
		printf("xnusys-flushfail: after the second unmount -> %d\n", fd);
		if (fd >= 0) { failures++; close(fd); }
	}

	printf("xnusys-flushfail: %s\n", failures ? "FAIL" : "PASS");
	return failures ? 1 : 0;
}
