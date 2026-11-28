/*
 * HakiosMali610 (registered as "HakiosGPU"): the Redmi Note 11T Pro (MT6895 / xaga)
 * Mali-G610 MC6 Panthor DRM UAPI service for Mesa 25.0.7 panfrost/panthor and standalone
 * userland tests.
 *
 * Activated only when boot-arg mali_gpu=1 is set and bsd/dev/hakios.mali610.c has brought
 * up the CSF MCU firmware and global interface. Selector numbers are the low 8 bits of the
 * Linux drm.h and panthor_drm.h ioctl numbers so drmIoctl() maps 1:1 with zero translation.
 */

#include <IOKit/IOLib.h>
#include <IOKit/IOLocks.h>
#include <IOKit/IOKitKeys.h>
#include <IOKit/IOPlatformExpert.h>
#include <IOKit/IOService.h>
#include <IOKit/IOUserClient.h>
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <IOKit/IOSubMemoryDescriptor.h>
#include <mach/mach_time.h>
#include <mach/vm_param.h>
#include <sys/errno.h>
#include <sys/systm.h>

#include "MiniIOSurface.h"

extern "C" {
struct mali610_gpu_info {
	uint32_t gpu_id;
	uint32_t gpu_rev;
	uint32_t csf_id;
	uint32_t l2_features;
	uint32_t tiler_features;
	uint32_t mem_features;
	uint32_t mmu_features;
	uint32_t thread_features;
	uint32_t max_threads;
	uint32_t thread_max_workgroup_size;
	uint32_t thread_max_barrier_size;
	uint32_t coherency_features;
	uint32_t texture_features[4];
	uint32_t as_present;
	uint32_t selected_coherency;
	uint64_t shader_present;
	uint64_t l2_present;
	uint64_t tiler_present;
	uint32_t core_features;
	uint32_t pad;
	uint64_t gpu_features;
};

struct mali610_csif_info {
	uint32_t csg_slot_count;
	uint32_t cs_slot_count;
	uint32_t cs_reg_count;
	uint32_t scoreboard_slot_count;
	uint32_t unpreserved_cs_reg_count;
	uint32_t pad;
};

struct mali610_timestamp_info {
	uint64_t timestamp_frequency;
	uint64_t current_timestamp;
	uint64_t timestamp_offset;
	uint32_t flags;
	uint32_t duration_nsec;
	uint64_t cycle_count;
	uint64_t cpu_timestamp_sec;
	uint64_t cpu_timestamp_nsec;
};

struct mali610_queue_spec {
	uint8_t priority;
	uint8_t pad[3];
	uint32_t ringbuf_size;
};

struct mali610_group_create_args {
	uint32_t vm_id;
	uint8_t max_compute;
	uint8_t max_fragment;
	uint8_t max_tiler;
	uint8_t priority;
	uint64_t compute_mask;
	uint64_t fragment_mask;
	uint64_t tiler_mask;
	uint32_t queue_count;
	const struct mali610_queue_spec *queues;
};

bool hakios_mali610_ready(void);
uint32_t hakios_mali610_bump_flush_id(void);
bool hakios_mali610_query_gpu_info(struct mali610_gpu_info *out);
bool hakios_mali610_query_csif_info(struct mali610_csif_info *out);
bool hakios_mali610_query_timestamp_info(struct mali610_timestamp_info *inout);
void hakios_mali610_flush_dcache(void *kva, size_t size);
void hakios_mali610_report_fault(const char *why);
int hakios_mali610_vm_create(uint64_t *inout_user_va_range, uint32_t *out_vm_id);
int hakios_mali610_vm_destroy(uint32_t vm_id);
int hakios_mali610_vm_get_state(uint32_t vm_id, uint32_t *out_state);
int hakios_mali610_vm_map_pas(uint32_t vm_id, uint64_t va, const uint64_t *pas,
    uint32_t count_4k, uint32_t map_flags);
int hakios_mali610_vm_unmap(uint32_t vm_id, uint64_t va, uint64_t size);
int hakios_mali610_vm_alloc_kernel_va(uint32_t vm_id, uint64_t size, uint64_t align,
    uint64_t *out_va);
void hakios_mali610_vm_free_kernel_va(uint32_t vm_id, uint64_t va, uint64_t size);
int hakios_mali610_group_create(const struct mali610_group_create_args *args,
    uint32_t *out_hw_group);
int hakios_mali610_group_destroy(uint32_t hw_group);
int hakios_mali610_group_get_state(uint32_t hw_group, uint32_t *out_state,
    uint32_t *out_fatal_queues);
int hakios_mali610_queue_submit(uint32_t hw_group, uint32_t queue_idx,
    uint64_t stream_addr, uint32_t stream_size, uint32_t latest_flush, uint64_t *out_seq);
int hakios_mali610_queue_poll(uint32_t hw_group, uint32_t queue_idx,
    uint64_t target_seq, bool *out_done);
void hakios_mali610_group_mark_timedout(uint32_t hw_group);

void HakiosMali610Start(void);
void MiniIOMobileFramebufferShow(const uint8_t *pixels, uint32_t width, uint32_t height,
    uint32_t rowBytes);
bool hakios_mali610_tiler_oom_grow(uint32_t vm_id, uint64_t heap_ctx_va,
    uint64_t *out_encoded_va, uint32_t *out_chunk_count);
}

/* ------------------------------------------------------------------ Linux drm.h & panthor_drm.h ABI */

enum {
	kDrmGemClose                    = 0x09,

	kPanthorDevQuery                = 0x40,
	kPanthorVmCreate                = 0x41,
	kPanthorVmDestroy               = 0x42,
	kPanthorVmBind                  = 0x43,
	kPanthorVmGetState              = 0x44,
	kPanthorBoCreate                = 0x45,
	kPanthorBoMmapOffset            = 0x46,
	kPanthorGroupCreate             = 0x47,
	kPanthorGroupDestroy            = 0x48,
	kPanthorGroupSubmit             = 0x49,
	kPanthorGroupGetState           = 0x4a,
	kPanthorTilerHeapCreate         = 0x4b,
	kPanthorTilerHeapDestroy        = 0x4c,
	kPanthorBoSetLabel              = 0x4d,
	kPanthorSetUserMmioOffset       = 0x4e,
	kPanthorBoSync                  = 0x4f,
	kPanthorBoQueryInfo             = 0x50,

	kHakiosSurface                  = 0x7e,
	kHakiosShow                     = 0x7f,

	kDrmSyncobjCreate               = 0xbf,
	kDrmSyncobjDestroy              = 0xc0,
	kDrmSyncobjHandleToFd           = 0xc1,
	kDrmSyncobjFdToHandle           = 0xc2,
	kDrmSyncobjWait                 = 0xc3,
	kDrmSyncobjReset                = 0xc4,
	kDrmSyncobjSignal               = 0xc5,
	kDrmSyncobjTimelineWait         = 0xca,
	kDrmSyncobjQuery                = 0xcb,
	kDrmSyncobjTransfer             = 0xcc,
	kDrmSyncobjTimelineSignal       = 0xcd,
};

#define kHakiosFlushIdMemoryType                0x7fff0000u
#define kHakiosMmapBaseOffset                   0x100000ull
#define kHakiosDescribe                         0xffffffffu

#define DRM_PANTHOR_USER_MMIO_OFFSET_32BIT      (1ull << 43)
#define DRM_PANTHOR_USER_MMIO_OFFSET_64BIT      (1ull << 56)

#define DRM_PANTHOR_DEV_QUERY_GPU_INFO                  0u
#define DRM_PANTHOR_DEV_QUERY_CSIF_INFO                 1u
#define DRM_PANTHOR_DEV_QUERY_TIMESTAMP_INFO            2u
#define DRM_PANTHOR_DEV_QUERY_GROUP_PRIORITIES_INFO     3u
#define DRM_PANTHOR_DEV_QUERY_MMU_INFO                  4u

#define DRM_PANTHOR_BO_NO_MMAP                  (1u << 0)
#define DRM_PANTHOR_BO_WB_MMAP                  (1u << 1)
#define DRM_PANTHOR_BO_IS_IMPORTED              (1u << 0)

#define DRM_PANTHOR_VM_BIND_ASYNC               (1u << 0)
#define DRM_PANTHOR_VM_BIND_OP_MAP_READONLY     (1u << 0)
#define DRM_PANTHOR_VM_BIND_OP_MAP_NOEXEC       (1u << 1)
#define DRM_PANTHOR_VM_BIND_OP_MAP_UNCACHED     (1u << 2)
#define DRM_PANTHOR_VM_BIND_OP_MAP_SPARSE       (1u << 3)
#define DRM_PANTHOR_VM_BIND_OP_FLAGS_VALID      0x7u
#define DRM_PANTHOR_VM_BIND_OP_TYPE_MASK        (0xfu << 28)
#define DRM_PANTHOR_VM_BIND_OP_TYPE_MAP         (0u << 28)
#define DRM_PANTHOR_VM_BIND_OP_TYPE_UNMAP       (1u << 28)
#define DRM_PANTHOR_VM_BIND_OP_TYPE_SYNC_ONLY   (2u << 28)

#define DRM_PANTHOR_SYNC_OP_HANDLE_TYPE_MASK            0xffu
#define DRM_PANTHOR_SYNC_OP_HANDLE_TYPE_SYNCOBJ         0u
#define DRM_PANTHOR_SYNC_OP_HANDLE_TYPE_TIMELINE_SYNCOBJ 1u
#define DRM_PANTHOR_SYNC_OP_SIGNAL                      (1u << 31)

#define DRM_SYNCOBJ_CREATE_SIGNALED             (1u << 0)
#define DRM_SYNCOBJ_FD_TO_HANDLE_IMPORT_SYNC    (1u << 0)
#define DRM_SYNCOBJ_HANDLE_TO_FD_EXPORT_SYNC    (1u << 0)
#define DRM_SYNCOBJ_WAIT_FLAGS_WAIT_ALL         (1u << 0)
#define DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT  (1u << 1)
#define DRM_SYNCOBJ_WAIT_FLAGS_WAIT_AVAILABLE   (1u << 2)
#define DRM_SYNCOBJ_WAIT_FLAGS_WAIT_DEADLINE    (1u << 3)
#define DRM_SYNCOBJ_QUERY_FLAGS_LAST_SUBMITTED  (1u << 0)

struct drm_gem_close {
	uint32_t handle;
	uint32_t pad;
};

struct drm_panthor_obj_array {
	uint32_t stride;
	uint32_t count;
	uint64_t array;
};

struct drm_panthor_sync_op {
	uint32_t flags;
	uint32_t handle;
	uint64_t timeline_value;
};

struct drm_panthor_dev_query {
	uint32_t type;
	uint32_t size;
	uint64_t pointer;
};

struct drm_panthor_group_priorities_info {
	uint8_t allowed_mask;
	uint8_t pad[3];
};

struct drm_panthor_mmu_info {
	uint64_t page_size_bitmap;
};

struct drm_panthor_vm_create {
	uint32_t flags;
	uint32_t id;
	uint64_t user_va_range;
};

struct drm_panthor_vm_destroy {
	uint32_t id;
	uint32_t pad;
};

struct drm_panthor_vm_bind_op {
	uint32_t flags;
	uint32_t bo_handle;
	uint64_t bo_offset;
	uint64_t va;
	uint64_t size;
	struct drm_panthor_obj_array syncs;
};

struct drm_panthor_vm_bind {
	uint32_t vm_id;
	uint32_t flags;
	struct drm_panthor_obj_array ops;
};

struct drm_panthor_vm_get_state {
	uint32_t vm_id;
	uint32_t state;
};

struct drm_panthor_bo_create {
	uint64_t size;
	uint32_t flags;
	uint32_t exclusive_vm_id;
	uint32_t handle;
	uint32_t pad;
};

struct drm_panthor_bo_mmap_offset {
	uint32_t handle;
	uint32_t pad;
	uint64_t offset;
};

struct drm_panthor_group_create {
	struct drm_panthor_obj_array queues;
	uint8_t max_compute_cores;
	uint8_t max_fragment_cores;
	uint8_t max_tiler_cores;
	uint8_t priority;
	uint32_t pad;
	uint64_t compute_core_mask;
	uint64_t fragment_core_mask;
	uint64_t tiler_core_mask;
	uint32_t vm_id;
	uint32_t group_handle;
};

struct drm_panthor_group_destroy {
	uint32_t group_handle;
	uint32_t pad;
};

struct drm_panthor_queue_submit {
	uint32_t queue_index;
	uint32_t stream_size;
	uint64_t stream_addr;
	uint32_t latest_flush;
	uint32_t pad;
	struct drm_panthor_obj_array syncs;
};

struct drm_panthor_group_submit {
	uint32_t group_handle;
	uint32_t pad;
	struct drm_panthor_obj_array queue_submits;
};

struct drm_panthor_group_get_state {
	uint32_t group_handle;
	uint32_t state;
	uint32_t fatal_queues;
	uint32_t pad;
};

struct drm_panthor_tiler_heap_create {
	uint32_t vm_id;
	uint32_t initial_chunk_count;
	uint32_t chunk_size;
	uint32_t max_chunks;
	uint32_t target_in_flight;
	uint32_t handle;
	uint64_t tiler_heap_ctx_gpu_va;
	uint64_t first_heap_chunk_gpu_va;
};

struct drm_panthor_tiler_heap_destroy {
	uint32_t handle;
	uint32_t pad;
};

struct drm_panthor_bo_set_label {
	uint32_t handle;
	uint32_t pad;
	uint64_t label;
};

struct drm_panthor_set_user_mmio_offset {
	uint64_t offset;
};

struct drm_panthor_bo_sync_op {
	uint32_t handle;
	uint32_t type;
	uint64_t offset;
	uint64_t size;
};

struct drm_panthor_bo_sync {
	struct drm_panthor_obj_array ops;
};

struct drm_panthor_bo_query_info {
	uint32_t handle;
	uint32_t extra_flags;
	uint32_t create_flags;
	uint32_t pad;
};

struct drm_syncobj_create {
	uint32_t handle;
	uint32_t flags;
};

struct drm_syncobj_destroy {
	uint32_t handle;
	uint32_t pad;
};

struct drm_syncobj_handle {
	uint32_t handle;
	uint32_t flags;
	int32_t fd;
	uint32_t pad;
};

