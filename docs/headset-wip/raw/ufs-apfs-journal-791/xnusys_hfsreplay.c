/*
 * Trial792b: mount a volume that was never unmounted, and see if the journal saved it.
 *
 * Trial792a wrote a file and stopped without unmounting, so the volume on the flash says
 * "in use" and its journal holds a transaction that was never checkpointed. A volume without a
 * journal would refuse to mount read-write in that state ("cannot mount dirty non-journaled
 * volumes"). With one, the mount runs journal_open(), which finds the uncommitted transaction,
 * replays it, and only then reports success.
 *
 * The question this answers is the only one that matters for /private/var: does the data survive
 * a system that ended without warning?
 */
#include <stdint.h>

#include "mini.h"

#define SYS_unmount  159

#define DIR  "/private/var/tmp/hfs-replay"
#define FILE_ DIR "/dirty.txt"
#define WANT "Trial792a: written but never unmounted.\n"

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

static void report(const char *when)
{
	static uint8_t blk[4096];
	int fd; long got;

	fd = (int)open("/dev/xnusys_var", O_RDONLY, 0);
	if (fd < 0) { printf("replay: %s: cannot open the device\n", when); return; }
	if (mini_syscall(SYS_lseek, fd, 0, 0, 0, 0, 0) < 0) { close(fd); return; }
	got = mini_syscall(SYS_read, fd, (long)blk, 4096, 0, 0, 0);
	close(fd);
	if (got != 4096) return;
	{
		uint32_t a = ((uint32_t)blk[1024+4] << 24) | ((uint32_t)blk[1024+5] << 16) |
		             ((uint32_t)blk[1024+6] << 8) | blk[1024+7];
		uint32_t fc = ((uint32_t)blk[1024+32] << 24) | ((uint32_t)blk[1024+33] << 16) |
		              ((uint32_t)blk[1024+34] << 8) | blk[1024+35];
		printf("replay: %s: attributes=0x%08x Journaled=%d Unmounted=%d fileCount=%u\n",
		    when, a, (a >> 13) & 1, (a >> 8) & 1, fc);
	}
}

static void report_header(const char *when)
{
	static uint8_t blk[4096];
	int fd; long got;

	fd = (int)open("/dev/xnusys_var", O_RDONLY, 0);
	if (fd < 0) return;
	if (mini_syscall(SYS_lseek, fd, 16384, 0, 0, 0, 0) < 0) { close(fd); return; }
	got = mini_syscall(SYS_read, fd, (long)blk, 4096, 0, 0, 0);
	close(fd);
	if (got != 4096) return;
	{
		uint32_t magic = blk[0] | (blk[1] << 8) | (blk[2] << 16) | ((uint32_t)blk[3] << 24);
		int64_t js = 0, je = 0;
		for (int i = 7; i >= 0; i--) js = (js << 8) | blk[8 + i];
		for (int i = 7; i >= 0; i--) je = (je << 8) | blk[16 + i];
		printf("replay: %s: journal magic=0x%08x start=%lld end=%lld %s\n",
		    when, magic, (long long)js, (long long)je,
		    js != je ? "not checkpointed" : "checkpointed");
	}
}

int
main(int argc, char **argv, char **envp)
{
	(void)argc; (void)argv; (void)envp;

	long rc; int fd, saved, failures = 0;

	printf("replay: --- start ---\n");
	report("before");
	report_header("before");

	rc = mini_syscall(SYS_mkdir, (long)DIR, 0755, 0, 0, 0, 0);
	saved = errno;
	if (rc != 0 && saved != 17) { printf("replay: FAIL (mkdir)\n"); return 1; }

	/* the mount that has to replay the journal */
	memset(&args, 0, sizeof(args));
	args.fspec = "/dev/xnusys_var";
	rc = mini_syscall(SYS_mount, (long)"hfs", (long)DIR, 0, (long)&args, 0, 0);
	saved = errno;
	printf("replay: [1] mount rw -> %ld", rc);
	if (rc != 0) {
		printf("  errno=%d   FAIL (a journaled volume should have mounted)\n", saved);
		printf("replay: FAIL\nreplay: --- end ---\n");
		return 1;
	}
	printf("   OK -- it mounted a volume that was never unmounted\n");

	/* the file 792a wrote, without unmounting */
	fd = (int)open(FILE_, O_RDONLY, 0);
	printf("replay: [2] open %s -> %d\n", FILE_, fd);
	if (fd < 0) {
		printf("replay:     the file 792a wrote is gone   FAIL\n");
		failures++;
	} else {
		long n = mini_syscall(SYS_read, fd, (long)buf, sizeof(buf) - 1, 0, 0, 0);
		printf("replay:     read -> %ld\n", n);
		if (n == (long)(sizeof(WANT) - 1)) {
			buf[n] = 0;
			int same = 1;
			for (long i = 0; i < n; i++) if (buf[i] != WANT[i]) { same = 0; break; }
			printf("replay:     content = %s", buf);
			if (same) printf("replay:     it matches what 792a wrote   PASS\n");
			else      { printf("replay:     content differs   FAIL\n"); failures++; }
		} else {
			printf("replay:     read %ld bytes, wanted %lu   FAIL\n",
			    n, (unsigned long)(sizeof(WANT) - 1));
			failures++;
		}
		close(fd);
	}

	/* the unmount that should checkpoint the journal and mark the volume clean */
	printf("replay: [3] unmount ...\n");
	rc = mini_syscall(SYS_unmount, (long)DIR, 0, 0, 0, 0, 0);
	saved = errno;
	printf("replay:     unmount -> %ld", rc);
	if (rc != 0) { printf("  errno=%d   FAIL", saved); failures++; }
	printf("\n");

	report("after");
	report_header("after");

	printf("replay: %s\n", failures ? "FAIL" : "PASS");
	printf("replay: --- end ---\n");
	return failures ? 1 : 0;
}
