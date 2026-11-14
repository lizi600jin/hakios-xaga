# SLNeko-XNU 仓库调研

调研对象：`https://github.com/ShuiLan114514/SLNeko-XNU`
调研方式：`git clone --depth 1` → `/tmp/SLNeko-XNU`，再 `git fetch --unshallow` 取到全部历史（只有 4 个提交）。
仓库快照：HEAD = `e764aac docs(xaga): update status for audio, keys and connectivity`（2026-11-14），工作树 55 MB。
全文引用均为实际读到的内容，行号对应上面这个 HEAD。

---

## 0. 一句话：这是什么（是不是 XNU 移植）

**是真正的 XNU 移植，而且就是我们这条线的同一个项目**：它是 **HakiOS 的 `xaga`（红米 Note 11T Pro / MT6895）移植源码树**，
把 Apple 开源的 `xnu-8796.121.2`（iOS 16.5 / Darwin 22.5.0 内核）跑在同一台机器上。

`README.md:5-10`：

```
This is the source tree for the **xaga** port of HakiOS: Apple's open-source Darwin/XNU
(``xnu-8796.121.2``, the iOS 16.5 kernel) booting on a Xiaomi Redmi Note 11T Pro / POCO X5 Pro
(MT6895).  The iOS 16.5 user space (20F66) runs unmodified: launchd → backboardd → SpringBoard.

The tree is derived from the HakiOS `mtk` branch.  The original upstream documentation is kept in
[`README.hakios.md`](README.hakios.md) and [`docs/`](docs/); this README only covers the xaga work.
```

即：**不是竞品，是同源分叉**。上游是 `oosexgang/hakios-xnu-kernel` 的 `mtk` 分支（`README.hakios.md:40`），
示例机是 Infinix Hot 20（X6826B / MT6768 / Helio G85），本仓库把同一套代码往 MT6895/xaga 上搬。

⚠️ **一个必须知道的事实**：这棵树里**大部分驱动仍然是 MT6768 / POCO X5 Pro（高通 SM7325，"redwood"）的**，
真正 xaga/MT6895 专属的只有少数几个文件（见 §3.2）。README 的「状态表」是 xaga 视角，
但代码里能跑在 xaga 上的部分远少于文件数量给人的印象。

⚠️ **另一个更麻烦的事实**：README 把 "Redmi Note 11T Pro" 和 "POCO X5 Pro" 并列成同一台机器，
但**这是两台不同 SoC 的手机**（xaga=MT6895 vs redwood=高通 SM7325）。详见 §1.2。
**在这棵树里看到 `redwood` 字样，默认指的是那台高通机，不是 xaga。**

---

## 1. 基本信息

| 项 | 值 | 出处 |
|---|---|---|
| XNU 版本 | `xnu-8796.121.2`（iOS 16.5 内核，Darwin 22.5.0） | `xnu/README.md:3`、`README.md:6` |
| 目标设备 | Xiaomi Redmi Note 11T Pro / POCO X5 Pro（代号 **xaga**） | `README.md:6` |
| SoC | MediaTek **MT6895**（天玑 8100） | `README.md:30` |
| 面板 | 1080×2460，`v_display=1` 图形模式 | `README.md:35` |
| iOS 用户空间 | 16.5（20F66），launchd → backboardd → SpringBoard，未改动 | `README.md:7` |
| 构建产物 | `xnu-src/xnu-8796.121.2/BUILD/obj/DEVELOPMENT_ARM64_BCM2837/mach.development.bcm2837` | `xnu/README.md:44` |
| 代码量 | `xnu/redwood.patch` 133 KB（改 Apple 原有 60 个文件）；`xnu/files/` 按 `xnu/README.md:6` 是 37 个新文件，实测 **122 个文件**（差的那些是 libdeflate/libzbitmap 等第三方，按目录一起放进来的） | `xnu/README.md:5-6` + `find` |
| 许可 | XNU 派生部分 APSL 2.0，其余 MIT | `README.md:98-100` |
| 历史 | 4 个提交，2026-11-11 起 | `git log` |

### 1.1 完成度（README 状态表原文，`README.md:14-26`）

| Area | State（原文要点） |
|---|---|
| Boot | XNU 8796.121.2 经 `mtkloader` 从 `boot_a` 启动；all-root/App launchd 可用 |
| Display | Sony/Novatek 面板在图形模式工作，SpringBoard 能画 Hello/OOBE |
| Touch | Novatek **NT36672E** on SPI2（GPIO 109–112，IRQ GPIO135），固件 `novatek_nt36672e_l16_fw01.bin`；**fw02 不能用**（能下载但 baseline 停在 `a1`，只报 `0x77` 填充） |
| OOBE | Hello → 上滑 → 主屏 |
| Logs | loader 把 HLOG logbook 写到 `iosroot` 尾部预留区最后 8 MiB（预留区共 200 MiB） |
| Battery | MT6375 on I2C5（GPIO33/34 **bit-bang**），VBAT + PWR_RDY + CHG_STAT，电压估算 SOC；库仑计未实现 |
| Audio | AFE DL1/I2S3 + 两颗 TFA9873（I2C6 `0x34`/`0x35`，rev `0x0b73`）工作；`mediaserverd` 能到 `/dev/redwood-audio` |
| Keys | 电源/音量加走 MT6363 PSC 状态 `0x091b`；音量减走 SoC keypad `KP_MEM1` bit 0。三键均工作 |
| Display Zoom | IOMFB selector 53，960×2079 画布 + 侧裁 + 触控补偿 |
| Wi-Fi/BT | 进行中；确认 `soc7_0` 固件族、WFDMA 约 `0x18024000`；**尚无可用的 Wi-Fi/BT 驱动** |
| Not a goal | 蜂窝、相机采集、Apple 在线服务/激活 |

对比上游 `README.hakios.md:16-34`（Infinix Hot 20，成熟得多）：那边已经有 Mali 驱动 + panfrost + Metal、Wi-Fi 能上网、
Sileo/apt 可用。**xaga 这一支明显更早期**。

### 1.2 ⚠️ README 把三台不同的手机混成了一台（读这份仓库最容易踩的坑）

`README.md:6` 写的是 "Redmi Note 11T Pro / POCO X5 Pro (MT6895)"。**这句话是错的**：

- **Redmi Note 11T Pro = xaga = MT6895**（天玑 8100）——本仓库声称的目标；
- **POCO X5 Pro 5G = redwood = 高通 SM7325**（骁龙 778G）——**完全是另一颗 SoC**。

代码里的证据（`redwood` 这个代号在仓库里指的是**那台高通机**）：

| 证据 | 内容 |
|---|---|
| `uefi/files/Platforms/Xiaomi/redwoodPkg/redwood.dsc:53-56` | `PcdSmbiosSystemModel\|"POCO X5 Pro 5G"`、`PcdSmbiosSystemRetailModel\|"redwood"`、`PcdSmbiosSystemRetailSku\|"POCO_X5_Pro_5G_redwood"` |
| `uefi/files/Platforms/Xiaomi/redwoodPkg/redwood.dsc:29-31` | `# 0 = SM7325` / `# 1 = SM7325-AE` / `# 2 = SM7325-AF` |
| `uefi/files/Platforms/Xiaomi/redwoodPkg/redwood.fdf:101` | `# SECTION RAW = redwoodPkg/FdtBlob/sm7325-xiaomi-lisa.dtb` |
| `xnu/files/bsd/dev/redwood_adreno.c:2` | `The GPU of the POCO X5 Pro (redwood), an Adreno 642L` |
| `xnu/files/bsd/dev/redwood_sound.c:3` | `Every address and number below belongs to the POCO X5 Pro (redwood)` |
| `xnu/files/bsd/dev/redwood_speaker.c:2` | `The speakers of the POCO X5 Pro (redwood)` |
| `xnuloader/loader.c:3064-3066` | 序列号按 `plat->name` 分三支：`"x6826b"` / `"renoir"` / 其余→`"RDW00POCOX5P"` |

**所以这棵树实际是三台机器的合体：**

| 代号 | 机型 | SoC | 在这棵树里的地位 |
|---|---|---|---|
| `x6826b` | Infinix Hot 20 | MT6768 | **上游主力**，`mtkloader/`、`docs/` 全部是它 |
| `redwood` | POCO X5 Pro 5G | 高通 SM7325 | **代码量最大的一支**（Adreno / UFS / ADSP / WCD938x / UEFI redwoodPkg 全是它） |
| `xaga` | Redmi Note 11T Pro | MT6895 | **只有 4 个驱动文件 + README**（§3.2） |

**另外：`POCO X4 GT`（和我们同平台的另一台 MT6895）在整个仓库里零命中**（`rg -i 'X4 GT|x4gt'` 无结果）。

> 对我们的直接含义：
> 1. 我们自己的设备表里 `POCO X4 GT = MT6895 = xaga 同平台` 是对的，**不要被这个 README 带偏**。
> 2. 看到这棵仓库里"redwood"字样时，**默认它指高通那台**，不是 xaga。`redwood_` 前缀在这里已经退化成"这个项目的命名空间"，
>    不再代表机型——这是它们命名上的一个真实缺陷，我们抄目录结构时别抄这个习惯。

---

## 2. 启动链 / bootloader 设计

### 2.1 全景

```
LK (preloader/bootloader, 已把 DRAM/显示/存储带起来)
  └─ boot_a 里的 Android boot image
       ├─ [0, CHAIN_OFFSET)         mtkloader（裸机 aarch64，MMU off）
       ├─ [CHAIN_OFFSET, ...)       Android 原来的 arm64 Image（0x40000 起）
       └─ [payload_off, ...)        "XNUP" payload（kernel + ramdisk），16 对齐
```

打包器 `tools/chainboot.py:1-13`：

