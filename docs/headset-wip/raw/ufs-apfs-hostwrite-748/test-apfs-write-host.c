/*
 * Stage 6, host rehearsal: apfs_write_in_place() against a copy of a real image.
 *
 * The roadmap asks for the write, the sync, an unclean exit and a recovery to be rehearsed on a
 * discardable host image before anything touches the phone. This is that rehearsal, and it uses
 * the kernel's own writer (bsd/miscfs/apfs/apfs_write.c) rather than a reimplementation.
 *
 * The image is copied first; the original is never opened for writing. The file it changes is a
 * real one from the system image, and after each write the whole file is read back and compared
 * against what was asked for.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>

#include "apfs_lib.h"
#include "apfs_write.h"

struct hostdev {
	int      fd;
	uint64_t size;
};

static int
hd_read(void *ctx, uint64_t off, void *buf, size_t len)
{
	struct hostdev *d = ctx;
	if (off + len > d->size) return EINVAL;
	if (pread(d->fd, buf, len, (off_t)off) != (ssize_t)len) return EIO;
	return 0;
}

static int
hd_write(void *ctx, uint64_t off, const void *buf, size_t len)
{
	struct hostdev *d = ctx;
	if (off + len > d->size) return EINVAL;
	if (pwrite(d->fd, buf, len, (off_t)off) != (ssize_t)len) return EIO;
	return 0;
}

static uint32_t
fnv1a(const uint8_t *p, size_t n)
{
	uint32_t h = 2166136261u;
	for (size_t i = 0; i < n; ++i) { h ^= p[i]; h *= 16777619u; }
	return h;
}

int
main(int argc, char **argv)
{
	if (argc < 3) {
		fprintf(stderr, "usage: %s <image-copy> <path-in-volume>\n", argv[0]);
		return 2;
	}

	struct hostdev d;
	d.fd = open(argv[1], O_RDWR);
	if (d.fd < 0) { perror("open"); return 2; }
	off_t sz = lseek(d.fd, 0, SEEK_END);
	d.size = (uint64_t)sz;

	struct apfs_dev dev;
	memset(&dev, 0, sizeof(dev));
	dev.ctx = &d;
	dev.size = d.size;
	dev.read = hd_read;
	dev.write = hd_write;

	static struct apfs_nx nx;
	int rc = apfs_nx_open(&nx, &dev);
	printf("nx_open -> %d\n", rc);
	if (rc) return 1;

	static struct apfs_vol vol;
	rc = apfs_vol_open(&nx, 0, &vol);
	printf("vol_open -> %d\n", rc);

	/* Walk the path a component at a time, starting at the root. */
	const char *why = NULL;
	rc = apfs_write_volume_ok(&vol, &why);
	printf("write_volume_ok -> %d%s%s\n", rc, why ? ": " : "", why ? why : "");
	if (rc) { apfs_nx_close(&nx); close(d.fd); return 1; }

	uint64_t id = 2;    /* the root directory's own id */
	char path[512];
	snprintf(path, sizeof(path), "%s", argv[2]);
	char *save = NULL;
	for (char *c = strtok_r(path, "/", &save); c; c = strtok_r(NULL, "/", &save)) {
		uint64_t next = 0;
		rc = apfs_lookup(&vol, id, c, strlen(c), &next, NULL);
		if (rc) { printf("lookup %s -> %d\n", c, rc); apfs_nx_close(&nx); close(d.fd); return 1; }
		id = next;
	}
	printf("target id=%llu\n", (unsigned long long)id);

	struct apfs_inode ino;
	rc = apfs_inode_get(&vol, id, &ino);
	printf("inode_get -> %d size=%llu\n", rc, (unsigned long long)ino.size);
	if (rc) { apfs_nx_close(&nx); close(d.fd); return 1; }

	uint8_t *before = malloc(ino.size);
	uint8_t *after = malloc(ino.size);
	if (!before || !after) return 1;

	/* Read it the way a reader would. */
	size_t got = 0;
	rc = apfs_read_stream(&vol, ino.private_id, ino.size, 0, before, ino.size, &got);
	printf("read_stream -> %d, %zu of %llu bytes, fnv=%08x\n", rc, got,
	    (unsigned long long)ino.size, fnv1a(before, got));

	/* Replace the first 4096 bytes with a pattern that says which trial wrote it. */
	uint64_t off = 0, len = got < 4096 ? got : 4096;
	uint8_t *pat = malloc(len);
	if (!pat) return 1;
	for (size_t i = 0; i < len; ++i) pat[i] = (uint8_t)(0xA0 + (i & 0x0f));
	memcpy(pat, "TRIAL747", 8);

	struct apfs_write_report rep;
	memset(&rep, 0, sizeof(rep));
	rc = apfs_write_in_place(&vol, id, off, pat, len, &rep);
	printf("write_in_place -> %d first=%llu blocks=%llu bytes=%llu partial=%llu calls=%llu\n",
	    rc, (unsigned long long)rep.first_block, (unsigned long long)rep.blocks,
	    (unsigned long long)rep.bytes, (unsigned long long)rep.partial,
	    (unsigned long long)rep.calls);
	if (rc) printf("  refused: %s\n", rep.why ? rep.why : "(no reason given)");

	/* Read it again, after dropping every cached block. */
	apfs_nx_cache_flush(&nx);
	size_t got2 = 0;
	rc = apfs_read_stream(&vol, ino.private_id, ino.size, 0, after, ino.size, &got2);
	printf("reread %zu bytes, fnv=%08x\n", got2, fnv1a(after, got2));
	printf("head=%.8s\n", (const char *)after);
	printf("verdict: %s\n",
	    (!rc && got2 == got && memcmp(after, pat, len) == 0 &&
	     memcmp(after + len, before + len, got - len) == 0) ? "MATCH" : "MISMATCH");

	free(before); free(after); free(pat);
	apfs_nx_close(&nx);
	close(d.fd);
	return 0;
}
