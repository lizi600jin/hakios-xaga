# HakiOS 全部仓库调研

> 调研时间：2026-11-14 22:20–22:35（Asia/Shanghai）
> 调研对象：GitHub 用户 `oosexgang` 的全部仓库 + 组织内所有 MTK 相关分支/移植仓 + 本地两份树
> 方法：`api.github.com` 列举 + `git clone --depth 1`（blobless 稀疏）到 `/tmp/` + 逐文件实读
> 纪律：只写读到的；找不到就写「未找到」；每条给文件路径 + 行号或 URL

本地引用路径约定：

| 简称 | 实际路径 | 来源 |
|---|---|---|
| `[MTK树]` | `/home/lizi/IOS/hakios-mtk/`（分支 `mtk`，HEAD `7df8a9d`） | 本地工作树，`origin` = `oosexgang/hackios-xnu-kernel` |
| `[DEV6768]` | `/tmp/hakios-mt6768/`（`git clone --depth 1`） | `https://github.com/oosexgang/hakios-mt6768` |
| `[XNUK]` | `/tmp/hakios-xnu-kernel/`（`main`，稀疏 `drivers mtk docs configs tools hakios-metal`） | `https://github.com/oosexgang/hakios-xnu-kernel` |
| `[XAGADOC]` | `/home/lizi/IOS/repo/hakios-mt6895/`（`lizi600jin/hakios-xaga`） | 我们自己的文档镜像 |

---

## 0. 仓库清单（全名 / 用途 / 活跃度 / 分支）

### 0.1 `oosexgang` 名下全部仓库（7 个，`api.github.com/users/oosexgang/repos?per_page=100`，2026-11-14 取，count=7）

| 全名 | 默认分支 | 全部分支 | 大小 | 最后 push | 用途（据 description / README 实读） |
|---|---|---|---|---|---|
| `oosexgang/hakios-xnu-kernel` | `main` | **`main` / `mtk` / `legacy-hackios`** | 203 805 KB | 2026-11-10T17:17Z | **唯一的公共内核树**。`main` = Qualcomm 线（POCO X5 Pro / redwood，SC7280/SM7325）；`mtk` = MediaTek 线（MT6768 已跑通 + MT6895 层） |
| `oosexgang/hakios-mt6768` | `main` | `main` | 431 KB | 2026-11-10T09:59Z | **设备仓**：Infinix Hot 20 (X6826B) / MT6768 / Mali-G52 MC2 / eMMC。**已成功**（README.md:15-29） |
| `oosexgang/hakios-sm7325` | `main` | `mac` / `main` / `u-boot` | 3 013 KB | 2026-10-27T23:41Z | 设备仓：POCO X5 Pro 5G (redwood)，SC7280 |
| `oosexgang/hakios-uefi` | `main` | `mac` / `main` / `u-boot` | 2 007 KB | 2026-10-27T23:42Z | HakiOS UEFI（Mu-Silicium 派生，Qualcomm 线专用） |
| `oosexgang/hakios-metal` | `main` | `main` | 3 616 KB | 2026-11-05T13:09Z | **已归档迁移**：README 首行写 "Moved. This driver is now developed in the kernel's repository … `hakios-xnu-kernel/hakios-metal` (since kernel 2.3.3)"。内容是 **Adreno** 的 Metal 驱动（不是 Mali） |
| `oosexgang/hackios-web` | `main` | `main` / `redesign-guide` | 16 974 KB | 2026-11-05T15:02Z | 文档站（TypeScript） |
| `oosexgang/hackios-xnu-userland` | `main` | `main` | 0 KB | 2026-10-25T16:44Z | 空仓（size 0，无语言） |

- **没有** `hakios-mt6895` 这个仓库：`api.github.com/repos/oosexgang/hakios-mt6895` 不存在；搜索 `hakios+mt6895` 只命中 `lizi600jin/hakios-xaga`（我们自己的）。见 §6。
- `oosexgang/hackios-xnu-kernel` 返回 **301**（重定向），实际存在的就是 `hakios-xnu-kernel`；本地 `[MTK树]` 的 `origin` 仍写成旧名 `hackios-xnu-kernel.git`（`git remote -v` 实读），GitHub 侧靠 301 兼容。
- `hakios`（无后缀）是**私有仓**，`CHANGELOG.md:33` 明写：「`oosexgang/hakios` is private and cloned for nobody」。

### 0.2 组织外的相关仓（同一套代码的 fork / 兄弟移植，用来交叉验证，本次只取索引级信息）

| 全名 | 最后 push | 说明 |
|---|---|---|
| `iSciFiLite/hakios-mt6768-pova` | 2026-11-12 | **MT6768 的 UEFI+XNU 生态分支**，把 `hakios/mtk/mt6768/drivers/gpu/{hakios.mali.c,hakios.mali.cpp}` 原样带过去了（用 GitHub tree API 逐条核对过文件大小：42068 / 21293 字节）。另带 `mtk/mt6785/drivers/gpu/` 一套 |
| `iSciFiLite/hakios-mt6785` | 2026-11-11 | MT6785（Realme 7 SALAA / Realme 6）profiles |
| `iSciFiLite/hakios-rm6785-nemo` | 2026-11-11 | 同上，另一台机 |
| `Luminous418/hakios-a32` | 2026-11-01 | 三星 A32 4G，**MT6769T + Mali-G52 MC2**（与 MT6768 同族，另一个 MTK 端口） |
| `lizi600jin/hakios-xaga` | 2026-11-14 | **我们自己的仓**（MT6895 的文档镜像 + tree/） |
| `kemi579/hakios-amber` | 2026-11-07 | 小米 11T（amber，**MT6893** Dimensity 1200），同为 MTK 高端 SoC |
| `manysuq/hakios-xnu-kernel-dipper`、`Luoxueowo/hakios-xnu-kernel`、`zhouzhoublm/hakios-xnu-kernel`、`xm6661885/hakios-xnu-kernel-umi-fixes` | 2026-11 | 内核树 fork（非 MTK），未逐个读 |
| `superboyyy/hakios-arm64e-dada` | 2026-10-31 | 骁龙 8 Elite 上的 arm64e / iOS 26 线 |

> 搜索 `hakios+mt6768` → 2 条（oosexgang 原仓 + iSciFiLite 的 pova）；`hakios+mtk` → 0 条；`hakios+mt6895` → 1 条（我们的）；`hakios+helio` → 2 条；`hakios+mali` → 1 条（A32）。`hakios+headset` / `hakios+accdet` / `hakios+3.5mm` 均 **0 条**（2026-11-14）。

### 0.3 分支层面的关键事实

`hakios-xnu-kernel` 的 `mtk` 分支 = **MT6768 的完整可跑实现**（loader + 6 个驱动 + 工具），`mt6895/` 层在这条分支里**也存在**（`[MTK树] mtk/mt6895/`）。但：

- `git ls-tree -r --name-only origin/mtk -- mtk/mt6895` → **空**。也就是说 **`mt6895` 层只在本地，没有推到 `origin/mtk`**。
- 本地 `[MTK树]` 相对 `origin/mtk`（`db08f63`）多出 2 个提交（`ef54fa0` 耳机驱动、`7df8a9d` 更正 0x26a9），且 `mtk/mt6895/` 整个目录在本地是 **staged-but-uncommitted（`git status` 全是 ` A ` / `?? `）**：
  ```
  A mtk/mt6895/drivers/gpu/conf
  A mtk/mt6895/drivers/gpu/hakios.mali610.c        (+5209 行)
  A mtk/mt6895/drivers/input/{conf,hakios.mt6895_keys.c,hakios.nvt.c}
  A mtk/mt6895/drivers/usb/{conf,hakios.mt6895_usb.c}
  A mtk/mt6895/loader/board.h                      (+910 行)
  A mtk/mt6895/soc.conf
  ?? mtk/mt6895/configs/
  ```
  结论：**MT6895 的工作没有一处进入公开仓库**，`origin/mtk` 上只有 MT6768。

---

## 1. hakios-mt6768（已成功的 MTK 移植）

设备仓 `[DEV6768]` 只有 80 个 git 文件、480 KB 内核代码；**真正的实现驱动在 `[MTK树] mtk/mt6768/drivers/`**（`AGENTS.md:27-28` 明写两者是同一份东西的两个名字：`redwood_*` ↔ `hakios.*`）。

规模（`wc -l` 实读）：

| 文件 | 行数 | 作用 |
|---|---|---|
| `drivers/gpu/hakios.mali.c` | 1146 | G52 上电/时钟/MMU/作业队列 |
| `drivers/gpu/hakios.mali.cpp` | 650 | `HakiosMali` 服务：panfrost DRM 接口的 IOKit 实现 |
| `drivers/wifi/hakios.mtk_connsys.c` | 776 | CONNSYS：电源/固件/BTIF/STP/WMT |
| `drivers/input/hakios.mtk_scp.c` | 539 | SCP 传感器核 |
| `drivers/input/hakios.tcm.c` | 501 | TD4160 触摸（SPI2 + TCM 协议） |
| `drivers/storage/hakios.mtk_emmc.c` | 478 | MSDC0 只读块设备 + root |
| `drivers/input/hakios.mtk_keys.c` | 363 | 键盘 + PMIC 电源键 + 大核/GPU 时钟 |

### 1.1 GPU：panfrost 如何集成进 XNU

**一句话**：内核里写一个 **仿 panfrost DRM ioctl 的 IOKit user client**，用户态的 Mesa panfrost 通过一个 ~226 行的 `libdrm` shim 把 ioctl 直接翻译成 `IOConnectCallStructMethod`，中间**没有 DRM 子系统、没有内核态 Mesa**。

#### (a) 内核侧两半的分工

`[MTK树] mtk/mt6768/drivers/gpu/hakios.mali.c:1-22` 头注释把 6 步写死：

```
1  the "mfg_sel" clock mux in topckgen, to the main PLL's third (364 MHz)
2  the four power domains in the SPM, MFG_ASYNC, MFG, MFG_CORE0 and _CORE1 ...
3  the block's own clock gate (BG3D in mfgcfg)
4  the GPU: which one it is, a soft reset, and its L2, tiler and two shader cores powered
5  one job ... through address space 0 in identity mode
6  the same job again through the GPU's own page tables
```

- 上电/时钟/复位：`hakios.mali.c:512-580`（`mali_up()`）。
  - 时钟：`wr(topckgen, 0x58, 3u | (1u << 7)); wr(topckgen, 0x54, 2u)` → `mfg_sel = syspll_d3 = 364 MHz`（`:522-523`）。
  - 四个域：`domains[4]` 表在 `:271-280`（MFG_ASYNC `0x334`/bit14、MFG `0x338`/bit11、MFG_CORE0 `0x34c`/bit12、MFG_CORE1 `0x310`/bit13）；`domain_on()` 在 `:282-300`，顺序是 `PWR_ON → PWR_ON_2ND → 等 PWR_STATUS/PWR_STATUS_2ND → 清 CLK_DIS → 清 ISO → 置 RST_B → 清 SRAM_PDN 等 ACK`。
  - 总线保护：MFG 上电后 `wr(infracfg, 0x2a4, (1<<21)|(1<<22))` 再 `wr(infracfg, 0x2a4, 1<<25)`（`:531-532`）。
  - 块内门控：`wr(mfgcfg, 0x008, 1)`（BG3D，`:540`）。
  - 软复位：`wr(mali, GPU_CMD, 1)` + 等 `IRQ_RESET_COMPLETED`（`:550-554`）。