```
LK decompresses the kernel to 0x40080000 and enters it there. The new kernel is gzip of:
program.bin, padded to CHAIN_OFFSET, then Android's own (uncompressed) Image. The program runs
first and then moves the Image down to 0x40080000 and starts it (mtkloader/fbdiag.c). An optional
payload (mtkloader's "XNUP") follows the Image, 16-aligned.
```

`CHAIN_OFFSET = 0x40000`（`tools/chainboot.py:17`）。注意：`docs/touch-handover.md:107-108` 记录了**为什么**从 `0x20000`
抬到 `0x40000`——触控固件太大塞不下了。这个"改偏移要三处同步"的坑值得记：`tools/chainboot.py`、`mtkloader/mtkloader.c`、`mtkloader/fbdiag.c`。

`mtkloader/mtkloader.c:1-24` 自述（**这段是整个启动链设计最凝练的说明**）：

```
 * mtkloader: boots XNU on the Infinix Hot 20 (X6826B, MT6768) from boot_a, and brings Android
 * back afterwards with XNU's log in pstore.
 *
 * The boot image's kernel (tools/chainboot.py) is this program, then Android's own kernel Image
 * at CHAIN_OFFSET, then the XNU payload ("XNUP": kernel, ramdisk). Each boot, by the marker:
 *
 *   none     RUN: load XNU, give it LK's framebuffer, arm the watchdog, start it.
 *   RUNNING  XNU was cut short by the watchdog: copy its log from DRAM into ramoops, start
 *            Android.
 *   DONE     XNU came back to us (panic, early exception, orderly end) and its log was saved
 *            then: start Android.
 *
 * The log ends up in /sys/fs/pstore/console-ramoops-0 under Android. See mtkdbg.h for the
 * screen and ramoops output.
```

### 2.2 三态标记（**防砖核心设计，强烈建议抄**）

`mtkloader/mtkloader.c:145-148`：

```c
#define MARKER_PA           0x80000000ULL
#define MARK_RUNNING        0x4e52584eu /* "NXRN" */
#define MARK_DONE           0x4e44584eu /* "NXDN" */
```

写在 DRAM 里，**三重冗余**（`marker_set` 写 3 份，`marker_get` 三份投票，`mtkloader.c:387-399`），跨 warm reset 存活。
主循环 `mtkloader.c:3368-3385`：

```c
	uint32_t mark = marker_get();
	...
	if (mark == MARK_DONE) {
		/* XNU's log was saved when it came back; leave ramoops as it is. */
		chain_android();
	}
	if (mark == MARK_RUNNING) {
		rescue_log();
		dbg_delay_ms(5000);
		chain_android();
	}
```

含义：**XNU 一旦被看门狗掐死，下一次上电 loader 看到 `NXRN` 就直接回 Android，不会再试一次 XNU**。
这就是"刷一次机不会把手机刷成砖"的根本机制——`README.md:75-77` 里"只写 `boot_a`，绝不碰 `boot_b`"是第二道保险。

### 2.3 一次性启动请求（不要用按键）

`mtkloader/mtkloader.c:2966-3005`：

```c
/*
 * Android unless asked for XNU, once. The keypad's state registers change by themselves (a scan
 * pattern), so keys are not used. The request is a line written to /dev/pmsg0 under Android
 * (tools/boot-xnu.sh): ramoops keeps it in its pmsg zone across the reboot, where this finds it
 * and wipes it, so the boot after XNU is Android again.
 */
static const char boot_xnu_flag[] = "DARWIN-REDWOOD:BOOT-XNU";
```

`pmsg_take()` 找到就把该串整段改成 `-`，保证只生效一次（`mtkloader.c:2974-3000`）。
Android 侧触发只要两行（`tools/boot-xnu.sh:6-8`）：

```sh
adb -s $S shell 'su -c "printf DARWIN-REDWOOD:BOOT-XNU > /dev/pmsg0"'
adb -s $S reboot
```

> **这条对我们直接可用**：黑屏时不用盲猜，只要"这一次请求 → 跑一次 XNU → 自动回 Android"，
> 一轮成本 = 一次重启，不需要人守着按 BROM 救砖。

### 2.4 内存布局与 boot_args

`mtkloader/mtkloader.c:29-45`（**注意这仍是 MT6768 的地址，xaga 上不能照抄**）：

```c
/* XNU's view of memory: one contiguous range that nothing else (LK, Android, TEE, ATF, the
 * framebuffer) uses; 0xc0000000-0xfe000000 is free DRAM on this board. */
#define XNU_PHYS_BASE       0xc0000000ULL
#define XNU_MEM_SIZE        0xfe000000ULL
#define SYSTEM_PA           (XNU_PHYS_BASE + 0x08000000ULL)
#define KERNEL_VIRT_BASE    0xfffffff004000000ULL
#define XNU_SLIDE           (XNU_PHYS_BASE - KERNEL_VIRT_BASE)
```

boot_args 填充在 `mtkloader.c:2285-2306`，关键一行：

```c
	ba->video.v_display = XNU_VERBOSE ? 0 : 1; /* 0: the kernel's text console, 1: for iOS to draw */
```

**这就是"黑屏"的一个天然来源**：正常（非 verbose）配置下 `v_display=1`，内核文本控制台**不画**，
屏上只有 iOS 自己画的东西。所以"黑屏"既可能是内核挂了，也可能是内核活着但还没轮到 SpringBoard 画。
**排查第一步应该先 `-DXNU_VERBOSE=1` 换成文本控制台**（`mtkloader.c:109-115、134-137`）。

### 2.5 显示交接（黑屏排查的另一半）

`mtkloader/mtkloader.c:406-444`：

```c
/* Opaque pixels only from here on: XNU's console writes alpha 0. */
static void
display_alpha_off(void)
{
	uint32_t con = rd32(OVL0_BASE + OVL_L0_CON);
	wr32(OVL0_BASE + OVL_L0_CON, con & ~(1u << 8));
	...
	/*
	 * LK composes two layers: layer 0 is the picture, and layer 3 is its own overlay with the
	 * warning it prints on an unlocked phone ("Orange State: your device has been unlocked"),
	 * from a buffer of its own. Nothing of iOS writes to that buffer, so the words stayed on
	 * top of the home screen. Leave the overlay's layers out of what the hardware composes.
	 */
	uint32_t src = rd32(OVL0_BASE + OVL_SRC_CON);
	wr32(OVL0_BASE + OVL_SRC_CON, src & 1u);
	wr32(DISP_MUTEX_EN, 1);
	wr32(DISP_MUTEX_RUN, 1);
```

两件事：**(a)** XNU 控制台写 `0x00rrggbb`（alpha=0），必须关掉 OVL0 的 per-pixel alpha，否则屏上什么都没有；
**(b)** LK 的 layer 3（"Orange State"解锁警告）要踢出合成。`mtkloader.c:17-23` 的注释把这条写成了完整的踩坑记录：

```
 * Display: the panel is video mode and keeps scanning OVL0 layer 0 (LK's framebuffer,
 * 0x787b0000, pitch 2944 = 736 pixels). LK enables per-pixel alpha on that layer, and XNU's
 * console writes 0x00rrggbb pixels: fully transparent. So per-pixel alpha is switched off, and
 * XNU's color table gets alpha 0xff besides.
```

### 2.6 看门狗（自动回 Android）

`mtkloader/mtkloader.c:2325-2338`：

```c
	if (XNU_WDT_SECONDS) {
		uint32_t t = XNU_WDT_SECONDS > 31 ? 31 : XNU_WDT_SECONDS;
		marker_set(MARK_RUNNING);
		wr32(TOPRGU_LENGTH, ((t * 32768u / 512u) << 5) | 0x8u);
		wr32(TOPRGU_RESTART, TOPRGU_RESTART_KEY);
		wr32(TOPRGU_MODE, TOPRGU_MODE_KEY | 0x1u);
		...
	} else {
		marker_set(0);
	}
```

`TOPRGU_RESTART_KEY 0x1971`、`TOPRGU_MODE_KEY 0x22000000`（`mtkloader/mtkdbg.h:29-33`）。
`mtkdbg.h:95-96` 注明这是 warm reset，**DRAM 与 pstore 都活着**——这是"日志能跨崩溃活下来"的物理基础。

### 2.7 内核侧的对称设计：`loader_return`

`xnu/files/osfmk/arm64/loader_return.c:1-13`：

```
 * On hardware without a serial port there is no way to get the kernel log
 * out. The XNU loader therefore leaves UEFI intact (no ExitBootServices) and
 * publishes the physical address of a return stub in /chosen/loader-return.
 * When the kernel has nothing more to do (panic, halt, timer) it collects its
 * message buffer, the panic buffer and any kprintf output, maps the stub 1:1
 * through TTBR0 and jumps to it with that log. The loader then restores the UEFI environment and writes
 * the log to storage.
 *
 * Only valid while the kernel does not drive hardware that UEFI also uses.
```

返回路径的四条触发口：
1. `loader_return_after=<秒>` 定时（`loader_return.c:415-473`）——"内核没崩只是在等，也能把日志要回来"
2. panic（`loader_return.c:396-402`，把 panic buffer 附在日志尾巴）
3. `loader_live.stop` 轮询（`loader_return.c:475-508`）——主机 `xl stop`
4. checkpoint（`loader_return.c:406-413`，`loader_stub_slot[2]` 匹配就停在指定点）

离开前**必须**逐硬件 quiesce，否则 UEFI 再接手会炸（`loader_return.c:378-386`）：

```c
	redwood_ufs_quiesce();          /* this kernel's requests to the storage done, UEFI's list as it was, for the log */
	redwood_adreno_quiesce();       /* the GPU block, if it was started, has tables in memory that is about to be reused */
	redwood_wifi_quiesce();         /* the Wi-Fi DSP, if it still runs (a panic), is not left to itself */
	redwood_adsp_quiesce();         /* nor the audio DSP */
	redwood_ddr_quiesce();          /* and the vote for the memory's bandwidth taken away */
	redwood_haptic_quiesce();       /* the vibration motor's chip back into reset, if it was let out */
	redwood_torch_quiesce();        /* the flashlight off, if it was lit */
	redwood_camera_quiesce();       /* the camera powered down, if it was powered up */
```