struct drm_syncobj_transfer {
	uint32_t src_handle;
	uint32_t dst_handle;
	uint64_t src_point;
	uint64_t dst_point;
	uint32_t flags;
	uint32_t pad;
};

struct drm_syncobj_wait {
	uint64_t handles;
	int64_t timeout_nsec;
	uint32_t count_handles;
	uint32_t flags;
	uint32_t first_signaled;
	uint32_t pad;
	uint64_t deadline_nsec;
};

struct drm_syncobj_timeline_wait {
	uint64_t handles;
	uint64_t points;
	int64_t timeout_nsec;
	uint32_t count_handles;
	uint32_t flags;
	uint32_t first_signaled;
	uint32_t pad;
	uint64_t deadline_nsec;
};

struct drm_syncobj_array {
	uint64_t handles;
	uint32_t count_handles;
	uint32_t pad;
};

struct drm_syncobj_timeline_array {
	uint64_t handles;
	uint64_t points;
	uint32_t count_handles;
	uint32_t flags;
};

struct hakios_surface {
	uint32_t surface;
	uint32_t handle;
	uint64_t size;
	uint32_t offset;
	uint32_t rowBytes;
	uint32_t width;
	uint32_t height;
	uint32_t pixelFormat;
	uint32_t planes;
};

struct hakios_show {
	uint64_t pixels;
	uint32_t width;
	uint32_t height;
	uint32_t rowBytes;
	uint32_t pad;
};

static_assert(sizeof(struct mali610_gpu_info) == 112, "drm_panthor_gpu_info size");
static_assert(sizeof(struct mali610_csif_info) == 24, "drm_panthor_csif_info size");
static_assert(sizeof(struct mali610_timestamp_info) == 56, "drm_panthor_timestamp_info size");
static_assert(sizeof(struct drm_panthor_vm_bind_op) == 48, "drm_panthor_vm_bind_op size");
static_assert(sizeof(struct drm_panthor_bo_create) == 24, "drm_panthor_bo_create size");
static_assert(sizeof(struct drm_panthor_group_create) == 56, "drm_panthor_group_create size");
static_assert(sizeof(struct drm_panthor_queue_submit) == 40, "drm_panthor_queue_submit size");
static_assert(sizeof(struct drm_panthor_tiler_heap_create) == 40, "tiler_heap_create size");
static_assert(sizeof(struct drm_syncobj_transfer) == 32, "drm_syncobj_transfer size");
static_assert(sizeof(struct drm_syncobj_timeline_wait) == 48, "drm_syncobj_timeline_wait size");

/* ------------------------------------------------------------------ Per-client resource tables */

#define PANTHOR_MAX_BOS                 4096u
#define PANTHOR_MAX_BO_SIZE             (256ull << 20)
#define PANTHOR_MAX_CLIENT_VMS          8u
#define PANTHOR_MAX_CLIENT_GROUPS       4u
#define PANTHOR_MAX_HEAPS               32u
#define PANTHOR_MAX_HEAP_CHUNKS         128u
#define PANTHOR_MAX_HEAP_BYTES          (256ull << 20)
#define PANTHOR_MAX_SURFACE_TRACK       64u
#define PANTHOR_MAX_SYNCOBJS            1024u
#define PANTHOR_MAX_SYNC_FILES          64u
#define PANTHOR_SYNCOBJ_POINTS          8u
#define PANTHOR_MAX_BIND_OPS            256u
#define PANTHOR_MAX_SYNC_OPS            128u
#define PANTHOR_MAX_WAIT_HANDLES        64u
#define PANTHOR_WAIT_CAP_NS             (5ull * NSEC_PER_SEC)

struct panthor_bo {
	IOMemoryDescriptor *memory;
	void *kva;
	MiniIOSurface *surface;
	uint64_t size;
	uint32_t create_flags;
	uint32_t exclusive_vm_id;
	uint32_t last_clean_seed;
	bool seed_valid;
	bool imported;
};

struct panthor_client_vm {
	bool active;
	uint32_t vm_id;
	uint64_t user_va_range;
};

struct panthor_heap_chunk {
	struct panthor_heap_chunk *next;
	IOBufferMemoryDescriptor *mem;
	uint64_t gpu_va;
	uint64_t alloc_size;
};

struct panthor_tiler_heap {
	bool active;
	uint32_t vm_id;
	uint32_t chunk_size;
	uint32_t chunk_count;
	IOBufferMemoryDescriptor *ctx_mem;
	uint64_t ctx_gpu_va;
	struct panthor_tiler_heap *global_next;
	struct panthor_heap_chunk *chunk_head;
	struct panthor_heap_chunk *chunk_tail;
};

struct panthor_sync_fence {
	uint32_t hw_group;              /* 0 = immediate / CPU-signaled */
	uint32_t queue_idx;
	uint64_t target_seq;
};

struct panthor_syncobj {
	bool in_use;
	bool binary_signaled;
	bool has_binary_fence;
	struct panthor_sync_fence binary_fence;
	uint64_t signaled_point;
	uint64_t max_point;
	struct {
		uint64_t point;
		struct panthor_sync_fence fence;
	} pending[PANTHOR_SYNCOBJ_POINTS];
};

struct panthor_sync_file {
	bool in_use;
	bool signaled;
	bool has_fence;
	struct panthor_sync_fence fence;
};

static IOLock *gPanthorLock;
static IOBufferMemoryDescriptor *gFlushIdMem;
static volatile uint32_t *gFlushIdPtr;
static struct panthor_tiler_heap *gActiveHeapList;

static inline uint64_t
panthor_pack_surface_seq(uint32_t hw_group, uint32_t queue_idx, uint64_t gpu_seq)
{
	if (hw_group == 0u || gpu_seq == 0ull) {
		return 0ull;
	}
	return ((uint64_t)(hw_group & 0xffu) << 56) |
	    ((uint64_t)(queue_idx & 0xffu) << 48) |
	    (gpu_seq & 0x0000ffffffffffffull);
}

static void
panthor_publish_flush_id(void)
{
	uint32_t id = hakios_mali610_bump_flush_id();
	if (gFlushIdPtr != NULL) {
		__atomic_store_n(&gFlushIdPtr[0], id ? id : 1u, __ATOMIC_RELEASE);
	}
}

static uint64_t
panthor_now_ns(void)
{
	uint64_t ns = 0ull;
	absolutetime_to_nanoseconds(mach_absolute_time(), &ns);
	return ns;
}

bool
hakios_mali_surface_drawn(MiniIOSurface *surface, uint64_t milliseconds)
{
	if (surface == NULL) {
		return true;
	}
	uint64_t packed = __atomic_load_n(&surface->maliSeq, __ATOMIC_ACQUIRE);
	if (packed == 0ull || !hakios_mali610_ready()) {
		return true;
	}
	uint32_t hw_group = (uint32_t)(packed >> 56);
	uint32_t queue_idx = (uint32_t)((packed >> 48) & 0xffu);
	uint64_t target_seq = packed & 0x0000ffffffffffffull;
	if (hw_group == 0u || target_seq == 0ull) {
		return true;
	}

	uint64_t start_ns = panthor_now_ns();
	uint64_t budget_ns = milliseconds * 1000000ull;
	if (budget_ns > PANTHOR_WAIT_CAP_NS) {
		budget_ns = PANTHOR_WAIT_CAP_NS;
	}
	uint64_t deadline_ns = start_ns + budget_ns;

	for (;;) {
		bool done = false;
		int perr = hakios_mali610_queue_poll(hw_group, queue_idx, target_seq, &done);
		if (perr == EINVAL) {
			done = true;
		} else if (perr != 0) {
			return false;
		}
		if (done) {
			if (surface->kpixels != NULL && surface->info.allocSize != 0u) {
				hakios_mali610_flush_dcache(surface->kpixels,
				    (size_t)round_page_64(surface->info.allocSize));
			}
			uint64_t expected = packed;
			(void)__atomic_compare_exchange_n(&surface->maliSeq, &expected, 0ull,
			    false, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED);
			return true;
		}
		if (!hakios_mali610_ready()) {
			return true;
		}
		if (panthor_now_ns() >= deadline_ns) {
			return false;
		}
		delay(25);
	}
}

/* Must be called with gPanthorLock held; internally calls VM helpers (gMaliApiLock). */
static bool
panthor_alloc_heap_chunk_locked(uint32_t vm_id, uint32_t chunk_size,
    struct panthor_heap_chunk **out_chunk)
{
	struct panthor_heap_chunk *chunk = IONewZero(struct panthor_heap_chunk, 1);
	if (chunk == NULL) {
		return false;
	}
	uint64_t alloc_sz = round_page_64(chunk_size);
	IOBufferMemoryDescriptor *cmem = IOBufferMemoryDescriptor::inTaskWithOptions(
	    kernel_task, kIODirectionInOut, (vm_size_t)alloc_sz, PAGE_SIZE);
	if (cmem == NULL) {
		IODelete(chunk, struct panthor_heap_chunk, 1);
		return false;
	}
	chunk->mem = cmem;
	chunk->alloc_size = alloc_sz;
	void *ckva = cmem->getBytesNoCopy();
	memset(ckva, 0, 64u);
	hakios_mali610_flush_dcache(ckva, 64u);

	uint64_t cva = 0ull;
	if (hakios_mali610_vm_alloc_kernel_va(vm_id, chunk_size, 0x1000ull, &cva) != 0) {
		cmem->release();
		IODelete(chunk, struct panthor_heap_chunk, 1);
		return false;
	}
	chunk->gpu_va = cva;

	uint64_t pas[64];
	uint32_t total_4k = chunk_size >> 12;
	uint32_t done_4k = 0u;
	while (done_4k < total_4k) {
		uint32_t batch = (total_4k - done_4k) > 64u ? 64u : (total_4k - done_4k);
		for (uint32_t p = 0; p < batch; p++) {
			IOByteCount slen = 0;
			pas[p] = (uint64_t)cmem->getPhysicalSegment(
			    (IOByteCount)((done_4k + p) << 12), &slen, kIOMemoryMapperNone);
			if (pas[p] == 0ull) {
				if (done_4k > 0u) {
					(void)hakios_mali610_vm_unmap(vm_id, cva,
					    (uint64_t)done_4k << 12);
				}
				hakios_mali610_vm_free_kernel_va(vm_id, cva, chunk_size);
				cmem->release();
				IODelete(chunk, struct panthor_heap_chunk, 1);
				return false;
			}
		}
		if (hakios_mali610_vm_map_pas(vm_id, cva + ((uint64_t)done_4k << 12),
		    pas, batch, DRM_PANTHOR_VM_BIND_OP_MAP_NOEXEC) != 0) {
			if (done_4k > 0u) {
				(void)hakios_mali610_vm_unmap(vm_id, cva,
				    (uint64_t)done_4k << 12);
			}
			hakios_mali610_vm_free_kernel_va(vm_id, cva, chunk_size);
			cmem->release();
			IODelete(chunk, struct panthor_heap_chunk, 1);
			return false;
		}
		done_4k += batch;
	}
	*out_chunk = chunk;
	return true;
}

extern "C" bool
hakios_mali610_tiler_oom_grow(uint32_t vm_id, uint64_t heap_ctx_va,
    uint64_t *out_encoded_va, uint32_t *out_chunk_count)
{
	if (gPanthorLock == NULL || vm_id == 0u || heap_ctx_va == 0ull ||
	    out_encoded_va == NULL || !hakios_mali610_ready()) {
		return false;
	}

	IOLockLock(gPanthorLock);
	struct panthor_tiler_heap *h = NULL;
	for (struct panthor_tiler_heap *cur = gActiveHeapList; cur != NULL; cur = cur->global_next) {
		if (cur->active && cur->vm_id == vm_id && cur->ctx_gpu_va == heap_ctx_va) {
			h = cur;
			break;
		}
	}
	if (h == NULL || h->chunk_count >= PANTHOR_MAX_HEAP_CHUNKS ||
	    (uint64_t)(h->chunk_count + 1u) * h->chunk_size > PANTHOR_MAX_HEAP_BYTES) {
		IOLockUnlock(gPanthorLock);
		return false;
	}

	struct panthor_heap_chunk *chunk = NULL;
	if (!panthor_alloc_heap_chunk_locked(vm_id, h->chunk_size, &chunk)) {
		IOLockUnlock(gPanthorLock);
		return false;
	}
	if (h->chunk_tail != NULL) {
		h->chunk_tail->next = chunk;
	} else {
		h->chunk_head = chunk;
	}
	h->chunk_tail = chunk;
	h->chunk_count++;
	panthor_publish_flush_id();

	*out_encoded_va = (chunk->gpu_va & ~0xfffull) | ((uint64_t)h->chunk_size >> 12);
	if (out_chunk_count != NULL) {
		*out_chunk_count = h->chunk_count;
	}
	IOLockUnlock(gPanthorLock);
	return true;
}

static IOReturn
panthor_errno_to_ior(int err)
{
	switch (err) {
	case 0:
		return kIOReturnSuccess;
	case EINVAL:
		return kIOReturnBadArgument;
	case ENOMEM:
		return kIOReturnNoMemory;
	case ENOSPC:
	case EBUSY:
		return kIOReturnNoResources;
	case ENOENT:
		return kIOReturnNotFound;
	case ENXIO:
		return kIOReturnNotReady;
	default:
		return kIOReturnError;
	}
}

static int
panthor_copyin_stride(uint64_t base_addr, uint32_t stride, uint32_t index,
    void *dst, size_t kern_size)
{
	memset(dst, 0, kern_size);
	size_t take = stride < kern_size ? (size_t)stride : kern_size;
	uint64_t elem_addr = base_addr + (uint64_t)index * (uint64_t)stride;
	if (take > 0 && copyin((user_addr_t)elem_addr, dst, take) != 0) {
		return EFAULT;
	}
	if ((size_t)stride > kern_size) {
		uint8_t zero_chk[64];
		size_t rem = (size_t)stride - kern_size;
		size_t off = kern_size;
		if (rem > 4096u) {
			return EINVAL;
		}
		while (rem > 0) {
			size_t chunk = rem > sizeof(zero_chk) ? sizeof(zero_chk) : rem;
			if (copyin((user_addr_t)(elem_addr + off), zero_chk, chunk) != 0) {
				return EFAULT;
			}
			for (size_t b = 0; b < chunk; b++) {
				if (zero_chk[b] != 0u) {
					return EINVAL;
				}
			}
			off += chunk;
			rem -= chunk;
		}
	}
	return 0;
}

