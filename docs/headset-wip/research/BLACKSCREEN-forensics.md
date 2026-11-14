# 黑屏取证：adb/fastboot 都不可用时怎么拿日志

> 调研对象：HakiOS / Xiaomi xaga (MT6895)，XNU 裸机移植，连续四次黑屏无日志。
> 结论全部标注出处：`文件:行号` 指本机代码，`[URL]` 指外部资料，`未确认` 表示没有一手证据。
> 本文只做只读分析与建议，**没有**执行任何 adb / fastboot / 真机操作。

---

## 0. 一句话：最可行方案

**分两半说。**

**立刻能做的（零代码、零风险）**：fastboot 在故障时其实还活着（能刷 `boot_a` 就证明 LK 在跑），
而 xaga 自己的 LK 里就带 `oem lkmsg` / `oem lpmsg` / `oem dump_pllk_log` / `oem expdb` / `oem fbreason`
五个**只读**命令（`rom/lk.img` 与 `dump/lk_a.img` 字符串双重确认，同 SoC 家族有实战先例）。
所以在写任何代码之前，**现在就能把 LK/preloader 侧日志捞出来**。

**而 XNU 那 1.5 MB DRAM 缓冲之所以一次都没拿到**，是我们自己 loader 里**三个 bug 叠在一起**：

1. **watcher 永久停机**：`XNU_WATCH_HEARTBEAT` 取默认值 1，于是 120 s 后满屏绿后
   `for(;;) wfe`（`mtkloader.c:2934-2940`）——**不复位、不快照、后面 120 行诊断全是死代码**；
2. **`MARK_RUNNING` 从不点亮**：`XNU_WDT_SECONDS=0` → `marker_set(0)`（`:3582-3592`），
   于是 `if (mark == MARK_RUNNING) rescue_log();`（`:7344`）这条分支**永远进不去**；
3. **`rescue_log()` 根本不写 flash**：`:2635` 只调 `save_log()` → `plog_putc()`，
   而 xaga 是 `BOARD_NO_PLOG`，`plog_putc()` 是**空函数**（`mtkdbg.h:274-277`）。

→ 净效果：logbook 里永远只有 `logbook_boot_note()`（`:1998`）那 768 字节。
**这三个 bug 加起来约 5 行改动**（见 §4 方案 2/3），改完下一次黑屏就能拿到完整内核日志。

---

## 1. 联发科取证硬件/分区（expdb、log_store、BROM）

### 1.1 `expdb`：128 MiB，MTK 的异常数据库，**是 flash 上的，能跨冷复位**

| 事实 | 值 | 出处 |
|---|---|---|
| 分区名 | `expdb` | `xaga_scatter.xml:263` |
| 起始 | `0xbd80000` | `xaga_scatter.xml:267-268` |
| 大小 | `0x8000000` = **128 MiB** | `xaga_scatter.xml:269` |
| Android 块设备 | `/dev/block/sdc11` | `dump/byname.txt`（`expdb -> /dev/block/sdc11`） |

里面装什么（全部来自 xaga 自己 `lk_a.img` 的字符串，`strings -a dump/lk_a.img`）：

```
kedump: Partition [%s] is not exist.
kedump: WARN - read size from expdb %ld.
kedump: found content in expdb
kedump: skip due to expdb has data
kedump: erase failed, partition [%s] is not exist.
kedump: flush failed %d, @0x%llx
kedump: failed to update panic header
kedump: crc = 0x%x
kedump: write oversize %lx -> %llx > %lx
SYS_PSTORE_RAW / pstore_raw / SYS_RAMCONSOLE_RAW / SYS_CUR_PLLK
-- Compress expdb offset %llx, size %llu
panic_header.exp_type = 0x%x
panic_header.wdt_status = 0x%x
```

**读法（三条，按代价排）**：

1. **`fastboot oem expdb`** —— LK 自带命令（`rom/lk.img` 字符串 `oem expdb`、`read_expdb: offset_src = %llx, len = %llu`）。
   不需要 Android、不需要 root、不需要 mtkclient。这是最便宜的一条。
2. **`mtkclient` 直接读分区**：`python mtk r expdb expdb.img`，然后 `strings expdb.img`。
   注意 MT6895 是 **V6 协议 + BootROM 已打补丁**，必须走 **preloader 模式**（不按键、直接插线，
   USB `0e8d:2000`）并用 `--loader` 指定 `Loaders/V6` 里的 loader —— 见 mtkclient README
   原文："These chipsets use a new protocol called V6 and the bootrom is patched. You need to use
   the `--loader` option and a proper loader from the Loaders/V6 directory. Bootrom won't work, you
   need to use preloader mode" [https://github.com/bkerler/mtkclient]。