- 作业与 MMU：`hakios.mali.c:346-510`。
  - `run_write_job()`（`:353-384`）手工拼一个 Bifrost job header：`job[4] = (2<<1)|1|(1<<16)`（write value / 64-bit ptr / job index 1）、`job[8..9]` 目标 GPU 地址、`job[10]=6`（32-bit immediate）、`job[12]=0xc0ffee01`，然后 `JS1+JS_HEAD_NEXT_LO`…`JS_COMMAND_NEXT = 1`，最后回读目标字确认。
  - **MMU**：自己写三张 AArch64 stage-1 表（`pt_page/pt_below/map_page`，`:426-477`），35 位地址 → 3 级索引 `bits34..30 / 29..21 / 20..12`（注释 `:401-407`）；PTE 属性 `PTE_AF|PTE_SH_INNER|PTE_AP_RW|PTE_PAGE`（`:474`）。
  - `TRANSCFG` 有两个版本（`:414-424`）：`TRANSCFG_WALK_WB`（`6|(20<<6)|(2<<24)|(1<<30)`）与 `TRANSCFG_WALK_NC`（去掉 INA_BITS），默认后者 —— 理由写在 `:416-422`：**GPU 不做表遍历缓存，新建 buffer 就不需要每次 `AS_COMMAND=UPDATE`**，否则每次建 buffer 要 2.4 ms（run 139，5 秒 320 个 buffer）。
- 一个细节值得抄：`mali_fastmap` 默认 1（`:423`），取消映射时仍走 `as0_update()`（`:856-860`），因为「删表项时 TLB 可能还留着」。

#### (b) 用户侧的 DRM 接口（这是"panfrost 怎么集成"的真正答案）

`[MTK树] mtk/mt6768/drivers/gpu/hakios.mali.cpp:76-90` 逐条列出实现的请求号，注释直说「Linux's include/uapi/drm/panfrost_drm.h and drm.h」：

```c
kDrmGemClose = 0x09, kPanfrostSubmit = 0x40, kPanfrostWaitBO = 0x41,
kPanfrostCreateBO = 0x42, kPanfrostMmapBO = 0x43, kPanfrostGetParam = 0x44,
kPanfrostGetBOOffset = 0x45, kPanfrostMadvise = 0x48,
kHakiosSurfaceSeq = 0x7c, kHakiosWait = 0x7d, kHakiosSurface = 0x7e, kHakiosShow = 0x7f,
```

- 结构体按 `drm_panfrost_*` 原样复刻：`drm_panfrost_submit`（`:124-132`）、`create_bo`（`:140-143`）、`mmap_bo`（`:145-148`）、`get_param`（`:150-153`）、`get_bo_offset`（`:155-158`）、`madvise`（`:160-162`）。
- buffer 的物理页逐页进 GPU 页表：`newBuffer()` 每 4 KB 一次 `getPhysicalSegment()` + `hakios_mali_map()`（`:313-318`）。注意注释 `:312`：「the GPU's pages, a quarter of the kernel's 16 KB ones」——**内核页 16 KB，GPU 页 4 KB**，这是 AArch64 XNU 上做 Mali MMU 必须显式处理的差异。
- `IOBufferMemoryDescriptor::inTaskWithOptions(... kIOMapWriteCombineCache ...)`（`:347-348`）：buffer 是 **write-combined 的 wired kernel memory**，注释 `:12-14` 说明理由（程序写进去的内容必须直接落在 GPU 读的地方）。
- 同步策略：**一个地址空间（AS0）给所有人，作业串行**，`hakios_mali_enqueue()` + `mali_worker` 线程（`:962-1068`）+ `hakios_mali_wait(seq)`（`:1071-1090`），fragment job 走 slot 0、其余走 slot 1（`:641`、`:906`）。
- `kPanfrostGetParam` 的 `THREAD_TLS_ALLOC` 特判（`:774-793`）是一个真实的坑：返回 0 时 panfrost 会用 `THREAD_MAX_THREADS` 估栈大小，而 GPU 每核给的栈更多 → 溢写；所以取 `max(0x310, 0x0a0)`。

#### (c) `libdrm` shim（用户态真正的接入点）

`[MTK树] tools/mesa/hakios-drm/hakios.drm.c`（226 行）+ `xf86drm.h`（76 行）：

- `drmOpenWithType()` 只认 `"msm"`（`:33`），找出 `HakiosGPU` 服务后 `IOServiceOpen`，然后 `open("/dev/null")` 造一个**假 fd**（`:48`）——「freedreno knows its device by a file descriptor. It gets one that means nothing」。
- `drmIoctl()` = `call(request & 0xff, arg, IOCPARM_LEN(request))`（`:79-89`），即 **ioctl 号低字节当 selector，结构体原样进出**。
- `drmGetVersion()` 谎报自己是 `"msm"` 1.7（`:109-120`），因为 panfrost 用 softpin（buffer 地址由内核给）。
- 注意：这个 shim 的 `HAKIOS_GPU_SERVICE` 写的是 `"HakiosGPU"`（`:19`），**不是** MT6768 的 `HakiosMali` —— 说明它原本是给 Adreno 线写的，Mali 版在同一目录同一套接口下复用（`mtk/README.md:58-60` 也承认 MT6768 的用户态「still reads the project's former names」）。

#### (d) Metal 怎么接上

- `[DEV6768] kernel/README.md:13`：`iokit/Kernel/RedwoodMali.cpp` = "panfrost's DRM interface as an IOKit user client"。
- `[DEV6768] docs/metal-x6826b-plan.md:5`：**"The AIR → NIR → panfrost translation stays"** —— 即 Metal 的 shader **不是**直接生成 Mali 指令，而是 **AIR → NIR**，然后交给 Mesa 的 panfrost 后端。这是 Mali 线与 Adreno 线的**根本分叉**：
  - Adreno（`[XNUK] hakios-metal/`）：AIR → NIR → **ir3**，Mesa 在**进程内**当编译器库用（`hakios-metal/src/compiler.cpp`，`README` 写 `built in with MESA=1`），不走 DRM。
  - Mali：AIR → NIR → panfrost 的 **midgard/bifrost 后端**，走 `HakiosMali` 的 DRM 请求。
- Metal 侧实际运行的记录（`docs/metal-x6826b-plan.md:34-35`）给出了一整套已验证的 boot-args：
  ```
  cpus=4 touchscreen=2 buttons=2 mali=1 mtk_cpufreq=3 mali_mhz=800 fb_queue=1 fb_flip=1
  fbscale=2 fbfit=3 metal=1 camtl=1 metal_profile=4 rwm_async=0 rwm_finish=1
  ```
- 有一个已定位的性能/正确性结论值得记：`rwm_async=1` 会出 "breakcore"（图标重影、widget 画成 "H"），设 `rwm_async=0 rwm_finish=1` 全同步后消失（`:25-27`）；OpenGL ES 路径（`camtl=0`）是干净的 → **问题在 Metal 驱动的异步路径，不在内核、不在显示、不在 panfrost**（`:27-31`）。

#### (e) MMU 处理的三个可直接抄的结论

1. **GPU 页 4 KB，内核页 16 KB**：所有 buffer 按 4 KB 逐页映射（`hakios.mali.cpp:312-318`），unmap 也按 4 KB 走（`hakios.mali.c:841`）。
2. **`AS_TRANSCFG` 的 INA_BITS 要去掉**（`TRANSCFG_WALK_NC` vs `WALK_WB`，`hakios.mali.c:414-415`），否则建 buffer 必须每次 `AS_COMMAND=UPDATE`，2.4 ms/个。
3. **删表项必须显式 `as0_update()`**（`hakios.mali.c:856-860` + 注释 `:863-869`），因为「with the region flush a fragment job faulted on a page of a buffer whose entry was there」—— 用整表 update 而非 region lock+flush。

### 1.2 驱动目录组织与构建接入

#### 目录布局（`[MTK树] mtk/`）

```
mtk/
├── README.md                        MTK 部分的说明书（含 MT6768 vs MT6895 对照表）
├── loader/                          mtkloader：一个程序服务所有 MTK 手机
│   ├── mtkloader.c (7496 行)       主程序
│   ├── start.S (537) / loop.S / diag_start.S / fbdiag_start.S
│   ├── msdc.h (200)                eMMC 读
│   ├── ufs.h (3201)                UFS 读（MT6895 用）
│   ├── mtkpwrap.h (79)             PMIC wrapper
│   ├── mtkspi.h (283) / tcm.h (312)  SPI + 触摸固件
│   ├── mtkmali.h (274)             GPU 寄存器手册（带出处）
│   ├── mtkconnsys.h (225)
│   ├── mtkdbg.h (478)              屏幕 + ramoops 输出
│   ├── build.sh (54) / build_flashed.py (20)
│   └── touchfw.h / font8x16.h      构建时生成，git 忽略
├── mt6768/                          SoC 层
│   ├── soc.conf (10 行)             SoC 的身份
│   ├── configs/mt6768.config (50)   SoC 的配置片段
│   └── drivers/{gpu,input,storage,wifi}/{conf, hakios.*.c[,cpp]}
├── mt6895/                          同形状的 SoC 层
│   ├── soc.conf (14)
│   ├── configs/mt6895.config
│   ├── loader/board.h (910)
│   └── drivers/{audio,gpu,input,usb}/{conf, hakios.*.c}
└── tools/                           xnu-cycle.sh / xaga-cycle.sh / 探针 / 日志读取器
```

#### `conf` 文件格式（**不是 Kconfig**）

`docs/drivers-config.md:1-12` 明说：XNU 用的是 `config/MASTER` 的 `options` + `conf/files` 的 `standard`/`optional`，这里**不模仿 Kconfig 的文件**，而是「what a driver is, is written once per subsystem, in `drivers/<subsystem>/conf`」。

语法（`docs/drivers-config.md:37-44` 的表格）：

| 词 | 意思 |
|---|---|
| `standard PATH [OPTION...]` | 每个内核都编的文件 |
| `driver NAME` | 产生符号 `CONFIG_HAKIOS_NAME` / XNU option `HAKIOS_NAME` / 属性 `hakios_name` |
| `file PATH [OPTION...]` | 它的一个文件，路径是**在 Apple 树里的路径**；`iokit/Kernel` 的 `.cpp` 还要加 `iokitcpp` |
| `requires NAME...` | 不能缺的依赖 |
| `default y\|n` | 没被点名时的默认 |
| `help TEXT` | 说明 + 哪个 boot-arg 启动它（可多行） |

`mt6768` 的四个 conf 实读：

- `drivers/gpu/conf:5-11`：`driver MALI` / `file bsd/dev/hakios.mali.c` / `file iokit/Kernel/hakios.mali.cpp iokitcpp` / `default n`。
- `drivers/input/conf`：`MTK_KEYS`(5-10)、`MTK_SCP`(12-16)、`TCM`(18-23)。
- `drivers/storage/conf:5-9`：`MTK_EMMC`。
- `drivers/wifi/conf:5-9`：`MTK_CONNSYS`。