/* ------------------------------------------------------------------ HakiosMali610 IOService */

class HakiosMali610 : public IOService
{
	OSDeclareDefaultStructors(HakiosMali610);

public:
	virtual bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
};

OSDefineMetaClassAndStructors(HakiosMali610, IOService);

bool
HakiosMali610::start(IOService *provider)
{
	if (!IOService::start(provider)) {
		return false;
	}
	setProperty(kIOUserClientClassKey, "HakiosMali610UserClient");
	setName("HakiosGPU");
	registerService();
	return true;
}

void
HakiosMali610Start(void)
{
	IOService *provider = IOService::getPlatform();
	HakiosMali610 *gpu;

	if (gPanthorLock == NULL) {
		gPanthorLock = IOLockAlloc();
	}
	if (gFlushIdMem == NULL) {
		/* Cached mapping on both kernel and user side; initial value MUST be >= 1. */
		gFlushIdMem = IOBufferMemoryDescriptor::inTaskWithOptions(kernel_task,
		    kIODirectionInOut | kIOMemoryKernelUserShared, PAGE_SIZE, PAGE_SIZE);
		if (gFlushIdMem != NULL) {
			gFlushIdPtr = (volatile uint32_t *)gFlushIdMem->getBytesNoCopy();
			memset((void *)gFlushIdPtr, 0, PAGE_SIZE);
			__atomic_store_n(&gFlushIdPtr[0], 1u, __ATOMIC_RELEASE);
		}
	}
	gpu = OSTypeAlloc(HakiosMali610);
	if (gpu == NULL || gPanthorLock == NULL || gFlushIdMem == NULL || provider == NULL ||
	    !gpu->init() || !gpu->attach(provider)) {
		OSSafeReleaseNULL(gpu);
		IOLog("HakiosGPU: cannot create Mali-G610 service\n");
		return;
	}
	if (!gpu->start(provider)) {
		gpu->detach(provider);
		gpu->release();
		IOLog("HakiosGPU: Mali-G610 service did not start\n");
		return;
	}
	gpu->release();
	IOLog("HakiosGPU: Mali-G610 panthor service started\n");
}

/* ------------------------------------------------------------------ HakiosMali610UserClient */

class HakiosMali610UserClient : public IOUserClient
{
	OSDeclareDefaultStructors(HakiosMali610UserClient);

	struct panthor_bo *             fBos;
	uint32_t                        fNextBo;
	struct panthor_client_vm        fVms[PANTHOR_MAX_CLIENT_VMS];
	uint32_t                        fGroups[PANTHOR_MAX_CLIENT_GROUPS];
	uint32_t                        fGroupVms[PANTHOR_MAX_CLIENT_GROUPS];
	struct panthor_tiler_heap *     fHeaps;
	struct panthor_syncobj *        fSyncobjs;
	uint32_t                        fNextSyncobj;
	struct panthor_sync_file        fSyncFiles[PANTHOR_MAX_SYNC_FILES];
	uint32_t                        fSurfaceBoHandles[PANTHOR_MAX_SURFACE_TRACK];
	uint32_t                        fSurfaceBoCount;
	bool                            fSurfaceBoOverflow;
	uint64_t                        fUserMmioOffset;

	struct panthor_bo *bo(uint32_t handle);
	void freeBoLocked(struct panthor_bo *b);
	void flushImportedSurfacesPreSubmitLocked(void);
	void tagImportedSurfacesPostSubmitLocked(uint32_t hw_group, uint32_t queue_idx,
	    uint64_t gpu_seq);
	void completeSurfacesForFenceLocked(struct panthor_sync_fence fence);
	struct panthor_client_vm *findClientVmLocked(uint32_t vm_id);
	struct panthor_syncobj *syncobjLocked(uint32_t handle);
	void recordTimelineFenceLocked(struct panthor_syncobj *so, uint64_t point,
	    struct panthor_sync_fence fence);
	bool lookupSyncFenceLocked(uint32_t handle, uint64_t point, bool wait_avail,
	    bool *out_ready, bool *out_unsubmitted, struct panthor_sync_fence *out_fence);
	void markSyncCompletedLocked(uint32_t handle, uint64_t point,
	    struct panthor_sync_fence fence);
	IOReturn waitSyncTargetsUnlocked(const uint32_t *handles, const uint64_t *points,
	    uint32_t count, uint32_t flags, int64_t timeout_nsec, uint32_t *out_first);

	IOReturn devQuery(struct drm_panthor_dev_query *q);
	IOReturn vmCreate(struct drm_panthor_vm_create *req);
	IOReturn vmDestroy(struct drm_panthor_vm_destroy *req);
	IOReturn vmBind(struct drm_panthor_vm_bind *req);
	IOReturn vmGetState(struct drm_panthor_vm_get_state *req);
	IOReturn boCreate(struct drm_panthor_bo_create *req);
	IOReturn boMmapOffset(struct drm_panthor_bo_mmap_offset *req);
	IOReturn boSync(struct drm_panthor_bo_sync *req);
	IOReturn boQueryInfo(struct drm_panthor_bo_query_info *req);
	IOReturn groupCreate(struct drm_panthor_group_create *req);
	IOReturn groupDestroy(struct drm_panthor_group_destroy *req);
	IOReturn groupSubmit(struct drm_panthor_group_submit *req);
	IOReturn groupGetState(struct drm_panthor_group_get_state *req);
	IOReturn tilerHeapCreate(struct drm_panthor_tiler_heap_create *req);
	IOReturn tilerHeapDestroy(struct drm_panthor_tiler_heap_destroy *req);
	void freeHeapLocked(struct panthor_tiler_heap *h);
	IOReturn surfaceBuffer(struct hakios_surface *req);

	IOReturn syncobjCreate(struct drm_syncobj_create *req);
	IOReturn syncobjDestroy(struct drm_syncobj_destroy *req);
	IOReturn syncobjHandleToFd(struct drm_syncobj_handle *req);
	IOReturn syncobjFdToHandle(struct drm_syncobj_handle *req);
	IOReturn syncobjResetOrSignal(struct drm_syncobj_array *req, bool signal);
	IOReturn syncobjQueryOrTimelineSignal(struct drm_syncobj_timeline_array *req, bool signal);
	IOReturn syncobjTransfer(struct drm_syncobj_transfer *req);

public:
	virtual bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
	virtual IOReturn clientClose(void) APPLE_KEXT_OVERRIDE;
	virtual void free(void) APPLE_KEXT_OVERRIDE;
	virtual IOReturn clientMemoryForType(UInt32 type, IOOptionBits *options,
	    IOMemoryDescriptor **memory) APPLE_KEXT_OVERRIDE;
	virtual IOReturn externalMethod(uint32_t selector, IOExternalMethodArguments *args,
	    IOExternalMethodDispatch *dispatch, OSObject *target, void *reference)
	    APPLE_KEXT_OVERRIDE;
};

OSDefineMetaClassAndStructors(HakiosMali610UserClient, IOUserClient);

bool
HakiosMali610UserClient::start(IOService *provider)
{
	if (!hakios_mali610_ready() || !IOUserClient::start(provider)) {
		return false;
	}
	fUserMmioOffset = DRM_PANTHOR_USER_MMIO_OFFSET_64BIT;
	fBos = IONewZero(struct panthor_bo, PANTHOR_MAX_BOS);
	fHeaps = IONewZero(struct panthor_tiler_heap, PANTHOR_MAX_HEAPS);
	fSyncobjs = IONewZero(struct panthor_syncobj, PANTHOR_MAX_SYNCOBJS);
	if (fBos == NULL || fHeaps == NULL || fSyncobjs == NULL) {
		return false;
	}
	return true;
}

void
HakiosMali610UserClient::free(void)
{
	if (fBos != NULL) {
		IODelete(fBos, struct panthor_bo, PANTHOR_MAX_BOS);
		fBos = NULL;
	}
	if (fHeaps != NULL) {
		IODelete(fHeaps, struct panthor_tiler_heap, PANTHOR_MAX_HEAPS);
		fHeaps = NULL;
	}
	if (fSyncobjs != NULL) {
		IODelete(fSyncobjs, struct panthor_syncobj, PANTHOR_MAX_SYNCOBJS);
		fSyncobjs = NULL;
	}
	IOUserClient::free();
}

struct panthor_bo *
HakiosMali610UserClient::bo(uint32_t handle)
{
	if (fBos == NULL || handle == 0u || handle > PANTHOR_MAX_BOS ||
	    fBos[handle - 1u].memory == NULL) {
		return NULL;
	}
	return &fBos[handle - 1u];
}

void
HakiosMali610UserClient::freeBoLocked(struct panthor_bo *b)
{
	if (b->imported && fBos != NULL) {
		uint32_t handle = (uint32_t)(b - fBos) + 1u;
		for (uint32_t i = 0; i < fSurfaceBoCount; i++) {
			if (fSurfaceBoHandles[i] == handle) {
				fSurfaceBoHandles[i] = fSurfaceBoHandles[--fSurfaceBoCount];
				break;
			}
		}
	}
	if (b->memory != NULL) {
		b->memory->release();
	}
	if (b->surface != NULL) {
		MiniIOSurfaceDrop(b->surface);
	}
	memset(b, 0, sizeof(*b));
}

void
HakiosMali610UserClient::flushImportedSurfacesPreSubmitLocked(void)
{
	if (!fSurfaceBoOverflow) {
		for (uint32_t i = 0; i < fSurfaceBoCount; i++) {
			struct panthor_bo *b = bo(fSurfaceBoHandles[i]);
			if (b != NULL && b->imported && b->surface != NULL && b->kva != NULL) {
				if (b->seed_valid && b->surface->kbase != NULL &&
				    b->surface->shared()->seed == b->last_clean_seed) {
					continue;
				}
				hakios_mali610_flush_dcache(b->kva, (size_t)b->size);
				if (b->surface->kbase != NULL) {
					b->last_clean_seed = b->surface->shared()->seed;
					b->seed_valid = true;
				}
			}
		}
		return;
	}
	for (uint32_t i = 0; fBos != NULL && i < PANTHOR_MAX_BOS; i++) {
		struct panthor_bo *b = &fBos[i];
		if (b->memory != NULL && b->imported && b->surface != NULL && b->kva != NULL) {
			if (b->seed_valid && b->surface->kbase != NULL &&
			    b->surface->shared()->seed == b->last_clean_seed) {
				continue;
			}
			hakios_mali610_flush_dcache(b->kva, (size_t)b->size);
			if (b->surface->kbase != NULL) {
				b->last_clean_seed = b->surface->shared()->seed;
				b->seed_valid = true;
			}
		}
	}
}

void
HakiosMali610UserClient::tagImportedSurfacesPostSubmitLocked(uint32_t hw_group,
    uint32_t queue_idx, uint64_t gpu_seq)
{
	uint64_t packed = panthor_pack_surface_seq(hw_group, queue_idx, gpu_seq);
	if (packed == 0ull) {
		return;
	}
	if (!fSurfaceBoOverflow) {
		for (uint32_t i = 0; i < fSurfaceBoCount; i++) {
			struct panthor_bo *b = bo(fSurfaceBoHandles[i]);
			if (b != NULL && b->imported && b->surface != NULL) {
				__atomic_store_n(&b->surface->maliSeq, packed, __ATOMIC_RELEASE);
			}
		}
		return;
	}
	for (uint32_t i = 0; fBos != NULL && i < PANTHOR_MAX_BOS; i++) {
		struct panthor_bo *b = &fBos[i];
		if (b->memory != NULL && b->imported && b->surface != NULL) {
			__atomic_store_n(&b->surface->maliSeq, packed, __ATOMIC_RELEASE);
		}
	}
}

void
HakiosMali610UserClient::completeSurfacesForFenceLocked(struct panthor_sync_fence fence)
{
	if (fence.hw_group == 0u || fence.target_seq == 0ull) {
		return;
	}
	uint32_t limit = fSurfaceBoOverflow ? PANTHOR_MAX_BOS : fSurfaceBoCount;
	for (uint32_t i = 0; i < limit; i++) {
		struct panthor_bo *b = fSurfaceBoOverflow
		    ? (fBos != NULL && fBos[i].memory != NULL ? &fBos[i] : NULL)
		    : bo(fSurfaceBoHandles[i]);
		if (b == NULL || !b->imported || b->surface == NULL) {
			continue;
		}
		uint64_t cur = __atomic_load_n(&b->surface->maliSeq, __ATOMIC_ACQUIRE);
		if (cur == 0ull) {
			continue;
		}
		uint32_t s_grp = (uint32_t)(cur >> 56);
		uint32_t s_q = (uint32_t)((cur >> 48) & 0xffu);
		uint64_t s_seq = cur & 0x0000ffffffffffffull;
		if (s_grp == fence.hw_group && s_q == fence.queue_idx &&
		    s_seq <= fence.target_seq) {
			if (b->kva != NULL) {
				hakios_mali610_flush_dcache(b->kva, (size_t)b->size);
			}
			(void)__atomic_compare_exchange_n(&b->surface->maliSeq, &cur, 0ull,
			    false, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED);
		}
	}
}

struct panthor_client_vm *
HakiosMali610UserClient::findClientVmLocked(uint32_t vm_id)
{
	if (vm_id == 0u) {
		return NULL;
	}
	for (uint32_t i = 0; i < PANTHOR_MAX_CLIENT_VMS; i++) {
		if (fVms[i].active && fVms[i].vm_id == vm_id) {
			return &fVms[i];
		}
	}
	return NULL;
}

struct panthor_syncobj *
HakiosMali610UserClient::syncobjLocked(uint32_t handle)
{
	if (fSyncobjs == NULL || handle == 0u || handle > PANTHOR_MAX_SYNCOBJS ||
	    !fSyncobjs[handle - 1u].in_use) {
		return NULL;
	}
	return &fSyncobjs[handle - 1u];
}

