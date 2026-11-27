# M4a 实现契约：Mesa 25.0.7 `panfrost`（Panthor CSF）跨编译 + `gl_test` 实机出图

> **起草**：Neon（架构 / 契约 / Review）  
> **执行**：Carbon（实现 + 主机跨编译验证）  
> **验收**：DSH（打包 `root.apfs` + 刷机实测 `gl_test` + 采集日志/拍照 + `RETURN`）  
> **前置基线**：`boot-994` M3 **一次 PASS**（`panthor_test` 10/10 轮 `compute=16/16`、`LATEST_FLUSH_ID` 只读页正常、0 fault、`RETURN 30s`）。  
> **内核原则**：**M4a 原则上零内核改动**（现有 `boot-994` 内核 `hakios.mali610.cpp` 已具备 Mesa `panthor_kmod` 所需的全部 13 个 Panthor ioctl + 全套 syncobj + `LATEST_FLUSH_ID` 映射 + `kHakiosSurface`/`kHakiosShow`）。

---

## 0. 目标与边界

### M4a 唯一目标
1. 扩展 `tools/mesa/hakios-drm/` 支持 `panthor`（同时保持 `msm` / `freedreno` 向后兼容）；
2. 新增 `tools/mesa/panfrost-darwin.patch`，在现有 `tools/mesa/*.patch` 之后干净应用；
3. 用 `DRIVERS="softpipe,panfrost" tools/mk-mesa.sh` 交叉编译出 `arm64-apple-ios` 的 `xnu-deps/build/mesa/out/libOSMesa.8.dylib`；
4. 修复 `userland/src/gl_test.c:352` 的服务匹配 bug（`IOServiceMatching` → `IOServiceNameMatching`），以 `GL=test` 模式打包进 `root.apfs`（**不切 `backboardd` / `SpringBoard` 到 GPU**）；
5. 实机运行 `/redwood/bin/gl_test`：
   - `GL_RENDERER` 必须为 `Mali-G610 (Panfrost)`（严禁回退 `softpipe`）；
   - `glReadPixels` 校验三角形像素与覆盖率（~26%）、`error 0x0`；
   - 手机屏幕通过 `kHakiosShow` 清晰显示红绿蓝渐变三角形；
   - 内核 0 次 `GPU_FAULT` / `MMU_FAULT` / `CS_FATAL`；
   - `RETURN < 40s`。

### 明确不做（留给 M4b / M5）
- ❌ 不改 `inject.c` / `inject_sb.c`，不在 M4a 启用 `GL=1` 切 `backboardd`；
- ❌ 不改内核 `hakios.mali610.c` / `hakios.mali610.cpp`（除非实机暴露真实 ABI 缺口，须先报 Neon 确认）；
- ❌ 不做 `CS_TILER_OOM` 动态扩容与 `MiniIOSurfaceDrawn()` 同步（属 M4b；`gl_test` 单三角形远小于初始 5×2 MiB = 10 MiB tiler heap）。

---

## 1. 需修改/新增文件清单

| # | 文件路径（相对 `/home/lizi/IOS/hakios-mtk`） | 动作 | 说明 |
|---|---|---|---|
| 1 | `tools/mesa/hakios-drm/xf86drm.h` | 修改 | 补齐 Mesa `panfrost`/`panthor_kmod` 依赖的 libdrm 声明与函数原型 |
| 2 | `tools/mesa/hakios-drm/hakios.drm.c` | 修改 | 支持 `panthor` 打开、版本上报、全套 `drmSyncobj*`、`drmCloseBufferHandle`、时钟换算与 `IOServiceNameMatching` |
| 3 | `tools/mesa/panfrost-darwin.patch` | **新增** | Mesa 25.0.7 `panfrost`/`panthor` Darwin 适配补丁（按字母序应用在 `freedreno-darwin.patch` 之后） |
| 4 | `userland/src/gl_test.c` | 修改 | 第 352 行 `IOServiceMatching("HakiosGPU")` → `IOServiceNameMatching("HakiosGPU")` |
| 5 | `tools/mk-mesa.sh` | 核查（按需微调） | 确认 `DRIVERS="softpipe,panfrost"` 时 `-Dfreedreno-kmds=msm` 不报错；核查 `built-from` 缓存逻辑 |

---

## 2. 模块一契约：`tools/mesa/hakios-drm/`

