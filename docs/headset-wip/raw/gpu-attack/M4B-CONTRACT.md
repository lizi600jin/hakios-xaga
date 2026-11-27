# M4b 实现契约：`CS_TILER_OOM` 动态扩容 + `MiniIOSurface` 零拷贝同步 + 100 帧稳态模糊压测

> **起草**：Neon（架构 / 契约 / 根因分析）  
> **执行**：Carbon（内核 + Mesa 补丁 + `gl_blur_test` 实现）  
> **验收**：DSH（机械自检 → 编译内核/Mesa/userland → 打包 `GL=test` → 刷机实测 `gl_blur_test` + `gl_test fill` + `RETURN`）  
> **前置基线**：`golden/boot-995-m4a-pass.img`（`Mali-G610 (Panfrost)` `OpenGL ES 3.1 Mesa 25.0.7`，三角形 26% 精确命中、上屏成功、0 fault、`RETURN 30s`）。  
> **隔离纪律**：M4b 仍保持 **`GL=test`**，**不切 `backboardd` / `SpringBoard` 到 GPU**（留给 M5）。

---

## 0. M4b 解决的四个核心问题

1. **Mesa `panthor_kmod.c` 导入 BO 的 `dma-buf` 死路径修复（头号必踩坑！）**：
   - M4a 已通过 `osmesa_surface_resource()` 把 `MiniIOSurface` 接到 `panfrost_resource_from_handle()` → `panthor_kmod_bo_import()`。
   - **但 `panthor_kmod_bo_import()` 会置 `PAN_KMOD_BO_FLAG_IMPORTED`**，导致 `panthor_kmod_bo_wait()`、`panthor_kmod_bo_attach_sync_point()`、`panthor_kmod_bo_get_sync_point()` 全部走 `if (shared)` 分支，去调用 Linux 专属的 `drmPrimeHandleToFD()` + `DMA_BUF_IOCTL_{EXPORT,IMPORT}_SYNC_FILE`（在 Darwin 上必返 `ENOSYS` 失败！）。
   - 修复后，`MiniIOSurface` 导入 BO 直接复用已跑通的 `panthor_bo->sync.handle` timeline syncobj 路径，零拷贝即可端到端跑通。
2. **`MiniIOSurface` 缓存一致性 + `hakios_mali_surface_drawn()` 同步**：
   - `MiniIOSurface` 像素页是 CPU 侧 **Cached（WB）** 内存，而 Mali-G610 是 **non-coherent（`selected_coherency = NONE`）**。
   - 必须保证：
     - **CPU 写 → GPU 读**：在 `surfaceBuffer()` 导入时和 `groupSubmit()` 提交前，对活跃 `MiniIOSurface` BO 做 `hakios_mali610_flush_dcache()`（clean+inv 到 PoC）；
     - **GPU 写 → CPU / 显示读**：在 `groupSubmit()` 后给关联 `MiniIOSurface` 写入编码后的 `surface->maliSeq`，并在 `hakios_mali_surface_drawn(surface, ms)`（供 `MiniIOMobileFramebuffer::show()` 和 `IOSurfaceLock` 等待）及 `waitSyncTargetsUnlocked()` job 完成路径里，等完 GPU `SYNC_ADD64`（其前已含 `FLUSH_CACHE2`）后执行 `hakios_mali610_flush_dcache()`，防止 CPU 读到脏 cache 行。
3. **`CS_TILER_OOM` 动态 chunk 扩容（含 MT6895 `be527d1` 勘误与防死锁锁序）**：
   - 当前 `hakios.mali610.c:8229-8238` 已按 MT6895 `be527d1`（`(((cs_req ^ cs_ack) | cs_req) & kCsTilerOomBit) != 0`）捕获了 `CS_TILER_OOM`，但目前回填 `heap_start = 0; heap_end = 0`（等于拒绝扩容）。
   - M4b 改为动态分配新 chunk 映射进 VM，按 Valhall CSF 硬件规范把 `(chunk_va & ~0xfffull) | (chunk_size >> 12)` 写回 `cs_in->heap_start` / `cs_in->heap_end` 并应答固件。
