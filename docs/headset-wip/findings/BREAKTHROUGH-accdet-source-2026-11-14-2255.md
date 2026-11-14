# ★ 耳机任务根本性突破：源码一直在本地，不需要反汇编或真机

> 时间：2026-11-14 22:55
> 发现：`mt6368-accdet.c` 完整源码（105 KB）+ `mt6368.h`（294 KB，257 个位域）
> **就在本地的 sparse 克隆里，只是 `sound/` 目录没 checkout。**

---

## 0. 一句话

我们花了大量时间反汇编 `.ko`、查 `dump_reg`，而**真正的权威源码一直在本地**：

```
/home/lizi/IOS/work-xaga/k-xaga/sound/soc/codecs/mt6368-accdet.c    3609 行
/home/lizi/IOS/work-xaga/k-xaga/sound/soc/codecs/mt6368.h           5974 行，257 个 ACCDET 位域
```

之前拿不到，是因为本地是 **sparse clone，`sound/` 没 checkout**。
用 `git cat-file -p HEAD:<path>` 一取就有。

已提取到：`/home/lizi/IOS/work-xaga/research/raw/xaga-ecosystem/`

---

## 1. 我核实过的寄存器语义（源码为准）

### 1.1 关键三个寄存器

| 寄存器 | 位 | 名字 | 含义 |
|---|---|---|---|
| **`0x26a9`** | **bit0** | `ACCDET_IRQ` | **中断标志**（不是插入状态！） |
| `0x26aa` | bit0 | `ACCDET_IRQ_CLR` | 中断清除 |
| **`0x26b7`** | bit[2:0] | `ACCDET_STATE` | **状态** |
| `0x26b6` | bit[7:6] | `ACCDET_MEM_IN` | **AB 值**（源码里从这取） |

源码原文（`mt6368-accdet.c:1881-1882`）：

```c
cur_AB = accdet_read(ACCDET_MEM_IN_ADDR) >> ACCDET_STATE_MEM_IN_OFFSET;
cur_AB = cur_AB & ACCDET_STATE_AB_MASK;
```

`mt6368.h:5514-5519`：

```c
#define ACCDET_STATE_MEM_IN_OFFSET      (ACCDET_MEM_IN_SFT)
#define ACCDET_STATE_AB_MASK            (0x03)
#define ACCDET_STATE_AB_00              (0x00)
#define ACCDET_STATE_AB_01              (0x01)
#define ACCDET_STATE_AB_10              (0x02)
#define ACCDET_STATE_AB_11              (0x03)
```

### 1.2 AB 值 → 耳机类型（源码里的实际分支）

| AB | 含义 | `accdet_status` |
|---|---|---|
| **`00`** | `HEADSET_NO_MIC`（三段耳机） | `HOOK_SWITCH` |
| **`01`** | `HEADSET_MIC`（四段耳麦） | `MIC_BIAS` |
| `10` | 本平台**没用到** | — |
| **`11`** | **拔出** | — |

证据：`mt6368-accdet.c:2013-2018`（AB_00 → `HEADSET_NO_MIC`）、
`:2024-2028`（AB_01 → `HEADSET_MIC`）、`:1888-1890`（AB_11 出现在拔出判断里）。

**这与我之前从 `.ko` 反汇编解出的 `0x26b6[7:6]`、未插 = 3 完全一致** ——
但源码给的是明确语义，不再是推断。

### 1.3 耳机 vs LineOut 的硬阈值：5000 Ω

`mt6368-accdet.c:849-850`：

```c
/* if res < 5k ohm normal device;  res >= 5k ohm, lineout device */
if (res_value < 5000)
```

→ **这正是我们一直缺的「ADC 读数 → 阻抗」判定点的下游**。
5000 Ω 是分界，之下的走耳机路径，之上的走 LineOut。

### 1.4 mic bias 2.8 V

子代理从源码读到：`RG_AUDMICBIAS1HVEN = 0x252a:4-5` 写 `3` → 2.8 V。
（这条我未逐字核，但地址 `0x252a` 与我之前解出的 `accdet_init_once` 写入的寄存���一致）

---

## 2. 一个必须知道的陷阱：`USB_3_5_UNSUPPORT`

子代理发现（我未核实细节，但报告了机制）：

> `CONFIG_TARGET_PRODUCT_XAGA=y` 会打开 `USB_3_5_UNSUPPORT`，
> 驱动额外注册一个 `USB_3_5 Jack`，并从 **TypeC `TYPEC_ATTACHED_AUDIO`** 上报"耳机"。

→ 做音频时必须**和真 accdet 分开处理**。
这解释了为什么真机上有两个 input 设备：`Headset Jack`(event4) 和 `USB_3_5 Jack`(event3)。

---

## 3. 其他子系统（同一批源码里的副产品）