### 2.1 内核 ABI 权威对照（以当前 `hakios.mali610.cpp` 为准，严禁使用 `PLAN.md` 早期草案的 `0x70..0x75`！）

内核 `hakios.mali610.cpp:131-168` 已将全部 selector 与 Linux `drm.h` / `panthor_drm.h` ioctl 低 8 位做 **1:1 映射**：

| Selector | 对应 Linux ioctl | 说明 |
|---|---|---|
| `0x09` | `DRM_IOCTL_GEM_CLOSE` | `struct drm_gem_close` |
| `0x40..0x4c` | `DRM_IOCTL_PANTHOR_*` | `DEV_QUERY`, `VM_CREATE`, `VM_DESTROY`, `VM_BIND`, `VM_GET_STATE`, `BO_CREATE`, `BO_MMAP_OFFSET`, `GROUP_CREATE`, `GROUP_DESTROY`, `GROUP_SUBMIT`, `GROUP_GET_STATE`, `TILER_HEAP_CREATE`, `TILER_HEAP_DESTROY` |
| `0x4d..0x50` | `BO_SET_LABEL`, `SET_USER_MMIO_OFFSET`, `BO_SYNC`, `BO_QUERY_INFO` | 均已实现 |
| `0x7e` / `0x7f` | `kHakiosSurface` / `kHakiosShow` | 零拷贝 IOSurface / 直接上屏 |
| `0xbf` | `DRM_IOCTL_SYNCOBJ_CREATE` | `struct drm_syncobj_create` |
| `0xc0` | `DRM_IOCTL_SYNCOBJ_DESTROY` | `struct drm_syncobj_destroy` |
| `0xc1` | `DRM_IOCTL_SYNCOBJ_HANDLE_TO_FD` | `struct drm_syncobj_handle`（`EXPORT_SYNC` 返回 `fd = 0x4000 + slot`） |
| `0xc2` | `DRM_IOCTL_SYNCOBJ_FD_TO_HANDLE` | `struct drm_syncobj_handle`（消费并释放 `0x4000 + slot`） |
| `0xc3` | `DRM_IOCTL_SYNCOBJ_WAIT` | `struct drm_syncobj_wait` |
| `0xc4` | `DRM_IOCTL_SYNCOBJ_RESET` | `struct drm_syncobj_array` |
| `0xc5` | `DRM_IOCTL_SYNCOBJ_SIGNAL` | `struct drm_syncobj_array` |
| `0xca` | `DRM_IOCTL_SYNCOBJ_TIMELINE_WAIT` | `struct drm_syncobj_timeline_wait` |
| `0xcb` | `DRM_IOCTL_SYNCOBJ_QUERY` | `struct drm_syncobj_timeline_array` |
| `0xcc` | `DRM_IOCTL_SYNCOBJ_TRANSFER` | `struct drm_syncobj_transfer` |
| `0xcd` | `DRM_IOCTL_SYNCOBJ_TIMELINE_SIGNAL` | `struct drm_syncobj_timeline_array` |

特殊 `clientMemoryForType` 类型：
- `kHakiosFlushIdMemoryType = 0x7fff0000u` → 映射 16 KiB 只读 `LATEST_FLUSH_ID` 页（`IOConnectMapMemory64` / `IOConnectUnmapMemory64`）。
- 普通 BO → `type = handle`（或 `mmap_offset`，内核两边都认）。

### 2.2 `xf86drm.h` 必须新增/补齐的声明

经全量扫描 Mesa 25.0.7 `src/panfrost/lib/`、`src/panfrost/perf/`、`src/gallium/drivers/panfrost/`、`src/gallium/winsys/panfrost/`，实际调用的 libdrm 符号共 16 个，全部必须声明并实现：

