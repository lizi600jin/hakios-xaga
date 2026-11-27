# Mali-G610 MC6 (MT6895 / xaga) GPU 攻坚总规划

> **状态**：规划定稿（基于 `boot-990` 实测基线与 HakiOS 现有源码核验）  
> **分工**：**Neon**（架构规划 / 审查 / 符号与日志分析） · **Astra**（`term_c52f26f4`, GPT-6.2-Astra xhigh，后端/驱动/Mesa 核心实现） · **DSH**（编译 / 打包 / 刷机 / 硬件实测 / 上游参考抓取）  
> **兜底基线**：`golden/boot-985b-golden.img`（及已验证 `RETURN 28s` 的 `boot-990`）  
> **目标**：打通 Mali-G610 MC6 硬件渲染管线，让 `backboardd` / `SpringBoard` 切离 CPU 软件光栅化，**一举解决 Dock 毛玻璃 vibrancy 恢复、锁屏时钟/壁纸渲染与 18 fps → 60 fps 流畅度**，同时保持 `RETURN < 40 s` 闭环。

---

## 0. 核心架构决策（先纠偏，省去数月弯路）

在进入实现前，必须明确两条经代码库核实的关键架构结论：

### 决策 A：走 **OpenGL ES 3.0（Mesa 25.0.7 `panfrost` + 现有 `eagl.c`）**，不从零写 Metal / IOAccelerator

HakiOS 仓库里**已经完整存在**面向 CoreAnimation 的 GPU OpenGL ES 3.0 适配层（原为 Qualcomm Adreno + Mesa `freedreno` 与 MT6768 Mali-G52 + Mesa `panfrost` 设计）：

```
SpringBoard / backboardd (CoreAnimation: CA::WindowServer::EAGLServer::renderer)
  └── EAGLContext (OpenGL ES 3.0)
       └── /redwood/lib/libhakios.gl.dylib   ← 已存在！(userland/src/eagl.c + gl_osmesa.c)
            └── /redwood/lib/libOSMesa.8.dylib  ← 已存在跨编译脚本！(tools/mk-mesa.sh, Mesa 25.0.7)
                 ├── Gallium 驱动: panfrost (Valhall v10 / Mali-G610 0xa867 + panthor CSF KMD — Mesa 25.0.7 原生内置！)
                 └── tools/mesa/hakios-drm/hakios.drm.c (将 Linux panthor DRM ioctl 转为 IOKit 调用)
                      └── 内核 IOKit 服务: HakiosGPU (iokit/Kernel/hakios.mali610.cpp — 参照 hakios.mali.cpp 编写)
                           ├── Panthor DRM UAPI 子集 (~13 个 ioctl + syncobj)
                           ├── HakiOS 扩展: kHakiosSurface (零拷贝 MiniIOSurface) + kHakiosShow
                           └── bsd/dev/hakios.mali610.c (M0..M2b 已通 → 补齐 AS1 MMU / CSG 调度 / Tiler Heap)
                                └── Mali-G610 MC6 硬件 (0xa8670004, CSF 0x40a0412, 6 Shader Cores, 1 Tiler, 1 L2)
```

**为什么这是唯一最优路径：**
1. **Mesa 25.0.7 原生内置完整 Valhall v10 (`0xa867` Mali-G610) 编译器 + CSF 命令流生成器 + `panthor` KMD 后端**（`src/panfrost/lib/kmod/panthor_kmod.c`），Astra **不需要**手写 GLSL/SPIR-V 编译器、shader 指令编码器或全套描述符生成器。
2. **`userland/src/eagl.c`（4,205 行）已处理好 CoreAnimation 全部 OpenGL ES 3.0 怪癖**（BGRA 纹理格式、像素坐标采样改写、无 shader 的 plain draw、`EXT_shader_framebuffer_fetch`、可变半径模糊 `vblur`、IOSurface 零拷贝 `glEGLImageTargetTexture2DOES`）。
3. **自动解锁 Dock / 锁屏 vibrancy**：`userland/src/inject.c:1087`（`opengl_es()`）与 `userland/src/inject_sb.c:658`（`gpu_draws()`）均通过检测 `/redwood/lib/libhakios.gl.dylib` + IOKit 服务 `HakiosGPU` 自动切换：
   - `backboardd` 自动从 CPU 软光栅切到 OpenGL ES 3.0；
   - `SpringBoard` 自动跳过 `plain_vibrancy()`（不再设置 `BSUIVibrancyEffectViewDisabled`），Dock 毛玻璃与锁屏时钟 vibrancy **自动恢复**；
   - `gl=0` boot-arg 保留为随时一键回退软渲染的保险丝，无需重编。
4. **Metal / AIR（`tools/mesa/air`）延后为 Phase 2（M6）**：CoreAnimation 首选 OpenGL ES 3.0 即可跑满 60 fps + 全特效，Metal 留到 OpenGL ES 3.0 稳态后再接（届时复用同一套内核 `HakiosGPU` panthor 驱动 + Mesa Valhall 编译器）。

### 决策 B：不做「仅在 `MiniIOSurface` 里做一次 GPU blit/blur」的捷径

- CoreAnimation 的 Dock 毛玻璃不是靠 `MiniIOSurface` 接口做的，而是 CoreAnimation 自己在渲染器里通过 `CABackdropLayer` + `vibrantColorMatrix` + 多级模糊画出来的；没有真 GLES 渲染器，`inject_sb.c` 依然处于无 GPU 分支，且无法解决 18 fps 合成瓶颈。
- 直接按 `M2c → M3 → M4a → M4b → M5` 接通 `panthor` + Mesa `panfrost`，工作量反而比手写一套只服务 CoreAnimation 的定制模糊/合成器更小、更干净、且一步到位 60 fps。

---

## 1. 现状基线与硬件/内核铁律（Astra 必读）

### 1.1 实测硬件参数（`boot-989` / `boot-990` 验证）

