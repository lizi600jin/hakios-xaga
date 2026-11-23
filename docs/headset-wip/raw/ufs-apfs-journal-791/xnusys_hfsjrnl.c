/*
 * Trial791, step 1a: let the kernel create the journal, then look at what it wrote.
 *
 * The volume carries attributes bit 13 (journaled) and JournalInfoBlock.flags =
 * kJIJournalInFSMask | kJIJournalNeedInitMask. A read-only mount refuses that combination
 * ("journal is dirty"), because creating the journal is a write. A writable mount runs
 * hfs_late_journal_init(), which checks that .journal and .journal_info_block are where the
 * volume header says, and then calls journal_create() to write the header itself.
 *
 * So this one mounts read-write, writes a file, unmounts, and then reports the three things that
 * say it worked: whether jnl was non-null at mount, whether the unmount returned, and what the
 * volume header and JournalInfoBlock look like on the flash afterwards. The journal header lives
 * at journal start (block 4), so magic 0x4a4e4c78 there means journal_create() ran.
 */
#include <stdint.h>

#include "mini.h"

#define SYS_unmount  159

#define DIR  "/private/var/tmp/hfs-jrnl"
#define FILE_ DIR "/jrnl.txt"
#define TEXT "Trial791: the journal was created by the kernel.\n"

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

/* The volume header and the JournalInfoBlock, straight off the device. Not while mounted. */
static void report_flash(const char *when)
{
	static uint8_t blk[4096];
	int fd; long got;
	uint32_t attrs, jflags;
	char last[5];

	fd = (int)open("/dev/xnusys_var", O_RDONLY, 0);
	if (fd < 0) { printf("jrnl: %s: cannot open the device\n", when); return; }
	if (mini_syscall(SYS_lseek, fd, 0, 0, 0, 0, 0) < 0) { close(fd); return; }
	got = mini_syscall(SYS_read, fd, (long)blk, 4096, 0, 0, 0);
	close(fd);
	if (got != 4096) { printf("jrnl: %s: short read\n", when); return; }

	{
		const uint8_t *vh = blk + 1024;
		attrs = ((uint32_t)vh[4] << 24) | ((uint32_t)vh[5] << 16) |
		        ((uint32_t)vh[6] << 8) | vh[7];
		for (int i = 0; i < 4; i++) last[i] = (char)vh[8 + i];
		last[4] = 0;
	}
	printf("jrnl: %s: volume header attributes=0x%08x Journaled=%d Unmounted=%d last=%s\n",
	    when, attrs, (attrs >> 13) & 1, (attrs >> 8) & 1, last);

	/* the JournalInfoBlock is allocation block 3, at 3 * 4096 */
	fd = (int)open("/dev/xnusys_var", O_RDONLY, 0);
	if (fd < 0) return;
	if (mini_syscall(SYS_lseek, fd, 3 * 4096, 0, 0, 0, 0) < 0) { close(fd); return; }
	got = mini_syscall(SYS_read, fd, (long)blk, 4096, 0, 0, 0);
	close(fd);
	if (got != 4096) return;
	jflags = ((uint32_t)blk[0] << 24) | ((uint32_t)blk[1] << 16) |
	         ((uint32_t)blk[2] << 8) | blk[3];
	printf("jrnl: %s: JournalInfoBlock flags=0x%08x InFS=%d NeedInit=%d\n",
	    when, jflags, jflags & 1, (jflags >> 2) & 1);

	/* the journal header is at the journal start, recorded in the JIB at offset 36 */
	{
		uint64_t jstart = 0;
		for (int i = 0; i < 8; i++) jstart = (jstart << 8) | blk[36 + i];
		fd = (int)open("/dev/xnusys_var", O_RDONLY, 0);
		if (fd < 0) return;
		if (mini_syscall(SYS_lseek, fd, (long)jstart, 0, 0, 0, 0) < 0) { close(fd); return; }
		got = mini_syscall(SYS_read, fd, (long)blk, 4096, 0, 0, 0);
		close(fd);
		if (got != 4096) return;
		{
			uint32_t magic = ((uint32_t)blk[0] << 24) | ((uint32_t)blk[1] << 16) |
			                 ((uint32_t)blk[2] << 8) | blk[3];
			uint32_t cksum = ((uint32_t)blk[4] << 24) | ((uint32_t)blk[5] << 16) |
			                 ((uint32_t)blk[6] << 8) | blk[7];
			uint32_t start = ((uint32_t)blk[24] << 24) | ((uint32_t)blk[25] << 16) |
			                 ((uint32_t)blk[26] << 8) | blk[27];
			uint32_t end   = ((uint32_t)blk[28] << 24) | ((uint32_t)blk[29] << 16) |
			                 ((uint32_t)blk[30] << 8) | blk[31];
			uint32_t seq   = ((uint32_t)blk[32] << 24) | ((uint32_t)blk[33] << 16) |
			                 ((uint32_t)blk[34] << 8) | blk[35];
			printf("jrnl: %s: journal header @%llu magic=0x%08x (want 0x4a4e4c78) "
			       "checksum=0x%08x start=%u end=%u seq=0x%x\n",
			       when, (unsigned long long)jstart, magic, cksum, start, end, seq);
		}
	}
}