**全部 `default n`**，理由在 `drivers/gpu/conf:2-3`：「MediaTek's drivers are `default n`: a configuration has them only when it includes mt6768.config」—— 防止 Qualcomm 设备的 defconfig 误编到 MTK 驱动。

#### 配置怎么接进 XNU 构建

`docs/drivers-config.md:105-123` 是这整套机制的说明书，逐条：

| 写进内核树的文件 | 内容 | 进 `xnu/hakios.patch`？ |
|---|---|---|
| `bsd/conf/files`、`iokit/conf/files` | 一段 `# HakiOS drivers: ...` 到 `# end of the HakiOS drivers`，每文件一行 `standard` 或 `optional hakios_<name>` | 是 |
| `config/MASTER.hakios` | `HAKIOS_DRIVERS = [ hakios_goodix ... ]` + 每个 driver 一行 `options HAKIOS_<NAME> # <hakios_<name>>` | 否（每次构建生成） |
| `bsd/dev/hakios.drivers.h` | 驱动清单，供 `sysctl kern.hakios.drivers` 和启动行 `hakios-kernel: N drivers built in` | 否 |

`SETUP/config/doconf` 读 `MASTER.hakios`（`docs/drivers-config.md:116`），选项以 `-DHAKIOS_<NAME>` 进编译命令（`:118-120`）。

**链接不到就 stub**：`drivers/core/hakios.stubs.c` 为「没编进来的驱动」提供空实现（`docs/drivers-config.md:125-132`）。本次实读确认 MT6895 的四个驱动都在这张表里：
```
drivers/core/hakios.stubs.c:61  void hakios_nvt_init(void) {}
drivers/core/hakios.stubs.c:65  void hakios_mt6895_keys_init(void) {}
drivers/core/hakios.stubs.c:74  void hakios_mt6895_headset_init(void) {}
drivers/core/hakios.stubs.c:78  void hakios_mali610_init(void) {}
```
对应的启动点：`drivers/input/hakios.input.c:233/236/243`。头文件声明在 `xnu/files/bsd/sys/hakios.input.h:49-53`、`xnu/files/bsd/sys/hakios.audio.h:64`。

#### 设备仓 ↔ 公共树的同步机制

- `devices/manifest.conf:19-30`：`soc mt6768 → path mtk/mt6768`、`soc mt6895 → path mtk/mt6895`、`device x6826b → repository hakios-mt6768 @ 10a4633a…`、`device xaga → path devices/xaga`。**MT6895 的 SoC 层是 `path`（树内），MT6768 的 SoC 层也是 `path`**，而设备是 `repository`。
- `[DEV6768] AGENTS.md:27-28` 明写两边是同一份文件的两个名字，改一边必须改另一边。

#### 构建入口

- `Makefile:94-114`：`kernel`（`xnu/apply.sh` + `tools/make-kernel.sh`）、`loader`（`mtk/loader/build.sh` 或 `xnuloader/build.sh`）、`payload`（`xnuloader/mkpayload.py` 打 kernel+ramdisk）、`boot-image`（`mtk/loader/build_flashed.py`）、`system-flash`。
- `Makefile:160-168`：`make phone` 在 `MTK` 非空时走 `$(CYCLE)`（`xnu-cycle.sh`）。
- `build.sh` 用 `-DBOARD_HEADER="mtk/<soc>/loader/board.h"` 把板级常量注入同一个 loader（`mtk/README.md:63-68`）。

### 1.3 MTK 平台层（SPMI/PMIC、时钟、存储、无线）

#### SPMI / PMIC wrapper

- **MT6768 走的是 PMIC wrapper（WACS2），不是 SPMI**：`mtk/loader/mtkpwrap.h:1-9` 说 PMIC 是 MT6358，经 `0x1000d000` 的 wrapper，只用 WACS2 通道（Linux 用的那个），布局抄 `drivers/soc/mediatek/mtk-pmic-wrap.c` 的 mt6765 表。
- 事务状态机（`mtkpwrap.h:26-59`）：
  - `pwrap_wait(IDLE)` → 写 `PWRAP_WACS2_CMD = (reg >> 1) << 16`（**寄存器号要除 2，因为 PMIC 寄存器是 16 位字**）→ 等 `WFVLDCLR` → 读 `RDATA` 低 16 位 → 写 `VLDCLR=1`。
  - 写事务：`(1u << 31) | ((reg>>1)<<16) | value`，然后等回 IDLE。
- 内核侧同款：`drivers/input/hakios.mtk_keys.c:29-35` 定义 `PWRAP_PHYS 0x1000d000`、`WACS2_CMD 0xc20`、`RDATA 0xc24`、`VLDCLR 0xc28`、`FSM(r) = (r>>16)&7`、`IDLE 0`、`WFVLDCLR 6`；电源键读 `MT6358_TOPSTATUS 0x28` 的 `PWRKEY_DEB (1<<1)`（`:37-38`）。
- GPU 的两路电源（`mtkpwrap.h:61-77`）：`BUCK_VGPU_CON0 0x1508` / `_DBG1 0x1520` / `_ELR0 0x1526`、`LDO_VSRAM_GPU_CON0 0x1bc8` / `_DBG1 0x1be4`、`LDO_VSRAM_CON3 0x1bf4`；电压换算 `500 + selector*6.25 mV`（`:74-77`）。但 **`hakios.mali.c:19-20` 明说这两路是 preloader 开的，驱动不碰**。
- **MT6895 换成了 SPMI**：`mtk/mt6895/drivers/audio/hakios.mt6895_headset.c:21-31` 用 `PMIF_BASE 0x1c804000`、`SWINF_ACC 0x0880`、`RDATA 0x0894`、`VLDCLR 0x08a4`、`STA 0x08a8`、`PMIF_CMD_EXT_REG_LONG=3`；`mt6895_keys.c` 用同一通道读 MT6363。这是**完全不同的硬件路径**（见 §5）。

#### 时钟

- GPU 时钟（MT6768）：`topckgen CLK_CFG_1`（`0x50` 值 / `0x54` SET / `0x58` CLR），`MFG_SEL_FIELD = 3u<<0`、`MFG_SEL_PDN = 1u<<7`、`MFG_SEL_SYSPLL_D3 = 2`（`mtkmali.h:66-73`）。落到代码：`hakios.mali.c:522-523`。
- 大核 / GPU 变频：`drivers/input/hakios.mtk_keys.c` 同时管 keypad 和 `mtk_cpufreq=`/`mali_mhz=`（`mt6768/drivers/input/conf:8-10` 的 help 写明 375–950 MHz 走 GPUPLL，并同时调 `VSRAM_GPU` 和 `VGPU`）。
- MT6895 的时钟复杂得多：`mfg_ref_sel`（`CLK_CFG_4 @0x50`，bits 25:24）、`mfg_sel_1_sel`（`CLK_CFG_5 @0x60`）、`mfg_sel_0_sel`（`CLK_CFG_30 @0x1f0`，bit16）、`CLK_UPDATE @0x04`（`mtk/mt6895/drivers/gpu/hakios.mali610.c:115-132`）。头注释 `:27-33` 记了一个大坑：十二个域全上电、块没门控，`GPU_ID` 仍读 0 —— 因为 `mfg_sel_0` 还指向 `mfg_ref_ck`，而它自己的父选择从没设过，GPU 根本没时钟；把 `mfg_ref_sel` 指到 26 MHz 晶振后 `GPU_ID` 立刻变成 `0xa8670004`。

#### 存储（eMMC / MSDC）

- 设计前提：**LK 已经把卡带起来了**，loader 和内核都只做「读」。
  - `mtk/loader/msdc.h:1-11`：「LK has already powered the card, set the bus up and read partitions of its own by the time we run, so this does not initialise anything」。
  - `drivers/storage/hakios.mtk_emmc.c:12-16` 同一句话，加「MSDC0 in PIO mode, plain block reads (CMD17, or CMD18 then CMD12). Registers as Linux's drivers/mmc/host/mtk-sd.c has them」。
- 寄存器（`msdc.h:17-62`）：`MSDC0_BASE 0x11230000`、`MSDC_CFG 0x00`（`PIO = 1<<3`）、`MSDC_INT 0x0c`、`FIFOCS 0x14`、`TXDATA 0x18`、`RXDATA 0x1c`、`SDC_CFG 0x30`、`SDC_CMD 0x34`、`SDC_ARG 0x38`、`SDC_STS 0x3c`、`SDC_RESP0 0x40`、`SDC_BLK_NUM 0x50`；命令宏 `CMD_OPC/RSP_R1/DTYPE_SINGLE/DTYPE_MULTI/STOP/BLKLEN`；`MMC_READ_SINGLE 17 / READ_MULTI 18 / STOP 12 / SEND_STATUS 13`。
- 内核驱动行为（`hakios.mtk_emmc.c:1-18`）：把 `xnusys` 分区当块设备，`/dev/emmc0` `/dev/remmc0`，`emmc_root=1` 时成为 root；`EMMC_MAX_BLOCKS 256`（128 KB/命令，`:40`）；**构造上只读** —— strategy 例程拒写、拒绝以写打开（`:16-18`）。
- 为什么要搬到 eMMC（`docs/x6826b-emmc.md:7-12` 的对照表，是这次调研里最有说服力的数据）：

| | md0（整镜像进内存） | eMMC（按需读） |
|---|---|---|
| `vm_page_bootstrap` | 97 218 free / **162 878 wired**（2.5 GB） | 254 397 free / **5 699 wired**（89 MB） |
| 几分钟后 | free 10–26 MB，compressor 90–138 MB，"System is unhealthy"，jetsam 杀进程 | free ~1 GB，available ~2.5 GB，compressor 0 |
| loader 读镜像 | 203 s（11 MB/s） | 无 |

- 两个必须记住的副作用（`docs/x6826b-emmc.md:20-26`）：
  1. **`rd=md0` 要留着**：libignition 靠它走 "ramdisk" 分支；改成 `rd=emmc` 会去找 preboot volume 并重启（run 322）。
  2. 用 `emmc_root=1` 时内核会**跳过 sealed-root 检查**（否则 run 321 panic "rootvp not authenticated"）。
- loader 构建要用 `-Os`：`-O2` 时内核超 boot 分区 2 KB（`docs/x6826b-emmc.md:27`）。

#### 无线（CONNSYS / Wi-Fi / BT）

`drivers/wifi/hakios.mtk_connsys.c:1-25` 是这次调研里信息密度最高的注释块，逐条地址都带出处（`MiCode/MTK_kernel_modules`，merlin-r-oss）：

```
SPM (0x10006000)  +0x32C TOP1_PWR_CTRL  bit0 reset released, 1 isolation, 2 power on, 3 power on(2nd), 4 clock
                  +0x180 PWR_STATUS     bit1 CONNSYS on, bit30 its second (TOP2) domain on
                  +0x184 PWR_STATUS_2ND
MCU_TOP_MISC_OFF (0x180B1000) +0x10 CONSYS_IP_VER: 0x10020501 once CONNSYS runs; +0x1C config
MCU (0x18002000)  +0x00 hw version, +0x04 firmware (ROM) version
```