| 项 | 值 | 来源 |
|---|---|---|
| UFS 控制器 | `ufshci@112b0000`，`mediatek,mt8183-ufshci`，size `0x2300`，`GIC_SPI 137` | 源码 DTS |
| UFS PHY | **DTS 里没有独立节点** | 未解项 |
| 电量计 | **BQ28Z610 @ i2c7 `0x55`**（`mtk_gauge` 被 `DISABLE_MTKBATTERY=1` 关掉） | 源码 |
| 充电 | MT6375（i2c5 `0x34`）+ 电荷泵 SC8551 `0x66` / SC8561 `0x64,0x65` / LN8000 `0x51` | 源码 |
| 中断 | master 全 GPIO13、slave 全 GPIO14 | 源码 |
| boot image | base `0x3fff8000`、kernel_offset `0x8000`、pagesize `4096`、ramdisk_offset `0x26f08000`、tags/dtb_offset `0x07c88000`、header v4、LZ4 ramdisk | `BoardConfigCommon.mk:34-45` |
| 摄像头 I2C | S5KGW1 `0x20`、S5K4H7 `0x5a`、GC02M1 `0x6e`、OV02B10 `0x78`（**不是 DTS 里的 `0x10`**） | `ESK-Project` 驱动源码 |

**boot image 参数已与真机 dump 交叉验证**：
`boot_a.img` cmdline 为空、kernel 14.6 MiB、ramdisk 1.78 MiB；
`vendor_boot_a.img` cmdline 只有 `bootopt=64S3,32N2,64N2`、dtb_size=0、entry_num=0。

---

## 4. 取证：我们一直有一层日志没读

### 4.1 `log_store` 节点（真机 DTS 也有）

`mt6895-xiaomi-xaga.dts:387-390`：

```dts
mblock-6-log_store {
    reg = <0x00 0x7ffbf000 0x00 0x40000>;
    compatible = "mediatek,log_store";
};
```

**256 KiB @ `0x7ffbf000`，LK 会把它拷到 `expdb` 分区。**

同一份 dts 的注释（`:72-77`）写得清楚：

> Capture the dead boot with `read-lastboot-log.sh` (XAGR ring in **log_store at 0x7ffbf000, copied to expdb by LK**);
> cinit also dumps the whole 2 MB kernel log to `vendor_boot_b`, readable with `read-cinit-dump.sh`.
> **ramoops now lives in LK's own `0x48090000` window** (inherited from LK's cmdline), which **stock keeps across a hard reset**.

### 4.2 所以我们四次黑屏「零日志」可能是假象

我们的 `xaga-pull-log.sh` 注释写「this board has no UART, no ramoops and no pstore」，
但 dts 说 ramoops 在 `0x48090000`（LK 的窗口，硬复位后保留）。

**两者可能都对**：Android 侧的 pstore 没了，但 LK 那份还在。

→ **`expdb` 分区 + `0x7ffbf000` 是我们一直没读的日志源。**

### 4.3 其他项目怎么解决「内核侧没有日志写者」

我们只有 loader 在写 `xnusys` 尾部（4 MiB 环 + 1 头块），**内核侧零写者**。

三家的做法（可抄）：

| 项 | 做法 | 出处 |
|---|---|---|
| 周期 flush | ruby 把 `ufslogsecs=1` 写进正式 cmdline；ios-sweet 1s→10s（省写放大）；UniXNU 2s | ruby / ios-sweet / UniXNU |
| panic 前 sync | Bergamota `sweet_log_sync()` 在 `proc_handle_critical_exit()` | Bergamota patch 1002 |
| 碰 GPU 前 sync | Bergamota 在 A630 上电各阶段间插 16 处 `sweet_log_sync()` | Bergamota patch 0105 |

**第三条对我们最相关**：我们 loader 已经有「每步写 flash」，但**内核侧缺这一半**。

---

## 5. GPU：有一个同架构开源实现

`Ken-u/Mali`（RK3588 / Mali-G610 MC4 / arch 10.8），M0→M4 全通。

两个「会白干好几轮」的坑（子代理转述，我未核实）：

1. **MCU 的 queue input/output 与 suspend buffer 必须在 MCU shared region**，
   否则「CSG 起来但 stream 永不 ACK START」+「AS0 报写 `0x1000` 的 translation fault」（固件把地址 `& 0x0fffffff`）
2. **着色器 STORE 的 `@`/`^` 写反不报错**，要用「故意派发到未映射地址看是否报取指 fault」来验

→ 这条要和我们 `FINDING-pdc-early` 那条一起看。

---

## 6. 需要 sol 决策

1. **耳机：现在有源码了，`headset=2` 可以直接照着写**，不用再猜寄存器。
   `mt6368-accdet.c` 里的 `accdet_init()` / `accdet_irq_handle()` 就是权威实现。