```c
/* 已有保留 */
int drmOpenWithType(const char *name, const char *busid, int type);
int drmIoctl(int fd, unsigned long request, void *arg);
int drmCommandWrite(int fd, unsigned long index, const void *data, unsigned long size);
int drmCommandWriteRead(int fd, unsigned long index, void *data, unsigned long size);
drmVersionPtr drmGetVersion(int fd);
void drmFreeVersion(drmVersionPtr version);
int drmGetCap(int fd, uint64_t capability, uint64_t *value);
int drmPrimeHandleToFD(int fd, uint32_t handle, uint32_t flags, int *prime_fd);
int drmPrimeFDToHandle(int fd, int surface, uint32_t *handle);
int drmSyncobjCreate(int fd, uint32_t flags, uint32_t *handle);
int drmSyncobjDestroy(int fd, uint32_t handle);
int drmSyncobjReset(int fd, const uint32_t *handles, uint32_t count);
int drmSyncobjFDToHandle(int fd, int obj_fd, uint32_t *handle);
int drmSyncobjHandleToFD(int fd, uint32_t handle, int *obj_fd);
int drmSyncobjImportSyncFile(int fd, uint32_t handle, int sync_file_fd);
int drmSyncobjExportSyncFile(int fd, uint32_t handle, int *sync_file_fd);

/* 必须新增（panthor_kmod.c / panfrost_kmod.c / pan_csf.c / pan_fence.c 直接调用） */
int drmCloseBufferHandle(int fd, uint32_t handle);
int drmSyncobjWait(int fd, uint32_t *handles, unsigned num_handles,
                   int64_t timeout_nsec, unsigned flags,
                   uint32_t *first_signaled);
int drmSyncobjTimelineWait(int fd, uint32_t *handles, uint64_t *points,
                           unsigned num_handles, int64_t timeout_nsec,
                           unsigned flags, uint32_t *first_signaled);
int drmSyncobjTransfer(int fd, uint32_t dst_handle, uint64_t dst_point,
                       uint32_t src_handle, uint64_t src_point,
                       uint32_t flags);
int drmSyncobjSignal(int fd, const uint32_t *handles, uint32_t handle_count);
int drmSyncobjTimelineSignal(int fd, const uint32_t *handles,
                             uint64_t *points, uint32_t handle_count);
int drmSyncobjQuery(int fd, uint32_t *handles, uint64_t *points,
                    uint32_t handle_count);
int drmSyncobjQuery2(int fd, uint32_t *handles, uint64_t *points,
                     uint32_t handle_count, uint32_t flags);
```

同时导出常量宏（供 `panthor_kmod.c` 使用）：
```c
#define HAKIOS_DRM_FLUSH_ID_HANDLE  0x7fff0000u
```

### 2.3 `hakios.drm.c` 实现要点（5 条硬要求，每条都是踩坑防爆点）

1. **服务匹配改为 `IOServiceNameMatching`**：
   - `drmOpenWithType()` 接受 `"panthor"` 与 `"msm"`（记住静态 `static const char *opened_driver;`），打开服务时用 **`IOServiceNameMatching(HAKIOS_GPU_SERVICE)`**（兼容 `HakiosMali610` 与旧 `HakiosGPU`，两者都调用了 `setName("HakiosGPU")`）。
   - 如果 `name` 是 `"panthor"`，可选在打开后发一次轻量 `DEV_QUERY(GPU_INFO, size=0)` 校验当前内核确实是 Panthor 驱动而非旧 Adreno 驱动；失败则关闭连接并返回 `-1`，保证 `osmesa_create_gpu_screen()` 在不匹配内核上安全回退。
2. **`drmGetVersion()` 按打开的驱动返回**：
   - 当 `opened_driver` 为 `"panthor"` 时：
     - `version_major = 1`, `version_minor = 3`, `version_patchlevel = 0`
     - `name = strdup("panthor")`, `name_len = 7`
     - `desc = strdup("Mali-G610 Panthor on Darwin")`
   - 当为 `"msm"` 时保持原值 `1.7 / "msm"` 不变。
   - **依据**：`pan_kmod.c:63` 通过 `strcmp("panthor", version->name)` 选中 `panthor_kmod_ops`；`panthor_kmod.c:138,160` 在 `>= 1.2` 时会依次查询 `TIMESTAMP_INFO` 与 `GROUP_PRIORITIES_INFO`（内核均已实现并通过 M3 实测）。
3. **`drmCloseBufferHandle()`**：
   - 构造 `struct drm_gem_close args = { .handle = handle, .pad = 0 };` 调用 `drmIoctl(fd, DRM_IOCTL_GEM_CLOSE, &args)`。