- **安全规则（很重要）**：`hakios.mtk_connsys.c:17-19` —— 「The CONNSYS registers (the last two blocks) are read only when SPM says both of its domains are on: **a read of a powered-off block of this kind can hang the bus**」。同一模式在 MT6895 的 `hakios.mali610.c:1514-1515`（mfg1 MPU 已上电就早退）也出现。
- 步骤化设计：`connsys_step=1..11`，每步只多做一件事，1–2 只读、3–4 才开始写（`:2-25`）。
- BT 走 `HCI over STP`，通道 0（`[DEV6768] kernel/README.md:15-17`）；Wi-Fi 是 gen4m 驱动（`:19`）。
- 文档 `[DEV6768] docs/wifi-x6826b.md` 616 行，含 `connsys_step=1..17` 的完整推进记录（`:190-379` 是逐日实测），BT 另有 152 行 `docs/bluetooth-x6826b.md` + 109 行 `docs/bluetooth-mtk-porting.md`（**后者是"换一台 MTK 手机怎么做"的迁移指南**，`:43-79` 给了「先查什么、按什么顺序」的清单，`:67-79` 是 `skel_quirks` 的答案表）。

#### SCP（传感器核）

`drivers/input/hakios.mtk_scp.c:1-23`：地址/寄存器全带出处（`scp/mt6768/scp_reg.h`）：

```
cfg (0x105C0000) +0x1C SCP_A_TO_HOST_REG (bit0 有消息给 host, bit8 watchdog)
                 +0x50..+0x6C SCP_A_GENERAL_REG0..7 (+0x64 重启握手, 0x34/1)
                 +0x84 SCP_A_WDT_REG
                 +0xA8 SP, +0xAC LR, +0xB0 PSP, +0xB4 PC  (Cortex-M 调试副本)
                 +0x114 SCP_SLEEP_STATUS_REG (bit0 睡, bit1 深睡)
infracfg (0x10001000) +0x148 MODULE_RESET_STATUS bit3 SCP 被按在复位
```

- **判活方法值得抄**：`PC 读两次、间隔 20 ms，不同就在跑`（`:17`）。
- 关键事实：Android 内核**不**加载 SCP 固件（`scp_helper.c` 里没有 `request_firmware`），是 **LK 加载的**，Android 只等固件的 "ready" 消息（`:5-7`）。
- 推进梯子：`scp_step=1..7`，5 才「通过 nanohub 使能」，6 才「给 hub DRAM FIFO」，7 才进线程（`:19-22`）。

### 1.4 启动链与 Android 共存

#### 结构

`[MTK树] mtk/loader/mtkloader.c:1-21` 头注释就是完整设计：

```
The boot image's kernel (tools/chainboot.py) is this program, then Android's own kernel Image
at CHAIN_OFFSET, then the XNU payload ("XNUP": kernel, ramdisk). Each boot, by the marker:

  none     RUN: load XNU, give it LK's framebuffer, arm the watchdog, start it.
  RUNNING  XNU was cut short by the watchdog: copy its log from DRAM into ramoops, start Android.
  DONE     XNU came back to us (panic, early exception, orderly end) and its log was saved then: start Android.
```

- `CHAIN_OFFSET 0x40000`（`:274`），`KERNEL_PA 0x40080000`（`:277-279`）。即 **boot_a 里 = [loader][Android Image @+0x40000][XNU payload]**，一个分区两套系统。
- 三个 marker 值写在 `MARKER_PA 0x80000000`（`:286-306`）：`MARK_RUNNING 0x4e52584e`（"NXRN"）、`MARK_DONE 0x4e44584e`（"NXDN"）。**DRAM 里三个副本（+0/+64/+128），`marker_get()` 取多数**（`:590-628`）—— 防位翻转。
- 「不是我们自己的就还给 Android」：`chain_android()`（`:1501`）。

#### 和 Android 的共存三件套

1. **boot_a 被链式劫持**：loader 在 Android 的内核 `Image` 前面，Android 照常启动；要跑 XNU 时由 `mtk/tools/boot-xnu.sh`（7 行）+ pstore 的 pmsg 区传一次性请求（`xaga-cycle.sh:15-17` 的注释描述了这个握手机制）。
2. **`loader_return_after=`**：XNU 侧 `osfmk/arm64/loader_return.c` 定时把控制权交回 loader，带上日志指针。定义在 `mtkloader.c:154-157`，拼进命令行在 `:237-239`。返回值语义（`:2445-2447`）：`0 = end`、`1 = early exception`、`2 = checkpoint`。
3. **看门狗**：`XNU_WDT_SECONDS`（`:66-69`）写 TOPRGU（`:3582-3590`），让板子在 N 秒后自己回到 Android；`XNU_WATCH_SECONDS`（`:70-73`）让第二个核当 watcher，超时抓快照。

#### 返回路径（`xnu_returned()`，`mtkloader.c:2432-2534`）

值得逐条抄的取证顺序：

1. `read_screen()` 先把 XNU 死时屏幕上的字符读回来（`:2441-2443`）—— 「what the screen showed when XNU came back」。
2. 打印 `reason` / `code` / `ELR` / `FAR`（`:2445-2456`）。
3. 打印 `tasks_count` / `nprocs` / `initproc`（`:2457-2470`）—— **判断用户态到底起没起**，`tasks==1` 就是只有内核任务。
4. `show_panic_string()` + `show_backtrace()`（`:2471-2474`）。
5. `save_log()` 进 ramoops（`:2481`）。
6. `save_host_keep()` 把内核的 `kern.hakios.host_keep` 灌进 ramoops 的 **pmsg** 区（`:2483`）。
7. `save_log_to_flash()` 写 logbook（`:2497`）。
8. `show_log_tail(40)` + `dbg_delay_ms(20000)`（`:2512-2513`）留 20 秒给拍照。
9. `mali_power_down()` 再 `chain_android()`（`:2522-2530`）—— **顺序不能反**：`:2523-2526` 注释说「a domain left raised is why every earlier run came back to this image instead of to adb」。

#### 板级适配（一个 loader 服务所有 MTK 手机）

`mtk/README.md:63-82`：

- `mtk/loader/mtkloader.c` 是**一个程序**，差异全在 `mtk/<soc>/loader/board.h`，`build.sh` 用 `-DBOARD_HEADER=` 注入。
- 四个开关（`mtk/README.md:74-79`）：`XNU_SYSTEM_IMAGE`（1=读 `xnusys` 当 md0）、`BOARD_TOUCH`（1=给 TD4160 发固件）、`XNU_BIG_CLOCK`（1=抬 A75 PLL）、`BOARD_STORAGE_UFS`（1=走 `ufs.h`，0=走 `msdc.h`）。
- `mtk/mt6895/loader/board.h`（910 行）是第一个这样写出来的板头，每个值都注明来自哪份 dump、错了要付什么代价（`:6-12`）。

### 1.5 日志与取证

这是 HakiOS 最值得抄的一整套机制，**四层**：

#### 层 1：ramoops console（`/sys/fs/pstore/console-ramoops-0`）

- loader 把 XNU 的日志拷进 ramoops：`save_log()`（`mtkloader.c:1594-1623`）。策略是 **head + tail**：
  ```c
  head = total <= room ? total : room / 4;      // 放得下就全放，否则头 1/4
  tail = total <= room ? 0 : room - head;       // 尾巴占 3/4
  ```
  中间那段被替换成一行 `[... N bytes of the XNU log left out here ...]`（`:1615-1617`）。
- 取回：`mtk/tools/xnu-cycle.sh:39` —— `adb shell 'su -c "cat /sys/fs/pstore/console-ramoops-0"'`，重试 10 次、每次 2 秒。

#### 层 2：pmsg 区（给"必须完整"的行）

`mtkloader.c:1630-1679`（`save_host_keep()`）+ `hakios.mali.c` 那一侧：

- 内核侧缓冲 `hakios_host_keep[0xff00]`（`hakios.mali.c:133-135`），注释 `:125-131` 解释为什么需要它：
  > "The log the host gets is a 256 KB head and tail of a run's ... the pipeline recipes a two-hour run built (run 352) were in neither."
- 两个写入口：sysctl `kern.hakios.host_keep`（用户态写，`hakios.mali.c:137-167`）和 `hakios_host_keep_add()`（内核自己，`:171-193`）。
- loader 把它整段塞进 pmsg 区，Android 上表现为 `/sys/fs/pstore/pmsg-ramoops-0`（`mtkloader.c:1626-1628`）。
- 取回：`xnu-cycle.sh:43` 追加到 `<log>.keep`。

#### 层 3：`kept:` 96 行环（屏幕可读的那份）

`hakios.mali.c:72-122`：`KEPT_LINES 96` 的环形缓冲，`hakios_keep()` 写，用户态用 `sysctl kern.hakios.say` 写（**行首 `!` 才进环**，`:113`），`mali_thread` 每 60 秒把所有行 `kept: %s` 重打一遍（`:1105-1109`）—— 这样长跑后日志尾部一定带着它们。

#### 层 4：logbook（烧进 flash，屏幕/UART 都不可靠时）

`mtkloader.c:1748-1869`：

- 位置：`xnusys` 分区**末尾 8 MiB**（`XNU_LOGBOOK_SLACK = 8<<20`，`:1761-1763`），最大 4 MiB 日志（`XNU_LOGBOOK_MAX`，`:1764-1766`）。
- 头 4096 字节 `struct logbook_hdr`，magic `0x474f4c48`（"HLOG"），字段：`version/len/reason/code/elr/far/ticks/where/sum`（`:1769-1780`）。
- 写失败会**分三段报告**：header / body / tail（`:1827-1858`），每段失败都打印 `board_storage_what()`。
- 读回：`mtk/tools/xaga-pull-log.sh`（49 行）：`dd ... bs=4096 skip=$SKIP count=$COUNT` + `adb pull` + Python 解头 + **校验和比对**（`:45`），并把 `reason` 翻成人话（`:35-36`）：`0=end`、`1=early exception`、`2=checkpoint`、`0xfeedface=boot note`。
- **为什么不写 pstore**：`xaga-pull-log.sh:4-6` 直说「this board has no UART, no ramoops and no pstore: the screen holds one page, and a photograph of a scrolling console is worthless」。

#### 层 5：屏幕截图进日志

- `fbshot=` boot-arg：内核把画面 base64 行打进日志（`fbshot N: <base64> w h`），`mtk/tools/fbshot.py`（41 行）还原成 PNG。
- 屏幕文字回读：`read_screen()` 在 XNU 死后把屏幕内容读回来（`mtkloader.c:2441`）。
- `frames.py`（140 行）读 `tl:` 行算帧时序，`cache-name.py`（35 行）把地址翻译成 iOS 共享缓存里的镜像名。

#### 层 6：watchdog 快照（连日志都拿不到时）

`mtkloader.c:2776-2960`：