void
HakiosMali610UserClient::recordTimelineFenceLocked(struct panthor_syncobj *so, uint64_t point,
    struct panthor_sync_fence fence)
{
	if (point > so->max_point) {
		so->max_point = point;
	}
	if (fence.hw_group == 0u || fence.target_seq == 0ull) {
		if (point > so->signaled_point) {
			so->signaled_point = point;
		}
		so->binary_signaled = true;
		return;
	}
	unsigned victim = 0u;
	uint64_t lowest_pt = ~0ull;
	for (unsigned i = 0; i < PANTHOR_SYNCOBJ_POINTS; i++) {
		if (so->pending[i].point == 0ull || so->pending[i].point <= so->signaled_point ||
		    so->pending[i].point == point) {
			victim = i;
			lowest_pt = 0ull;
			break;
		}
		if (so->pending[i].point < lowest_pt) {
			lowest_pt = so->pending[i].point;
			victim = i;
		}
	}
	if (lowest_pt != 0ull && lowest_pt < point && lowest_pt > so->signaled_point) {
		/* Wait synchronously only if ring of 8 pending timeline points wraps; rare. */
		bool done = false;
		for (unsigned spin = 0; spin < 1000u && !done; spin++) {
			if (hakios_mali610_queue_poll(so->pending[victim].fence.hw_group,
			    so->pending[victim].fence.queue_idx,
			    so->pending[victim].fence.target_seq, &done) != 0) {
				break;
			}
			if (!done) {
				delay(100);
			}
		}
		if (done && lowest_pt > so->signaled_point) {
			so->signaled_point = lowest_pt;
		}
	}
	so->pending[victim].point = point;
	so->pending[victim].fence = fence;
}

bool
HakiosMali610UserClient::lookupSyncFenceLocked(uint32_t handle, uint64_t point, bool wait_avail,
    bool *out_ready, bool *out_unsubmitted, struct panthor_sync_fence *out_fence)
{
	struct panthor_syncobj *so = syncobjLocked(handle);
	if (so == NULL) {
		return false;
	}
	*out_ready = false;
	*out_unsubmitted = false;
	memset(out_fence, 0, sizeof(*out_fence));

	if (point == 0ull) {
		if (wait_avail) {
			if (so->binary_signaled || so->has_binary_fence || so->max_point > 0ull) {
				*out_ready = true;
			} else {
				*out_unsubmitted = true;
			}
			return true;
		}
		if (so->binary_signaled) {
			*out_ready = true;
			return true;
		}
		if (so->has_binary_fence) {
			*out_fence = so->binary_fence;
			return true;
		}
		*out_unsubmitted = true;
		return true;
	}

	if (point > so->max_point) {
		*out_unsubmitted = true;
		return true;
	}
	if (wait_avail || point <= so->signaled_point) {
		*out_ready = true;
		return true;
	}
	/* Find the earliest pending fence whose point >= requested point, or highest pending. */
	uint64_t best_pt = ~0ull;
	bool found = false;
	for (unsigned i = 0; i < PANTHOR_SYNCOBJ_POINTS; i++) {
		uint64_t pt = so->pending[i].point;
		if (pt >= point && pt < best_pt && so->pending[i].fence.hw_group != 0u) {
			best_pt = pt;
			*out_fence = so->pending[i].fence;
			found = true;
		}
	}
	if (!found) {
		*out_ready = true;
	}
	return true;
}

void
HakiosMali610UserClient::markSyncCompletedLocked(uint32_t handle, uint64_t point,
    struct panthor_sync_fence fence)
{
	struct panthor_syncobj *so = syncobjLocked(handle);
	if (so == NULL) {
		return;
	}
	if (point == 0ull) {
		if (so->has_binary_fence &&
		    so->binary_fence.hw_group == fence.hw_group &&
		    so->binary_fence.queue_idx == fence.queue_idx &&
		    so->binary_fence.target_seq <= fence.target_seq) {
			so->binary_signaled = true;
			so->has_binary_fence = false;
		}
		return;
	}
	for (unsigned i = 0; i < PANTHOR_SYNCOBJ_POINTS; i++) {
		if (so->pending[i].point != 0ull &&
		    so->pending[i].fence.hw_group == fence.hw_group &&
		    so->pending[i].fence.queue_idx == fence.queue_idx &&
		    so->pending[i].fence.target_seq <= fence.target_seq) {
			if (so->pending[i].point > so->signaled_point) {
				so->signaled_point = so->pending[i].point;
			}
			so->pending[i].point = 0ull;
		}
	}
	if (point > so->signaled_point) {
		so->signaled_point = point;
	}
	if (so->signaled_point >= so->max_point && so->max_point > 0ull) {
		so->binary_signaled = true;
	}
}

/* Called WITHOUT gPanthorLock held so sleep polls never block other UAPI requests. */
IOReturn
HakiosMali610UserClient::waitSyncTargetsUnlocked(const uint32_t *handles, const uint64_t *points,
    uint32_t count, uint32_t flags, int64_t timeout_nsec, uint32_t *out_first)
{
	if (count == 0u || count > PANTHOR_MAX_WAIT_HANDLES) {
		return kIOReturnBadArgument;
	}
	bool wait_all = (flags & DRM_SYNCOBJ_WAIT_FLAGS_WAIT_ALL) != 0u;
	bool wait_for_submit = (flags & DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT) != 0u;
	bool wait_avail = (flags & DRM_SYNCOBJ_WAIT_FLAGS_WAIT_AVAILABLE) != 0u;

	uint64_t start_ns = panthor_now_ns();
	uint64_t budget_ns = 0ull;
	if (timeout_nsec <= 0) {
		budget_ns = 0ull;
	} else if ((uint64_t)timeout_nsec >= 0x7fffffffffffffffull) {
		budget_ns = PANTHOR_WAIT_CAP_NS;
	} else if ((uint64_t)timeout_nsec > start_ns) {
		budget_ns = (uint64_t)timeout_nsec - start_ns;
		if (budget_ns > PANTHOR_WAIT_CAP_NS) {
			budget_ns = PANTHOR_WAIT_CAP_NS;
		}
	}
	uint64_t deadline_ns = start_ns + budget_ns;
	uint32_t last_hw_group = 0u;

	for (;;) {
		struct panthor_sync_fence fences[PANTHOR_MAX_WAIT_HANDLES];
		bool ready[PANTHOR_MAX_WAIT_HANDLES];
		uint32_t fence_count = 0u;
		bool all_ready = true;
		uint32_t first_ready = 0xffffffffu;

		IOLockLock(gPanthorLock);
		for (uint32_t i = 0; i < count; i++) {
			uint64_t pt = points ? points[i] : 0ull;
			bool is_ready = false, unsubmitted = false;
			struct panthor_sync_fence f;

			if (!lookupSyncFenceLocked(handles[i], pt, wait_avail,
			    &is_ready, &unsubmitted, &f)) {
				IOLockUnlock(gPanthorLock);
				return kIOReturnBadArgument;
			}
			if (unsubmitted && !wait_for_submit) {
				IOLockUnlock(gPanthorLock);
				return kIOReturnNotFound;
			}
			ready[i] = is_ready;
			fences[i] = f;
			if (is_ready) {
				if (first_ready == 0xffffffffu) {
					first_ready = i;
				}
			} else {
				all_ready = false;
				if (f.hw_group != 0u) {
					fence_count++;
					last_hw_group = f.hw_group;
				}
			}
		}
		IOLockUnlock(gPanthorLock);

		if ((wait_all && all_ready) || (!wait_all && first_ready != 0xffffffffu)) {
			if (out_first != NULL && !wait_all) {
				*out_first = first_ready;
			}
			return kIOReturnSuccess;
		}

		/* Poll hardware queues outside gPanthorLock. */
		if (fence_count > 0u) {
			bool progressed = false;
			for (uint32_t i = 0; i < count; i++) {
				if (ready[i] || fences[i].hw_group == 0u) {
					continue;
				}
				bool done = false;
				int perr = hakios_mali610_queue_poll(fences[i].hw_group,
				    fences[i].queue_idx, fences[i].target_seq, &done);
				if (perr != 0) {
					hakios_mali610_report_fault("syncobj wait poll error");
					return panthor_errno_to_ior(perr);
				}
				if (done) {
					IOLockLock(gPanthorLock);
					markSyncCompletedLocked(handles[i],
					    points ? points[i] : 0ull, fences[i]);
					/* Leave surface->maliSeq intact so MiniIOSurfaceDrawn() flushes only the surface actually presented or locked. */
					IOLockUnlock(gPanthorLock);
					progressed = true;
				}
			}
			if (progressed) {
				continue;
			}
		}

		if (!hakios_mali610_ready()) {
			return kIOReturnNotReady;
		}
		if (panthor_now_ns() >= deadline_ns) {
			if (budget_ns >= NSEC_PER_SEC && last_hw_group != 0u) {
				hakios_mali610_group_mark_timedout(last_hw_group);
				hakios_mali610_report_fault("syncobj wait timeout");
			}
			return kIOReturnTimeout;
		}
		delay(25);
	}
}

IOReturn
HakiosMali610UserClient::devQuery(struct drm_panthor_dev_query *q)
{
	union {
		struct mali610_gpu_info gpu;
		struct mali610_csif_info csif;
		struct mali610_timestamp_info ts;
		struct drm_panthor_group_priorities_info prio;
		struct drm_panthor_mmu_info mmu;
	} info;
	size_t kern_size = 0;

	memset(&info, 0, sizeof(info));
	switch (q->type) {
	case DRM_PANTHOR_DEV_QUERY_GPU_INFO:
		kern_size = sizeof(info.gpu);
		if (q->pointer != 0ull && !hakios_mali610_query_gpu_info(&info.gpu)) {
			return kIOReturnNotReady;
		}
		break;
	case DRM_PANTHOR_DEV_QUERY_CSIF_INFO:
		kern_size = sizeof(info.csif);
		if (q->pointer != 0ull && !hakios_mali610_query_csif_info(&info.csif)) {
			return kIOReturnNotReady;
		}
		break;
	case DRM_PANTHOR_DEV_QUERY_TIMESTAMP_INFO:
		kern_size = sizeof(info.ts);
		if (q->pointer != 0ull) {
			size_t in_sz = q->size < kern_size ? (size_t)q->size : kern_size;
			if (in_sz > 0 && copyin((user_addr_t)q->pointer, &info.ts, in_sz) != 0) {
				return kIOReturnVMError;
			}
			if (!hakios_mali610_query_timestamp_info(&info.ts)) {
				return kIOReturnNotReady;
			}
		}
		break;
	case DRM_PANTHOR_DEV_QUERY_GROUP_PRIORITIES_INFO:
		kern_size = sizeof(info.prio);
		info.prio.allowed_mask = 0x7u;          /* LOW | MEDIUM | HIGH */
		break;
	case DRM_PANTHOR_DEV_QUERY_MMU_INFO:
		kern_size = sizeof(info.mmu);
		info.mmu.page_size_bitmap = 0x1000ull;  /* 4 KiB GPU MMU leaf granule */
		break;
	default:
		return kIOReturnBadArgument;
	}

	if (q->pointer == 0ull) {
		q->size = (uint32_t)kern_size;
		return kIOReturnSuccess;
	}
	if (q->size > 4096u) {
		return kIOReturnBadArgument;
	}
	size_t copy_sz = (size_t)q->size < kern_size ? (size_t)q->size : kern_size;
	if (copy_sz > 0 && copyout(&info, (user_addr_t)q->pointer, copy_sz) != 0) {
		return kIOReturnVMError;
	}
	if ((size_t)q->size > kern_size) {
		uint8_t zeroes[64];
		memset(zeroes, 0, sizeof(zeroes));
		size_t rem = (size_t)q->size - kern_size;
		size_t off = kern_size;
		while (rem > 0) {
			size_t chunk = rem > sizeof(zeroes) ? sizeof(zeroes) : rem;
			if (copyout(zeroes, (user_addr_t)(q->pointer + off), chunk) != 0) {
				return kIOReturnVMError;
			}
			off += chunk;
			rem -= chunk;
		}
	}
	q->size = (uint32_t)copy_sz;
	return kIOReturnSuccess;
}

IOReturn
HakiosMali610UserClient::vmCreate(struct drm_panthor_vm_create *req)
{
	if (req->flags != 0u) {
		return kIOReturnBadArgument;
	}
	struct panthor_client_vm *slot = NULL;
	for (uint32_t i = 0; i < PANTHOR_MAX_CLIENT_VMS; i++) {
		if (!fVms[i].active) {
			slot = &fVms[i];
			break;
		}
	}
	if (slot == NULL) {
		return kIOReturnNoResources;
	}
	uint64_t uva = req->user_va_range;
	uint32_t vm_id = 0u;
	int err = hakios_mali610_vm_create(&uva, &vm_id);
	if (err != 0) {
		return panthor_errno_to_ior(err);
	}
	slot->active = true;
	slot->vm_id = vm_id;
	slot->user_va_range = uva;
	req->id = vm_id;
	req->user_va_range = uva;
	return kIOReturnSuccess;
}

void
HakiosMali610UserClient::freeHeapLocked(struct panthor_tiler_heap *h)
{
	if (h == NULL || !h->active) {
		return;
	}
	struct panthor_tiler_heap **pp = &gActiveHeapList;
	while (*pp != NULL) {
		if (*pp == h) {
			*pp = h->global_next;
			break;
		}
		pp = &(*pp)->global_next;
	}
	h->global_next = NULL;

	struct panthor_heap_chunk *c = h->chunk_head;
	while (c != NULL) {
		struct panthor_heap_chunk *next = c->next;
		if (c->gpu_va != 0ull) {
			(void)hakios_mali610_vm_unmap(h->vm_id, c->gpu_va, h->chunk_size);
			hakios_mali610_vm_free_kernel_va(h->vm_id, c->gpu_va, h->chunk_size);
		}
		OSSafeReleaseNULL(c->mem);
		IODelete(c, struct panthor_heap_chunk, 1);
		c = next;
	}
	h->chunk_head = NULL;
	h->chunk_tail = NULL;

	if (h->ctx_gpu_va != 0ull) {
		(void)hakios_mali610_vm_unmap(h->vm_id, h->ctx_gpu_va, 0x1000ull);
		hakios_mali610_vm_free_kernel_va(h->vm_id, h->ctx_gpu_va, 0x1000ull);
	}
	OSSafeReleaseNULL(h->ctx_mem);
	memset(h, 0, sizeof(*h));
}