4. **全套 `drmSyncobj*` 返回值语义与超时错误码（⚠️ 极其关键！）**：
   - **上游 `libdrm` 契约**：`drmSyncobj*` 系列函数底层走 `drmIoctl`，`panthor_kmod.c:535-542` 和 `pan_fence.c:66-69` 同时存在两种检查方式：
     - 多数调用点检查 `if (ret)` 并读 `errno`；
     - `panthor_kmod.c:538-541` 检查 `if (ret >= 0) return true; if (ret != -ETIME) mesa_loge(...);`（直接把返回值当 `-errno` 判 `-ETIME`！）。
   - **iOS 16.5 SDK 中 `ETIMEDOUT == 60`，`ETIME == 101`，两者不同！**
   - **硬性规定**：
     - `call()` 返回正 `errno`；对于 `kIOReturnNoResources` 映射 `ENOSPC`/`EBUSY`、`kIOReturnNotReady` 映射 `ENXIO`；
     - 在 `drmSyncobjWait` / `drmSyncobjTimelineWait` 中，若内核返回 `kIOReturnTimeout`（`ETIMEDOUT`），**统一转成 `ETIME`**：设 `errno = ETIME` 并 `return -ETIME`！
     - 所有 `drmSyncobj*` 失败时统一 `errno = err; return -err;`（成功返回 `0`）。这样无论 Mesa 查 `ret < 0`、`ret != 0`、`ret == -ETIME` 还是查 `errno`，全部 100% 兼容！
   - **结构体构造必须与 Linux `drm.h` 逐字段对齐**（特别是 `drmSyncobjTransfer` 参数顺序：`dst_handle, dst_point, src_handle, src_point, flags` → `struct drm_syncobj_transfer { .src_handle = src_handle, .dst_handle = dst_handle, .src_point = src_point, .dst_point = dst_point, .flags = flags }`，**千万别把 `src`/`dst` 写反！**）。
   - `drmSyncobjExportSyncFile` → `DRM_IOCTL_SYNCOBJ_HANDLE_TO_FD`（`flags = DRM_SYNCOBJ_HANDLE_TO_FD_FLAGS_EXPORT_SYNC_FILE`）；`drmSyncobjImportSyncFile` → `DRM_IOCTL_SYNCOBJ_FD_TO_HANDLE`（`flags = DRM_SYNCOBJ_FD_TO_HANDLE_FLAGS_IMPORT_SYNC_FILE`）。
5. **绝对超时时钟基转换（⚠️ 极其关键！避免有限超时立刻误判 `ETIME`）**：
   - Mesa `panthor_kmod_bo_wait()`（`panthor_kmod.c:532`）和 `panfrost_fence_finish()`（`pan_fence.c:62`）都用 `os_time_get_nano()`（Darwin 上走 `clock_gettime(CLOCK_MONOTONIC)`）构造 `abs_timeout_nsec`；
   - 而内核 `hakios.mali610.cpp:550` 的 `panthor_now_ns()` 用的是 **`mach_absolute_time()`**！
   - Darwin 上 `CLOCK_MONOTONIC` 与 `mach_absolute_time()` 起点不同（`freedreno-darwin.patch:93-98` 踩过同款大坑）。
   - **统一在 `hakios.drm.c` 中写一个 `convert_monotonic_timeout(int64_t timeout_nsec)`**：
     - `timeout_nsec <= 0` → 原样返回 `0`（非阻塞 poll）；
     - `timeout_nsec >= INT64_MAX / 2`（例如 `INT64_MAX`）→ 返回 `INT64_MAX`（内核直接采用 5s 上限）；
     - 其余有限绝对时间 → 用 `clock_gettime(CLOCK_MONOTONIC, &ts)` 算 `now_mono_ns`，得 `rel = timeout_nsec - now_mono_ns`；若 `rel <= 0` 返回 `0`，否则用 `mach_absolute_time() * tb.numer / tb.denom + rel` 换算到内核时钟域！
   - `drmSyncobjWait` 和 `drmSyncobjTimelineWait` 都经过此转换再发内核。

---

## 3. 模块二契约：`tools/mesa/panfrost-darwin.patch`

> **补丁顺序硬要求**：`tools/mk-mesa.sh:61` 按 `*.patch` 字母序依次打补丁：  
> `air-frontend.patch` → `air-ir3.patch` → `freedreno-darwin.patch` → `ir3-round-default.patch` → `osmesa-es-profile.patch` → **`panfrost-darwin.patch`**。  
> 因此 `panfrost-darwin.patch` **必须基于已应用前 5 个补丁的 Mesa 25.0.7 树生成**，且不能破坏 `DRIVERS="softpipe,freedreno"` 的编译。

