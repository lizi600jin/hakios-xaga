/*
 * Trial791b: set the journal bits in place, and nothing else.
 *
 * Trial791 has to put two changes into the volume: bit 13 of both volume headers, and
 * JournalInfoBlock.flags = kJIJournalInFSMask | kJIJournalNeedInitMask. Between the snapshot this
 * was prepared from and the flash, those come to three bytes.
 *
 * The seed probe writes every non-zero block of the prepared image, which is the right thing when
 * the whole volume is being laid down at once. Here it would rewrite fourteen blocks to deliver
 * three bytes, and any block whose recorded content had drifted would be silently reverted. So
 * this probe takes each block, changes only the bytes that need changing, and writes it back --
 * read-modify-write on the host side, where it can be checked before anything is flashed.
 *
 * JournalInfoBlock lives at allocation block 3 of the volume, i.e. window block 3.
 * The volume header is at offset 1024 of window block 0.
 * The alternate volume header is at offset 1024 of the last window block.
 */
#include <stdint.h>

#include "mini.h"

#define SYS_mount    167
#define SYS_unmount  159

struct margs {
	const char *fspec;
	uint32_t uid, gid;
	uint16_t mask, pad;
	uint32_t encoding;
	int tz[2];
	int flags, journal_tbuffer_size, journal_flags, journal_disable;
};

static struct margs args;

/* Read a window block through the block device. */
static int read_window_block(int fd, uint32_t block, uint8_t *dst)
{
	if (mini_syscall(SYS_lseek, fd, (long)((uint64_t)block * 4096), 0, 0, 0, 0) < 0) return -1;
	return mini_syscall(SYS_read, fd, (long)dst, 4096, 0, 0, 0) == 4096 ? 0 : -1;
}

/* Write one window block back through the block device. */
static int write_window_block(int fd, uint32_t block, const uint8_t *src)
{
	if (mini_syscall(SYS_lseek, fd, (long)((uint64_t)block * 4096), 0, 0, 0, 0) < 0) return -1;
	return mini_syscall(SYS_write, fd, (long)src, 4096, 0, 0, 0) == 4096 ? 0 : -1;
}

static uint32_t be32(const uint8_t *p)
{
	return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static void put_be32(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
	p[2] = (uint8_t)(v >> 8);  p[3] = (uint8_t)v;
}

int
main(int argc, char **argv, char **envp)
{
	(void)argc; (void)argv; (void)envp;

	static uint8_t blk[4096];
	int fd, changed = 0;
	uint32_t last_block = 44543;   /* HFS_WINDOW_BLOCKS - 1 */

	printf("hfsbits: --- start ---\n");

	fd = (int)open("/dev/xnusys_var", O_RDWR, 0);
	if (fd < 0) { printf("hfsbits: cannot open the device   FAIL\n"); return 1; }

	/* [1] primary volume header: attributes |= kHFSVolumeJournaledMask (bit 13) */
	if (read_window_block(fd, 0, blk) != 0) { printf("hfsbits: read block 0 failed\n"); close(fd); return 1; }
	{
		uint32_t a = be32(blk + 1024 + 4);
		printf("hfsbits: [1] primary attributes = 0x%08x  Journaled=%d\n", a, (a >> 13) & 1);
		if (!(a & 0x2000)) {
			put_be32(blk + 1024 + 4, a | 0x2000);
			if (write_window_block(fd, 0, blk) != 0) { printf("hfsbits: write block 0 failed\n"); close(fd); return 1; }
			changed++;
			printf("hfsbits:     -> 0x%08x  written\n", a | 0x2000);
		}
	}

	/* [2] JournalInfoBlock at block 3: flags = InFS | NeedInit */
	if (read_window_block(fd, 3, blk) != 0) { printf("hfsbits: read block 3 failed\n"); close(fd); return 1; }
	{
		uint32_t f = be32(blk);
		printf("hfsbits: [2] JournalInfoBlock flags = 0x%08x  InFS=%d NeedInit=%d\n", f, f & 1, (f >> 2) & 1);
		if (f != 0x5u) {
			put_be32(blk, 0x5u);
			if (write_window_block(fd, 3, blk) != 0) { printf("hfsbits: write block 3 failed\n"); close(fd); return 1; }
			changed++;
			printf("hfsbits:     -> 0x00000005  written\n");
		}
	}

	/* [3] alternate volume header: same bit, its own other fields untouched */
	if (read_window_block(fd, last_block, blk) != 0) { printf("hfsbits: read block %u failed\n", last_block); close(fd); return 1; }
	{
		uint32_t a = be32(blk + 1024 + 4);
		printf("hfsbits: [3] alternate attributes = 0x%08x  Journaled=%d\n", a, (a >> 13) & 1);
		if (!(a & 0x2000)) {
			put_be32(blk + 1024 + 4, a | 0x2000);
			if (write_window_block(fd, last_block, blk) != 0) { printf("hfsbits: write block %u failed\n", last_block); close(fd); return 1; }
			changed++;
			printf("hfsbits:     -> 0x%08x  written\n", a | 0x2000);
		}
	}
	close(fd);

	/* [4] read them back and say what the flash now holds */
	printf("hfsbits: [4] reading back\n");
	fd = (int)open("/dev/xnusys_var", O_RDONLY, 0);
	if (fd < 0) { printf("hfsbits: reopen failed\n"); return 1; }
	{
		int ok = 1;
		if (read_window_block(fd, 0, blk) == 0) {
			uint32_t a = be32(blk + 1024 + 4);
			printf("hfsbits:     primary   attributes=0x%08x Journaled=%d\n", a, (a >> 13) & 1);
			if (!(a & 0x2000)) ok = 0;
			printf("hfsbits:     freeBlocks=%u last=", be32(blk + 1024 + 48));
			for (int i = 0; i < 4; i++) printf("%c", blk[1024 + 8 + i]);
			printf("\n");
		} else ok = 0;
		if (read_window_block(fd, 3, blk) == 0) {
			uint32_t f = be32(blk);
			printf("hfsbits:     JIB flags=0x%08x InFS=%d NeedInit=%d\n", f, f & 1, (f >> 2) & 1);
			if ((f & 0x5u) != 0x5u) ok = 0;
		} else ok = 0;
		if (read_window_block(fd, last_block, blk) == 0) {
			uint32_t a = be32(blk + 1024 + 4);
			printf("hfsbits:     alternate attributes=0x%08x Journaled=%d\n", a, (a >> 13) & 1);
			if (!(a & 0x2000)) ok = 0;
		} else ok = 0;
		close(fd);
		if (!ok) { printf("hfsbits: FAIL (the bits did not stick)\n"); return 1; }
	}

	printf("hfsbits: %d block(s) changed, all three read back correct\n", changed);
	printf("hfsbits: PASS\n");
	printf("hfsbits: --- end ---\n");
	return 0;
}