以及**关 MMU 前必须刷 cache**（`loader_return.c:273-279`）：

```c
	/*
	 * The stub turns the MMU off, and on real hardware dirty cache lines do
	 * not survive that: write the log back to RAM first.
	 */
	for (uintptr_t p = (uintptr_t)loader_log & ~63UL; p < (uintptr_t)loader_log + loader_log_len; p += 64) {
		__asm__ volatile ("dc cvac, %0" : : "r" (p));
	}
```

### 2.8 双核看门狗（kernel 侧）

`xnu/files/osfmk/arm64/loader_return.c:510-518、568-589`：

```
 * boot-arg hv_wdt_pet=1: the loader has armed the Qualcomm hypervisor's
 * virtual watchdog (it resets the phone when a kernel hangs, so that test
 * runs need nobody at the device). Its timeout is at most 65 seconds; feed
 * it from a timer so that a healthy kernel can run for as long as it likes.
```

```c
#define VIRT_WDT_PET            0x86000007u
#define HV_WDT_PET_SECONDS      10
```

每 10 秒 `smc #0`（x0=`0x86000007`）喂狗，同时给 `loader_live.heartbeat` 加一（`loader_return.c:583`）。
loader 端另有 watcher core（`mtkloader.c:50-70`）：

```c
/* Seconds after which the second core takes a snapshot of XNU and resets; 0: no watcher. */
#ifndef XNU_WATCH_SECONDS
#define XNU_WATCH_SECONDS   0
#endif
```

`mtkloader.c:62-66` 的注释值得完读——**这是"给内核多留一个核当看门狗"的坑**：

```
 * A watcher on a core the kernel wants is worse than none: PSCI CPU_ON answers "already on", the
 * kernel counts a processor that never checks in, and every rendezvous between cores waits for
 * it -- which is how a 15 minute run stayed on the Apple logo.
```

---

## 3. 驱动清单与目录组织

### 3.1 目录组织

驱动全部平铺在 `xnu/files/bsd/dev/`，**没有子目录分类**，统一 `redwood_` 前缀：

```
xnu/files/
├── bsd/dev/        <- 47 个 .c + 2 个固件 .h（主体）
├── bsd/kern/       <- kern_minimac.c（AMFI/Sandbox 桩）
├── bsd/miscfs/     <- apfs/（只读）+ ramfs/（内存可写）
├── bsd/pthread/    <- pthread_kext_static.c
├── bsd/sys/        <- 各驱动的 ABI 头（redwood_input.h / redwood_audio.h / redwood_net.h ...）
├── iokit/Kernel/   <- 闭源驱动的替代：MiniIOSurface / MiniIOMobileFramebuffer / MiniAppleKeyStore / RedwoodBattery / RedwoodGPU / RedwoodMali / RedwoodWLAN
├── libkern/amfi/   <- miniamfi.c / miniamfi_entitlements.cpp
└── osfmk/          <- arm64 平台层（GICv3 / PSCI SMP / loader_return / user_exception_trace）+ minicrypto
```

**注册方式**（`xnu/redwood.patch:68-108`，改 `bsd/conf/files`）：

```
+bsd/dev/redwood_input.c		standard
+bsd/dev/redwood_virtio_input.c	standard
+bsd/dev/redwood_goodix.c	standard
+bsd/dev/redwood_tcm.c	standard
+bsd/dev/redwood_nvt.c	standard
+bsd/dev/redwood_mtkkeys.c	standard
...
```

即：**每个驱动一个 `standard`，编译进去，运行时由 boot-arg 决定是否启动**。
调用点集中在 `xnu/files/bsd/dev/redwood_input.c:219-241`：

```c
	redwood_cpufreq_init();
	redwood_virtio_input_init();
	redwood_goodix_init();
	redwood_tcm_init();
	redwood_nvt_init();
	...
	redwood_buttons_init();
	redwood_mtkkeys_init();
	redwood_battery_init();
	redwood_xaga_battery_init();
	redwood_ufs_init();
	redwood_net_init();
	redwood_adreno_init();
	redwood_mali_init();
	redwood_connsys_init();
	redwood_scp_init();
	redwood_wifi_init();
	redwood_adsp_init();
```

**这个模式非常好用**：所有 `xxx_init()` 自己 `PE_parse_boot_argn` 判断要不要干活，不干活就直接 `return`。
想关掉某个驱动的噪声，只要不带那个 boot-arg 就行，不用改代码重编两套。

### 3.2 哪些真是 xaga 的，哪些是继承来的（**重要**）

我逐个看了文件头注释，结论如下：

**真 xaga / MT6895 专属（4 个 .c + 1 个头）：**

| 文件 | 说明 | 证据 |
|---|---|---|
| `xnu/files/bsd/dev/redwood_nvt.c`（64 KB） | Novatek NT36672E 触控，SPI2 bit-bang | `:2-3` "The touch screen of the Redmi Note 11T Pro (xaga)… Only with boot-arg touchscreen=3" |
| `xnu/files/bsd/dev/redwood_backlight.c`（14.7 KB） | RT4831 背光 + **xaga 的 TFA9873 / AFE 音频** | `:2` "LCD backlight for xaga"；`:360` "a first xaga speaker test: TFA9873 on I2S3" |
| `xnu/files/bsd/dev/redwood_xaga_battery.c`（9.4 KB） | MT6375 电量/充电，I2C5 bit-bang | `:2` "Battery and charger status for xaga (Redmi Note 11T Pro, MT6895)" |
| `xnu/files/bsd/dev/redwood.nvt_fw.h`（847 KB） | NVT 固件 blob（由 `tools/mknvtfw.py` 生成） | `redwood_nvt.c:52-53` |
| `xnu/files/bsd/sys/redwood_input.h` | 含 `redwood_nvt_init` / `redwood_xaga_battery_init` 声明 | `:45、:49` |

**注意：README 说的"MT6363 PSC `0x091b`"在三键驱动里找不到。**
`redwood_buttons.c` 是为 **POCO X5 Pro（高通 PMK8350/PM7325 SPMI）** 写的（`:1-30`），
`redwood_mtkkeys.c` 是为 **Infinix MT6768（MT6358 PWRAP + keypad）** 写的（`:1-11`），
两者都不含 MT6363，全文搜 `0x091b` 只命中 README（见 §9）。

**其余全部是继承来的，且多数只对别的机器有效：**

| 文件 | 目标机 | 证据 |
|---|---|---|
| `redwood_adreno.c`（168 KB） | **高通 Adreno**（POCO X5 Pro） | `xnu/README.md:84` |
| `redwood_sound.c` / `redwood_speaker.c` / `redwood_mic.c` / `redwood_adsp.c` | **POCO X5 Pro**（WCD938x + AW88261 + Hexagon ADSP） | `redwood_sound.c:2-4`、`redwood_speaker.c:2-3`、`redwood_mic.c:2-5`、`redwood_adsp.c:2-4` |
| `redwood_ufs.c` / `redwood_ufshc.c` | 高通 UFS（UFSDxe 共享） | `redwood_ufs.c:2-7` |
| `redwood_emmc.c` | X6826B eMMC | `:2` "The X6826B's eMMC" |
| `redwood_mali.c`（42 KB） | **Infinix 的 Mali-G52 MC2** | `:2-4` |
| `redwood_mtkkeys.c` | Infinix MT6768 | `:2-4` |
| `redwood_connsys.c` / `redwood_mtkwifi.c` / `redwood_wifi.c` / `redwood_wlfw.c` / `redwood_qmi.c` | MT6768 CONNSYS 或高通 WCN6750 | 各文件头 |
| `redwood_scp.c` | MT6768 传感器 Hub | `xnu/README.md:81` |
| `redwood_haptic.c` / `redwood_torch.c` / `redwood_camera.c` | MT6768 / 高通 | 各文件头 |

**`mtkloader/` 整个目录也是 MT6768 的**：`mtkloader.c:2` 明说 "boots XNU on the Infinix Hot 20 (X6826B, MT6768)"，
里面 `XNU_PHYS_BASE 0xc0000000`、`PANEL_W 720`、`PANEL_H 1640`、`LK_FB_BASE 0x787b0000` 全是那台机的值；
搜 `1080`/`2460`/`xaga` 在 `mtkloader/` 下**零命中**。

> 结论：**这个仓库里没有一份能直接烧到 xaga 上跑的 loader**。它提供给我们的价值主要是**方法、结构和调试机制**，
> 而不是可直接复用的 xaga 平台代码。

### 3.3 各驱动的“分步 boot-arg”写法（**最值得抄的工程习惯**）

几乎每个驱动都有 `xxx_steps=N`，第 N 步包含前面所有步，日志里**每一步做什么先说一句再说**。
`redwood_buttons.c:31-37`：

```
 * The bring-up is made in steps, each said in the log before it is made. Boot-arg
 * buttons_steps=N stops after the first N:
 *   1  map the arbiter and read its version             (the first access to that block)
 *   2  find the channels of the two peripherals         (reads of the arbiter's tables)
 *   3  ask each peripheral what it is                   (the first transfers on the bus)
 *   4  read how the keys' inputs are set up, and what they say now
 *   5  watch them                                       (the default)
```

实现在 `redwood_buttons.c:450-530`，模式是"每步之间 `if (steps < N) goto out;`"。
同样模式还有 `connsys_step`（`docs/wifi-x6862b.md:192`）、`scp_step`（`docs/sensors-x6826b.md:29`）、
`buttons_steps`、`wifi_step`（`docs/wifi-x6826b.md:155`）。

`docs/camera-mtk-porting.md:49` 把这个习惯的理由写透了：