| 项 | 实测值 | 含义 |
|---|---|---|
| `GPU_ID` | `0xa8670004` | Valhall v10, Mali-G610 r0p0 (`ARCH 10.8`, `PROD 6`, `REV 7`) |
| `CSF_ID` | `0x040a0412` | CSF 固件架构已跑通 M1/M2a/M2b |
| `MMU_FEATURES` | `0x2830` | **48-bit VA / 40-bit PA** |
| `AS_PRESENT` | `0xff` | 8 个地址空间 `AS0..AS7`（**`AS0` 专属 MCU 固件，用户 VM 用 `AS1..AS7`**） |
| `SHADER_PRESENT` | `0x130013` | 6 个 Shader Core（2 stack × 3 cores，MC6） |
| `TILER_PRESENT` | `0x1` | 1 个 Tiler |
| `L2_PRESENT` | `0x1` | 1 个 L2 Cache (`l2_config = 0x7130000`) |
| `THREAD_MAX_THREADS` | `0x800` | 2048 线程 |
| `MALI_MEMATTR` | `0x00084c8d888d8f88` | MediaTek vendor 表：idx 0 = cached (`0x88`), idx 5 = NC (`0x4c`), idx 6 = shared (`0x08`) |
| 当前启动状态 | `M0 ✅ M1 ✅ M2a ✅ M2b ✅` | `group 0 stream 0 running`，`m2b: PASS`，`RETURN 28s` 稳定 |

### 1.2 仓库现有关键资产（严禁重复造轮子）

| 文件 | 行数/状态 | 作用 |
|---|---|---|
| `logs/ufs-apfs-fstree-root-736/kernel-src/bsd/dev/hakios.mali610.c` | **7,159 行（权威最新！）** | MT6895 GPUEB 上电、时钟/总线保护、4 级 4 KiB 页表 `pt_map()`、CSF 固件加载、M1/M2a/M2b、`hakios_mali610_stop()`/`quiesce()` |
| `mtk/mt6895/drivers/gpu/hakios.mali610.c` | **5,464 行（已过期！）** | ⚠️ **Astra 动工前必须先把 7,159 行版本同步过来**，此后每次改动保持两份字节一致（`cmp` + `md5sum` 校验） |
| `mtk/mt6768/drivers/gpu/hakios.mali.cpp` | 650 行 | **IOKit Mali 服务蓝本**：展示 `HakiosMali` / `IOUserClient` / `IOBufferMemoryDescriptor` / `IOSubMemoryDescriptor` / `kHakiosSurface` / `MiniIOSurfaceDrawn` 完整写法 |
| `drivers/gpu/hakios.gpu.cpp` | 1,222 行 | **IOKit `HakiosGPU` 服务蓝本**：服务名 `HakiosGPU`、`kHakiosSurface`、`kHakiosShow`、缓存维护、帧率统计与超时诊断 |
| `tools/mk-mesa.sh` | 125 行 | Mesa 25.0.7 交叉编译脚本（`arm64-apple-ios`，产 `libOSMesa.8.dylib`） |
| `tools/mesa/hakios-drm/hakios.drm.c` + `xf86drm.h` | 226 + ~100 行 | Mesa 侧 `libdrm` 替身（将 `drmIoctl` / `drmCommandWriteRead` / `mmap` / `PRIME` 转发给 `IOConnectCallStructMethod` / `IOConnectMapMemory64`） |
| `tools/mesa/freedreno-darwin.patch` | 365 行 | **Mesa Darwin 适配补丁蓝本**：展示怎么接 `hakios-drm`、`osmesa_create_gpu_screen()` 和 `osmesa_surface_resource()` |
| `userland/src/eagl.c` + `userland/src/gl_osmesa.c` | 4,205 + 115 行 | `libhakios.gl.dylib`（与底层驱动解耦，只要 Mesa 输出真 GPU `GL_RENDERER` 且实现 `OES_EGL_image` 即自动工作） |
| `docs/headset-wip/raw/mainline-xaga-gpu/` | 15 个文件 | 主线 Linux `7.2-mt6895-xiaomi-xaga` 的 `panthor_{device,fw,gpu,hw,mmu}.c` + 寄存器头 + `mt6895.dtsi` + `gpueb_power.c` |

### 1.3 必须死守的 6 条 MT6895 / XNU 硬件铁律

1. **页粒度铁律（16 KiB vs 4 KiB）**：XNU `PAGE_SIZE = 16384`，但 Mali-G610 MMU L3 叶子是 **4 KiB**（`PT_LEAF_GRANULE = 0x1000`）。每映射一个 16 KiB XNU 页，必须拆成 4 个连续 4 KiB PTE 写入页表（参照 `hakios.mali.cpp:313`）。
2. **地址空间隔离铁律**：`AS0` 专供 CSF MCU 固件（不要把用户 BO 塞进 `AS0`）。用户 GPU VM 必须绑定到 **`AS1..AS7`**（初期单 VM 固定用 `AS1`，通过 `csg_in->config` 指定 `AS1`）。
3. **40-bit PA 验证铁律**：XNU `kmem_alloc` 与 `MiniIOSurface` 的物理页都在 4 GiB 以上（如 `0x1_xxxx_xxxx`）。`M2c` 第一件事就是实测 `AS1` 映射 >4 GiB PA 页，确认 Mali-G610 正常读写，不依赖 192 MiB 的 `mem_alloc_low` 窗口。
4. **缓存一致性铁律**：
   - Mesa 创建的常规 BO（`PANTHOR_BO_CREATE`）使用 `kIOMapWriteCombineCache`（与 `hakios.gpu.cpp` / `hakios.mali.cpp` 一致），CPU 写完即直达内存（配合 `dsb sy`）；
   - `MiniIOSurface` 的像素内存是 CPU 侧 **Cached** 内存，提交前必须 `FlushPoC_DcacheRegion`，GPU 写回后 CPU 读前必须做 L2/LSC flush（CSF `FLUSH_CACHE2`）+ CPU D-cache invalidate，严禁 `pmap_set_cache_attributes()` 造成别名冲突（见 `hakios.mali610.c:868-875` 血泪注释）。
