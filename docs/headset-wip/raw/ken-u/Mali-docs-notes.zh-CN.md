# Mali-G610 内核驱动开发笔记（中文，开发日志）

> 来自移植过程中的实测记录。命令里的路径用环境变量表示（见 `.env.example`）；`板子运行 X` 指当时保存的串口日志。

# Mali-G610 (RK3588) 移植笔记

目标：让 iPadOS 用户态（backboardd / QuartzCore）拿到 IOAccelerator，用 GPU 合成，摆脱 CPU 软渲染
（慢、撕裂、30 s 的 IOAccelerator 等待、锁屏壁纸黑）。

## 路线（沿用 UniXNU 在 sheng / Adreno 740 上跑通的那条）

```
QuartzCore/RenderBox → AdrenoMetal（Metal 前端，用户态插件） → metal2vulkan（AIR→SPIR-V）
  → Turnip（Mesa Vulkan）→ Darwin KMD → Adreno740（XNU 内核驱动）→ GPU
```

换成 G610：Turnip → **PanVK**（Mesa `src/panfrost/vulkan/csf`。Turnip 那边是 `turnip-darwin-kmd.patch` 加 1400 行 Darwin 后端；
PanVK 直接发 Panthor ioctl，不止走 `pan_kmod`，所以这边改成用户态 libdrm 垫片 + 内核实现 Panthor uAPI，见 M3-2）；Adreno740 → **Mali**（本目录同级
`Mali 仓库`，参考 Linux Panthor）。

| 要用的东西 | 在哪 |
| --- | --- |
| Panthor 内核驱动（参考实现，1.2 万行） | `$AOSP/kernel-6.12/drivers/gpu/drm/panthor` |
| RK3588 电源域（`pm_domains.c`，vendor 5.10/6.1） | `$AOSP/kernel-6.1/drivers/soc/rockchip/pm_domains.c` |
| CSF 固件（274432 B） | `$AOSP/vendor/rockchip/common/gpu/MaliG610/firmware/mali_csffw.bin` |
| Mesa（26.1-devel，含 PanVK + `pan_kmod`） | `$AOSP/external/mesa3d` |
| UniXNU 的 Adreno 栈（参照物） | `$UNIXNU/drivers/{Adreno740,AdrenoMetal}`、`scripts/build-turnip-darwin.py` |
| iPhoneOS SDK | `$IOS_SDK_DIR/iPhoneOS16.5.sdk`（和 `iPhoneOS16.5.sdk.tar.xz`），构建机上就有 |

## 硬件事实（全部实测或来自板子的设备树）

| 项 | 值 |
| --- | --- |
| GPU 寄存器 | `0xfb000000`，大小 `0x200000`；CSF 门铃 `0x80000 + i*0x10000` |
| 中断（SPI） | GPU 94 / MMU 93 / JOB 92（GIC INTID = 32 + SPI） |
| 电源域 | `power-domains = <&power 12>`；PMU `0xfd8d8000` |
| 时钟 | `clk_mali`＝SCMI 5（BL31 管，`assigned-clock-rates=200 MHz`）；CRU 276/277/278（clk_gpu / coregroup / stacks） |
| 电压 | `mali-supply = vdd_gpu_s0`（RK806 DCDC_REG1，`regulator-boot-on`，没动过） |
| GPU_ID | **`0xa8670005`** = Mali-G610（arch 10.8.6，product 7） |
| 核 | SHADER_PRESENT `0x00050005`（4 核），TILER 1，L2 1，STACK_PRESENT `0x55`，AS_PRESENT `0xff` |
| MMU | MMU_FEATURES `0x2830` = VA 48 位 / PA 40 位 |
| 上电后默认 | GPU_IRQ_RAWSTAT = `0x100`（RESET_COMPLETED），MCU_CONTROL/STATUS = 0（MCU 关着） |

### 上电序列（U-Boot 里手动验证过，2026-10-28）

```
mw.l 0xfd7c0578 0x00ff0005   # CLKSEL_CON(158)：clk_gpu_src = GPLL / 6 ≈ 198 MHz（复位值是 SPLL/1，太快）
mw.l 0xfd8d814c 0x00010000   # PMU_PWR：bit0=0 → 域 12 开（高半字是写使能）
md.l 0xfd8d8290 1            # 修复状态 bit1 = 1 → 已上电；idle/ack（0x120/0x118 bit0）自己掉到 0，不用再 deidle
md.l 0xfb000000 8            # a8670005 07120306 00000000 00000809 00000301 00002830 000000ff 040a0412
```

CRU 门控 `CLKGATE_CON(66)` = `0xfd7c0908`：复位后 GPU 的四个时钟（bit 1/4/6/7）本来就是开的（读到 `0x4`，bit 2 是别的）。
复位前板子上 PMU `0x14c` = `0xfff9`、`0x290` = `0xfff70001`：域 12 关。

## 里程碑

| | 内容 | 状态 |
| --- | --- | --- |
| M0 | 上电 + 复位 + 读 GPU_ID + L2 上电 | **完成**（U-Boot 手动 + 内核，`板子运行 mali0.*`） |
| M1 | 固件进 MCU 地址空间（AS0，自建 AArch64 4K 页表）、MCU 启动、全局接口握手 | **完成**（`板子运行 mali1.*`，`mali=2`）：MCU 起来，CSF 接口 v1.1.0，8 个 group × 8 个 stream，config + ping 被固件应答 |
| M2 | 一个命令流队列，一个写内存的 job | **完成（命令流部分）**（`板子运行 mali6.*`，`mali=3`）：一个 group + 一个 queue 起来，命令流（MOVE48/MOVE32/STORE_MULTIPLE/WAIT）把两个字写进 AS1 里的内存，CPU 读回一致；连提交两次都对。|
| M2b | `RUN_COMPUTE` + 手汇编的 Valhall 着色器 | **完成**（`板子运行 mali16.*`，`mali=4`）：一个线程的着色器把一个字写进内存，CPU 读回一致；FW 自己把 4 个着色器核上电（`SHADER_READY` 0 → 0x50005） |
| M3 | IOKit user client + 用户态 DRM 垫片（PanVK 的 Panthor ioctl 转发）；板子上跑 Vulkan 计算 | **完成（计算）**：PanVK 特性核查 ✅、iOS 交叉编译链接 ✅、内核侧通用化 M3a ✅、IOKit service + user client M3b ✅、用户态进程提交作业 M3c ✅、**tiler heap + libdrm 垫片 + PanVK 在真板上枚举 Mali-G610、建设备、编译管线、dispatch、fence、读回全对 M3d ✅**；图形作业（FBD + RUN_FRAGMENT）、中断、错误上报 ⬜ |
| M4 | AdrenoMetal 前端换 PanVK；backboardd 拿到 IOAccelerator；第一帧 GPU 合成 | 未开始 |