IOReturn
HakiosMali610UserClient::vmDestroy(struct drm_panthor_vm_destroy *req)
{
	if (req->pad != 0u) {
		return kIOReturnBadArgument;
	}
	struct panthor_client_vm *cvm = findClientVmLocked(req->id);
	if (cvm == NULL) {
		return kIOReturnBadArgument;
	}
	for (uint32_t g = 0; g < PANTHOR_MAX_CLIENT_GROUPS; g++) {
		if (fGroups[g] != 0u && fGroupVms[g] == req->id) {
			uint32_t hw_grp = fGroups[g];
			fGroups[g] = 0u;
			fGroupVms[g] = 0u;
			(void)hakios_mali610_group_destroy(hw_grp);
		}
	}
	for (uint32_t i = 0; fHeaps != NULL && i < PANTHOR_MAX_HEAPS; i++) {
		if (fHeaps[i].active && fHeaps[i].vm_id == req->id) {
			freeHeapLocked(&fHeaps[i]);
		}
	}
	int err = hakios_mali610_vm_destroy(req->id);
	memset(cvm, 0, sizeof(*cvm));
	panthor_publish_flush_id();
	return panthor_errno_to_ior(err);
}

IOReturn
HakiosMali610UserClient::vmGetState(struct drm_panthor_vm_get_state *req)
{
	if (findClientVmLocked(req->vm_id) == NULL) {
		return kIOReturnBadArgument;
	}
	uint32_t st = 0u;
	int err = hakios_mali610_vm_get_state(req->vm_id, &st);
	if (err == 0) {
		req->state = st;
	}
	return panthor_errno_to_ior(err);
}

/* Called WITHOUT gPanthorLock so async wait sync_ops can poll safely. */
IOReturn
HakiosMali610UserClient::vmBind(struct drm_panthor_vm_bind *req)
{
	if ((req->flags & ~DRM_PANTHOR_VM_BIND_ASYNC) != 0u) {
		return kIOReturnBadArgument;
	}
	if (req->ops.count == 0u) {
		return kIOReturnSuccess;
	}
	if (req->ops.array == 0ull || req->ops.stride < 40u ||
	    req->ops.count > PANTHOR_MAX_BIND_OPS) {
		return kIOReturnBadArgument;
	}
	bool is_async = (req->flags & DRM_PANTHOR_VM_BIND_ASYNC) != 0u;

	for (uint32_t idx = 0; idx < req->ops.count; idx++) {
		struct drm_panthor_vm_bind_op op;
		int cerr = panthor_copyin_stride(req->ops.array, req->ops.stride, idx,
		    &op, sizeof(op));
		if (cerr != 0) {
			return cerr == EFAULT ? kIOReturnVMError : kIOReturnBadArgument;
		}
		if (!is_async && op.syncs.count != 0u) {
			return kIOReturnBadArgument;
		}
		if (op.syncs.count > PANTHOR_MAX_SYNC_OPS ||
		    (op.syncs.count != 0u &&
		    (op.syncs.array == 0ull || op.syncs.stride < sizeof(struct drm_panthor_sync_op)))) {
			return kIOReturnBadArgument;
		}

		/* Process WAIT sync_ops before performing the bind op. */
		for (uint32_t s = 0; s < op.syncs.count; s++) {
			struct drm_panthor_sync_op sop;
			cerr = panthor_copyin_stride(op.syncs.array, op.syncs.stride, s,
			    &sop, sizeof(sop));
			if (cerr != 0) {
				return cerr == EFAULT ? kIOReturnVMError : kIOReturnBadArgument;
			}
			uint32_t htype = sop.flags & DRM_PANTHOR_SYNC_OP_HANDLE_TYPE_MASK;
			if ((sop.flags & ~(DRM_PANTHOR_SYNC_OP_SIGNAL |
			    DRM_PANTHOR_SYNC_OP_HANDLE_TYPE_MASK)) != 0u ||
			    htype > DRM_PANTHOR_SYNC_OP_HANDLE_TYPE_TIMELINE_SYNCOBJ ||
			    (htype == DRM_PANTHOR_SYNC_OP_HANDLE_TYPE_SYNCOBJ &&
			    sop.timeline_value != 0ull)) {
				return kIOReturnBadArgument;
			}
			if ((sop.flags & DRM_PANTHOR_SYNC_OP_SIGNAL) == 0u) {
				uint64_t pt = (htype == DRM_PANTHOR_SYNC_OP_HANDLE_TYPE_TIMELINE_SYNCOBJ)
				    ? sop.timeline_value : 0ull;
				IOReturn wret = waitSyncTargetsUnlocked(&sop.handle,
				    htype == DRM_PANTHOR_SYNC_OP_HANDLE_TYPE_TIMELINE_SYNCOBJ ? &pt : NULL,
				    1u, DRM_SYNCOBJ_WAIT_FLAGS_WAIT_ALL, 0x7fffffffffffffffll, NULL);
				if (wret != kIOReturnSuccess) {
					return wret;
				}
			}
		}

		uint32_t op_type = op.flags & DRM_PANTHOR_VM_BIND_OP_TYPE_MASK;
		uint32_t low_flags = op.flags & ~DRM_PANTHOR_VM_BIND_OP_TYPE_MASK;

		IOLockLock(gPanthorLock);
		struct panthor_client_vm *cvm = findClientVmLocked(req->vm_id);
		if (cvm == NULL) {
			IOLockUnlock(gPanthorLock);
			return kIOReturnBadArgument;
		}
		IOReturn op_ret = kIOReturnSuccess;

		if (op_type == DRM_PANTHOR_VM_BIND_OP_TYPE_MAP) {
			if ((low_flags & ~DRM_PANTHOR_VM_BIND_OP_FLAGS_VALID) != 0u ||
			    op.size == 0ull || (op.va & 0xfffull) != 0ull ||
			    (op.size & 0xfffull) != 0ull || (op.bo_offset & 0xfffull) != 0ull ||
			    op.va >= cvm->user_va_range || op.size > cvm->user_va_range - op.va) {
				IOLockUnlock(gPanthorLock);
				return kIOReturnBadArgument;
			}
			struct panthor_bo *b = bo(op.bo_handle);
			if (b == NULL || op.bo_offset >= b->size ||
			    op.size > b->size - op.bo_offset ||
			    (b->exclusive_vm_id != 0u && b->exclusive_vm_id != req->vm_id)) {
				IOLockUnlock(gPanthorLock);
				return kIOReturnBadArgument;
			}
			/* Default BOs are WC on CPU -> map UNCACHED on non-coherent GPU unless WB. */
			uint32_t mflags = low_flags;
			if (!b->imported && (b->create_flags & DRM_PANTHOR_BO_WB_MMAP) == 0u) {
				mflags |= DRM_PANTHOR_VM_BIND_OP_MAP_UNCACHED;
			}
			uint64_t pas[64];
			uint64_t total_4k = op.size >> 12;
			uint64_t done_4k = 0ull;
			while (done_4k < total_4k) {
				uint32_t batch = (total_4k - done_4k) > 64ull
				    ? 64u : (uint32_t)(total_4k - done_4k);
				for (uint32_t p = 0; p < batch; p++) {
					IOByteCount seg_len = 0;
					uint64_t bo_off = op.bo_offset + ((done_4k + p) << 12);
					addr64_t phys = b->memory->getPhysicalSegment(
					    (IOByteCount)bo_off, &seg_len, kIOMemoryMapperNone);
					if (phys == 0ull) {
						op_ret = kIOReturnVMError;
						break;
					}
					pas[p] = (uint64_t)phys;
				}
				if (op_ret != kIOReturnSuccess) {
					break;
				}
				int merr = hakios_mali610_vm_map_pas(req->vm_id,
				    op.va + (done_4k << 12), pas, batch, mflags);
				if (merr != 0) {
					op_ret = panthor_errno_to_ior(merr);
					break;
				}
				done_4k += batch;
			}
			panthor_publish_flush_id();
		} else if (op_type == DRM_PANTHOR_VM_BIND_OP_TYPE_UNMAP) {
			if (low_flags != 0u || op.bo_handle != 0u || op.bo_offset != 0ull ||
			    op.size == 0ull || (op.va & 0xfffull) != 0ull ||
			    (op.size & 0xfffull) != 0ull ||
			    op.va >= cvm->user_va_range || op.size > cvm->user_va_range - op.va) {
				IOLockUnlock(gPanthorLock);
				return kIOReturnBadArgument;
			}
			int uerr = hakios_mali610_vm_unmap(req->vm_id, op.va, op.size);
			op_ret = panthor_errno_to_ior(uerr);
			panthor_publish_flush_id();
		} else if (op_type == DRM_PANTHOR_VM_BIND_OP_TYPE_SYNC_ONLY) {
			if (!is_async || op.syncs.count == 0u || low_flags != 0u ||
			    op.bo_handle != 0u || op.bo_offset != 0ull ||
			    op.va != 0ull || op.size != 0ull) {
				IOLockUnlock(gPanthorLock);
				return kIOReturnBadArgument;
			}
		} else {
			IOLockUnlock(gPanthorLock);
			return kIOReturnBadArgument;
		}

		if (op_ret != kIOReturnSuccess) {
			IOLockUnlock(gPanthorLock);
			return op_ret;
		}

		/* Signal out-syncobjs immediately since VM_BIND completed synchronously. */
		for (uint32_t s = 0; s < op.syncs.count; s++) {
			struct drm_panthor_sync_op sop;
			if (panthor_copyin_stride(op.syncs.array, op.syncs.stride, s,
			    &sop, sizeof(sop)) != 0) {
				IOLockUnlock(gPanthorLock);
				return kIOReturnVMError;
			}
			if ((sop.flags & DRM_PANTHOR_SYNC_OP_SIGNAL) != 0u) {
				struct panthor_syncobj *so = syncobjLocked(sop.handle);
				if (so == NULL) {
					IOLockUnlock(gPanthorLock);
					return kIOReturnBadArgument;
				}
				uint32_t htype = sop.flags & DRM_PANTHOR_SYNC_OP_HANDLE_TYPE_MASK;
				if (htype == DRM_PANTHOR_SYNC_OP_HANDLE_TYPE_TIMELINE_SYNCOBJ) {
					struct panthor_sync_fence imm = { 0u, 0u, 0ull };
					recordTimelineFenceLocked(so, sop.timeline_value, imm);
				} else {
					so->binary_signaled = true;
					so->has_binary_fence = false;
				}
			}
		}
		IOLockUnlock(gPanthorLock);
	}
	return kIOReturnSuccess;
}

IOReturn
HakiosMali610UserClient::boCreate(struct drm_panthor_bo_create *req)
{
	if (req->pad != 0u ||
	    (req->flags & ~(DRM_PANTHOR_BO_NO_MMAP | DRM_PANTHOR_BO_WB_MMAP)) != 0u ||
	    ((req->flags & DRM_PANTHOR_BO_NO_MMAP) != 0u &&
	    (req->flags & DRM_PANTHOR_BO_WB_MMAP) != 0u)) {
		return kIOReturnBadArgument;
	}
	if (req->exclusive_vm_id != 0u && findClientVmLocked(req->exclusive_vm_id) == NULL) {
		return kIOReturnBadArgument;
	}
	if (req->size == 0ull || req->size > PANTHOR_MAX_BO_SIZE) {
		return kIOReturnBadArgument;
	}
	uint64_t alloc_size = round_page_64(req->size);
	if (alloc_size == 0ull || alloc_size > PANTHOR_MAX_BO_SIZE) {
		return kIOReturnBadArgument;
	}

	struct panthor_bo *slot = NULL;
	uint32_t handle = 0u;
	for (uint32_t n = 0; n < PANTHOR_MAX_BOS; n++) {
		uint32_t i = (fNextBo + n) % PANTHOR_MAX_BOS;
		if (fBos[i].memory == NULL) {
			slot = &fBos[i];
			handle = i + 1u;
			fNextBo = i + 1u;
			break;
		}
	}
	if (slot == NULL) {
		return kIOReturnNoResources;
	}

	IOOptionBits opts = kIODirectionInOut | kIOMemoryKernelUserShared;
	if ((req->flags & DRM_PANTHOR_BO_WB_MMAP) == 0u) {
		opts |= kIOMapWriteCombineCache;
	}
	IOBufferMemoryDescriptor *mem = IOBufferMemoryDescriptor::inTaskWithOptions(
	    kernel_task, opts, (vm_size_t)alloc_size, PAGE_SIZE);
	if (mem == NULL) {
		return kIOReturnNoMemory;
	}
	void *kva = mem->getBytesNoCopy();
	if (kva != NULL) {
		memset(kva, 0, (size_t)alloc_size);
		if ((req->flags & DRM_PANTHOR_BO_WB_MMAP) != 0u) {
			hakios_mali610_flush_dcache(kva, (size_t)alloc_size);
		}
	}

	memset(slot, 0, sizeof(*slot));
	slot->memory = mem;
	slot->kva = kva;
	slot->size = alloc_size;
	slot->create_flags = req->flags;
	slot->exclusive_vm_id = req->exclusive_vm_id;
	slot->imported = false;

	req->size = alloc_size;
	req->handle = handle;
	return kIOReturnSuccess;
}

IOReturn
HakiosMali610UserClient::boMmapOffset(struct drm_panthor_bo_mmap_offset *req)
{
	if (req->pad != 0u) {
		return kIOReturnBadArgument;
	}
	struct panthor_bo *b = bo(req->handle);
	if (b == NULL) {
		return kIOReturnNotFound;
	}
	if ((b->create_flags & DRM_PANTHOR_BO_NO_MMAP) != 0u) {
		return kIOReturnBadArgument;
	}
	req->offset = kHakiosMmapBaseOffset + ((uint64_t)req->handle << 14);
	return kIOReturnSuccess;
}

