/*
 * varstat: what does this kernel say /private/var has?
 *
 * The Settings app shows "Available: Zero KB" while the volume header says 63.63 GiB free and
 * 382 MiB in use -- and the system writes to it without complaining. So either statfs is
 * reporting something the display layer cannot use, or the display layer is misreading a value
 * that is fine. Those are different bugs and only the kernel's own answer separates them.
 *
 * Prints the raw words, not a formatted sentence: the point is to see the numbers statfs
 * actually returned, in units the C code used, without this tool doing arithmetic on them.
 *
 *   varstat /private/var
 *   varstat /          (for comparison: a volume whose size displays correctly)
 */

#include "mini.h"

int
main(int argc, char **argv, char **envp)
{
	struct statfs fs;
	const char *path;
	int rc;

	(void)envp;
	path = (argc >= 2) ? argv[1] : "/private/var";

	rc = statfs(path, &fs);
	printf("varstat: statfs(\"%s\") returned %d\n", path, rc);
	if (rc != 0) {
		printf("varstat: errno %d (%s)\n", errno, strerror(errno));
		return 1;
	}

	printf("varstat: f_bsize   %ld\n", (long)fs.f_bsize);
	printf("varstat: f_iosize  %ld\n", (long)fs.f_iosize);
	printf("varstat: f_blocks  %ld\n", (long)fs.f_blocks);
	printf("varstat: f_bfree   %ld\n", (long)fs.f_bfree);
	printf("varstat: f_bavail  %ld\n", (long)fs.f_bavail);
	printf("varstat: f_files   %ld\n", (long)fs.f_ffree ? (long)fs.f_files : (long)fs.f_files);
	printf("varstat: f_ffree   %ld\n", (long)fs.f_ffree);
	printf("varstat: f_flags   %ld (0x%lx)\n", (long)fs.f_flags, (unsigned long)fs.f_flags);
	printf("varstat: f_type    %d\n", (int)fs.f_type);
	printf("varstat: fstypename %s\n", fs.f_fstypename);
	printf("varstat: mntonname  %s\n", fs.f_mntonname);
	printf("varstat: mntfromname %s\n", fs.f_mntfromname);
	printf("varstat: owner     %u\n", (unsigned)fs.f_owner);

	/* The arithmetic the display layer is supposed to do, done here so the two can be compared. */
	printf("varstat: bytes total %lld\n", (long long)fs.f_blocks * (long long)fs.f_bsize);
	printf("varstat: bytes free  %lld\n", (long long)fs.f_bavail * (long long)fs.f_bsize);
	return 0;
}