4. **独立验收程序 `userland/src/gl_blur_test.c`（含稳态帧时间实测）**：
   - 验证零拷贝 `MiniIOSurface` 2-pass 高斯模糊 + `colorMatrix` 连续 **100 帧**，输出**稳态单帧 GPU 耗时（min/avg/max ms）**，直接读 `IOSurfaceGetBaseAddress()` 校验像素（不走 `glReadPixels`），并 `kHakiosShow` 上屏；
   - 同程序另跑一轮高顶点数 Tiler OOM 压测（触发并验证 `CS_TILER_OOM grown chunk` 真实生效、0 fault）。
   - 顺带在实机跑一次现成的 `/redwood/bin/gl_test fill 1080 2400` 拿到无冷启动开销的纯 GPU 稳态填充耗时基线。

---

## 1. 需修改/新增文件清单

| # | 文件路径（相对 `/home/lizi/IOS/hakios-mtk`） | 动作 | 同步要求 |
|---|---|---|---|
| 1 | `tools/mesa/panfrost-darwin.patch` | 修改（增量补 `panthor_kmod.c` 导入 BO 同步路径） | 重新运行 `DRIVERS="softpipe,panfrost" tools/mk-mesa.sh` |
| 2 | `logs/ufs-apfs-fstree-root-736/kernel-src/iokit/Kernel/hakios.mali610.cpp` | 修改 | **必须与 `mtk/mt6895/drivers/gpu/hakios.mali610.cpp` 逐字节一致** |
| 3 | `mtk/mt6895/drivers/gpu/hakios.mali610.cpp` | 同步 | 同上（`cmp -s` + `sha256sum` 校验） |
| 4 | `logs/ufs-apfs-fstree-root-736/kernel-src/bsd/dev/hakios.mali610.c` | 修改 | **必须与 `mtk/mt6895/drivers/gpu/hakios.mali610.c` 逐字节一致** |
| 5 | `mtk/mt6895/drivers/gpu/hakios.mali610.c` | 同步 | 同上（`cmp -s` + `sha256sum` 校验） |
| 6 | `userland/src/gl_blur_test.c` | **新增** | M4b 零拷贝 + 100 帧模糊 + `CS_TILER_OOM` 验收测试 |
| 7 | `userland/build.sh` | 修改 | 在 `GL` 分支编译 `obj/gl_blur_test`（链接 `CoreFoundation`, `IOKit`, `IOSurface`, `libOSMesa.8.dylib`）并复制到 `root/bin/gl_blur_test`（`chmod 0755`） |

---

## 2. 模块一契约：Mesa `panfrost-darwin.patch` 增量修改

在 `tools/mesa/panfrost-darwin.patch` 的 `src/panfrost/lib/kmod/panthor_kmod.c` hunk 中追加 4 处 `#ifdef __APPLE__`（保持对非 Darwin 零改动）：

1. **`panthor_kmod_bo_import()`（约第 402 行）**：
   ```c
   #ifdef __APPLE__
      /* Darwin imports MiniIOSurface via kHakiosSurface; no Linux dma-buf implicit sync.
       * Use the BO's own timeline syncobj (signaled initially), matching non-shared BOs. */
      int ret = drmSyncobjCreate(dev->fd, DRM_SYNCOBJ_CREATE_SIGNALED,
                                 &panthor_bo->sync.handle);
   #else
      int ret = drmSyncobjCreate(dev->fd, 0, &panthor_bo->sync.handle);
   #endif
   ```
   并在 `panthor_kmod_bo_import()` 成功路径中初始化 `panthor_bo->sync.read_point = 0; panthor_bo->sync.write_point = 0;`（`pan_kmod_dev_alloc` 已清零，显式确认）。
2. **`panthor_kmod_bo_wait()`（约第 486 行）**：
   ```c
   #ifdef __APPLE__
      bool shared = false;
   #else
      bool shared =
         bo->flags & (PAN_KMOD_BO_FLAG_EXPORTED | PAN_KMOD_BO_FLAG_IMPORTED);
   #endif
   ```
3. **`panthor_kmod_bo_attach_sync_point()`（约第 559 行）**：
   - 同上，`#ifdef __APPLE__` 下 `bool shared = false;`（此时因 `panthor_vm == NULL`，会走 `else` 分支调用已验证通过的 `drmSyncobjTransfer()` 把 `vm_sync_handle` 的 `sync_point` 传给 `panthor_bo->sync.handle`）。
4. **`panthor_kmod_bo_get_sync_point()`（约第 631 行）**：
   - 同上，`#ifdef __APPLE__` 下 `bool shared = false;`（直接返回 `panthor_bo->sync.handle` + `read_point`/`write_point` timeline sync point）。