必须覆盖以下 7 处精确修改：

### 3.1 `meson.build`（在 `freedreno-darwin.patch` 的 `dep_libdrm` 块上扩展）
- 将：
  ```meson
  if host_machine.system() == 'darwin' and with_gallium_freedreno
  ```
  改为：
  ```meson
  if host_machine.system() == 'darwin' and (with_gallium_freedreno or with_gallium_panfrost)
  ```
- 检查 `meson.build` 中是否存在其他仅对 `system_has_kms_drm` 开放但 `with_gallium_panfrost` 需要的块（核查 `with_gallium_kmsro`：应保持 `false`，因为我们直接用 `panfrost_drm_screen_create` / `panfrost_create_screen`）。

### 3.2 `src/panfrost/lib/kmod/meson.build`
- 当 `host_machine.system() == 'darwin' and not with_gallium_freedreno` 时，将 `files('../../../hakios-drm/hakios.drm.c')` 加入 `libpankmod_lib_files`（避免与 `freedreno` 同时启用时重复链接 `hakios.drm.c` 符号）。

### 3.3 `src/panfrost/lib/kmod/pan_kmod.h`（BO CPU 映射）
- 在 `pan_kmod_bo_mmap()`（第 614–627 行）中加 `#ifdef __APPLE__`：
  - 通过 `hakios_drm_map(bo->dev->fd, bo->handle, bo->size)` 映射（注意：若 `bo_offset != 0`，校验 `bo_offset == 0` 或返回 `(uint8_t *)host_addr + bo_offset`，映射失败返回 `MAP_FAILED`）。
  - **注意**：`pan_bo.c:344` 用 `if (bo->ptr.cpu == MAP_FAILED)` 判错！不要把 `MAP_FAILED` 转成 `NULL`（上游第 623 行有个 return NULL 的小怪癖，Darwin 分支直接返回 `hakios_drm_map(...)` 即可，失败即 `MAP_FAILED`）。

### 3.4 `src/panfrost/lib/kmod/panthor_kmod.c`（`LATEST_FLUSH_ID` 只读页映射/解映射）
- 第 153 行 `panthor_kmod_dev_create()`：
  ```c
  #ifdef __APPLE__
     panthor_dev->flush_id = hakios_drm_map(fd, HAKIOS_DRM_FLUSH_ID_HANDLE,
                                            getpagesize());
  #else
     panthor_dev->flush_id = os_mmap(0, getpagesize(), PROT_READ, MAP_SHARED, fd,
                                     DRM_PANTHOR_USER_FLUSH_ID_MMIO_OFFSET);
  #endif
  ```
- 第 197 行 `panthor_kmod_dev_destroy()`：
  ```c
  #ifdef __APPLE__
     hakios_drm_unmap(dev->fd, HAKIOS_DRM_FLUSH_ID_HANDLE, panthor_dev->flush_id);
  #else
     os_munmap(panthor_dev->flush_id, getpagesize());
  #endif
  ```

### 3.5 `src/panfrost/lib/kmod/pan_kmod.c`（为 M4b 预留 IOSurface 导入 size 获取）
- 第 179 行 `pan_kmod_bo_import()`：
  ```c
  #ifdef __APPLE__
     struct hakios_drm_surface surface = { .surface = (uint32_t)fd };
     size_t size = hakios_drm_surface(dev->fd, &surface) ? 0 : surface.size;
  #else
     size_t size = lseek(fd, 0, SEEK_END);
  #endif
  ```

### 3.6 `src/gallium/drivers/panfrost/pan_bo.c`（BO CPU 解映射）
- 第 358 行 `panfrost_bo_munmap()`：
  ```c
  #ifdef __APPLE__
     hakios_drm_unmap(bo->dev->kmod.dev->fd, bo->kmod_bo->handle,
                      (void *)(uintptr_t)bo->ptr.cpu);
  #else
     if (os_munmap((void *)(uintptr_t)bo->ptr.cpu, panfrost_bo_size(bo))) {
        mesa_loge("munmap failed: %s", strerror(errno));
        abort();
     }
  #endif
  ```