int
main(int argc, char **argv, char **envp)
{
	(void)argc; (void)argv; (void)envp;

	long rc; int fd, saved, failures = 0;

	printf("jrnl: --- start ---\n");
	report_flash("before");

	rc = mini_syscall(SYS_mkdir, (long)DIR, 0755, 0, 0, 0, 0);
	saved = errno;
	if (rc != 0 && saved != 17) { printf("jrnl: FAIL (mkdir)\n"); return 1; }

	/* [1] writable mount: this is what runs the late journal init */
	memset(&args, 0, sizeof(args));
	args.fspec = "/dev/xnusys_var";
	rc = mini_syscall(SYS_mount, (long)"hfs", (long)DIR, 0, (long)&args, 0, 0);
	saved = errno;
	printf("jrnl: [1] mount rw -> %ld", rc);
	if (rc != 0) { printf("  errno=%d   FAIL\njrnl: FAIL\n", saved); return 1; }
	printf("   OK\n");

	/* [2] a write, so the journal has something to commit */
	fd = (int)open(FILE_, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	printf("jrnl: [2] open -> %d\n", fd);
	if (fd < 0) { failures++; }
	else {
		long n = mini_syscall(SYS_write, fd, (long)TEXT, (long)(sizeof(TEXT) - 1), 0, 0, 0);
		printf("jrnl:     write %lu -> %ld\n", (unsigned long)(sizeof(TEXT) - 1), n);
		if (n != (long)(sizeof(TEXT) - 1)) failures++;
		printf("jrnl:     fsync -> %ld\n", mini_syscall(SYS_fsync, fd, 0, 0, 0, 0, 0));
		close(fd);
	}

	/* [3] unmount: with a journal this goes through a transaction */
	printf("jrnl: [3] unmount ...\n");
	rc = mini_syscall(SYS_unmount, (long)DIR, 0, 0, 0, 0, 0);
	saved = errno;
	printf("jrnl:     unmount -> %ld", rc);
	if (rc != 0) { printf("  errno=%d   FAIL", saved); failures++; }
	printf("\n");

	/* [4] what the kernel left on the flash */
	report_flash("after");

	/* [5] mount again without touching the volume: the journal should already be there */
	rc = mini_syscall(SYS_mount, (long)"hfs", (long)DIR, 0, (long)&args, 0, 0);
	saved = errno;
	printf("jrnl: [5] remount rw -> %ld", rc);
	if (rc != 0) { printf("  errno=%d   FAIL", saved); failures++; }
	printf("\n");

	if (rc == 0) {
		fd = (int)open(FILE_, O_RDONLY, 0);
		if (fd >= 0) {
			long n = mini_syscall(SYS_read, fd, (long)buf, sizeof(buf) - 1, 0, 0, 0);
			if (n > 0) { buf[n] = 0; printf("jrnl:     content = %s", buf); }
			close(fd);
		}
		rc = mini_syscall(SYS_unmount, (long)DIR, 0, 0, 0, 0, 0);
		printf("jrnl:     unmount -> %ld\n", rc);
	}

	printf("jrnl: %s\n", failures ? "FAIL" : "PASS");
	printf("jrnl: --- end ---\n");
	return failures ? 1 : 0;
}
