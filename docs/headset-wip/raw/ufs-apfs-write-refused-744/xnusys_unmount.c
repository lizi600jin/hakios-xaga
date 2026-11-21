/*
 * Trial744: the two things stage 4 asks for that the earlier runs did not measure --
 * a write open being refused, and the volume coming back off.
 *
 * The write test tries three ways in: opening the block node for writing, opening a file of the
 * mounted volume for writing, and writing to a descriptor obtained read-only. Each must fail; the
 * point is not that one of them does.
 */
#include <stdint.h>

#include "mini.h"

struct xnusys_mount_arg { char from[64]; char at[192]; };

#define XNUSYS_UNMOUNT (0x80000000u | (256u << 16) | (0x78u << 8) | 2u)

static void put(char *dst, const char *src, unsigned cap)
{
	unsigned i = 0;
	while (src[i] && i < cap - 1u) { dst[i] = src[i]; ++i; }
	dst[i] = '\0';
}

int
main(int argc, char **argv, char **envp)
{
	(void)argc; (void)argv; (void)envp;

	int failures = 0;
	int fd, rc;

	/* 1. the block device refuses a write open */
	fd = (int)open("/dev/xnusys", O_WRONLY, 0);
	printf("xnusys-unmount: open block node for write -> %d\n", fd);
	if (fd >= 0) { failures++; close(fd); }

	/* 2. so does a file on the mounted volume */
	fd = (int)open("/redwood/lib/usr/libexec/GKCentralCache.momd/VersionInfo.plist",
	    O_WRONLY, 0);
	printf("xnusys-unmount: open a mounted file for write -> %d\n", fd);
	if (fd >= 0) { failures++; close(fd); }

	/* 3. and a read-only descriptor will not take a write */
	fd = (int)open("/redwood/lib/usr/libexec/GKCentralCache.momd/VersionInfo.plist",
	    O_RDONLY, 0);
	if (fd < 0) {
		printf("xnusys-unmount: cannot open the file to try (-)\n");
		failures++;
	} else {
		char c = 'x';
		rc = (int)mini_syscall(SYS_write, fd, (long)&c, 1, 0, 0, 0);
		printf("xnusys-unmount: write through a read-only fd -> %d\n", rc);
		if (rc >= 0) failures++;
		close(fd);
	}

	/* 4. unmount, then show the path is gone again */
	{
		struct xnusys_mount_arg a;
		printf("xnusys-unmount: reached the unmount step\n");
		for (unsigned i = 0; i < sizeof(a); ++i) ((char *)&a)[i] = 0;
		put(a.from, "/dev/xnusys", sizeof(a.from));
		put(a.at, "/redwood/lib", sizeof(a.at));

		fd = (int)open("/dev/rxnusys", O_RDONLY, 0);
		printf("xnusys-unmount: control node -> %d\n", fd);
		if (fd < 0) {
			printf("xnusys-unmount: no control node\n");
			return 1;
		}
		rc = (int)mini_syscall(SYS_ioctl, fd, XNUSYS_UNMOUNT, (long)&a, 0, 0, 0);
		close(fd);
		printf("xnusys-unmount: unmount -> %d\n", rc);
		if (rc != 0) failures++;

		/* The file that was readable a moment ago must not be now. */
		fd = (int)open("/redwood/lib/usr/libexec/GKCentralCache.momd/VersionInfo.plist",
		    O_RDONLY, 0);
		printf("xnusys-unmount: after unmount, open the file -> %d\n", fd);
		if (fd >= 0) { failures++; close(fd); }
	}

	printf("xnusys-unmount: %s\n", failures ? "FAIL" : "PASS");
	return failures ? 1 : 0;
}