- 确认 `pan_disk_cache.c`：因 `mk-mesa.sh` 已传 `-Dshader-cache=disabled`，`ENABLE_SHADER_CACHE` 未定义，但仍需检查 `#include "util/build_id.h"` 是否能在 Darwin 正常编译（如有问题加 `#ifndef __APPLE__` 保护）。

### 3.7 `src/gallium/targets/osmesa/meson.build` + `osmesa_target.c`
- `src/gallium/targets/osmesa/meson.build`：在 `dependencies` 中加入 `driver_panfrost`（与 `driver_freedreno` 并列，未启用时是空 `declare_dependency()`）。
- `src/gallium/targets/osmesa/osmesa_target.c`：
  - 新增 `#ifdef GALLIUM_PANFROST` 路径（与 `#ifdef GALLIUM_FREEDRENO` 共存，互不冲突）：
    - `#include "panfrost/drm/panfrost_drm_public.h"`
    - `panfrost_options[]` 引入 `#include "panfrost/driinfo_panfrost.h"`
    - `osmesa_create_panfrost_screen()`：
      1. 检查 `debug_get_bool_option("OSMESA_SOFTWARE", false)`；
      2. `fd = drmOpenWithType("panthor", NULL, DRM_NODE_RENDER);`
      3. `driParseOptionInfo(&info, panfrost_options, ARRAY_SIZE(panfrost_options));`
      4. `driParseConfigFiles(&cache, &info, 0, "panfrost", NULL, NULL, NULL, 0, NULL, 0);`
      5. 调用 `panfrost_drm_screen_create(fd, &config)`（注意：`panfrost_drm_screen_create` 内部会 `os_dupfd_cloexec(fd)` + `u_pipe_screen_lookup_or_create`，成功后记住 `gpu_fd = fd` 并返回 `debug_screen_wrap(screen)`；若失败需 `close(fd)`）。
    - 在 `osmesa_create_screen()` 中：先尝试 `GALLIUM_FREEDRENO`（若编译了），再尝试 `GALLIUM_PANFROST`（若编译了），都返回 `NULL` 时回退 `softpipe`。
    - 在 `osmesa_surface_resource()` 中：将 `#ifdef GALLIUM_FREEDRENO` 条件扩为 `#if defined(GALLIUM_FREEDRENO) || defined(GALLIUM_PANFROST)`，同时确保 `handle.modifier = DRM_FORMAT_MOD_LINEAR`（`0`），为 M4b 铺平道路。

---

## 4. 模块三契约：`userland/src/gl_test.c` 与 `tools/mk-mesa.sh`

1. **`userland/src/gl_test.c:352`**：
   - 将：
     ```c
     service = IOServiceGetMatchingService(0, IOServiceMatching("HakiosGPU"));
     ```
     改为：
     ```c
     service = IOServiceGetMatchingService(0, IOServiceNameMatching("HakiosGPU"));
     ```
   - 原因：与 `panthor_test.c` 完全一致，`HakiosMali610` 的 IOKit 类名是 `HakiosMali610`、节点名是 `"HakiosGPU"`；不改则无法调用 `kHakiosShow` 上屏。
2. **`tools/mk-mesa.sh`**：
   - 核实 `DRIVERS="softpipe,panfrost" tools/mk-mesa.sh` 能完整走通 `meson setup` + `ninja` + `llvm-strip` + `install_name_tool` + `nm`。
   - 核实若 `DRIVERS` 不含 `freedreno` 时，`-Dfreedreno-kmds=msm` 是否触发 meson 警告/报错；若报错，仅在 `$DRIVERS` 含 `freedreno` 时传递 `-Dfreedreno-kmds=msm`。
   - **顺带注意**：`mk-mesa.sh:123` 生成 `out/built-from` 时未把 `$DRIVERS` 纳入校验；建议把 `printf '%s\n' "$DRIVERS"` 或保持与 `userland/build.sh:303` 一致（若改 `built-from` 格式必须同步改 `userland/build.sh:303`，否则不要改，由 DSH 先跑 `DRIVERS="softpipe,panfrost" tools/mk-mesa.sh` 再跑 `GL=test` 即可）。

---

## 5. Carbon 交付前自检清单（全绿才交 Neon Review）

Carbon 完成代码后，必须在本机实际跑完以下 5 项检查并把真实输出贴回：

