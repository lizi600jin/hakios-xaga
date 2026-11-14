# ios-sweet / UniXNU / Bergamota 调研

调研时间：2026-11-14。目标：把这三个「XNU/Darwin 移植到安卓机」项目的**已解决问题**和**踩过的坑**挖出来，
重点是我们最急的一环：**黑屏时怎么拿到内核日志**。

**调研中途额外找到第 4 个、而且是最贴近我们的一个：`ruby`（Redmi Note 12 Pro 5G / MT6877 天玑 1080）** ——
联发科 + XNU-8796 + iPhone X iOS 16.7.16 + 16K 页 + 已经进桌面，**并且它的日志机制就是 SWEETLOG 环 + recovery 读**，
连格式都和 ios-sweet 一样。它单独写在 **§1.5**，并贯穿下文（凡标 `ruby` 的条目都是它）。

标注约定：

- **[代码]** = 有源码/commit diff 可直接核对（我把仓库整包拉下来读过）
- **[README]** = 项目自述，无代码或日志佐证
- **[实测]** = 有真机日志/串口片段/量化数字
- **未确认** = 没找到直接证据，不编

所有仓库整包下载到 `/home/lizi/IOS/work-xaga/research/raw/`（`ios-sweet-main/`、`Bergamota-develop/`、
`UniXNU-main/`、`UniXNU-sheng-main/`、`Mali-main/`、`ios-topaz-topaz/`、`ruby-ios-bringup-main/`），
下面的行号/文件名都可复核。

---

## 0.0 一句话结论（先看这个）

**最有用的三条，按「今天就能做」排序：**

1. **`expdb` / log_store 我们本来就有，只是没读。** ruby（MT6877）证明 MTK 的 LK 会把启动日志写进 `expdb`；
   而我们自己的 `mt6895-xiaomi-xaga.dts` 第 72–75 / 387–389 行已经写明
   **「XAGR ring in `log_store` at `0x7ffbf000`（256 KiB），copied to `expdb` by LK」**。
   → 拉一次 `expdb`、grep `jump to K64` / `hdr_loaded` / `panic` / `mt_disp_update:`，可能直接解开「四次黑屏零内核行」。
2. **我们的 logbook 只有 loader 在写。** 窗口尺寸（4 MiB 环 + 1 头块）和 ios-sweet/ruby 完全一样，
   但内核侧没有写者。补三件事即可：**1 s 周期 flush + panic 前 sync + 碰 GPU/GPUEB 前 sync**
   （Bergamota 0105/1002，ruby 用 `ufslogsecs=1`）。
3. **GPU 别自创，照 `Ken-u/Mali` 的 M0→M2b 走。** 它跑的是**同架构 Mali-G610（Valhall arch 10.8）CSF**，
   已经把「MCU queue/suspend buffer 必须放 MCU shared region」「STORE 的 `@`/`^` 写反不报错」这些坑写在中文日志里。

完整论证见 §1（日志）、§2（启动/救砖）、§3（驱动）、§4（黑屏案例）、§5（行动清单）。

---

## 0. 四个项目对比表

