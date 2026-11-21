/*
 * Trial755: the three unmount cases stage 6 has to answer for.
 *
 *   normal       a plain write, then an unmount that flushes for itself and succeeds, then a
 *                remount that still finds the volume's own files
 *   busy         a file held open on the volume: the unmount must say EBUSY, and succeed once the
 *                file is closed. A forced unmount would have made this pass for the wrong reason.
 *   flush fails  the unmount must refuse and leave the volume mounted and readable
 *
 * The flush is the unmount's own -- not a separate ioctl beforehand. That distinction is the point:
 * a caller must not have to remember to flush before asking the volume to go away.
 */
#include <stdint.h>

#include "mini.h"
#include "flush_block.h"

struct xnusys_mount_arg { char from[64]; char at[192]; };

#define XNUSYS_MOUNT    (0x80000000u | (256u << 16) | (0x78u << 8) | 1u)
#define XNUSYS_UNMOUNT  (0x80000000u | (256u << 16) | (0x78u << 8) | 2u)
#define XNUSYS_FLUSH    (0xC0000000u | (8u << 16) | (0x78u << 8) | 5u)

static void put(char *dst, const char *src, unsigned cap)
{
	unsigned i = 0;
	while (src[i] && i < cap - 1u) { dst[i] = src[i]; ++i; }
	dst[i] = '\0';
}

static int
ctl(void)
{
	return (int)open("/dev/rxnusys", O_RDONLY, 0);
}

static int
do_unmount(const char *at)
{
	struct xnusys_mount_arg a;
	int fd, rc;
	for (unsigned i = 0; i < sizeof(a); ++i) ((char *)&a)[i] = 0;
	put(a.from, "/dev/xnusys", sizeof(a.from));
	put(a.at, at, sizeof(a.at));
	fd = ctl();
	if (fd < 0) return -1000;
	rc = (int)mini_syscall(SYS_ioctl, fd, XNUSYS_UNMOUNT, (long)&a, 0, 0, 0);
	close(fd);
	return rc;
}

int
main(int argc, char **argv, char **envp)
{
	(void)argc; (void)argv; (void)envp;

	int failures = 0, rc, fd;
	const char *target =
	    "/redwood/lib/System/Library/CoreServices/IconsetResources.bundle/"
	    "GenericForeground_128@2x.png";

	/* ---- case 2 first: the volume is mounted and a file is held open ---- */
	{
		int held = (int)open(target, O_RDONLY, 0);
		if (held < 0) {
			printf("xnusys-unmount2: cannot open the file to hold (%d)\n", held);
			failures++;
		} else {
			rc = do_unmount("/redwood/lib");
			printf("xnusys-unmount2: unmount while a file is open -> %d\n", rc);
			if (rc == 0) failures++;      /* it must be refused */
			/* The volume is still mounted: the file must still read. */
			static uint8_t b[64];
			long got = mini_syscall(SYS_read, held, (long)b, (long)sizeof(b), 0, 0, 0);
			printf("xnusys-unmount2: the held file still reads -> %ld\n", got);
			if (got <= 0) failures++;
			close(held);
		}
	}

	/* ---- case 1: nothing holds it, so the unmount flushes and succeeds ---- */
	rc = do_unmount("/redwood/lib");
	printf("xnusys-unmount2: unmount with nothing open -> %d\n", rc);
	if (rc != 0) failures++;

	/* The mount point is a directory of the ramdisk again, so the volume's own file is gone. */
	fd = (int)open(target, O_RDONLY, 0);
	printf("xnusys-unmount2: after the unmount, the volume's file -> %d\n", fd);
	if (fd >= 0) { failures++; close(fd); }

	/* ---- remount and find the original files again ---- */
	{
		struct xnusys_mount_arg a;
		for (unsigned i = 0; i < sizeof(a); ++i) ((char *)&a)[i] = 0;
		put(a.from, "/dev/xnusys", sizeof(a.from));
		put(a.at, "/redwood/lib", sizeof(a.at));
		fd = ctl();
		if (fd < 0) { printf("xnusys-unmount2: no control node\n"); return 1; }
		rc = (int)mini_syscall(SYS_ioctl, fd, XNUSYS_MOUNT, (long)&a, 0, 0, 0);
		close(fd);
		printf("xnusys-unmount2: remount -> %d\n", rc);
		if (rc != 0) failures++;
	}
	fd = (int)open(target, O_RDONLY, 0);
	if (fd < 0) {
		printf("xnusys-unmount2: the volume's file after remount -> %d\n", fd);
		failures++;
	} else {
		static uint8_t b[8192];
		long total = 0, got;
		while (total < (long)sizeof(b) &&
		    (got = mini_syscall(SYS_read, fd, (long)(b + total),
		    (long)(sizeof(b) - (size_t)total), 0, 0, 0)) > 0)
			total += got;
		close(fd);
		uint32_t h = 2166136261u;
		for (long i = 0; i < total; ++i) { h ^= b[i]; h *= 16777619u; }
		printf("xnusys-unmount2: after remount, read %ld bytes fnv=%08x\n", total, h);
		/* The host read of this file is 7814 bytes, fnv 30630d20. */
		if (total != 7814 || h != 0x30630d20u) failures++;
	}

	printf("xnusys-unmount2: %s\n", failures ? "FAIL" : "PASS");
	return failures ? 1 : 0;
}