5. **动态页表池铁律**：当前 `gPt[MALI_PT_MAX]` 上限仅 512 个页表页（且静态数组无释放）。支撑 SpringBoard 需要动态分配/回收页表页并加锁，支持 `pt_unmap()` + `AS_COMMAND_FLUSH_PT` / `AS_COMMAND_FLUSH_MEM`。
6. **`RETURN` 闭环铁律**：每个里程碑刷机前必须保证 `hakios_mali610_stop()` 会先停全部活跃 CSG（`CSG_STATE_TERMINATE`）、关闭用户 client 新提交、禁用 MCU、释放 GPUEB power ref，确保每版 **`RETURN < 40 s` 无挂死**。

---

## 2. 五阶段攻坚路线图（M2c → M5）

| 阶段 | 名称 | 核心目标 | 预计复杂度 | 验收判据 |
|---|---|---|---|---|
| **M2c** | AS1 MMU + 首个 Shader Core 执行 | 打通用户态地址空间 `AS1`（含 >4 GiB PA）并在 Shader Core 上跑通一条真实 `RUN_COMPUTE` Valhall shader | 中 | `m2c: PASS`（shader 算出的多字模式落盘），`fault=0`，`RETURN < 40s` |
| **M3** | 内核 `panthor` UAPI + IOKit `HakiosGPU` | 实现 `hakios.mali610.cpp`（注册 `HakiosGPU`）与 `panthor` 最小 ioctl 集 + 用户态 `panthor_test` | 高 | `panthor_test` 用户态创建 VM/BO/Group 提交 CSF 指令并等 syncobj 返回 PASS |
| **M4a** | Mesa 25.0.7 `panfrost` 跨编译 + `gl_test` | 扩展 `hakios-drm` + 写 `panfrost-darwin.patch` + 编出 `libOSMesa.8.dylib` + 设备跑通 GLES 3.0 三角形 | 中-高 | `GL_RENDERER` 为 `Mali-G610 (Panfrost)`，`gl_test` 画面显示红/彩三角形，覆盖率 ~26%，0 fault |
| **M4b** | 零拷贝 `MiniIOSurface` + Tiler Heap OOM + 模糊压测 | 接通 `kHakiosSurface` + `MiniIOSurfaceDrawn` + `CS_TILER_OOM` 动态扩容 + 离屏模糊/FBO 100 帧压测 | 中 | `gl_blur_test` 零拷贝渲染到 `MiniIOSurface` 并上屏，连续 100 帧 0 fault、0 花屏 |
| **M5** | `backboardd` / `SpringBoard` 全线切 GPU | 打包 `GL=1` 镜像上线，`backboardd` 切 OpenGL ES 3.0，`SpringBoard` 自动恢复毛玻璃 vibrancy | 中 | `hakios-gl:` 报 `Mali-G610`，Dock 恢复毛玻璃白/半透明，滑动 45-60 fps，`RETURN < 40s` |

---

### Stage 1 — M2c：用户地址空间 `AS1` + 动态 MMU + 首个 Valhall Compute Shader

**目标**：把 `hakios.mali610.c` 从「只能在 `AS0` 共享段跑 5 条 CEU 指令」推进到「拥有完整可动态映射/解映射的 `AS1` 地址空间 + 驱动 6 核 Shader Core 真实执行代码」。

**关键技术点**：
1. **同步双份代码**：先将 `kernel-src/bsd/dev/hakios.mali610.c`（7,159 行）同步到 `mtk/mt6895/drivers/gpu/hakios.mali610.c`，确认 `cmp` 一致。
2. **多 VM / `AS1` MMU 抽象**（参考 `docs/headset-wip/raw/mainline-xaga-gpu/panthor_mmu.c`）：
   - 抽出 `struct mali_vm`（含 `root_pa`、`root_kva`、动态页表链表/红黑树、引用计数、绑定槽位 `as_slot`）。
   - 支持 `vm_map_range(vm, gpu_va, pa, size, flags)`（按 4 KiB 步进填 L3 PTE，权限位遵循 `PTE_UNPRIV` / `PTE_RDONLY` / `PTE_XN` + `MALI_MEMATTR` 索引）与 `vm_unmap_range(vm, gpu_va, size)`。
   - 实现 `as_flush_range(as, va, size)`（先 `AS_COMMAND_LOCK` → 更新 PTE → `AS_COMMAND_FLUSH_PT` / `AS_COMMAND_FLUSH_MEM` → `AS_COMMAND_UNLOCK`）。
   - 用 `as_enable(1, vm1->root_pa)` 启用 **`AS1`**，并在 `csg_in->config` 里将 Group 0 绑定到 `as = 1`（参照 `panthor_sched.c` 中 `CSG_CONFIG_AS`）。
3. **验证 >4 GiB PA**：用标准 `kmem_alloc`（不走 `mem_alloc_low`）分配 `AS1` 的 ring buffer、输出 buffer 与 shader buffer，验证 CEU 在 `AS1` 下能正常 fetch ring 并执行 `MOV48 + SYNC_ADD64`。
4. **验证 Shader Core（`RUN_COMPUTE`）**：
   - 在 `AS1` 里放置一个极简预编译 Valhall v10 compute shader（8-16 字节机器码，如 `STORE` 一个由 `workgroup_id`/`local_id` 算出的 32-bit 常数到寄存器/Uniform 指向的 `AS1` 缓冲区 + `END`），配上最小 Compute Job 描述符（SRT / DCD / FAU / Thread Storage）——可从 Mesa `panfrost` 的 Valhall 单元测试或 `panthor` 文档精确核对字节布局。
   - 在 ring 里发送 `MOV48` 设置 Compute 寄存器 + `RUN_COMPUTE`（opcode `0x07`）+ `WAIT` + `FLUSH_CACHE2`（L2+LSC clean+inv）+ `SYNC_ADD64`。
5. **M2c 验收标准（DSH 上机采集）**：
   - `/dev/hlog` 打印 `AS1 enabled` 且 `m2c_as1_high_pa: PASS`（打印实测 PA `>= 0x100000000`）；
   - `/dev/hlog` 打印 `m2c_compute: PASS`，CPU 读回目标缓冲区内容与 shader 预期计算值逐字匹配；
   - `cs fault 0x0 fatal 0x0`，`mmu irq` 无 `PAGE_FAULT`；
   - `RETURN` 正常回 Android（`< 40 s`）。

