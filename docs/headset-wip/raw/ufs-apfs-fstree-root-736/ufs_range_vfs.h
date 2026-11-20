/* Trial727 exposes one pre-authorized read at its LUN-relative byte offset.
 * A write open and reads at EOF must never enter the UFS engine. */
#define RANGE_VFS_OFFSET 12031u
#define RANGE_VFS_LENGTH 770u
#define RANGE_VFS_CRC    UINT32_C(0x8f350378)
#define RANGE_NODE       "mt6895-ufs-range"
#define RANGE_PATH       "/dev/" RANGE_NODE

static uint32_t vfs_request_used;


/* Mode 4 exports exactly the primary GPT header block. The registered geometry is
 * fixed before publishing the node. Earlier modes retain their existing window. */
static uint32_t
range_source_offset(uint32_t mode)
{
	return mode >= 4u ? VR_BLOCK_BYTES : RANGE_VFS_OFFSET;
}

static uint32_t
range_source_length(uint32_t mode)
{
	return mode >= 4u ? VR_BLOCK_BYTES : RANGE_VFS_LENGTH;
}

static uint32_t
range_window_size(void)
{
	/* The published window is what the mode says it is; the runtime stage limit only bounds the
	 * modes that index from the start of the disk. */
	if (range_mode_id == APFS_FSTREE_MODE) return APFS_FSTREE_SNAPSHOT_BYTES;
	if (range_mode_id == APFS_VOLUME_MODE) return APFS_SNAPSHOT_BYTES;
	if (range_mode_id == XNUSYS_MODE) return XNUSYS_SNAPSHOT_BYTES;
	if (range_mode_id == GPT_BACKUP_MODE) return GPT_SNAPSHOT_BYTES;
	if (range_mode_id == GPT_ARRAY_MODE) return GPT_SNAPSHOT_BYTES;
	return vr_runtime_limits().lba_max_exclusive * VR_BLOCK_BYTES;
}