```
- Each operation says (`say()`) what it is about to do **before** a step that could hang a bus; the last line of a log that ends is the step that hung.
```

**"日志最后一行 = 挂在哪一步"** —— 这句话是整个调试体系的设计前提。

---

## 4. 音频 / 耳机 / codec

### 4.1 结论先说

- **扬声器**：xaga 有一条能出声的路径（AFE DL1 → I2S3 → TFA9873 ×2），代码在 `redwood_backlight.c` 里（是的，背光文件里）。
- **耳机 / 3.5mm / accdet / MT6368**：**完全没有**。见 §9。

### 4.2 音频 ABI（内核 ↔ 用户态）

`xnu/files/bsd/sys/redwood_audio.h:1-23`：

```
 * /dev/redwood-audio: the sound iOS plays, on its way to whatever can make it heard.
 *
 * iOS expects its output devices from Apple's audio drivers through IOAudio2; neither exists
 * here. Instead the audio server (mediaserverd) is given a plug-in of ours that presents one
 * stereo output device, and the plug-in writes what the server has mixed for that device
 * here. In the kernel the samples go to a sink -- a driver for something that plays them --
 * and, if asked for, into a recording that outlives the kernel (see dev/redwood_audio.c).
 *
 *   write(): whole frames in the format below. Never blocks: the writer is the audio server's
 *            real-time thread, which paces itself (48000 frames a second while anything
 *            plays, nothing at all otherwise). What the sink cannot take is dropped.
```

格式固定：48 kHz / 2ch / S16_LE 交织（`:25-28`）。音量通过 ioctl 下行（`:30-40`）：

```c
struct redwood_audio_volume {
	uint32_t below;                 /* eighths of a dB */
	uint32_t muted;
};
#define REDWOOD_AUDIO_SET_VOLUME _IOW('r', 1, struct redwood_audio_volume)
```

sink 注册接口（`:47-51`）：

```c
typedef uint32_t (*redwood_audio_sink_t)(const int16_t *frames, uint32_t count);

void redwood_audio_set_sink(const char *name, redwood_audio_sink_t sink);
```

> **这个 ABI 设计对我们有直接价值**：不实现 IOAudio2 家族，而是给 `mediaserverd` 一个插件，
> 内核只提供一个"字符设备 + sink 回调"。`userland/src/audio_plugin.plist` 是那个插件的描述。

### 4.3 xaga 的扬声器实现（`redwood_backlight.c`）

**两颗 TFA9873 在 I2C6 `0x34`/`0x35`，rev `0x0b73`**（`redwood_backlight.c:234-238`）：

```c
/* ---- the two Goodix TFA9874 amplifiers of xaga, for a first hardware probe ----------------------- */

#define TFA_RST         213
#define TFA_SPK_SW      160
#define TFA_REV         0x03
```

（注释写 "Goodix TFA9874"，实际代码里判 rev `0x0b73` 并按 TFA9873 初始化，`README.md:36-37` 也说 TFA9873——注释与实现有出入。）

初始化序列是从 vendor 驱动抄的，**每一步都有出处注释**（`redwood_backlight.c:388-431`）：

```c
	/* the TFA9873 rev 0x0b73 init sequence from the vendor driver */
	tfa_write(address, 0x02, 0x0628); /* AUDFS = 48 kHz */
	tfa_write(address, 0x04, 0x0241);
	...
	/* TDM/I2S input: two 32-bit slots, 64 BCK per frame, 16-bit sample */
	tfa_write(address, 0x20, 0x2883); /* Android TDM: 2 slots, 1-BCK FS */
	...
	/* set MANSCONF (hardware manager enable), then leave power-down */
	(void)tfa_read(address, 0x01, &v);
	v |= (1u << 2);
```

AFE 侧寄存器序列（`redwood_backlight.c:519-536`）：

```c
	/* 16 KiB DL1 loop, initially silence, 48 kHz stereo 32-bit samples */
	uint32_t *samples = (uint32_t *)afe_sram;
	for (unsigned i = 0; i < 4096; i++) {
		samples[i] = 0;
	}
	aw(0x000c, 0x60000004);
	aw(0x0010, 0x00000001);
	aw(0x0020, 0x00000020);
	...
	aw(0x0054, 0x1e102000);
	aw(0x0064, 0x1e105fff);
	...
	aw(0x02ec, 0x00010001);
	aw(0x0310, 0x10000139);
	aw(0x0314, 0x00000001);
```

地址：`AFE_PHYS 0x1e100000`、`AFE_SRAM_PHYS 0x1e102000`（`:366-367`），
I2S3 引脚 `BCK 21 / LRCK 22 / DATA 24`（`:362-364`）。

**"AFE DMA 只在第一次写入时启动"**（`redwood_backlight.c:477-482`）——这是保证环路从头开始的小技巧：

```c
	if (!afe_started) {
		aw(0x1b00, 0x00000010); /* AFE_AGENT_ON: DL1_ON */
		afe_started = true;
		tfa_say("tfa: AFE DMA started\n");
	}
```

**欠载保护**（`redwood_backlight.c:487-499`）：300 ms 没写入就清零 SRAM、关 agent，避免爆音。

**音量教训**（`redwood_backlight.c:441-444`，以及 commit `17e8bb0`）：

```c
/* iOS already applies its software volume to these samples.  Keep them at
 * full scale here; the old /64 was 36 dB of extra attenuation and made the
 * speaker almost inaudible even at iOS volume 0.5. */
#define AFE_SINK_SHIFT  0
```

> **这一条值得直接记进我们的踩坑库**：iOS 侧已经做了软件音量，内核 sink 再做衰减就是**双重衰减**，
> 表现为"音量 0.5 时几乎听不见"。这个 bug 在这棵树里活了一段时间才在 `17e8bb0` 修掉。

### 4.4 一次运行为什么"能听机器"——录音取证

`xnu/files/bsd/dev/redwood_audio.c:1-28`：

```
 *   into a recording, with boot-arg rec_sound=N: the first N seconds of sound are also kept
 *               in one piece of memory that is left in place when the kernel returns to the
 *               loader, which hands it to the host ("xl sound"; under QEMU, the monitor's
 *               pmemsave), where tools/sound.py makes a WAV file of it. Only what is played
 *               takes room, the silence in between does not: every write is stored with the
 *               time it was made.
 *
 *                 struct rec_header, then per write: struct rec_block, frames * 4 bytes
 *
 *               With rec_sound_fed=1 as well, the recording has instead what the speakers' guard
 *               let through (redwood_guard.c, on the phone): what reaches the amplifiers. With
 *               rec_sound_mic=1, what the main microphone hears (redwood_mic.c, mic=3), in both
 *               channels.
 *
 * When the writer has stopped, a line in the log says how long it wrote, at what rate, how much
 * of it was not silence and how loud: the only way to "hear" a machine one cannot listen to.
```

录音结构体（`redwood_audio.c:52-64`）：

```c
struct rec_header {
	char     magic[8];              /* "RWSND001" */
	uint32_t rate, channels;
	uint32_t blocks;
	uint32_t reserved;
};
struct rec_block {
	uint64_t nanoseconds;           /* when it was written, since the kernel started */
	uint32_t frames;
	uint32_t reserved;
};
```

> **这个思路我们可以直接搬给耳机**：不用人戴耳机听，而是把"送到 codec/耳机的采样"录下来带出机器，
> 在主机上转 WAV 听。对 accdet 来说更关键的是"**寄存器读不到**"而不是"听不到"，
> 但录 `rec_sound_fed`（经过 guard 的实际输出）能验证通路是否真的通了。

### 4.5 codec 相关的其它部分（都不是 xaga）

`redwood_mic.c` / `redwood_sound.c` / `redwood_adsp.c` / `redwood_speaker.c` 是 **POCO X5 Pro（高通 WCD938x + AW88261 + LPASS Hexagon）** 的，
`redwood_mic.c:2-5` 写明 "Every address and number below belongs to the POCO X5 Pro (redwood)"。
**这些在 MT6895 上没有参考价值**（联发科没有 LPASS/Hexagon/SoundWire）。

---

## 5. GPU

### 5.1 现状

- **xaga 没有 GPU 驱动。** 搜 `G610` / `Valhall` / `CSF` / `panthor` 在整棵树里**零命中**。
- 树里的 GPU 代码是两套**别的机器**的：`redwood_adreno.c`（高通 Adreno 642L）和 `redwood_mali.c`（Mali-G52 MC2，Bifrost，JM 架构）。

### 5.2 `redwood_mali.c`：Mali 上电的完整顺序（**对我们仍有参考价值**）

`xnu/files/bsd/dev/redwood_mali.c:2-26`：

```
 * The GPU of the Infinix Hot 20 (X6826B), a Mali-G52 MC2, brought up from the kernel. Only with
 * boot-arg mali=1: every address below belongs to this phone (the other phone's GPU is an Adreno,
 * bsd/dev/redwood_adreno.c).
 *
 * A Bifrost Mali needs no firmware, so bringing it up is power and clocks and nothing else, in the
 * order MediaTek's own kernel uses -- the same sequence the loader's GPU probe proved on the phone
 * (mtkloader/mtkmali.h, where it is written out with its sources):
 *
 *   1  the "mfg_sel" clock mux in topckgen, to the main PLL's third (364 MHz)
 *   2  the four power domains in the SPM, MFG_ASYNC, MFG, MFG_CORE0 and _CORE1, each with its SRAM,
 *      and once MFG has power the bus protection between the GPU block and the rest released
 *   3  the block's own clock gate (BG3D in mfgcfg)
 *   4  the GPU: which one it is, a soft reset, and its L2, tiler and two shader cores powered
 *   5  one job, the smallest there is -- write a value into memory -- through address space 0 in
 *      identity mode (a GPU address is the physical one), and the processor reads the value back
 *   6  the same job again through the GPU's own page tables, at a GPU address of their making
 *
 * The PMIC's two GPU rails, VGPU and VSRAM_GPU, are left on by the preloader; this does not touch
 * them. Afterwards the GPU stays powered, for what is to drive it next. sysctl kern.redwood.mali
 * says how far it got: 6 means every step above worked.
```