## M1 实测（2026-10-28 15:05，`板子运行 mali1）

```
firmware: header 0.3, 274432 bytes, entries end at 960
section 0..6 loaded（共 7 段 + 1 段 protected-mode 跳过），36864 B 页表，root 0x5e802000
AS0 enabled: transtab 0x5e802000 transcfg 0x420001c6 memattr 0x4c4c4c4c9c4c9f4c
MCU booted (MCU status 1, job irq raw 0x80000000)
CSF interface v1.1.0, features 0, 8 groups (stride 0xa0), instrumentation features 0x71, input 0x4002000 output 0x4006000
group 0: 8 streams (stride 0xc), suspend buffer 0x63c0
global interface config + ping ACKNOWLEDGED (req 0x10e ack 0x10e)
```

## M2 实测（2026-10-28 16:00，`板子运行 mali6）

```
m2: group 0 … stream 0 input 0x4003400 output 0x4007200
m2: suspend buffer 25536 B, protected-mode suspend buffer 25536 B
AS1 enabled: transtab … transcfg 0x420001c6
m2: group STARTED (csg req 0x11 ack 0x11, …)      # START | ENDPOINT_CONFIG 都被应答
m2: stream STARTED (cs req 0x511 ack 0x11 …)
m2: job 0: OK: extract 64 of 64, data 0x600dcafe 0x12345678
m2: job 1: OK: extract 128 of 128, data 0x600dcaff 0x23456789
m2 PASSED
```

做法（全部照 Panthor）：队列的 8 KB input/output 对和 suspend buffer（含 protm suspend buffer）映在 **MCU 的 AS0** 里，ring buffer 和数据页映在 **GPU 的 AS1** 里；
`cs_slot_prog_locked` → `csg_slot_prog_locked`（`config` = AS 号、`endpoint_req` 优先级 15、suspend/protm buf VA、`doorbell_req` 按队列掩码翻转）→ CSG `req` = START | 翻转 ENDPOINT_CONFIG →
翻全局 `doorbell_req` 的 csg 位并敲 doorbell 0 → 写命令流、`insert += 64`、敲 `CSF_DOORBELL(csg_id + 1)`。命令流每次 8 条（64 B 对齐，不足补 NOP）：
`SET_SB_ENTRY(3,0)`、`MOVE48 r0=地址`、`MOVE32 r2/r3=值`、`STORE_MULTIPLE r2..r3 → [r0]`、`WAIT(1<<0)`。

**坑（花了 4 轮）：** MCU 的队列接口和 suspend buffer **必须放在 MCU 的 shared region（0x04000000–0x08000000）里**（Panthor 的 `CSF_MCU_SHARED_REGION_*`）。
放到 0x10000000 时 CSG 能启动、但 stream 永远不 ACK START，AS0 上报「写 0x1000 的 translation fault」——固件把地址当 `& 0x0fffffff` 用了（`ringbuf_input` 变 0、
`ringbuf_output` 变 0x1000，读 0 能过、写 0x1000 就挂）。看到「AS0 写 fault、地址是 0x1000 的倍数」就查这条。
另外：用 0x100000000 以上的 GPU VA 放 ring 没有问题（我先怀疑过 32 位截断，实测不是）。

## M2b 实测（2026-10-28 16:40，`板子运行 mali16）

```
m2b: shader ready ready 0 present 0x50005; dispatching
m2b: dispatch finished: extract 320 of 320, shader wrote 0xc0ffee42 (wanted 0xc0ffee42), end marker 0xd0de0001, shader ready 0x50005, cs fault 0 fatal 0
m2b PASSED: a shader ran on the shader cores and wrote memory
```

命令流（23 条 + 1 条补齐）：`REQ_RESOURCE(compute)`、`SET_SB_ENTRY(3,0)`、SPD_0/TSD_0/SRT_0/FAU_0 各一条 `MOVE48`、`MOVE32` 给 GLOBAL_ATTRIBUTE_OFFSET / WG_SIZE（1×1×1 的 minus-one 编码 = 0）/ JOB_OFFSET×3 / JOB_SIZE×3（= 1）、
`RUN_COMPUTE(increment 1, axis Z)`（Mesa 对 1×1×1 的选择）、`WAIT(1<<3)`、`FLUSH_CACHE2(L2/LSC clean+inv, others inv, flush id 寄存器 = 0)`、`WAIT(1<<0)`，最后用 `STORE_MULTIPLE` 写一个「做完了」的标记。
着色器 4 条 Valhall 指令：`IADD_IMM r0 = 数据`、`IADD_IMM r2:r3 = 地址`、`STORE.i32.slot0.end @r0, ^r2`（源：`shader/store.asm`，驱动运行时把立即数补进去）。

**坑（花了 6 轮）**：`STORE` 的 `@` 寄存器是数据、`^` 那对是地址——我先写反了，结果着色器照样被取指、派发照样完成、没有任何 fault，内存里却什么都没变。
`mali=5` 保留了当时用来定位的诊断：M2b 失败时再派发一个 SPD 指向未映射地址的 job，AS1 应该立刻报「取指 translation fault」（`FAULTSTATUS 0x1c2`）。
被我怀疑过但不是原因的：缓存属性（NC / WB 两种页都试了）、`FLUSH_CACHE2` 和 `GPU_CMD` 冲缓存、标记和数据同一条缓存线、task axis（X 和 Z 都行）、ring 的 VA 高低。
描述符的位布局用 Mesa 的 `gen_pack.py` 生成 `v10_pack.h`，在 构建机 上编了个小 C 程序打出 SPD / TSD / WG_SIZE 的字，和手拼的完全一致，这条路可以复用（`-DPAN_ARCH=10 -DUTIL_ARCH_LITTLE_ENDIAN=1 -DUTIL_ARCH_BIG_ENDIAN=0 -I<mesa>/src -I<mesa>/src/panfrost -I<mesa>/include`）。

## M3a 实测（2026-10-28 18:20，`板子运行 m3a1，`mali=6`）

把 `m2()` 里写死的一条队列做成可复用的东西，**第一次上板就全过**：