1. **全新解包 + 全量补丁应用干净**（0 fuzz / 0 hunk fail）：
   ```bash
   rm -rf /home/lizi/IOS/hakios-mtk/xnu-deps/build/mesa/mesa-25.0.7 \
          /home/lizi/IOS/hakios-mtk/xnu-deps/build/mesa/build-softpipe-panfrost
   DRIVERS="softpipe,panfrost" /home/lizi/IOS/hakios-mtk/tools/mk-mesa.sh
   ```
2. **产出物核验**：
   - `xnu-deps/build/mesa/out/libOSMesa.8.dylib` 存在，为 `arm64` Mach-O dylib，install name 为 `/redwood/lib/libOSMesa.8.dylib`；
   - `nm -gU` / `nm -u` 检查：**零未解析的 `drm*` / `hakios_drm_*` / Linux-only 符号**；
   - `strings out/libOSMesa.8.dylib | grep "Mali-G610"` 命中（确认 Valhall v10 / `panfrost` / `panthor_kmod` 确实编进来了）。
3. **`gl_test` 编译验证**：
   ```bash
   source /home/lizi/IOS/hakios-mtk/xnu-env.sh
   xnu-clang -arch arm64 -isysroot "$THEOS/sdks/iPhoneOS16.5.sdk" -O1 \
     -framework CoreFoundation -framework IOKit \
     /home/lizi/IOS/hakios-mtk/xnu-deps/build/mesa/out/libOSMesa.8.dylib \
     -o /home/lizi/IOS/hakios-mtk/userland/obj/gl_test \
     /home/lizi/IOS/hakios-mtk/userland/src/gl_test.c
   ```
   确认 0 error / 0 warning。
4. **回归检查**：确认未修改内核源码（`hakios.mali610.{c,cpp}` 未动），且 `tools/mesa/freedreno-darwin.patch` 原文件未被破坏。
5. **交 Neon Review**：Neon 逐行审查 `hakios.drm.c`、`xf86drm.h`、`panfrost-darwin.patch`、`gl_test.c` diff 与 `nm -u` 输出后，放行给 DSH 打包 `GL=test` 刷机实测。

---

## 6. DSH 实机验收判据（M4a PASS 门槛）

打包命令提醒：先确认 `xnu-deps/build/mesa/out/libOSMesa.8.dylib` 为 `softpipe,panfrost` 构建产物，再以 `GL=test`（配合 `RC_JOBS=rc.jobs.gltest` 或手动运行 `/redwood/bin/gl_test 8`）打包 `root.apfs`，内核继续使用 `boot-994`（`mali_gpu=1`）。

| 验收项 | 判据 |
|---|---|
| GL 驱动识别 | `gl_test` 打印 `GL_RENDERER` 包含 **`Mali-G610 (Panfrost)`**（绝不是 `softpipe`） |
| 渲染结果正确 | `error 0x0`；四角背景为深蓝（`~26 26 51 255`）；中心及三顶点附近呈红/绿/蓝渐变；三角形覆盖率 **25%–27%**（几何理论 26%） |
| 屏幕上屏 | `gl_test: the picture is on the panel (0x0), for 8 seconds`，肉眼/拍照可见清晰彩色三角形 |
| 硬件零异常 | 内核日志 0 次 `GPU_FAULT` / `MMU_FAULT` / `CS_FATAL` / `syncobj wait timeout` |
| 退回闭环 | **`RETURN < 40 s`** 干净回 Android |

### 实机排障速查（如首次未一次过，DSH 直接采这些给 Neon）
- 若回退成 `softpipe`：开 `PAN_MESA_DEBUG=trace` 或查 `stderr` 中的 `mesa_loge`（最常见原因：`drmGetVersion` 名字/版本不匹配、`LATEST_FLUSH_ID` map 失败、`DEV_QUERY` 某项字段校验失败）。
- 若 `gl_test` 卡住 5 秒报 timeout：抓 `/dev/hlog` 中 `hakios_mali610_report_fault` 的 `cs_out` / `as1 faultstatus` / `irq` 现场（首要排查：`CS_TILER_OOM` 是否被单三角形触发、或者某条 `VM_BIND` 权限/缓存标志不匹配）。
- 若 `glReadPixels` 读回全 `0x55` 或全黑但无 fault：检查 `pan_bo.c` CPU 映射是否命中写合并/缓存一致性，或用 `PAN_MESA_DEBUG=sync` 强制逐 job 同步验证。