---

### Stage 2 — M3：内核 `panthor` UAPI + IOKit `HakiosGPU` 服务

**目标**：向用户态暴露 Linux `panthor` DRM UAPI（经由 HakiOS 标准的 IOKit `HakiosGPU` `IOUserClient`），使 Mesa 25.0.7 的 `panthor_kmod.c` 只需极小胶水即可驱动 Mali-G610。

**核心设计（高度复用 `hakios.mali.cpp` + `hakios.gpu.cpp` 模式）**：
1. **新建 `iokit/Kernel/hakios.mali610.cpp`**（同步放一份到 `mtk/mt6895/drivers/gpu/hakios.mali610.cpp`，并在 `mtk/mt6895/drivers/gpu/conf` 注册）：
   - 注册 IOKit 服务名 **`HakiosGPU`**（关键！与 `inject.c:1087`、`inject_sb.c:658`、`hakios.drm.c:19` 完全对齐，同时保持与 Qualcomm `hakios.gpu.cpp` 互斥编译）。
   - `externalMethod(selector, ...)` 按 Linux DRM ioctl 低字节（`DRM_COMMAND_BASE + nr`）分发：

| Selector | 对应 Linux `panthor_drm.h` / `drm.h` | M3 首版最小实现策略 |
|---|---|---|
| `0x09` | `DRM_IOCTL_GEM_CLOSE` | 等在途 job 完成 → unmap VM 映射 → 释放 `IOMemoryDescriptor` |
| `0x40` | `DRM_IOCTL_PANTHOR_DEV_QUERY` | 返回真实硬件寄存器填充的 `drm_panthor_gpu_info`、`drm_panthor_csif_info`、`drm_panthor_timestamp_info`、`drm_panthor_group_priorities_info` |
| `0x41` | `DRM_IOCTL_PANTHOR_VM_CREATE` | 创建 `struct mali_vm`（首版绑定 `AS1`，后续多 client 动态分配 `AS1..AS7`） |
| `0x42` | `DRM_IOCTL_PANTHOR_VM_DESTROY` | 停关联 Group → 刷空 AS → 释放 VM 页表 |
| `0x43` | `DRM_IOCTL_PANTHOR_VM_BIND` | 同步执行 `MAP` / `UNMAP` / `SYNC_ONLY`（Mesa `panthor_kmod` 同时支持同步与异步 `VM_BIND`，首版只设 sync 或同步完成并立即 signal out-syncobj） |
| `0x44` | `DRM_IOCTL_PANTHOR_VM_GET_STATE` | 返回 `DRM_PANTHOR_VM_STATE_USABLE`（发生未处理页错误时置 `UNUSABLE`） |
| `0x45` | `DRM_IOCTL_PANTHOR_BO_CREATE` | 分配 16 KiB 对齐 `IOBufferMemoryDescriptor`（`kIOMapWriteCombineCache`），支持 `DRM_PANTHOR_BO_NO_MMAP` |
| `0x46` | `DRM_IOCTL_PANTHOR_BO_MMAP_OFFSET` | 返回 `(uint64_t)handle << 12`（与 `hakios.mali.cpp:543` 一致，通过 `clientMemoryForType(handle)` 映射进用户态） |
| `0x47` | `DRM_IOCTL_PANTHOR_GROUP_CREATE` | 在 `AS1` 创建 CSG + 1..N 个 CS（Mesa panfrost 通常只建 1 个 CS，含 ringbuf + user_io + suspend_buf + tiler heap 绑定） |
| `0x48` | `DRM_IOCTL_PANTHOR_GROUP_DESTROY` | `CSG_STATE_TERMINATE` + 等待固件 ACK + 回收资源 |
| `0x49` | `DRM_IOCTL_PANTHOR_GROUP_SUBMIT` | 解析 `drm_panthor_queue_submit`（`stream_addr` / `stream_size`）→ 写入 CS ring：`CALL` 指令指向用户 `stream_addr`（长度 `stream_size`）+ `FLUSH_CACHE2` + `SYNC_ADD64`（写内核单调 `seq`）→ 敲 `DOORBELL(1)` → 更新 out-syncobj 为该 `seq` |
| `0x4a` | `DRM_IOCTL_PANTHOR_GROUP_GET_STATE` | 返回 `state = 0`（异常时置 `TIMEDOUT` / `FATAL_FAULT`） |
| `0x4b` | `DRM_IOCTL_PANTHOR_TILER_HEAP_CREATE` | 预分配 `initial_chunk_count` 个 chunk（每块按请求大小，如 2 MiB）并映射进 VM，建立 Valhall tiler heap context 链表 |
| `0x4c` | `DRM_IOCTL_PANTHOR_TILER_HEAP_DESTROY` | 释放 heap chunks 并从 VM unmap |
| `0x70..0x75` | `DRM_IOCTL_SYNCOBJ_*`（CREATE / DESTROY / WAIT / TIMELINE_WAIT / RESET / SIGNAL） | 轻量级内核 syncobj 表：每个 syncobj 记录 `(timeline_point, gpu_seq)`，`WAIT`/`TIMELINE_WAIT` 轮询或等待 `gSyncSeq >= target_seq`（超时 5 s 吐完整 fault dump，参照 `hakios.gpu.cpp:190`） |
| `0x7e` | `kHakiosSurface` | 包装 `MiniIOSurface`（`IOSubMemoryDescriptor` 从 `PAGE_SIZE` 起）返回 BO handle + 宽高/步长/格式（含 `kHakiosDescribe` 模式） |
| `0x7f` | `kHakiosShow` | 将用户态像素缓冲通过 `MiniIOMobileFramebufferShow()` 直接刷到屏幕，用于 M4a 独立可视化验收 |