**关键一句**：`A Bifrost Mali needs no firmware`——**Bifrost 无需固件，所以整个 bring-up 就是电源+时钟**。
这正是我们的 **G610（Valhall CSF）** 与它最本质的区别：**CSF 有一块自己的 MCU 要跑固件**，
"MCU 收到 AUTO 后不执行指令"这类问题在这个仓库里**没有对应经验**。

`mtkloader/mtkmali.h:1-23` 是那份"带出处的上电序列"，注释里给了每条的依据：

```
 * Sources: Linux drivers/clk/mediatek/clk-mt6768-pg.c (the power sequences, word for word),
 * clk-mt6768.c (the mux and the gate), drivers/misc/mediatek/base/power/mt6768/mtk_gpufreq_core.c
 * (the order), all from MediaTek's own 4.14 tree; drivers/gpu/drm/panfrost/panfrost_regs.h for the
 * GPU's registers.
```

SPM 四个域的寄存器（`mtkmali.h:52-62`）——**联发科的 GPU 上电是通用的套路，xaga 也是这套**：

```c
/* In the order they come up; they go down in the other. */
static const struct mali_domain mali_domains[4] = {
	{ "MFG_ASYNC", 0x334, 1u << 14, 0 },
	{ "MFG",       0x338, 1u << 11, 1 },
	{ "MFG_CORE0", 0x34c, 1u << 12, 1 },
	{ "MFG_CORE1", 0x310, 1u << 13, 1 },
};
```

总线保护（`mtkmali.h:65-69`）：

```c
#define INFRA_TOPAXI_PROTECTEN_STA1  0x228
#define INFRA_TOPAXI_PROTECTEN_SET   0x2a0
#define INFRA_TOPAXI_PROTECTEN_CLR   0x2a4
#define MFG_PROT_STEP1       (1u << 25)
#define MFG_PROT_STEP2       ((1u << 21) | (1u << 22))
```

### 5.3 上游（Infinix）的 GPU 栈：Mesa + panfrost + Metal

`README.hakios.md:21-23`：

```
- **Graphics** — an in-kernel Mali driver (`bsd/dev/redwood_mali.c`), Mesa with **panfrost**, and a **Metal** driver
  (`userland/src/redwoodmetal.m`, an AIR→NIR compiler): CoreAnimation draws through Metal on the Mali-G52. Lock screen, swipe to
  unlock, the full home screen, system gestures and Control Center.
```

**注意：panfrost 只覆盖 Bifrost/Midgard，不含 Valhall CSF（G610 需要 panthor/CSF 路径）。**
`tools/mesa/panfrost-darwin.patch:1-20` 是让 panfrost 在没有 DRM 的 Darwin 上通过 `src/redwood-drm` 桩工作的改动。

`docs/metal.md` 773 行是"iOS 怎么找 Metal 驱动"的完整逆向，其中 §1 非常有价值（`docs/metal.md:23-42`）：
`IOServiceMatching("IOAcceleratorES")` → `MetalPluginName` → `MetalPluginClassName` → `isSubclassOfClass:_MTLDevice`。
这个和 GPU 硬件无关，**任何 GPU 后端都要走这条路**。

### 5.4 对我们的意义

| 能用的 | 不能用的 |
|---|---|
| MTK SPM 电源域上电顺序（MFG_ASYNC/MFG/CORE0…）、TOPAXI 保护位、mfgcfg BG3D 门控——**这些 xaga 上是同一套 IP** | Bifrost "无需固件"的结论（G610 CSF 有 MCU） |
| "分 6 步、每步一个 sysctl 计数"的 bring-up 记录法 | 任何 CSF/MCU 指令流的经验 |
| Metal 查找链（IOAcceleratorES → 插件 bundle） | Adreno 的一切 |

---

## 6. 调试与日志机制（重点）

这一节是这个仓库**最有价值的部分**。四条独立通道，覆盖四种失败模式。

### 6.1 通道一览

| 通道 | 机制 | 覆盖的失败模式 | 代码 |
|---|---|---|---|
| **A. loader_return** | 内核主动把控制权交回 loader，loader 写存储 | panic / 正常结束 / 定时到期 / 主机要求停止 | `osfmk/arm64/loader_return.c` |
| **B. 看门狗 + DRAM 快照** | 第二核看门狗超时后从 DRAM 直接把内核日志缓冲区读出来 | 内核彻底挂死（连 panic 都打不出来） | `mtkloader.c:1541 rescue_log()` |
| **C. ramoops/pstore** | 裸机代码把输出写进 Android 的 ramoops 区 | loader 阶段（XNU 还没起来就挂了） | `mtkloader/mtkdbg.h` |
| **D. 屏幕** | framebuffer 直接画字/画色带 | 上面全都不通时 | `mtkdbg.h` fbcon_*、`fb_rect` |
| **E. 屏幕录像 / 录音** | 内存里录一段，退出时交给主机 | "过程性"故障，无法靠终态判断 | `MiniIOMobileFramebuffer.cpp:166-187`、`redwood_audio.c` |

### 6.2 通道 A：`loader_return` 的日志缓冲区（**三段式，值得照抄**）

`xnu/files/osfmk/arm64/loader_return.c:30-38`：

```c
/*
 * The console log handed back to the loader. A long session writes more than fits, and both
 * ends matter: the start of the boot is kept as it is, and of everything after it the most
 * recent part (a ring); room is left for what is appended on the way out (the kernel message
 * buffer, a panic's text). loader_log_linearize() puts the pieces in order before returning.
 */
#define LOADER_LOG_SIZE (1536 * 1024)
#define LOADER_LOG_HEAD (256 * 1024)
#define LOADER_LOG_RING (1024 * 1024)
```

**为什么是"头 256 KB + 环 1 MB"而不是纯环形**：开头（boot 阶段）和结尾（崩前）都重要，中间可以丢。
这个设计比我们现在的"4 MiB 纯环形"更抗"话多的驱动把早期输出顶掉"（对比 `docs/DEBUGGING.md:73-76` 里我们自己记的那个坑）。

写字符的实现（`loader_return.c:88-112`）——**从第一行 C 到 panic 都能用**：

```c
	daif = __builtin_arm_rsr64("DAIF");
	__builtin_arm_wsr("DAIFSet", DAIFSC_IRQF | DAIFSC_FIQF);
	for (uint32_t tries = 0; !locked && tries < 10000000; tries++) {
		locked = os_atomic_cmpxchg(&loader_log_lock, 0, 1, acquire);
	}
	loader_log_put_locked(c);
	if (locked) {
		os_atomic_store(&loader_log_lock, 0, release);
	}
```

注释解释了为什么用"拿不到锁就硬着头皮写"（`loader_return.c:78-83`）：

```
 * Characters come from every processor, and from interrupt handlers on top of whatever was
 * writing one. The lock is of the simplest kind, good from the first line of C code to the
 * last of a panic; whoever cannot have it for a long while (its holder is gone: a panic
 * inside this very function) goes ahead without it.
```

而且 `loader_log_putc` **同时写 UART 和内存**（`loader_return.c:97-100`）：

```c
	if (c == '\n') {
		mt6577_uart_putc('\r');
	}
	mt6577_uart_putc(c);
```

`loader_log_line()`（`loader_return.c:118-...`）一次写整行、持锁，**防止多核交错**：

```
 * A whole line at once, under the lock: lines written from several processors at the same time
 * (tools/latency.py's "tl:" lines, a frame's on one and a touch's on another) are not mixed up.
```

### 6.3 通道 A 的握手：`loader_live` 结构（**loader 主动"看着"内核**）

`xnu/files/osfmk/arm64/loader_return.c:187-222`：

```c
/*
 * For a loader that watches from another processor core while the kernel runs (it starts the
 * kernel on a core of its choice and stays on the first one): a sign of life, and where the log
 * is, so that the log of a kernel that hangs -- or dies before it has said a word through any
 * other channel -- is not lost with it. The loader finds this by its first word in the kernel
 * it has loaded. The addresses are the kernel's virtual ones, filled in by the linker and so
 * valid from the first instruction; the loader knows the distance to the physical ones.
 */
struct loader_live {
	uint64_t magic;                 /* "XNULIVE!" */
	uint64_t heartbeat;             /* goes up every HV_WDT_PET_SECONDS while the kernel is alive */
	const char *log;                /* loader_log */
	const uint32_t *log_len;        /* how much of it is in use */
	const uint32_t *log_ring_at;    /* where the ring part is being written */
	const bool *log_wrapped;        /* the ring has gone round */
	uint64_t log_head, log_ring;    /* sizes of the two parts */
	const uint64_t *recording;      /* loader_recording: { physical address, length } of a screen recording */
	volatile uint64_t stop;         /* the loader's to write: the host has asked for the run to end */
	uint64_t magic2;                /* "XNULIVE2": what follows is there (a loader may meet an older kernel) */
	const uint64_t *sound;          /* loader_sound: the same for a recording of the sound */
};
```

**设计要点**：
- loader **不依赖符号表**（至少这一步不依赖）——靠魔数 `"XNULIVE!"` 在镜像里搜。
- 字段是**指针**而不是值，所以内核一直更新，loader 一读就是最新的。
- `stop` 反向：**loader 写、内核读**，实现"主机要求停止"。
- `magic2` 做版本兼容（`"XNULIVE2"`）。

`loader_recording[3]` / `loader_sound[3]`：`{物理地址, 字节数, 内核地址}`（`loader_return.c:209-215`）。

### 6.4 通道 B：看门狗快照 `rescue_log()`

`mtkloader/mtkloader.c:1541-1592`（**从内核镜像里解析符号表，直接读 DRAM**）：