> **核对**：`panfrost_bo_import()`（`pan_bo.c:485`）先调 `drmPrimeFDToHandle` 再调 `pan_kmod_bo_import`（其内部再调一次 `drmPrimeFDToHandle`），内核 `hakios.mali610.cpp:1956-1963` 已按 `fBos[i].surface == s` 去重返回同一 handle 且不泄漏引用，无需改 `pan_bo.c`。

---

## 3. 模块二契约：`CS_TILER_OOM` 动态 chunk 扩容（内核双文件）

### 3.1 Valhall CSF 硬件协议（已核对上游 Linux `panthor_heap.c` / `panthor_sched.c`）

当 CSF 固件触发 `CS_TILER_OOM`（bit 26 = `kCsTilerOomBit`）时：
1. 固件上报：
   - `qu->cs_out->heap_address` = 触发 OOM 的 **`tiler_heap_ctx_gpu_va`**（即 `TILER_HEAP_CREATE` 返回的 `h->ctx_gpu_va`）；
   - `qu->cs_out->heap_vt_start` / `heap_vt_end` / `heap_frag_end`（用于日志诊断）。
2. 内核分配 1 个新 chunk（大小 = `h->chunk_size`，从 `vm_id` 的 kernel auto-VA 分配 `cva` 并以 `DRM_PANTHOR_VM_BIND_OP_MAP_NOEXEC` 映射进 `vm_id`）：
   - 将新 chunk 的前 64 字节清零（`hdr[0] = 0ull` 表示独立单块）并 `hakios_mali610_flush_dcache(ckva, 64u)`；
   - **编码公式（严禁漏掉低 12 位！）**：
     ```c
     uint64_t encoded_va = (cva & ~0xfffull) | ((uint64_t)h->chunk_size >> 12);
     ```
   - 调用 `panthor_publish_flush_id()`（确保新页表项被后续/恢复的 GPU 访问看到）。
3. 内核应答固件：
   ```c
   qu->cs_in->heap_start = encoded_va;
   qu->cs_in->heap_end   = encoded_va;
   qu->cs_in->req        = (qu->cs_in->req & ~kCsTilerOomBit) | (cs_ack & kCsTilerOomBit);
   ```
   紧接着常规应答 `csg_in->cs_irq_ack` + 翻转 `csg_in->doorbell_req` + `mem_flush(&gShared->mem)` + `dsb sy` + `mwr(kCsfDoorbellBase, 1u)`。
   - **注意 MT6895 `be527d1` 勘误**：
     - 检测条件保持现有 `(((cs_req ^ cs_ack) | cs_req) & kCsTilerOomBit) != 0u`；
     - 扩容条件**不用 `max_chunks` 或 `target_in_flight` 硬截断**，仅设内核安全天花板（如单 heap 最多 **128 块**、总大小 `<= 256 MiB`）；
     - **关键细节**：若遇到 `REQ == ACK` 镜像且该 bit 同为 `1`（即 `cs_req & kCsTilerOomBit != 0 && cs_ack & kCsTilerOomBit != 0`），直接把 `qu->cs_in->req = (cs_req & ~kCsTilerOomBit) | (cs_ack & kCsTilerOomBit)` 写回去不会翻转该 bit！请核对：
       - 标准情况（`cs_req != cs_ack`）：固件翻转了 `cs_ack` bit 26，驱动把 `cs_in->req` bit 26 设成 `cs_ack` bit 26（`cs_req == cs_ack` 表示已应答）；
       - 镜像勘误情况（`cs_req == cs_ack == 1`）：按 `MT6895-Mainline@be527d1`，应先通过翻转或清空确认——实际 `be527d1` 里 firmware 把 `cs_ack` 设成了与 `cs_req` 相同后触发 `cs_irq_req`，应答时同样写 `cs_in->heap_start/end` 并令 `cs_in->req` 跟踪 `cs_ack` + 敲 `doorbell_req` 即可唤醒固件读取 `heap_start/end`（若需保险，只对 `cs_ack & kCsTilerOomBit` 同步并必敲 `csg_in->doorbell_req ^= qbit`）。

### 3.2 ⚠️ 严禁锁序反转（AB-BA 死锁防线）

