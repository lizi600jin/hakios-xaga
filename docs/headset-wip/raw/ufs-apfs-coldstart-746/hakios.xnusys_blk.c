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
#include <sys/mount_internal.h>
#include <sys/systm.h>
#include <sys/mount.h>
#include <sys/vnode.h>
#include <sys/vnode_internal.h>
#include <sys/vfs_context.h>

/*
 * Trial741: mounting has to happen when a process asks, not during bsd_init.
 *
 * The volume's mount point is a directory of the ramdisk, and the ramdisk is mounted by the
 * first user process -- after bsd_init has returned. A mount attempted from bsd_init therefore
 * finds no mount point (ENOENT, measured in Trial740). So the mount is an ioctl on this device:
 * process 1 calls it once /redwood exists, and everything it needs is already up by then.
 */
#define XNUSYS_MOUNT    _IOW('x', 1, struct xnusys_mount_arg)
/* Trial744: the counterpart, so that unmounting is measured rather than assumed. */
#define XNUSYS_UNMOUNT  _IOW('x', 2, struct xnusys_mount_arg)
/* Trial746: what the file system cost, for the cold-start record. */
#define XNUSYS_STATS    _IOR('x', 3, struct xnusys_stats_arg)

struct xnusys_stats_arg {
	uint64_t first_read_us;     /* registration to the first read that returned data */
	uint64_t reads;             /* requests served since then */
	uint64_t blocks;            /* blocks those requests covered */
};

struct xnusys_mount_arg {
	char from[64];      /* the device path to mount, e.g. "/dev/xnusys" */
	char at[192];       /* where to mount it */
};

/* 512-byte sectors, because that is what spec_open() counts in. */
#define XNUSYS_SECTOR       UINT32_C(512)
#define XNUSYS_BLOCK        UINT32_C(4096)
#define XNUSYS_SECTORS_PER_BLOCK (XNUSYS_BLOCK / XNUSYS_SECTOR)
#define XNUSYS_BLOCKS       UINT64_C(608448)
#define XNUSYS_BYTES        (XNUSYS_BLOCKS * XNUSYS_BLOCK)
#define XNUSYS_MAX_XFER     UINT32_C(65536)

extern bool hakios_mt6895_ufs_mount_read(uint64_t offset, void *dst, uint32_t len);

/*
 * Trial746: what a cold start costs, measured rather than estimated.
 *
 * `first_ns` is the time from this device's registration to the first read that returned data --
 * the point a mounted volume first touches the flash. `reads` and `blocks` count what the file
 * system asked for after that, which is the number the phase-5 record quotes. All three are
 * updated under no lock: a wrong total is harmless here, and a lock on the read path would be
 * measuring the lock.
 */
static uint64_t xnusys_first_ns;
static uint32_t xnusys_reads;
static uint64_t xnusys_blocks_read;
static uint64_t xnusys_start_ns;