- `watcher_main()` 在**第二个核**上跑，`mmu_on()` 后用可缓存映射看 XNU 的内存（`:2824-2837`）。
- 把 console 输出 tee 到 `SNAP_PA`（`:2793-2816`），`snap_end()` 前 `dcache_clean_range()` —— 「the record has to be in DRAM, not only in a cache that the reset drops」（`:2814-2815`）。
- **屏幕上的颜色就是判据**（`:2865-2879`、`:2922-2935`）：整屏刷一个色块，绿=watcher 跑到了，蓝=走完了，Apple logo 无绿=watcher 入口就挂了。这段注释值得原样读：
  > "Green here and still green later: this core runs … Green here, blue after the watch: the block finished and the two reset attempts are what does not work. Apple logo and no green at all: this core never got here."

---

## 2. hakios-xnu-kernel（Qualcomm 线）

`main` 分支，HEAD `63fc2b4`（2026-11-06）。树内 `drivers/` 按子系统分 12 个目录（`[XNUK] drivers/README.md:22-35`）：`input / storage / gpu / camera / haptic / audio / power / wifi / bluetooth / modem / net / core`。

`drivers/audio/` 规模（`wc -l` 实读）：

| 文件 | 行数 | 作用 |
|---|---|---|
| `hakios.adsp.c` | 3306 | 音频 DSP 加载 + 链接 |
| `hakios.mic.c` | 1852 | 麦克风（WCD938x + SoundWire） |
| `hakios.sound.c` | 1541 | 扬声器播放（ASM/ADM/AFE） |
| `hakios.speaker.c` | 1242 | 两颗 AW88261 智能功放 |
| `hakios.audio.c` | 362 | `/dev/hakios-audio`（统一出口 + 录音） |
| `hakios.guard.c` | 318 | 扬声器保护（限幅/高通/温压降额） |
| `hakios.virtio_sound.c` | 416 | QEMU 用 |

`drivers/audio/conf`（32 行）：`standard bsd/dev/hakios.audio.c` + `driver ADSP / SPEAKER / SOUND (requires ADSP SPEAKER) / MIC (requires ADSP SOUND) / VIRTIO_SOUND`。

### 2.1 音频/耳机/codec 怎么写的

**先说最重要的一条结论：Qualcomm 线里没有 3.5 mm 耳机检测。**

证据（全树扫，非抽样）：

1. `grep -rniE "\bjack\b|accessory|headset|mbhc" --include=*.c --include=*.h --include=*.cpp --include=*.md .`（在 `[XNUK]` 全树）→ **0 命中**。
2. GitHub tree API 拿 `main` 的完整 blob 列表（879 个），过滤 `jack|headset|accdet|mbhc|codec` → **0 命中**。
3. 搜仓库 `hakios+headset` / `hakios+accdet` / `hakios+3.5mm` → 均 **total_count 0**。
4. `drivers/audio/` 里唯一带 `HPH` 的是 `hakios.mic.c:805-811` 与 `:921-932`，但读上下文（`:896-937 codec_init()`）可确认那是 **`wcd938x_init_reg()` 里寄存器初始化的一部分**（`WCD_HPH_NEW_INT_RDAC_HD2_CTL_L/R`、`WCD_HPH_REFBUFF_UHQA_CTL`、`WCD_HPH_SURGE_HPHLR_SURGE_EN`），**不是插拔检测，也没有 `mbhc` 模块**（WCD938x 的 MBHC 在 Linux 里是 `wcd-mbhc-v2.c`，这里没有对应实现）。

**所以：** 对我们 MT6895 的 3.5 mm 问题，「Qualcomm 线有现成 jack 代码可以抄」这个前提**不成立**。能从它身上抄的是**结构**，不是 jack 本身。

#### 它到底写了什么（结构层面，可迁移的部分）

**(1) 分层：硬件 → `/dev/hakios-audio` → 播放/录音**

`drivers/audio/hakios.audio.c:1-27` 的头注释把出口讲清楚了，两个方向：

```
to a sink    a driver that plays it: the virtio sound device under QEMU ...
             On the phone there is none yet (hakios.speaker.c is where it begins).
into a recording, with boot-arg rec_sound=N: ... tools/sound.py makes a WAV file of it
```

- 录音格式是自描述的（`hakios.audio.c:51-61`）：`rec_header{magic "RWSND001", rate, channels, blocks}` + 每块 `rec_block{nanoseconds, frames}` + `frames*4` 字节 16-bit 样本。
- 录音走**物理地址传给 loader**：`extern uint64_t loader_sound[3];`（`:63-64`），loader 再交给主机（`"xl sound"`，`:12-13`）。
- 静音判定阈值 `SILENCE 8`（`:48`），`QUIET_MS 500`（`:47`，500 ms 没写就算写者停了），`REC_MAX_SECONDS 1200`（`:49`）。

**(2) codec 访问的"写后读回"纪律（这是最该抄的一条）**

`drivers/audio/hakios.mic.c:842-876`：

```c
static bool codec_put(uint16_t reg, uint8_t value, uint8_t check)   // :844
{
    ...
    if (!swr_read(codec_device, reg, &back) || (back & check) != (value & check)) {
        say("mic: the codec's %#06x <- %#04x, but it reads %#04x: STOPPING HERE\n", ...);
        broken = true;                    // 一次不匹配，全局熔断
        return false;
    }
}

static bool codec_bits(uint16_t reg, uint8_t mask, uint8_t value)   // :867  读-改-写-核对
```

**每写一个寄存器都读回校验，一次不符立刻 `broken = true` 全线停手**。这跟我们耳机这边「写了看不到效果还得猜」的处境正好对症。

**(3) 寄存器常量的组织方式**

`hakios.mic.c:795-838` 是一整块常量表，按 codec 的寄存器页分组（`WCD_*`、`SWRS_*`、`SWRM_*`），**每个都带 Android 的地址**。这套命名直接对应 Linux 的 `wcd938x-registers.h` / `swrm-registers.h`，可以照搬到 MT6368。

**(4) 分级启用 + 分级回滚**

`hakios.mic.c:21-57` 用 `mic=1..3` 分三档：1=只看（读寄存器 + 投票时钟，然后**原样还回去**）、2=把 codec 拉到能在 SoundWire 上应答（再全部倒序撤销）、3=真录音。`mic=3` 还带一堆单点开关做对照实验：`mic_input=0`（喂 ZERO，验证是否精确静音）、`mic_adc=0`、`mic_test=1/2`、`mic_main=1`、`mic_tap=1`。

**(5) 扬声器功放：写-读回 + 故障即双关**

`hakios.speaker.c:48-56`：
> "Every write is said in the log and read back; a value that does not read back as written, a byte nobody acknowledges, or a fault in the status register (under-voltage, over-current of the boost or the output, over-temperature, no clock, the watchdog, over-voltage) has both amplifiers powered down at once … and nothing more done."

**(6) I2C 用 GPIO 手工 bit-bang**

`hakios.speaker.c:58-60`：「I2C is done by hand: a pin is either pulled low or let go … at 100 kHz at most. For each register read or written the two pins are taken from the serial engine (function qup03) as GPIOs and given back as they were found right after.」—— 因为 XNU 里没有 I2C 框架。**这条对 MTK 同样成立**（`hakios.tcm.c:12-13` 也说 "There is no clock, pin or SPI framework in this kernel, so the driver reaches the registers itself"）。

#### 文档侧可参考的

- `[XNUK] docs/driver-review/sound.md`（37 KB）是一份**只读审计报告**：`S-01..S-16` 每条标 `CONFIRMED / PLAUSIBLE / NEEDS-HARDWARE`（`:54-204`），并有「Checked and found fine」（`:204`）与「Not checked」（`:245`）两节。这种「先声明没查什么」的写法值得学。
- `[XNUK] docs/metal-plan.md:1-40` 是 Adreno 线 Metal 的现状与三步拆分（A 编译器 / B 设备 / C 内核侧），其中 `C. The kernel side` = `HakiosGPUUserClient`（`drivers/gpu/hakios.gpu.cpp`）**同时服务两条路径**，请求 `0x7d` 收客户端自己的页当 buffer。

---

## 3. hakios-mt6895（我们的树）

### 3.1 仓库与分支现状

| 项 | 事实 | 依据 |
|---|---|---|
| 公开的 MT6895 仓库 | **不存在**（`oosexgang/hakios-mt6895` 404；搜索只有我们自己的 `lizi600jin/hakios-xaga`） | §0.1 |
| `origin/mtk` 上有 mt6895 层吗 | **没有**（`git ls-tree -r --name-only origin/mtk -- mtk/mt6895` → 空） | 本地 git |
| 本地 `[MTK树]` 上的 mt6895 | 全在暂存区/未跟踪，**一次都没提交**（2 个耳机提交除外） | `git status --porcelain` |
| MT6895 已写的驱动 | 5 个文件 9566 行 | `wc -l` |

| 文件 | 行数 | 状态（据 conf 与头注释） |
|---|---|---|
| `drivers/gpu/hakios.mali610.c` | 5209 | M0（上电/时钟/识别）+ M1（CSF 固件进 MCU 地址空间 + 启动 + 全局接口握手）**卡在 MCU 不执行** |
| `drivers/usb/hakios.mt6895_usb.c` | 1885 | MTU3 @0x11201000 + IPPC @0x11203e00 + USB2 phy @0x11e40000，`usb=2` |
| `drivers/input/hakios.nvt.c` | 1547 | Novatek NT36672E on SPI2 @0x11012000，GPIO109/110/111/112 手工驱动，`touchscreen=3` |
| `drivers/input/hakios.mt6895_keys.c` | 791 | keypad @0x1c00e000 + MT6363 TOPSTATUS over SPMI @0x1c804000，`buttons=2` |
| `drivers/audio/hakios.mt6895_headset.c` | **134** | MT6368 accdet，**只读 dump**，`headset=1` |

### 3.2 SoC 层配置

- `mtk/mt6895/soc.conf:9-14`：`cores 8: four Cortex-A55 (cores 0-3) and four Cortex-A78 (cores 4-7)`，`gpu Mali-G610 (Valhall, "arm,mali-valhall" in the device tree at 0x13000000)`。
- `mtk/mt6895/configs/mt6895.config:9-23` 有一段**坦白书式**的说明，逐条列出「这里没有什么、为什么」：UFS（无 eMMC、loader 只会 MSDC、`HAKIOS_UFS` 依赖 UEFI 的 UFSDxe 常驻）、GPU、键与传感器核、Wi-Fi。
- 已启用的：`CONFIG_HAKIOS_NVT`、`CONFIG_HAKIOS_MTK6895_KEYS`、`CONFIG_HAKIOS_MT6895_HEADSET`、`CONFIG_HAKIOS_MT6895_USB`、`CONFIG_HAKIOS_MALI610`（`:74-90`）。
- `devices/xaga/configs/xaga.config:40` 的实跑命令行：
  ```
  CONFIG_BOOTARGS="buttons=2 touchscreen=3 keylog=1 key_scan=1 fbwidth=1080 fbheight=2460 fb_disp=1 mali=6 mali_cold=0 mali_preas0=1"
  ```

### 3.3 GPU 侧（我们卡住的地方）