- 全驱动铁律锁序：**`gPanthorLock` → `gMaliApiLock`**。
- `hakios_mali610_queue_poll()` 被 `waitSyncTargetsUnlocked()` 在**不持 `gPanthorLock`** 时调用，内部获取 `gMaliApiLock`。
- **绝对禁止**在持有 `gMaliApiLock` 时调用获取 `gPanthorLock` 的函数！
- **标准写法（在 `hakios.mali610.c:hakios_mali610_queue_poll()` 中）**：
  1. 检测到 `CS_TILER_OOM` 时，在 `gMaliApiLock` 内拷贝局部变量：`uint32_t oom_vm = grp->vm_id; uint64_t oom_heap_va = qu->cs_out->heap_address; uint32_t oom_cs_ack = cs_ack;`（同时记录 `vt_start/vt_end/frag_end`）；
  2. **`lck_mtx_unlock(&gMaliApiLock);`**
  3. 调用 `extern bool hakios_mali610_tiler_oom_grow(uint32_t vm_id, uint64_t heap_ctx_va, uint64_t *out_encoded_va, uint32_t *out_chunk_count);`（在 `hakios.mali610.cpp` 内实现，内部拿 `gPanthorLock` → 调 `hakios_mali610_vm_alloc_kernel_va` / `vm_map_pas`（它们拿 `gMaliApiLock`，完全符合 `gPanthorLock → gMaliApiLock`！）→ 释放 `gPanthorLock`）；
  4. **`lck_mtx_lock(&gMaliApiLock);`**
  5. 重新校验 `grp->active && qu->active && mali_uapi_hw_live_locked()`，若失效直接返回 `ENXIO`/`EINVAL`；
  6. 写入 `qu->cs_in->heap_start = encoded_va; qu->cs_in->heap_end = encoded_va;`（若 grow 失败则填 `0`），再写 `req` / `cs_irq_ack` / `doorbell_req` + flush + `mwr(kCsfDoorbellBase, 1u)`。
  - 若 `hakios_mali610_tiler_oom_grow` 成功，打印一行诊断日志：
    `printf("mali: uapi: CSG%u.Q%u CS_TILER_OOM grown chunk #%u encoded=%#llx\n", ...)`。

### 3.3 `hakios.mali610.cpp` Heap 数据结构升级

1. 将 `struct panthor_heap_chunk` 改为可动态增长的链表节点（或 `chunks` 固定上限提升为链表）：
   ```cpp
   struct panthor_heap_chunk {
       struct panthor_heap_chunk *next;
       IOBufferMemoryDescriptor *mem;
       uint64_t gpu_va;
       uint64_t alloc_size;
   };
   ```
   并在 `struct panthor_tiler_heap` 中增加：
   ```cpp
   struct panthor_tiler_heap *global_next;   /* 挂入全局 gActiveHeapList（由 gPanthorLock 保护） */
   struct panthor_heap_chunk *chunk_head;
   struct panthor_heap_chunk *chunk_tail;
   uint32_t chunk_count;
   ```
   - `tilerHeapCreate()`：尊重 `req->initial_chunk_count`（去掉 M3 临时的 `if (nchunks < 4u) nchunks = 4u;` 人为抬高，因为 Mesa 默认已是 5×2 MiB，且保留 `initial_chunk_count=1` 可用于精确触发 `CS_TILER_OOM` 测试！），逐块分配并串联 `hdr[0]`，最后把 `h` 挂入 `gActiveHeapList`。
   - `freeHeapLocked()`：从 `gActiveHeapList` 摘除 `h`，遍历 `chunk_head` 逐块 `vm_unmap` + `vm_free_kernel_va` + `OSSafeReleaseNULL(mem)` + `IODelete(node, ...)`，再释放 `ctx_gpu_va` / `ctx_mem`。
   - `extern "C" bool hakios_mali610_tiler_oom_grow(...)`：加 `gPanthorLock`，在 `gActiveHeapList` 按 `h->active && h->vm_id == vm_id && h->ctx_gpu_va == heap_ctx_va` 找到 `h`；校验 `h->chunk_count < 128u` 且 `(uint64_t)(h->chunk_count + 1u) * h->chunk_size <= (256ull << 20)`；分配并映射新 chunk，前 64B 清零 + `hakios_mali610_flush_dcache(ckva, 64u)`，挂入 `h->chunk_tail`，`h->chunk_count++`，`panthor_publish_flush_id()`，返回 `encoded_va`。