- `Ctx`（一个地址空间，页表池可增长到 16 MB）、`Group`（一个 CSG 槽 + 最多 4 条队列，每条队列的环形缓冲和 group 的 syncobj 页都在 ctx 里的内核 VA 区）、
  `groupSubmit()`（**照 Panthor `prepare_job_instrs()` 逐条搭**：`MOV32 flush_id / FLUSH_CACHE2.clean_inv_all / MOV48 cs.start / MOV32 cs.size / WAIT(0) / CALL / MOV48 sync / MOV48 1 / WAIT(all) / SYNC_ADD64 / ERROR_BARRIER`，补到 16 条＝128 字节）、
  `groupWait()`（队列的 syncobj 计数 ≥ 作业序号即完成）。
- MCU 侧的每槽资源（四条队列的输入/输出页、suspend 缓冲）第一次用时分配、之后复用；group 销毁 = CSG_REQ 置 TERMINATE(0) + 全局门铃，等 ack，再把每个 CS 置 STOP。（**2026-11-05 起这些资源是 group 自己的、group 被逐出槽位用 SUSPEND 而不是 TERMINATE**，见文末「CSG 被逐出」一节。）
- 实测：一个 group 三条队列各跑一次 job ✓；40 个 job 过 4 KiB 环（128 B 一个，环绕）✓；20 个 job 排队只等最后一个栅栏 ✓；
  两个 context + 两个 group 同时在（**同一个 VA、不同的物理内存，互不串**）✓；销毁第一个，第二个照常；再建一个落在被释放的槽 0 / AS1 上 ✓。
- 顺带量到：`CS_FEATURES = 0x7085f`（96 个寄存器、8 个 scoreboard、compute+fragment+tiler）；`allowFragment` 填满着色器核也被固件接受。

## M3b 实测（2026-10-28 18:45，`板子运行 m3b2，`mali=7`）

内核里的 IOKit 服务 **`Mali`**（`OSDefineMetaClassAndStructors`，由 `mali_start()` 手工 `init/attach(IOResources)/start/registerService`，
用户态用 `IOServiceMatching("MaliService")` 找）和它的 user client **`MaliClient`**。接口在 `mali_uapi.h`（C，垫片同用），
就是 Panthor uAPI 拆成不带用户指针的 10 个 selector（`DEV_QUERY / VM_CREATE / VM_DESTROY / BO_CREATE / BO_DESTROY / VM_BIND / GROUP_CREATE / GROUP_DESTROY / GROUP_SUBMIT / FENCE_WAIT`），
全是 `IOConnectCallStructMethod`；内存用 `IOConnectMapMemory64(type = BO 句柄)`，type = `0x80000000 | group` 是那个 group 的 syncobj 页（只读）。垫片负责把 `DRM_IOCTL_PANTHOR_*` 里的数组摊平、一个元素一次调用。

- **BO 是非连续的**（`inTaskWithPhysicalMask`，<2^40，逐页收集物理地址，CPU 侧 WC）；`VM_BIND` 记一张「VA 区间 → BO」的表，BO 引用计数，**部分 unmap 会把记录拆成左右两段**；
  重叠映射返回 `kIOReturnNoSpace`，越界返回 `BadArgument`，unmap 空区间返回 `NotFound`。`UNCACHED` 标志选 NC 叶子，默认选 WB（Panthor 的语义；GPU 缓存靠每个 job 开头的 FLUSH_CACHE2）。
- 页表池改成可增长（256 KiB 一块，最多 63 块 + 首块，约 16 MB 表 ≈ 8 GB 稠密映射）。
- **进程退出清理**：`clientClose` / `clientDied` → `releaseAll()`：按 group → address space → BO 的顺序全部交还，句柄表和归属检查都是每个连接私有的（第二个连接拿第一个的句柄一律 `BadArgument`，也映射不了它的 BO）。
- 自测（内核里 `newUserClient(kernel_task,…)` 造两个连接，直接调 `externalMethod`）全过：DEV_QUERY 与寄存器一致；1 MiB 非连续 BO 的**最后一页**被 GPU 写对（散列表正确）；中间挖一个洞后两侧照常、洞的那一页重新映射到别处也写对；
  36 个 job 环绕；`VM_DESTROY` 在 group 还在用时返回 `Busy`；最后**不销毁任何东西直接关连接**，group、AS、BO 全部回收（`gBoLive` 回到 0）。
- `mali` 档位：6 = 只跑 M3a；7 = M3a + M3b 自测 + 注册服务；8 = 固件起来 + 注册服务、不跑测试（给用户态用）。

**用户态那一半还没验证**：`IOConnectMapMemory64` 把 BO 映射进真正的用户进程（不是 `kernel_task`）、沙盒是否允许 `iokit-open Mali`、一个自己的程序怎么在 AMFI 下跑起来
（trust cache 里要有它的 cdhash，或者放进 `/System` 里已有信任的位置）——这些要等第一个用户态测试程序。

## M3c 实测（2026-10-28 19:10，`板子运行 gpu2，`mali=8 evb10-exec=1`）

**第一个自己的用户态程序在板子上跑起来了，并且通过 IOKit 把作业交给了 GPU。** 做法（一整条链，每一环都是新东西）：

1. `MaliMetal 的 usertest/gpu_test.c`：iOS arm64 命令行程序（clang-20 + ld64.lld，`-Wl,-adhoc_codesign`；lld 的 CodeDirectory 页大小是 4 KiB，和 4K 内核天然对得上，`native-4k-signature-hash-cache` 的 16K 分支不会碰它）。
   `IOServiceGetMatchingService(IOServiceMatching("MaliService"))`（**匹配的是类名，不是注册表里的名字**；第一次跑就是因为写成 `"Mali"` 找了 130 s 没找到）→ `IOServiceOpen` → 10 个 selector → `IOConnectMapMemory64`。
2. **owner trust cache**：`uefi-evb10 的 tools/xnu-probe/make-owner-trust-bundle.py` 做一个 bundle = 一个 owner 模块（只有这个程序的 cdhash，版本 2 模块，条目按升序）+ 原版两个模块**一字节不改**。这是 UniXNU 自己给「设备主人加的代码」留的口子
   （`UniXNU/scripts/make-owner-trust-bundle.py` 给 Adreno Metal 插件用同样的办法）；payload 这一步只在 bundle 的 SHA-256 列在 `[payload] trust_cache_sha256` 里才收，`usertest/build.sh` 会把它钉进 `board.toml`（第二项，原版那项不动）。
   **一个 cdhash 只信一份二进制**：改了 `gpu_test.c` 就要重跑 `build.sh`，再重编内核（程序在内核里）、重打 payload（bundle 在 payload 里），`run-kernel.sh` 的 payload 步骤看到 `evb10-exec=1` 会自动换用 owner bundle。