2. **关键细节（避坑指南）**：
   - **不要在 M3 就做用户态直写 Doorbell MMIO**：Mesa `panthor_kmod` 的提交全部走 `DRM_IOCTL_PANTHOR_GROUP_SUBMIT`（内核把 `CALL(stream_addr, stream_size)` 塞进内核管理的 ring 并敲 `DOORBELL(1)`），**无需**把 GPU Doorbell MMIO 暴露给用户态！仅需把 `cs_user_io` / `glb_time`（若 Mesa 查询 `PANTHOR_MMIO_FLUSH_CACHE` 或 `PANTHOR_USER_MMIO_OFFSET`）处理好即可（注意检查 Mesa 25.0.7 `panthor_kmod.c` 是否 mmap `DRM_PANTHOR_USER_MMIO_OFFSET`——如果是，提供一个只读/安全映射的假页或同步页即可，务必先读源码核实！）。
   - **固件中断 / 轮询策略**：首版可以与 `hakios.gpu.cpp` 一致，用 200 us 轮询 `uout->extract` + `sync_word`（配合 `FlushPoC_DcacheRegion`），M4b 时再补齐 `CS_TILER_OOM` 轮询/中断处理。
   - **安全清理**：`clientClose()` 时必须等待该 client 所有在途 `GROUP_SUBMIT` 完成，再 terminate 其所有 Group、unmap 所有 BO 并释放 `MiniIOSurface` 引用。
3. **用户态独立测试程序 `userland/src/panthor_test.c`**：
   - 打开 `HakiosGPU`，依次调用 `DEV_QUERY(GPU_INFO)`、`VM_CREATE`、`BO_CREATE`（映射后写入一段含 `MOV48 + SYNC_ADD64` 的 CSF 小程序）、`VM_BIND`、`GROUP_CREATE`、`SYNCOBJ_CREATE`、`GROUP_SUBMIT`、`SYNCOBJ_TIMELINE_WAIT`，最后校验目标 BO 被 GPU 写入预期值并全部 clean destroy。
4. **M3 验收标准（DSH 上机采集）**：
   - 设备启动日志出现 `HakiosGPU: Mali-G610 panthor service started`；
   - 执行 `/redwood/bin/panthor_test` 输出 `panthor_test: PASS (gpu_id=0xa8670004, seq=1, wrote=0xdeadbeef)`；
   - 连续运行 10 次 `panthor_test` 无泄漏、无 GPU/MMU fault；
   - `RETURN < 40 s`。

---

### Stage 3 — M4a：Mesa 25.0.7 `panfrost` 跨编译 + `gl_test` 实机画三角形

**目标**：让上游 Mesa 25.0.7 的 `panfrost` 驱动在 iOS 16.5 ARM64 用户态跑起来，通过 `HakiosGPU` 在 Mali-G610 上完成完整 OpenGL ES 3.0 图形管线（顶点着色器 + Tiler 分块 + 片段着色器 + 帧缓冲写回）。

**关键改动**：
1. **扩展 `tools/mesa/hakios-drm/hakios.drm.c` + `xf86drm.h`**：
   - `drmOpenWithType()` 同时支持 `"panthor"`（与 `"msm"` 兼容，或查询 `HakiosGPU` 属性自动识别当前内核驱动类型）。
   - `drmGetVersion()` 返回 `name = "panthor"`, `version_major = 1, version_minor = 3`（确认满足 Mesa 25.0.7 `panthor_kmod.c` 的最低版本校验）。
   - 补齐 `drmSyncobjCreate` / `Destroy` / `Wait` / `TimelineWait` / `Reset` / `TimelineSignal` 转发到 `HakiosGPU` 的 `0x70..0x75` selector（原 `hakios.drm.c` 对 `freedreno` 返回 `ENOSYS`，但 `panthor` **强依赖** timeline syncobj！）。
   - `drmIoctl()` 注意解析 `panthor` 的 ioctl 号并正确处理大结构体（如 `drm_panthor_group_submit` 中含指针指向 `queue_submits` 和 `syncs` 数组，内核 `externalMethod` 需 `copyin()` 用户态指针内容！⚠️ **重点提醒 Astra：IOKit `structureInputSize` 最大 4096 字节，且所有嵌套 `uint64_t pointer` 必须用 `copyin`/`copyout` 读写**）。
2. **新增 `tools/mesa/panfrost-darwin.patch`**（完全仿照 `tools/mesa/freedreno-darwin.patch`）：
   - `meson.build`：当 `host_machine.system() == 'darwin' and with_gallium_panfrost` 时使用 `src/hakios-drm` 作为 `dep_libdrm`。
   - `src/panfrost/lib/kmod/panthor_kmod.c`：
     - 将 `os_mmap` / `os_munmap` 替换为 `hakios_drm_map` / `hakios_drm_unmap`；
     - 屏蔽 Linux 特有头文件/调用（`sys/sysinfo.h`、disk cache `build_id`、eventfd/dma-buf）；
     - 适配 `panthor_kmod_bo_import` 接收 `IOSurface` ID（仿照 `freedreno_bo.c:248` 用 `hakios_drm_surface()` 取 size）。
   - `src/gallium/targets/osmesa/osmesa_target.c` + `meson.build`：
     - 当定义 `GALLIUM_PANFROST` 时，在 `osmesa_create_gpu_screen()` 中打开 `"panthor"` 并调用 `panfrost_create_screen(fd, &config, NULL)`；
     - `osmesa_surface_resource()` 保持与 `freedreno` 相同的 `WINSYS_HANDLE_TYPE_FD`（传 `IOSurface` ID）路径，使 `panfrost_resource_from_handle()` 零拷贝导入 `MiniIOSurface`。
3. **编译验证**：
   - `DRIVERS="softpipe,panfrost" tools/mk-mesa.sh` → 产 `xnu-deps/build/mesa/out/libOSMesa.8.dylib`；
   - 编译独立测试 `gl_test`（链接 `libOSMesa.8.dylib`）：创建 GLES 3.0 context，打印 `GL_VENDOR` / `GL_RENDERER` / `GL_VERSION`，清屏蓝色并绘制一个红色三角形，`glReadPixels` 校验中心像素与覆盖率，并调用 `kHakiosShow` 直接上屏显示！