---

## 4. 模块三契约：`MiniIOSurface` 零拷贝同步与缓存一致性（`hakios.mali610.cpp`）

### 4.1 `MiniIOSurface::maliSeq` 编码（零改动 `MiniIOSurface.h`）

复用 `MiniIOSurface.h:125` 现成的 `uint64_t maliSeq`：
```cpp
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
```
- `hw_group` ∈ `[1..4]`，`queue_idx` ∈ `[0..3]`，`gpu_seq` 48-bit 单调递增。

### 4.2 活跃导入 Surface BO 快速追踪（避免每次 submit 扫 4096 槽位）

在 `HakiosMali610UserClient` 中增加轻量计数/索引：
- 在 `struct panthor_bo` 中增加 `uint32_t last_clean_seed;` 与 `bool seed_valid;`；
- 在 `HakiosMali610UserClient` 中维护 `uint32_t fSurfaceBoHandles[64]; uint32_t fSurfaceBoCount;`（仅存当前 `imported == true` 的 BO handle，通常 0–20 个）：
  - `surfaceBuffer()` 新建槽位时加入 `fSurfaceBoHandles`（若满 64 则回退扫 `fBos`，不报错），并立即对 `slot->kva` 做一次 `hakios_mali610_flush_dcache(slot->kva, (size_t)slot->size)`，记 `slot->last_clean_seed = s->shared()->seed; slot->seed_valid = true;`；
  - `freeBoLocked()` 释放 `b->imported` 时从 `fSurfaceBoHandles` 移除。

### 4.3 三处缓存维护与同步点（保证 CPU ↔ GPU 零花屏）

1. **提交前（`groupSubmit()` 持 `gPanthorLock`、调 `hakios_mali610_queue_submit()` 之前）**：
   - 遍历当前活跃导入 `MiniIOSurface` BO：对每个 `b->surface != NULL && b->kva != NULL`，执行 `hakios_mali610_flush_dcache(b->kva, (size_t)b->size)`（注：CPU 侧可能通过 `IOSurfaceLock/Unlock` 写，也可能不加锁直接写，M4b 首版对活跃导入 surface 在 submit 前直接 clean+inv PoC，确保 100% 不丢像素；后续如 M5 实测有开销再按 `seed` 精确剪枝）。
2. **提交后（`groupSubmit()` 拿到 `gpu_seq != 0` 后，仍在 `gPanthorLock` 内）**：
   - 对当前活跃导入 `MiniIOSurface` BO，写入：
     ```cpp
     uint64_t packed = panthor_pack_surface_seq(hw_group, qs.queue_index, gpu_seq);
     __atomic_store_n(&b->surface->maliSeq, packed, __ATOMIC_RELEASE);
     ```