3. 程序怎么进板子：`gpu_test_image.inc`（`build.sh` 生成，和 `mali_csffw.inc` 一个办法）编进 Mali 驱动；`evb10-exec=1` 时 `mali_userland_prepare()` 在 Data 卷挂好之后把它写到 `/private/var/evb10/gpu-test`（每次开机重写），
   `load_init_program_at_path()` 的第一次 exec 尝试换成这个路径（`mali_init_override()`，钩子在 `xnu-patches/userland-mounts/make-patches.py` 里）。**它当 PID 1，launchd 不起来**——只有一个进程，输出走 `/dev/console`（串口）。
   写不进去或 exec 失败，后面照旧试 `/sbin/launchd`；去掉 `evb10-exec=1` 就是原来的用户态。
4. 结果（用户进程、真 IOKit、真 WC 映射）：`DEV_QUERY` 与寄存器一致；1 MiB 非连续 BO 映射进用户进程；3 条队列 120 个作业（偶数号用 `FENCE_WAIT`，奇数号轮询映射进来的 syncobj 页）**0 个错，平均 52 µs，最坏 159 µs**；别人的句柄被拒；销毁全部成功。沙盒没有挡（PID 1 不带沙盒）。

顺带量到：**这个进程从 exec 到 `main` 要 69 s − 24 s = 45 s，几乎全是系统态时间**（`ps` 里 `sys 45.09`）；`shared_region: … vm_shared_region_start_address() failed` 两行在起点。这是 launchd 那条路径上也有的老问题（共享缓存映射 / 4K 哈希缓存），
不是这个程序的问题，但它让每一轮试验多等 45 s。

## M3d 实测（2026-10-28 20:50，`板子运行 gpu11，`mali=8 evb10-exec=1`）

**PanVK（Mesa 26.1-devel，arm64-apple-ios17.0 交叉编的 `libvulkan_panfrost.dylib`）在板子上跑完了一次 Vulkan 计算：**

```
panvk (Mali-G610 MC4) Mesa 26.1.0-devel
vk-test: Mali-G610 MC4, api 1.4.346, driver 0x6800063, vendor 0x13b5 device 0xa8670005
vk-test: vkCreateDevice ok / pipeline ok / vkQueueSubmit / vkWaitForFences -> 0
vk-test: compute result: 0 of 1024 words wrong     (shaders/square.comp: d[i] = i*i + 7, 16 个工作组 × 64)
vk-test: ALL OK (PanVK: instance, device, compute pipeline, dispatch, fence, readback)
```

整条链：`gpu-test`（PID 1，owner trust cache 里的 ad hoc 签名程序）做完内核 API 自测后 `fork()`，子进程 `dlopen` ICD → PanVK → `drm_darwin`（libdrm 垫片，`MaliMetal 的 darwin-drm/drm_darwin.c`，把 Panthor 的 ioctl
翻成 `MaliClient` 的 selector）→ IOKit → `Mali` → CSF 固件 → 着色器核。ICD 里的 Bifrost/Valhall 编译器（NIR → Valhall）也是在板子上跑的（管线编译这一步）。

**这一轮新东西：**

| 东西 | 在哪 | 要点 |
| --- | --- | --- |
| tiler heap | `Mali.cpp` `heapCreate/heapFree`，selector `HEAP_CREATE/HEAP_DESTROY` | 块按「新的在前」串起来（`header[0] = (上一块 & ~0xfff) \| (chunkSize >> 12)`）；句柄 = 槽号 + 1；**没有 OOM 处理（还没有中断）** |
| libdrm 垫片 | `MaliMetal 的 darwin-drm/drm_darwin.c` | `drm_darwin_open_render()` 返回 /dev/null 的 fd；`drmIoctl` 按 Panthor 的 ioctl 号分发；syncobj（二进制 + timeline）、`VM_BIND`（先等再绑再放信号）、`GROUP_SUBMIT`（`dsb sy` 后提交，给每个 queue 挂 fence）、BO 映射走 `IOConnectMapMemory64`（WC）、偏移 ≥ 1<<43 的是 flush-id 页（给一页零）。`DRM_DARWIN_TRACE=1` 每个 ioctl 打一行 |
| Mesa 补丁 | `MaliMetal 的 mesa-patches/0002-os-mman-darwin.patch`、`0003-panvk-apple-build-id.patch` | `os_mmap` 在 Darwin 改走垫片（MAP_FIXED 不支持）；Mach-O 的 LC_UUID 只有 16 字节，PanVK 要的 build-id 检查在 `__APPLE__` 放宽到 16 |
| 文件投递 | `usertest/build.sh` → `Mali/gpu_files.inc`；`mali_exec_prepare()` | 三个文件（`gpu-test`、`libtiny.dylib`、`libvulkan_panfrost.dylib` 16.5 MB）以 zlib 流编进内核，开机时写到 Data 卷 `/private/var/evb10/`（0755）。**内核的 zlib 没有默认分配器（NO_ZCFUNCS）：`uncompress()` 会跳到 0 崩溃，`mali_inflate()` 自己给 `zalloc/zfree`** |
| mmap 放行 | `xnu-patches/evb10-mmap-grant.patch` | exec 过 trust cache 的程序没问题，但 **Data 卷上任何文件 `mmap(PROT_EXEC)` 都是 EPERM**（dyld 报「file system sandbox blocked mmap()」；系统卷的文件没事）。`kern_mman.c` 里加钩子：路径在 `/private/var/evb10/` 且 `mali_exec_ready` 时放行 |
| 沙盒放行 | `MaliService` 的属性 `NativeSandboxGrantProcess`（逗号分隔进程名，默认 `gpu-test`，boot-arg `mali-grant=` 可改） | PID 1 能 `IOServiceOpen`，**fork 出来的子进程被平台沙盒拒（`0xe00002e2` kIOReturnNotPermitted）**。UniXNU 给 Adreno 做的 `native_sandbox_grant` 就认这个属性（`IOUserClient.cpp`），不用改内核。**M4 时 backboardd / 渲染服务进程的名字要加进这个列表** |
| CTR_EL0 | `xnu-patches/evb10-sctlr-uct.patch`（`SCTLR_EXTRA = SCTLR_UCT_ENABLED`） | Mesa 的 `util/cache_ops_aarch64.c` 在 EL0 读 `ctr_el0` 取缓存行大小；XNU 默认 `SCTLR_EL1.UCT=0` → **SIGILL**（`d53b0029` = `mrs x9, ctr_el0`）。Linux 也开这一位。A55/A76 缓存行都是 64 B，两种核读出来一样 |
| 崩溃定位 | `usertest/vk_test.c` 的 `crash()` | 装了 SIGILL/SEGV/BUS/TRAP/ABRT/FPE 处理函数：打 PC、指令字、LR、fp 链（镜像 + 偏移）。`libvulkan_panfrost.dylib` 的偏移用 `$MESA_BUILD/panvk.map`（`build-mesa-ios.sh` 的 `-Wl,-map`）查函数 |

