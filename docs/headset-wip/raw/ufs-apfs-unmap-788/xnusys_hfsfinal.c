/*
 * Trial790: the whole thing, in the order it has to happen.
 *
 * Trial789 ran two probes that both wanted the volume and got in each other's way; one of them
 * also tried to read the device while it was still mounted, which the device refuses (EBUSY) and
 * which told us nothing. This one does the sequence properly, and every number in it comes from
 * either statfs (the file system's own view) or the flash itself (the truth).
 *
 * Five questions, in order:
 *   1. does a writable mount still work on a clean volume?
 *   2. does a write allocate a block and does fsync commit it?
 *   3. does unmount return, or does it still hang?
 *   4. after unmount, is the clean bit set again in the volume header on the flash?
 *   5. does the file survive into a second mount?
 */
#include <stdint.h>

#include "mini.h"

#define SYS_unmount  159
#define SYS_statfs64 345

#define DIR  "/private/var/tmp/hfs-final"
#define FILE_ DIR "/final.txt"
#define TEXT "Trial790: a writable mount that ends with a clean unmount.\n"

struct margs {
	const char *fspec;
	uint32_t uid, gid;
	uint16_t mask, pad;
	uint32_t encoding;
	int tz[2];
	int flags, journal_tbuffer_size, journal_flags, journal_disable;
};

struct pst {
	uint32_t f_bsize; int32_t f_iosize;
	uint64_t f_blocks, f_bfree, f_bavail, f_files, f_ffree;
	int32_t f_fsid[2]; uint32_t f_owner, f_type, f_flags, f_fssubtype;
	char f_fstypename[16]; char f_mntonname[1024]; char f_mntfromname[1024];
	uint32_t f_reserved[8];
};

static struct margs args;
static struct pst st;
static char buf[512];
static int failures;

static long do_mount(int flags)
{
	memset(&args, 0, sizeof(args));
	args.fspec = "/dev/xnusys_var";
	return mini_syscall(SYS_mount, (long)"hfs", (long)DIR, flags, (long)&args, 0, 0);
}

static uint64_t free_blocks(void)
{
	memset(&st, 0, sizeof(st));
	if (mini_syscall(SYS_statfs64, (long)DIR, (long)sizeof(st) ? (long)&st : 0,
	    (long)sizeof(st), 0, 0, 0) != 0)
		return ~0ULL;
	return st.f_bfree;
}

/* The volume header as the flash has it. Only valid while nothing is mounted. */
static int flash_header(uint32_t *attrs, uint32_t *freeblocks, char *last)
{
	static uint8_t blk[4096];
	int fd; long got;

	fd = (int)open("/dev/xnusys_var", O_RDONLY, 0);
	if (fd < 0) return -1;
	if (mini_syscall(SYS_lseek, fd, 0, 0, 0, 0, 0) < 0) { close(fd); return -1; }
	got = mini_syscall(SYS_read, fd, (long)blk, 4096, 0, 0, 0);
	close(fd);
	if (got != 4096) return -1;
	{
		const uint8_t *vh = blk + 1024;
		*attrs = ((uint32_t)vh[4] << 24) | ((uint32_t)vh[5] << 16) |
		         ((uint32_t)vh[6] << 8) | vh[7];
		*freeblocks = ((uint32_t)vh[48] << 24) | ((uint32_t)vh[49] << 16) |
		              ((uint32_t)vh[50] << 8) | vh[51];
		for (int i = 0; i < 4; i++) last[i] = (char)vh[8 + i];
	}
	return 0;
}

