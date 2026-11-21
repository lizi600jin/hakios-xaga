/*
 * Trial746, stage 5: what a cold start costs to read one real resource.
 *
 * The resource is not a test file. It is a 19577-byte PNG out of the system image's
 * IconsetResources.bundle -- five blocks, uncompressed, so every byte of it comes off the flash
 * through the mounted volume. The host read the same file independently and got fnv1a bbdbcb6f.
 *
 * Nothing here knows where the bytes are: it opens a path, reads, and asks the device afterwards
 * what that cost. The device's own counters are the evidence -- the program cannot inflate them.
 */
#include <stdint.h>

#include "mini.h"

struct xnusys_stats_arg {
	uint64_t first_read_us;     /* registration to the first read that returned data */
	uint64_t reads;
	uint64_t blocks;
};

#define XNUSYS_STATS (0x40000000u | (24u << 16) | (0x78u << 8) | 3u)

static const char *const target =
    "/redwood/lib/System/Library/CoreServices/IconsetResources.bundle/GenericForeground_256@2x.png";

int
main(int argc, char **argv, char **envp)
{
	(void)argc; (void)argv; (void)envp;

	static uint8_t data[32768];
	long total = 0, got;
	int fd;

	fd = (int)open(target, O_RDONLY, 0);
	if (fd < 0) {
		printf("xnusys-coldstart: cannot open the resource (%d)\n", fd);
		return 1;
	}

	while (total < (long)sizeof(data) &&
	    (got = mini_syscall(SYS_read, fd, (long)(data + total),
	    (long)(sizeof(data) - (size_t)total), 0, 0, 0)) > 0)
		total += got;
	close(fd);

	uint32_t h = 2166136261u;
	for (long i = 0; i < total; ++i) { h ^= data[i]; h *= 16777619u; }

	printf("xnusys-coldstart: read %ld bytes, fnv=%08x\n", total, h);
	printf("xnusys-coldstart: head=%c%c%c%c%c%c%c%c\n", data[0], data[1], data[2], data[3],
	    data[4], data[5], data[6], data[7]);

	/* What the device says it cost. */
	{
		struct xnusys_stats_arg st;
		int cfd = (int)open("/dev/rxnusys", O_RDONLY, 0);
		if (cfd < 0) {
			printf("xnusys-coldstart: no control node\n");
			return 1;
		}
		int rc = (int)mini_syscall(SYS_ioctl, cfd, XNUSYS_STATS, (long)&st, 0, 0, 0);
		close(cfd);
		if (rc != 0) {
			printf("xnusys-coldstart: stats failed (%d)\n", rc);
			return 1;
		}
		printf("xnusys-coldstart: device says first=%llu us, reads=%llu, blocks=%llu\n",
		    (unsigned long long)st.first_read_us, (unsigned long long)st.reads,
		    (unsigned long long)st.blocks);
	}

	int ok = (total == 19577) && (h == 0xbbdbcb6fu);
	printf("xnusys-coldstart: %s\n", ok ? "PASS" : "FAIL");
	return ok ? 0 : 1;
}