static int
range_read_bytes(int64_t offset, int64_t size, uint8_t *dst, uint32_t capacity, uint32_t *done)
{
	*done = 0;
	if (offset < 0 || size < 0) return EINVAL;
	if (size == 0 || (uint64_t)offset >= range_window_size()) return 0;
	if (offset != range_source_offset(range_mode_id) || size != range_source_length(range_mode_id) || dst == NULL) return EINVAL;
	struct vrb_window window;
	const struct vr_limits limits = vr_runtime_limits();
	if (vrb_plan((uint32_t)offset, (uint32_t)size, capacity, &limits, &window) != VRB_OK) return EINVAL;
	const struct vr_request request = { VR_LUN, window.first_lba, window.blocks, VR_BLOCK_BYTES };
	uint32_t unused = 0;
	if (us_stopped(&twoblock_lease)) return ENXIO;
	if (!__atomic_compare_exchange_n(&vfs_request_used, &unused, 1u, false,
	    __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) return EBUSY;
	return twoblock_run(&request, &limits, 1u, &window, dst, capacity, done) ? 0 : EIO;
}

#ifdef US_HOST_TEST
#include "ufs_vfs_test_env.h"
#endif
static int range_major = -1;
static uint32_t range_ready;
enum snapshot_state { SNAP_EMPTY, SNAP_FILLING, SNAP_READY, SNAP_FAILED };
static uint32_t snapshot_state;
/* Sized for the largest published window (Trial735's 45056 bytes); every earlier mode uses a
 * prefix of it, so nothing is re-laid-out for them. */
/* Sized for the largest published window; every earlier mode uses a prefix of it. */
static uint8_t snapshot[APFS_FSTREE_SNAPSHOT_BYTES];

static uint32_t
range_snapshot_length(uint32_t mode)
{
	if (mode == APFS_FSTREE_MODE) return APFS_FSTREE_SNAPSHOT_BYTES;
	if (mode == APFS_VOLUME_MODE) return APFS_SNAPSHOT_BYTES;
	if (mode == XNUSYS_MODE) return XNUSYS_SNAPSHOT_BYTES;
	return (mode == GPT_BACKUP_MODE || mode == GPT_ARRAY_MODE) ? GPT_SNAPSHOT_BYTES :
	    range_source_length(mode);
}

static int
range_fill_snapshot(uint32_t length)
{
	uint32_t done = 0;
	const uint32_t fixed_mode = range_mode_id;
	if (length > sizeof(snapshot) || length != range_snapshot_length(fixed_mode)) return EINVAL;
	if (fixed_mode != GPT_ARRAY_MODE && fixed_mode != GPT_BACKUP_MODE &&
	    fixed_mode != XNUSYS_MODE && fixed_mode != APFS_VOLUME_MODE &&
	    fixed_mode != APFS_FSTREE_MODE) {
		int error = range_read_bytes(range_source_offset(fixed_mode), length,
		    snapshot, length, &done);
		return error ? error : (done == length ? 0 : EIO);
	}
	/* The two fixed windows differ only in which table drives them: the primary array reads
	 * LBA 1/2/4, the backup window reads the last LBA and the four blocks before it. */
	const bool backup_layout = fixed_mode == GPT_BACKUP_MODE || fixed_mode == XNUSYS_MODE ||
	    fixed_mode == APFS_VOLUME_MODE || fixed_mode == APFS_FSTREE_MODE;
	const struct vr_request *const table =
	    backup_layout ? gpt_backup_requests : gpt_requests;

	/* The backup window sits above the stage range, so those requests are bounded by capacity. */
	const struct vr_limits limits = backup_layout ? vr_capacity_limits() : vr_runtime_limits();
	uint32_t offset = 0;
	for (uint32_t i = 0; i < GPT_REQUESTS; ++i) {
		const struct vr_request *r = &table[i];
		const uint32_t size = r->blocks * VR_BLOCK_BYTES;
		const struct vrb_window full = { r->lba, r->blocks, 0, VR_BLOCK_BYTES, size - VR_BLOCK_BYTES };
		if (size > length - offset || us_stopped(&twoblock_lease)) return EIO;
		if (!twoblock_run(r, &limits, i + 1u, &full, snapshot + offset, size, &done) ||
		    done != size) return EIO;
		offset += done;
	}

	/* Trial735: after Trial734's four commands, walk the APFS chain one step at a time.
	 *
	 * Each step asks the parser where to go next and is accepted only for the ordinal that asked.
	 * A step whose answer names a different LBA or length than the ordinal allows is refused, so no
	 * command can stand in for another. Nothing is delivered until the whole chain verifies.
	 */
	/* Trial734: only now, with all three GPT commands verified, resolve xnusys from the array we
	 * just fetched and issue the single extra block read. A failed resolve refuses the whole
	 * snapshot, so no partial window is ever published. */
	if (fixed_mode == XNUSYS_MODE || fixed_mode == APFS_VOLUME_MODE ||
	    fixed_mode == APFS_FSTREE_MODE) {
		uint32_t lba = 0u, last = 0u;
		const uint8_t *array = snapshot + VR_BLOCK_BYTES;
		uint32_t array_bytes = offset - VR_BLOCK_BYTES;
		/* The xnusys block is always the next one; mode 8 still has the chain after it. */
		const uint32_t tail = (fixed_mode == APFS_VOLUME_MODE ||
		    fixed_mode == APFS_FSTREE_MODE) ?
		    XNUSYS_SNAPSHOT_BYTES - offset : length - offset;
		if (tail != VR_BLOCK_BYTES ||
		    !xnusys_layout_valid(snapshot, array, array_bytes) ||
		    !xnusys_resolve(array, array_bytes, &lba, &last)) {
			printf("mt6895-ufs-twoblock: xnusys entry refused; no fourth request\n");
			return EIO;
		}
		printf("mt6895-ufs-twoblock: partition xnusys first=%u last=%u resolved=1\n",
		    lba, last);
		const struct vr_request fourth = { VR_LUN, lba, 1u, VR_BLOCK_BYTES };
		const struct vrb_window whole = { lba, 1u, 0u, VR_BLOCK_BYTES, 0u };
		if (us_stopped(&twoblock_lease)) return EIO;
		if (!twoblock_run(&fourth, &limits, 4u, &whole, snapshot + offset, VR_BLOCK_BYTES,
		    &done) || done != VR_BLOCK_BYTES) return EIO;
		offset += done;
	}

	if (fixed_mode == APFS_VOLUME_MODE || fixed_mode == APFS_FSTREE_MODE) {
		/* `complete` is how many APFS blocks the snapshot already holds: the chain starts with the
		 * single NXSB block, then 3, 4, 5 and finally 6. */
		/* The chain is six contiguous blocks starting at the NXSB, which command 4 wrote at the
		 * last block of the xnusys window. Pointing there makes 20480..45056 the whole chain. */
		const uint8_t *const chain = snapshot + XNUSYS_SNAPSHOT_BYTES - VR_BLOCK_BYTES;
		/* Command 4 already fetched relative block 0, the NXSB itself; the parser needs it as the
		 * first chain block, so it is moved into the chain area and the write cursor advances past
		 * it. The chain area is the tail of the published window. */
		/* The fetched blocks append straight after the NXSB, so the six blocks stay contiguous. */
		uint32_t have = 1u;
		/* Mode 8 stops after the volume superblock; mode 9 goes on to the tree root. */
		const uint32_t wanted = (fixed_mode == APFS_FSTREE_MODE) ?
		    APFS_CHAIN_REQUESTS + APFS_FSTREE_CHAIN_BLOCKS : APFS_CHAIN_REQUESTS;
		for (uint32_t step = 0; step < wanted; ++step) {
			uint32_t rel = 0u, cnt = 0u;
			const bool more = (fixed_mode == APFS_FSTREE_MODE) ?
			    av_next_root(chain, have, &rel, &cnt) : av_next(chain, have, &rel, &cnt);
			if (!more) {
				printf("mt6895-ufs-twoblock: apfs step %u refused; no request\n", step + 5u);
				return EIO;
			}
			if (cnt == 0u) {
				/* The parser reports the chain complete only on the final step. */
				if (step + 1u != wanted) {
					printf("mt6895-ufs-twoblock: apfs ended early at step %u\n", step + 5u);
					return EIO;
				}
				break;
			}
			/* Exactly one command per step, with the length the ordinal allows. */
			const uint32_t want = (step == 0u) ? 2u : 1u;
			if (cnt != want || rel >= VR_CAPACITY_BLOCKS - XNUSYS_LBA) {
				printf("mt6895-ufs-twoblock: apfs step %u geometry refused rel=%u cnt=%u\n",
				    step + 5u, rel, cnt);
				return EIO;
			}
			const struct vr_request r = { VR_LUN, XNUSYS_LBA + rel, cnt, VR_BLOCK_BYTES };
			const uint32_t size = cnt * VR_BLOCK_BYTES;
			const struct vrb_window whole = { r.lba, cnt, 0u, VR_BLOCK_BYTES,
			    size - VR_BLOCK_BYTES };
			if (offset + size > length || us_stopped(&twoblock_lease)) return EIO;
			if (!twoblock_run(&r, &limits, 5u + step, &whole, snapshot + offset, size, &done) ||
			    done != size) return EIO;
			offset += done;
			have += cnt;
		}
		/*
		 * Mode 8 ends at the volume superblock, so the parser is asked with complete=6 and must
		 * answer "nothing more". Mode 9 carries on: the same call now names the tree-root path,
		 * so its completion is checked at the end of that longer walk instead.
		 */
		if (fixed_mode == APFS_VOLUME_MODE) {
			uint32_t rel = 0u, cnt = 0u;
			if (have != 6u || !av_next(chain, have, &rel, &cnt) ||
			    rel != 0u || cnt != 0u) {
				printf("mt6895-ufs-twoblock: apfs final object refused; no snapshot\n");
				return EIO;
			}
		} else {
			uint32_t rel = 0u, cnt = 0u;
			if (have != 6u + APFS_FSTREE_CHAIN_BLOCKS ||
			    !av_next_root(chain, have, &rel, &cnt) || rel != 0u || cnt != 0u) {
				printf("mt6895-ufs-twoblock: apfs root refused; no snapshot\n");
				return EIO;
			}
		}
	}
	return offset == length ? 0 : EIO;
}

/* Only the winner writes snapshot. Readers acquire the published immutable bytes. */
static int
range_stream_read(struct uio *uio, int64_t offset, int64_t requested)
{
	if (offset < 0 || requested < 0) return EINVAL;
	if (us_stopped(&twoblock_lease)) return ENXIO;
	const uint32_t length = range_snapshot_length(range_mode_id);
	if (!requested || (uint64_t)offset >= length) return 0;

	uint32_t state = __atomic_load_n(&snapshot_state, __ATOMIC_ACQUIRE);
	if (state == SNAP_EMPTY && __atomic_compare_exchange_n(&snapshot_state, &state,
	    SNAP_FILLING, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
		int error = range_fill_snapshot(length);
		bool valid = !error && !us_stopped(&twoblock_lease);
		state = valid ? SNAP_READY : SNAP_FAILED;
		__atomic_store_n(&snapshot_state, state, __ATOMIC_RELEASE);
		if (!valid) return error ? error : EIO;
	}
	if (state == SNAP_FILLING) return EBUSY;
	if (state != SNAP_READY) return EIO;
	if (us_stopped(&twoblock_lease)) return ENXIO;
	size_t available = length - (size_t)offset;
	int n = (int)((uint64_t)requested < available ? (size_t)requested : available);
	return uiomove((caddr_t)(snapshot + (size_t)offset), n, uio);
}
extern kern_return_t thread_terminate(thread_t);

static uint32_t
range_rings(void)
{
	return __atomic_load_n(&twoblock_total_rings, __ATOMIC_ACQUIRE);
}

static int
range_dev_open(dev_t dev, int flags, __unused int type, __unused struct proc *proc)
{
	if (flags & FWRITE) return EROFS;
	return minor(dev) == 0 && __atomic_load_n(&range_ready, __ATOMIC_ACQUIRE) &&
	    !us_stopped(&twoblock_lease) ? 0 : ENXIO;
}

static int
range_dev_close(__unused dev_t dev, __unused int flags, __unused int type, __unused struct proc *proc)
{
	return 0;
}

static int
range_dev_write(__unused dev_t dev, __unused struct uio *uio, __unused int flags)
{
	return EROFS;
}

static int
range_dev_read(dev_t dev, struct uio *uio, __unused int flags)
{
	if (minor(dev) != 0 || !__atomic_load_n(&range_ready, __ATOMIC_ACQUIRE)) return ENXIO;
	if (range_mode_id >= 2u && !uio_isuserspace(uio)) return EINVAL;
	const int64_t offset = uio_offset(uio);
	const int64_t requested = uio_resid(uio);
	uint8_t copy[RANGE_VFS_LENGTH];
	uint32_t done = 0;
	int error = range_mode_id >= 3u ? range_stream_read(uio, offset, requested) :
	    range_read_bytes(offset, requested, copy, sizeof(copy), &done);
	if (!error && done) error = uiomove((caddr_t)copy, (int)done, uio);
	if (range_mode_id >= 2u) {
		printf("mt6895-ufs-twoblock: userspace read pid=%d offset=%lld requested=%lld "
		    "error=%d residual=%lld rings=%u\n", proc_selfpid(), (long long)offset,
		    (long long)requested, error, (long long)uio_resid(uio), range_rings());
	}
	return error;
}

static const struct cdevsw range_cdevsw = {
	.d_open = range_dev_open, .d_close = range_dev_close, .d_read = range_dev_read,
	.d_write = range_dev_write, .d_ioctl = eno_ioctl, .d_stop = eno_stop, .d_reset = eno_reset,
	.d_select = eno_select, .d_mmap = eno_mmap, .d_strategy = eno_strat,
	.d_reserved_1 = eno_getc, .d_reserved_2 = eno_putc
};

static void
range_selftest(__unused void *unused, __unused wait_result_t result)
{
	vfs_context_t ctx = vfs_context_kernel();
	vnode_t node = NULL;
	bool opened = false, passed = false;
	uint8_t guard[RANGE_VFS_LENGTH + 2u];
	memset(guard, 0xa5, sizeof(guard));
	int residual = 0;
	delay_for_interval(2000, NSEC_PER_MSEC);
	int error = vnode_lookup(RANGE_PATH, VNODE_LOOKUP_NOFOLLOW, &node, ctx);
	printf("mt6895-ufs-twoblock: vfs lookup error=%d\n", error);
	if (error || cdevsw[range_major].d_read != range_dev_read || vnode_vtype(node) != VCHR ||
	    vnode_specrdev(node) != makedev(range_major, 0)) goto done;
	uint32_t before = range_rings();
	error = VNOP_OPEN(node, FWRITE, ctx);
	printf("mt6895-ufs-twoblock: vfs write-open error=%d rings_before=%u rings_after=%u\n",
	    error, before, range_rings());
	if (!error) (void)VNOP_CLOSE(node, FWRITE, ctx);
	if (error != EROFS || before != 0 || range_rings() != before) goto done;
	error = VNOP_OPEN(node, FREAD, ctx);
	if (error) goto done;
	opened = true;
	error = vn_rdwr(UIO_READ, node, (caddr_t)(guard + 1), RANGE_VFS_LENGTH, RANGE_VFS_OFFSET,
	    UIO_SYSSPACE, IO_NOCACHE, vfs_context_ucred(ctx), &residual, vfs_context_proc(ctx));
	uint32_t crc = vrq_crc32(guard + 1, RANGE_VFS_LENGTH);
	bool guards_ok = guard[0] == 0xa5 && guard[sizeof(guard)-1] == 0xa5;
	printf("mt6895-ufs-twoblock: vfs read offset=%u bytes=%u error=%d residual=%d crc=%#x guards=%u rings=%u\n",
	    RANGE_VFS_OFFSET, RANGE_VFS_LENGTH, error, residual, crc, guards_ok ? 1u : 0u, range_rings());
	if (error || residual || crc != RANGE_VFS_CRC || !guards_ok || range_rings() != 1u) goto done;
	uint8_t eof[4] = { 1, 2, 3, 4 };
	before = range_rings();
	error = vn_rdwr(UIO_READ, node, (caddr_t)eof, sizeof(eof), range_window_size(), UIO_SYSSPACE,
	    IO_NOCACHE, vfs_context_ucred(ctx), &residual, vfs_context_proc(ctx));
	bool unchanged = eof[0] == 1 && eof[1] == 2 && eof[2] == 3 && eof[3] == 4;
	printf("mt6895-ufs-twoblock: vfs eof error=%d residual=%d unchanged=%u rings_before=%u rings_after=%u\n",
	    error, residual, unchanged ? 1u : 0u, before, range_rings());
	passed = error == 0 && residual == (int)sizeof(eof) && unchanged &&
	    before == 1u && range_rings() == before;
done:
	if (opened && VNOP_CLOSE(node, FREAD, ctx)) passed = false;
	if (node) vnode_put(node);
	__atomic_store_n(&range_ready, 0u, __ATOMIC_RELEASE);
	printf("mt6895-ufs-twoblock: vfs final passed=%u rings=%u control=%#x shared=%u\n",
	    passed ? 1u : 0u, range_rings(), us_control(&twoblock_lease), shared_snapshot(&platform).state);
	if (!passed) us_stop(&twoblock_lease);
	thread_terminate(current_thread());
}

static void
range_register(uint32_t mode)
{
	range_major = cdevsw_add(-24, &range_cdevsw);
	if (range_major < 0 || !devfs_make_node(makedev(range_major, 0), DEVFS_CHAR, UID_ROOT,
	    GID_WHEEL, 0400, RANGE_NODE)) {
		if (range_major >= 0) (void)cdevsw_remove(range_major, &range_cdevsw);
		printf("mt6895-ufs-twoblock: vfs registration failed\n");
		us_stop(&twoblock_lease);
		return;
	}
	range_mode_id = mode;
	__atomic_store_n(&range_ready, 1u, __ATOMIC_RELEASE);
	if (mode >= 3u) {
		printf("mt6895-ufs-twoblock: snapshot registered path=" RANGE_PATH " bytes=%u budget=%u\n",
		    range_snapshot_length(mode),
		    mode == APFS_FSTREE_MODE ? APFS_TOTAL_REQUESTS :
		    mode == APFS_VOLUME_MODE ? GPT_REQUESTS + 1u + APFS_CHAIN_REQUESTS :
		    mode == XNUSYS_MODE ? GPT_REQUESTS + 1u :
		    mode >= GPT_ARRAY_MODE ? GPT_REQUESTS : 1u);
		return;
	}
	if (mode == 2u) {
		printf("mt6895-ufs-twoblock: userspace registered path=" RANGE_PATH " budget=1\n");
		return;
	}
	printf("mt6895-ufs-twoblock: vfs registered path=" RANGE_PATH " budget=1\n");
	thread_t thread;
	if (kernel_thread_start(range_selftest, NULL, &thread) != KERN_SUCCESS) {
		__atomic_store_n(&range_ready, 0u, __ATOMIC_RELEASE);
		us_stop(&twoblock_lease);
		return;
	}
	thread_deallocate(thread);
}
