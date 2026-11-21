/*
 * Trial742: read the mounted volume through ordinary paths.
 *
 * Everything here is a plain VFS call on /redwood/lib: stat, list a directory, open a file and
 * read it. No device, no ioctl, no knowledge of where the bytes are. The file it reads is the one
 * Trial738 read through the driver directly, so the numbers can be compared.
 */
#include <stdint.h>

#include "mini.h"

struct statbuf {
	uint32_t st_dev; uint16_t st_mode; uint16_t st_nlink;
	uint64_t st_ino; uint32_t st_uid; uint32_t st_gid;
	uint32_t st_rdev; uint32_t st_pad;
	int64_t  st_atime; int64_t st_atimensec;
	int64_t  st_mtime; int64_t st_mtimensec;
	int64_t  st_ctime; int64_t st_ctimensec;
	int64_t  st_size; int64_t st_blocks; int32_t st_blksize;
	uint32_t st_flags; uint32_t st_gen; int32_t st_lspare;
	int64_t  st_qspare[2];
};

struct dirent64 {
	uint64_t d_ino;
	uint64_t d_seekoff;
	uint16_t d_reclen;
	uint16_t d_namlen;
	uint8_t  d_type;
	char     d_name[];
};

static const char *const target = "/redwood/lib/usr/libexec/GKCentralCache.momd/VersionInfo.plist";

int
main(int argc, char **argv, char **envp)
{
	(void)argc; (void)argv; (void)envp;

	/* 1. stat the mount point itself: it must be a directory on a different device now. */
	int rc;
	struct statbuf sb;
	rc = (int)mini_syscall(SYS_stat64, (long)"/redwood/lib", (long)&sb, 0, 0, 0, 0);
	printf("xnusys-walk: stat /redwood/lib -> %d mode=%u ino=%llu size=%lld\n", rc,
	    (unsigned)sb.st_mode, (unsigned long long)sb.st_ino, (long long)sb.st_size);

	/* 2. list the top of the volume. */
	int dfd = (int)open("/redwood/lib", O_RDONLY | O_DIRECTORY, 0);
	if (dfd < 0) {
		printf("xnusys-walk: cannot open the mount point (%d)\n", dfd);
		return 1;
	}
	char buf[4096];
	int64_t position = 0;
	/*
	 * The fourth argument is where the kernel writes the offset back, and it copies into it
	 * unconditionally -- passing zero makes every call fail with EFAULT, which reads as -1 and
	 * looks like the directory being unreadable. It is not: it is a missing out-parameter.
	 */
	long n = mini_syscall(SYS_getdirentries64, dfd, (long)buf, (long)sizeof(buf),
	    (long)&position, 0, 0);
	printf("xnusys-walk: entries -> %ld\n", n);
	if (n > 0) {
		long off = 0;
		int shown = 0;
		while (off < n && shown < 12) {
			struct dirent64 *d = (struct dirent64 *)(buf + off);
			if (d->d_reclen == 0) break;
			/* The little printf here takes no length modifier, so the name goes out one
			 * character at a time -- bounded by the length the kernel reported. */
			printf("xnusys-walk:   ");
			for (unsigned k = 0; k < d->d_namlen && k < 32u; ++k)
				printf("%c", d->d_name[k]);
			printf(" (type %u)\n", (unsigned)d->d_type);
			off += d->d_reclen;
			shown++;
		}
	}
	close(dfd);

	/* 3. stat and read the file Trial738 read through the driver. */
	rc = (int)mini_syscall(SYS_stat64, (long)target, (long)&sb, 0, 0, 0, 0);
	printf("xnusys-walk: stat target -> %d ino=%llu size=%lld\n", rc,
	    (unsigned long long)sb.st_ino, (long long)sb.st_size);
	if (rc != 0) return 1;

	int fd = (int)open(target, O_RDONLY, 0);
	if (fd < 0) {
		printf("xnusys-walk: cannot open the file (%d)\n", fd);
		return 1;
	}
	{
		static uint8_t data[8192];
		long total = 0, got;
		while (total < (long)sizeof(data) &&
		    (got = mini_syscall(SYS_read, fd, (long)(data + total),
		    (long)(sizeof(data) - (size_t)total), 0, 0, 0)) > 0)
			total += got;
		printf("xnusys-walk: read %ld bytes\n", total);

		/* The first eight bytes of that file are "bplist00". */
		if (total >= 8)
			printf("xnusys-walk: head=%c%c%c%c%c%c%c%c\n", data[0], data[1], data[2],
			    data[3], data[4], data[5], data[6], data[7]);

		/* A cheap checksum, enough to compare two reads of the same file. */
		uint32_t h = 2166136261u;
		for (long i = 0; i < total; ++i) { h ^= data[i]; h *= 16777619u; }
		printf("xnusys-walk: fnv=%08x bytes=%ld\n", h, total);
	}
	close(fd);
	return 0;
}