```c
static void rescue_log(void)
{
	...
	find_syms(pi.kernel, &s);
	if (!s.log || !s.log_len || !s.log_ring_at || !s.log_wrapped) {
		out("XNU log symbols not found\n");
		return;
	}
	/* osfmk/arm64/loader_return.c: a 256 KB head, then a 1 MB ring */
	const uint64_t head_sz = 256 * 1024, ring_sz = 1024 * 1024;
	const volatile char *log = (const volatile char *)kva_to_pa(s.log);
	uint32_t len = rd32(kva_to_pa(s.log_len));
	uint32_t ring_at = rd32(kva_to_pa(s.log_ring_at));
	uint8_t wrapped = *(const volatile uint8_t *)kva_to_pa(s.log_wrapped);
	...
	if (!wrapped) {
		sp[n++] = (struct span){ log, len };
	} else {
		sp[n++] = (struct span){ log, head_sz };
		sp[n++] = (struct span){ log + head_sz + ring_at, ring_sz - ring_at };
		sp[n++] = (struct span){ log + head_sz, ring_at };
	}
	...
	dbg_puts("=== XNU log (from DRAM; a few bits may have flipped in the reset) ===\n");
	save_log(sp, n);
```

符号查找（`mtkloader.c:484-535`）——**自己解析 Mach-O `LC_SYMTAB`，按名字列表取地址**：

```c
	static const char *const names[] = {
		"_loader_stub_slot", "_vc_colors", "_loader_log", "_loader_log_len", "_loader_log_ring_at",
		"_loader_log_wrapped", "_debugger_panic_str", "_debugger_panic_args", "_iso_font",
		"_msgbufp", "_cpu_ttep", "_loader_live",
		...
	};
```

> **这个可以完全脱离 loader 复用**：只要内核镜像里有符号表（`mach.development.bcm2837` 就是 unstripped 的），
> 就能算出 `loader_log` 那一块缓冲的物理地址。我们现在的 `SNAP_PA 0x90000100` 是**写死的固定地址**，
> 改成"按符号定位 + 三段式"更稳。

### 6.5 通道 C：ramoops / pstore

`mtkloader/mtkdbg.h:1-13`：

```
/*
 * Debug output for bare-metal code on the X6826B (MT6768) that runs with the MMU off:
 *
 *  - plog_*: the ramoops console zone that Android's pstore keeps across a warm reset. What is
 *    written here shows up after the next Android boot as /sys/fs/pstore/console-ramoops-0.
 *    Zone layout from Android's ramoops (mem 0x4d010000 size 0xe0000, console 0x40000): dump
 *    records first, then console at 0x4d09f000. Header: sig "DBGC", start (next write), size.
 *  - fbcon_*: text on the panel. The panel is video mode (td4160, BURST_VDO): OVL0 layer 0
 *    scans LK's framebuffer continuously, so pixels written there appear on the next frame.
 *    OVL0 has per-pixel alpha enabled (LK sets aen=1), so every pixel needs alpha 0xff.
 *
 * Everything here is device memory (MMU off): no unaligned accesses, no SIMD, no dc zva.
 * Build with -mgeneral-regs-only -mstrict-align -fno-builtin.
 */
```

地址（`mtkdbg.h:20-28`）——**这是 MT6768 的；xaga 上必须换，但格式是通用的**：

```c
#define RAMOOPS_CONSOLE      0x4d09f000ULL
#define RAMOOPS_CONSOLE_SIZE 0x40000u
#define RAMOOPS_SIG          0x43474244u
/* the pmsg zone: Android's request for one XNU boot comes in it (boot-xnu.sh), the kernel's lines for
 * the host go out in it (kern.redwood.host_keep) */
#define RAMOOPS_PMSG         0x4d0e0000ULL
#define RAMOOPS_PMSG_SIZE    0x10000u
```

环形缓冲实现（`mtkdbg.h:107-141`），头是 `sig/start/size`：

```c
struct ramoops_hdr {
	...
};
#define PLOG_HDR  ((volatile struct ramoops_hdr *)RAMOOPS_CONSOLE)
#define PLOG_DATA ((volatile char *)(RAMOOPS_CONSOLE + sizeof(struct ramoops_hdr)))
#define PLOG_CAP  (RAMOOPS_CONSOLE_SIZE - sizeof(struct ramoops_hdr))
```

> **注意**：`docs/DEBUGGING.md` 我们自己写的是"ramoops 区域被 Android 占着，pstore 一直是空的"。
> 这个仓库的做法是**直接往 Android 的 ramoops 区间里写**，并依赖 Android 下次启动把 `console-ramoops-0` 呈现出来。
> **这条在我们机器上是否可行，必须实测**（可能正是我们 pstore 空的原因——不是不能写，而是写的位置/头格式不对）。
> 我们至少有两条线索可查：`mtkdbg.h` 的头格式（`"DBGC"` 魔数 + start/size），以及 `RAMOOPS_CONSOLE` 的偏移算法。

### 6.6 通道 E：屏幕/声音录像（**过程性故障的唯一手段**）

`xnu/files/iokit/Kernel/MiniIOMobileFramebuffer.cpp:166-187`：

```
 * A recording of the screen, for a machine one cannot look at while it runs (the phone is
 * [in a bag / on a bench]...
 * Boot-args rec_secs=N [rec_start=S] [rec_mb=M]: from S seconds after the first frame, for N
 * [seconds]... hands it to the host ("xl recording"), where tools/recording.py makes a video of it.
 *
 * Boot-arg rec_ring=1: when the memory is full the recording does not stop but goes on over its
 * [beginning]... (the same size, one after the other) and when the recording began, in the kernel's time, for the
```

宿主侧拿取（`xl:19-30`）：

```
#   xl screenshot [F]   save what is in the phone's framebuffer as a PNG (default logs/screen.png);
#                       works during a run too (from another terminal)
#   xl recording [F]    fetch the screen recording the last run made (rec_secs=N among XL_CMDLINE) and
#                       make a video of it (default logs/recording.mp4)
#   xl sound [F]        fetch the sound the last run recorded (rec_sound=N among XL_CMDLINE: the first N
#                       seconds of what iOS played) and make a WAV file of it (default logs/sound.wav)
#   xl camera [P]       fetch the frames the last run's camera left (cam=3 and up among XL_CMDLINE) and make
#                       pictures of them (P-0.png, ...; default prefix logs/camera)
```

### 6.7 `host_keep`：给"日志中间会被顶掉"打的补丁

`xnu/files/bsd/dev/redwood_mali.c:126-138`：

```
 * kern.redwood.host_keep: lines for the host that must not be lost with the log's middle. The log the
 * host gets is a 256 KB head and tail of a run's (mtkloader save_log): the pipeline recipes a two-hour
 * run built (run 352) were in neither. A line written here goes into this buffer only (the writer says
 * in the log what it wants said there), and mtkloader puts the buffer into ramoops' pmsg zone when the
 * kernel comes back, by whatever way it does (its end, a panic, the watchdog), where Android shows it
 * as /sys/fs/pstore/pmsg-ramoops-0 and tools/xnu-cycle.sh appends it to the log. Lines that do not fit
 * are counted, not kept.
 */
#define HOST_KEEP_SIZE 0xff00          /* the pmsg zone is 64 KB, less its header and mtkloader's first line */
```

两个入口：`kern.redwood.host_keep`（用户态写一行，`redwood_mali.c:141-168`）和 `redwood_host_keep_add()`（内核自己写，`:174-193`）。

配套的 `kern.redwood.say`（`redwood_mali.c:69-73`）——**作业自己的输出没有地方存，就让它进内核日志**：

```
 * kern.redwood.say: a line from a program, into the kernel's log (a job's own output is kept nowhere):
 * sysctlbyname("kern.redwood.say", NULL, NULL, line, strlen(line)).
```

`!` 开头的行会被留存并**每分钟重播一次**（`redwood_mali.c:97-113`），`KEPT_LINES 96`。
注释解释了为什么（`redwood_mali.c:170-172`）：

```
/* ... and from the kernel itself: a line said, kept for the end of the log, and kept for the host (a bring-up's results,
 * which the minute's 96 kept lines lose to the display's in a run of some minutes: CONNSYS's in run 355) */
```

### 6.8 其它调试设施

- **checkpoint**：`loader_checkpoint(n)` + `loader_stub_slot[2]`，让内核停在第 n 个检查点并交回日志（`loader_return.c:406-413`）。`xl run [N]` 就是它。
- **`user_exception_trace.c`**：`xnu/README.md:75` — "отладка userland: `usertrap:`, `userabort:`, стеки потоков (`ustacks`)"。
- **`tools/latency.py`、`tools/frames.py`、`tools/fbshot.py`**：帧/延迟/抓屏分析。
- **`xl state` / `xl log` / `xl stop` / `xl hang`**：宿主侧状态机（`xl:38-52`）。

---

## 7. 文档 / 踩坑记录

### 7.1 docs/ 清单（只列有内容的）

| 文件 | 行数 | 语言 | 内容 |
|---|---|---|---|
| `docs/touch-handover.md` | 196 | 俄 | **最有价值**：触控从零到"能读不能启"的完整交接，含逐条"已验证的硬件事实"和"试过什么/什么回应" |
| `docs/wifi-x6826b.md` | 616 | 俄 | CONNSYS 上电的逐步记录，每步一次 run 编号 |
| `docs/metal.md` | 773 | 俄 | iOS 找 Metal 驱动的逆向 + AIR 分析 |
| `docs/air.md` | 698 | 英 | AIR→NIR 编译器 |
| `docs/air-report.md` | 176 | 英 | 456 个 AIR 模块的语料报告 |
| `docs/camera-mtk-porting.md` | 58 | 英 | **把一台机器的移植经验写成"给另一台机器的工序表"**，方法论价值高 |
| `docs/sensors-x6826b.md` / `x6826b-emmc.md` / `x6826b-wallpaper.md` / `x6826b-sep-appstore.md` / `metal-x6826b-plan.md` | 45–249 | 混 | 各子系统的 run 记录 |
| `docs/x6826b-aks-selectors.txt` | — | — | 155 行 selector 列表 |

