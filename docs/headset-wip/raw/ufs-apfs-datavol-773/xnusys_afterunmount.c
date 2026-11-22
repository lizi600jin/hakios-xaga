/*
 * Trial773: is the data volume readable once the APFS volume is off the device?
 *
 * The one boot that came back cleanly ran an empty rc: /redwood/lib was mounted, launchd started,
 * nothing ever asked the device for a block. Every boot that did ask -- nine of them -- ended with
 * the lease poisoned and the phone in fastboot.
 *
 * So the question is whether the read itself is wrong or whether asking while the APFS volume is
 * mounted on the same device is wrong. This unmounts /redwood/lib first and only then reads
 * flash block 608512. If the HFS Plus signature comes back, the lease is fine and the conflict is
 * the two users of one controller; if it hangs, the read is broken on its own.
 */
#include <stdint.h>

#include "mini.h"

struct xnusys_mount_arg { char from[64]; char at[192]; };
struct xnusys_read_block_arg {
	uint64_t block;
	uint64_t bytes;
	void    *data;
	uint32_t ok;
	uint32_t pad;
};

#define XNUSYS_UNMOUNT    (0x80000000u | (256u << 16) | (0x78u << 8) | 2u)
#define XNUSYS_READ_BLOCK (0xC0000000u | (32u << 16) | (0x78u << 8) | 6u)

static void put(char *dst, const char *src, unsigned cap)
{
	unsigned i = 0;
	while (src[i] && i < cap - 1u) { dst[i] = src[i]; ++i; }
	dst[i] = '\0';
}

static uint32_t fnv1a(const uint8_t *p, size_t n)
{
	uint32_t h = 2166136261u;
	for (size_t i = 0; i < n; ++i) { h ^= p[i]; h *= 16777619u; }
	return h;
}

int
main(int argc, char **argv, char **envp)
{
	(void)argc; (void)argv; (void)envp;

	static uint8_t blk[4096];
	int fd, failures = 0, rc;

	/* 1. take the APFS volume off the device. */
	fd = (int)open("/dev/rxnusys", O_RDONLY, 0);
	if (fd < 0) { printf("xnusys-after: no control node (%d)\n", fd); return 1; }
	{
		struct xnusys_mount_arg a;
		for (unsigned i = 0; i < sizeof(a); ++i) ((char *)&a)[i] = 0;
		put(a.from, "/dev/xnusys", sizeof(a.from));
		put(a.at, "/redwood/lib", sizeof(a.at));
		rc = (int)mini_syscall(SYS_ioctl, fd, XNUSYS_UNMOUNT, (long)&a, 0, 0, 0);
		printf("xnusys-after: unmount /redwood/lib -> %d\n", rc);
		if (rc != 0) failures++;
	}

	/* 2. now ask the device for the window's first block. */
	{
		struct xnusys_read_block_arg a;
		a.block = 608512; a.bytes = 4096; a.data = blk; a.ok = 0; a.pad = 0;
		rc = (int)mini_syscall(SYS_ioctl, fd, XNUSYS_READ_BLOCK, (long)&a, 0, 0, 0);
		printf("xnusys-after: read 608512 -> rc=%d ok=%u fnv=%08x\n",
		    rc, a.ok, fnv1a(blk, sizeof(blk)));
		printf("xnusys-after: header at 1024: %02x %02x %02x %02x\n",
		    blk[1024], blk[1025], blk[1026], blk[1027]);
		if (rc != 0) failures++;
		if (!(blk[1024] == 0x48 && blk[1025] == 0x2b)) failures++;
	}
	close(fd);

	printf("xnusys-after: %s\n", failures ? "FAIL" : "PASS");
	return failures ? 1 : 0;
}