**PanVK 眼里的这块 GPU**（`drm_darwin.c` 谎报/选择的几处）：驱动版本 `panthor 1.4`（没有 1.10 的 `SET_USER_MMIO_OFFSET`；flush-id 页偏移 1<<56 由垫片识别）；
`selected_coherency = NONE`；内存类型 0 = 仅设备本地、类型 1 = 主机可见 + coherent（实际是 WC，不是缓存；GPU 不一致，所以 CPU 写完要 `dsb sy` 才提交）；`user_va_range = 2^47`；
队列族 0 = 图形+计算+传输（2 个队列），族 1 = 传输；一个 queue = 一个 group、3 个 subqueue，创建时 `TILER_HEAP_CREATE`（2 MiB 块）并同步等初始化命令流。

**还没做（图形）**：一个 clear 渲染（FBD + `RUN_FRAGMENT`）；tiler OOM / 中断（GIC SPI 92/93/94）；`GROUP_GET_STATE` 与错误上报；超过 8 个 group 的换入换出；
`drm_darwin` 的 `drmSyncobjHandleToFD` 等返回 ENOSYS（WSI / 跨进程 fence 以后才要）。

## M4 实测（2026-10-29，`板子运行 gpu24…`gpu32`）：backboardd 用 Mali 合成，画面进了面板

**一句话**：launchd 模式下 backboardd 经 `AGXAccelerator` → `NativeMaliMetal.bundle` → metal2vulkan → PanVK → `Mali` 拿到 Metal 设备，QuartzCore 的帧在 Mali 上画，
`NativeLegacyDisplay` 收下每一次 swap（`swap ok N rej 0`，gpu27 起；之前是 `rej 2897`）。

链路（从上到下，每一环都在板子上验证过）：

| 环节 | 在哪 | 要点 |
| --- | --- | --- |
| 加速器注册 | `Mali.cpp` `publishAccelerator()`（`mali-metal=1`） | 运行时给 `IOAcceleratorES` 挂一个元类 `AGXAccelerator`（类大小必须等于原版基类），属性 `MetalPluginName=NativeMaliMetal`、`MetalPluginClassName=NativeMaliMetalDevice`、`NativeSandboxGrantProcess=<进程名,…>`（boot-arg `mali-plugin-grant=`） |
| Metal 发现 | 原版 `MTLCreateSystemDefaultDevice` | `IOServiceMatching("IOAcceleratorES")` → 读 `MetalPluginName` → 加载 `/System/Library/Extensions/NativeMaliMetal.bundle` → `[[NativeMaliMetalDevice alloc] initWithAcceleratorPort:]` |
| 插件 | `metal-plugin/`（`build-plugin.sh`） | `NativeMaliMetalPlugin.mm`（入口、`mlog` 经 Mali 的 `MALI_LOG` selector 打到内核串口、状态线程）+ `frontend/NativeBridgeMetalFrontend.mm`（Metal 协议 → `nmb_*`）+ Rust 桥 `libnative_metal_bridge.a`（metal2vulkan：Metal → Vulkan，AIR → SPIR-V）+ PanVK ICD（`libvulkan_panfrost.dylib`）放在同一个 bundle 里 |
| 文件投递 | `make-addon.py`（UniXNU `make-gpu-addon.py` 的格式 `NGPUADD2`）→ `run-kernel.sh` 的 payload 步骤（`--gpu-addon`）→ `Payloads/kernel.macho.gpuaddon` | DarwinBoot（`uefi-evb10 的 DarwinBootPkg/` 是 UniXNU 新版的覆盖拷贝，`OVERLAY.txt`）校验头、把 phys/size 填进 ADT `/chosen/memory-map/GpuAddon`；内核 `mali_gpu_addon_prepare()` 把文件写到 Data 卷 `/private/var/ngpu/addons/`，再用 bindfs 只读并集挂在 `/System/Library/Extensions` 上（封印的系统卷不动）。boot-arg `evb10-gpu-addon=1` |
| 信任 | `usertest/build.sh` | 每次插件/ICD 变了都要跑：owner trust cache（cdhash）重做，SHA-256 钉进 `board.toml` 的 `trust_cache_sha256`（第二项）。一个 cdhash 只信一份二进制 |
| 沙盒 | `NativeSandboxGrantProcess` | 名字按进程名前 16 字符比；`mmap(PROT_EXEC)` 放行包含 bundle 路径（`evb10-mmap-grant.patch`） |

### 这一轮踩到的几个坑（症状 → 原因 → 改法）

1. **swap 全被拒（`reason 4`，surface `1072x1904 format 62336138 planes 2`）**：`'b3a8'` = `kCVPixelFormatType_30RGBLE_8A_BiPlanar`（10 位广色域 + 8 位 alpha 两个平面，iPad7,1 的 P3 屏默认走这个，不是压缩）；
   `NativeLegacyDisplay::validateSwap` 只收 1 平面的 `BGRA`/`&BGA`。QuartzCore（17.7.11，缓存地址见 `uefi-evb10 的 tools/xnu-probe/symbolize/README.md` 的 `qcfind.py`/`dataref.py` 一节）：
   `IOMFBDisplay::framebuffer_pixel_format()` 在显示器标志位 3（`+0x238`）置位时返回 `b3a8`；位 3 由 `IOMFBDisplay` 构造函数按全局字节 `0x1f491ae10`（`___CADeviceUseCIF10`：MobileGestalt `Aixt/MEN2O2B7f+8m4TxUA` 的答案，再被偏好 `CAEnableDeepFramebuffer` 覆盖）设。
   **偏好在 backboardd 自己的域（`backboardd.plist`）里没被读到（`CFPreferencesCopyAppValue` 看不到，pref bits 0xc），写进全局域 `.GlobalPreferences.plist` 才生效（0xf，deep 0，显示器标志 0）**——
   原因多半是沙盒只放行 backboardd 读自己列出的域，全局域总能读。UniXNU 的 `add-gpu-plugin-data.py --publish` 也是两个都写。
   **做法**：内核每次开机在 `mali_gpu_addon_prepare()` 里写 `backboardd.plist`（mobile + root）和两个家目录的 `.GlobalPreferences.plist`（`AppleLanguages`、`AppleLocale` + 三个 `CAEnable…=false`；已有 `CAEnableDeepFramebuffer` 键的文件不动），`evb10-gpu-prefs=0` 可关。
   首次开机就生效（进程启动前文件已在盘上）。插件另外在前 3 个 10 s 节拍里（`clearDeepFramebuffer`）清全局字节和显示器标志位 3 兜底，并在串口打 `qc open/tick: … deep N … display flags … pref bits …` 看实际状态。