- 头注释 `hakios.mali610.c:10-33` 把每个数字的出处列全了：寄存器表来自 **panthor**（`panthor_regs.h` / `panthor_mmu.c` / `panthor_fw.c`，GPL-2.0 or MIT）；电源/时钟来自小米公开的 `xaga-s-oss`，具体到 5 个文件（`:22-25`）。
- 两个里程碑：`mali=1` = M0，`mali=2` = M1（`:6-8`）。
- 关键寄存器都在 `:390-441`：`kMcuControlDisable/Enable/Auto (0/1/2)`、`kSentinelVersion 0xff5aa5f0`。
- 「时钟的惊喜」在 `:27-33`：十二域全开、块未门控时 `GPU_ID` 读 0 —— 因为 `mfg_sel_0` 还指着 `mfg_ref_ck`，而它父选择没设过；把 `mfg_ref_sel` 指到 26 MHz 晶振后 `GPU_ID` 立刻 `0xa8670004`。
- 已排除/已确认的清单在 `[MTK树] work-notes-*.md`（24 个文件、18463 行）。与 GPU 直接相关的：
  - `work-notes-gpu-status.md:9-13`：一句话状态 = M0 与 M1 前四步（L2/固件/AS0/MCU enabled）真机跑通，**卡在 `MCU_CONTROL=AUTO` 后一条指令不执行**；判据是哨兵 `0xff5aa5f0` 未被改写。
  - `work-notes-mcu-noexec.md:1-8`：源码级归因；`:10-36` 先纠正一个前提错误（`AS_STATUS_AS_ACTIVE = 0` 是**成功**不是故障，它是忙标志）。
  - `work-notes-csf-mcu-window.md:14-40`：结论是 **`0x4000_0000` 那 256 KiB 不需要 AP 侧映射**，它是 GPU 硅片内 MCU 私有 MMIO，硬件固定译码、不过 GPU MMU；强行映射有害。并把嫌疑转向三条：**VTOR/MPU 没设**（最强）、`GPU_CMD=0x101` 在冷启动的位置、`L2_PWRON` 与 coherency/L2 config 的先后（`:36-41`）。
  - `work-notes-csf-mcu-window.md:38` 的关键实测：RK3588 上能跑的 `arch10.8` 固件自己在 reset 序里写 `VTOR(0xE000ED08)` 与 `MPU_CTRL(0xE000ED94)`；**MTK 固件一次都不写**（两个常量在 MTK text 段命中 0 次，在 `arch10.8` 各命中 1 次）。

**loader 侧（`mtk/mt6895/loader/board.h`，910 行）——这是 MT6895 GPU 工作里信息量最大的文件，逐条都是真机教训：**

| 宏 | 默认 | 文件位置 | 读到的教训 |
|---|---|---|---|
| `BOARD_MALI_PROBE` | 0 | `:560-575` | 十二个域的控制/状态寄存器偏移**无法从文档得到**（MT6895 不在 mainline 表里，mt6893/mt8195 最近但偏移不同）。做法：**把整个 SPM 页每个 dword + GPU 识别寄存器 + MFG 门控全 dump 进 logbook**，靠"关的读零、开的读真值"反推偏移 |
| `BOARD_MALI_POWER` | **1** | `:577-592` | **电源/时钟必须放 loader 做，不能在 XNU 里做**。理由（`:580-585`）：「those blocks fault when they are written while their own clock is stopped, and a bus fault with no vector taken stops the core where it stands, so nothing is ever printed and the loader's logbook stays empty too」。loader 里 MMU 关、映射平坦、异常向量还是 LK 的，**错了还能说话** |
| `BOARD_MALI_PREPOWER` | 1 | `:594-612` | 与 `POWER` 分开是因为 `POWER` 还守着 `chain_android()` 里的清理；**清理不能删** —— 留着 GPU 上电就回 Android 会让板子**重新加载同一张镜像**（Android 的 gpufreq 看到 sleep controller 里有个它从不创建的域状态，就在 adb 起来前复位）。`stage 8` 反证：抬 mfg1 再放回去，Android 30 秒起来 |
| `BOARD_MALI_RETURN` | 0 | `:614-628` | 上电→写 flash→回 Android 而不是进 XNU。理由（`:617-621`）：「the panel goes black as soon as the bring-up runs on this path, so there is nothing to photograph, and a finished bring-up that halted looks exactly like one that took a bus fault」。**借的就是 `save_log_to_flash()`，reason 36 = GPU bring-up** |
| `BOARD_MALI_STAGE` | 0 | `:630-653` | 五档停点：0=真路径跳 XNU，1=十二域后，2=26 MHz 晶振后，3=识别寄存器后，4=MFGPLL+mux 后，5=**一个 GPU 寄存器都不碰**（控制组）。`:646-649` 说 stage 5 必须最先跑：「if the control build also fails to come back then the return path is broken on its own … and no result from stages one to four means anything」 |
| `BOARD_MALI_DOMAINS` | **2** | `:655-683` | 这是全文件最硬的一条实测：**手工抬十二个域会卡在 mfg5** —— `PWR_ON`/`ON_2ND` 置上但 ACK 永不回来（读 `0x111e`），"a request the SPM has taken and will not complete"，挂起的请求把域卡在半开状态，**板子根本不回 Android**。`1` = Android 每次启动做的（安全默认），`2` = L2 需要（`mfg2` 就是 L2 自己的域），`12` = 原始尝试，**保留以便失败可复现**。注释还给了 MTK 自己驱动为什么不抬：`GPUFREQ_PDCv2_ENABLE` 只经 clock framework 把 mfg1 带起来，mfg2 以上交给硬件 PDC，所以十二个域全带 `MTK_SCPD_BYPASS_INIT_ON` |
| `BOARD_MALI_EARLY` | 0 | `:685-697` | 只为一个变量：同样的十二域+晶振+PLL，在 probe 位置跑干净，几千条指令后在 `run_xnu()` 里就只剩 4/12 抬起来、`GPU_ID` 读 0 —— 差别在中间跑的 `watcher_start()` / `touch_bring_up()` / `big_cores_clock()` |
| `BOARD_MALI_STOP` | 0 | `:699-714` | 上电后死循环停机而不是复位（复位会重载同一张镜像，就没得拍了） |

**这段与 MT6768 的对照很关键**：MT6768 的 GPU 上电**写在 XNU 里**（`hakios.mali.c:512-580`），而 MT6895 的**必须写在 loader 里**（`board.h:577-592` 的 boot-loop 教训）。这不是风格差异，是**同一颗 SoC 的时钟/电源块在时钟停住时写会总线 fault** 的硬约束。所以「MT6768 的 GPU 驱动模型可以借鉴」这句话要加限定：**分层可以借鉴，落点不能照搬**。

### 3.4 耳机侧（当前唯一在做的驱动）

`hakios.mt6895_headset.c` 全文只有 134 行，**是纯只读 dump**：

- 头注释 `:4-10` 声明：只用 `SWINF_2` 的 PMIF `0x0880..0x08a8`，只提交并确认 READ 事务，**没有任何 PMIC 写命令、没有 WDATA 访问、没有写 shutdown 0x014b**；并且**必须在 keys 驱动之前同步跑完**（「Do not move this dump to a concurrent worker」）。
- 地址（`:22-37`）：`PMIF_BASE 0x1c804000`、`SWINF_ACC 0x0880`、`RDATA 0x0894`、`VLDCLR 0x08a4`、`STA 0x08a8`、`PMIF_CMD_EXT_REG_LONG 3`、`SWINF_IDLE 0x00`、`SWINF_WFVLDCLR 0x06`、`PMIF_SPIN_LIMIT 200000`；`MT6368_SID 5`、`ACCDET 0x2688..0x26d3`、`AUDDEC_ANA 0x2508..0x2537`。
- 事务（`:59-86`）：等 IDLE → 写 `ACC = (3<<30)|(SID<<24)|(0<<16)|addr` → 等 `WFVLDCLR` → 读 `RDATA & 0xff` → 写 `VLDCLR=1`。
- conf 的 help（`drivers/audio/conf:7-8`）**自己承认**：「`headset=1` 只做 dump；`headset=2` 初始化和插入轮询 **remain TODO**」。

我们这边的进度记录在 `[XAGADOC]`：

- `docs/headset-wip/ROUND-20-VERDICT.md:1-25`：round-20 四次刷机全黑屏；`:3-11` 有一块**自我更正**（「loader 被重写 4172 行」是错的；「逐字复刻 692 参数」也是错的），并明确说「第 4 次刷机仍黑屏 ⇒ 排除耳机驱动」这个推论**不成立**。
- `docs/headset-wip/ROUND-20-VERDICT.md:44-47` 的 `code` 语义表（与我们 loader 的 `reason` 不同层，但同一套 logbook）：`0xfeedface` = 正常 boot note，`0x13` = `probe_flash(19u)` 崩溃标记。
- 本地 HEAD 提交 `7df8a9d` 的标题就是「耳机：更正插入检测寄存器（0x26a9 bit0 是错的）」—— 即**曾经以为 0x26a9 bit0 是插入位，后来自己推翻了**。

### 3.5 本地未公开的两份 xaga 手册（内容质量高，但没进 git）

`mtk/tools/` 下有两份中文手册，`git status` 显示都是 ` A `（新建未提交，`origin/mtk` 上没有）：

- **`xaga-first-boot.md`（148 行）**——首刷清单，最有价值的是 `:73-82` 记的一次**真 bug 与根因**：
  > `gpt_find()` 原来假设扇区是 512 字节…xaga 的 UFS 逻辑块是 **4096**（真机 `/sys/block/sdc/queue/logical_block_size` 实测），GPT header 在 **byte 4096**，而 byte 512..1023 是保留区的全零。所以那行比对永远失败，`load_system_image` 会打印 `no xnusys partition` 然后返回 0 —— XNU 就没有 md0，卡在 `Still waiting for root device`。
  连带修了两处同源错误：条目表地址要乘块大小（`ent_lba * 4096` 不是 `* 512`）、分区大小的 MB 显示原来会显示成真实值的 1/8。
  另 `:17-28` 给了备份的硬要求：`boot_a` 里是 KernelSU，**不能用官方 stock 当备份**（恢复 stock 会丢 root，而 `su -c` 读 pstore 是唯一拿 XNU 日志的路），md5 应为 `a63b2e35a7c2522cb24a6a10332b9bc8`。
- **`xaga-input.md`（243 行）**——触控/按键/USB 三块的逆向手册，`:57` 明写协议「全部由真机 `nvt36672c.ko` 反汇编确认」，`:93` 的 GPIO 寄存器布局「真机 `pinctrl-mt6895.ko` 的 reg_cal 表逐字确认」，`:138` 的 keypad 布局「真机 `mtk-kpd.ko` 反汇编确认」。`:111` 有「一次启动就能判断是没电还是协议错」的判据。

> 这两份是**本次调研里唯一读到的、MT6895 侧的完整工程记录**，但都不在公开仓库，也不在 `[XAGADOC]` 镜像里（`ls docs/` 实读：`BOARD.md / DEBUGGING.md / GPU-ROUND-* / UFS.md / headset-wip/` 等，无同名文件）。

> ⚠️ 顺带一个纪律问题：`mtk/tools/xaga-pull-log.sh:15` 把真机序列号写死在默认值里（`S=${1:-GAJZ4HIN99AQ69Y9}`），`OUT` 默认值还写死了绝对家目录（`:19`）。这两条都违反该项目自己的 `AGENTS.md:50-52` 规则 3（「No personal data: no device serial numbers (scripts read HAKIOS_SERIAL), no home-directory paths」）。同目录的 `xaga-cycle.sh:26` 是对的（`${HAKIOS_SERIAL:?...}`）。**在把 xaga 工具链推上 GitHub 之前必须先改掉这两行。**