IOReturn
HakiosMali610UserClient::boSync(struct drm_panthor_bo_sync *req)
{
	if (req->ops.count == 0u) {
		return kIOReturnSuccess;
	}
	if (req->ops.array == 0ull || req->ops.stride < sizeof(struct drm_panthor_bo_sync_op) ||
	    req->ops.count > PANTHOR_MAX_BIND_OPS) {
		return kIOReturnBadArgument;
	}
	for (uint32_t i = 0; i < req->ops.count; i++) {
		struct drm_panthor_bo_sync_op op;
		int cerr = panthor_copyin_stride(req->ops.array, req->ops.stride, i,
		    &op, sizeof(op));
		if (cerr != 0) {
			return cerr == EFAULT ? kIOReturnVMError : kIOReturnBadArgument;
		}
		if (op.type > 1u) {
			return kIOReturnBadArgument;
		}
		struct panthor_bo *b = bo(op.handle);
		if (b == NULL || op.offset > b->size || op.size > b->size - op.offset) {
			return kIOReturnBadArgument;
		}
		if (b->kva != NULL && op.size != 0ull) {
			hakios_mali610_flush_dcache((uint8_t *)b->kva + op.offset,
			    (size_t)op.size);
		}
	}
	return kIOReturnSuccess;
}

IOReturn
HakiosMali610UserClient::boQueryInfo(struct drm_panthor_bo_query_info *req)
{
	struct panthor_bo *b = bo(req->handle);
	if (b == NULL) {
		return kIOReturnNotFound;
	}
	req->extra_flags = b->imported ? DRM_PANTHOR_BO_IS_IMPORTED : 0u;
	req->create_flags = b->imported ? 0u : b->create_flags;
	req->pad = 0u;
	return kIOReturnSuccess;
}

IOReturn
HakiosMali610UserClient::groupCreate(struct drm_panthor_group_create *req)
{
	if (req->pad != 0u || req->queues.count == 0u || req->queues.count > 4u ||
	    req->queues.array == 0ull ||
	    req->queues.stride < sizeof(struct mali610_queue_spec)) {
		return kIOReturnBadArgument;
	}
	if (findClientVmLocked(req->vm_id) == NULL) {
		return kIOReturnBadArgument;
	}
	uint32_t gslot = 0xffffffffu;
	for (uint32_t i = 0; i < PANTHOR_MAX_CLIENT_GROUPS; i++) {
		if (fGroups[i] == 0u) {
			gslot = i;
			break;
		}
	}
	if (gslot == 0xffffffffu) {
		return kIOReturnNoResources;
	}

	struct mali610_queue_spec qspecs[4];
	for (uint32_t q = 0; q < req->queues.count; q++) {
		int cerr = panthor_copyin_stride(req->queues.array, req->queues.stride, q,
		    &qspecs[q], sizeof(qspecs[q]));
		if (cerr != 0) {
			return cerr == EFAULT ? kIOReturnVMError : kIOReturnBadArgument;
		}
	}

	struct mali610_group_create_args cargs;
	memset(&cargs, 0, sizeof(cargs));
	cargs.vm_id = req->vm_id;
	cargs.max_compute = req->max_compute_cores;
	cargs.max_fragment = req->max_fragment_cores;
	cargs.max_tiler = req->max_tiler_cores;
	cargs.priority = req->priority;
	cargs.compute_mask = req->compute_core_mask;
	cargs.fragment_mask = req->fragment_core_mask;
	cargs.tiler_mask = req->tiler_core_mask;
	cargs.queue_count = req->queues.count;
	cargs.queues = qspecs;

	uint32_t hw_group = 0u;
	int err = hakios_mali610_group_create(&cargs, &hw_group);
	if (err != 0) {
		return panthor_errno_to_ior(err);
	}
	panthor_publish_flush_id();
	fGroups[gslot] = hw_group;
	fGroupVms[gslot] = req->vm_id;
	req->group_handle = gslot + 1u;
	return kIOReturnSuccess;
}

IOReturn
HakiosMali610UserClient::groupDestroy(struct drm_panthor_group_destroy *req)
{
	if (req->pad != 0u || req->group_handle == 0u ||
	    req->group_handle > PANTHOR_MAX_CLIENT_GROUPS) {
		return kIOReturnBadArgument;
	}
	uint32_t hw_group = fGroups[req->group_handle - 1u];
	if (hw_group == 0u) {
		return kIOReturnNotFound;
	}
	fGroups[req->group_handle - 1u] = 0u;
	fGroupVms[req->group_handle - 1u] = 0u;
	int err = hakios_mali610_group_destroy(hw_group);
	panthor_publish_flush_id();
	return panthor_errno_to_ior(err);
}

IOReturn
HakiosMali610UserClient::groupGetState(struct drm_panthor_group_get_state *req)
{
	if (req->pad != 0u || req->group_handle == 0u ||
	    req->group_handle > PANTHOR_MAX_CLIENT_GROUPS) {
		return kIOReturnBadArgument;
	}
	uint32_t hw_group = fGroups[req->group_handle - 1u];
	if (hw_group == 0u) {
		return kIOReturnNotFound;
	}
	uint32_t st = 0u, fq = 0u;
	int err = hakios_mali610_group_get_state(hw_group, &st, &fq);
	if (err == 0) {
		req->state = st;
		req->fatal_queues = fq;
	}
	return panthor_errno_to_ior(err);
}

/* Called WITHOUT gPanthorLock so pre-submit WAIT sync_ops can poll safely. */
IOReturn
HakiosMali610UserClient::groupSubmit(struct drm_panthor_group_submit *req)
{
	if (req->pad != 0u || req->group_handle == 0u ||
	    req->group_handle > PANTHOR_MAX_CLIENT_GROUPS) {
		return kIOReturnBadArgument;
	}
	if (req->queue_submits.count == 0u) {
		return kIOReturnSuccess;
	}
	if (req->queue_submits.array == 0ull ||
	    req->queue_submits.stride < sizeof(struct drm_panthor_queue_submit) ||
	    req->queue_submits.count > 16u) {
		return kIOReturnBadArgument;
	}

	IOLockLock(gPanthorLock);
	uint32_t hw_group = fGroups[req->group_handle - 1u];
	IOLockUnlock(gPanthorLock);
	if (hw_group == 0u) {
		return kIOReturnNotFound;
	}

	for (uint32_t i = 0; i < req->queue_submits.count; i++) {
		struct drm_panthor_queue_submit qs;
		int cerr = panthor_copyin_stride(req->queue_submits.array,
		    req->queue_submits.stride, i, &qs, sizeof(qs));
		if (cerr != 0) {
			return cerr == EFAULT ? kIOReturnVMError : kIOReturnBadArgument;
		}
		if (qs.pad != 0u || qs.syncs.count > PANTHOR_MAX_SYNC_OPS ||
		    (qs.syncs.count != 0u &&
		    (qs.syncs.array == 0ull || qs.syncs.stride < sizeof(struct drm_panthor_sync_op)))) {
			return kIOReturnBadArgument;
		}

		/* 1. Wait on all input sync_ops. */
		for (uint32_t s = 0; s < qs.syncs.count; s++) {
			struct drm_panthor_sync_op sop;
			cerr = panthor_copyin_stride(qs.syncs.array, qs.syncs.stride, s,
			    &sop, sizeof(sop));
			if (cerr != 0) {
				return cerr == EFAULT ? kIOReturnVMError : kIOReturnBadArgument;
			}
			uint32_t htype = sop.flags & DRM_PANTHOR_SYNC_OP_HANDLE_TYPE_MASK;
			if ((sop.flags & ~(DRM_PANTHOR_SYNC_OP_SIGNAL |
			    DRM_PANTHOR_SYNC_OP_HANDLE_TYPE_MASK)) != 0u ||
			    htype > DRM_PANTHOR_SYNC_OP_HANDLE_TYPE_TIMELINE_SYNCOBJ ||
			    (htype == DRM_PANTHOR_SYNC_OP_HANDLE_TYPE_SYNCOBJ &&
			    sop.timeline_value != 0ull)) {
				return kIOReturnBadArgument;
			}
			if ((sop.flags & DRM_PANTHOR_SYNC_OP_SIGNAL) == 0u) {
				uint64_t pt = (htype == DRM_PANTHOR_SYNC_OP_HANDLE_TYPE_TIMELINE_SYNCOBJ)
				    ? sop.timeline_value : 0ull;
				bool skip_wait = false;
				IOLockLock(gPanthorLock);
				bool is_ready = false, unsubmitted = false;
				struct panthor_sync_fence f;
				if (lookupSyncFenceLocked(sop.handle, pt, false, &is_ready,
				    &unsubmitted, &f) &&
				    (is_ready || (!unsubmitted && f.hw_group == hw_group &&
				     f.queue_idx == qs.queue_index))) {
					/* Same CSF FIFO queue: hardware WAIT + L2/LSC flush already serialises jobs. */
					skip_wait = true;
				}
				IOLockUnlock(gPanthorLock);
				if (!skip_wait) {
					IOReturn wret = waitSyncTargetsUnlocked(&sop.handle,
					    htype == DRM_PANTHOR_SYNC_OP_HANDLE_TYPE_TIMELINE_SYNCOBJ ? &pt : NULL,
					    1u, DRM_SYNCOBJ_WAIT_FLAGS_WAIT_ALL, 0x7fffffffffffffffll, NULL);
					if (wret != kIOReturnSuccess) {
						return wret;
					}
				}
			}
		}

		/* 2. Validate signal sync_ops before enqueuing hardware job. */
		IOLockLock(gPanthorLock);
		if (fGroups[req->group_handle - 1u] != hw_group) {
			IOLockUnlock(gPanthorLock);
			return kIOReturnNotFound;
		}
		for (uint32_t s = 0; s < qs.syncs.count; s++) {
			struct drm_panthor_sync_op sop;
			if (panthor_copyin_stride(qs.syncs.array, qs.syncs.stride, s,
			    &sop, sizeof(sop)) != 0) {
				IOLockUnlock(gPanthorLock);
				return kIOReturnVMError;
			}
			if ((sop.flags & DRM_PANTHOR_SYNC_OP_SIGNAL) != 0u &&
			    syncobjLocked(sop.handle) == NULL) {
				IOLockUnlock(gPanthorLock);
				return kIOReturnBadArgument;
			}
		}

		/* 3. Flush CPU-cached MiniIOSurface pixels before enqueuing GPU job. */
		flushImportedSurfacesPreSubmitLocked();

		uint64_t gpu_seq = 0ull;
		int serr = hakios_mali610_queue_submit(hw_group, qs.queue_index,
		    qs.stream_addr, qs.stream_size, qs.latest_flush, &gpu_seq);
		if (serr != 0) {
			IOLockUnlock(gPanthorLock);
			return panthor_errno_to_ior(serr);
		}
		if (gpu_seq != 0ull) {
			tagImportedSurfacesPostSubmitLocked(hw_group, qs.queue_index, gpu_seq);
		}
		if (gFlushIdPtr != NULL) {
			uint32_t fid = hakios_mali610_bump_flush_id();
			__atomic_store_n(&gFlushIdPtr[0], fid ? fid : 1u, __ATOMIC_RELEASE);
		}

		/* 4. Attach (hw_group, queue_index, gpu_seq) fence to all output sync_ops. */
		struct panthor_sync_fence fence = {
			.hw_group = gpu_seq == 0ull ? 0u : hw_group,
			.queue_idx = qs.queue_index,
			.target_seq = gpu_seq,
		};
		for (uint32_t s = 0; s < qs.syncs.count; s++) {
			struct drm_panthor_sync_op sop;
			(void)panthor_copyin_stride(qs.syncs.array, qs.syncs.stride, s,
			    &sop, sizeof(sop));
			if ((sop.flags & DRM_PANTHOR_SYNC_OP_SIGNAL) == 0u) {
				continue;
			}
			struct panthor_syncobj *so = syncobjLocked(sop.handle);
			if (so == NULL) {
				continue;
			}
			uint32_t htype = sop.flags & DRM_PANTHOR_SYNC_OP_HANDLE_TYPE_MASK;
			if (htype == DRM_PANTHOR_SYNC_OP_HANDLE_TYPE_TIMELINE_SYNCOBJ) {
				recordTimelineFenceLocked(so, sop.timeline_value, fence);
			} else {
				if (gpu_seq == 0ull) {
					so->binary_signaled = true;
					so->has_binary_fence = false;
				} else {
					so->binary_signaled = false;
					so->has_binary_fence = true;
					so->binary_fence = fence;
				}
			}
		}
		IOLockUnlock(gPanthorLock);
	}
	return kIOReturnSuccess;
}

IOReturn
HakiosMali610UserClient::tilerHeapCreate(struct drm_panthor_tiler_heap_create *req)
{
	if (req->initial_chunk_count == 0u || req->initial_chunk_count > req->max_chunks ||
	    (req->chunk_size & 0xfffu) != 0u ||
	    req->chunk_size < (128u << 10) || req->chunk_size > (8u << 20)) {
		return kIOReturnBadArgument;
	}
	if (findClientVmLocked(req->vm_id) == NULL) {
		return kIOReturnBadArgument;
	}
	struct panthor_tiler_heap *h = NULL;
	uint32_t handle = 0u;
	for (uint32_t i = 0; i < PANTHOR_MAX_HEAPS; i++) {
		if (!fHeaps[i].active) {
			h = &fHeaps[i];
			handle = i + 1u;
			break;
		}
	}
	if (h == NULL) {
		return kIOReturnNoResources;
	}

	uint32_t nchunks = req->initial_chunk_count;
	if (nchunks > PANTHOR_MAX_HEAP_CHUNKS ||
	    (uint64_t)nchunks * req->chunk_size > PANTHOR_MAX_HEAP_BYTES) {
		return kIOReturnNoMemory;
	}

	memset(h, 0, sizeof(*h));
	h->active = true;
	h->vm_id = req->vm_id;
	h->chunk_size = req->chunk_size;
	h->global_next = gActiveHeapList;
	gActiveHeapList = h;

	/* 1. Allocate 64B-aligned zeroed GPU heap context in VM kernel auto-VA. */
	h->ctx_mem = IOBufferMemoryDescriptor::inTaskWithOptions(kernel_task,
	    kIODirectionInOut, PAGE_SIZE, PAGE_SIZE);
	if (h->ctx_mem == NULL) {
		freeHeapLocked(h);
		return kIOReturnNoMemory;
	}
	void *ctx_kva = h->ctx_mem->getBytesNoCopy();
	memset(ctx_kva, 0, PAGE_SIZE);
	hakios_mali610_flush_dcache(ctx_kva, 0x1000u);

	IOByteCount seg_len = 0;
	uint64_t ctx_pa = (uint64_t)h->ctx_mem->getPhysicalSegment(0, &seg_len, kIOMemoryMapperNone);
	if (ctx_pa == 0ull ||
	    hakios_mali610_vm_alloc_kernel_va(req->vm_id, 0x1000ull, 0x1000ull, &h->ctx_gpu_va) != 0 ||
	    hakios_mali610_vm_map_pas(req->vm_id, h->ctx_gpu_va, &ctx_pa, 1u,
	    DRM_PANTHOR_VM_BIND_OP_MAP_NOEXEC) != 0) {
		freeHeapLocked(h);
		return kIOReturnNoMemory;
	}

	/* 2. Allocate and singly-link initial tiler heap chunks in VM kernel auto-VA. */
	for (uint32_t c = 0; c < nchunks; c++) {
		struct panthor_heap_chunk *chunk = NULL;
		if (!panthor_alloc_heap_chunk_locked(req->vm_id, req->chunk_size, &chunk)) {
			freeHeapLocked(h);
			return kIOReturnNoMemory;
		}
		if (h->chunk_tail != NULL) {
			uint64_t *prev_hdr = (uint64_t *)h->chunk_tail->mem->getBytesNoCopy();
			prev_hdr[0] = (chunk->gpu_va & ~0xfffull) |
			    ((uint64_t)req->chunk_size >> 12);
			hakios_mali610_flush_dcache(prev_hdr, 64u);
			h->chunk_tail->next = chunk;
		} else {
			h->chunk_head = chunk;
		}
		h->chunk_tail = chunk;
		h->chunk_count++;
	}

	panthor_publish_flush_id();
	req->handle = handle;
	req->tiler_heap_ctx_gpu_va = h->ctx_gpu_va;
	req->first_heap_chunk_gpu_va = h->chunk_head->gpu_va;
	return kIOReturnSuccess;
}

