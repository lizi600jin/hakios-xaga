/*
 * Trial788: the A/B that Astra asked for.
 *
 * Trial785c mounted this volume read-only, read 56 bytes of a regular file, and then hung in
 * unmount -- no "hfs: unmount initiated" ever appeared, so the hang was above the file system,
 * in dounmount's own work. Trial786, which opened only a directory, unmounted cleanly.
 *
 * The difference between them is a UPL: reading a regular file goes through cluster_read, which
 * gives the driver a clustered buffer whose mapping buf_map() creates and buf_unmap() must undo.
 * The driver mapped and never unmapped, so each read left a reference on the file's VM object and
 * paging_in_progress never returned to zero. vclean() waits for exactly that.
 *
 * So this is that read, kept byte for byte, with the unmap now in place. If the fix is right the
 * unmount returns; if it hangs again the leak was not the whole story.
 */
#include <stdint.h>

#include "mini.h"

#define SYS_unmount  159

#define DIR   "/private/var/tmp/hfs-ab"
#define FILE_ DIR "/probe.txt"

struct margs {
	const char *fspec;
	uint32_t uid, gid;
	uint16_t mask, pad;
	uint32_t encoding;
	int tz[2];
	int flags, journal_tbuffer_size, journal_flags, journal_disable;
};

static struct margs args;
static char buf[512];

int
main(int argc, char **argv, char **envp)
{
	(void)argc; (void)argv; (void)envp;

	long rc;
	int fd, saved, failures = 0;

	printf("hfs-ab: --- start ---\n");

	rc = mini_syscall(SYS_mkdir, (long)DIR, 0755, 0, 0, 0, 0);
	saved = errno;
	if (rc != 0 && saved != 17) { printf("hfs-ab: FAIL (mkdir)\n"); return 1; }

	/* read-only mount, exactly as 785c did */
	memset(&args, 0, sizeof(args));
	args.fspec = "/dev/xnusys_var";
	rc = mini_syscall(SYS_mount, (long)"hfs", (long)DIR, MNT_RDONLY, (long)&args, 0, 0);
	saved = errno;
	printf("hfs-ab: mount ro -> %ld", rc);
	if (rc != 0) { printf("  errno=%d\nhfs-ab: FAIL\n", saved); return 1; }
	printf("\n");

	/* open and read a regular file: this is what built the UPL */
	fd = (int)open(FILE_, O_RDONLY, 0);
	printf("hfs-ab: open %s -> %d", FILE_, fd);
	if (fd < 0) { printf("   FAIL\nhfs-ab: FAIL\n", 0); return 1; }
	printf("\n");

	{
		long n = mini_syscall(SYS_read, fd, (long)buf, 56, 0, 0, 0);
		printf("hfs-ab: read 56 -> %ld", n);
		if (n > 0) {
			buf[n] = 0;
			printf("\n");
			printf("hfs-ab: content = %s", buf);
		} else {
			printf("   FAIL\n");
			failures++;
		}
	}
	{
		long cr = close(fd);
		printf("hfs-ab: close -> %ld\n", cr);
	}

	/* everything that matters is printed before the call that used to hang */
	printf("hfs-ab: calling unmount -- this is where 785c stopped\n");

	rc = mini_syscall(SYS_unmount, (long)DIR, 0, 0, 0, 0, 0);
	saved = errno;
	printf("hfs-ab: unmount -> %ld", rc);
	if (rc != 0) { printf("  errno=%d", saved); failures++; }
	printf("\n");

	printf("hfs-ab: %s\n", failures ? "FAIL" : "PASS");
	printf("hfs-ab: --- end ---\n");
	return failures ? 1 : 0;
}
