/*
 * Trial740: the xnusys partition as a read-only block device, so that the APFS driver already in
 * this kernel can mount the volume on it and ordinary paths can reach the files.
 *
 *   xnusys_blk=1   register /dev/xnusys, readable, 608448 blocks of 4096
 *
 * It is deliberately small. There is no queue, no interrupt and no cache: a request is turned
 * into one or more reads through the driver that has been reading this partition since Trial736,
 * and the answer goes straight into the caller's buffer. What a mount costs is how many blocks
 * the file system asks for, and at this size that is a few thousand, which the UFS driver serves
 * fast enough that nothing here is worth optimising first.
 *
 * Writes are refused. The partition is the volume the phone boots from when it boots this image,
 * and nothing in this work needs to change it; a device that cannot write cannot damage it.
 */

#include <kern/clock.h>
#include <libkern/libkern.h>
#include <mach/mach_types.h>
#include <miscfs/devfs/devfs.h>
#include <pexpert/pexpert.h>
#include <sys/buf.h>
#include <sys/conf.h>
#include <sys/disk.h>
#include <sys/errno.h>
#include <sys/systm.h>

/* 512-byte sectors, because that is what spec_open() counts in. */
#define XNUSYS_SECTOR       UINT32_C(512)
#define XNUSYS_BLOCK        UINT32_C(4096)
#define XNUSYS_SECTORS_PER_BLOCK (XNUSYS_BLOCK / XNUSYS_SECTOR)
#define XNUSYS_BLOCKS       UINT64_C(608448)
#define XNUSYS_BYTES        (XNUSYS_BLOCKS * XNUSYS_BLOCK)
#define XNUSYS_MAX_XFER     UINT32_C(65536)

extern bool hakios_mt6895_ufs_mount_read(uint64_t offset, void *dst, uint32_t len);

static int  xnusys_bmajor = -1;
static bool xnusys_ready;
static void *xnusys_node;

static int
xnusys_open(dev_t dev, int flags, int devtype, struct proc *p)
{
	(void)dev; (void)devtype; (void)p;
	if (!xnusys_ready) return ENXIO;
	if (flags & FWRITE) return EROFS;
	return 0;
}

static int
xnusys_close(dev_t dev, int flags, int devtype, struct proc *p)
{
	(void)dev; (void)flags; (void)devtype; (void)p;
	return 0;
}

/*
 * The read path. `buf_blkno` counts 512-byte sectors, and the partition is read in 4096-byte
 * blocks, so a request that is not sector-aligned to a block is served by reading the blocks it
 * touches -- the file system only ever asks in whole blocks, and anything else would be a caller
 * this device was not built for.
 */
static void
xnusys_strategy(buf_t bp)
{
	uint64_t offset;
	size_t len;
	caddr_t mapped;
	int error;

	if (!xnusys_ready) {
		buf_seterror(bp, ENXIO);
		buf_biodone(bp);
		return;
	}
	if (!(buf_flags(bp) & B_READ)) {
		buf_seterror(bp, EROFS);
		buf_biodone(bp);
		return;
	}

	offset = (uint64_t)buf_blkno(bp) * XNUSYS_SECTOR;
	len = buf_count(bp);

	if (offset >= XNUSYS_BYTES) {
		buf_seterror(bp, offset == XNUSYS_BYTES ? 0 : EINVAL);
		buf_setresid(bp, buf_count(bp));
		buf_biodone(bp);
		return;
	}
	if (offset + len > XNUSYS_BYTES) len = (size_t)(XNUSYS_BYTES - offset);

	if ((error = buf_map(bp, &mapped)) != 0) {
		buf_seterror(bp, error);
		buf_biodone(bp);
		return;
	}

	if ((offset % XNUSYS_BLOCK) == 0u && (len % XNUSYS_BLOCK) == 0u) {
		if (!hakios_mt6895_ufs_mount_read(offset, mapped, (uint32_t)len)) {
			buf_seterror(bp, EIO);
			buf_biodone(bp);
			return;
		}
	} else {
		/* Block by block into a bounce buffer; only ever a partial first or last block. */
		static uint8_t bounce[XNUSYS_BLOCK];
		size_t done = 0;

		while (done < len) {
			const uint64_t at = offset + done;
			const uint64_t base = at - (at % XNUSYS_BLOCK);
			const size_t within = (size_t)(at - base);
			const size_t take = min(len - done, (size_t)(XNUSYS_BLOCK - within));

			if (!hakios_mt6895_ufs_mount_read(base, bounce, XNUSYS_BLOCK)) {
				buf_seterror(bp, EIO);
				buf_biodone(bp);
				return;
			}
			memcpy(mapped + done, bounce + within, take);
			done += take;
		}
	}

	buf_setresid(bp, 0);
	buf_biodone(bp);
}