IOReturn
HakiosMali610UserClient::tilerHeapDestroy(struct drm_panthor_tiler_heap_destroy *req)
{
	if (req->pad != 0u || req->handle == 0u || req->handle > PANTHOR_MAX_HEAPS ||
	    fHeaps == NULL || !fHeaps[req->handle - 1u].active) {
		return kIOReturnBadArgument;
	}
	freeHeapLocked(&fHeaps[req->handle - 1u]);
	panthor_publish_flush_id();
	return kIOReturnSuccess;
}

IOReturn
HakiosMali610UserClient::surfaceBuffer(struct hakios_surface *req)
{
	MiniIOSurface *s = MiniIOSurfaceCopy(req->surface);
	if (s == NULL) {
		return kIOReturnNotFound;
	}
	if (s->client != NULL) {
		MiniIOSurfaceDrop(s);
		return kIOReturnUnsupported;
	}
	uint64_t size = round_page_64(s->info.allocSize);
	IOMemoryDescriptor *sub = IOSubMemoryDescriptor::withSubRange(
	    s->memory, PAGE_SIZE, (IOByteCount)size, kIODirectionInOut);
	if (sub == NULL || size == 0ull || size > PANTHOR_MAX_BO_SIZE) {
		OSSafeReleaseNULL(sub);
		MiniIOSurfaceDrop(s);
		return kIOReturnNoMemory;
	}
	req->size = size;
	req->offset = s->info.offset;
	req->rowBytes = s->info.bytesPerRow;
	req->width = s->info.width;
	req->height = s->info.height;
	req->pixelFormat = s->info.pixelFormat;
	req->planes = s->info.planeCount;

	if (req->handle == kHakiosDescribe) {
		req->handle = 0u;
		sub->release();
		MiniIOSurfaceDrop(s);
		return kIOReturnSuccess;
	}
	for (uint32_t i = 0; i < PANTHOR_MAX_BOS; i++) {
		if (fBos[i].memory != NULL && fBos[i].surface == s) {
			req->handle = i + 1u;
			sub->release();
			MiniIOSurfaceDrop(s);
			return kIOReturnSuccess;
		}
	}
	struct panthor_bo *slot = NULL;
	uint32_t handle = 0u;
	for (uint32_t n = 0; n < PANTHOR_MAX_BOS; n++) {
		uint32_t i = (fNextBo + n) % PANTHOR_MAX_BOS;
		if (fBos[i].memory == NULL) {
			slot = &fBos[i];
			handle = i + 1u;
			fNextBo = i + 1u;
			break;
		}
	}
	if (slot == NULL) {
		sub->release();
		MiniIOSurfaceDrop(s);
		return kIOReturnNoResources;
	}
	memset(slot, 0, sizeof(*slot));
	slot->memory = sub;
	slot->kva = s->kpixels;
	slot->surface = s;
	slot->size = size;
	slot->create_flags = DRM_PANTHOR_BO_WB_MMAP;
	slot->imported = true;
	if (slot->kva != NULL) {
		hakios_mali610_flush_dcache(slot->kva, (size_t)slot->size);
	}
	if (s->kbase != NULL) {
		slot->last_clean_seed = s->shared()->seed;
		slot->seed_valid = true;
	}
	if (fSurfaceBoCount < PANTHOR_MAX_SURFACE_TRACK) {
		fSurfaceBoHandles[fSurfaceBoCount++] = handle;
	} else {
		fSurfaceBoOverflow = true;
	}
	req->handle = handle;
	return kIOReturnSuccess;
}

IOReturn
HakiosMali610UserClient::syncobjCreate(struct drm_syncobj_create *req)
{
	if ((req->flags & ~DRM_SYNCOBJ_CREATE_SIGNALED) != 0u) {
		return kIOReturnBadArgument;
	}
	struct panthor_syncobj *so = NULL;
	uint32_t handle = 0u;
	for (uint32_t n = 0; n < PANTHOR_MAX_SYNCOBJS; n++) {
		uint32_t i = (fNextSyncobj + n) % PANTHOR_MAX_SYNCOBJS;
		if (!fSyncobjs[i].in_use) {
			so = &fSyncobjs[i];
			handle = i + 1u;
			fNextSyncobj = i + 1u;
			break;
		}
	}
	if (so == NULL) {
		return kIOReturnNoResources;
	}
	memset(so, 0, sizeof(*so));
	so->in_use = true;
	so->binary_signaled = (req->flags & DRM_SYNCOBJ_CREATE_SIGNALED) != 0u;
	req->handle = handle;
	return kIOReturnSuccess;
}

IOReturn
HakiosMali610UserClient::syncobjDestroy(struct drm_syncobj_destroy *req)
{
	if (req->pad != 0u) {
		return kIOReturnBadArgument;
	}
	struct panthor_syncobj *so = syncobjLocked(req->handle);
	if (so == NULL) {
		return kIOReturnNotFound;
	}
	memset(so, 0, sizeof(*so));
	return kIOReturnSuccess;
}

IOReturn
HakiosMali610UserClient::syncobjHandleToFd(struct drm_syncobj_handle *req)
{
	if (req->pad != 0u ||
	    (req->flags & ~DRM_SYNCOBJ_HANDLE_TO_FD_EXPORT_SYNC) != 0u) {
		return kIOReturnBadArgument;
	}
	struct panthor_syncobj *so = syncobjLocked(req->handle);
	if (so == NULL) {
		return kIOReturnNotFound;
	}
	uint32_t slot = 0xffffffffu;
	for (uint32_t i = 0; i < PANTHOR_MAX_SYNC_FILES; i++) {
		if (!fSyncFiles[i].in_use) {
			slot = i;
			break;
		}
	}
	if (slot == 0xffffffffu) {
		return kIOReturnNoResources;
	}
	bool ready = false, unsub = false;
	struct panthor_sync_fence f;
	(void)lookupSyncFenceLocked(req->handle, so->max_point, false, &ready, &unsub, &f);
	fSyncFiles[slot].in_use = true;
	fSyncFiles[slot].signaled = ready || unsub;
	fSyncFiles[slot].has_fence = !ready && !unsub && f.hw_group != 0u;
	fSyncFiles[slot].fence = f;
	req->fd = (int32_t)(0x4000u + slot);
	return kIOReturnSuccess;
}

IOReturn
HakiosMali610UserClient::syncobjFdToHandle(struct drm_syncobj_handle *req)
{
	if (req->pad != 0u ||
	    (req->flags & ~DRM_SYNCOBJ_FD_TO_HANDLE_IMPORT_SYNC) != 0u ||
	    req->fd < 0x4000 || (uint32_t)(req->fd - 0x4000) >= PANTHOR_MAX_SYNC_FILES) {
		return kIOReturnBadArgument;
	}
	uint32_t slot = (uint32_t)(req->fd - 0x4000);
	if (!fSyncFiles[slot].in_use) {
		return kIOReturnNotFound;
	}
	struct panthor_syncobj *so = NULL;
	if ((req->flags & DRM_SYNCOBJ_FD_TO_HANDLE_IMPORT_SYNC) != 0u) {
		so = syncobjLocked(req->handle);
		if (so == NULL) {
			return kIOReturnNotFound;
		}
	} else {
		struct drm_syncobj_create cr = { 0u, 0u };
		IOReturn cret = syncobjCreate(&cr);
		if (cret != kIOReturnSuccess) {
			return cret;
		}
		req->handle = cr.handle;
		so = syncobjLocked(cr.handle);
	}
	so->binary_signaled = fSyncFiles[slot].signaled;
	so->has_binary_fence = fSyncFiles[slot].has_fence;
	so->binary_fence = fSyncFiles[slot].fence;
	memset(&fSyncFiles[slot], 0, sizeof(fSyncFiles[slot]));
	return kIOReturnSuccess;
}

IOReturn
HakiosMali610UserClient::syncobjResetOrSignal(struct drm_syncobj_array *req, bool signal)
{
	if (req->pad != 0u || req->count_handles > PANTHOR_MAX_SYNCOBJS) {
		return kIOReturnBadArgument;
	}
	if (req->count_handles == 0u) {
		return kIOReturnSuccess;
	}
	if (req->handles == 0ull) {
		return kIOReturnBadArgument;
	}
	for (uint32_t off = 0; off < req->count_handles; off += 64u) {
		uint32_t handles[64];
		uint32_t batch = (req->count_handles - off) > 64u ? 64u : (req->count_handles - off);
		if (copyin((user_addr_t)(req->handles + (uint64_t)off * sizeof(uint32_t)),
		    handles, batch * sizeof(uint32_t)) != 0) {
			return kIOReturnVMError;
		}
		for (uint32_t k = 0; k < batch; k++) {
			struct panthor_syncobj *so = syncobjLocked(handles[k]);
			if (so == NULL) {
				return kIOReturnNotFound;
			}
			if (signal) {
				so->binary_signaled = true;
				so->has_binary_fence = false;
			} else {
				memset(so, 0, sizeof(*so));
				so->in_use = true;
			}
		}
	}
	return kIOReturnSuccess;
}

IOReturn
HakiosMali610UserClient::syncobjQueryOrTimelineSignal(struct drm_syncobj_timeline_array *req,
    bool signal)
{
	if (req->count_handles > PANTHOR_MAX_SYNCOBJS) {
		return kIOReturnBadArgument;
	}
	if (signal && req->flags != 0u) {
		return kIOReturnBadArgument;
	}
	if (!signal && (req->flags & ~DRM_SYNCOBJ_QUERY_FLAGS_LAST_SUBMITTED) != 0u) {
		return kIOReturnBadArgument;
	}
	if (req->count_handles == 0u) {
		return kIOReturnSuccess;
	}
	if (req->handles == 0ull || req->points == 0ull) {
		return kIOReturnBadArgument;
	}
	bool last_sub = (req->flags & DRM_SYNCOBJ_QUERY_FLAGS_LAST_SUBMITTED) != 0u;

	for (uint32_t off = 0; off < req->count_handles; off += 32u) {
		uint32_t handles[32];
		uint64_t points[32];
		uint32_t batch = (req->count_handles - off) > 32u ? 32u : (req->count_handles - off);

		if (copyin((user_addr_t)(req->handles + (uint64_t)off * sizeof(uint32_t)),
		    handles, batch * sizeof(uint32_t)) != 0) {
			return kIOReturnVMError;
		}
		if (signal) {
			if (copyin((user_addr_t)(req->points + (uint64_t)off * sizeof(uint64_t)),
			    points, batch * sizeof(uint64_t)) != 0) {
				return kIOReturnVMError;
			}
			for (uint32_t k = 0; k < batch; k++) {
				struct panthor_syncobj *so = syncobjLocked(handles[k]);
				if (so == NULL) {
					return kIOReturnNotFound;
				}
				struct panthor_sync_fence imm = { 0u, 0u, 0ull };
				recordTimelineFenceLocked(so, points[k], imm);
			}
		} else {
			for (uint32_t k = 0; k < batch; k++) {
				struct panthor_syncobj *so = syncobjLocked(handles[k]);
				if (so == NULL) {
					return kIOReturnNotFound;
				}
				if (!last_sub) {
					for (unsigned p = 0; p < PANTHOR_SYNCOBJ_POINTS; p++) {
						if (so->pending[p].point > so->signaled_point &&
						    so->pending[p].fence.hw_group != 0u) {
							bool done = false;
							if (hakios_mali610_queue_poll(
							    so->pending[p].fence.hw_group,
							    so->pending[p].fence.queue_idx,
							    so->pending[p].fence.target_seq,
							    &done) == 0 && done) {
								/* Leave surface->maliSeq for MiniIOSurfaceDrawn() to flush on demand. */
								so->signaled_point = so->pending[p].point;
								so->pending[p].point = 0ull;
							}
						}
					}
				}
				points[k] = last_sub ? so->max_point : so->signaled_point;
			}
			if (copyout(points,
			    (user_addr_t)(req->points + (uint64_t)off * sizeof(uint64_t)),
			    batch * sizeof(uint64_t)) != 0) {
				return kIOReturnVMError;
			}
		}
	}
	return kIOReturnSuccess;
}