4. **M4a 验收标准（DSH 上机采集）**：
   - `gl_test` 打印 `GL_RENDERER = Mali-G610 (Panfrost)`（**绝不能回退到 `softpipe`**）；
   - `glReadPixels` 校验中心像素为纯红、背景为纯蓝、三角形面积占比与几何理论值（如 26%）吻合误差 `< 1%`；
   - 手机屏幕清晰显示该红蓝测试图；
   - 内核日志 0 次 `GPU_FAULT` / `MMU_FAULT` / `CS_FATAL`；
   - `RETURN < 40 s`。

---

### Stage 4 — M4b：零拷贝 `MiniIOSurface` + Tiler Heap OOM + 模糊/Vibrancy 压测

**目标**：消除接入 `backboardd` 前的最后三个杀手级隐患（Tiler OOM 死锁、IOSurface 缓存不一致/撕裂、复杂 shader/FBO 崩溃）。

**关键技术点**：
1. **Tiler Heap 动态扩容（`CS_TILER_OOM`）**：
   - 简单三角形只用几十 KB tiler heap，但 SpringBoard 复杂页面很容易突破初始 chunk。
   - 双保险方案：(a) `TILER_HEAP_CREATE` 首版允许适当调大初始 chunk 数量（如 `max(initial_chunks, 4)`）；(b) 在 `GROUP_SUBMIT` 等待循环里监测 `cs_out->ack` / `csg_out->cs_irq_req` 的 `CS_TILER_OOM` 位，收到后动态分配新 chunk 映入 VM 并通过 `cs_in->heap_start/heap_end` + `CS_REQ_TILER_OOM` 应答固件（严格照搬 `panthor_sched.c` 的 `cs_slot_process_tiler_oom_event_locked()`）。
2. **`MiniIOSurface` 零拷贝 + `MiniIOSurfaceDrawn()` 同步**：
   - 在 `MiniIOSurface.h` 中复用 `maliSeq`（或 `drawn`）字段，在 `GROUP_SUBMIT` 涉及关联 `MiniIOSurface` 的 BO 时记录该 `seq`；
   - 在 `MiniIOSurfaceDrawn(surface, ms)` 中等待该 `seq` 完成（参照 `hakios.mali.cpp:65`），保证 `MiniIOMobileFramebuffer` 扫出屏幕前 GPU 已写完并刷出 L2/LSC；
   - 核实 `BGRA` 步长对齐要求（Mali-G610 线性纹理行步长需 64 字节对齐——`MiniIOSurface` 的 `bytesPerRow` 需校验，如 `1080 * 4 = 4320`，`MiniIOMobileFramebuffer` 当前 `rowbytes = 4352 = 68 * 64`，天然 64 字节对齐！但应用层小图标 surface 若非 64B 对齐需测试是否回退或兼容）。
3. **离屏多 pass / Blur 压测 `gl_blur_test`**：
   - 模拟 CoreAnimation 的典型帧：创建两个 `MiniIOSurface`（源图 + 屏幕目标），用 `glEGLImageTargetTexture2DOES` 导入，执行 2-pass 高斯模糊 + `colorMatrix` 混合，连续渲染 **100 帧**，统计每帧 GPU 耗时（ms）并校验第 100 帧结果像素无花屏、无内存泄漏。
4. **M4b 验收标准（DSH 上机采集）**：
   - `gl_blur_test` 连续 100 帧全部成功，零拷贝路径命中（无软件复制回退），平均单帧耗时 `< 8 ms`；
   - 0 次 `MMU_FAULT` / `CS_TILER_OOM` 死锁 / 内存泄漏；
   - `RETURN < 40 s`。

---

### Stage 5 — M5：`backboardd` + `SpringBoard` 正式上线 Mali-G610（`GL=1`）

**目标**：端到端跑通 iOS 16.5 桌面、锁屏、Dock 毛玻璃 vibrancy、壁纸与日常 App，实现流畅高帧率。

**关键步骤**：
1. 使用 `GL=1 REST=1` 构建 userland，将 `libOSMesa.8.dylib` 与 `libhakios.gl.dylib` 打包进 `/redwood/lib/`。
2. **首测务必保留 `gl=0` 逃生舱**：若 `backboardd` 启动异常，DSH 只需在 boot-args 加 `gl=0` 即可瞬切回 CPU 软渲染排查，无需重编。
3. **上线排障顺序**（借助 `eagl.c` 已有调试开关）：
   - 先用 `glframes=1` 检查首 10 帧是否顺利提交、`MiniIOMobileFramebuffer` 日志是否变为 `after NNN us waiting for the GPU`；
   - 如遇个别图层花屏（例如 `EXT_shader_framebuffer_fetch` 或低精度 `lowp`/`mediump`），先用 `eagl.c` 现成 boot-args `glfetch=0`、`glcopy=1`、`glprecision=0`、`gltrace=1-5` 零重编二分定位；
   - 确认 `SpringBoard` 日志**不再打印** `no GPU draws, vibrant views are plain`（表示 `gpu_draws() == true`），肉眼验收 **Dock 恢复毛玻璃半透明、锁屏时钟恢复 vibrancy、壁纸正常渲染**。
4. **M5 验收标准（操作者 + DSH 共同验收）**：
   - `hakios-gl:` 确认 `Mali-G610 (Panfrost)` 渲染，`backboardd` 稳定不重启；
   - `MiniIOMobileFramebuffer` 实测滑动帧率显著跃升（目标 **45–60 fps**，对比 CPU 基线 18 fps），`0 swaps late`，`0 gave up`；
   - **Dock 栏毛玻璃恢复正常**（不再纯黑），时钟 / 音乐 / 设置 / 天气小组件均正常；
   - 连续交互 3 分钟后 **`RETURN < 40 s`** 干净回 Android。

---

## 3. 给 Astra 的输入材料清单（已就绪 vs 需 DSH 补充抓取）