static int
xnusys_ioctl(dev_t dev, u_long cmd, caddr_t data, int flag, struct proc *p)
{
	(void)dev; (void)flag; (void)p;

	if (!xnusys_ready) return ENXIO;
	switch (cmd) {
	case DKIOCGETBLOCKSIZE:
		*(uint32_t *)data = XNUSYS_SECTOR;
		return 0;
	case DKIOCSETBLOCKSIZE:
		/* spec_open() counts in 512-byte sectors and treats any other answer as a broken
		 * device; the sector size is the only one this device agrees to. */
		return *(uint32_t *)data == XNUSYS_SECTOR ? 0 : EINVAL;
	case DKIOCGETBLOCKCOUNT:
		*(uint64_t *)data = XNUSYS_BYTES / XNUSYS_SECTOR;
		return 0;
	case DKIOCISWRITABLE:
		*(uint32_t *)data = 0;
		return 0;
	case DKIOCGETMAXBLOCKCOUNTREAD:
		*(uint64_t *)data = XNUSYS_MAX_XFER / XNUSYS_SECTOR;
		return 0;
	case DKIOCGETMAXBLOCKCOUNTWRITE:
		*(uint64_t *)data = 0;
		return 0;
	case DKIOCGETMAXSEGMENTCOUNTREAD:
	case DKIOCGETMAXSEGMENTCOUNTWRITE:
		*(uint64_t *)data = 1;
		return 0;
	case DKIOCGETMAXSEGMENTBYTECOUNTREAD:
	case DKIOCGETMAXSEGMENTBYTECOUNTWRITE:
		*(uint64_t *)data = XNUSYS_MAX_XFER;
		return 0;
	case DKIOCSYNCHRONIZE:
	case DKIOCSYNCHRONIZECACHE:
		return 0;
	default:
		return ENOTTY;
	}
}

static int
xnusys_size(dev_t dev)
{
	(void)dev;
	return (int)(XNUSYS_BYTES / XNUSYS_SECTOR);
}

static int
xnusys_read(dev_t dev, struct uio *uio, int ioflag)
{
	(void)ioflag;
	return physio(xnusys_strategy, NULL, dev, B_READ, (u_int (*)(buf_t))xnusys_size,
	    uio, XNUSYS_SECTOR);
}

static int
xnusys_write(dev_t dev, struct uio *uio, int ioflag)
{
	(void)dev; (void)uio; (void)ioflag;
	return EROFS;
}

static struct bdevsw xnusys_bdevsw = {
	.d_open     = xnusys_open,
	.d_close    = xnusys_close,
	.d_strategy = xnusys_strategy,
	.d_ioctl    = xnusys_ioctl,
	.d_dump     = eno_dump,
	.d_psize    = xnusys_size,
	.d_type     = D_DISK,
};

static struct cdevsw xnusys_cdevsw = {
	.d_open     = xnusys_open,
	.d_close    = xnusys_close,
	.d_read     = xnusys_read,
	.d_write    = xnusys_write,
	.d_ioctl    = xnusys_ioctl,
	.d_stop     = eno_stop,
	.d_reset    = eno_reset,
	.d_ttys     = NULL,
	.d_select   = eno_select,
	.d_mmap     = eno_mmap,
	.d_strategy = xnusys_strategy,
	.d_type     = D_DISK,
};

/* Called once the driver that serves the reads is up. */
int
hakios_xnusys_blk_start(void);

int
hakios_xnusys_blk_start(void)
{
	uint32_t mode = 0;

	if (!PE_parse_boot_argn("xnusys_blk", &mode, sizeof(mode)) || mode != 1u) return 0;
	xnusys_ready = true;

	xnusys_bmajor = bdevsw_add(-1, &xnusys_bdevsw);
	if (xnusys_bmajor < 0) {
		printf("xnusys-blk: bdevsw_add failed\n");
		xnusys_ready = false;
		return ENXIO;
	}
	xnusys_node = devfs_make_node(makedev(xnusys_bmajor, 0), DEVFS_CHAR, UID_ROOT,
	    GID_WHEEL, 0644, "xnusys");
	if (xnusys_node == NULL) {
		printf("xnusys-blk: devfs_make_node failed\n");
		(void)bdevsw_remove(xnusys_bmajor, &xnusys_bdevsw);
		xnusys_ready = false;
		return ENXIO;
	}
	printf("xnusys-blk: /dev/xnusys %llu blocks of %u, read only, bmajor=%d\n",
	    (unsigned long long)XNUSYS_BLOCKS, XNUSYS_BLOCK, xnusys_bmajor);
	return 0;
}