IOReturn
HakiosMali610UserClient::syncobjTransfer(struct drm_syncobj_transfer *req)
{
	if (req->pad != 0u ||
	    (req->flags & ~(DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT |
	    DRM_SYNCOBJ_WAIT_FLAGS_WAIT_AVAILABLE)) != 0u) {
		return kIOReturnBadArgument;
	}
	struct panthor_syncobj *src = syncobjLocked(req->src_handle);
	struct panthor_syncobj *dst = syncobjLocked(req->dst_handle);
	if (src == NULL || dst == NULL) {
		return kIOReturnNotFound;
	}

	uint64_t src_pt = req->src_point;
	if (src_pt == 0ull && !src->has_binary_fence && !src->binary_signaled && src->max_point > 0ull) {
		src_pt = src->max_point;
	}
	bool ready = false, unsubmitted = false;
	struct panthor_sync_fence fence;
	(void)lookupSyncFenceLocked(req->src_handle, src_pt, false, &ready, &unsubmitted, &fence);
	if (unsubmitted) {
		return kIOReturnNotFound;
	}

	if (req->dst_point == 0ull) {
		if (ready || fence.hw_group == 0u) {
			dst->binary_signaled = true;
			dst->has_binary_fence = false;
		} else {
			dst->binary_signaled = false;
			dst->has_binary_fence = true;
			dst->binary_fence = fence;
		}
	} else {
		if (ready) {
			struct panthor_sync_fence imm = { 0u, 0u, 0ull };
			recordTimelineFenceLocked(dst, req->dst_point, imm);
		} else {
			recordTimelineFenceLocked(dst, req->dst_point, fence);
		}
	}
	return kIOReturnSuccess;
}

IOReturn
HakiosMali610UserClient::clientClose(void)
{
	if (gPanthorLock != NULL) {
		IOLockLock(gPanthorLock);
		for (uint32_t g = 0; g < PANTHOR_MAX_CLIENT_GROUPS; g++) {
			if (fGroups[g] != 0u) {
				uint32_t hw_grp = fGroups[g];
				fGroups[g] = 0u;
				fGroupVms[g] = 0u;
				(void)hakios_mali610_group_destroy(hw_grp);
			}
		}
		for (uint32_t h = 0; fHeaps != NULL && h < PANTHOR_MAX_HEAPS; h++) {
			if (fHeaps[h].active) {
				freeHeapLocked(&fHeaps[h]);
			}
		}
		for (uint32_t v = 0; v < PANTHOR_MAX_CLIENT_VMS; v++) {
			if (fVms[v].active) {
				uint32_t vm_id = fVms[v].vm_id;
				memset(&fVms[v], 0, sizeof(fVms[v]));
				(void)hakios_mali610_vm_destroy(vm_id);
			}
		}
		for (uint32_t i = 0; fBos != NULL && i < PANTHOR_MAX_BOS; i++) {
			if (fBos[i].memory != NULL) {
				freeBoLocked(&fBos[i]);
			}
		}
		if (fSyncobjs != NULL) {
			memset(fSyncobjs, 0, sizeof(struct panthor_syncobj) * PANTHOR_MAX_SYNCOBJS);
		}
		memset(fSyncFiles, 0, sizeof(fSyncFiles));
		fSurfaceBoCount = 0u;
		fSurfaceBoOverflow = false;
		IOLockUnlock(gPanthorLock);
	}
	terminate();
	return kIOReturnSuccess;
}

IOReturn
HakiosMali610UserClient::clientMemoryForType(UInt32 type, IOOptionBits *options,
    IOMemoryDescriptor **memory)
{
	if (!hakios_mali610_ready()) {
		return kIOReturnNotReady;
	}
	if (type == kHakiosFlushIdMemoryType) {
		if (gFlushIdMem == NULL) {
			return kIOReturnNoMemory;
		}
		gFlushIdMem->retain();
		*memory = gFlushIdMem;
		*options = kIOMapReadOnly;
		return kIOReturnSuccess;
	}

	uint32_t handle = type;
	if (type >= (uint32_t)kHakiosMmapBaseOffset + (1u << 14) &&
	    ((type - (uint32_t)kHakiosMmapBaseOffset) & 0x3fffu) == 0u) {
		handle = (type - (uint32_t)kHakiosMmapBaseOffset) >> 14;
	}

	IOReturn ret = kIOReturnBadArgument;
	IOLockLock(gPanthorLock);
	struct panthor_bo *b = bo(handle);
	if (b != NULL && (b->create_flags & DRM_PANTHOR_BO_NO_MMAP) == 0u) {
		b->memory->retain();
		*memory = b->memory;
		*options = 0;
		ret = kIOReturnSuccess;
	}
	IOLockUnlock(gPanthorLock);
	return ret;
}

IOReturn
HakiosMali610UserClient::externalMethod(uint32_t selector, IOExternalMethodArguments *args,
    __unused IOExternalMethodDispatch *dispatch, __unused OSObject *target,
    __unused void *reference)
{
	union {
		struct drm_gem_close gem_close;
		struct drm_panthor_dev_query dev_query;
		struct drm_panthor_vm_create vm_create;
		struct drm_panthor_vm_destroy vm_destroy;
		struct drm_panthor_vm_bind vm_bind;
		struct drm_panthor_vm_get_state vm_state;
		struct drm_panthor_bo_create bo_create;
		struct drm_panthor_bo_mmap_offset bo_mmap;
		struct drm_panthor_group_create group_create;
		struct drm_panthor_group_destroy group_destroy;
		struct drm_panthor_group_submit group_submit;
		struct drm_panthor_group_get_state group_state;
		struct drm_panthor_tiler_heap_create heap_create;
		struct drm_panthor_tiler_heap_destroy heap_destroy;
		struct drm_panthor_bo_set_label bo_label;
		struct drm_panthor_set_user_mmio_offset mmio_off;
		struct drm_panthor_bo_sync bo_sync;
		struct drm_panthor_bo_query_info bo_info;
		struct drm_syncobj_create sync_create;
		struct drm_syncobj_destroy sync_destroy;
		struct drm_syncobj_handle sync_fd;
		struct drm_syncobj_transfer sync_xfer;
		struct drm_syncobj_wait sync_wait;
		struct drm_syncobj_timeline_wait sync_twait;
		struct drm_syncobj_array sync_arr;
		struct drm_syncobj_timeline_array sync_tarr;
		struct hakios_surface surface;
		struct hakios_show show;
		uint8_t raw[256];
	} r;
	size_t size = args->structureInputSize;

	if (args->structureInput == NULL || size == 0u || size > sizeof(r) ||
	    fBos == NULL || fHeaps == NULL || fSyncobjs == NULL || gPanthorLock == NULL) {
		return kIOReturnBadArgument;
	}
	if (!hakios_mali610_ready()) {
		return kIOReturnNotReady;
	}
	memset(&r, 0, sizeof(r));
	memcpy(&r, args->structureInput, size);

	IOReturn ret = kIOReturnSuccess;

	/* Dispatch sleeping/copyin operations without holding gPanthorLock across waits. */
	switch (selector) {
	case kPanthorDevQuery:
		ret = devQuery(&r.dev_query);
		goto out;
	case kPanthorVmBind:
		ret = vmBind(&r.vm_bind);
		goto out;
	case kPanthorGroupSubmit:
		ret = groupSubmit(&r.group_submit);
		goto out;
	case kDrmSyncobjWait: {
		if (r.sync_wait.pad != 0u ||
		    r.sync_wait.count_handles > PANTHOR_MAX_WAIT_HANDLES ||
		    (r.sync_wait.flags & ~(DRM_SYNCOBJ_WAIT_FLAGS_WAIT_ALL |
		    DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT |
		    DRM_SYNCOBJ_WAIT_FLAGS_WAIT_AVAILABLE |
		    DRM_SYNCOBJ_WAIT_FLAGS_WAIT_DEADLINE)) != 0u) {
			return kIOReturnBadArgument;
		}
		if (r.sync_wait.count_handles == 0u) {
			goto out;
		}
		uint32_t handles[PANTHOR_MAX_WAIT_HANDLES];
		if (r.sync_wait.handles == 0ull ||
		    copyin((user_addr_t)r.sync_wait.handles, handles,
		    r.sync_wait.count_handles * sizeof(uint32_t)) != 0) {
			return kIOReturnVMError;
		}
		ret = waitSyncTargetsUnlocked(handles, NULL, r.sync_wait.count_handles,
		    r.sync_wait.flags, r.sync_wait.timeout_nsec, &r.sync_wait.first_signaled);
		goto out;
	}
	case kDrmSyncobjTimelineWait: {
		if (r.sync_twait.pad != 0u ||
		    r.sync_twait.count_handles > PANTHOR_MAX_WAIT_HANDLES ||
		    (r.sync_twait.flags & ~(DRM_SYNCOBJ_WAIT_FLAGS_WAIT_ALL |
		    DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT |
		    DRM_SYNCOBJ_WAIT_FLAGS_WAIT_AVAILABLE |
		    DRM_SYNCOBJ_WAIT_FLAGS_WAIT_DEADLINE)) != 0u) {
			return kIOReturnBadArgument;
		}
		if (r.sync_twait.count_handles == 0u) {
			goto out;
		}
		uint32_t handles[PANTHOR_MAX_WAIT_HANDLES];
		uint64_t points[PANTHOR_MAX_WAIT_HANDLES];
		if (r.sync_twait.handles == 0ull || r.sync_twait.points == 0ull ||
		    copyin((user_addr_t)r.sync_twait.handles, handles,
		    r.sync_twait.count_handles * sizeof(uint32_t)) != 0 ||
		    copyin((user_addr_t)r.sync_twait.points, points,
		    r.sync_twait.count_handles * sizeof(uint64_t)) != 0) {
			return kIOReturnVMError;
		}
		ret = waitSyncTargetsUnlocked(handles, points, r.sync_twait.count_handles,
		    r.sync_twait.flags, r.sync_twait.timeout_nsec, &r.sync_twait.first_signaled);
		goto out;
	}
	case kHakiosShow: {
		uint64_t bytes = (uint64_t)r.show.rowBytes * r.show.height;
		uint8_t *pixels = NULL;
		if (r.show.width == 0u || r.show.width > 4096u ||
		    r.show.height == 0u || r.show.height > 4096u ||
		    r.show.rowBytes < 4u * r.show.width ||
		    bytes > (64ull << 20) ||
		    (pixels = (uint8_t *)IOMallocData(bytes)) == NULL) {
			return kIOReturnBadArgument;
		}
		if (copyin((user_addr_t)r.show.pixels, pixels, bytes) != 0) {
			ret = kIOReturnVMError;
		} else {
			MiniIOMobileFramebufferShow(pixels, r.show.width, r.show.height,
			    r.show.rowBytes);
		}
		IOFreeData(pixels, bytes);
		goto out;
	}
	default:
		break;
	}

	IOLockLock(gPanthorLock);
	switch (selector) {
	case kDrmGemClose: {
		if (r.gem_close.pad != 0u) {
			ret = kIOReturnBadArgument;
			break;
		}
		struct panthor_bo *b = bo(r.gem_close.handle);
		if (b == NULL) {
			ret = kIOReturnNotFound;
		} else {
			freeBoLocked(b);
		}
		break;
	}
	case kPanthorVmCreate:
		ret = vmCreate(&r.vm_create);
		break;
	case kPanthorVmDestroy:
		ret = vmDestroy(&r.vm_destroy);
		break;
	case kPanthorVmGetState:
		ret = vmGetState(&r.vm_state);
		break;
	case kPanthorBoCreate:
		ret = boCreate(&r.bo_create);
		break;
	case kPanthorBoMmapOffset:
		ret = boMmapOffset(&r.bo_mmap);
		break;
	case kPanthorGroupCreate:
		ret = groupCreate(&r.group_create);
		break;
	case kPanthorGroupDestroy:
		ret = groupDestroy(&r.group_destroy);
		break;
	case kPanthorGroupGetState:
		ret = groupGetState(&r.group_state);
		break;
	case kPanthorTilerHeapCreate:
		ret = tilerHeapCreate(&r.heap_create);
		break;
	case kPanthorTilerHeapDestroy:
		ret = tilerHeapDestroy(&r.heap_destroy);
		break;
	case kPanthorBoSetLabel:
		if (r.bo_label.pad != 0u) {
			ret = kIOReturnBadArgument;
		} else if (bo(r.bo_label.handle) == NULL) {
			ret = kIOReturnNotFound;
		}
		break;
	case kPanthorSetUserMmioOffset:
		if (r.mmio_off.offset != DRM_PANTHOR_USER_MMIO_OFFSET_32BIT &&
		    r.mmio_off.offset != DRM_PANTHOR_USER_MMIO_OFFSET_64BIT) {
			ret = kIOReturnBadArgument;
		} else {
			fUserMmioOffset = r.mmio_off.offset;
		}
		break;
	case kPanthorBoSync:
		ret = boSync(&r.bo_sync);
		break;
	case kPanthorBoQueryInfo:
		ret = boQueryInfo(&r.bo_info);
		break;
	case kHakiosSurface:
		ret = surfaceBuffer(&r.surface);
		break;
	case kDrmSyncobjCreate:
		ret = syncobjCreate(&r.sync_create);
		break;
	case kDrmSyncobjDestroy:
		ret = syncobjDestroy(&r.sync_destroy);
		break;
	case kDrmSyncobjHandleToFd:
		ret = syncobjHandleToFd(&r.sync_fd);
		break;
	case kDrmSyncobjFdToHandle:
		ret = syncobjFdToHandle(&r.sync_fd);
		break;
	case kDrmSyncobjReset:
		ret = syncobjResetOrSignal(&r.sync_arr, false);
		break;
	case kDrmSyncobjSignal:
		ret = syncobjResetOrSignal(&r.sync_arr, true);
		break;
	case kDrmSyncobjQuery:
		ret = syncobjQueryOrTimelineSignal(&r.sync_tarr, false);
		break;
	case kDrmSyncobjTimelineSignal:
		ret = syncobjQueryOrTimelineSignal(&r.sync_tarr, true);
		break;
	case kDrmSyncobjTransfer:
		ret = syncobjTransfer(&r.sync_xfer);
		break;
	default:
		IOLog("HakiosGPU: unsupported selector %#x (%zu bytes)\n", selector, size);
		ret = kIOReturnUnsupported;
		break;
	}
	IOLockUnlock(gPanthorLock);

out:
	if (args->structureOutput != NULL && args->structureOutputSize >= size) {
		memcpy(args->structureOutput, &r, size);
		args->structureOutputSize = (uint32_t)size;
	}
	return ret;
}