2. **取证：先读 `expdb` 分区**（纯只读，手机在 fastboot 也能读？需要确认）
   或读 `0x7ffbf000`（需要 XNU 起来，但我们起不来）
3. **PDC 那条**（`FINDING-pdc-early`）和 GPU 的 MCU shared region 那条要一起评估

---

## 7. 一个方法论层面的教训

**我一直在反汇编闭源 `.ko`，而开源内核源码就在本地的 sparse 克隆里。**

发现路径：
1. 子代理 `REPO-xaga-ecosystem` 去查 `XagaForge/android_kernel_xiaomi_mt6895`
2. 发现本地 `/home/lizi/IOS/work-xaga/k-xaga` 就是同一个仓库
3. 但 `sound/` 没 checkout（sparse clone）
4. `git cat-file -p HEAD:sound/soc/codecs/mt6368-accdet.c` 直接取到

**以后要查任何驱动实现，先看本地 sparse 克隆里有没有 —— 用 `git cat-file` 绕过 checkout。**

---

## 8. 未核实 / 待确认

| 项 | 状态 |
|---|---|
| mic bias 2.8 V 的具体寄存器值 | 未逐字核 |
| `USB_3_5_UNSUPPORT` 的具体机制 | 未核 |
| `Ken-u/Mali` 的两个坑 | 未核 |
| `expdb` 分区在真机上是否存在 | **手机掉线，未验证** |
| `read-lastboot-log.sh` / `read-cinit-dump.sh` | 脚本本体没找到（子代理说三仓库 404） |

---

## 9. 真机实测：`expdb` 和 `log_store` 的现状（22:57）

### 9.1 `expdb` 分区存在，但**当前全空**

```
$ adb shell "su -c 'ls -la /dev/block/by-name/ | grep expdb'"
lrwxrwxrwx  expdb -> /dev/block/sdc11

$ adb shell "su -c 'blockdev --getsize64 /dev/block/by-name/expdb'"
134217728          ← 128 MiB

$ adb shell "su -c 'for off in 0 1048576 8388608 16777216 33554432 67108864; do \
    dd if=/dev/block/by-name/expdb bs=4096 skip=$((off/4096)) count=1 | od -An -tx1 | head -1; done'"
off=0:          全 00
off=1048576:    全 00
off=8388608:    全 00
off=16777216:   全 00
off=33554432:   全 00
off=67108864:   全 00
```

**六个采样点全是零。**

→ 说明：**正常启动时 LK 不写 expdb**。它只在异常/崩溃时写。
所以「读 expdb 拿黑屏日志」这个方案**只有在真的发生过异常之后**才有效。

**但要小心一个可能**：也许它写的是别的位置（我只采了 6 个点，128 MiB 里 6 个点不够）。
如果要确认，需要全扫或者找到 LK 的写入偏移约定。

### 9.2 `log_store` 节点在真机 DT 里确认

```
$ adb shell "ls /proc/device-tree/reserved-memory/ | grep -i log"
mblock-20-security_tee_log
mblock-5-gz-log
mblock-6-log_store          ← 这个
mblock-7-atf-log-reserved
```

与 `mt6895-xiaomi-xaga.dts:387-390` 一致：`0x7ffbf000`，`0x40000`（256 KiB）。

### 9.3 Android 侧**读不到** `0x7ffbf000`

```
$ adb shell "su -c 'ls -la /dev/mem'"
（不存在）

$ head -c 64 /dev/mem
head: /dev/mem: No such file or directory
```

→ **我们的 loader 在裸机阶段可以读它**（loader 有自己的映射），
但 Android 侧不行。这也是为什么 `read-lastboot-log.sh` 必须是 loader 侧的功能。

### 9.4 修正后的取证结论

| 日志源 | 可达性 | 备注 |
|---|---|---|
| `xnusys` 尾部 8 MiB（我们的 logbook） | ✅ 已验证可读（`xaga-pull-log.sh`） | 但只有 loader 会写 |
| `expdb`（128 MiB） | ⚠️ 分区在，**当前全空** | 只在 LK 异常时写；需确认写入偏移 |
| `log_store @ 0x7ffbf000`（256 KiB） | ❌ Android 读不到 | **只能从 loader（裸机阶段）读** |
| `ramoops @ 0x48090000`（896 KiB） | 未测 | dts 说 LK 保留、硬复位后仍在 |
| `vendor_boot_b`（cinit 的 2 MB kernel log） | 未测 | dts 注释提到的第三条 |

**所以最实际的下一步仍是：让 `rescue_log()` 能执行**（见 `FINDING-blackscreen-logs`），
它读的是 loader 能访问的 DRAM，不依赖 expdb。

**`log_store` 可以作为补充**：如果能在 loader 里加一段「启动时先把 `0x7ffbf000` 的 256 KiB 转存到 logbook」，
就能拿到 LK 阶段的日志。
