/*
 * Trial787: is the volume header on the flash, or only in the kernel's memory?
 *
 * Trial786b read f_bfree before and after a write and saw 41447 -> 41446, which looked like the
 * 512-byte header write had landed. But f_bfree comes from hfs_freeblks(), a field of the in-core
 * volume control block -- not from the device. So that number proved the file system's own
 * bookkeeping moved, not that the flash changed.
 *
 * This one asks the device directly: mount read-write, write a file, then read block 0 of
 * /dev/xnusys_var and report the attributes and freeBlocks that are actually on the flash.
 * Whatever those say is the truth, and it says it without an unmount.
 */
#include <stdint.h>

#include "mini.h"

#define SYS_unmount  159
#define SYS_statfs64 345

#define DIR   "/private/var/tmp/hfs-disk"
#define FILE_ DIR "/disk.txt"
#define TEXT  "Trial787: does the volume header reach the flash?\n"

struct margs {
	const char *fspec;
	uint32_t uid, gid;
	uint16_t mask, pad;
	uint32_t encoding;
	int tz[2];
	int flags, journal_tbuffer_size, journal_flags, journal_disable;
};

static struct margs args;

/* Read block 0 of the device -- the one holding the volume header at offset 1024. */
static int read_header_block(uint32_t *attrs, uint32_t *freeblocks, char *lastmounted)
{
	static uint8_t blk[4096];
	int fd;
	long got;

	fd = (int)open("/dev/xnusys_var", O_RDONLY, 0);
	if (fd < 0) return -1;
	if (mini_syscall(SYS_lseek, fd, 0, 0, 0, 0, 0) < 0) { close(fd); return -1; }
	got = mini_syscall(SYS_read, fd, (long)blk, 4096, 0, 0, 0);
	close(fd);
	if (got != 4096) return -1;

	/* HFSPlusVolumeHeader at offset 1024, big-endian */
	{
		const uint8_t *vh = blk + 1024;
		*attrs = ((uint32_t)vh[4] << 24) | ((uint32_t)vh[5] << 16) |
		         ((uint32_t)vh[6] << 8) | vh[7];
		*freeblocks = ((uint32_t)vh[48] << 24) | ((uint32_t)vh[49] << 16) |
		              ((uint32_t)vh[50] << 8) | vh[51];
		for (int i = 0; i < 4; i++) lastmounted[i] = (char)vh[8 + i];
	}
	return 0;
}

int
main(int argc, char **argv, char **envp)
{
	(void)argc; (void)argv; (void)envp;

	long rc;
	int fd, saved;
	uint32_t a0 = 0, f0 = 0, a1 = 0, f1 = 0;
	char lm0[5] = {0}, lm1[5] = {0};

	printf("hfs-disk: --- start ---\n");

	/* 0. what the flash says before anything is mounted */
	if (read_header_block(&a0, &f0, lm0) != 0) {
		printf("hfs-disk: cannot read block 0 before mounting   FAIL\n");
		return 1;
	}
	printf("hfs-disk: FLASH before: attributes=0x%08x freeBlocks=%u lastMounted=%s\n",
	    a0, f0, lm0);
	printf("hfs-disk:   Unmounted=%d Journaled=%d\n",
	    (a0 >> 8) & 1, (a0 >> 13) & 1);

	rc = mini_syscall(SYS_mkdir, (long)DIR, 0755, 0, 0, 0, 0);
	saved = errno;
	if (rc != 0 && saved != 17) { printf("hfs-disk: FAIL (mkdir)\n"); return 1; }

	/* 1. writable mount */
	memset(&args, 0, sizeof(args));
	args.fspec = "/dev/xnusys_var";
	rc = mini_syscall(SYS_mount, (long)"hfs", (long)DIR, 0, (long)&args, 0, 0);
	saved = errno;
	printf("hfs-disk: mount rw -> %ld", rc);
	if (rc != 0) { printf("  errno=%d\nhfs-disk: FAIL\n", saved); return 1; }
	printf("\n");

	/* 2. the flash straight after mounting: mount itself clears the clean bit */
	if (read_header_block(&a1, &f1, lm1) == 0) {
		printf("hfs-disk: FLASH after mount: attributes=0x%08x freeBlocks=%u lastMounted=%s\n",
		    a1, f1, lm1);
		printf("hfs-disk:   Unmounted=%d\n", (a1 >> 8) & 1);
		if (a1 != a0 || f1 != f0)
			printf("hfs-disk:   the mount wrote the header by itself\n");
		else
			printf("hfs-disk:   the mount did NOT write the header\n");
	} else {
		printf("hfs-disk: read after mount failed\n");
	}

	/* 3. write a file, which must consume a block */
	fd = (int)open(FILE_, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	printf("hfs-disk: open for write -> %d\n", fd);
	if (fd >= 0) {
		long n = mini_syscall(SYS_write, fd, (long)TEXT, (long)(sizeof(TEXT) - 1), 0, 0, 0);
		printf("hfs-disk: write %lu -> %ld\n", (unsigned long)(sizeof(TEXT) - 1), n);
		printf("hfs-disk: fsync -> %ld\n", mini_syscall(SYS_fsync, fd, 0, 0, 0, 0, 0));
		close(fd);
	}
	{
		static struct { uint32_t f_bsize; int32_t f_iosize;
			uint64_t f_blocks, f_bfree, f_bavail, f_files, f_ffree;
			int32_t f_fsid[2]; uint32_t f_owner, f_type, f_flags, f_fssubtype;
			char f_fstypename[16]; char f_mntonname[1024]; char f_mntfromname[1024];
			uint32_t f_reserved[8]; } st;
		memset(&st, 0, sizeof(st));
		if (mini_syscall(SYS_statfs64, (long)DIR, (long)&st, (long)sizeof(st), 0, 0, 0) == 0)
			printf("hfs-disk: statfs f_bfree (in memory) = %llu\n",
			    (unsigned long long)st.f_bfree);
	}

	/* 4. the flash again: this is the number that counts */
	if (read_header_block(&a1, &f1, lm1) == 0) {
		printf("hfs-disk: FLASH after write: attributes=0x%08x freeBlocks=%u lastMounted=%s\n",
		    a1, f1, lm1);
		if (f1 != f0) printf("hfs-disk:   freeBlocks moved %u -> %u   THE HEADER IS ON THE FLASH\n", f0, f1);
		else          printf("hfs-disk:   freeBlocks still %u   THE HEADER NEVER REACHED THE FLASH\n", f1);
	} else {
		printf("hfs-disk: read after write failed\n");
	}

	/* No unmount: that is the call that hangs, and this run does not need it. */
	printf("hfs-disk: leaving the mount in place on purpose\n");
	printf("hfs-disk: --- end ---\n");
	return 0;
}