int
main(int argc, char **argv, char **envp)
{
	(void)argc; (void)argv; (void)envp;

	long rc; int fd, saved;
	uint64_t fb_before, fb_after;
	uint32_t a; uint32_t fb; char lm[5] = {0};

	printf("hfs-final: --- start ---\n");

	/* 0. the flash before anything */
	if (flash_header(&a, &fb, lm) == 0) {
		printf("hfs-final: flash before: attributes=0x%08x freeBlocks=%u last=%s\n", a, fb, lm);
		printf("hfs-final:   Unmounted=%d\n", (a >> 8) & 1);
	} else {
		printf("hfs-final: cannot read the flash before mounting\n");
	}

	rc = mini_syscall(SYS_mkdir, (long)DIR, 0755, 0, 0, 0, 0);
	saved = errno;
	if (rc != 0 && saved != 17) { printf("hfs-final: FAIL (mkdir)\n"); return 1; }

	/* 1. writable mount */
	rc = do_mount(0);
	saved = errno;
	printf("hfs-final: [1] mount rw -> %ld", rc);
	if (rc != 0) { printf("  errno=%d   FAIL\nhfs-final: FAIL\n", saved); return 1; }
	printf("   OK\n");

	fb_before = free_blocks();
	printf("hfs-final:     free blocks = %llu\n", (unsigned long long)fb_before);

	/* 2. write, fsync */
	fd = (int)open(FILE_, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	printf("hfs-final: [2] open -> %d\n", fd);
	if (fd < 0) { failures++; goto unmount; }
	{
		long n = mini_syscall(SYS_write, fd, (long)TEXT, (long)(sizeof(TEXT) - 1), 0, 0, 0);
		printf("hfs-final:     write %lu -> %ld\n", (unsigned long)(sizeof(TEXT) - 1), n);
		if (n != (long)(sizeof(TEXT) - 1)) failures++;
	}
	printf("hfs-final:     fsync -> %ld\n", mini_syscall(SYS_fsync, fd, 0, 0, 0, 0, 0));
	close(fd);
	fb_after = free_blocks();
	printf("hfs-final:     free blocks = %llu", (unsigned long long)fb_after);
	if (fb_after < fb_before) printf("  (a block was allocated)\n");
	else                      { printf("  (unchanged)\n"); }

unmount:
	/* 3. unmount -- the call that used to hang */
	printf("hfs-final: [3] unmount ...\n");
	rc = mini_syscall(SYS_unmount, (long)DIR, 0, 0, 0, 0, 0);
	saved = errno;
	printf("hfs-final:     unmount -> %ld", rc);
	if (rc != 0) { printf("  errno=%d   FAIL", saved); failures++; }
	printf("\n");

	/* 4. the flash after the unmount: the clean bit must be back */
	if (flash_header(&a, &fb, lm) == 0) {
		printf("hfs-final: [4] flash after: attributes=0x%08x freeBlocks=%u last=%s\n", a, fb, lm);
		printf("hfs-final:     Unmounted=%d", (a >> 8) & 1);
		if ((a >> 8) & 1) printf("   the volume is clean on the flash\n");
		else             { printf("   the volume is STILL DIRTY   FAIL\n"); failures++; }
	} else {
		printf("hfs-final: [4] cannot read the flash after unmounting   FAIL\n");
		failures++;
	}

	/* 5. mount again and read it back */
	rc = do_mount(MNT_RDONLY);
	saved = errno;
	printf("hfs-final: [5] remount ro -> %ld", rc);
	if (rc != 0) { printf("  errno=%d   FAIL\nhfs-final: FAIL\n", saved); return 1; }
	printf("\n");

	fd = (int)open(FILE_, O_RDONLY, 0);
	printf("hfs-final:     open -> %d\n", fd);
	if (fd < 0) {
		failures++;
	} else {
		long n = mini_syscall(SYS_read, fd, (long)buf, sizeof(buf) - 1, 0, 0, 0);
		if (n == (long)(sizeof(TEXT) - 1)) {
			buf[n] = 0;
			printf("hfs-final:     content = %s", buf);
		} else {
			printf("hfs-final:     read %ld, not what was written   FAIL\n", n);
			failures++;
		}
		close(fd);
	}

	rc = mini_syscall(SYS_unmount, (long)DIR, 0, 0, 0, 0, 0);
	saved = errno;
	printf("hfs-final:     unmount -> %ld", rc);
	if (rc != 0) { printf("  errno=%d   FAIL", saved); failures++; }
	printf("\n");

	printf("hfs-final: %s\n", failures ? "FAIL" : "PASS");
	printf("hfs-final: --- end ---\n");
	return failures ? 1 : 0;
}
