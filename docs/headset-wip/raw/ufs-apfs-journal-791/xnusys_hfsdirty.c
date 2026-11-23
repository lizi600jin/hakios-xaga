/*
 * Trial792a: leave the volume dirty on purpose.
 *
 * Trial791 proved the kernel creates the journal when it is told to, and that a mount takes the
 * journal_open path afterwards. What it has not proved is the thing a journal exists for: that a
 * volume which was never cleanly unmounted still mounts, and comes back with its data.
 *
 * So this one does the first half. It mounts read-write, writes a file, fsyncs it, prints
 * everything that matters, and then stops -- no unmount. The loader's return-after will end the
 * system with the volume still marked in use, which is exactly the state a real power loss leaves.
 *
 * Everything that has to be known is printed before the mount is abandoned; the next boot reads
 * the rest off the flash.
 */
#include <stdint.h>

#include "mini.h"

#define DIR  "/private/var/tmp/hfs-dirty"
#define FILE_ DIR "/dirty.txt"
#define TEXT "Trial792a: written but never unmounted.\n"

struct margs {
	const char *fspec;
	uint32_t uid, gid;
	uint16_t mask, pad;
	uint32_t encoding;
	int tz[2];
	int flags, journal_tbuffer_size, journal_flags, journal_disable;
};

static struct margs args;

/* The journal header at the start of the journal, as the flash has it. */
static void report_journal(const char *when)
{
	static uint8_t blk[4096];
	int fd; long got;

	fd = (int)open("/dev/xnusys_var", O_RDONLY, 0);
	if (fd < 0) { printf("dirty: %s: cannot open the device\n", when); return; }
	if (mini_syscall(SYS_lseek, fd, 3 * 4096, 0, 0, 0, 0) < 0) { close(fd); return; }
	got = mini_syscall(SYS_read, fd, (long)blk, 4096, 0, 0, 0);
	close(fd);
	if (got != 4096) return;
	{
		uint32_t jflags = ((uint32_t)blk[0] << 24) | ((uint32_t)blk[1] << 16) |
		                  ((uint32_t)blk[2] << 8) | blk[3];
		uint64_t jstart = 0;
		uint32_t vh_attrs = 0;
		for (int i = 0; i < 8; i++) jstart = (jstart << 8) | blk[36 + i];

		printf("dirty: %s: JIB flags=0x%08x InFS=%d NeedInit=%d journalStart=%llu\n",
		    when, jflags, jflags & 1, (jflags >> 2) & 1, (unsigned long long)jstart);

		/* volume header attributes, block 0 offset 1024 */
		fd = (int)open("/dev/xnusys_var", O_RDONLY, 0);
		if (fd >= 0) {
			if (mini_syscall(SYS_lseek, fd, 0, 0, 0, 0, 0) == 0 &&
			    mini_syscall(SYS_read, fd, (long)blk, 4096, 0, 0, 0) == 4096) {
				vh_attrs = ((uint32_t)blk[1024+4] << 24) | ((uint32_t)blk[1024+5] << 16) |
				           ((uint32_t)blk[1024+6] << 8) | blk[1024+7];
				printf("dirty: %s: volume attributes=0x%08x Journaled=%d Unmounted=%d\n",
				    when, vh_attrs, (vh_attrs >> 13) & 1, (vh_attrs >> 8) & 1);
			}
			close(fd);
		}

		/* the journal header itself */
		fd = (int)open("/dev/xnusys_var", O_RDONLY, 0);
		if (fd >= 0 && mini_syscall(SYS_lseek, fd, (long)jstart, 0, 0, 0, 0) == 0) {
			if (mini_syscall(SYS_read, fd, (long)blk, 4096, 0, 0, 0) == 4096) {
				/* fields are host byte order: magic, endian, start(8), end(8) */
				uint32_t magic = blk[0] | (blk[1] << 8) | (blk[2] << 16) | ((uint32_t)blk[3] << 24);
				int64_t  js = 0, je = 0;
				for (int i = 7; i >= 0; i--) js = (js << 8) | blk[8 + i];
				for (int i = 7; i >= 0; i--) je = (je << 8) | blk[16 + i];
				printf("dirty: %s: journal header magic=0x%08x start=%lld end=%lld %s\n",
				    when, magic, (long long)js, (long long)je,
				    js != je ? "HOME (uncheckpointed transaction)" : "checkpointed");
			}
			close(fd);
		}
	}
}

int
main(int argc, char **argv, char **envp)
{
	(void)argc; (void)argv; (void)envp;

	long rc; int fd, saved;

	printf("dirty: --- start ---\n");
	report_journal("before");

	rc = mini_syscall(SYS_mkdir, (long)DIR, 0755, 0, 0, 0, 0);
	saved = errno;
	if (rc != 0 && saved != 17) { printf("dirty: FAIL (mkdir)\n"); return 1; }

	/* read-write mount: with the journal already created this goes through journal_open */
	memset(&args, 0, sizeof(args));
	args.fspec = "/dev/xnusys_var";
	rc = mini_syscall(SYS_mount, (long)"hfs", (long)DIR, 0, (long)&args, 0, 0);
	saved = errno;
	printf("dirty: mount rw -> %ld", rc);
	if (rc != 0) { printf("  errno=%d   FAIL\n", saved); return 1; }
	printf("   OK\n");

	fd = (int)open(FILE_, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	printf("dirty: open -> %d\n", fd);
	if (fd < 0) { printf("dirty: FAIL (open)\n"); return 1; }
	{
		long n = mini_syscall(SYS_write, fd, (long)TEXT, (long)(sizeof(TEXT) - 1), 0, 0, 0);
		printf("dirty: write %lu -> %ld\n", (unsigned long)(sizeof(TEXT) - 1), n);
		if (n != (long)(sizeof(TEXT) - 1)) { printf("dirty: FAIL (write)\n"); return 1; }
	}
	printf("dirty: fsync -> %ld\n", mini_syscall(SYS_fsync, fd, 0, 0, 0, 0, 0));
	close(fd);

	printf("dirty: no unmount on purpose -- the volume is left marked in use\n");
	printf("dirty: PASS (the next boot reads the rest off the flash)\n");
	printf("dirty: --- end ---\n");
	return 0;
}