2. **`no-copy buffer refused … Unsupported(HostMemory, NotRepresentable)`**（QuartzCore 用 `newBufferWithBytesNoCopy:` 包它 mmap 的只读文件页，如 metallib）：`IOMemoryDescriptor::prepare()` 以写方向装订，只读映射返回 `KERN_PROTECTION_FAILURE`（2）。
   改在 `Mali.cpp` `boCreateHost()`：先试 `kIODirectionInOut`，失败退 `kIODirectionOut`，该 BO 标 `readOnly`，`ctxMapBo` 映射时加 `kPteRdOnly`。拒绝的原因现在都会打 `mali: host bo refused: …`。
3. **PanVK 缺两个能力**（Mesa 补丁 0005/0006）：`VkExternalBufferProperties` 对 `HOST_ALLOCATION` 要报 importable（metal2vulkan 的 no-copy 自测用到）；
   `VK_EXT_rasterization_order_attachment_access`（QuartzCore 的 `luma_boost_lph` 管线要 FramebufferFetch，没有它 backboardd 在 "Metal failed to build render pipeline" SIGABRT）。
4. **补丁没生效**：`xnu-patches/userland-mounts/evb10-userland-mounts.c.inc` 改了之后必须先跑 `make-patches.py` 再 `build-kernel.sh`——构建**不会**重新生成 `.patch`（内核里没有 `evb10: addon` 日志就是这个）。
5. **FV 要在干净环境里编**：持久 shell 的 PATH 被 mesa venv 和 xnu-unixnu-linux 污染，`env -i … source env.sh` 再 `uefi-evb10 的 build.sh -p evb10 -b DEBUG`，然后 `uefi-evb10 的 tools/uefi-probe/build.sh` 重做 `probe.img`。
6. **别重叠跑板**：`ps aux | grep -c "[r]un-kernel"` 为 0 再起新一轮（gpu15/gpu16 重叠把 板子宿主机 上的脚本互相覆盖）。

### 观测与工具

- 插件状态（每 10 s，`mali: user: status: …`）：`c a/b` 提交/失败，`d` 完成，`p` 管线数，`x` 问题数，`R` 渲染 pass，`D` draw，`F` 全屏 pass，`M` 最大 pass（Kpx），`C` dispatch，`B` blit；
  `vkpipe n=… failed=… total=…ms`（`METAL2VULKAN_LOG_PIPELINES=1` 才有数）。前端的 `native-metal: …` 问题行（160 字符一段）转成 `fe: …`。
- 内核每 5 s：`evb10: display t+Ns: swap ok N rej M (reason R) …`；`swap rejected, surface …` 打被拒 surface 的格式/平面/stride。
- `mali: connection opened by <进程名>`：谁打开了 Mali 服务（前 64 次）。
- 崩溃现场翻译：`uefi-evb10 的 tools/xnu-probe/symbolize/dsc.py`（`--slide` 用同一行的 slide）。

### 还没做 / 已知问题

- QuartzCore 的 `default.metallib` 二进制归档读不了（`binary archive: InvalidFile`，非致命）；5 个设备选择子无答案（`maxVertexAmplificationCount` 等，非致命）。
- 画面更新频率低只是因为锁屏是静态的（时钟一分钟一变）；动起来的流畅度、撕裂、tiler OOM/中断（轮询）、GPU 频率（198 MHz）都还没调。
- 锁屏壁纸由 AegirPoster / KaleidoscopePoster 这些 poster 扩展进程画，它们自己要 Metal 设备（见下面「客户端进程」）。

## 撕裂：先让扇出等 GPU 做完（2026-10-29，`板子运行 tear1、`up1`…`up4`）

**原因（实测）**：合成器在把一个面（surface）交给 `swap_submit` 的时候，画它的作业只是「排上了队」，不是「做完了」。零拷贝扫描（`evb10-vop-direct=1`）一收到 swap 就把
VOP 的窗口地址翻到这个面——面板扫到画了一半的面。`up3` 里 133 个 swap 有 108 个（81 %）到达时 backboardd 的 GPU 作业还没做完。
**不是**Mali L2 里还留着脏行：`evb10-vop-fence-check=1`（翻页前取 512 个字、让 GPU 清 L2/LSC、再读）在 `tear1` 里 39 次翻页 19968 个样本，**0 个变化**——所以
`mali-postflush`（作业末尾 FLUSH_CACHE2 清 L2）默认关，留着当 A/B 开关。

**做法**（内核，三处）：
- `Mali.cpp`：`Group.serial`（组号不重复，组被销毁又重建不会认错）；`mali_fence_take()`（每个 backboardd 的 group 的每个 queue：还没到 `q[q].seqno` 的记下
  `{serial, seqno, 组号<<8|queue}`，最多 24 条，更多就记成「全空闲」）；`mali_fence_done()`；`mali_fence_describe()`（放弃时打前 12 次：哪个进程的哪个 group 做到第几个作业）。
  **只数 backboardd 的 group**——海报扩展或 app 自己卡住的作业不该拖住整屏。全程 `IOLockTryLock(gLock)`，忙就返回 -1 稍后再试。
- `NativeLegacyDisplay.cpp`：`validateSwap()`（swap 到达，客户端线程）里拍一次快照（`fenceNote`，每个 request 一份，8 份环）——**快照必须在到达时拍**：第一版在第一次 `kickOnce()`
  时才拍，结果把 swap 到达之后才提交、而且在等这个 swap 自己 present 的下一帧作业也算进来，等不完（`up1`/`up2`：一半以上的翻页「放弃」）。`kickOnce()` 在 `flip()` 前
  `fenceClear()`：快照里的作业都到了才翻，没到就 `fenceTimer`（300 µs）再来；250 ms 还没到才放弃（防 GPU 挂死）。
- 开关：`evb10-vop-fence=1`（只在 `evb10-vop-direct=1` 下有意义）、`evb10-vop-fence-check=1`（只做上面那个 L2 检查）、`mali-postflush=1`。
  `evb10: display … fence:` 行：`N of M swaps arrived with the render server's GPU work not done; … flips waited for the GPU (mean, longest, gave up after 250 ms)`。

