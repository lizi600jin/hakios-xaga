/*
 * Trial757: several readers at once, which is what a mounted volume actually sees.
 *
 * The page-in path can ask for blocks from more than one thread, and the lease underneath admits
 * one request at a time -- a second caller arriving while the first holds it is refused outright.
 * Before the lock in the driver that showed up as spurious read failures; a failing page-in is a
 * process that cannot start.
 *
 * This forks several children, each reading the same file through the mount over and over, and
 * checks that every one of them got the whole file every time.
 */
#include <stdint.h>

#include "mini.h"

#define NCHILD 4
#define ROUNDS 12

static const char *const target =
    "/redwood/lib/System/Library/CoreServices/IconsetResources.bundle/"
    "GenericForeground_128@2x.png";

/* Read the file once; 1 when it came back whole (7814 bytes, fnv 30630d20). */
static int
one_read(void)
{
	static uint8_t b[8192];
	int fd = (int)open(target, O_RDONLY, 0);
	long total = 0, got;

	if (fd < 0) return -1;
	while (total < (long)sizeof(b) &&
	    (got = mini_syscall(SYS_read, fd, (long)(b + total),
	    (long)(sizeof(b) - (size_t)total), 0, 0, 0)) > 0)
		total += got;
	close(fd);
	if (total != 7814) return -2;
	uint32_t h = 2166136261u;
	for (long i = 0; i < total; ++i) { h ^= b[i]; h *= 16777619u; }
	return h == 0x30630d20u ? 1 : -3;
}

int
main(int argc, char **argv, char **envp)
{
	(void)argc; (void)argv; (void)envp;

	int failures = 0;

	/* One first, so a broken mount is not blamed on concurrency. */
	printf("xnusys-conc: single reader -> %d\n", one_read());
	if (one_read() != 1) {
		printf("xnusys-conc: the volume does not read at all, nothing to test\n");
		return 1;
	}

	for (int c = 0; c < NCHILD; ++c) {
		pid_t pid = fork();
		if (pid == 0) {
			int bad = 0;
			for (int r = 0; r < ROUNDS; ++r) {
				if (one_read() != 1) bad++;
			}
			_exit(bad);
		}
		if (pid < 0) { printf("xnusys-conc: fork failed\n"); failures++; }
	}

	for (int c = 0; c < NCHILD; ++c) {
		int status = 0;
		if (mini_syscall(SYS_wait4, -1, (long)&status, 0, 0, 0, 0) > 0) {
			int bad = (status >> 8) & 0xff;
			printf("xnusys-conc: child done, %d of %d reads failed\n", bad, ROUNDS);
			if (bad != 0) failures++;
		}
	}

	printf("xnusys-conc: %d readers x %d reads each\n", NCHILD, ROUNDS);
	printf("xnusys-conc: %s\n", failures ? "FAIL" : "PASS");
	return failures ? 1 : 0;
}