### 3.1 仓库里**已有**（Astra 可直接读取）
- **当前驱动代码（权威）**：`/home/lizi/IOS/hakios-mtk/logs/ufs-apfs-fstree-root-736/kernel-src/bsd/dev/hakios.mali610.c`（7,159 行）
- **IOKit GPU 蓝本**：
  - `/home/lizi/IOS/hakios-mtk/mtk/mt6768/drivers/gpu/hakios.mali.cpp`（650 行，Mali `panfrost` IOKit 服务）
  - `/home/lizi/IOS/hakios-mtk/drivers/gpu/hakios.gpu.cpp`（1,222 行，Adreno `HakiosGPU` IOKit 服务）
  - `/home/lizi/IOS/hakios-mtk/logs/ufs-apfs-fstree-root-736/kernel-src/iokit/Kernel/MiniIOSurface.h`
- **Mesa 跨编译与 Darwin 适配蓝本**：
  - `/home/lizi/IOS/hakios-mtk/tools/mk-mesa.sh`
  - `/home/lizi/IOS/hakios-mtk/tools/mesa/hakios-drm/hakios.drm.c` + `xf86drm.h`
  - `/home/lizi/IOS/hakios-mtk/tools/mesa/freedreno-darwin.patch`
  - `/home/lizi/IOS/hakios-mtk/userland/src/eagl.c` + `userland/src/gl_osmesa.c`
- **主线 Linux `panthor`（已有部分）**：`/home/lizi/orca/hakios-xaga/docs/headset-wip/raw/mainline-xaga-gpu/`
  - `panthor_device.c`, `panthor_fw.c`, `panthor_gpu.c`, `panthor_hw.c`, `panthor_mmu.c`, `panthor_*_regs.h`, `mt6895.dtsi`, `gpueb_power.c`
- **Android 侧参考**：`/home/lizi/IOS/work-xaga/ko/mali_kbase_mt6895.ko`、`/home/lizi/IOS/work-xaga/disp/mali-regs-history-android.txt`

### 3.2 建议 DSH **提前帮 Astra 抓取**放进 `docs/headset-wip/raw/gpu-attack/refs/` 的缺失上游源码
（Astra 可能处于无外网或离线环境，提前抓齐这 7 个文件能让 Astra 零阻塞推进 M2c–M4a！）

1. **Linux 主线 `panthor` 剩余 5 个核心文件**（同源 `MT6895-Mainline/linux` 分支 `7.2-mt6895-xiaomi-xaga` 或上游 Linux `6.12+`）：
   - `include/uapi/drm/panthor_drm.h`（**M3 结构体与 ioctl 编号唯一权威定义！**）
   - `drivers/gpu/drm/panthor/panthor_sched.c`（**M2c `AS1`/CSG 绑定 + M3 `GROUP_SUBMIT` `CALL` 指令 + M4b `TILER_OOM` 唯一权威参考！**）
   - `drivers/gpu/drm/panthor/panthor_heap.c`（**M3/M4b Tiler Heap chunk 链表结构唯一权威参考！**）
   - `drivers/gpu/drm/panthor/panthor_drv.c`（ioctl 分发与 `DEV_QUERY` 字段填充逻辑）
   - `drivers/gpu/drm/panthor/panthor_gem.c` + `panthor_fw.h`
2. **Mesa 25.0.7 源码包**（提前拉到 `/home/lizi/IOS/hakios-mtk/xnu-deps/build/mesa/mesa-25.0.7.tar.xz` 并校验 SHA256 `592272df3cf01e85e7db300c449df5061092574d099da275d19e97ef0510f8a6`），并确保 Astra 可直接查阅解压后的：
   - `src/panfrost/lib/kmod/panthor_kmod.c`（**M3/M4a 精确看 Mesa 实际调用哪些 ioctl、syncobj 和 mmap！**）
   - `src/panfrost/lib/kmod/pan_kmod.c`
   - `src/gallium/drivers/panfrost/pan_screen.c` & `pan_resource.c`
   - `src/panfrost/compiler/valhall/`（M2c 极简 compute shader 机器码参考）

> **注意**：**不需要** Android 的 `libGLES_mali.so`（那是闭源 kbase blob，协议与 `panthor` 完全不同，只会引入噪音）；也不需要手啃千页 Mali 架构手册——上游 Linux `panthor` + Mesa 25.0.7 `panfrost` 已经是现成、经过真实 Mali-G610 硬件验证的完整规范。

---

## 4. Astra 任务清单（按执行顺序，每步单变量推进）

- [ ] **Task 0（代码对齐）**：把 `kernel-src/bsd/dev/hakios.mali610.c`（7,159 行）同步到 `mtk/mt6895/drivers/gpu/hakios.mali610.c`，确认 `cmp` 一致；阅读 `panthor_drm.h`、`panthor_sched.c`、`panthor_heap.c` 与 Mesa 25.0.7 `panthor_kmod.c`。
- [ ] **Task 1（M2c — `AS1` + >4 GiB PA）**：在 `hakios.mali610.c` 实现多 VM 动态 4 KiB 页表（`vm_create`/`vm_map`/`vm_unmap`/`as_flush`），启用 `AS1`，用标准 >4 GiB `kmem_alloc` 页跑通 `AS1` 上的 M2b（`MOV48 + SYNC_ADD64`），交给 DSH 实机验证 + `RETURN`。
- [ ] **Task 2（M2c — Shader Core `RUN_COMPUTE`）**：在 `AS1` 构造最小 Valhall v10 compute shader + DCD/SRT，发出 `RUN_COMPUTE + FLUSH_CACHE2 + SYNC_ADD64`，交给 DSH 实机验证 shader 写回 + `RETURN`。
- [ ] **Task 3（M3 — `HakiosGPU` Panthor UAPI + `panthor_test`）**：实现 `iokit/Kernel/hakios.mali610.cpp`（服务名 `HakiosGPU`，完整 Panthor 13 个 ioctl + syncobj + `kHakiosSurface`/`kHakiosShow`）及配套内核接口，同步 `mtk/mt6895/drivers/gpu/`；编写 `userland/src/panthor_test.c`，交给 DSH 编译刷机验证 + `RETURN`。
- [ ] **Task 4（M4a — Mesa 25.0.7 `panfrost` + `gl_test`）**：扩展 `tools/mesa/hakios-drm/` 支持 `panthor` + timeline syncobj，编写 `tools/mesa/panfrost-darwin.patch`，用 `DRIVERS="softpipe,panfrost" tools/mk-mesa.sh` 编出 `libOSMesa.8.dylib`；在设备上跑通 `gl_test` 硬件三角形渲染并上屏 + `RETURN`。
- [ ] **Task 5（M4b — 零拷贝 `MiniIOSurface` + `TILER_OOM` + `gl_blur_test`）**：打通 `CS_TILER_OOM` 动态 chunk 扩容、`kHakiosSurface` 零拷贝与 `MiniIOSurfaceDrawn()` 同步，跑通 100 帧模糊压测 + `RETURN`。
- [ ] **Task 6（M5 — `backboardd` / `SpringBoard` 上线）**：打 `GL=1` 镜像验证 `backboardd` 切入 `Mali-G610 (Panfrost)`、Dock 毛玻璃恢复、滑动帧率测试与 3 分钟稳定性 + `RETURN < 40 s` 闭环。