**代价（要说清楚）**：撕裂没有了，但「帧率高」之前有一部分是**没画完的帧**。`up3`（`evb10-touch-demo=150` 滚动）里等 GPU 平均 100 ms、最长 239 ms，滚动时真实帧率约 8–10 fps；
采样时 GPU 99 % 忙，一帧的作业连续 100 ms 左右（`mali: activity over 200 ms: busy 99.x %, burst … longest 110 ms`）。**瓶颈是 GPU 在滚动场景里的耗时**，不是 CPU，也不是翻页。
静态/缓动场景里（`tear1`、等待均值 7–14 ms）依然一帧不落。想要 60 fps 要把一帧的 GPU 时间从约 100 ms 压到 16 ms 以内——下一步得量是哪些 pass（模糊/材质背景）、哪个管线、有没有 AFBC/平铺问题。

## 2 MiB 块映射 + 物理内存池：TLB 假设不成立（2026-10-29，`板子运行 blk1…`blk4`）

**假设**：那个固定 28 ms 的通道是 GPU 页表遍历（TLB）造成的 → 把大缓冲区映成 2 MiB 块应该变快。

**做了什么**（`Mali 仓库/Mali.cpp`，都已在内核里）：
- 页表支持 2 MiB 块：`ptMapBlock`、`blockSplit`、`vmUnmap` 处理块、`ptMap` 遇到块返回 false；`ctxMapBo` 在 VA 和 PA 都 2 MiB 对齐且 512 页连续时映块。
- 物理内存碎片：开机约 115 s 之后连一个 2 MiB 对齐的连续段都分不到（`blk2`：包括 91 MiB 壁纸在内全部失败）；开机 23 s 时也分不到 128 MiB 连续（`blk3`：只拿到 64 MiB）。
  所以驱动在 M0 结束时（约 23 s）**预留一个池**：最多 512 MiB，由单独的 2 MiB 对齐连续颗粒组成（`boPoolInit`，`blk4`：256 个颗粒全拿到）。
  用户（`owner != null`）的 ≥ 2 MiB 缓冲区从池里取任意空闲颗粒，CPU 侧用 `IOMemoryDescriptor::withAddressRanges` 映成一整段，GPU 侧每个颗粒一个块。
  池颗粒 `pmap_set_cache_attributes(...,0x6)` 一次设好，释放时不用还原。内核内部缓冲区（堆、上下文）仍走原来的路径。
- 新启动参数：`mali-blocks=0`（关块映射）、`mali-contig=0`（关连续尝试和池）、`mali-pool=MiB`（池大小，默认 512）。
- 状态行 `mali: client mappings so far` 现在带块数、池占用（峰值）、池命中/未命中。

**结果（`blk4`，同样的 touch-demo 负载）**：功能上正确——155 个 2 MiB 块（310 MiB）、池峰值 153/256 颗粒、27 个缓冲区取自池、0 次未命中、无 panic、无 GPU 故障；
**帧率没有变**：手势期间每 5 s 交给显示的帧数 15–31（平均间隔 16–64 ms），和 `perf7`（18–31）一样。

**结论**：TLB / 页表遍历**不是**那个 28 ms 通道的瓶颈。池和块映射保留在内核里（对的、无害，默认开），但不要指望它提速。

**下一步候选（当时写的，2026-10-30 已全部查过，结论在 MaliMetal 仓库的 `docs/notes.zh-CN.md`「帧率」一节）**：
1. 那个通道确实是壁纸的绘制，但它慢在 **m2v 逐位仿 YCbCr 线性滤波的片元着色器（约 570 条指令一次采样）**，是算力：`mali-ycbcr-filter=device` 之后 750 MHz 下 22.5 → 7.9 ms。
2. GPU 内存通路不是瓶颈：重通道的时间严格跟 GPU 时钟走（198 / 594 / 750 MHz：84.9 / 28.4 / 22.5 ms；「198 MHz 同样帧时间」那条是转换 dispatch 占着一帧时量的，不成立）。
   `mali-pll=1 mali-div=1`（本仓库 `aa7028f`）把 GPU 抬到 750 MHz（UEFI 设的 0.75 V，不升压）；调频本身仍没做。
3. 减少采样的字节数：把影子图存成 8 位的实验没有改变帧时间（带宽不是瓶颈），已删。

## GPU 时钟：`mali-pll`、`mali-npll`（2026-10-30，`板子运行 gp2`、`soak1`）

`clk_gpu_src`（CLKSEL_CON(158)）的源：`mali-pll=0` GPLL（1188 MHz）、`1` CPLL（1500）、`2` AUPLL（786）、`3` NPLL（固件设的 850），`mali-div=N` 是分频减一。`mali-pll=3 mali-div=0` = 850 MHz。
**`mali-npll=MHz`（400–1100）**：切到 NPLL 之前把它改成别的频率。只改乘数 M（CON0 [9:0]），P、S 保持固件的值：`rate = 24 MHz × M / P / 2^S`，固件是 M 425、P 3、S 2 = 850 MHz（VCO 3.4 GHz），
1000 MHz = M 500（VCO 4 GHz，PLL 的范围 2.25–4.5 GHz，越界的不做）。写完等锁定位（CON6[15]）最多 5 ms，没锁住就恢复原来的 M。切换时 GPU 还在别的源上（驱动刚启动），NPLL 在这个系统里没有别的用户。
板上（`gp2`）：`mali: mali-npll=1000: m 425 -> 500, npll now 1000000 kHz (locked 1, con 0x1f4 0x83)`，GPU 周期计数器探针读到 `1000 MHz`（21008038 个 GPU 周期 / 21008 µs）。
**电压**：UEFI 把 `vdd_gpu_s0` 设成 0.85 V（`uefi-evb10` 的 `RockchipPlatformLib.c`，原来 0.75 V）；厂商的工作点表：800 MHz 0.75 V、900 MHz 0.80 V、1 GHz 0.85 V。驱动不调频，所以这根轨按「启动参数可能选的最高频率」一次设好。
效果：命令缓冲的 GPU 时间中位数 9.1 → 7.9 ms（850 → 1000 MHz），提交→完成 p50 13.7 → 12.7 ms、p90 17.3 → 15.6 ms，超过一帧能给的 19.3 ms 的重命令缓冲 3.5 → 1.6 %（MaliMetal 仓库 `docs/notes.zh-CN.md`「帧率（二）」）。

## 风险（按大小）

1. ~~PanVK 够不够 metal2vulkan 的 Vulkan 特性~~——纸面上够（M3-1），余下是 framebuffer-fetch 策略和 PanVK 真跑起来之后的编译耗时。
2. ~~PanVK 在 iOS 上的构建~~——已打通（M3-3）。剩下的未知项是**运行时**：dylib 在 iOS 用户态能不能 `vkCreateInstance`、
   Mesa 里还有没有别的 Linux 假设（`memfd`、`/proc`、`/sys` 读取、`dup` 的 fd 语义）只在运行时才暴露。