---

## 4. 可直接借鉴到 MT6895 的做法（按优先级）

> 只列**读到的实现**，不是建议。

### P0 — GPU：VTOR / MPU 的预置（`work-notes-csf-mcu-window.md:38` 的直接推论）

MT6768 的 GPU 驱动没有这一段（Bifrost 无固件，不需要），但**同一个仓库的 MT6895 侧已经把它列为最强嫌疑**，而 MT6768 提供了**执行这类"loader 预置"的现成范式**：

- 范式本身：`mtkloader.c:3643-3648` 的 `BOARD_TOUCH` / `XNU_BIG_CLOCK`—— 「XNU 不擅长做的事放 loader 做」，且用板级开关隔离（`mtk/README.md:74-79` 的四开关表）。触摸固件就是"必须由 loader 在 XNU 之前送进去"的先例（`hakios.tcm.c:6-10`：「The controller keeps its firmware in RAM and loses it at every reset … and that somebody is the loader」）。
- 具体到 GPU：`[MTK树] mtk/loader/mtkmali.h` 整个文件就是"loader 侧 GPU 序列"的样板（274 行，每个常量带 Linux 出处），MT6895 侧对应的是 `hakios.mali610.c:5719-5720` 附近的 `mali_power_on/mali_aoc_up/mali_clock_26m/mali_pll_300mhz`（在 loader 里）。
- 但注意：**仓库里没有写 VTOR/MPU 的代码**（`grep -n VTOR mtk/mt6895/drivers/gpu/hakios.mali610.c` → 无命中，exit 1；`grep -n "MPU_CTRL\|0xE000ED"` → 无命中）。这一条是**缺口**，不是现成实现。要补的话，落点应该跟 MT6895 其它 GPU 事一样放 **loader**（`board.h:577-592` 的理由：loader 里 MMU 关、向量还是 LK 的，错了还能打印）。

### P0 — GPU：分层判断「什么该放 loader、什么该放内核」

这是 MT6768 与 MT6895 之间**唯一一条必须做出相反选择的**经验，而且两边都写在代码注释里：

| | MT6768（`hakios.mali.c`） | MT6895（`board.h:577-592`） |
|---|---|---|
| 电源域 / 时钟 / PLL | **在 XNU 里**（`:512-580`） | **在 loader 里**（`BOARD_MALI_POWER=1`） |
| 理由 | Bifrost 上电只是"power and clocks and nothing else"（`:6`），顺序已由 loader 的 GPU probe 在真机上验证过（`:7-8`） | 「those blocks fault when they are written while their own clock is stopped, and a bus fault with no vector taken stops the core where it stands, so nothing is ever printed」 |
| 内核留什么 | 全部 | 只留"不属于 bootloader"的部分：**交固件给 MCU、建地址空间、跟 global interface 握手**（`:587-588`） |

**可迁移的是这条判断规则本身**：一个块的寄存器在它自己的时钟停着时能不能安全写 —— 不能，就放 loader；能，就放内核。MT6768 用另一种方式表达了同一条规则：`hakios.mtk_connsys.c:17-19`「a read of a powered-off block of this kind can hang the bus」，所以先查 SPM 域位再读。

另外两条更细的、可以直接对照的：

- **域的数量不能贪多**：`BOARD_MALI_DOMAINS` 默认从 12 改成 2（`board.h:681-683`），因为 12 会卡在 mfg5 的 pending ACK（`0x111e`）并把域卡在半开，板子根本不回 Android。MT6768 只有 4 个域、全抬（`hakios.mali.c:525-534`），所以那边从来没撞上这个问题 —— **不能因为 MT6768 四个域全抬没问题，就认为 MT6895 十二个域也能全抬**。
- **清理必须保留**：`board.h:594-612` 记的是「留 GPU 上电就回 Android → Android 的 gpufreq 发现异常域状态 → 复位板子 → 重载同一张镜像」。对应 MT6768 的做法是 `xnu_returned()` 里**先 `mali_power_down()` 再 `chain_android()`**（`mtkloader.c:2522-2530`，注释 `:2523-2526` 说的是同一件事）。**这一条 MT6768 已经做对了，照抄即可。**

### P0 — GPU：把"建 buffer"和"改地址空间"解耦

`hakios.mali.c:414-424` 的 `TRANSCFG_WALK_NC` + `mali_fastmap` 默认 1：**GPU 不缓存页表遍历 → 新建映射不需要 `AS_COMMAND=UPDATE`**，只有**删**映射才需要（`:856-860`）。实测收益写在注释里：2.4 ms/个 buffer → 0（run 139，5 秒 320 个 buffer）。

MT6895 侧现状：`work-notes-gpu-status.md:34` 记录已采用 `AS_TRANSCFG = 0x42000006`（去掉 panthor 的 INA_BITS），与 MT6768 的 `TRANSCFG_WALK_NC` 是同一个思路 —— **这条路已经走对了，可以确认它是从 MT6768 抄过来的**。

### P0 — MMU：4 KB GPU 页 vs 16 KB 内核页

`hakios.mali.cpp:312-318`：`for (offset = 0; offset < size; offset += 4096) { getPhysicalSegment(from+offset) → hakios_mali_map(address+offset, phys) }`，注释明写「the GPU's pages, a quarter of the kernel's 16 KB ones」。`hakios.mali.c:841` 的 unmap 循环同样按 4096 步进。

这是 **XNU on AArch64 特有**的坑（Linux 上 4 KB 页没这问题），任何 Mali 移植都会撞上。

### P1 — 驱动接入的机械流程（零风险，直接照做）

四步，全在 `docs/porting/drivers.md:17-77` 写死：

1. 文件放 `mtk/<soc>/drivers/<subsystem>/hakios.<name>.c`，头注释写清楚「每个地址从哪来、实测到什么」（`:19-20`）。
2. `conf` 里加 `driver NAME` + `file bsd/dev/...` + `help`（`:55-61`）。
3. 启动方式二选一（`:70-76`）：**首选** XNU 的 `STARTUP(EARLY_BOOT, STARTUP_RANK_LAST, my_init);`；只有顺序敏感时才挂到 hub（`hakios_input_init()` / `hakios_audio_init()`），且**必须**在 `drivers/core/hakios.stubs.c` 补空实现，否则链不过。
4. `CONFIG_HAKIOS_<NAME>=y` 进 `<soc>.config`。

我们 MT6895 的 5 个驱动**已经全部按这套做了**（`stubs.c:61/65/74/78` + `input.c:233/236/243`），可以确认这套流程本身没问题。

### P1 — 日志三层，尤其"必须完整"的那一层

我们的处境（xaga 无 UART、无 pstore、屏幕一页）与 MT6768 一模一样，而 MT6768 **为此专门做了 logbook + pmsg + kept 环三层**：

- logbook 在 `xnusys` 尾部 8 MiB，`mtkloader.c:1748-1869`；读回脚本 `mtk/tools/xaga-pull-log.sh` 已经存在于我们的树里（49 行，`SKIP=653560 COUNT=1025`），**带校验和比对**（`:45`）。
- 内核侧 `kern.hakios.host_keep` 的 0xff00 缓冲（`hakios.mali.c:125-135`）解决的是「head+tail 截断把中间掐掉」的问题——**我们 GPU 19 轮的 probe 行很可能就是这么丢的**（这是推断，不是读到的结论）。
- `kept:` 96 行环每 60 秒重打（`hakios.mali.c:1105-1109`），保证长跑尾部一定带上。

### P2 — PMIC/SPMI 的"写后读回 + 熔断"

`hakios.mic.c:842-876` 的 `codec_put` / `codec_bits`：**每次写都读回比对，一次不符 `broken = true` 全线停手**。对 MT6368 的 accdet 尤其合适——我们现在的 `hakios.mt6895_headset.c:84` 只是 `pmif_wr(SWINF_VLDCLR, 1)`，没有任何写后校验。

### P2 — 只读优先 + 分级 boot-arg

全仓库的硬规矩（`AGENTS.md:38-40`、`docs/porting/drivers.md:48`、`docs/drivers-config.md:90-93`）：

> 「Every hardware step is behind a boot-arg and does nothing without it … New hardware work follows the same pattern: **numbered steps, reading only first, each step's result said in the log.**」

现成的分级编号可直接沿用：`mali=1/2`(M0/M1)、`scp_step=1..7`、`connsys_step=1..11`、`mic=1..3`、`sound=1..4`、`speaker_probe=1/2`。

### P2 — 危险读的保护

`hakios.mtk_connsys.c:17-19`：**掉电的块读寄存器可能挂总线**，必须先查 SPM 的两个域位；`hakios.mali610.c:1514-1515` 同款（mfg1 MPU 已上电就早退）。我们耳机那边 `[XAGADOC] docs/headset-wip/DANGER-spmi-access.md` 记的「写错 SID 会导致设备掉线」是同一类风险的另一面。

---

## 5. 不能借鉴的（架构不同）