3. **完成后（两处都做，双保险）**：
   - **路径 A：`waitSyncTargetsUnlocked()` 中 `hakios_mali610_queue_poll()` 返回 `done == true` 时**：
     - 持 `gPanthorLock` 调用 `markSyncCompletedLocked()` 的同时，遍历当前活跃导入 `MiniIOSurface` BO：若其 `maliSeq` 的 `(hw_group, queue_idx)` 与刚完成的 fence 匹配且 `seq <= fence.target_seq`，对该 BO 执行 `hakios_mali610_flush_dcache(b->kva, (size_t)b->size)` 并将 `b->surface->maliSeq` 置 `0`（用 CAS 或锁内比对避免清掉更新的 submit）。
     - **效果**：用户态 `glFinish()` 返回后，即使不经过 `MiniIOMobileFramebuffer` 或 `surf_lockwait=1`，CPU 直接读 `IOSurfaceGetBaseAddress()` 也能立刻看到 GPU 最新写回的像素！
   - **路径 B：`hakios_mali_surface_drawn(MiniIOSurface *surface, uint64_t milliseconds)`**（替换 M3 占位符 [hakios.mali610.cpp:124](file:///home/lizi/IOS/hakios-mtk/mtk/mt6895/drivers/gpu/hakios.mali610.cpp#L124)）：
     - 读 `uint64_t packed = __atomic_load_n(&surface->maliSeq, __ATOMIC_ACQUIRE);`
     - 若 `packed == 0ull`，直接返回 `true`；
     - 解包 `hw_group = (uint32_t)(packed >> 56)`、`queue_idx = (uint32_t)((packed >> 48) & 0xffu)`、`target_seq = packed & 0x0000ffffffffffffull`；
     - 在 `milliseconds` 预算内循环调用 `hakios_mali610_queue_poll(hw_group, queue_idx, target_seq, &done)`（每次 `delay(200)`）：
       - 若 `perr == EINVAL`（group 已销毁/已结束），视为 `done = true`；
       - 若 `done == true`：若 `surface->kpixels != NULL && surface->info.allocSize != 0`，调用 `hakios_mali610_flush_dcache(surface->kpixels, (size_t)round_page_64(surface->info.allocSize))`；若 `surface->maliSeq == packed` 则清 0；返回 `true`；
       - 超时返回 `false`（让 `MiniIOMobileFramebuffer` 打出明确日志）。

---

## 5. 模块四契约：`userland/src/gl_blur_test.c`（M4b 综合压测程序）

新建 `userland/src/gl_blur_test.c`，并在 `userland/build.sh` 的 `GL` 分支编出 `obj/gl_blur_test` → `root/bin/gl_blur_test`（`chmod 0755`）：
```sh
$DYN -framework CoreFoundation -framework IOKit -framework IOSurface -Wall -Wextra \
  $M/libOSMesa.8.dylib -o obj/gl_blur_test src/gl_blur_test.c
```

### 测试逻辑（单二进制支持 2 个模式，默认连续跑完）

1. **Part 1 — 零拷贝 `MiniIOSurface` 2-pass 高斯模糊 + `colorMatrix` 100 帧稳态压测**（默认 `1080 × 2400`，或可命令行传参）：
   - 用 `IOSurfaceCreate` 创建 2 个 `'BGRA'`（`kIOSurfaceBytesPerElement = 4`）`IOSurfaceRef`：`src_surf`（`1080 × 2400`）和 `dst_surf`（`1080 × 2400`），打印两者的 `sid` / `bytesPerRow` / `allocSize`（验证 `bytesPerRow % 64 == 0`）。
   - CPU 写 `src_surf`（经 `IOSurfaceLock`/`Unlock`）：清晰高对比度图案（例如左半纯红 `BGRA(0,0,255,255)`、右半纯蓝 `BGRA(255,0,0,255)`，中间加一条亮绿竖带 + 棋盘格），便于模糊效果可数学精确校验。
   - 创建 OSMesa GLES 2/3 context（与 `gl_test.c` 同参数），打印 `GL_VENDOR` / `GL_RENDERER` / `GL_VERSION`，断言 `strstr(renderer, "Mali-G610") != NULL`（严禁回退 `softpipe`）。
   - 通过 `OSMesaGetProcAddress("glEGLImageTargetTexture2DOES")` 获取零拷贝绑定入口：
     - 绑定 `src_tex` ← `(void *)(uintptr_t)IOSurfaceGetID(src_surf)`，检查 `glGetError() == 0`；
     - 绑定 `dst_tex` ← `(void *)(uintptr_t)IOSurfaceGetID(dst_surf)`，检查 `glGetError() == 0`；
     - 另建 1 个普通离屏中间纹理 `mid_tex`（`1080 × 2400`, `GL_RGBA`）及两个 FBO：`fbo_mid`（挂 `mid_tex`）和 `fbo_dst`（挂 `dst_tex`），断言两者 `GL_FRAMEBUFFER_COMPLETE`。
   - Shader：9-tap 可分离高斯模糊（Pass 1 水平 `src_tex → fbo_mid`，Pass 2 垂直 + 模拟 CoreAnimation vibrancy `colorMatrix` 偏置 `mid_tex → fbo_dst`）。
   - **预热 3 帧**（跳过 shader 编译与首帧 BO/heap 冷启动）。
   - **稳态压测 100 帧**：每帧用 `mach_absolute_time()` 测 `Pass1 + Pass2 + glFinish()` 耗时（微秒），每帧微调 uniform（例如轻微移动模糊半径或色彩权重，防止被优化掉），统计并打印：
     - `frames=100 min=... ms avg=... ms max=... ms`（同时打印是否 `avg < 8.0 ms`；注：当前 GPUEB 还在 219 MHz 启动档，M5 提频至 600-852 MHz 后会再提速 3-4 倍，因此实测数据本身也是给 M5 调频的黄金基线！）。
   - **零拷贝直接读回校验（不调用 `glReadPixels`！）**：
     - 直接 `IOSurfaceLock(dst_surf, kIOSurfaceLockReadOnly, NULL)` 读 `IOSurfaceGetBaseAddress(dst_surf)`：
       - 断言整帧不再是初始全 0；
       - 检查红蓝交界处像素确实发生平滑混合（红、蓝通道均 `> 60`）且远离交界处保持各自纯色特征；
       - 检查四角和中心像素无异常条纹/花屏；
     - `IOSurfaceUnlock(dst_surf, ...)`。
   - 通过 `IOServiceNameMatching("HakiosGPU")` + `kHakiosShow (0x7f)` 把 `dst_surf` 像素上屏显示 4 秒（注意 `MiniIOSurface` 本身已是 `'BGRA'`，直接传 `IOSurfaceGetBaseAddress(dst_surf)` + `IOSurfaceGetBytesPerRow(dst_surf)`！）。

2. **Part 2 — `CS_TILER_OOM` 动态扩容压测（子进程模式 `gl_blur_test oom`）**：
   - 默认主进程跑完 Part 1 后，用 `posix_spawn` 调自己 `gl_blur_test oom`（环境变量设置 `pan_csf_chunk_size=262144`、`pan_csf_initial_chunks=1`、`pan_csf_max_chunks=32`，使初始 tiler heap 仅有 **1 块 × 256 KiB**！）。
   - 在子进程里创建一个 `1080 × 2400` FBO，单次 `glDrawArrays(GL_TRIANGLES, 0, N * 3)` 提交 **65,536 个**覆盖全屏的三角形（跨越全部 tile bin，单帧多边形列表必超 256 KiB，强制触发至少 2 次以上 `CS_TILER_OOM` 动态扩容！），`glFinish()` 后 `glReadPixels` 校验中心像素非背景色且 `glGetError() == 0`。
   - 退出码 `0` 表示 `PASS`，父进程汇总打印：`gl_blur_test: PASS (zero_copy=1, frames=100, avg_ms=..., oom_pass=1)`。

---

## 6. Carbon 交付前自检 + DSH 机械验证清单

按新分工，Carbon 实现完后由 **DSH 执行机械检查并直接编译刷机**（仅在异常时召回 Neon）：

1. **内核双份字节一致校验**：
   ```bash
   cmp -s /home/lizi/IOS/hakios-mtk/mtk/mt6895/drivers/gpu/hakios.mali610.c \
          /home/lizi/IOS/hakios-mtk/logs/ufs-apfs-fstree-root-736/kernel-src/bsd/dev/hakios.mali610.c
   cmp -s /home/lizi/IOS/hakios-mtk/mtk/mt6895/drivers/gpu/hakios.mali610.cpp \
          /home/lizi/IOS/hakios-mtk/logs/ufs-apfs-fstree-root-736/kernel-src/iokit/Kernel/hakios.mali610.cpp
   ```
2. **语法与编译检查**：
   - 内核重编 0 error（确认 `hakios.mali610.o` 与 `hakios.mali610.cpo` 均重新编译并链入）；
   - 全新重跑 `DRIVERS="softpipe,panfrost" tools/mk-mesa.sh`（0 fuzz / 0 hunk fail，`nm -u` 0 未解析符号）；
   - `GL=test` 重打 `root.apfs`，确认 `root/bin/gl_blur_test` 与 `root/bin/gl_test` 权限 `0755`。
3. **刷 `boot-996` 上机验收判据**：

| 验收项 | 判据 |
|---|---|
| `gl_blur_test` 零拷贝 100 帧 | 打印 `Mali-G610 (Panfrost)`、`zero_copy=1`、100/100 帧成功、直接读 `IOSurfaceGetBaseAddress` 校验通过、上屏成功 |
| `CS_TILER_OOM` 动态扩容 | `gl_blur_test oom` PASS，且 `/dev/hlog` 中**真实出现** `CS_TILER_OOM grown chunk #` 日志 |
| 稳态帧时间采集 | 记录 `gl_blur_test` 100 帧 `min/avg/max ms` 及 `gl_test fill 1080 2400` 10 项微秒数据（给 M5 调频作对照基线） |
| 硬件零异常 | 0 次 `GPU_FAULT` / `MMU_FAULT` / `CS_FATAL` / `syncobj wait timeout` |
| 退回闭环 | **`RETURN < 40 s`** 干净回 Android |