**注意：`docs/` 里没有一份是讲 xaga 的**（除了 README）。`docs/touch-handover.md:7-9` 说的是 X6826B。
所以 §6 里我们看到的 xaga 专属细节（MT6363、NVT、TFA9873）**全在代码注释里，不在文档里**。

### 7.2 `docs/touch-handover.md` 的写法（**建议照抄这个体裁**）

它不是"文档"，是**给下一个 agent 的交接包**（`:1-5`）：

```
# Передача работы: сенсорный экран X6826B

Скопируй всё, что ниже разделителя, в новый разговор с агентом.
```

内容分六块，每块都有明确用途：
1. ** чего нельзя делать **（禁止事项：只对这台机器、绝不碰 slot B、userdata 可擦）
2. **Где всё стоит сейчас**（现状一句话）
3. **Один цикл проверки**（一个验证循环要多久、跑哪条命令）
4. **Что про железо уже выяснено (всё проверено на телефоне)**（已验证的硬件事实，逐条）
5. **Что работает и что нет**（含一张"试过什么 → 什么回应"的表，`:141-157`）
6. **Куда смотреть дальше**（带排序的下一步，含"已经试过并失败的路径"）

第 4 块的密度极高，例如 `:50-70`：

```
- Шина: **SPI2 по адресу `0x11012000`**, режим 3, до 8 МГц.
- Выводы: **GPIO25 = выбор кристалла, 26 = выход данных, 27 = такт, 28 = вход данных**,
  функция 2. Это таблица **MT6768**, а не MT6765 — у MT6765 те же
  функции лежат на GPIO17–20, и это была моя первая ошибка, стоившая нескольких
  циклов.
- Такт: длительности полупериодов на этом поколении задаются в **CFG2**, а не в
  CFG0 (в Linux это называется `enhance_timing`). В CFG0 остаются только
  установка и удержание выбора кристалла. Записанные в CFG0 длительности
  оставляют такт нулевой длины, при этом флаг завершения всё равно поднимается —
  а точнее, остаётся висеть с прошлого обращения, и каждое чтение выглядит
  удачным, возвращая `0xff`.
- Сам тактовый сигнал SPI был **выключен**: делитель `spi_sel` в topckgen это
  два бита со сдвигом 24 в `CLK_CFG_2` (`0x10000060`, с регистром сброса бит по
  `0x68`) плюс бит выключения 31.
```

**"每个读都看起来成功、返回 `0xff`"** —— 这是嵌入式调试的经典陷阱（bus 没起来，FIFO 留着上次的值）。
我们耳机"从未读到过任何寄存器"的排查里，**必须先确认"读到 0 或 0xff"不是这种情况**。

### 7.3 其它值得注意的踩坑条目

- **`docs/touch-handover.md:76`**：「Каждое продолжение чтения начинается заново с двух байт `0xa5 0x03`」——协议层陷阱，导致长时间误判。
- **`docs/touch-handover.md:107`**：固件变大导致 `CHAIN_OFFSET` 从 `0x20000` 抬到 `0x40000`，要改三个文件。
- **`README.md:18`**：`Do not use fw02 on this panel: it downloads but its baseline stays at a1 and reports only 0x77 fill.`
- **`mtkloader.c:62-66`**：给内核留一个"看门狗核"如果选错核，会让内核卡在 Apple logo 15 分钟。
- **`redwood_backlight.c:441-444`**：AFE 固定 /64 衰减 + iOS 软件音量 = 双重衰减。
- **`docs/camera-mtk-porting.md:38-40`**：`"On the X6826B the DMA, programmed as the HAL does, writes one byte a pixel ... the partial frames seen before that was found were that mismatch (runs 496-499)."`

---

## 8. 对 HakiOS 的具体可用点（按优先级）

### P0-1　三态标记 + 一次性请求：把"刷机黑屏"变成"自动回 Android"

**能用来干什么**：直接解决"最近四次刷机全部黑屏，进不去 XNU"里最痛的部分——**不知道挂在哪、且每次都要人工救**。

具体做法（我们已经有 `SNAP_PA`、`chain_android()`、`XNU_WDT_SECONDS`，缺的只是这个状态机）：
1. 在 DRAM 里放 `MARK_RUNNING / MARK_DONE`，**三重冗余**写（`mtkloader.c:387-399`）。
2. `run_xnu()` 前写 `NXRN` 并 arm TOPRGU（`mtkloader.c:2325-2338`，key `0x1971` / `0x22000000`）。
3. 下一轮上电先读标记：`NXRN` → 从 DRAM 捞日志 → 直接回 Android；`NXDN` → 直接回 Android（`mtkloader.c:3375-3385`）。
4. 启动请求走 Android 的 `pmsg0`（`pmsg_take`，`mtkloader.c:2974-3000`），**不用按键**。

代价：一次刷机后，即使内核挂死，最多一次重启就自动回 Android，且带回一份 DRAM 里的日志。
**这一条能让我们从"每次黑屏都要人工恢复"变成"自动化跑 20 轮"**。

### P0-2　把 `v_display` 和"黑屏"区分开

**能用来干什么**：先排除最廉价的误判。

`mtkloader.c:2301`：`v_display = XNU_VERBOSE ? 0 : 1`。非 verbose 时内核文本控制台**不画**。
所以"黑屏"有两种可能：(a) 内核死在早期；(b) 内核活着但 iOS 还没画。
**第一步永远是 `-DXNU_VERBOSE=1` 切文本控制台**，能在屏上直接看到卡在哪一行。这一条我们自己可能也有（需核对）。

### P0-3　`loader_return` 的"头 256 KB + 环 1 MB"三段式日志

**能用来干什么**：修我们 `docs/DEBUGGING.md:73-76` 记的那个坑——"话多的驱动把早期输出顶掉"。

我们现在是 4 MiB 纯环形，`touchscreen` 一开就 18 秒清空。改成：
- `HEAD 256 KB`（boot 阶段原样保留）
- `RING 1 MB`（最近输出，`ring_at` / `wrapped` 两个游标）
- `TAIL 256 KB`（退出时追加 msgbuf + panic buffer）

代码在 `loader_return.c:36-38、140-182`（`loader_log_linearize`）和 `mtkloader.c:1541-1585`（`rescue_log` 的三段重组）。
**loader 端和内核端的 span 逻辑要配套改**，但两边代码都在，可以照抄。

### P0-4　"从内核镜像的符号表定位日志缓冲"——不再写死物理地址

**能用来干什么**：我们现在 `SNAP_PA 0x90000100` 是硬编码的。一旦改了内核布局就失效，且没有任何提示。

`mtkloader.c:484-535` 的 `find_syms()` 自己解析 Mach-O `LC_SYMTAB`，按名字列表取 `_loader_log` / `_loader_log_len` /
`_loader_log_ring_at` / `_loader_log_wrapped` 的地址，再 `kva_to_pa()`。
**这个函数可以整段搬过来**（只依赖 `mach_header_64` / `nlist_64` / `symtab_command`，都是标准结构）。

### P1-1　SPMI：`redwood_buttons.c` 的 arbiter 代码是**现成的 SPMI 读寄存器实现**

**能用来干什么**：**这是对我们耳机任务最直接的一块**。我们"从未在真机上读到过任何耳机寄存器"，
这个文件里有一份完整、在真机上跑通过（三键都工作）的 SPMI 读路径——只不过它是高通 PMIC arbiter，不是 MTK。

**不能直接用的部分**：MT6895 的 PMIC 不是走高通 arbiter，是走 **PWRAP**（`mtkloader/mtkpwrap.h`，MT6358/MT6363 那套）。

**能直接用的部分（方法论 + 代码结构）**：
1. **分 5 步、每步先声明再执行**（`redwood_buttons.c:31-37、450-530`）。我们的 accdet 完全应该照这个写：
   `accdet_step=1` 只读 arbiter/PWRAP 版本；`=2` 找 slave 5 的通道；`=3` 第一次真正读 transfer；
   `=4` 读 type/subtype + 当前状态；`=5` 开始轮询。**每步不通过就停，日志里说为什么不通过。**
2. **"先验证外设身份再相信读数"**（`redwood_buttons.c:357-379`）：读 `REG_TYPE(0x04)` / `REG_SUBTYPE(0x05)`，
   跟预期比对，不对就"left alone"并打印实际值。
   > 我们的 accdet 应该先读 MT6368 的 `0x0004/0x0005` 确认"这真是 MT6368"，而不是一上来就读耳机状态位。
3. **总线错误分类**（`redwood_buttons.c:183-193`）——四种失败分开报，这一条极有价值：

```c
	if (!(status & STATUS_DONE)) {
		problem = "no answer from the arbiter";
	} else if (status & STATUS_DENIED) {
		problem = "the arbiter refuses";
	} else if (status & STATUS_FAILURE) {
		problem = "the transfer failed (nobody at that address?)";
	} else if (status & STATUS_DROPPED) {
		problem = "the transfer was dropped";
	}
```

4. **总线统计每分钟报一次**（`redwood_buttons.c:441-448`、`515-526`）：读了多少次、平均/最长耗时、失败几次；
   **失败超过一半就放弃**（`:521-524`）。这比"一直读、一直超时"好得多。
5. **"只读不写"的纪律**（`redwood_buttons.c:20-26`）：

```
 * THIS DRIVER ONLY READS. Nothing is written to either chip, neither a register of a PMIC nor
 * the configuration of the pin: the PMICs make the phone's supplies, and a wrong write there
 * can switch one off.
```

> 对耳机来说这条**特别重要**：accdet 在 MT6368 上，MT6368 同时管 codec。写错一个寄存器可能把 codec 关掉或让 PMIC 保护。
> 先做纯读版本，确认能读到，再考虑写。