static int  xnusys_bmajor = -1;
static int  xnusys_cmajor = -1;
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

	if (xnusys_first_ns == 0u) {
		uint64_t now = 0;
		clock_get_uptime(&now);
		xnusys_first_ns = now;
		printf("xnusys-blk: first read after %llu us, offset %llu len %llu\n",
		    (unsigned long long)((now - xnusys_start_ns) / NSEC_PER_USEC),
		    (unsigned long long)offset, (unsigned long long)len);
	}
	xnusys_reads++;
	xnusys_blocks_read += (uint64_t)(len / XNUSYS_BLOCK);

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

	if (cmd == XNUSYS_MOUNT)
		printf("xnusys-blk: mount ioctl seen (data=%p)\n", (void *)data);
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
	case XNUSYS_STATS: {
		struct xnusys_stats_arg *st = (struct xnusys_stats_arg *)data;
		uint64_t now = 0;

		if (st == NULL) return EINVAL;
		clock_get_uptime(&now);
		st->first_read_us = xnusys_first_ns ? (xnusys_first_ns - xnusys_start_ns) / NSEC_PER_USEC : 0u;
		st->reads = xnusys_reads;
		st->blocks = xnusys_blocks_read;
		return 0;
	}
	case XNUSYS_UNMOUNT: {
		const struct xnusys_mount_arg *arg = (const struct xnusys_mount_arg *)data;
		vfs_context_t ctx = vfs_context_current();
		char at[192];
		vnode_t vp = NULLVP;
		struct mount *mp;
		int error;

		if (arg == NULL) return EINVAL;
		strlcpy(at, arg->at, sizeof(at));
		if (at[0] != '/') return EINVAL;

		error = vnode_lookup(at, 0, &vp, ctx);
		if (error) {
			printf("xnusys-blk: unmount %s: no such path (%d)\n", at, error);
			return error;
		}
		/*
		 * Drop the lookup's reference before unmounting. dounmount() waits for the mount's
		 * root to become unreferenced, and holding this vnode through the call is asking it to
		 * wait for something only this function can release -- which is how the first attempt
		 * ended: no result, and the system taken back by the loader.
		 */
		mp = vnode_mount(vp);
		printf("xnusys-blk: unmount: mp=%p from a lookup of %s\n", (void *)mp, at);
		vnode_put(vp);
		vp = NULLVP;
		printf("xnusys-blk: unmount: calling dounmount\n");
		/*
		 * withref is 0: this call holds no reference of its own to give back. Passing 1 makes
		 * dounmount() drop a reference that was never taken, which unbalances the mount's count.
		 */
		error = dounmount(mp, MNT_FORCE, 0, ctx);
		printf("xnusys-blk: unmount %s -> %d\n", at, error);
		return error;
	}
	case XNUSYS_MOUNT: {
		/*
		 * `data` is already a kernel buffer: VOP_IOCTL copies the argument in before the
		 * device's ioctl runs, so copying again here would read a kernel address as if it
		 * were a user one and fail every time -- which is what the first version did.
		 */
		const struct xnusys_mount_arg *arg = (const struct xnusys_mount_arg *)data;
		vfs_context_t ctx = vfs_context_current();
		char from[64], at[192];
		vnode_t vp = NULLVP;
		int error, tries;

		if (arg == NULL) return EINVAL;
		strlcpy(from, arg->from, sizeof(from));
		strlcpy(at, arg->at, sizeof(at));
		if (from[0] != '/' || at[0] != '/') return EINVAL;

		for (tries = 0; tries < 100; tries++) {
			if (vnode_lookup(from, 0, &vp, ctx) == 0) { vnode_put(vp); break; }
			delay_for_interval(20, NSEC_PER_MSEC);
		}
		if (tries >= 100) {
			printf("xnusys-blk: %s never showed up\n", from);
			return ENXIO;
		}
		error = kernel_mount("apfs", NULLVP, NULLVP, at, from, 0,
		    MNT_RDONLY | MNT_NOSUID | MNT_NODEV, KERNEL_MOUNT_NOAUTH, ctx);
		printf("xnusys-blk: mount %s on %s -> %d\n", from, at, error);
		return error;
	}
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
	/*
	 * A block device node, and a character one beside it that shares the same major. The first
	 * version made only a character node on the block major, and open() on it failed: the two
	 * namespaces are separate, and a node has to be made in the one its switch was added to.
	 */
	xnusys_cmajor = cdevsw_add_with_bdev(-1, &xnusys_cdevsw, xnusys_bmajor);
	if (xnusys_cmajor < 0) {
		printf("xnusys-blk: cdevsw_add failed\n");
		(void)bdevsw_remove(xnusys_bmajor, &xnusys_bdevsw);
		xnusys_ready = false;
		return ENXIO;
	}
	if (devfs_make_node(makedev(xnusys_bmajor, 0), DEVFS_BLOCK, UID_ROOT,
	    GID_OPERATOR, 0644, "xnusys") == NULL ||
	    devfs_make_node(makedev(xnusys_cmajor, 0), DEVFS_CHAR, UID_ROOT,
	    GID_OPERATOR, 0644, "rxnusys") == NULL) {
		printf("xnusys-blk: devfs_make_node failed\n");
		xnusys_ready = false;
		return ENXIO;
	}
	clock_get_uptime(&xnusys_start_ns);
	printf("xnusys-blk: /dev/xnusys %llu blocks of %u, read only, bmajor=%d cmajor=%d\n",
	    (unsigned long long)XNUSYS_BLOCKS, XNUSYS_BLOCK, xnusys_bmajor, xnusys_cmajor);
	return 0;
}