| | **ios-sweet** | **UniXNU** | **Bergamota** | **ruby**（★最贴近我们） |
|---|---|---|---|---|
| 仓库 | [dipa-studio/ios-sweet](https://github.com/dipa-studio/ios-sweet) | [UniXNU/UniXNU](https://github.com/UniXNU/UniXNU) + 每设备一仓 | [ry-diffusion/Bergamota](https://github.com/ry-diffusion/Bergamota) | [lky005/ruby-ios-bringup](https://github.com/lky005/ruby-ios-bringup)（+ 目录仓 [UniXNU-ruby](https://github.com/lky005/UniXNU-ruby)） |
| 目标设备 | Redmi Note 10 Pro（`sweet`） | 参考机 Xiaomi Pad 6S Pro（`sheng`）；`devices.toml` 里 **374** 个设备条目，其中 **13 台联发科** | Poco F1（`beryllium`） | Redmi Note 12 Pro 5G（`ruby`） |
| SoC | Qualcomm SM7150 / SD732G（Kryo 470，ARMv8.2，**无 PAC**） | Qualcomm SM8550 / 8 Gen 2（`sheng`）；另有 SM7435/SM8750/SM8650 等设备仓 | Qualcomm SDM845 / SD845（Kryo 385，ARMv8.2，**无 PAC**） | **MediaTek MT6877TT 天玑 1080**（6×A55 + 2×A76，16K 页，LSE 原生） |
| 跑什么用户态 | iPhone X（iPhone10,3 / D22AP）iOS 16.7.16（20H392） | iPadOS 17（2017 iPad Pro 固件）；也有 macOS 27 profile | 先是 iPhone X iOS 16.7.16，2026-10-23 起换 **iPhone XR iOS 18.7.8（22H352）** | **iPhone X iOS 16.7.16（20H392）—— 和我们完全一致** |
| 内核版本 | xnu-8796 | xnu-10063（iPadOS 17）→ 设备仓里有 11215 / 12377 系列补丁 | xnu-8796 → **xnu-11215.81.4**（iOS 18 代），另有 patches-12377 | **xnu-8796 —— 和我们完全一致** |
| 完成度 | **进 iOS 桌面**、解锁、设置、控制中心、触摸+按键、USB 网卡（ue0）；**纯 CPU 渲染 20–30fps，无 GPU**；无 Wi-Fi/BT/音频/相机。核数自述有出入：README 写「6 of the 8 cores (six Cortex-A55)」，NOTES 的 patch 0078 写 "all 8 cores (cpus=8, cpu6/7 = A76)"（该 8 核镜像当时未实跑验证） | 目标不是「一台手机」而是**可复用的 UEFI 引导层 + 组件目录**；GPU 在 A740 上跑通 Metal（AdrenoMetal + metal2vulkan + Turnip） | **进 SpringBoard 桌面**（Safari/设置等注册可用）、8 核（4×A55 + 4×A75）、USB CDC-ECM（SSH + 实时日志 + 网络共享）、真电量计、**GPU 做到 Adreno 630 GX 上电 + GMU/HFI 起来**、Wi-Fi 做到 WPA2 入网（内核内 4 次握手） |
| 最后活跃 | pushed 2026-10-23（`45263bee update website`）；`updated_at` 2026-11-14 是 issue/star 活动 | pushed 2026-11-14（长期高频，一天几十个 PR） | pushed 2026-10-26（`develop`）；仓库 size 14 MB，1 197 个文件 | pushed 2026-11-07（线 `0195`，tag `v0.6.0`），文档写得很新 |
| 星/分叉 | ★1366 / 972 fork | ★2882 / 449 fork | ★146 / 41 fork | ★ 低（目录仓个位数；不是靠星数看质量的那种项目） |
| 许可 | MIT | BSD-2-Clause-Patent | 无 LICENSE 文件（未确认） | MIT（自有）+ APSL 2.0（XNU 补丁） |
| 一句话 | **「不刷机、RAM 启动 + 持久日志环」的极简路线**，最适合我们抄 | **「UEFI + 组件化」的重型路线**，日志/取证体系最完整 | **ios-sweet 的下游 fork，把 sweet 的日志/诊断体系推到最狠的一家**，坑记录最详细 | **唯一验证过「联发科 + XNU-8796 + 16K 页 + iOS 16.7.16」这条组合的公开实例**；日志机制和我们同款但参数调对了 |

**重要事实：前三个是同一条血缘，ruby 是第四个下游。** Bergamota 的 `NOTES.md` 自己写：

> "This repo is `ry-diffusion/Bergamota` (fork of dipa-studio/ios-sweet, remote `upstream`) … Kernel patches 0001-0081
> are upstream sweet; 0082+ add the Poco F1. One kernel serves both phones"
> — [Bergamota NOTES.md](https://github.com/ry-diffusion/Bergamota/blob/develop/NOTES.md)

而 ios-sweet 的站点 hackiosx.dev（静态源在仓库 `site/` 下）又把 UniXNU 列为「同类项目的第三条路线」
（[site/porting.html](https://github.com/dipa-studio/ios-sweet/blob/main/site/porting.html)）。
ruby 同样是 sweet 系：它的补丁从 `0080-ruby-ufs.patch` 起，之前是上游 sweet 的基线
（[README.zh.md](https://github.com/lky005/ruby-ios-bringup/blob/main/README.zh.md) 写「内容固定的 82 文件基线 + 本项目增量」）。

**所以结论很省事：抄 Bergamota 的日志机制 = 抄 ios-sweet 的，且已经过一轮「被逼到极限」的加固；
而 ruby 证明了这套机制在联发科 + XNU-8796 上确实能跑。** 凡是 ruby 改过参数的，我在 §1.5 单独标出来。

> 说明：任务里提到的 `Bergamota` 我一开始按名字搜到两个假货——`Marcelo-Barella/bergamota` 是个 Nuxt 作品集网站，
> `bergamota-wifi/berga-*` 是 2019 年的巴西 WiFi 项目。真项目是 **`ry-diffusion/Bergamota`**，
> 从 [karthiknathan98-tech/A50s_IOS](https://github.com/karthiknathan98-tech/A50s_IOS) 的 README 里链到的
> "Project Bergamota" 反查出来的。

---

## 1. 日志与取证机制（重点）

这一节按「不依赖内核正常退出」这条主线组织。前三个项目都**没有串口**（板子上调试 UART 被复用成别的功能；
ruby 有 MTK 的 `expdb` 分区可用，见 §1.5），
所以「怎么在机器死了以后还能拿到死前的话」是它们的核心工程问题。它们最后收敛到**四层**：

```
第 0 层  framebuffer 早期标记（loader/内核最早期，连 printf 都还没有）
第 1 层  屏幕上的诊断页 / 脉冲条（人眼+相机，不依赖任何存储）
第 2 层  持久日志环（内核线程持续 flush 到分区末尾，panic 时再补一刀）
第 3 层  独立 reader 环境（recovery / 自己的 Linux 镜像）把环读出来，自动回 fastboot
（加分项）USB 实时流：内核一起来就把 console 通过 CDC-ECM 打到 PC，不用来回刷
```

### 1.1 ios-sweet：4 MiB 环 + OrangeFox 读 + USB 实时流

**(a) 持久日志环 —— patch 0047**

实现（[xnu/patches/0047-sweet-persistent-console-log.patch](https://github.com/dipa-studio/ios-sweet/blob/main/xnu/patches/0047-sweet-persistent-console-log.patch)）[代码]：

- `osfmk/console/serial_console.c` 的 `_cnputs()` 里加一行 `sweet_log_append(c, size)`，
  于是**所有 console 输出**（内核 printf + 守护进程往 `/dev/console` 写的东西）都同时进一个 4 MiB 静态环形缓冲。
- `bsd/dev/sweet_ufs.c` 起一个内核线程，**分区可写（`ufsrw=1`）后每 ~1 秒**把新增的环块 + 头部写到
  **userdata 最后 1025 个 4 KiB 块**（= 4 MiB 环 + 1 块头），然后 `SYNCHRONIZE CACHE`。
- 头部格式：`"SWEETLOG" | u32 version | u32 ring size | u64 total bytes | u64 flush seq`；
  第 n 字节在环里的偏移 = `n % size`。
- 关掉它：boot-arg `sweetlog=0`。

读取（[tools/sweet-log-pull.sh](https://github.com/dipa-studio/ios-sweet/blob/main/tools/sweet-log-pull.sh)、
[tools/sweet-log.py](https://github.com/dipa-studio/ios-sweet/blob/main/tools/sweet-log.py)）[代码]：

```sh
# 手机进 fastboot -> 临时 fastboot boot OrangeFox -> 等 adb
dev=$(adb shell 'for d in /dev/block/by-name/userdata /dev/block/bootdevice/by-name/userdata; do [ -e $d ] && readlink -f $d && break; done')
bytes=$(adb shell blockdev --getsize64 "$dev")
skip=$((bytes / 4096 - 1025))
adb exec-out "dd if=$dev bs=4096 skip=$skip count=1025" > out.bin   # 只读最后 1025 块
python3 sweet-log.py out.bin out.txt
```

要点：**它读的是分区最后 1025 块，不是文件系统**——所以 recovery 内核不需要 hfsplus 驱动，
`dd` 原始块就行。这是整个设计最聪明的一点。

**(b) 这套机制的已知短板（作者自己写的）**

> "Not yet verified on the phone. **Panics after the last flush are not in it** (screen still shows them)."
> — [NOTES.md 第 94 行](https://github.com/dipa-studio/ios-sweet/blob/main/NOTES.md)

后来 patch 0069 把 flush 周期从 1 s 改成 **10 s**（`ufslogsecs` 可调，为了省 UFS 写放大）
（[0069-sweet-ufs-faster.patch](https://github.com/dipa-studio/ios-sweet/blob/main/xnu/patches/0069-sweet-ufs-faster.patch)，
`PE_parse_boot_argn("ufslogsecs", &sweet_log_secs, …)`，注释写 "the partition every ufslogsecs seconds (default 10)"）[代码]。
**代价：死前 10 秒的话会丢。** 这正是我们「几百字节 loader 输出、零内核行」的同款症状，只是程度不同。

**(c) 第 1 层：framebuffer 早期标记 + 分阶段编号**

patch 0010/0012 在 `arm_init()` 里一路画 `SWEET EARLY 1..20`（以及 panic 时把 panic 文本直接怼到 framebuffer）。
每一号代表一个明确的里程碑（`1` = 进 `arm_init`，`8` = 离开 bootstrap TTBR0，`20` = kprintf 启动完成）。
Bergamota 后来把这个升级成「**默认只记不画**」：`sweet_early_putc()` 先进 1.5 KiB 环，
只有 `sweetearlyfb=1` 或 panic 才画到屏上，`kprintf` 起来后 `sweet_early_flush_log()` 一次性把它灌进正式日志
（[0195-sweet-early-console-log-only.patch](https://github.com/ry-diffusion/Bergamota/blob/develop/xnu/patches-11215/0195-sweet-early-console-log-only.patch)）[代码]。

**(d) 加分项：USB 实时日志（patch 0077）—— 不用来回刷 recovery**

`fastboot boot` 一个带 `sweetusb=2` 的内核，PC 上跑 `tools/sweet-usbnet.sh logs/live.txt`：
等手机的 CDC-ECM 网卡出现 → PC 设 `192.168.7.1/24` → 手机**每 100 ms** 把内核 console 以 UDP 发到
`192.168.7.1:5140`（连上时先补发最后 64 KiB）。`ping 192.168.7.2` 验证链路
（[NOTES.md 第 13 行](https://github.com/dipa-studio/ios-sweet/blob/main/NOTES.md)）[实测]。

这条路线的代价：**USB 起来之前死的，还是得回 recovery 读环**（作者原话）。

### 1.2 Bergamota：把上面三层全部加固（**最有参考价值**）

Bergamota 是 sweet 的 fork，所以 0047 那套它全有；它在此基础上加了四个我们最需要的东西：

**(a) `sweet_log_sync()`：在「可能挂死 SoC」的动作之前主动刷盘，并等最多 500 ms**

```c
void sweet_log_sync(void) {
    if (!running || ml_at_interrupt_context() || !ml_get_interrupts_enabled()) return;
    uint64_t target = os_atomic_load(&sweet_log_total, relaxed);
    wakeup(&sweet_log_chan);
    for (int i = 0; i < 50 && flushed < target; i++) delay(10000);   /* 50 × 10 ms */
}
```
（[0105-sweet-log-sync.patch](https://github.com/ry-diffusion/Bergamota/blob/develop/xnu/patches-11215/0105-sweet-log-sync.patch)）[代码]

调用点非常讲究：**凡是碰 GPU/MCU/TrustZone 这种「一碰就可能整机冻住」的寄存器前，先 sync**
—— 在 `0103-beryllium-gpu-a630-bringup.patch` 里有 **16 处** `sweet_log_sync()` 调用点（逐个上电阶段之间）。
这条对我们直接可用：
我们的 loader 有 `BOARD_MALI_RETURN` / `BOARD_MALI_STAGE` 的「每步写 flash 再回安卓」，
但**内核侧一旦碰到 MFG/GPUEB 就冻住，日志就断在最后一次 flush 之前** —— 补一个 sync 就解决。

**(b) panic 前补一刀：`proc_handle_critical_exit()` 里 sync**

```c
sync(p, NULL, NULL);
#if defined(ARM64_BOARD_CONFIG_SWEET)
    sweet_log_sync();   /* 把 CRASH 行也落盘，再 panic */
#endif
```
（[1002-sweet-log-sync-before-initproc-panic.patch](https://github.com/ry-diffusion/Bergamota/blob/develop/xnu/patches-12377/1002-sweet-log-sync-before-initproc-panic.patch)）[代码]

注释写得很清楚，就是踩过的坑：

> "launchd dying right after the root mount **panicked before its first flush** (first iOS 26 boots:
> `tools/sweet-log-pull.sh` still found the previous iOS 18 log)."

**这正是我们现在的病**：内核在第一次写盘之前就死了 → 盘上只剩 loader 写的几百字节。

**(c) 让 console 被「静音」后仍然进日志**

xnu-11215 的 `console_write()` 在 `disableConsoleOutput` 时**直接 return，走不到环里**，
于是 GUI 一起来、console 被接管，printf 就从两份日志里一起消失：

```c
void console_write(char *str, int size) {
    if (disableConsoleOutput) {
#if defined(ARM64_BOARD_CONFIG_SWEET)
        sweet_log_append(str, size);   /* 11215 在这里就 return 了，环和 _cnputs 都够不到 */
#endif
        return;
    }
```
（[0172-sweet-log-while-console-disabled.patch](https://github.com/ry-diffusion/Bergamota/blob/develop/xnu/patches-11215/0172-sweet-log-while-console-disabled.patch)）[代码]

同一补丁还把 `pe_kprintf.c` 的 `serial_putc_crlf()` 也接了 tee，注释：
"kprintf() does not go through the console ring: tee it into the persistent / live log"。

**(d) `sweetbootlog_keep=1`：把启动日志钉在屏上，GUI 的 swap 照收不画**

排查「GUI 挂死/崩溃循环」时，让 bootlog 独占面板，CoreAnimation 的 swap 全部正常完成但**不扫描输出**
（[1008-sweet-bootlog-keep.patch](https://github.com/ry-diffusion/Bergamota/blob/develop/xnu/patches-12377/1008-sweet-bootlog-keep.patch)）[代码]。
配合 `-v` 用。这解决的是「日志有，但屏上是黑的，我想边看边等」的场景。

**(e) 实测量级**

- boot 0053s 长跑：日志 407 KB、**1428 次 flush**
- boot 0047n：3 分钟 70 KB、111 次 flush
（[NOTES.md 第 83 行 / 第 93 行](https://github.com/ry-diffusion/Bergamota/blob/develop/NOTES.md)）[实测]

**(f) 统一日志（iOS 侧）也有离线取证路径**

`tools/sweet-ios-pull.sh`：在 OrangeFox 里推一个静态 aarch64 `hfsdump`，把
`/private/var/db/diagnostics` + `uuidtext` 只读拷到 /tmp 再 adb pull，用 `unifiedlog_iterator` 转 jsonl
（[NOTES.md 第 325 行](https://github.com/ry-diffusion/Bergamota/blob/develop/NOTES.md)）[代码]。
**这点对我们很关键**：内核 console 只覆盖 kprintf，守护进程的 os_log 全在 tracev3 里；
他们正是靠这个把「IOSurface 版本号不匹配」这种黑屏原因挖出来的（见 §4）。

### 1.3 UniXNU：最完整的「取证体系」，但为 UEFI 路线设计

UniXNU 把它写成了一份可以照抄的流程文档：
[docs/porting/log-capture.md](https://github.com/UniXNU/UniXNU/blob/main/docs/porting/log-capture.md) 和
[docs/guide/debugging.md](https://github.com/UniXNU/UniXNU/blob/main/docs/guide/debugging.md) [代码/文档]。

它的四层比前两家更「产品化」：

**(a) 第 0 层：UEFI 固件日志用「二维码」扫出来**

固件里塞一个 `AdvancedLogDxe` 导出器，Tools → Advanced Logger 把当前固件日志缓冲**变成一串二维码**
在屏上轮播，用手机 App（OneSend / 扫传）扫下来存成 `.log`。
（[log-capture.md §2](https://github.com/UniXNU/UniXNU/blob/main/docs/porting/log-capture.md)）[代码]
**这一招在「连 USB 都还没枚举」的阶段是无敌的**——不依赖任何线、任何驱动。

**(b) 第 1 层：屏幕诊断页 + 脉冲条（可拍照）**

- `boot-progress=1`：全屏 48×120 文本页，30 s 无进展就**冻结**并把画面留着给你拍；
  `=2` 持续刷新。字段包括：`time / idle / IRQ / monitor / root / procs / free`、
  存储请求数、校验块数、失败数、**last PC**、`TCR/TTBR/SCTLR`。
- `boot-diag-resume=10`：用户态接管显示、不再扫描输出后，10 s 没新帧就把面板抢回来重画。
- **脉冲条**：面板最下面 32 px 一条绿色，独立高优先级线程每秒刷
  `P<turn> I<irq> M<monitor>/<sec> B<bind>:<result> d<depth> @<base> lock<busy>/<render> PC<pc>`。
  **它活着的意义：区分「内核还活着但画面没换」和「整机死了」。**
- 拍照 SOP 都写了（关闪光、侧打光、锁对焦、拍标题栏+整页+脉冲条、记 SHA-256）。

**(c) 第 2 层：分区预留区 + 自己的 Linux reader（最接近我们需要的形态）**

参考实现在 [UniXNU-sheng](https://github.com/UniXNU/UniXNU-sheng)：`package/BootLog.c`（2 931 行）[代码]

窗口布局（4 KiB 块）：
```
block 0        头（"SHLOG001", version 5, boot_id, first_lba, window_blocks, …）
block 1..32    内核 message buffer
block 33..     文本槽，每槽 16 块，环形
```

设计要点（每一条都是踩过坑以后加的）：

1. **头最后写**：*"the header names only what is on the medium"* —— reader 永远不会信任一个头没数过的槽。
2. **只写变化的块**：一轮只写这轮文本用到的槽块 + 新内核消息到达的 msg 块，参考板约 **7 KiB/s**；
   panic flush 才写整个 message 区。
3. **panic flush 自带现场**：`boot_log_panic_flush()` 在 message buffer 末尾追加
   ```
   [boot_log] at panic: round N running 1234ms, step 'ufs message write' for 800ms,
              watch reports 0, gpu pool fill none
   [panic] <2048 字节 panic 屏文本>
   ```
   然后 `msg_mode = 0`（线性）、`state = 5`，写 msg 区 → 写头 → `native_ufs_log_sync()`。
4. **round watch 线程**：每 5 s 看一次写者，一轮超过 10 s 就 `printf("boot_log: round N running Ns, in step '%s' for Ns")`
   —— **写者自己卡住也会留下「卡在哪一步」**。这条对我们极有价值：内核死前最后一句往往就是卡在某个 UFS/GPU 步骤。
5. **`autoreset=N`**：写完这轮日志后，标记 bootloader 单元 + 武装 hypervisor watchdog，**N 秒后（+60 s 兜底）
   自动重启回 fastboot**，不用人按电源键。实现见 `boot_reset_arm()`。
6. **reader 是独立 Linux**：`package/linux-log/`：预编译 sheng 内核 + busybox initramfs，
   `init` 只做三件事——把 gadget 配成 CDC-ACM、`find /sys/class/block/*/uevent` 找到 `PARTNAME=userdata`、
   `dd if=/dev/$P bs=4096 skip=$((B-8192)) count=8192 of=/run/log.bin`（**最后 32 MiB**），
   然后开 shell 等 PC 来取；取完跑 `/bin/rebootbl`（`reboot(LINUX_REBOOT_CMD_RESTART2,"bootloader")`）
   **自动回 fastboot**。
   **"Block devices are only read; nothing is mounted."** —— 这一点比 OrangeFox 更安全也更可控。
7. PC 侧 `scripts/read-sheng-log.py` 通过 CDC-ACM 取回、按头解码出
   `header.txt / msgbuf.txt / slots.txt / latest.txt`，**默认取完就自动 rebootbl**。

**(d) 第 3 层：用户态故障取证**

- `BootFaultProbe.c`：在**异常投递之前**抓 EL0 的 `pid/name/PC/LR/SP/FP/ESR/FAR/slide/x0-x28/最多 12 帧`，
  **trap 路径里不许分配内存**。
- `BootProcessWorker.c`：进程栈采样**与日志写者分开**，注释：*"so a user-page fault cannot stop log persistence"*。
- `boot-ui-focus=launchd / boot-ui-focus2=SpringBoard / boot-ui-focus3=backboardd / boot-ui-trace=1`：
  采样指定进程的 PC/LR/FP/栈片段，用 radix-64 压进屏幕行，拍照后用
  `scripts/decode-boot-ui-trace.py` 解回 JSON。

**(e) `patch-xnu-entry-probe.py`：三色条 + 定时复位**

Green（DarwinBoot 交接）/ Blue（MMU off 的内核入口跑到了）/ Magenta（MMU 打开前）。
`entry_probe_reset_delay=6`：MMU 打开后等 6 s **主动 PSCI SYSTEM_RESET** ——
「重启了」= 「MMU 那步过去了」；「没重启」= 「死在 MMU 之前」。**用一个重启当信号位。**
（[debugging.md "Color bars" / "Entry probe reset"](https://github.com/UniXNU/UniXNU/blob/main/docs/guide/debugging.md)）[代码]

### 1.4 四家日志机制横向对比

| 能力 | ios-sweet | Bergamota | UniXNU(sheng) | ruby（MT6877） |
|---|---|---|---|---|
| 早期 framebuffer 标记 | ✅ 1..20 编号条 | ✅ 默认只记不画，panic 才画（0195） | ✅ 三色条 + entry probe | ⚠️ 靠 LK 的可见层 + `expdb` 特征行 |
| 屏幕诊断页 | ❌（只有 console 文本） | ❌ | ✅ 48×120 + 脉冲条 + 冻结 | ❌ |
| 持久环位置 | userdata **最后 1025 块**（4 MiB 环 + 1 头块） | 同左（4 MiB） | userdata **最后 8192 块 = 32 MiB**，头 + 32 块 msg 区 + 16 块/槽的环形文本槽 | `iosroot` **最后 1025 块**（同 sweet 0047） |
| flush 周期 | 1 s → **10 s**（0069，`ufslogsecs`） | **10 s**（`ufslogsecs`，上限 60） | **2 s**（`LogPeriodMs = 2000`） | **1 s**（`ufslogsecs=1` 写进正式 cmdline） |
| 主动 sync 点 | ❌ | ✅ GPU 上电每阶段之间 **16 处** + panic 前（0105 / 0103 / 1002） | ✅ panic flush | ❌（靠 1 s 周期把窗口压小） |
| panic 时补写 | ❌（只靠屏） | ✅ `proc_handle_critical_exit()` 里 sync（1002） | ✅ `boot_log_panic_flush()`，带「卡在哪个 step」 | ⚠️ 未见专门 panic 钩子（未确认） |
| 「写者卡住」自证 | ❌ | ❌ | ✅ `round_watch` 线程 + `[boot_log] at panic: … step '<名字>' for Nms` | ❌ |
| 交接瞬间硬件 dump | ❌ | ❌ | ⚠️ 有 `platform-stage=<n>` 停在某阶段 | ✅ `rubydump=1` → `RUBY DUMP stage=N …` 进环 |
| 读日志的宿主 | OrangeFox（adb dd 原始块） | OrangeFox / 自己的 HFS dump | **自己的 Linux initramfs（只读、不挂载）** | **TWRP 刷进 `boot_a`**（无 recovery 分区） |
| 回 fastboot | ❌ 人手按 Power+VolDown | ⚠️ `sweetreboot fastboot`（要人在 SSH 里敲） | ✅ `rebootbl`（reader 取完自动）+ `autoreset=N`（内核自己定时） | ⚠️ 靠 LK/按键（未确认） |
| USB 实时流 | ✅ UDP 5140 / 100 ms | ✅ 同左 + SSH + NAT 共享 | ✅ CDC-ACM 交互式终端（含截屏、PTY、`reboot.fastboot`） | ✅ vendor-specific EP0 命令通道 + EP2 日志帧（1209:0002） |
| 统一日志（os_log） | ❌（要看只能整盘 pull） | ✅ tracev3 → jsonl | ⚠️ 未强调 | ⚠️ 未强调 |

### 1.5 ★ 额外找到的第四个：`ruby`（MT6877 天玑 1080）—— 这家的日志直觉和我们一模一样

调研过程中在 UniXNU 的 `devices.toml`（374 个设备条目）里翻到 **13 台联发科设备**，
其中 `ruby`（Redmi Note 12 Pro 5G，**MT6877 天玑 1080**）这一家是**唯一一台跑 XNU-8796 + iOS 16.7.16
并且进了 SpringBoard 桌面的联发科机**，和我们的技术栈几乎逐项对齐（联发科 + XNU 8796 + iPhone X 用户态 + 16K 页）。

- 目录仓：[lky005/UniXNU-ruby](https://github.com/lky005/UniXNU-ruby)（只是身份条目）
- **实现在 [lky005/ruby-ios-bringup](https://github.com/lky005/ruby-ios-bringup)**（MIT + APSL 2.0，有中英双语 `docs/zh/`，
  2026-11-07 线 `0195`，patchset 187 文件）
- 状态：锁屏 ~58 s（首启）/ 22 s（热重启）、合成上滑进桌面、**真实 Goodix GT9916R 触摸**
  （EINT14 → 子 EINT controller → 专用 IOWorkLoop oneshot → SPI3 → HID）、音量/电源键、MT6360 电池只读、
  USB EP2 日志通道；**没有** GPU/Wi-Fi/BT/音频/相机/RTC/sleep（[README.zh](https://github.com/lky005/ruby-ios-bringup/blob/main/README.zh.md)）

**它的日志路线 = SWEETLOG 环 + TWRP 读，和我们已有的一模一样，但把三个参数调对了**：

| 项 | ruby 的选择 | 出处 |
|---|---|---|
| 环位置 | **root/`iosroot` 分区最后 1025 个 4 KiB 块**（1 块头 + 4 MiB 环）——直接复用 sweet 的 0047 | [tools/sweetlog/README.md](https://github.com/lky005/ruby-ios-bringup/blob/main/tools/sweetlog/README.md) |
| flush 周期 | **`ufslogsecs=1`（1 秒）**，注释理由是「so the trace is flushed quickly」 | [config/README.md](https://github.com/lky005/ruby-ios-bringup/blob/main/config/README.md)、[config/0179-boot.cmdline](https://github.com/lky005/ruby-ios-bringup/blob/main/config/0179-boot.cmdline) |
| 读的时机 | **TWRP 刷进 `boot_a`**（这台机器没有 recovery 分区、LK 没有 `boot:`，只能刷），`dd` 只读 |
| 读的工具 | `tools/sweetlog/decode.py`，**和 ios-sweet 的 `sweet-log.py` 同一格式** |
| 取证纪律 | 「环是易失的：每次启动都变，可能已经绕圈。**记录环的 SHA-256 和解码文本的 SHA-256 作为本轮证据，不要当固定镜像 hash**」 |

还有一条我们**应该马上抄**的：`rubydump=1` —— 在 USB 起来之前，
**把 `PE_state`/设备树 + IPPC/MAC/PHY 寄存器按阶段打成 `RUBY DUMP stage=N …` 写进 SWEETLOG**
（[0088-ruby-handoff-dump.patch](https://github.com/lky005/ruby-ios-bringup/blob/main/patches/0088-ruby-handoff-dump.patch)）。
这正是「黑屏且内核没起来」时最需要的东西：**把交接瞬间的硬件状态落到盘上**，而不是靠人拍照。

它的「三堵墙」总结也几乎是我们现在的处境（[01-architecture.md](https://github.com/lky005/ruby-ios-bringup/blob/main/docs/zh/01-architecture.md)）：
1. **镜像布局**（LK 只认完整 128 MiB + stock kernel 流大小；36.7 MB 镜像留下不匹配的 AVB footer → `hdr_loaded` 失败）
2. **显示**（可见 buffer 是 `0x7cdfc000`/pitch 4352，不是 kernel fb `0x7ba10000`；按 PA 刷 cache 会 panic，按内核 VA 刷 + `dsb sy` 才行）
3. **用户态**（lsd 首启等一个 MobileGestalt 消费者，内核通知其实已送达 `sent=1`；它用 ICL 4 字节 NOP 绕过消费者）

> **注意**：ruby **不是**我们要照抄的「上游」，因为它自己也是 sweet 的下游（补丁 0080 UFS、0085 触摸之后才是自己的）。
> 但它**是唯一验证过「MTK 平台 + XNU 8796 + 16K 页 + iOS 16.7.16 用户态」这条完整组合的公开实例**，
> 所以它的 `02-hardware.md` / `03-boot-chain.md` / `07-experiments.md` 应该当成**联发科平台的对照检查表**来读。
>
> **它也明确说 GPU 没做**（"GPU/Wi-Fi/蓝牙/音频/相机/SEP 可用：都没移植；UI 是软件渲染"，
> [02-hardware.md](https://github.com/lky005/ruby-ios-bringup/blob/main/docs/zh/02-hardware.md)）。
> 所以在 GPU 这件事上，**中文公开资料里唯一的 Mali-G610 CSF 实现仍然是 RK3588 那家（§3.2 路线 B）**，
> ruby 帮不上忙；但它在「MTK 上电顺序 / GIC / 定时器 / UFS / LK」上的结论能帮我们少踩很多坑。

---

## 2. 启动链与「刷坏了怎么救」

### 2.1 四条启动链

**ios-sweet / Bergamota（shim 路线）**

```
ABL（stock、已解锁）
  → fastboot boot shim-boot.img            ← 什么都不刷进 boot 分区
     → shim：自建 MMU、读活 FDT、从 ramdisk 里取 payload
        → XNU Mach-O + 生成式 ADT + boot-args
           → XNU → UFS 驱动 → userdata 里的 HFS+ root
              → dyld + shared cache → launchd → daemons → backboardd + SpringBoard
```
（[ios-sweet README](https://github.com/dipa-studio/ios-sweet/blob/main/README.md)）[README]

Bergamota 换成**自己的 bootloader**（Enxerto），但仍然是 `boot` 分区或 `fastboot boot`：

```
ABL → Enxerto（boot 分区）→ 解压 Heartwood 载荷 → XNU → HFS+ root
       Enxerto 自带：logo/进度条、Bergamota fastboot（serial bergamota0001）、关机充电屏
```
（[Bergamota README](https://github.com/ry-diffusion/Bergamota/blob/develop/README.md)、
[enxerto/README.md](https://github.com/ry-diffusion/Bergamota/blob/develop/enxerto/README.md)）[代码]

**UniXNU（UEFI 路线）**

```
ABL → fastboot boot boot.img（Android 头，里面是 BootShim + Mu-Silicium FD）
    → UEFI → UniXNU 菜单（读 \EFI\UniXNU\unixnu.toml，加载驱动）
       → DarwinBoot → XNU（+ ADT + boot-args + trust cache）
```
（[UniXNU docs/guide/debugging.md](https://github.com/UniXNU/UniXNU/blob/main/docs/guide/debugging.md)）[代码]

**ruby（MTK LK 路线，和我们最像）**

```
小米 preloader
  -> LK（已解锁，无 `boot:`、无 recovery 分区）
     -> boot_a：Android boot v2，128 MiB，kernel 槽是精确 16,606,685 B 的 gzip 流
        -> shim/loader（raw AArch64）@ 0x40080000
           -> XNU 8796 + ADT + boot-args
              -> UFS LUN 2 -> HFS+ root（iosroot）
                 -> dyld + launchd -> lsd -> backboardd -> SpringBoard
                    -> 显示：IOSurface/IOMFB -> LK framebuffer（0x7cdfc000，pitch 4352）
                    -> 输入：SweetHID -> IOHIDEventService
```
（[01-architecture.md](https://github.com/lky005/ruby-ios-bringup/blob/main/docs/zh/01-architecture.md)、
[03-boot-chain.md](https://github.com/lky005/ruby-ios-bringup/blob/main/docs/zh/03-boot-chain.md)）[代码/实测]

### 2.2 「刷坏了怎么救」——它们真的踩过

**(a) ios-topaz（sweet 系分支，MediaTek 无关但教训通用）：UFS 驱动写坏了 LUN0 的 GPT**

> "The iOS port's UFS driver wrote to LUN0 and **destroyed its GPT** (all LUN0 partitions vanished:
> super, userdata, persist, esp, win). After that ABL could not boot anything. Until that driver is fixed,
> the iOS kernel must not write to the disk (boot it without `ufsrw=1 rootrw=1`), and a fresh GPT dump of
> LUN0 should be taken before every iOS test."
> — [RECOVERY.md](https://github.com/MakrSas/ios-topaz/blob/topaz/RECOVERY.md) [代码/实测]

它的 runbook 写得很完整（可当模板）：先 `fastboot getvar all | grep -E "current-slot|slot-|anti|unlocked"` 定位，
`fastboot getvar partition-size:userdata` 找不到 = **LUN0 GPT 坏了** → 刷回备份的 `gpt_both0.bin`；
`load error` 连环出现先 `fastboot reboot bootloader`（ABL 会进坏状态）；
「Couldn't initialize user 0」= userdata 不是 Android 文件系统，recovery 里 Wipe Data。
**Never 清单**：不要用 ROM 自带的 `gpt_*.bin`、不要 erase esp/win、不要手改 GPT 的 A/B 属性。

**(b) ios-sweet：ABL 在「大 flash 之后」会挂**

> "After a big flash the Xiaomi ABL fastboot hangs: next `fastboot boot` stalls at 'Sending' -> the USER must
> re-enter fastboot by hand (Power+VolDown). **Do not retry commands against the hung ABL.** A flash right
> after `adb reboot bootloader` once failed on chunk 1 (USB protocol error): just retry."
> — [NOTES.md 第 46 行](https://github.com/dipa-studio/ios-sweet/blob/main/NOTES.md) [实测]

同一段还有一条：**刷机中途别拔线**，他们有过 25/25 块时被拔，只能重刷。

**(c) Bergamota：给内核补了「重启路径」，并把 reboot reason 打通**

> "Before 0137 the XNU port had **no restart path at all** (`PE_halt_restart` was never set: 'MACH Reboot'
> printed, then a spin until a watchdog or the user reset the phone)."
> — [docs/REBOOT_MODE.md](https://github.com/ry-diffusion/Bergamota/blob/develop/docs/REBOOT_MODE.md) [代码]

三层写 reason：PM8998 PON 的 `PON_SOFT_RB_SPARE`（0x88f，bits 7:1）、IMEM `restart_reason`（0x146bf65c）、
然后 `PS_HOLD = 0` + 2 s 后 PSCI SYSTEM_RESET。
还加了 **`bergamota` 模式**：写 IMEM = `0x42455247 ("BERG")`，ABL 正常起 boot 分区 → Enxerto 看到 `BERG`
就清掉并进 **Bergamota fastboot**（不是高通 ABL 的 fastboot）。
用户态工具 `sweetreboot fastboot|recovery|bergamota|normal` + `sysctl kern.sweet_reboot_mode`。

**这条我们直接该抄**：我们「四次黑屏进不去」时，如果没有重启路径，就只能等人按电源键；
有了它，`autoreset` + reboot reason 可以做到**无人值守、每轮自动回 fastboot**。

**(d) ios-sweet 的「反例」：内核自己不能 reset**

> "our kernel cannot reset the SoC itself (`PE_halt_restart` is never set; reboot/halt print 'MACH Reboot'/'CPU halted'
> and spin), so the reset came from outside XNU: Qualcomm watchdog or a TZ/XPU violation. Unknown which"
> — [NOTES.md 第 164 行](https://github.com/dipa-studio/ios-sweet/blob/main/NOTES.md) [实测]

**这解释了我们「黑屏 + 不重启 + 不写盘」的一种可能**：内核死了但不 reset，机器就静静躺着。

**(e) ruby（MT6877）：联发科平台没有 recovery 分区、也没有 `fastboot boot`**

这是和我们最像的一条，值得单独记住（[08-recovery.md](https://github.com/lky005/ruby-ios-bringup/blob/main/docs/zh/08-recovery.md)、
[02-hardware.md](https://github.com/lky005/ruby-ios-bringup/blob/main/docs/zh/02-hardware.md)）：

- **LK 命令表里没有 `boot:`** → `fastboot boot` 不可用，**每次实验都必须刷 `boot_a`**。
- **没有独立 recovery 分区** → 想进 TWRP 只能**把 TWRP 刷进 `boot_a`**；这就是唯一的离线抓手。
- **b 槽本身是坏的**：即使刷一个已知可启动的镜像进 `boot_b`，也在 `hdr_loaded` 失败 → 只用 `_a`。
- 恢复 Android = 把**已知可启动的完整 128 MiB 镜像**刷回 `boot_a` + `--set-active=a`；
  验证要看 `getprop sys.boot_completed` / `ro.boot.slot_suffix` / `su -c id`，**不要只看 fastboot 返回 OKAY**。
- `fastboot` 报 `no link` = LK 会话掉了：长按电源 10–15 s，音量下+电源重进，**用后置 USB 口、不要 hub**。
- **绝不刷** `preloader`/`lk`/`persist`/`nvram`/`nvdata`。
- **LK 的日志在 `expdb` 分区（40 MiB）**：这是 MTK 平台白送的一层取证——`dd` 出来 grep 特征行就行：
  `jump to K64`（镜像真被进入）/ `hdr_loaded`、`img_loaded`、`can't find v2 dtb`（LK 拒绝镜像）/
  `decompress kernel`（gzip 流问题）/ `panic` / `mt_disp_update:`、`OVL`（实际扫描的 buffer）。

> ⚠️ 这条对我们（MT6895）**需要现场核实**：xaga 是小米/红米机型，`fastboot boot` 在我们这里是**可用的**
> （现有流程就在用），所以 LK 的 `boot:` 支持情况与 ruby 不同。但「**`expdb` 里有 LK 日志**」这一点
> 在 MTK 平台是共通的，值得优先验证——那可能是我们「黑屏时只有 loader 几百字节」的**第二个免费日志源**。

### 2.3 刷坏的兜底：四家都靠「不碰 boot 分区」

- ios-sweet：`fastboot boot` 只在 RAM 跑，`userdata` 是唯一被写的地方；bootloader/recovery/modem/persist **从不碰**
  → 坏了自己长按 Power+VolDown 就回 fastboot。
- Bergamota：可以 `fastboot flash boot` 做冷启动镜像，也留了 `fastboot boot` 的 RAM 路径。
- UniXNU：boot.img 是 UEFI 固件，配置在 `\EFI\UniXNU\unixnu.toml`，改配置不用重刷。
- ruby：只能刷 `boot_a`（无 recovery 分区、无 `boot:`），所以**每次实验前必须留一份已知可启动的整镜像**，
  并且**先备份 `expdb` 和 GPT**；它把这叫「恢复是工程约束，不是命令清单」。

---

## 3. 驱动策略（GPU / 音频 / codec）

### 3.1 四家共同结论：**Apple 驱动一行都用不了，全部重写**

ios-sweet 的 README 写得很直白：*"Rendering runs entirely on the CPU; there is no GPU driver."*
它的显示路径是**自己实现 IOSurface + IOMobileFramebuffer**（patch 0046），让 CoreAnimation 的
**软件渲染器**（`CA::OGL::SWContext`）画，再 CPU memcpy 到 ABL 留的 framebuffer。
它甚至逆向了 IOSurface/IOMFB 的完整 user-client ABI（选号、结构体偏移、trap 号），
写进 NOTES 里（[NOTES.md 第 97–119 行](https://github.com/dipa-studio/ios-sweet/blob/main/NOTES.md)）[代码/逆向]。
**这是「没 GPU 也能出画面」的完整配方**，而且已经被 Bergamota/topaz 复用。

**ruby 在 MTK 上验证了同一条路的另一半**（[04-display.md](https://github.com/lky005/ruby-ios-bringup/blob/main/docs/zh/04-display.md)）：
surface → IOMFB → `memcpy` → LK framebuffer 能出图、UI 会刷新，但**有两个 MTK 专属的坑**：

1. **可见层不是内核以为的那块**：kernel fb / ATAG 基址是 `0x7ba10000`，但 **LK 实际扫描的是 `0x7cdfc000`、pitch 4352**。
   往 `0x7ba10000` 写 = 全黑（"早期 shim 写 `0x7ba10000`，屏幕不可见")。
   → **换平台第一件事：从 live FDT 的 `mblock-*-framebuffer` + LK 的 `mt_disp_update:` 日志确认真正被扫的地址。**
2. **cache 维护不能用 PA 刷**：`flush_dcache64(pa, len, TRUE)` 直接 panic
   （`phys_to_kv_range: illegal PA: 0x7cdfc000`，因为 framebuffer 不在内核管理的物理段）；
   必须 `flush_dcache64((addr64_t)fPixels, len, FALSE)` + `dsb sy`（按内核 VA 刷）。

对我们是直接可用的：**MTK 的 framebuffer 是 LK 的 overlay 层，不在内核内存图里**，
所以「按物理地址刷 cache」这条在 MTK 上天生不成立。

### 3.2 GPU：现在有三条成熟路线，其中一条**正好是 Mali-G610 CSF**

这是本次调研对我们**最直接有用**的发现。

**路线 A：Metal → Vulkan 翻译（Adreno 系，UniXNU/Bergamota）**

```
QuartzCore → 插件(NativeAdrenoMetal / bgmtl) → metal2vulkan(AIR→SPIR-V)
  → Turnip(Mesa Vulkan) → Darwin KMD → 内核 GPU 驱动 → GPU
```
- UniXNU 的 `Adreno740` + `AdrenoMetal` 在 A740/SM8550 上**声称**跑通
  （[README 新闻条目](https://github.com/UniXNU/UniXNU/blob/main/README.md)）[README]。
- Bergamota 用 **Chlorophyll**（另一个 Metal 驱动，走 Turnip）在 Adreno 630 上跑到了
  **SpringBoard 用 GPU 画图标/模糊，50–75 fps**，但**反复 SMMU fault**，最后靠 kill switch 回退到 stock
  （[NOTES.md 第 47 行起](https://github.com/ry-diffusion/Bergamota/blob/develop/NOTES.md)）[实测]。
  它踩的坑很具体：`RenderBox` 的 uber fragment 在 ir3 里要编译 7 s 并把 GPU 挂死（缺 `nir_lower_returns`）；
  `framebuffer fetch` 在 Turnip 上没有 driver "fb read" 描述符，所有 `[[color(n)]]` 读到空
  （圆角变八边形、mask 失效、背景材质不模糊）。

**路线 B（★ 与我们同款硬件）：Mali-G610 CSF 的 XNU 内核驱动已经开源**

[Ken-u/Mali](https://github.com/Ken-u/Mali)：*"Mali-G610 (CSF) kernel driver for UniXNU: power, firmware, MMU,
command streams and user client"*，跑在 **RK3588（Mali-G610 MC4）** 上，**已经把 iPadOS 17.7.11 的锁屏用 GPU 合成出来了**
（52.6 fps 平均）。

它的中文 bring-up 日志 [docs/notes.zh-CN.md](https://github.com/Ken-u/Mali/blob/main/docs/notes.zh-CN.md)
把 CSF/MCU 的启动序列写成了可照抄的清单 [代码/实测]：

| 里程碑 | 内容 | 关键点 |
|---|---|---|
| M0 | 上电 + 复位 + 读 `GPU_ID` | `GPU_ID = 0xa8670005`（G610, arch 10.8.6），`MMU_FEATURES = 0x2830`（VA48/PA40）；**先看 PMU 状态位再读寄存器**（"不要在没上电的电源域上读 GPU 寄存器，可能挂总线"） |
| M1 | 固件进 MCU 地址空间（**AS0**，自建 AArch64 **4 KiB** 页表）+ MCU 启动 + 全局接口握手 | `mali_csffw.bin` 274 432 B，7 段（1 段 protected-mode 跳过）；CSF interface v1.1.0，8 group × 8 stream；config+ping 要 ACK |
| M2 | 一个 CSG + 一条 queue + 写内存的 job | **坑（花了 4 轮）**：MCU 的队列接口和 suspend buffer **必须放在 MCU shared region**；放到别处会「CSG 能启动但 stream 永不 ACK START」，AS0 报「写 0x1000 的 translation fault」——固件把地址 `& 0x0fffffff` 用了 |
| M2b | `RUN_COMPUTE` + 手写 Valhall 着色器 | **坑（花了 6 轮）**：`STORE` 的 `@` 是数据、`^` 是地址对，写反了**没有任何 fault**，内存就是不变。用「再派发一个 SPD 指向未映射地址的 job 看 AS1 是否报取指 fault」来区分 |
| M3 | IOKit user client + 用户态 DRM 垫片 → PanVK 在板上跑 Vulkan 计算 | 10 个 selector（`DEV_QUERY/VM_CREATE/BO_CREATE/VM_BIND/GROUP_SUBMIT/FENCE_WAIT/…`），BO 非连续（`inTaskWithPhysicalMask`），`IOConnectMapMemory64` 映射 |
| M4 | **backboardd 用 Mali 合成，画面进面板** | 见下 |

M4 的「怎么让 iOS 认这块 GPU」是全套现成的 [代码]：

1. `publishAccelerator()`：**运行时给 `IOAcceleratorES` 挂一个元类 `AGXAccelerator`**（类大小必须等于原版基类），
   设属性 `MetalPluginName = NativeMaliMetal`、`MetalPluginClassName = NativeMaliMetalDevice`、
   `NativeSandboxGrantProcess = <进程名,…>`。
2. iOS 侧原版 `MTLCreateSystemDefaultDevice` 会 `IOServiceMatching("IOAcceleratorES")` → 读 `MetalPluginName`
   → 加载 `/System/Library/Extensions/NativeMaliMetal.bundle` → `initWithAcceleratorPort:`。
3. 插件里是 `metal2vulkan` 的 Rust 桥 + PanVK ICD（`libvulkan_panfrost.dylib`）。
4. 文件投递：`NGPUADD2` 格式的 addon → DarwinBoot 校验后填 ADT `/chosen/memory-map/GpuAddon`
   → 内核写到 Data 卷 → **bindfs 只读并集挂到 `/System/Library/Extensions`**（不动封印的系统卷）。

M4 的四个坑（都是「画面黑/花/被拒」类，和我们症状同族）[实测]：

1. **swap 全被拒**（`reason 4`，surface 格式 `'b3a8'` = 10 位广色域双平面）：QuartzCore 在
   「显示器标志位 3」置位时走 `kCVPixelFormatType_30RGBLE_8A_BiPlanar`。
   根因是偏好设置：**`CFPreferencesCopyAppValue` 在 backboardd 自己的域里读不到，写进
   `.GlobalPreferences.plist` 才生效**（沙盒只放行它读自己列出的域）。
   → 内核开机时直接写 `backboardd.plist` **和** 两个家目录的 `.GlobalPreferences.plist`（`CAEnable…=false`）。
2. **`no-copy buffer refused … Unsupported(HostMemory, NotRepresentable)`**：
   `IOMemoryDescriptor::prepare()` 按写方向装订，**只读映射返回 `KERN_PROTECTION_FAILURE`**；
   改成先试 `kIODirectionInOut`，失败退 `kIODirectionOut` 并把 BO 标 `readOnly` + PTE 只读。
3. **PanVK 缺两个能力**导致 backboardd SIGABRT：`VkExternalBufferProperties` 对 `HOST_ALLOCATION` 要报 importable；
   `VK_EXT_rasterization_order_attachment_access`（QuartzCore 的 `luma_boost_lph` 管线要 FramebufferFetch）。
4. **CSG 被逐出时用 SUSPEND 不要 TERMINATE**（2026-11-05 定位）：
   现象是开机 ~80/290/400 s 突然 `MMU page fault in ASn … status 0x7cc002c1 address 0`，之后所有提交不再完成。
   根因：逐出时 `CSG_REQ = TERMINATE` 把**队列寄存器清零**，而 PanVK 只在一条队列的第一个作业里设一次寄存器。
   修法：逐出改 `CSG_STATE_SUSPEND`（2）+ 放回 `RESUME`（3），suspend buffer 归 group 所有而不是槽位。

**路线 C：GPU 只做「合成/扫描输出」加速，不碰 Metal**（Bergamota 的保守方案，见
[docs/GPU_RESEARCH.md](https://github.com/ry-diffusion/Bergamota/blob/develop/docs/GPU_RESEARCH.md) 的 fallback 段）。
只做内核侧 blit/scale/compose，CoreAnimation 仍然 CPU 光栅化。**投入最小、见效最快。**

> 对我们（Mali-G610 CSF MCU，19 轮未破）：
> **路线 B 有同代同架构（Valhall gen2 / arch 10.8）的完整开源实现**，连 `GPU_ID`、AS0/AS1 页表、CSF 接口版本、
> doorbell 编号、suspend buffer 大小、以及「MCU shared region 必须在 0x04000000–0x08000000」这种硬约束都写清楚了。
> 唯一要换的是 SoC 侧的电源/时钟（MTK 是 MFG0-11 + GPUEB，RK3588 是 power-domain 12 + CRU/SCMI）。

### 3.3 音频 / codec

- **ios-sweet**：不做。根的 `VirtualAudio.plugin` 直接被移走（`/Library/Audio/Plug-Ins/HAL.disabled/`），
  因为 mediaserverd 会 SIGSEGV —— 崩溃链是
  `ApplyBeamFormerPreference <- VirtualAudio_PlugIn::SetPropertyData <- AudioObjectSetPropertyData`，
  默认虚拟音频设备 `'vdef'` 不存在（没有音频硬件、没有 `/product/audio`）
  （[NOTES.md 第 76 行](https://github.com/dipa-studio/ios-sweet/blob/main/NOTES.md)）[实测]。
  **这是「没有 codec 时让系统不崩」的最小改法，我们可直接抄。**
- **Bergamota**：同样没音频；但做了 Wi-Fi 的内核内 WPA2 四次握手、BT 的 HCI 传输
  （[docs/BT_IOS_TRANSPORT.md](https://github.com/ry-diffusion/Bergamota/blob/develop/docs/BT_IOS_TRANSPORT.md)）。
- **UniXNU(sheng)**：有音频（CS35L43 speaker amp + AudioReach），有专门的
  `scripts/check-audioreach-host.py`、`scripts/check-cs35l43-host.py`、`scripts/build-sheng-audio-hal.py`
  [代码]。**这是唯一一家把「codec 驱动」真做出来的**，路径是
  「内核 I2S/TDM + AudioReach DSP 固件 + 用户态 HAL 插件」。
- **ruby（MT6877，同样 MTK）**：也没音频，但它在 **MT6360 的 LDO/寄存器读写**上踩过的坑
  **和我们的「3.5mm 读不到真机寄存器」是同一类故障**，值得直接搬过来当检查表：
  1. **只写 `enable` 不够**：LK/PMIC 默认 `VMC = 3.0 V`、`VTP = 1.8 V`，只置 enable 会让 AVDD 欠压、
     芯片"沉默"（读回全 0）。必须**先读旧 VOUT → 断电 → 写目标电压（1.8 V / 3.3 V）→ 回读**（决策 D10）。
  2. **读回全 0 也要验**：`VER CS ok=1` 是假阳性——**全 0 数据的 checksum 也是 0**；
     必须断言「非全 0 + magic 命中（如 `BERLIN`）」才算通。
  3. **先 bit-bang 再上控制器**：控制器路径混了 FIFO、pad-select 和 CS 行为，
     先 bit-bang 去掉这些变量证明芯片活着，再回头优化（决策 D9）。
  4. **FIFO 剩字节会让读前缀错位**：写成功后读全 0，原因是 FIFO 里剩 3 字节 → 需要 `SPI_CMD_RST` 或 DMA 路径。
  5. **MMU 关闭时小心非对齐 SIMD**：LLVM 在 MMU off 下会生成非对齐 SIMD load，FAR 指向栈就 panic `0x96000021`
     → 关向量化 + `volatile`。

  > 我们 3.5mm 的下一步建议（按 ruby 的顺序）：先确认 codec/耳机的 **供电 rail 与 VOUT 是不是真的到了目标值**
  > （而不是只置了 enable），再用 bit-bang/单步读验证 I²C 通路，最后才怀疑协议/寄存器映射。
  > 出处：[07-experiments.md 失败目录](https://github.com/lky005/ruby-ios-bringup/blob/main/docs/zh/07-experiments.md)。
- 关于我们的 **3.5mm 耳机读不到寄存器**：**没有任何一家**做过 3.5mm/耳机检测的寄存器级工作
  （它们都把音频整块砍了，或只做 speaker amp）。→ 这一项**没有成品可抄**，
  但 ruby 的「rail/电压 → 通路 → 协议」排查顺序可以直接用。
  **如实说：3.5mm 本身是这四个项目的共同空白区。**

---

## 4. 失败案例：黑屏怎么排查的

按「症状 → 定位手段 → 真因」整理。这些是我们最该对着查的清单。

| 项目 | 症状 | 定位手段 | 真因 / 修法 |
|---|---|---|---|
| Bergamota | **黑屏，但日志说 swap 在跑** | 统一日志里 `IOSurface creation failed` ×6065、`CAIOSurfaceCreate: failed` ×6068 | **IOSurface limits 结构体版本**：iOS 18 的 sel 13 返回 0x28 字节、版本在 `+0x22` 且要求 `== 1`；sweet 返回的是 iOS 16 的 0x18/`+0x16`。框架报 "IOSurface.framework versus IOSurface.kext version mismatch" 后关连接，之后每次 create 都失败（0157） |
| Bergamota | **黑屏，但 5000+ 次 swap 被记数** | 发现 `SWEET FB: swap N layer …` 一次都没打 | **swap 记录布局变了**：iOS 18 的 sel 5 是 0x514 字节（iOS 16: 0x46c），字段整体 +0x48，且**没有 layer mask**（id != 0 即在用），驱动还在按老的 0x104 mask / 0x54 id 读（0160） |
| Bergamota | SpringBoard SIGSEGV 在 `vImageDeepmap2Decode` | 栈 + 反汇编 | **create 结果布局变了**：iOS 18 把结果拷到 client+0x70、总 0xc68 字节（iOS 16 是 0xa68），老布局把 `bytesPerElement` 当成 `Offset` 读写越界（0159） |
| Bergamota | **Apple logo 卡死**（不是慢，是真等） | `sweet-log-pull.sh` + `sweet-ios-pull.sh` + `research/smp-sym.py` 符号化 SMP 采样 | SpringBoard t0 等 LaunchServices 上下文锁，锁在 lsd；lsd 每个 bundle 都问 MobileGestalt，而 **MG 的 Wi-Fi 地址解析器**在等 `IOServiceMatching("IONetworkController")` 直到超时（60 次，"Could not get wifi address"）→ mkadt 里塞一个合成 MAC |
| Bergamota | **整机安静、什么都不动** | 串口/USB 日志最后一行 | `sweetfb_noconsole=1` **把 panic 也藏了** —— 看着像挂死，其实是数据异常。教训：**stage 级测试不要开 noconsole** |
| Bergamota | 开机 80/290/400 s 后画面停住 | GPU MMU fault + `did NOT terminate` | CSG 逐出用 TERMINATE 清了队列寄存器（见 §3.2） |
| UniXNU | 卡在 magenta 色条之后，**无 panic 无输出** | 一级一级的 reset probe + 反汇编 `_PE_create_console` | BCM2837 机器配置写死了 `PLATFORM_PANIC_LOG_PADDR`，`check_for_panic_log()` 无条件去 map+读 RPi 的地址；**QEMU 读返回 0 所以 CI 全绿**，真机上 SDM845 该地址是 bus-error → 在 console 存在之前就同步异常死掉。[issue #101](https://github.com/UniXNU/UniXNU/issues/101) |
| UniXNU | 同上（另一家） | 同上 | 缺 panic log 区导致 `debug_log_init()` 两次报错后**永久自旋**（`ios-sweet` 的 PR #6 也有同样记录，并给了 `/pram` + `embedded-panic-log-size` 的修法） |
| UniXNU | fastboot 报 Booting OKAY 但**黑屏、停在 fastboot** | 比对 `image_size` / `text_offset` | shim 的 ARM64 头 `image_size` 只写了 FD_SIZE、`text_offset` 写成绝对地址 0xA7100000，ABL 把镜像放到保留区外 → 黑屏回 fastboot（[piano 记录](https://github.com/UniXNU/UniXNU/blob/main/docs/devices/piano-donyanchen.md)） |
| UniXNU | 黑屏但**什么都没写进去** | — | 把裸 FD 交给 fastboot（不是 Android boot image）→ 手机停在 fastboot 黑屏，一个字节都没写（[cmi 记录](https://github.com/UniXNU/UniXNU/blob/main/docs/devices/cmi.md)） |
| UniXNU | ABL 交接的 framebuffer 拿不到、面板全黑 | 比对设备树 | **ABL 只有在 boot image 带手机自己的 device tree 时才交接 framebuffer**；Mu-Silicium 自带的树里没有 `cont_splash_region@9c000000`（[kebab 记录](https://github.com/UniXNU/UniXNU/blob/main/docs/devices/kebab-stevedang67889.md)） |
| ios-sweet | 屏幕一直红（shim 的红底），XNU 一行不出 | — | `kprintf` 在 `STARTUP_SUB_KPRINTF` 之前是 no-op，**死在 serial_init 之前就一句都没有** → 于是有了 patch 0010 的早期 framebuffer 标记（[NOTES.md 第 419 行](https://github.com/dipa-studio/ios-sweet/blob/main/NOTES.md)） |
| ios-sweet | 一夜之间自己重启进 recovery | 排除法 | 不是 watchdog（bark 20 s 但 panic 屏能挂几分钟）；**内核自己没有 reset 路径**，所以是外部（TZ/XPU 违规或 Qualcomm watchdog），未定论 |
| ruby (MT6877) | 屏幕不可见（全黑） | 比对 LK 扫描层 | console 写在 `0x7ba10000`（kernel fb 基址），**实际可见层是 `0x7cdfc000`、pitch 4352**；早期 shim 因此"以为在画"其实全黑（[03-boot-chain.md](https://github.com/lky005/ruby-ios-bringup/blob/main/docs/zh/03-boot-chain.md)） |
| ruby (MT6877) | 刷完镜像 LK 直接 `hdr_loaded` 拒绝 | dump `expdb` 读特征行 | 镜像必须是**完整 128 MiB**、kernel gzip 流**精确等于 stock 的 16,606,685 字节**、DTB/AVB0/AVB footer 保持 stock 偏移；36.7 MB 镜像会留下不匹配的 AVB footer（[07-experiments.md](https://github.com/lky005/ruby-ios-bringup/blob/main/docs/zh/07-experiments.md) D1） |
| ruby (MT6877) | XNU 在 `initCPU` 之后挂住、无输出 | 先在 shim 里准备 GIC、先跑 1 核 | GIC/timer 没给 secondary 准备好；**并且 MTK TOPRGU watchdog 要喂**（`0x10007000` 写 `0x22000000` 关，自旋里 `0x10007008 = 1` 喂狗） |
| ruby (MT6877) | 触摸 IC 寄存器**全 0**（和我们 3.5mm 耳机的症状同族） | 读 PMIC LDO 的 VOUT 寄存器 | **只写 enable 不够**：LK/PMIC 默认 VMC 3.0 V / VTP 1.8 V，AVDD 欠压 → 芯片沉默。必须先读旧 VOUT、断电、写 1.8 V/3.3 V，再回读（D10） |
| ruby (MT6877) | 触摸全 0 数据却报 `VER CS ok=1` | 校验非零数据和 magic | 全 0 的 checksum 也是 0 —— **校验和通过不代表读到了东西**，必须断言"非全 0 + magic 命中" |
| ruby (MT6877) | probe 阶段 `panic 0x96000021` | 反汇编 + FAR | MMU 关闭时 LLVM 生成了**非对齐 SIMD load**（FAR 指向栈）→ 关向量化 + `volatile` |
| 我们（对照） | 四次黑屏、只有 loader 几百字节 | — | 与上表第 4/5/7 行同族：**死点在「第一次 flush 之前」或「console 被静音之后」**，两种都被上面几家分别解决过 |

**从这张表能提炼的三条通用排查律**（四家都反复踩）：

1. **「屏黑」不等于「没跑」**：先分清是「没输出」「输出到看不见的地方」「输出被静音」。
   Bergamota 的 `SWEET FB: swap N` 计数、UniXNU 的脉冲条就是干这个的。
2. **「日志最后一行」必须自带「我卡在哪一步」**：光有最后一行文本不够，
   要像 UniXNU 的 `[boot_log] at panic: … step 'ufs message write' for 800ms` 一样**记录正在进行的那一步**。
3. **CI/QEMU 全绿不代表真机不黑**：UniXNU #101 是最好的例子（QEMU 读未映射地址返回 0）。
   ios-sweet 因此坚持每个补丁先在 QEMU 跑 45 s（`--secs 45`，12 s 会漏掉 30 s 的 IORTC 等待）。

---

## 5. 对我们最直接可用的 5 条（按紧急度排序）

> **先划清一个版本差异，否则会抄错**：我们在 **xnu-8796**，Bergamota 后来升到 **xnu-11215 / 12377**。
> 两者的 console 路径不同 —— 补丁 0172 的注释原话：
> *"xnu-11215 returns here before the ring and `_cnputs()` (**8796 only checked in `_cnputs`**, where the persistent log /
> USB live log hook sits)."*
> 也就是说：**8796 的 `console_write()` 会走到 `_cnputs()`，所以「静音后丢日志」这个问题在 8796 上不存在；
> 但一旦我们升级内核版本，这个问题立刻出现。** Bergamota 的 0172 是给 11215 打的补丁，我们不能照抄到 8796 上。
> 反过来说：**ios-sweet 的 0047 在 8796 上是成立的**，Bergamota 的 0105/1002（sync）与版本无关，可以照抄。

### ① 立刻：在「内核第一次写盘之前」就把日志落到能读的地方

**先对一下账**：我们自己的 logbook 是 `xnusys` 分区第 653560 块起、**1025 个 4 KiB 块（4 MiB + 1 头块）**，
和 ios-sweet / ruby 的窗口**尺寸和位置约定完全一样**（4 MiB 环 + 1 块头）。
差别只有一处：**我们的写者只有 loader，内核从来没有写过**。
Bergamota 的教训正好命中这一点：

> launchd 在 root mount 之后立刻死 → **panic 发生在第一次 flush 之前** → 恢复出来的还是上一次的旧日志。

四个动作，按代价从低到高：

1. **panic/exit 钩子里同步刷一次**（Bergamota `1002-sweet-log-sync-before-initproc-panic.patch` 的做法）：
   在 `proc_handle_critical_exit()` / `panic()` 里，先 `sweet_log_sync()` 再往下走。
2. **把 flush 周期压到 1 s**。四家都调过这个参数，结论一致：**测试期用 1 s，别用 10 s**——
   ios-sweet 原本 1 s，为省写放大改成 10 s；UniXNU 是 2 s；
   **ruby 直接把 `ufslogsecs=1` 写进正式 boot cmdline**，注释理由是「so the trace is flushed quickly」
   （[config/README.md](https://github.com/lky005/ruby-ios-bringup/blob/main/config/README.md)）。
   我们的 logbook 在 xnusys 末尾，写放大压力比 userdata 小，可以更激进。
3. **在「可能冻住 SoC」的动作前主动 sync**（Bergamota `0105`）：碰 MFG/GPUEB/TrustZone 寄存器之前先刷。
   我们 loader 已经有 `BOARD_MALI_RETURN` 的「每步写 flash 再回安卓」，**内核侧缺这一半**。
4. **把「交接瞬间的硬件状态」也 dump 进环**（ruby `rubydump=1`）：`PE_state`/设备树 + IPPC/MAC/PHY 寄存器
   按阶段写成 `RUBY DUMP stage=N …`。黑屏且内核没起来时，这是唯一能证明「到底交给了 XNU 什么」的证据
   （[0088-ruby-handoff-dump.patch](https://github.com/lky005/ruby-ios-bringup/blob/main/patches/0088-ruby-handoff-dump.patch)）。

> 我们的窗口参数见本项目 `docs/round-16-support/run-once.py`（`slot_block, slot_blocks = 653560, 1025`）。
>
> 出处：<https://github.com/ry-diffusion/Bergamota/blob/develop/xnu/patches-12377/1002-sweet-log-sync-before-initproc-panic.patch>、
> <https://github.com/ry-diffusion/Bergamota/blob/develop/xnu/patches-11215/0105-sweet-log-sync.patch>、
> <https://github.com/lky005/ruby-ios-bringup/blob/main/config/README.md>、
> <https://github.com/lky005/ruby-ios-bringup/blob/main/patches/0088-ruby-handoff-dump.patch>

### ② 立刻：先把「日志到底断在哪一段」量出来（别急着改机制）

Bergamota 最有价值的一条**不是**某个补丁，而是它的方法论：**先证明日志断在哪，再改。**

它踩的具体坑是：`sweetfb_noconsole=1` **连 panic 一起藏了** ——
> *"Caveat: `sweetfb_noconsole=1` hides panics, so it may be an invisible data abort."*
> *"Boots 34/35 were a hidden `ml_io_unmap` panic on a KMA_PERMANENT mapping … stage-4 boots must keep panics visible
> (no `sweetfb_noconsole=1`)."*
> — [NOTES.md 第 165 行](https://github.com/ry-diffusion/Bergamota/blob/develop/NOTES.md) [实测]

所以我们的排查顺序应该是：

1. **关掉任何「静音/隐藏」开关**跑一轮：`sweetfb_noconsole` 之类一律不开，panic 必须画在屏上。
2. **看屏上有没有 panic 文本**：
   - 有 panic 文本 → 内核活着走到了 panic，问题是「没写盘」→ 走 ① 的 sync。
   - 屏上只有 Apple logo / 全黑、无 panic → 死点在 panic 之前（早期 MMU/中断/内存）→ 走第 0 层（早期标记）。
   - 屏上连 loader 的标记都没有 → 死在 shim/loader（不关 XNU 的事）。
3. **对 xnu-8796 的 console 路径做一次静态确认**：`console_write()` → `_cnputs()` → `sweet_log_append()` 这条链上
   有没有提前 return（`disableConsoleOutput` 在 8796 里是在 `_cnputs()` 内部判的，见 §5 开头的版本说明）。
   如果我们要升到 11215，就必须补 Bergamota 的
   [0172](https://github.com/ry-diffusion/Bergamota/blob/develop/xnu/patches-11215/0172-sweet-log-while-console-disabled.patch)。
4. 顺带抄 `sweetbootlog_keep=1`（bootlog 独占面板，GUI swap 照收不画）——这是「边跑边看」的唯一办法
   （[1008](https://github.com/ry-diffusion/Bergamota/blob/develop/xnu/patches-12377/1008-sweet-bootlog-keep.patch)）。

### ③ 一天内可做：把日志区做成「头最后写 + 只写变化块 + 自带卡点」

UniXNU 的 `BootLog.c` 是现成范本（2 931 行，MIT）[代码]，四条规定直接搬：

1. **头最后写**，reader 只信头数过的槽；
2. **只写这轮变化的块**（参考板 ~7 KiB/s；我们 4 MiB 环 + 1 s 周期，量级完全够）；
3. **每轮写一行 `[log round] last/max/slowest_step/slow_rounds`** + panic 时写
   `at panic: round N running Xms, step '<名字>' for Yms`；
4. **round watch 线程**每 5 s 检查写者，一轮超过 10 s 就 `printf` 一行「卡在哪个 step」。

> 出处：<https://github.com/UniXNU/UniXNU-sheng/blob/main/package/BootLog.c>（文件头注释就是完整规格）

### ④ 一天内可做：reader 换成「自己的最小 Linux」，并让它**自动回 fastboot**

我们现在的 `xaga-pull-log.sh` 走的是 recovery/adb 路线（和 ios-sweet 一样）。
UniXNU 的做法更省事、也更适合无人值守：

- 一个 busybox initramfs：只做「配 CDC-ACM → 找 `PARTNAME=userdata` → `dd` 最后 N 块到 /tmp → 开 shell」，
  **"Block devices are only read; nothing is mounted."**
- PC 侧脚本取完自动 `reboot(LINUX_REBOOT_CMD_RESTART2,"bootloader")` 回 fastboot。
- 内核侧再配 `autoreset=N`：本轮日志落盘后 N 秒**自动 PSCI 重启回 fastboot**（Bergamota 用
  PMIC PON + IMEM restart_reason 两条路，`docs/REBOOT_MODE.md`）。

这样「刷一次 → 自己跑 → 自己回来 → 自动取日志」可以串成循环，把四次黑屏这种手工轮次变成脚本。

> 出处：<https://github.com/UniXNU/UniXNU-sheng/blob/main/package/linux-log/init>、
> <https://github.com/ry-diffusion/Bergamota/blob/develop/docs/REBOOT_MODE.md>

### ⑤ 本周：GPU 直接照 `Ken-u/Mali` 的 M0→M2b 走，不要再自创

我们 19 轮卡在 Mali-G610 **CSF MCU**，而这家在 **Mali-G610（同 arch 10.8）** 上从 M0 走到 M4
（iPadOS 锁屏 GPU 合成，52 fps）。它的日志把我们缺的每一环都写了 [代码/实测]：

- 上电后先读什么：`GPU_ID = 0xa8670005`、`MMU_FEATURES = 0x2830`、`SHADER_PRESENT = 0x00050005`、
  `STACK_PRESENT = 0x55`；**上电前先看 PMU 状态位**（读未上电域的寄存器可能挂总线）。
- 固件：`mali_csffw.bin`（274 432 B，sha256 `8e7c821a…712df5`），7 段装进 **AS0**，自建 **4 KiB** 页表。
- **最容易踩死的两条**：
  (a) MCU 的 queue input/output 与 suspend buffer **必须在 MCU shared region**，否则「CSG 起来但 stream 永不 ACK START」+
  「AS0 报写 0x1000 的 translation fault」（固件把地址 `& 0x0fffffff`）；
  (b) 着色器 `STORE` 的 `@`/`^` 写反**不报错**，要用「故意派发到未映射地址看是否报取指 fault」来验。
- 让 iOS 认 GPU 的那一步：运行时给 `IOAcceleratorES` 挂元类 `AGXAccelerator` + `MetalPluginName` 属性（M4）。

> 出处：<https://github.com/Ken-u/Mali>、<https://github.com/Ken-u/Mali/blob/main/docs/notes.zh-CN.md>、
> <https://github.com/UniXNU/UniXNU/issues/127>

**另外三条「顺手就能修」的（不排进前五，但值得记）**：

- **`/pram` + `embedded-panic-log-size` 必须有**，否则 `gPanicBase == 0` → `debug_log_init()` 报两次错后
  永久自旋（ios-sweet PR #6 / UniXNU #101 两处独立复现）。我们 `board.h` 里已有
  `PANIC_LOG_BASE 0x80100000` / `PANIC_LOG_SIZE 0x80000`，**但要确认 ADT 里真的发出了 `/pram` 和
  `/chosen/embedded-panic-log-size`**，否则等于没有。
- ★★ **去翻 MTK 的 `expdb` 分区 —— 我们自己的设备树里已经写明这条路存在**
  （ruby 的发现 + 我们本地资料交叉验证，**这是本次调研里成本最低、收益最高的项**）：
  - ruby：LK 把启动日志写在 `expdb`（40 MiB），`dd` 只读出来 grep
    `jump to K64` / `hdr_loaded` / `img_loaded` / `decompress kernel` / `panic` / `mt_disp_update:` / `OVL`。
  - 我们自己的 `research/raw/mt6895-xiaomi-xaga.dts` 第 72–75 行写着：
    > "Capture the dead boot with `read-lastboot-log.sh` (**XAGR ring in log_store at 0x7ffbf000,
    > copied to expdb by LK**); `cinit` also dumps the whole 2 MB kernel log to `vendor_boot_b`,
    > readable with `read-cinit-dump.sh`. **ramoops now lives in LK's own 0x48090000 window**
    > (inherited from LK's cmdline), which stock keeps across a hard reset."
  - 同文件第 387–389 行有节点：`mblock-6-log_store { reg = <0 0x7ffbf000 0 0x40000>; compatible = "mediatek,log_store"; }`
    → **xaga 上确实有 256 KiB 的 log_store，并且 LK 会把它拷进 `expdb`。**

  **所以我们的「日志拿不到」很可能不是没有日志源，而是没人去读 `expdb` / log_store。**
  下一步（成本极低，纯只读）：`adb shell dd if=/dev/block/by-name/expdb` 拉出来 grep 上面那些特征行，
  再 `dd` 一段 `0x7ffbf000` 附近的物理内存/或直接找 `log_store` 映射。
  ⚠️ 注意：我们 `tree/mtk/tools/xaga-pull-log.sh` 的注释写 "this board has no UART, **no ramoops and no pstore**"，
  和 dts 的 "ramoops now lives in LK's own 0x48090000 window" 矛盾 —— **这两句必须现场对一次**，
  很可能是「Android 侧的 pstore 没了，但 LK 侧那份还在」。
- **没有音频硬件时，先把 `VirtualAudio.plugin` 移出 `HAL/`**，否则 mediaserverd 会无限 SIGSEGV
  （ios-sweet 的修法，一条 mv 就够）。

---

## 6. 所有引用 URL

**ios-sweet**

- 仓库：<https://github.com/dipa-studio/ios-sweet>
- README（启动链、日志环、boot-args 表）：<https://github.com/dipa-studio/ios-sweet/blob/main/README.md>
- NOTES.md（433 行工程日志，每次 boot/crash 都在里面）：<https://github.com/dipa-studio/ios-sweet/blob/main/NOTES.md>
- 持久日志环补丁 0047：<https://github.com/dipa-studio/ios-sweet/blob/main/xnu/patches/0047-sweet-persistent-console-log.patch>
- 日志拉取脚本：<https://github.com/dipa-studio/ios-sweet/blob/main/tools/sweet-log-pull.sh>、
  <https://github.com/dipa-studio/ios-sweet/blob/main/tools/sweet-log.py>
- 崩溃符号化：<https://github.com/dipa-studio/ios-sweet/blob/main/tools/sweet-crash.py>
- 移植指南（含「卡在哪一步」症状表）：<https://github.com/dipa-studio/ios-sweet/blob/main/site/porting.html>
- 未解决问题（Metal/BT/通话）：<https://github.com/dipa-studio/ios-sweet/blob/main/site/future.html>
- PR #6（`/pram` panic 区，含 gPanicBase 卡死证据）：<https://github.com/dipa-studio/ios-sweet/pull/6>
- issue #14（SM8350 undefined-instruction，含 boot image v3 ramdisk 被 ABL 丢掉）：<https://github.com/dipa-studio/ios-sweet/issues/14>

**Bergamota**

- 仓库：<https://github.com/ry-diffusion/Bergamota>（分支 `develop`）
- README（Enxerto/Heartwood/Renegade 四件套、状态表）：<https://github.com/ry-diffusion/Bergamota/blob/develop/README.md>
- NOTES.md（342 KB，最详细的踩坑记录）：<https://github.com/ry-diffusion/Bergamota/blob/develop/NOTES.md>
- Enxerto 引导器：<https://github.com/ry-diffusion/Bergamota/blob/develop/enxerto/README.md>
- 重启/回 fastboot：<https://github.com/ry-diffusion/Bergamota/blob/develop/docs/REBOOT_MODE.md>
- 日志 sync 补丁：<https://github.com/ry-diffusion/Bergamota/blob/develop/xnu/patches-11215/0105-sweet-log-sync.patch>
- panic 前 sync：<https://github.com/ry-diffusion/Bergamota/blob/develop/xnu/patches-12377/1002-sweet-log-sync-before-initproc-panic.patch>
- console 静音后仍记录：<https://github.com/ry-diffusion/Bergamota/blob/develop/xnu/patches-11215/0172-sweet-log-while-console-disabled.patch>
- 早期日志只记不画：<https://github.com/ry-diffusion/Bergamota/blob/develop/xnu/patches-11215/0195-sweet-early-console-log-only.patch>
- bootlog 独占面板：<https://github.com/ry-diffusion/Bergamota/blob/develop/xnu/patches-12377/1008-sweet-bootlog-keep.patch>
- GPU 研究（Metal→Vulkan 路线与工作量估计）：<https://github.com/ry-diffusion/Bergamota/blob/develop/docs/GPU_RESEARCH.md>
- SSH over USB（含 live log 说明）：<https://github.com/ry-diffusion/Bergamota/blob/develop/docs/SSH_OVER_USB.md>

**UniXNU**

- 主仓：<https://github.com/UniXNU/UniXNU>，文档站 <https://unixnu.com>
- 日志与取证总纲：<https://github.com/UniXNU/UniXNU/blob/main/docs/porting/log-capture.md>
- 调试手册（诊断页/脉冲条/三色条/entry probe/拍照 SOP）：<https://github.com/UniXNU/UniXNU/blob/main/docs/guide/debugging.md>
- bring-up 分阶段：<https://github.com/UniXNU/UniXNU/blob/main/docs/porting/bring-up.md>
- 排错表：<https://github.com/UniXNU/UniXNU/blob/main/docs/reference/troubleshooting.md>
- sheng 设备仓（BootLog.c / linux-log reader）：<https://github.com/UniXNU/UniXNU-sheng>
- BootLog 实现：<https://github.com/UniXNU/UniXNU-sheng/blob/main/package/BootLog.c>
- reader init（只读、不挂载）：<https://github.com/UniXNU/UniXNU-sheng/blob/main/package/linux-log/init>
- reader 取日志 + 自动回 fastboot：<https://github.com/UniXNU/UniXNU-sheng/blob/main/scripts/read-sheng-log.py>
- issue #101（panic log 固定地址在非 RPi 上致命，QEMU 掩盖）：<https://github.com/UniXNU/UniXNU/issues/101>
- issue #127（Mali-G610 CSF 的 Metal 加速，指向 Ken-u/Mali）：<https://github.com/UniXNU/UniXNU/issues/127>

**Mali-G610 CSF（对我们最相关的外部实现）**

- 内核驱动：<https://github.com/Ken-u/Mali>
- 中文 bring-up 日志 M0→M4（含全部坑）：<https://github.com/Ken-u/Mali/blob/main/docs/notes.zh-CN.md>
- Metal 前端/PanVK 桥：<https://github.com/Ken-u/MaliMetal>
- 设备仓（RK3588 EVB10）：<https://github.com/Ken-u/UniXNU-evb10>

**ruby（MT6877，最贴近我们的联发科实例）**

- 实现仓：<https://github.com/lky005/ruby-ios-bringup>（中文 README：<https://github.com/lky005/ruby-ios-bringup/blob/main/README.zh.md>）
- 目录仓：<https://github.com/lky005/UniXNU-ruby>
- 中文文档总目录：<https://github.com/lky005/ruby-ios-bringup/blob/main/docs/zh/README.md>
- 架构与「三堵墙」：<https://github.com/lky005/ruby-ios-bringup/blob/main/docs/zh/01-architecture.md>
- 硬件坐标（CPU/GIC/定时器/UFS/显示/LK 扫描层）：<https://github.com/lky005/ruby-ios-bringup/blob/main/docs/zh/02-hardware.md>
- 启动链、128 MiB 布局、expdb、TOPRGU：<https://github.com/lky005/ruby-ios-bringup/blob/main/docs/zh/03-boot-chain.md>
- 显示（cache 维护、可见层地址）：<https://github.com/lky005/ruby-ios-bringup/blob/main/docs/zh/04-display.md>
- 用户态（lsd/MobileGestalt 死锁 + ICL 4 字节 NOP）：<https://github.com/lky005/ruby-ios-bringup/blob/main/docs/zh/05-userland.md>
- 实验/失败目录/11 条决策：<https://github.com/lky005/ruby-ios-bringup/blob/main/docs/zh/07-experiments.md>
- 恢复（TWRP 刷 boot_a、expdb 特征行、硬规矩）：<https://github.com/lky005/ruby-ios-bringup/blob/main/docs/zh/08-recovery.md>
- USB 上位机通道（EP0 vendor 请求、511 B 帧、XACT）：<https://github.com/lky005/ruby-ios-bringup/blob/main/docs/zh/12-usb-host.md>
- SWEETLOG 解码器（和 sweet 同格式）：<https://github.com/lky005/ruby-ios-bringup/blob/main/tools/sweetlog/README.md>
- 交接 dump 补丁（`rubydump=1`）：<https://github.com/lky005/ruby-ios-bringup/blob/main/patches/0088-ruby-handoff-dump.patch>
- 状态/验证/保证（记录风格值得抄）：<https://github.com/lky005/ruby-ios-bringup/blob/main/docs/records/STATUS.md>
- boot cmdline（`ufslogsecs=1` 等）：<https://github.com/lky005/ruby-ios-bringup/blob/main/config/0179-boot.cmdline>

**同族分支（补充证据）**

- ios-topaz（sweet 系，MediaTek 无关但救砖 runbook 极完整）：<https://github.com/MakrSas/ios-topaz>
  - 救砖手册：<https://github.com/MakrSas/ios-topaz/blob/topaz/RECOVERY.md>
- hackiosx.dev（社区文档，症状表很实用）：<https://hackiosx.dev/porting>
  ⚠️ 该站对 curl/无头请求返回 **403**（反爬），我没能直连验证；但同一份内容以静态页形式在
  ios-sweet 仓库里，**可直连核对**：<https://github.com/dipa-studio/ios-sweet/blob/main/site/porting.html>
  （症状表在同页，未解决问题在 <https://github.com/dipa-studio/ios-sweet/blob/main/site/future.html>）

**扑空/存疑说明**

- `Marcelo-Barella/bergamota`（<https://github.com/Marcelo-Barella/bergamota>）是 Nuxt 作品集网站，**不是**本项目。
- `bergamota-wifi/berga-*` 是 2019 年的巴西 WiFi 管理项目，**不是**本项目。
- `DoritoYT9/Bergamota`、`shrimpapplepro/bergamota-equuleus`、`karthiknathan98-tech/A50s_IOS` 都是
  `ry-diffusion/Bergamota` 的下游移植仓（README 里自述），不是上游。
- Bergamota 仓库**没有 LICENSE 文件**（GitHub API `license: null`），商用/再分发前需向作者确认 —— 未确认。
- 三个项目**都没有** 3.5mm 耳机 / codec 寄存器级记录，这块没有先例。