| 维度 | MT6768（可借鉴） | MT6895（我们） | 差异性质 |
|---|---|---|---|
| **GPU 架构** | Mali-G52 MC2，**Bifrost，无固件**（`hakios.mali.c:6`：「A Bifrost Mali needs no firmware」） | Mali-G610 MC6，**Valhall + CSF，必须由 host 载固件**（`hakios.mali610.c:2-3`） | 根本性：Bifrost 的 bring-up 只有「电源+时钟」，Valhall 多了 MCU、共享内存接口块、doorbell、固件容器 |
| **GPU 电源域** | 4 个（`hakios.mali.c:275-280`：MFG_ASYNC/MFG/CORE0/CORE1，SPM 偏移 `0x334/0x338/0x34c/0x310`） | **12 个**（`hakios.mali610.c:67-78`：`0x0ebc`–`0x0ee8`），SPM 也在 `0x1c001000` 而非 `0x10006000` | 寄存器基址与数量全不同 |
| **GPU 时钟** | 单 mux：`CLK_CFG_1` → `syspll_d3` = 364 MHz（`hakios.mali.c:522-523`） | 双 mux + PLL：`CLK_CFG_4/5/30` + `CLK_UPDATE` + MFGPLL @`0x13fa0000`（`hakios.mali610.c:115-132`） | 完全不同 |
| **GPU MMU** | 35 位 VA、3 级表（`hakios.mali.c:401-407`） | panthor 的 MMU，`AS_TRANSCFG 0x42000006`、`MEMATTR 0x00084c8d888d8f88`、`IDX_CACHE/NC/SHARED` 分索引（`work-notes-gpu-status.md:34-37`） | 属性编码不同 |
| **存储** | eMMC / MSDC0 @`0x11230000`，PIO + CMD17/18/12（`msdc.h`） | **UFS 3.1** @`0x112b0000`，`/dev/block/sdcNN`，**整个 SoC 没有 MSDC 窗口**（`mtk/README.md:95`） | 无共通信道 |
| **UFS 的 loader 实现** | 无 | `mtk/loader/ufs.h`（3201 行）：继承 LK 的 UTRLBA、停表、换自己的表、发 `READ(10)` 轮询 doorbell（`mtk/README.md:107-130`） | MT6768 没有这一段 |
| **PMIC 通道** | **PMIC wrapper / WACS2** @`0x1000d000`，MT6358，16 位寄存器号要 `>>1`（`mtkpwrap.h`） | **SPMI/PMIF** @`0x1c804000`，MT6363 + MT6368，**SID 5**、`PMIF_CMD_EXT_REG_LONG`、`SWINF_2`（`hakios.mt6895_headset.c:21-37`） | 完全不同的总线 |
| **按键** | keypad @`0x10010000` + PMIC 直连（`hakios.mtk_keys.c:26-38`） | keypad @`0x1c00e000` + MT6363 over SPMI（`mt6895/drivers/input/conf:8-10`） | 地址与 PMIC 全不同 |
| **触摸** | OmniVision TD4160 over SPI2，**需要 loader 送 100 KB 固件**（`hakios.tcm.c:6-10`） | Novatek NT36672E on SPI2 @`0x11012000`，**自带 flash 跑自己的固件，不用送**（`mt6895/drivers/input/conf:15-19`） | 完全不需固件下发 |
| **无线** | CONNSYS 一代 + MT6631，`connsys_step` 按 MT6768 标定（`hakios.mtk_connsys.c`） | CONNSYS **新一代**，`connsys_step` 全部失效（`mt6895.config:22-23`） | 固件与步骤全不同 |
| **SCP** | `0x105C0000`，firmware/SRAM 映射是 MT6768 的（`mt6895.config:19-21`） | 同样的块但地址不同，需自己的驱动 | 需重写 |
| **音频 codec** | **完全没做**（`device.conf:19` `status audio missing`） | MT6368（accdet 在里面） | MT6768 这一块是空白，**没有可抄的 MTK 音频代码** |
| **Qualcomm 线的 codec** | — | — | **WCD938x + SoundWire + LPASS/ADSP**（`hakios.mic.c:9-19`），MTK 是 **MT6368 + SPMI/PMIF + 无独立 ADSP 路径**；`hakios.adsp.c` 3306 行整个是 QMI/AFE 体系，一行都用不上 |
| **启动方式** | LK 有 `boot:` 命令？→ `xnu-cycle.sh` 可 RAM 起（`mtk/README.md:152-157` 说的是 xaga 没有） | **`lk_a` 不注册 `boot:` 命令**，每次跑都必须写 `boot_a`，且无 recovery 分区（`mtk/README.md:152-157`） | 刷机风险完全不同 |
| **日志通道** | ramoops + pstore（`/sys/fs/pstore/console-ramoops-0`） | **无 ramoops、无 pstore**（`xaga-pull-log.sh:4-6`），只能靠 xnusys 尾部 logbook | 取证路径不同 |

---

## 6. 明确没找到的

1. **`oosexgang/hakios-mt6895` 仓库不存在。** `api.github.com/repos/oosexgang/hakios-mt6895` → 404；`search/repositories?q=hakios+mt6895` → 只有 `lizi600jin/hakios-xaga`。§0.1 的 7 个仓库里也没有。
2. **`origin/mtk` 上没有 `mtk/mt6895/`。** `git ls-tree -r --name-only origin/mtk -- mtk/mt6895` → 空。我们的 MT6895 层（10404 行）**全部未提交、未推送**。
3. **没有找到任何 HakiOS 的 3.5 mm 耳机/插拔检测实现。**
   - `[XNUK]` 全树 grep `\bjack\b|accessory|headset|mbhc` → 0 命中。
   - `main` 的 879 个 blob 里过滤 `jack|headset|accdet|mbhc|codec` → 0 命中。
   - `search/repositories` 的 `hakios+headset` / `hakios+accdet` / `hakios+3.5mm` → 全 0。
   - 唯一的 MTK 侧实现是我们自己的 `hakios.mt6895_headset.c`（134 行，**只读 dump**，`headset=2` 的初始化与轮询在 conf 里自己写 "remain TODO"，`drivers/audio/conf:8`）。
4. **MT6768 仓没有音频驱动。** `[DEV6768] device.conf:19` `status audio missing`；`kernel/README.md` 的驱动表（`:9-21`）没有任何 audio 项；`[MTK树] mtk/mt6768/drivers/` 只有 `gpu/input/storage/wifi` 四个子系统。
5. **没有找到 `work-notes-*.md` 的 MT6768 版本。** 24 个 `work-notes-*.md`（18463 行）**全部在 `[MTK树]` 根目录**，内容全是 MT6895/xaga 的 GPU 追查；MT6768 的踩坑记录在 `[DEV6768] docs/` 的 8 个文件里（`wifi-x6826b.md` 616 行最长），没有以 `work-notes` 命名。
6. **没有找到 MT6768 的 `docs/porting/` 或 `docs/boot-args.md` / `docs/drivers-config.md`。** 这三个文档在**公共树** `[MTK树] docs/` 下（`boot-args.md` 440 行、`drivers-config.md` 144 行、`porting/{index,drivers,repositories}.md`），不在设备仓。`[DEV6768] docs/` 只有 8 个设备专属文档。
7. **没有找到 `hakios-mt6768` 的设备树 / 固件 / 截图。** 这是**刻意**的：`AGENTS.md:44-55` 的规则 1–2 明写「Nothing of Apple's in the tree」「Nothing of the vendor's」，`LEGAL.md` 与 `README.md:50-54` 也重复。
8. **没有找到 VTOR / MPU_CTRL 的实际写入代码。** `work-notes-csf-mcu-window.md:38` 把它列为**最强嫌疑**并给出对照证据（MTK 固件 text 段 0 命中 vs `arch10.8` 各 1 命中），但代码里**没有**写 `0xE000ED08` / `0xE000ED94` 的实现。`grep -n VTOR mtk/mt6895/drivers/gpu/hakios.mali610.c` → **零命中**（exit 1）；`grep -n "MPU_CTRL\|0xE000ED"` → 也**零命中**。
9. **没有找到 `hakios-metal` 里与 Mali 相关的任何东西。** 它是纯 Adreno（`hakios-metal/README.md` 全文，`pm4.hpp` / `a6xx.hpp` / `ir3`），Mali 的 Metal 走的是 `AIR → NIR → panfrost`（`docs/metal-x6826b-plan.md:5`），**两条线不共用 Metal 驱动**。
10. **没有找到 `hackios-xnu-userland` 的任何内容。** 仓库 size = 0，`language = None`，最后 push 2026-10-25。
11. **没有找到 MT6895 的 `docs/`（在公共树里）。** `[MTK树] docs/devices/` 只有 `index.md / qemu.md / redwood.md / x6826b.md`，**没有 xaga.md**；`mtk/README.md:84-149` 是 MT6895 唯一的说明文字（且只到「loader 有了、kernel 层还没有」）。
12. **`xaga-*` 系列工具在公开仓库里没有。** `git ls-tree -r --name-only origin/mtk -- mtk` 列出的 45 个文件里**一个 `xaga-*` 都没有**；本地 `mtk/tools/` 实读有 `xaga-auto.sh` / `xaga-carve.sh` / `xaga-cycle.sh` / `xaga-pull-log.sh` / `xaga-first-boot.md` / `xaga-input.md`，`git status --porcelain` 逐个核对，六个**全是 ` A `（已 `git add`、未 commit）** —— 即**这套 xaga 工具链只存在于本地，没有一处进过 GitHub**。`mtk/README.md:152-215` 对 `xaga-cycle.sh` 和 `xaga-carve.sh` 有完整文档（含 `--gib 2.5` 的理由、GPT 双备份改写流程），但文档描述的文件在公开树里不存在。

---

## 附：本次实读的关键文件清单（供复核）

| 路径 | 读到的行范围 |
|---|---|
| `[MTK树] mtk/mt6768/drivers/gpu/hakios.mali.c` | 1–22, 42–54, 72–135, 137–195, 233–300, 302–344, 346–384, 386–424, 426–510, 512–580, 582–643, 645–760, 760–808, 810–881, 883–951, 953–1090, 1092–1146 |
| `[MTK树] mtk/mt6768/drivers/gpu/hakios.mali.cpp` | 1–57, 59–120, 124–162, 164–198, 200–244, 246–284, 286–334, 336–362, 364–400 |
| `[MTK树] tools/mesa/hakios-drm/hakios.drm.c` | 1–120 |
| `[MTK树] mtk/mt6768/drivers/{gpu,input,storage,wifi}/conf` | 全文 |
| `[MTK树] mtk/mt6768/configs/mt6768.config`、`soc.conf` | 全文 |
| `[MTK树] docs/drivers-config.md` | 全文（144 行） |
| `[MTK树] docs/porting/drivers.md` | 1–80 |
| `[MTK树] mtk/README.md` | 全文（215 行） |
| `[MTK树] mtk/loader/mtkloader.c` | 1–120, 150–330, 1540–1700, 1730–1880, 2432–2560, 2776–2960, 3540–3660 |
| `[MTK树] mtk/loader/{mtkpwrap.h, msdc.h, mtkmali.h}` | 1–80 / 1–80 / 1–120 |
| `[MTK树] mtk/tools/{xnu-cycle.sh, xaga-pull-log.sh, xaga-cycle.sh, fbshot.py, frames.py}` | 全文或头部 |
| `[MTK树] Makefile` | 60–175 |
| `[MTK树] devices/{manifest.conf, xaga/device.conf, xaga/configs/xaga.config}` | 全文 |
| `[MTK树] mtk/mt6895/{soc.conf, configs/mt6895.config, drivers/*/conf}` | 全文 |
| `[MTK树] mtk/mt6895/drivers/audio/hakios.mt6895_headset.c` | 全文（134 行） |
| `[MTK树] mtk/mt6895/drivers/gpu/hakios.mali610.c` | 1–34, 53–212, 390–441, 1150, 1465, 1514–1515, 1623, 3482, 3635–3741, 3968, 4019, 4092, 4148, 4578, 4887 |
| `[MTK树] work-notes-gpu-status.md`、`work-notes-mcu-noexec.md`、`work-notes-csf-mcu-window.md` | 1–45 / 1–44 / 1–41 |
| `[XNUK] drivers/README.md`、`drivers/audio/conf` | 全文 |
| `[XNUK] drivers/audio/hakios.audio.c` | 1–80 |
| `[XNUK] drivers/audio/hakios.mic.c` | 1–60, 795–945 |
| `[XNUK] drivers/audio/hakios.speaker.c` | 1–60 |
| `[XNUK] drivers/audio/hakios.sound.c` | 1–43 |
| `[XNUK] docs/metal-plan.md` | 1–40 |
| `[XNUK] docs/driver-review/sound.md` | 目录级（标题行号） |
| `[DEV6768] README.md`、`AGENTS.md`、`device.conf`、`kernel/README.md` | 全文 |
| `[DEV6768] docs/{metal-x6826b-plan.md, x6826b-emmc.md, x6826b-wallpaper.md}` | 全文 |
| `[XAGADOC] docs/headset-wip/ROUND-20-VERDICT.md` | 1–60 |