6. **"读到的值不可信"的判据**（`redwood_buttons.c:24-28` 的规则声明，`redwood_buttons.c:294-311` 的 `button_look()` 实现）：`a key that reads as held before it has ever been seen released is not believed`。
   对应我们的场景：**如果耳机检测位一上来就读成"已插入"，那不可信**；必须先看到一次"未插入"再相信。

### P1-2　PWRAP 读寄存器的现成实现（MTK 侧）

`mtkloader/mtkpwrap.h:1-45` 是 MT6358 的 PWRAP WACS2 完整实现（idle → cmd → wait → read → VLDCLR），
`xnu/files/bsd/dev/redwood_mtkkeys.c:70-108` 是内核态版本（同样的状态机，多了 `pwrap_wait` 超时）。

**对我们的价值**：
- 如果 MT6363/MT6368 走 PWRAP，这份代码就是**模板**。（**未验证**：本仓库里没有 MT6363/MT6368 的 PWRAP 访问代码，MT6895 上是否同一套通道需要我们自己确认。）
- `mtkloader/mtkconnsys.h` / `redwood_connsys.c:82` 提到"through the PMIC wrapper's WACS2 channel"——说明这套在 MTK 平台上是通用的。
- **注意**：`mtkloader.c` 里的 `PWRAP_PHYS 0x1000d000` 是 MT6768 的；MT6895 的地址要查我们自己的 dts（`research/raw/mt6895.dtsi` 就在手边）。

### P1-3　"boot-arg 分步 + 每步先声明"的开发纪律

**能用来干什么**：把 accdet 的调试从"改代码重编重刷"变成"同一份内核、不同 boot-arg 跑五轮"。

参照 `connsys_step` 的表格化记录方式（`docs/wifi-x6826b.md:186-200`），每轮 run 记一行：
```
| 步 | 做什么 | run |
| 1 | 只读 PWRAP 版本 + MT6368 type/subtype | 待跑 |
| 2 | 找 SPMI slave 5 的通道 / 确认地址可达 | |
...
```

### P2-1　音频 sink ABI（如果要做耳机输出）

`sys/redwood_audio.h` 的"一个字符设备 + 一个 sink 回调 + 一个 ioctl 音量"结构很干净，
比实现 IOAudio2 现实得多。`redwood_audio.c` 的 `rec_sound` 录音取证也可以直接搬来做"耳机通路验证"。

**但注意**：xaga 的实现（`redwood_backlight.c:360-545`）是 **AFE 直驱 + bit-bang I2S**，
注释自己承认 `diagnostic only`（`:360`）。做为"能出声"的临时方案可以，长期要换真 I2S 控制器。

### P2-2　MTK GPU 上电序列（SPM 电源域 / TOPAXI / BG3D）

`mtkmali.h:52-69` 那三张表（4 个 SPM 域 + 2 组保护位 + mfgcfg 门控）是 MT6768 的值。**联发科跨代是否同一套 IP 本仓库没有证据**（它没有 MT6895 的任何 GPU 代码），
只能拿我们的 dts 逐条对寄存器地址验证后再用。**但仅限"上电"，不涉及 CSF/MCU 的任何逻辑。**

### P2-3　Metal 查找链

`docs/metal.md:23-42`：`IOAcceleratorES` → `MetalPluginName` / `MetalPluginClassName` → `isSubclassOfClass:_MTLDevice`。
这段和 GPU 硬件无关，如果我们做出任何 GPU 后端（哪怕只画个三角形），都要走这条路。

### P3　文档体裁：交接包

`docs/touch-handover.md` 的六块结构（禁止事项/现状/验证循环/已验证事实/试过什么/下一步）值得直接用到我们
`work-xaga/` 的交接文档里。

---

## 9. 明确没有的东西

| 找不到的东西 | 证据 |
|---|---|
| **任何耳机 / 3.5mm / 耳机检测代码** | 全仓库 `rg -i 'earphone\|headset\|headphone\|audio.?jack\|hs_det\|hsmic\|accdet'` **零命中** |
| **MT6368（codec/accdet）** | `rg -i 'mt6368\|6368'` 零命中（除一条无关 sha256） |
| **MT6363 的 SPMI 代码** | `rg 'MT6363'` 只命中 `README.md:23,38` 和 `README.zh-CN.md:25,41` 的**文字**；`rg '0x091b'` 同样只命中 README 四个位置。**代码里没有** |
| **MTK 的 SPMI 控制器驱动** | `rg -i 'spmi' mtkloader/` 零命中；`xnu/files/` 里的 SPMI 全是高通 arbiter（`redwood_buttons.c` / `redwood_torch.c` / `redwood_wifi.c`） |
| **xaga 的 loader** | `mtkloader/` 全目录是 MT6768：`mtkloader.c:2`、`XNU_PHYS_BASE 0xc0000000`（`:32`）、`PANEL_W 720 / PANEL_H 1640`（`mtkdbg.h:38-39`）、`LK_FB_BASE 0x787b0000`（`mtkdbg.h:37`）；`rg '1080\|2460\|xaga' mtkloader/` 零命中 |
| **Mali-G610 / Valhall / CSF / panthor / MCU 固件** | `rg 'G610\|Valhall\|csf_\|CSF'` 零命中；`redwood_mali.c:6` 明说 "A Bifrost Mali needs no firmware" |
| **`fbdiag/pull-iosroot-logbook.sh`** | README `:82` 让跑这个脚本，但**仓库里没有 `fbdiag/` 目录**（`ls -d fbdiag` → No such file）。全仓库 `find -name 'pull-*' -o -name '*logbook*'` 零结果 |
| **HLOG logbook 的写入代码** | `grep -ral 'HLOG'` 只命中 `README.md` / `README.zh-CN.md`。README 说"loader 把 HLOG 写到 iosroot 尾部最后 8 MiB"，但**代码不在这个仓库里**（可能在他们未公开的 loader 分支，或还没提交） |
| **xaga 的文档** | `docs/` 下 11 个 md **全部**讲 X6826B / POCO X5 Pro / QEMU，没有一份讲 MT6895 移植 |
| **构建/刷机脚本的 xaga 版本** | `xl`、`mtkloader/build.sh`、`tools/chainboot.py` 的常量全是 MT6768 / 高通；`README.md:84` 的 `fbdiag/` 路径不存在 |
| **`iosroot` 分区的任何代码** | `rg -n 'iosroot'` 在 `mtkloader/`、`xnuloader/` 零命中（只有 README 提到） |
| **电荷计（库仑计）** | `README.md:21` 自己说 "the real MT6375 coulomb counter is not implemented yet" |
| **Wi-Fi/BT 驱动（xaga）** | `README.md:25` "No functional Wi-Fi/BT driver yet" |
| **GPU 驱动（xaga）** | README 状态表里**没有 GPU 这一行**，全文搜 G610/Mali（在 MT6895 语境下）零命中 |

### 9.1 一个需要特别提醒的不一致

**README 的按键描述与代码对不上。** `README.md:23` 说：

```
| Keys | Power and volume-up use the MT6363 PSC interrupt status `0x091b` ... All three keys work. |
```

但：
- `redwood_buttons.c` 是 POCO X5 Pro 的（高通 PMK8350 PON + PM7325 GPIO6，`:1-30`）；
- `redwood_mtkkeys.c` 是 Infinix MT6768 的（MT6358 `TOPSTATUS 0x28` + keypad `KP_MEM1`，`:20-31`）；
- 两者都**没有** `0x091b`，也**没有** MT6363。

可能解释（**我没有验证，不下结论**）：xaga 的按键驱动在未提交的工作区里，或者 README 描述的是他们本地版本。
但无论如何：**这份 README 的状态表不能当作"xaga 上已经跑通了什么"的证据**，只能说"作者声称跑通了"。

### 9.2 另外三处"README 说有、仓库里没有"的东西

| README 的说法 | 实际情况 |
|---|---|
| `README.md:20,57`：logs = "loader 把 HLOG logbook 写到 iosroot 尾部最后 8 MiB" | 全仓库搜 `HLOG` 只命中两个 README；**写 logbook 的代码不在仓库里**。`iosroot` 分区名在 `mtkloader/`、`xnuloader/` 也零命中 |
| `README.md:82`：`fbdiag/pull-iosroot-logbook.sh` | **`fbdiag/` 目录不存在**。只有 `mtkloader/fbdiag.c`（一个裸机探针，跟拉日志无关） |
| `README.md:22`：Audio "AFE DL1/I2S3 和两颗 TFA9873 工作" | 代码在 `redwood_backlight.c:360-545`，但那段自己标着 **`a first xaga speaker test ... (diagnostic only)`**（`:360`），且 `speaker_test_thread` 里写死 `uint32_t test = 1; /* diagnostic boot: always try the tone */`（`:585`）——**没有开关，是常开诊断路径** |

**结论**：这个仓库的 README 是"作者的工作日志摘要"，不是"这个 commit 能构建出什么"的说明。
把它当**线索**用，不要当**事实**用；每条要用之前先去代码里找对应实现。

---

## 附：对我们最该先做的三件事（按 §8 推出来的）

1. **把 P0-1（三态标记 + pmsg 一次性请求）落到 loader 里**——先让"黑屏"变成"自动回 Android 且带回日志"，
   否则后面每一次实验都要人工救砖，成本太高。
2. **把 P0-3（三段式日志）+ P0-4（符号定位）落进去**——解决"日志被顶掉"和"改布局就瞎"两个结构性问题。
3. **按 P1-1 的分步法重写 accdet 探测**——`accdet_step=1..5`，每步先声明、先验身份（读 `0x0004/0x0005`）、
   错误分四类、每分钟报总线统计、失败过半就停。**在拿到"MT6368 确实应答"这一条之前，不要写任何寄存器。**