---

## 5. 协作纪律

1. **严格单变量推进**：M2c（AS1）、M2c（Shader）、M3（panthor_test）、M4a（gl_test）、M4b（blur 压测）、M5（SpringBoard）逐级刷机闭合，**绝不在 `gl_test` 还没出图前直接把 `backboardd` 切到 GPU**。
2. **代码归属与双份同步**：Astra 改完任何内核/工具代码后，必须保持 `kernel-src/bsd/dev/hakios.mali610.c` ↔ `mtk/mt6895/drivers/gpu/hakios.mali610.c`（以及新建的 `hakios.mali610.cpp`）字节一致；DSH 负责编译、刷机、采集 `/dev/hlog` + `xnusys` logbook + 屏幕拍照/录屏反馈。
3. **每版必验 `RETURN`**：任何阶段一旦导致 `RETURN` 超时或挂死，立即先修 `hakios_mali610_stop()` 清理路径再继续，绝不带病叠加下一个里程碑。

---

## 6. 风险点矩阵与每阶段回滚 / 保险丝机制

| 阶段 | 最可能卡死的点 | 会不会把整机挂死？ | 工程防护（提前防爆） | 零重编保险丝 / 回滚方案 |
|---|---|---|---|---|
| **M2c**（AS1 MMU + `RUN_COMPUTE`） | ① `AS1` PTE 填错物理地址（指向非 DRAM/受保护 EMI 区）；② 误改正在跑固件的 `AS0`；③ `RUN_COMPUTE` 描述符格式错导致 CS fatal/hang | 仅 VA 未映射只会报 `MMU PAGE_FAULT` **不会挂机**；但映射非法 PA 或关钟后写 MMIO **会触发 EMI 挂死/SError** | 严禁动 `AS0`；`vm_map` 强制校验 `pa` 对齐 4 KiB 且落在合法 DRAM 区间；`RUN_COMPUTE` 轮询上限 `200 ms`，超时打全量 `cs_out`/`as1 faultstatus`/`irq` 后退出不阻塞启动 | 新功能挂在独立 boot-arg **`mali_m2c=1`** 后（默认关闭直到首测通过）；异常直接去 boot-arg 或回刷 `golden/boot-985b-golden.img` |
| **M3**（`HakiosGPU` Panthor UAPI） | ① `GROUP_SUBMIT` / `VM_BIND` 嵌套用户指针 `copyin` 越界或内核页引用泄漏导致 XNU panic；② 未停干净的 CSG 卡住 `RETURN` | XNU panic 会直接进 panic/reboot；活跃 CSG 未 terminate 会拖慢或卡死 `RETURN` | 上限保护（单 client 最多 4096 BO、单 BO <= 256 MiB）；`hakios_mali610_stop()` 先置 `gMaliStopping=1` 拒收新提交、最多等 500 ms 后发 `CSG_STATE_TERMINATE` 再关 MCU/释放 GPUEB | 服务注册挂在 **`gpu=1`**（或 `mali_gpu=1`）；此阶段尚未安装 `libhakios.gl.dylib`，`backboardd` 完全不受影响，仅由独立 `panthor_test` 触发 |
| **M4a / M4b**（Mesa `panfrost` + `TILER_OOM` + 零拷贝） | ① **头号大坑**：Valhall CSF 复杂帧触发 `CS_TILER_OOM` 时若内核未应答扩容，GPU 会永久挂在 tiler wait；② `MiniIOSurface` 缓存未 `FlushPoC_DcacheRegion` 或非 64B 对齐 `bytesPerRow` 导致花屏 | **不会挂整机**，但该提交会超时（5 s）并置 Group 为 `TIMEDOUT` | `TILER_HEAP_CREATE` 初始给足 chunk（>= 4 块）+ M4b 必做 `CS_TILER_OOM` 动态扩容；5 s 超时看门狗自动吐 fault dump 并返回 `kIOReturnTimeout`；所有测试先在独立 `gl_test` / `gl_blur_test` 闭合 | `backboardd` 此时仍不加载 `libhakios.gl.dylib`（或保持 `gl=0`），完全隔离风险 |
| **M5**（`backboardd` / `SpringBoard` `GL=1` 上线） | ① `backboardd` 某条 CoreAnimation shader 触发 `EXT_shader_framebuffer_fetch` 或精度差异导致局部花屏/黑屏；② 高频提交暴露竞态/内存泄漏 | 最坏情况黑屏或 `backboardd` 重启循环，但 `hakid` / `RETURN` 仍可用 | 先用 `glframes=1` 观察前 10 帧，再利用 `eagl.c` 内置开关零重编二分（`glcopy=1` 关零拷贝、`glfetch=0` 关 fb fetch、`glprecision=0`、`gltrace=1-5`） | **一键回退 boot-arg `gl=0`** 立即切回 CPU 软渲染；终极兜底 `golden/boot-985b-golden.img` |