3. 已封装好的工具：`mtk-log-util`（`logutil.py expdb` / `pstore`，自动 `strings`）
   [https://github.com/chkndrp/mtk-log-util]；`dump_pllk_log`（走原始 USB 把 `oem dump_pllk_log`
   的 20 字符分块拼回完整文本）[https://github.com/R0rt1z2/dump_pllk_log]；
   Windows 版格式化工具 [https://forum.xda-developers.com/t/tool-windows-pl-lk-log-formatter.4763660/]。

> ⚠️ **格式细节未确认**：`kedump` 写进 expdb 的记录带 CRC（`KEDUMP_CRC`）和 `panic_header`，
> 内部是按 `SYS_*` 名字分条目的压缩块（`-- Compress expdb offset ...`）。**没有找到
> 一手源码**（`grep.app` 被 Vercel 人机校验挡住，MTK 的 `bootable/bootloader/lk/app/mt_boot/aee/KEDump.c`
> 不在公开仓库里）。实用结论：**不要试图解析格式，直接 `strings`**——这是 `mtk-log-util` 的做法，
> 也是社区公认做法。

### 1.2 LK 的 `log_store`：`0x7ffbf000`，256 KiB —— **这条是「持续 flush」的正解，而且是原厂自带的**

**内存位置（一手）**：真机 Android dmesg 里内核 log_store 驱动自己打印的

```
[    0.898285] sig 0x5678ef90 flag 0x627 add 0x7ffbf000 size 0x40000 offsize 0x20 point 0x1490f
[    0.898414] log store:sram_dram_buff addr 0x7ffbf000, size 0x40000.
[    0.898424] [PHY layout]log_store_mem:0x7ffbf000-0x7fffefff (0x40000)
```

出处：`codex/out/round-8-support/trial-672-android-dmesg.txt`（我们自己从这台机器抓的 dmesg）。

**结构（可反推，可信度中高）**：`sig=0x5678ef90`、`offsize=0x20`（**头 32 字节**）、
`size=0x40000`（256 KiB 环）、`point=0x1490f`（= 84367，**已写约 82 KiB**）、`flag=0x627`（状态位）。
即「32 字节头 + 环形文本区」，与 `mtkdbg.h:252-256` 里 ramoops 的 `{sig,start,size}` 三字段风格一致。

**DTS 声明（一手，MTK mainline 移植仓库）**：`mblock-6-log_store`，
`reg = <0x00 0x7ffbf000 0x00 0x40000>`，`compatible = "mediatek,log_store"`
（本机副本 `research/raw/mt6895-xiaomi-xaga.dts:387-390`；
上游 `https://github.com/MT6895-Mainline/linux` 的 `arch/arm64/boot/dts/mediatek/mt6895-xiaomi-xaga.dts`）。
注意该节点**没有 `no-map`**（同文件 387-390），与 ramoops 那块（367-380，有 `no-map`）不同。

**机制（关键，有原厂注释背书）**：同文件 `:73` 的注释原文

> "Capture the dead boot with `read-lastboot-log.sh` (**XAGR ring in log_store at 0x7ffbf000,
> copied to expdb by LK**); cinit also dumps the whole 2 MB kernel log to vendor_boot_b"

也就是说：**内核 printk 持续镜像进 0x7ffbf000 的 XAGR 环 → 下次开机 LK 把它拷进 expdb → 从此在
flash 上，随便什么时候读**。这就是「不依赖系统正常退出」的持久化通道，MTK 原厂已经做完了。
（`read-lastboot-log.sh` 这个脚本本机三个仓库里都没找到，某前序调研也确认 404 —— 见
`research/WEB-xnu-android-projects.md:666,854`，**属于「机制存在但脚本不存在」**。）

**LK 侧对应实现（字符串证据，`rom/lk.img`）**：
`LOG_STORE_LK`、`log_store_lk_init`、`log_store_new`、`log_store_sync_sram`、
`logstore_sync_callback`、`logstore_shutdown_callback`、`log_store: init log store addr:0x%x, size: 0x%x!`、
`%s: mblock alloc error for log_store`、`log_store dts write failed`；
DTS 里还挂了 `logstore { enabled = <0x01>; pmic_register = <0xa0d>; }`（`dump/lk_live.dts:4732-4736`）
和 `/soc/logstore` 别名（`dump/lk_live.dts:11186`）。
→ **「LK 把 log_store 落盘到 expdb」这个机制在 xaga 的 LK 里是编进去的**，✅ 确认；
**具体触发时机（正常开机 / 异常 / shutdown）未确认**。

### 1.3 `aee_debug_kinfo` / `minirdump` / `aee_lk`：都在 DRAM 里，热复位能留，冷复位会烂

| mblock | 区域 | base | size | compatible |
|---|---|---|---|---|
| 8 | aee_debug_kinfo | `0x48080000` | `0x10000` | `mediatek,aee_debug_kinfo` |
| 10 | minirdump | `0x48170000` | `0x10000` | `mediatek,minirdump` |
| 11 | aee_lk | `0x50700000` | `0x800000` | `mediatek,aee_lk` |

出处：`research/raw/mt6895-xiaomi-xaga.dts:355-362`；本机 `xaga-facts.md:356-358`。

真机侧印证（`trial-672-android-dmesg.txt`）：

```
[    0.890402] [mrdump] phys:0x48080000 - 0x48090000 (0x10000)
[    0.890429] mrdump_parse_chosen: mrdump_cbaddr=48082000, mrdump_cbsize=8000
[    0.890432] mrdump_parse_chosen: lk version MRDUMP11
[    0.893997] mrdump: hang data 0xffffff80c9000000 size:0x200000
```

`mrdump` 需要 `mrdump.ko` + `aee_aed.ko`（Android 用户态模块，`init: Loading module /lib/modules/mrdump.ko`），
**XNU 下这套完全不存在**，所以 `aee_debug_kinfo` / `minirdump` 对 HakiOS **当前是死资源**。
唯一可用的形态是：LK 在异常时（它自己 catch 到的）往这几个区写，然后 `oem mrdump` / `oem expdb` 读回来。
`fastboot oem mrdump_chkimg` / `mrdump_fallocate` / `mrdump_out_set` 三个命令都在 LK 里
（`rom/lk.img`），但它们面向「LK 自己 dump」，**不覆盖 XNU 的 log**。

### 1.4 BROM / Preloader / mtkclient：**能不改数据地读**

| 能力 | 命令 | 是否写 | 出处 |
|---|---|---|---|
| 读分区 | `python mtk r <part> <out>` | 否 | mtkclient README |
| 读物理内存 | `python mtk da peek <addr> <len>` | 否 | mtkclient README |
| stage2 读内存到文件 | `python stage2 memread <addr> <len> --filename f.bin` | 否 | mtkclient README |
| 读 preloader / brom | `mtk dumppreloader` / `mtk dumpbrom` | 否 | mtkclient README |
| 进 BROM | 关机后 `vol± + power` 插线 | 否 | mtkclient README |
| **MT6895 特例** | **必须 preloader 模式 + `--loader` V6** | 否 | mtkclient README 首段 |

> 🔴 **风险分级**：以上 `r` / `da peek` / `memread` 全是**只读**，不动 flash 内容。
> 但「进入 preloader/BROM」这个动作本身通常伴随一次复位；**mtkclient 的 `payload` 类操作会自动
> 打补丁并可能写内存**——只做取证时**不要**碰 `payload` / `w` / `e` / `da seccfg`。
> `mtk r` 只读、不落盘到设备，安全；代价是时间（expdb 128 MiB 读取约几分钟）。

---

## 2. 不依赖系统正常退出的机制（对比）

| 机制 | 持久位置 | 需要内核做什么 | 需要「正常退出」吗 | xaga 现成度 | 可靠性 |
|---|---|---|---|---|---|
| **A. ramoops / pstore**（stock MTK） | DRAM `0x48090000`（896 KiB），LK 再搬 expdb | printk 自动写（内核自带） | 不需要（panic/WDT 都写） | ✅ 分区/cmdline 都在 | 中（DRAM 需活着，见下） |
| **B. log_store XAGR 环**（stock MTK） | DRAM `0x7ffbf000`（256 KiB）→ LK 搬 expdb | `CONFIG_MTK_DRAM_LOG_STORE` 驱动 | 不需要 | ✅ LK 侧已在，**内核侧 XNU 没有** | 中高 |
| **C. 我们自己的 logbook**（`HLOG`） | flash：`xnusys` 末 8 MiB | loader 主动写 | **需要**（`xnu_returned`/probe/看门狗分支） | ✅ 已实现 | 高（真 flash）但触发点少 |
| **D. watcher 快照 → 下次开机落盘**（`SNAP`） | DRAM `0x90000100`（1 MiB）→ 下次 loader 写 flash | loader 的第二个核 | 不需要（自己复位） | ⚠️ 已实现但**被 bug 废掉**（§5） | 中高，**修完最好用** |
| **E. 独立 recovery 外部读** | — | — | — | ❌ xaga **无 recovery 分区**，TWRP 靠 `fastboot boot` 也**没有**（`board.h:740-743`） | 不可行 |
| **F. UART / p2u** | 实时 | 内核 console 指到 UART | 不需要 | ❌ 我们 `BOARD_NO_UART=1`（`board.h:896`） | 需额外硬件 |
| **G. 内核自己持续写 flash** | flash | XNU 需要 UFS 驱动 | 不需要 | ❌ xaga 上 `CONFIG_HAIKOS_UFS` 未开、也无 native UFS（`mtk/mt6895/configs/mt6895.config:10-16,39`） | 当前不可行 |

### 2.1 关于 A（ramoops）在 MTK 上的正确配法

xaga 的真机配置（`dump/cmdline.txt`，与 `trial-672-android-dmesg.txt` 一致）：

```
log_buf_len=2M
ramoops.mem_address=0x48090000
ramoops.mem_size=0xe0000
ramoops.pmsg_size=0x10000
ramoops.console_size=0x40000
```

真实布局（`dump/iomem.txt`，逐个 4 KiB 页列出，可比对）：

```
48080000-4817ffff : reserved
  48090000 … 4811efff : ramoops:dmesg(0/142) … (142/142)     ← 0x90000
  4811f000-4815efff : ramoops:console                        ← 0x40000
  4815f000-4815ffff : ramoops:ftrace(0..7/7)                 ← 0x2000（8×0x400）
  48160000-4816ffff : ramoops:pmsg                           ← 0x10000
```

→ **`board.h:200-224` 的注释和常量完全正确**（dmesg 0x90000 / console 0x40000 / pmsg 0x10000）。

⚠️ **但 xaga 上这套对我们没用，原因是硬的**：`BOARD_NO_PLOG=1`（`board.h:908`），
注释说明了这是**实测**结论：

> "It is DRAM the Android ramoops driver has claimed, and on this board it is not ours to write:
> the first character that lands in it is the last thing the loader does."（`board.h:900-907`）

「**第一次写就死**」——写 `0x4811f000` 会把 loader 挂住。原因未确认（怀疑当时 MMU/属性不对，
或该页被 secure world 保护），**不要**在没搞清前把 `BOARD_NO_PLOG` 关掉。

⚠️ **DRAM 会烂**：MTK mainline 的作者注释（`research/raw/mt6895-xiaomi-xaga.dts:1546-1556`）：

> "The WDT's external reset output is wired to the PMIC and turns an AP_WDT timeout into a full
> **cold reset, which is what rots the ramoops window (~13% of bits)** before pstore can be read.
> Keep the reset internal to the SoC so LK sees a normal **warm** AP_WDT reboot and the DRAM
> contents survive." — 并给了 `&watchdog { mediatek,disable-extrst; };`

**这条极其重要**：xaga 的 WDT 超时默认是 **PMIC 冷复位**，会毁掉约 13% 的 DRAM 位；
把 `WDT_MODE.EXRST_EN`（bit 2）关掉、只做 SoC 内部复位，才是 **warm reset，DRAM 保住**。
上游驱动里就是这一位：`WDT_MODE_EXRST_EN (1 << 2)`，`mediatek,disable-extrst` → `reg &= ~WDT_MODE_EXRST_EN`
（`https://github.com/torvalds/linux/blob/master/drivers/watchdog/mtk_wdt.c`，`mtk_wdt_set_timeout`）。

**我们 loader 的对应实现是「碰巧对」的**：`wdt_reset()`（`mtkdbg.h:222-234`）写的是
`TOPRGU_MODE = TOPRGU_MODE_KEY | 0x1`，**只置 EN(bit0)，没有置 EXRST_EN(bit2)**
→ 得到的是 **内部 warm reset**，DRAM 保住。✅ 这对取证是好事，值得写进文档免得以后被人「顺手补上」。

### 2.2 「内核持续 flush 到持久位置」——有人做过吗

**有，而且是联发科原厂**：就是 §1.2 的 `log_store`（XAGR 环）。Android 侧由
`CONFIG_MTK_DRAM_LOG_STORE` 驱动持续镜像 printk（`k-xaga-16.2/drivers/misc/mediatek/Makefile:74`
有 `obj-$(CONFIG_MTK_DRAM_LOG_STORE) += log_store/`，**但源码目录在本机 vendor 树里是空的**，
未确认其实现细节）。这条思路对 HakiOS 的可用形态是：

- **内核持续写 DRAM 环**（不需要 UFS 驱动，只 memcpy + 周期性 `dcache_clean`）；
- **持久化交给下一棒的软件**（LK 或我们自己的 mtkloader）。

在 xaga 上，「下一棒」我们有两条可选：
1. 让 LK 干（写 `0x7ffbf000`，靠 `log_store_sync_sram` → expdb）——**零代码**，但要 32 字节头格式对；
2. 让 mtkloader 干（写 `0x90000100` 的 `SNAP` 区）——**已经在跑的实现**，见 §5。

### 2.3 🔴 「怎么把手机救回来」这件事本身会毁掉证据

这条是**最容易踩、代价最大**的一步，比任何代码都重要：

| 复位方式 | 复位类型 | DRAM 里 SNAP / ramoops / log_store 还在吗 |
|---|---|---|
| watcher 的 `psci_system_reset()` | SoC warm reset | ✅ 在 |
| loader 的 `wdt_reset()`（`mtkdbg.h:222-234`，`MODE = KEY|0x1`，**不含 EXRST_EN**） | SoC 内 warm reset | ✅ 在 |
| `fastboot reboot` / `adb reboot` | 正常 reboot | ✅ 在 |
| **长按电源键强关 / 长按电源+音量** | **PMIC 冷复位** | ❌ **DRAM 掉电不刷新** |
| 拔电池（本机不可拆）/ 电量耗尽 | 冷 | ❌ |

证据（MTK mainline xaga DTS 作者的一手注释，`research/raw/mt6895-xiaomi-xaga.dts:1537-1556`）：

> "Power key: the PMIC's single-key long-press reset is a **cold reset (DRAM is not kept refreshed,
> so ramoops comes back corrupted)**. Keep the PON reset restricted to power+homekey so it cannot
> fire on its own…"
>
> "The WDT's external reset output is wired to the PMIC and turns an AP_WDT timeout into a **full
> cold reset, which is what rots the ramoops window (~13% of bits)** before pstore can be read."

→ **黑屏后的正确动作是：等 watcher 自己复位（≤ 90 s），或 `fastboot reboot`。
最不该做的就是长按电源键 10 秒。**

⚠️ 同时注意：`watchdog { mediatek,disable-extrst; }` 只在 **Linux 驱动**加载后生效
（`mtk_wdt_set_timeout` 里 `reg &= ~WDT_MODE_EXRST_EN`）。**在 XNU 跑的这段时间里，没有任何
人写过这个位**——如果 XNU 自己 arm 了 AP_WDT（当前没有）或 LK 留了，超时就可能是冷复位。
我们 loader 的 `wdt_reset()` 碰巧写的是 warm（见上表），这一点**不要"顺手指"地改成带 EXRST_EN**。

### 2.4 WDT 复位之前能留什么

按「能不能活下来」排序：

| 载体 | WDT warm reset | WDT cold reset（PMIC） | 断电 |
|---|---|---|---|
| DRAM（`0x90000000` 标记 / `0x90000100` SNAP / `0x48090000` ramoops / `0x7ffbf000` log_store） | ✅ 活 | ⚠️ ~13% 位翻转，`strings` 仍可读 | ❌ |
| framebuffer（屏幕） | ✅ 活 | ✅ 活 | ❌（下一棒 LK 会盖掉） |
| flash（`xnusys` 末 8 MiB / `expdb`） | ✅ | ✅ | ✅ |

**实操结论**：WDT 前最值得留的是 **DRAM 的 SNAP 区**（我们已经有 1 MiB 且已经实现快照），
因为「下一次 mtkloader 启动」是必然发生的，而它会把 SNAP 写进 flash。

---

## 3. 启动链上能插观测点的位置

```
BootROM → Preloader → LK → [我们的 mtkloader (boot_a 头部)] → XNU payload → (Android Image @CHAIN_OFFSET)
   ↑          ↑         ↑            ↑                          ↑
  BROM 模式  expdb     boot_a 可控   ★最肥的观测点              ★真正的现场
```

### 3.1 Preloader / LK 阶段

| 观测量 | 怎么拿 | 证据 |
|---|---|---|
| PL 日志 | `oem lpmsg`（64 KiB 消息缓冲）/ `oem dump_pllk_log` | LK 字符串 ` oem_dump_lpmsg: log_header = 0x%p , data_size =0x%x`、`lpmsg log::::` |
| LK 日志 | `oem lkmsg`（256 KiB console） | ` oem_dump_lkmsg: log_header = 0x%p , data_size =0x%x`、`lkmsg log::::` |
| 上次复位原因 | `oem fbreason` | LK 字符串 `oem fbreason`、`androidboot.bootreason=` |
| 异常数据库 | `oem expdb` | §1.1 |
| LK 自己对 pstore 的判断 | `oem uefilog`、`oem lks` | LK 字符串 |
| **实时串口** | `fastboot oem p2u on` + `fastboot continue` | MTK 官方 FAQ：`fastboot oem p2u on` 打开 UART log，重启失效 [http://www.chinaunix.net/uid-29728680-id-5470395.html] |
| USB 转 JTAG | `oem usb2jtag on` | LK 字符串 `[FASTBOOT] oem p2u on!` / `Enable USB2JTAG` |

**社区实证（同 SoC 家族，值得抄作业）**：Redmi K30 Ultra(MT6889) 黑屏那次，作者就是靠
`oem lkmsg` / `oem lpmsg` / `oem dump_pllk_log` 捞到「256 KiB console buffer + 64 KiB message buffer」，
进而看到 `ecc error ... ERXMISC0_EL1` 定位到 CPU/cache 而不是背光
[https://www.20051110.xyz/blog/redmi-k30-ultra-bootloop/en]。
同一篇还给了两个**极便宜的增强**：启动参数加 `ignore_loglevel loglevel=8 initcall_debug text`、
以及 `maxcpus=1` 做对照实验。

⚠️ **坑（原文亲测）**：`oem lkmsg` 读的是一个**固定大小环形缓冲**，前半是新内容、后半是上一轮残留；
**连续两次抓到的字节可能完全一样**。所以必须给每轮抓取编号 + 存原始字节 + 记 hash，否则会被
「看起来有日志」骗过去。

### 3.2 bootloader → 内核的 cmdline / DTB 能塞什么

- **cmdline 是我们的**：`XNU_BOOT_ARGS` 完全由 loader 拼（`mtkloader.c:226-239`），
  当前值 `rd=md0 -v cpus=4 buttons=2 touchscreen=3 keylog=1 fbwidth=1080 fbheight=2460 fb_disp=1 headset=1 loader_return_after=150`。
  → 想加 `-x` / `debug=0x144` / 自定义 `hakios_*` 开关，改一行即可，零风险。
- **DTB 只能读，不能换**：`lk_dtb` 是 LK 传进来的（`mtkloader.c:1553` `stub(lk_dtb, ...)`）；
  分区层面 `dtbo_a` 是 LK 自己挑的，改它会崩 LK 本身（前序调研已记录，本条**未在本轮复核**）。
- **不要让 loader 去改 LK 的 `/chosen`**：MTK mainline 注释说得很明确
  「LK rewrites /chosen and would drop it」（`xaga-facts.md` §4.6 引 mainline DTS 注释）。

### 3.3 「刷进去就自动 dump 内存到磁盘再重启」——可行，而且我们已经有 90%

`mtkloader` 已经有全部零件：

| 零件 | 位置 |
|---|---|
| 找 XNU payload / 符号 | `find_syms()` `mtkloader.c:804` |
| 从 DRAM 读 XNU 的 `loader_log`（256 KB 头 + 1 MB 环，含 wrap 处理） | `rescue_log()` `mtkloader.c:2587-2638` |
| 写 flash（UFS，含回读校验语义） | `save_log_to_flash()` `mtkloader.c:1796` |
| flash 里的容器格式 `HLOG` | `logbook_hdr` `mtkloader.c:1768-1780` |
| 主机侧解析 | `mtk/tools/xaga-pull-log.sh` |

缺的只是**触发点**和**一个真的会写 flash 的调用**（§5 详述）。

---

## 4. ★ 给 HakiOS 的具体方案（3-5 个，按性价比）

排序依据：**拿到信息的概率 / 改动行数 / 风险**。标 🔴 的必须有备份与回滚路径。

### 【方案 1】⭐ 立刻做：用 LK 自带的 5 个 `oem` 命令捞 LK 侧日志（零代码、零风险）

**怎么做**（手机停在 fastboot 时，逐条执行，全部只读）：

```sh
source ~/.dsh/env.sh
fastboot oem fbreason            # 上次为什么复位：cold/warm/WDT/按键
fastboot oem lkmsg               # LK 的 256 KiB console 环
fastboot oem lpmsg               # preloader 的 64 KiB 消息缓冲
fastboot oem dump_pllk_log       # PL+LK 合并日志（20 字符分块，需拼）
fastboot oem expdb               # 异常数据库（可能分块，量大）
```

`dump_pllk_log` 的输出被 fastboot 切成 20 字符并加 `(bootloader)` 前缀，**必须**用
`dump_pllk_log` 工具（走 raw USB 重组）或 XDA 的 formatter 还原：
[https://github.com/R0rt1z2/dump_pllk_log] / [https://forum.xda-developers.com/t/tool-windows-pl-lk-log-formatter.4763660/]

**能拿到什么**：
- LK 有没有真的把我们的 boot_a 解出来、有没有 AVB/签名/分区表报错；
- LK 判定 kernel 参数、`bootreason`（这是我们**完全空白**的一格信息）；
- `oem expdb` 里若已有 `pstore_raw` / `SYS_RAMCONSOLE_RAW`，可能是**上一次**跑挂时被 LK 收走的残留。

**要改什么**：无。
**风险**：这些命令是否在 xaga 的 fastboot 分发里**真的注册**，只有跑一次才知道（字符串在
`dump/lk_a.img` 里是强证据但非充分证据）。**全部是只读**，最坏结果是 `FAIL: unknown command`。

> 📌 建议同时做的记录纪律（照抄 K30 Ultra 那篇的教训）：每轮抓取存
> `logs/<round>/lkmsg-<n>.txt` + `sha256sum`，避免「抓到的是上一轮残留」这种假情报。

---

### 【方案 2】⭐⭐ 一行改动，让 watcher 真的把 XNU 日志写进 flash（收益最大）

**问题**：`XNU_WATCH_HEARTBEAT` 在 xaga 上**没有被 board.h 覆盖**（`grep XNU_WATCH_HEARTBEAT mtk/mt6895/loader/board.h` 无结果），
取 `mtkloader.c:80-82` 的默认值 `1`。而 `=1` 时代码是（`mtkloader.c:2934-2940`）：

```c
fb_rect(0, 0, PANEL_W, PANEL_H, 0xff00c000u);   /* 满屏绿 */
dcache_clean_range(fb_base, ...);
snap_end();
marker_set(MARK_DONE);
for (;;) { __asm__ volatile("wfe"); }           /* ← 永久停机：不复位、不写 flash、后面全不执行 */
```

→ 120 s 后手机变**一屏纯绿**并**永远停在那里**；`psci_system_reset()`（`:3078`）和整段
PMU/GIC/msgbuf 诊断（`:2950-3074`）全是**死代码**。用户手动断电重启后走
`mark == MARK_DONE` → `snapshot_take()`（`:7272`），而 `snap_end()` 写下的 `len` 是
`dbg_tee_at`，在这条分支里**没有任何 `dbg_puts`**，于是 `len == 0` →
`snapshot_take()` 直接 `return 0`（`:2101`）→ **flash 里一个字节都没写**。

**这解释了「日志只剩几百字节 boot note」**：logbook 里最后一条始终是 `logbook_boot_note()`
（`:1998`）写的那 768 字节。

**怎么做**（改 `mtk/mt6895/loader/board.h`，1 行）：

```c
/* 让 watcher 走完整诊断路径：快照 → 复位 → 下次开机落盘 */
#define XNU_WATCH_HEARTBEAT     0
```

并把顺序修对（`board.h:793 / :796`）：

```c
#define XNU_WATCH_SECONDS       90      /* 必须 < XNU_RETURN_AFTER，否则 watcher 永远先动手 */
#define XNU_RETURN_AFTER        "150"
```

**能拿到什么**：复位前那 1 MiB 的 `SNAP` 文本，其中包含
`=== watcher: XNU still running after … ===`、`cpu_ttep`、`heartbeat`、`loader_log_len`、
**cpu0 的 24 个 PC 采样（PMPCSR）**、完整 GIC 状态（`GICD_CTLR`/`GICR_*`/`ISPENDR0`/`IPRIORITY27`）、
`=== what the screen shows ===`、kernel message buffer、`loader_log (head) 256 KB`。
→ 足以判断「是死在中断没来 / 卡在等锁 / 还是根本没跑到 EARLY_BOOT」。

**风险**：低。`XNU_WATCH_HEARTBEAT` 只影响画不画红条和走哪个分支；置 0 后屏幕交给 XNU
（`fbc.base = 0`，`:2946`），不改任何硬件状态。唯一副作用：**失去「满屏绿=watcher 活着」这个
肉眼信号**，但换来的是它真的会复位并落盘。建议同时把 `psci_system_reset()` 失败后的
`wdt_reset()` 回退路径留好（已有，`:3085`）。

🔴 **配套必须改**：`rescue_log()` **没有写 flash**（见方案 3），否则 `MARK_RUNNING` 那条路仍然白跑。

---

### 【方案 3】⭐⭐ `rescue_log()` 补上 `save_log_to_flash()`，并把 `MARK_RUNNING` 点亮

**两个独立缺陷**：

1. **标记没置**：`XNU_WDT_SECONDS == 0`（`board.h:799`）时走 `marker_set(0)`（`mtkloader.c:3590-3592`），
   于是 `MARK_RUNNING` 永远不出现 → `if (mark == MARK_RUNNING) rescue_log();`（`:7344-7345`）
   这条分支**从来不会被执行**。
2. **就算执行了也不写 flash**：`rescue_log()` 末尾是 `save_log(sp, n)`（`:2635`），
   而 `save_log()` 只往 **ramoops 环**（`plog_putc`）写；xaga 是 `BOARD_NO_PLOG=1`，
   `plog_putc()` 是空函数（`mtkdbg.h:274-277`）→ **什么都不写**。紧接着的 `show_log_tail(40)` 只是屏幕。

**怎么做（两处，都很小）**：

```c
/* 1) mtkloader.c 约 :3592，把 else 分支改成仍然点亮 RUNNING（WDT 关掉不等于没有风险） */
} else {
        marker_set(MARK_RUNNING);   /* 下一次开机才知道「XNU 没交还」 */
}
```

```c
/* 2) rescue_log() 末尾（:2635 save_log(sp, n) 之后）补一行 —— 不要自己线性化！
 *
 *    save_log_to_flash() 只吃「一段连续 pa/len」，而 loader_log 的布局本来就是
 *    head(256 KiB) + ring(1 MiB) 连续 1.25 MB（loader_return.c:39-41）。
 *    所以整段原样写进 flash，把「环的状态」塞进 header 的 code/elr 两个字段，
 *    由主机侧脚本重排 —— 比在 loader 里 malloc 1.25 MB 安全得多。
 */
(void)save_log_to_flash((uint64_t)(uintptr_t)log,
                        head_sz + ring_sz,          /* 1.25 MB, 原样 */
                        3u /* reason: watchdog */,
                        (uint64_t)ring_at,          /* code = ring 写指针 */
                        (uint64_t)wrapped,          /* elr  = 是否成环 */
                        0);
```

主机侧（`xaga-pull-log.sh` 里加十几行，或另写 `xaga-pull-v2.py`）：

```python
if elr == 0:                       # 未成环：前 len 字节就是全部
    body = raw[:min(ln, 256*1024 + 1024*1024)]
else:                              # 成环：头 256 KiB，然后从 ring_at 接环尾，再回头接环头
    head = raw[:256*1024]
    ring = raw[256*1024:256*1024+1024*1024]
    body = head + ring[code:] + ring[:code]
```

**能拿到什么**：XNU 死掉后，下一次 mtkloader 启动时把 **head 256 KB + ring 1 MB 的完整内核日志**
（含 wrap 正确排序）写进 `xnusys` 末 8 MiB 的 `HLOG`，用 `mtk/tools/xaga-pull-log.sh` 读。
**这就是「内核日志一直在那里，只是没人读」的正面回答。**

**风险**：低-中。
- `marker_set(MARK_RUNNING)` 会让**每次**正常开机也走 rescue 分支 → 必须确保 XNU 正常返回时
  `xnu_returned()`（`:2497`）或 `chain_android()` 会把 marker 清掉（`chain_android` `:1512`
  有 `marker_set(0)`，✅ 已覆盖）。
- 写入量 1.25 MB（> `XNU_LOGBOOK_MAX` 4 MiB？不，1.25 < 4，✅ 不会被截断）。
- 若 flash 写失败（`save_log_to_flash` 返回 0），日志仍在 DRAM，下次还能再试
  （`HLOG` 头只在 `len==0` 时才不写，`mtkloader.c:1800-1840`）。

---

### 【方案 4】⭐⭐⭐ 把日志落到「不需要 Android 也能读」的地方（换目标分区）

**问题**：现在的 `HLOG` 在 `xnusys` 末尾 8 MiB（`mtkloader.c:1762` `XNU_LOGBOOK_SLACK`、
`:55` `SYSTEM_PART "xnusys"`）。读它只有一个办法：**Android 起来 → adb → su → dd**
（`mtk/tools/xaga-pull-log.sh`）。而黑屏时**恰恰没有 Android**。

**怎么做**（按「读出门槛」选一个，可并存）：

| 选项 | 写入方 | 读法（黑屏时） | 改动 | 风险 |
|---|---|---|---|---|
| **4a. 写 `expdb` 的高偏移段** | `save_log_to_flash` 加一个 `partition` 参数 | `mtk ro <off> <len> f.bin`（preloader 模式，**无需 Android/fastboot**）；`oem expdb` 也可能带出来 | 小（`gpt_find("expdb")` 已有） | 🟡 中：`expdb` 是 MTK 的 AEE 区，`kedump` 会往里写/擦；必须挑一个**远离 AEE 头部**的固定偏移（如末尾 8 MiB），并接受「可能被 kedump 覆盖」的事实 |
| **4b. 保持 `xnusys`，改用 mtkclient 按偏移读** | 不变 | `python mtk.py ro <offset> <len> logbook.bin` | **零代码** | 🟢 低（纯只读） |
| **4c. 双写：`xnusys`（给 adb）+ `expdb` 尾部（给 fastboot/mtkclient）** | 都写 | 任意 | 中 | 🟡 中，但最稳 |

**4b 的具体命令**（✅ 已确认 mtkclient 有按字节偏移读的能力，`rflash offset`，
见 [README-USAGE.md](https://github.com/bkerler/mtkclient/blob/main/README-USAGE.md)：

```
python mtk.py ro 0x128000 0x200000 flash.bin     # "Read flash offset 0x128000 with length 0x200000"
```

所以读我们的 logbook 是（`HLOG` 在 `xnusys` 末尾 8 MiB，偏移 = 分区起始 + 大小 − 8 MiB）：

```sh
source ~/.dsh/env.sh
# 1) 先拿 xnusys 的起始字节与大小（mtkclient 的 printgpt，只读）
python3 -m mtkclient printgpt | grep -i xnusys
# 2) 假设起始 B、大小 S，则 logbook 在 B + S - 0x800000，长度 0x401000（16 KiB 头 + 4 MiB 日志）
python3 -m mtkclient --loader Loaders/V6/<xaga>.bin ro $((B+S-0x800000)) 0x401000 /tmp/logbook.bin
# 3) 用现成脚本的解析部分拆 HLOG（xaga-pull-log.sh 里那段 python 可直接复用）
python3 -c "import struct;d=open('/tmp/logbook.bin','rb').read();print(struct.unpack_from('<IIQQQQQQQQ',d,0))"
```

耗时是**秒级**（4 MiB），不再是「读 2.5 GiB 分区」。**这一条今天就能用，不需要改任何代码。**

> ⚠️ 精确偏移不用手算：`mtk/loader/mtkloader.c:1786-1794` 的 `logbook_offset()` 就是
> `lba*block + size - 8 MiB`，而 `gpt_find` 打印的 `xnusys at block N`（`mtkloader.c:1051`）
> 在 logbook 头里也有（`h.where`）。最稳的做法是**从 logbook 头里的 `magic == HLOG` 反搜**。

**推荐 4b 先上（零代码，今天就能用）**，4a/4c 作为下一步。

> 🔴 **风险声明**：`expdb` 属 MTK 私有取证区，**不是** `nvdata`/`nvram`/`protect*`/`seccfg`/`otp`
> 这类「碰了掉 IMEI / 回锁 / 变砖」的分区，写它**不会**丢用户数据，但可能让 MTK 的
> 售后诊断工具读不到本次异常。**绝对不要**为了腾地方去动 `nvdata`、`nvram`、`seccfg`、
> `persist`、`protect1/2`、`otp`、`frp`、`preloader`。

---

### 【方案 5】⭐ 让 XNU 持续写 `log_store`（原厂同款，最优雅，成本最高）

**原理**：把 `loader_log_putc()`（`xnu/files/osfmk/arm64/loader_return.c:84`）在写自己那 1.5 MB
缓冲之外，**同时**写一份进 `0x7ffbf000` 的 256 KiB 环（32 字节头 + 环），让 LK 下次开机按
`log_store_sync_sram` 搬进 `expdb`。**这样连「复位」都不需要，纯黑屏也行**——只要再开一次机。

**要改什么**：
1. 在 XNU 侧建一个 1:1 的 2 MB 块映射到 `0x7fe00000`（`loader_return.c` 已有 `loader_l1/loader_l2`
   和 `1:1 映射`的现成代码，`:69-70`、`:100-120`）；
2. 写头 `{sig=0x5678ef90, flag, addr=0x7ffbf000, size=0x40000, offsize=0x20, point}` + 环；
3. 每 N 字节或每次 `loader_log_putc` 更新 `point`，并 `dcache_clean` 对应行。

**能拿到什么**：**最近 256 KiB 的内核输出，且只要手机还能再开机就一定拿得到**（LK 会搬）。
是最接近「专业内核移植团队做法」的方案。

**风险**：🟡 **中高**，且有三处**未确认**：
- 那 32 字节头的**精确字段语义**未知（只有 `sig/flag/add/size/offsize/point` 的打印格式反推）；
  写错 LK 可能判定「无内容」而丢弃，或更糟——把 LK 自己的 log_store 初始化搞乱；
- `0x7ffbf000` 在 `no-map` 之外（DTS 387-390 无 `no-map`），Linux 会把它当普通内存用来放别的东西；
  **XNU 独占时没问题，但回到 Android 后可能被覆写**（这正是 LK 必须尽快搬走的原因）；
- 与方案 2/3 的 SNAP/logbook 有**语义重复**，建议**先做 2+3 拿到第一批真实日志**，再上这条。

---

### 方案对比（一屏）

| # | 改动量 | 需要重启几次 | 拿到的信息量 | 风险 | 建议 |
|---|---|---|---|---|---|
| 1 | 0 | 0（现在就能跑） | LK/PL 侧全部 + 复位原因 | 无（只读） | **今天做** |
| 2 | 1 行 | 1 | watcher 全套现场（PC 采样/GIC/msgbuf/日志头 256 KB） | 低 | **今天做** |
| 3 | ~20 行 | 1 | **完整 1.25 MB 内核日志落 flash** | 低-中 | **今天做** |
| 4b | 0 | 0 | 已有 logbook 的离线读法 | 无 | 配套 |
| 4a/4c | 中 | 1 | 日志在黑屏态可读（fastboot/mtkclient） | 中 | 下一轮 |
| 5 | 大 | 0（但每次开机） | 最近 256 KiB，永不丢 | 中高 | 兜底方案 |
| p2u | 0 代码 + 一根 DIY 线 | 每次 | **实时**串口 | 低（但需硬件） | 想彻底解决就上 |

---

## 5. 现有机制的问题（对照分析）

### 5.1 四条写入路径的真实状态（逐条复核）

| 位置 | 函数 | 实际行为 | 判定 |
|---|---|---|---|
| `:1998` | `logbook_boot_note()` | ✅ 真的写 flash（`save_log_to_flash`，768 字节，`reason=0xfeedface`） | **可用**，这就是我们唯一看到的东西 |
| `:2497` | `xnu_returned()` → `save_log_to_flash` | 只有 XNU 主动交还才走；XNU 死了永远不到 | 只在「跑通」时有用 |
| `:5481` | probe 模式（`probe_flash`） | `BOARD_MALI_PROBE=0`（`board.h:573-575`），且 `probe_flash` 标了 `__attribute__((unused))` | 不可达 |
| `:7345` | `rescue_log()` | ①`MARK_RUNNING` 从不被置（`XNU_WDT_SECONDS=0` → `marker_set(0)`，`:3592`）；②**即使进了也不写 flash**，只 `save_log()`→`plog_putc()`→`BOARD_NO_PLOG` 空函数（`:2635`、`mtkdbg.h:274-277`） | **双重失效** |

### 5.2 三个把日志吃掉的 bug（按危害排序）

**(a) watcher 永久停机，什么都不留** — `mtkloader.c:2934-2940` + `mtkloader.c:81` 默认 `XNU_WATCH_HEARTBEAT=1`，
board.h 未覆盖。→ 120 s 后满屏绿、`wfe` 死循环、`psci_system_reset()`（`:3078`）永不到达、
`SNAP.len` 停在 `dbg_tee_at`（这条分支里没有任何 `dbg_puts`，所以是 0）
→ 下次开机 `snapshot_take()` 因 `len==0` 直接返回（`:2101`）→ **flash 零字节**。
这条最致命，因为它把整个 watcher 的设计（PC 采样、GIC 状态、msgbuf、日志头）全部变成死代码。

**(b) `MARK_RUNNING` 从未点亮** — `board.h:799 XNU_WDT_SECONDS 0` → `mtkloader.c:3582-3592` 的
`else { marker_set(0); }`。`rescue_log()` 那条分支（`:7344`）是**为了它写的**，却永远进不去。

**(c) `rescue_log()` 不落 flash** — `:2635 save_log(sp, n)` 写的是 ramoops 环，xaga 关掉了。
即使 (b) 修好，(c) 不修，仍然拿不到任何东西。

### 5.3 时序参数互相打架（已知，复核确认）

```
XNU_RETURN_AFTER   = "150"   (board.h:793)
XNU_WATCH_SECONDS  = 120     (board.h:796)
```
→ watcher **永远**在 `loader_return_after` 之前 30 s 动手。注释（`board.h:786-792`）解释了 150 是为了
等到 GPU probe 之后，但那意味着「正常交还」这条路在本板上**结构性地不可能赢**。
要么把 `loader_return_after` 降到 < 90，要么承认「watcher 是主路径」并把它修好（方案 2+3）。

### 5.4 其它已核实的小问题

| 问题 | 证据 | 影响 |
|---|---|---|
| `BOARD_NO_PLOG=1` 让 `plog_putc()` 变空函数，`save_log()` / `save_host_keep()` 全部失效 | `board.h:908`、`mtkdbg.h:274-277` | ramoops/pmsg 两条路都是空的，`show_log_tail()` 也没有 plog 可画 |
| `BOARD_NO_UART=1`，且注释记录「读 `UART_BASE+0x14` 会挂」 | `board.h:888-896` | 拿不到串口，也解释了为什么不能简单「打开 UART」 |
| `HLOG` 落在 `xnusys` 末尾，只有 Android+root 能读 | `mtkloader.c:1762`、`xaga-pull-log.sh` | 黑屏时读不到（方案 4 要解决的就是这个） |
| `rescue_log()` 里 `save_log_to_flash` 未接线 | `:2635` | 见 5.2(c) |
| 真机 `ramoops` 布局常量其实是对的（dmesg/console/ftrace/pmsg） | `board.h:200-224` vs `dump/iomem.txt` | ✅ 无需改；但注意 `ftrace` 占 `0x4815f000-0x4815ffff`，若将来自己算偏移别踩 |

---

## 6. 引用 URL

**外部资料**

1. mtkclient（V6 协议 / MT6895 需 preloader 模式 + `--loader`；`r` / `ro` / `da peek` / `memread` 只读命令）
   — https://github.com/bkerler/mtkclient
   — 用法细节（`ro <offset> <length> <file>` 按字节偏移读 flash）：
     https://github.com/bkerler/mtkclient/blob/main/README-USAGE.md
2. `mtk-log-util`（`logutil.py expdb` / `pstore`，自动 `strings`；`--pstore-address` 默认 `0x48090000`）
   — https://github.com/chkndrp/mtk-log-util
3. `dump_pllk_log`（走 raw USB 重组 `oem dump_pllk_log` 的 20 字符分块）
   — https://github.com/R0rt1z2/dump_pllk_log
4. XDA：PL/LK log formatter（`oem dump_pllk_log` 输出可读化；确认 PL/LK 日志存 `expdb`）
   — https://forum.xda-developers.com/t/tool-windows-pl-lk-log-formatter.4763660/
5. 同 SoC 家族黑屏实战（`oem lkmsg`/`lpmsg`/`dump_pllk_log` 捞出 256 KiB console + 64 KiB msg；
   环形缓冲残留陷阱；`ignore_loglevel loglevel=8 initcall_debug`；`maxcpus=1` 对照实验）
   — https://www.20051110.xyz/blog/redmi-k30-ultra-bootloop/en
   （中文原文 https://theunknownth.ing/blog/redmi-k30-ultra-bootloop）
6. MTK 官方 FAQ：`fastboot oem p2u on` + `fastboot continue` 临时打开 UART log
   — http://www.chinaunix.net/uid-29728680-id-5470395.html
7. MTK 平台串口日志汇总（波特率 **921600**；`CFG_USB_UART_SWITCH` / `WITH_DEBUG_UART` /
   `printk.disable_uart=0` / `MTK_UART_USB_SWITCH`）
   — https://www.cnblogs.com/haoran123/p/17352972.html
8. MTK debug cable（用 USB D+/D- 当 UART，PL2303/CP210x 改线即可，无需焊机）
   — https://www.stevenhoneyman.co.uk/2014/11/mtk-mediatek-debug-cable.html
9. 上游 `mtk_wdt.c`（`WDT_MODE_EXRST_EN = 1<<2`；`mediatek,disable-extrst` → 清该位 → warm reset）
   — https://github.com/torvalds/linux/blob/master/drivers/watchdog/mtk_wdt.c
10. MTK mainline xaga DTS（`mblock-6-log_store` @0x7ffbf000/0x40000、`mblock-9-ramoops` @0x48090000、
    `read-lastboot-log.sh` 注释、`&watchdog { mediatek,disable-extrst; }`）
    — https://github.com/MT6895-Mainline/linux （`arch/arm64/boot/dts/mediatek/mt6895-xiaomi-xaga.dts`）
11. Linux ramoops 文档（`mem_address`/`mem_size`/`console_size`/`pmsg_size`/`ecc` 语义）
    — https://www.kernel.org/doc/html/latest/admin-guide/ramoops.html
12. MTK AEE/expdb 与 `KEDump.c` 位置（`kedump_mini()` 把异常写 expdb；`MTK_MRDUMP_ENABLE` /
    `KEDUMP_MINI` 的开关关系）— https://blog.csdn.net/lei7143/article/details/122985803
13. MTK 分区用途说明（EXPDB = Exception database；MISC/LOGO/NVDATA/PROINFO 等）
    — https://devpress.csdn.net/v1/article/detail/89704307

**本机代码/数据（可作为一手证据）**

14. `mtk/loader/mtkloader.c` — `:55` `SYSTEM_PART "xnusys"`、`:81` `XNU_WATCH_HEARTBEAT 1`、
    `:804` `find_syms`、`:1491-1495` `reset_board`、`:1594` `save_log`、`:1762` `XNU_LOGBOOK_SLACK`、
    `:1768-1780` `logbook_hdr`、`:1796` `save_log_to_flash`、`:1881` `logbook_boot_note`、
    `:1998` boot-note 写入、`:2095-2130` `snapshot_take`、`:2432` `xnu_returned`、`:2497` 保存、
    `:2587-2638` `rescue_log`、`:2822-3088` `watcher_main`、`:3582-3592` WDT 与 marker、
    `:7249-7350` marker 分发
15. `mtk/mt6895/loader/board.h` — `:164` `MARKER_PA`、`:181` `SNAP_PA`、`:200-224` ramoops 常量、
    `:424-425` `BOARD_STORAGE_UFS 1`、`:740-760` 三条退路注释、`:786-799` 时序参数、
    `:870-930` `BOARD_NO_UART` / `BOARD_NO_PLOG` 的实测原因
16. `mtk/loader/mtkdbg.h` — `:222-248` `wdt_reset` / `wdt_arm`、`:252-303` ramoops 环与 `BOARD_NO_PLOG`
17. `xnu/files/osfmk/arm64/loader_return.c` — `:39-41` 1.5 MB 缓冲（256 KB head + 1 MB ring）、
    `:84` `loader_log_putc`、`:403-410` `loader_checkpoint`、`:413-471` `loader_return_after`
18. `mtk/mt6895/configs/mt6895.config:10-16,39,65` — xaga **没有** `CONFIG_HAIKOS_UFS` / native UFS
19. `mtk/tools/xaga-pull-log.sh` — `HLOG` 的主机侧解析（`skip=653560`，8 MiB 预留）
20. `rom/lk.img` / `dump/lk_a.img` — `strings` 得到 `oem lkmsg|lpmsg|dump_pllk_log|expdb|fbreason|lks|uefilog|p2u|usb2jtag`、
    `kedump:*`、`log_store_*`、`ramoops.mem_address=0x%x …`、`panic_header.wdt_status`
21. `xaga_scatter.xml:263-274` — `expdb` @`0xbd80000`，size `0x8000000`
22. `dump/byname.txt` — `expdb -> /dev/block/sdc11`
23. `dump/cmdline.txt` — 真机 cmdline（`ramoops.*`，`mtk_printk_ctrl.disable_uart=1`）
24. `dump/iomem.txt` — ramoops 四个子区的真实页表（dmesg/console/ftrace/pmsg）
25. `codex/out/round-8-support/trial-672-android-dmesg.txt` — 真机 dmesg：
    `sig 0x5678ef90 … add 0x7ffbf000 size 0x40000 offsize 0x20 point 0x1490f`（log_store 头）、
    `ramoops: using 0xe0000@0x48090000`、`mrdump phys:0x48080000 - 0x48090000`
26. `research/raw/mt6895-xiaomi-xaga.dts:73,355-390,1546-1556` — log_store / ramoops / aee / 冷热复位注释
27. `research/WEB-xnu-android-projects.md:651-668,839,854` — 前序调研对 `log_store` 与
    `read-lastboot-log.sh`（三仓库 404）的核查

**未确认清单（不要当成事实用）**

- `expdb` 内部二进制格式（`KEDUMP_CRC` / `panic_header` / 压缩块）——无公开源码；
- LK 把 `log_store` 搬进 `expdb` 的**触发时机**（每次开机？仅异常？shutdown 回调？）；
- `log_store` 32 字节头的字段语义（只能从 `sig/flag/add/size/offsize/point` 的打印顺序反推）；
- `oem lkmsg` / `oem expdb` 在 xaga 的 fastboot 分发里**是否真的注册**（字符串存在只是强证据）；
- 写 `0x4811f000` 让 loader 挂住的**根因**（怀疑 MMU 属性/secure 保护，均未证实）；
- `mtk ro <offset> <length>` 在 **MT6895/V6 + preloader 模式**下是否同样可用（命令本身已在
  README-USAGE 里确认存在，但 MT6895 走的是新协议，实际是否支持**只有试一次才知道**）；
- `xnusys` 的精确 GPT 起始/大小（`xaga-carve.sh` 是按 `--gib 2.5` 现算的，每台机器可能不同；
  用 `printgpt` 现场取，别抄本文的数字）；
- xaga 的 `dtbo_a` 是否真的「改了会崩 LK」（本轮未复核，前序调研记录）。

---

## 附：今天就能执行的最小动作清单

```sh
source ~/.dsh/env.sh
mkdir -p logs

# ---- A. 手机停在 fastboot 时：只读，零风险，现在就能跑 ----
for c in fbreason lkmsg lpmsg dump_pllk_log; do
    fastboot oem "$c" | tee "logs/lk-$c-$(date +%s).txt"
done
sha256sum logs/lk-*.txt          # 记 hash：防止「抓到的是上一轮残留」

# ---- B. 改 3 处代码（方案 2 + 3），重刷 boot_a，跑一次，再取 ----
#   board.h:  #define XNU_WATCH_HEARTBEAT 0
#             #define XNU_WATCH_SECONDS 90     (< XNU_RETURN_AFTER=150)
#   mtkloader.c: else { marker_set(MARK_RUNNING); }
#   mtkloader.c: rescue_log() 末尾 save_log_to_flash(...)   ← §4 方案 3 的两行

# ---- C. 黑屏之后：不用 adb / 不用 Android，直接从 flash 取 HLOG ----
#   1) 拿 xnusys 的 GPT 起点和大小
python3 -m mtkclient printgpt | grep -i xnusys
#   2) 只读末尾 8 MiB（4 MiB 头+日志够用），秒级
python3 -m mtkclient --loader Loaders/V6/<xaga>.bin \
    ro $(( B + S - 0x800000 )) 0x401000 /tmp/logbook.bin
#   3) 用 mtk/tools/xaga-pull-log.sh 里那段 python 解析 HLOG
```

> 全程不需要 recovery、不需要 adb、不需要拆机、不需要长按电源键。
> **A 和 C 都不改设备上的任何字节。**

### 一句话操作纪律

1. **黑屏后别长按电源键**（PMIC 冷复位会把 DRAM 证据烂掉 ~13%）；等 watcher 自己复位，或 `fastboot reboot`。
2. **先跑 A 再刷任何东西**——刷 `boot_a` 之后 LK 会重启，`oem lkmsg` 的内容可能就换了。
3. **每轮日志存 hash**，不然会把上一轮的残留当成新证据。