3. Panthor 依赖的 Linux 设施（DRM/GEM/dma_fence/io-pgtable/调度器）在 XNU 上都要自己写。
4. 电压/时钟：驱动自己的默认是 198 MHz（没有 `mali-pll` 参数时），`UniXNU-evb10` 的 `board.toml` 默认是 750 MHz（UEFI 的 0.75 V）；1 GHz 要 0.85 V 的固件（`GPU_RAIL_UV`）；SCMI 调频没做。
5. 对 G610 的缓存一致性：先按「非一致」处理（CPU 侧共享缓冲用 Write-Combine）。

## CSG 被逐出：用 SUSPEND，不要 TERMINATE（2026-11-05，Orange Pi 5 上定位并修复）

**现象**（EVB10 和 Orange Pi 5 都有，开机后约 80 s、290 s、400 s 不等）：一行 `MMU page fault in ASn (a client's): status 0x7cc002c1 address 0`，接着
`group N did NOT terminate (req 0 ack 0x30000001)`、`STALL group …`，之后所有提交都不再完成：backboardd 空转，画面不再换帧（锁屏的时钟停在 4:05）、指针不动。
`status 0x7cc002c1` = 转换错误（第 1 级）、读访问；地址 0。

**怎么找到的**（OPi5 板上，`gpu1`，加了 `jobDump()` 之后）：
- 故障前最后几行是 `AS3 enabled …` 和 `group 1 in slot 4: address space 3`：**backboardd 的 group 1 刚被重新放进一个槽位**（它第一次起是在槽位 0）。
  **全部 10 份带 `MMU page fault` 的串口日志（OPi5 七份、EVB10 三份）都是这样：第一次故障在 0.5 s 之内紧跟在一个「已经起过的 group」的第二次 `in slot` 之后**
  （OPi5 的 vop1 78.5 s、dvfsusb3 81.8 s、sb16 81.2 s、gpu1 402.5 s；EVB10 的 tb1 294.9 s、tg1 291.3 s、wifi11 286.1 s；没有一份例外）。
- 卡住的作业的环（`JOB … ring` 行，是内核自己搭的 16 条指令）正常：`MOV48 addr,0xff190000 / MOV32 size,0xd00 / WAIT / CALL`，固件的 extract 停在 CALL 那一条（`+0x28`）。
  出错的访问地址是 0，不是用户流的地址：错在用户命令流**里面**读了一个从寄存器取的指针。
- 逐出走的是 `groupEvictIdle()` → `groupStop()`：CSG_REQ 置 **TERMINATE**，再放回槽位时 **START**，suspend 缓冲和队列接口页都清零。**队列的寄存器全被清零。**
  Mesa 的 PanVK 在一条队列的第一个作业里一次性设好寄存器（子队列上下文、scoreboard、tiler 指针等），之后的命令流只用不设；Panthor 的逐出是 SUSPEND，固件把寄存器存进
  group 自己的 suspend 缓冲，放回时 RESUME，用户态才能这么写。清零之后第一个用到那些寄存器的 load 读的就是地址 0。
- 故障之后固件再也不响应 TERMINATE（`did NOT terminate`），占着的槽位让其他 group 也起不来，所以整个 GPU 停了。
  （文件里别处「STALL 在 19 / 48 份旧日志里出现」有相当一部分不是这个：旧的 `gpuStallScan()` 对一直在提交的 group 也会报，现在只报「两次扫描之间一个作业都没完成」的。）

**修法**（`Mali.cpp`）：
- `HwRes`：队列的输入/输出页和 suspend / protm suspend 缓冲不再属于槽位，而属于 group（Panthor 的 `queue->iface`、`group->suspend_buf`），从池里取、group 销毁后还回去（MCU 的 VA 只发不收，所以池只增长到同时存在的 group 数）。
- `groupStop(g, suspend)`：被逐出时请求 `CSG_STATE_SUSPEND`（2），等 ack；`g.suspended = true`。固件不应答（或已变成 TERMINATE）才退回 TERMINATE，并打 `did NOT suspend`。
- `groupStart()`：`g.suspended` 时不清页、不重设 extract，请求 `CSG_STATE_RESUME`（3）；CS 照旧发 START（Panthor 的 `cs_slot_prog_locked` 也是）。
- 销毁仍是 TERMINATE。只逐出空闲的 group（`groupIdle()`），这一点没变；忙的 group 不逐出，所以这次没用到「在作业中途挂起」。

**板上结果**（OPi5，`gpu5`，不动鼠标，没有鼠标演示）：开机后 308–318 s 那一波海报扩展起了十几个 group，backboardd 的 group 1 在 309.039 s 被放回槽位 4（地址空间 3）——
就是旧驱动里 402.5 s 出故障的那一步——group 11、12、14 也换了槽位；**没有 MMU 故障、没有 `did NOT suspend`、没有 `STALL`、backboardd 没有退出**，日志到 855 s（还在跑）；时钟一直在走（4:09 对应约 570 s，换帧计数继续增加）。
旧驱动在同样的开机里（`gpu1`）402.5 s 出故障后再也没有换帧。

**没验证**：EVB10（共用这个文件，这次没在 EVB10 上跑）；长时间（只看了 14 分钟，一个开机）；忙 group 的逐出；寄存器清零是原因这一点没有直接读寄存器验证（证据是上面的现象和改后同一事件不再出错）。
**新现象，没查**：那一波之后，锁屏时钟的数字（backboardd 合成的、SpringBoard 用 CPU 画的文字层）带横向的虚线噪点（`gpu5` 的 4:08、4:09），波之前的 4:03 基本干净（有一条细虚线）。
那一波里 BO 池几乎用满（`pool 205 of 256 granules in use (peak 253)`，之后 `pool 249 … peak 254`，**26 个缓冲池里放不下**、走了池外的页），可能相关；也可能是文字位图的缓存行没清到内存。都只是猜测，没验证。

## 工具

- `uefi-evb10 的 tools/uboot-enter.sh`（在 板子宿主机 上跑）：把板子带到 U-Boot `=> ` 并停住；`uefi-evb10 的 tools/ub.sh 'md.l …' 'mw.l …'`（在 构建机 上跑）发命令。
  复位用 `uefi-evb10 的 tools/uefi-probe/hw-reset.sh reset`（一次一发）。秒级试验寄存器序列，比改内核快两个数量级。
- 不要在没上电的电源域上读 GPU 寄存器（可能挂总线）；永远先看 `0xfd8d8290` bit 1。

