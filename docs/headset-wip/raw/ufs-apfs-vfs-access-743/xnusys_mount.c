/*
 * Trial741: ask the block device to mount the volume, once /redwood is up.
 *
 * This runs from /redwood/etc/rc, which is read after the ramdisk is mounted, so the mount point
 * it names is a directory that exists. The kernel side was measured in Trial740 to fail from
 * bsd_init for exactly that reason -- ENOENT, no mount point yet.
 */
#include <stdint.h>
#include "mini.h"

struct xnusys_mount_arg {
	char from[64];
	char at[192];
};

/*
 * _IOW('x', 1, struct xnusys_mount_arg), spelled out: group 'x' = 0x78, one argument of 256
 * bytes, write direction. The kernel side uses the same _IOW, so the numbers must agree.
 */
#define XNUSYS_MOUNT (0x80000000u | (256u << 16) | (0x78u << 8) | 1u)

int
main(int argc, char **argv, char **envp)
{
	(void)argc; (void)argv; (void)envp;
	struct xnusys_mount_arg a;
	int fd, rc;

	for (unsigned i = 0; i < sizeof(a); ++i) ((char *)&a)[i] = 0;
	/* The volume is on the block node; this program talks to the character one. */
	{
		const char *f = "/dev/xnusys";
		for (unsigned i = 0; f[i] && i < sizeof(a.from) - 1u; ++i) a.from[i] = f[i];
	}
	{
		const char *t = "/redwood/lib";
		for (unsigned i = 0; t[i] && i < sizeof(a.at) - 1u; ++i) a.at[i] = t[i];
	}

	fd = (int)open("/dev/rxnusys", O_RDONLY, 0);
	if (fd < 0) {
		printf("xnusys-mount: no /dev/rxnusys (%d)\n", fd);
		return 1;
	}
	rc = (int)mini_syscall(SYS_ioctl, fd, XNUSYS_MOUNT, (long)&a, 0, 0, 0);
	close(fd);
	printf("xnusys-mount: mount %s on %s -> %d\n", a.from, a.at, rc);
	return rc == 0 ? 0 : 1;
}
