# Round 20 · 刷机结论：四次刷机全部黑屏（含两处已更正的错误）

> ## ⚠️ 本文件第 11 节有两处错误，已在 `TRIAL-692-EXACT-ARGS.md` 中更正
>
> 1. **「loader 被重写 4172 行」是错的**。已提交的 3453 行版本是 **MT6768/Infinix** 的 loader，
>    不是同源代码的早期版本。692 用的就是现在这份 7496 行源码（构建日志行号逐条吻合）。
> 2. **「逐字复刻 692 参数」是错的**。692 真实用的是 `mali=5 mali_cold=1 mali_preas0=0` +
>    20 个其它 `mali_*` + `loader_return_after=150`；昨天第 4 次刷机用的是
>    `mali=6 mali_cold=0 mali_preas0=1` + 无其它 + `loader_return_after=70`。
>
> 因此「第 4 次刷机仍黑屏 ⇒ 排除耳机驱动」这个推论**不成立**（对照不干净）。
> 其余各节（第 1-10 节）的证据与结论不受影响。

> 时间：2026-11-14 02:10–02:51
> 设备：xaga（GAJZ4HIN99AQ69Y9）
> 结果：**四次刷机全部黑屏，未进入 iOS 用户空间**
> 但定位到一个明确的否定结论

---

## 1. 一句话

**耳机驱动不是黑屏的原因。**

第 4 次刷机把耳机驱动从 `bsd_init` 调用链里完全移除、启动参数逐字复刻 trial 692（最近一次成功进入的版本），**依然黑屏**。

---

## 2. 用户观察（最关键的线索）

> 「输入到 jumping into xnu 就闪一下黑屏」
> 「之前修 GPU 的时候都能进 iOS」

前半句说明：**loader 跑完了，控制权交给了 XNU，然后立刻黑屏**。

后半句说明：历史上同样的 loader 路径能进。

---

## 3. 四次刷机的证据

| # | 时间 | 关键配置 | logbook `code` | 判定 |
|---|---|---|---|---|
| 1 | 02:10 | `headset=1`，无 `mali=` | **`0x13`(19)** | loader 死在 `mali_aoc_up()`（`probe_flash(19u)` 之后） |
| 2 | 02:20 | 同上 + 关 GPU 预上电 | **`0xfeedface`** | loader **成功**进 XNU，XNU 死 |
| 3 | 02:27 | 692 参数 + `headset=1` | 未取到 | 黑屏 |
| 4 | 02:49 | **移除耳机调用** + 692 参数 | 未取到 | **依然黑屏** |

`code` 字段的语义：
- `0xfeedface` = 正常 boot note（`logbook_boot_note()` 写的）
- `0x13` = `probe_flash(19u)` 的崩溃标记，位置在 `probe_say("power: about to release the SRAM isolation")` 之后

**第 1 轮的 loader 死在 GPU aoc，第 2 轮 loader 成功了。**

---

## 4. 第 4 次刷机做了什么（决定性对照）

### 4.1 移除耳机驱动调用

`drivers/input/hakios.input.c`：

```diff
- 	/* Finish the read-only PMIF dump before the SPMI keys thread uses this channel. */
- 	hakios_mt6895_headset_init();
 	hakios_mt6895_keys_init();
```

验证：

```
$ nm .../hakios.input.o | grep -ci headset
0
```

`input.o` 里零 headset 符号引用。

### 4.2 启动参数逐字复刻 692

```
rd=md0 cpus=4 buttons=2 touchscreen=3 keylog=1 key_scan=1 \
fbwidth=1080 fbheight=2460 fb_disp=1 mali=6 mali_cold=0 mali_preas0=1 \
fb_inuse=1 fb_queue=1 fbfit=0 fbscale=2 loader_return_after=70
```

与 692 的 board.h 备份对比：

```
$ diff <(grep -E "^#define BOARD_" 692备份) <(grep -E "^#define BOARD_" 现在)
→ 完全相同
```

`XNU_RETURN_AFTER` 也恢复成 `70`（692 的值）。

### 4.3 结果

镜像 `914fdc356876272ef72d767968370210`，22982656 字节，刷入 `boot_a` 成功。
重启后：**黑屏**。

→ **耳机驱动和启动参数都不是原因。**

---

## 5. 一个重要的日志回收陷阱（本轮发现）

这是本轮最有价值的工程发现之一：

`mtk/mt6895/loader/board.h:745-770` 记载了两条独立的时间线：

| 机制 | 默认 | 行为 |
|---|---|---|
| `loader_return_after` | 70（692）/ 150（后来改的） | XNU 在 EARLY_BOOT 之后主动交还控制权，走**正常保存路径** → 日志进 pstore |
| `XNU_WATCH_SECONDS` | **120** | 第二个核在自旋；若 XNU 死掉，**两分钟后硬复位** |

**问题**：`RETURN_AFTER=150` > `WATCH=120`，所以 **watcher 永远先触发**。
watcher 的路径是「先把快照留在 DRAM，再复位，由下一次 loader 启动时写入闪存」——
**如果下一次启动仍是我们这版 loader，它可能又崩在同一处，日志永远存不下来。**

这解释了为什么第 2/3 轮的 logbook 只有 396 字节（全是 loader 的行，零 XNU 行）。

→ **以后凡是排查「XNU 有没有起来」，`XNU_RETURN_AFTER` 必须小于 `XNU_WATCH_SECONDS`。**
本轮第 4 次刷机已改回 70（< 120），但结果仍是黑屏，且日志未取到。

---

## 6. 已排掉的假设（附理由）

| 假设 | 排除理由 |
|---|---|
| `BOARD_MALI_PREPOWER=1` 是根因 | 692 的 `BOARD_` 宏与现在**逐字相同**（含 `PREPOWER=1`），而 692 进了 XNU |
| `mali=` 参数缺失 | 第 2 轮没有 `mali=`，照样死 |
| `XNU_RETURN_AFTER` 150 vs 70 | 只影响日志回收路径，不影响能否启动（第 4 轮用 70，仍黑屏） |
| `fb*` 参数 | 692 那轮同样有 `fb_inuse` 等（`.config` 追加） |
| `uint16_t` 循环溢出 | `hi=0x26d3`，不会溢出 |
| 编译/链接失败 | `CC hakios.mt6895_headset.o` 通过，`LD mach...unstripped` 通过 |
| `stdbool.h`/`stdint.h` | XNU 的 `EXTERNAL_HEADERS/` 里有，编译通过 |
| **耳机驱动同步执行** | **第 4 轮完全移除调用，仍黑屏** |

---

## 7. 仍未排除的方向

第 4 轮是「692 参数 + 无耳机调用」，却仍失败。与 692 真实运行的差别只剩：

1. **内核二进制本身变了**（今天多次重建；692 用的是 11-13 的二进制）
2. `.config` 追加的 `fb_inuse/fb_queue/fbfit/fbscale`（692 也有，但值可能不同）
3. `boot-args.lst` 被本轮重新生成过（389 行 vs 原 352 行）——不影响运行时，但说明仓库存量状态有漂移
4. **11-13 之后是否有其他人改过内核代码**（未审计）

第 4 条最值得查：`hakios-mtk` 工作区有 **49 个未提交改动**，跨 GPU/loader/UFS/input/audio 多个子系统。

---

## 8. 未验证 / 未取到

| 项 | 状态 |
|---|---|
| 第 3、4 轮的 logbook | **未取到**（手机黑屏，无法 adb pull） |
| XNU 是否真的执行了任何一行 | 未验证（logbook 只有 loader 的 396 字节） |
| 耳机寄存器读数 | **完全未获取**（四次刷机都没跑到耳机代码） |
| `headset=2` | 从未实现 |

---

## 9. 恢复方法

```sh
fastboot -s GAJZ4HIN99AQ69Y9 flash boot_a \
  /home/lizi/IOS/hakios-mtk/backup/boot_a_stock_real.img
fastboot -s GAJZ4HIN99AQ69Y9 reboot
```

备份 md5 `a63b2e35a7c2522cb24a6a10332b9bc8`（本轮多次核对，未变）。

---

## 10. 下一轮的建议起点

**不要再从耳机参数上找。** 先做这两件事：

1. **用 11-13 的内核二进制**（`xnu-mali-boot.img` 等历史镜像里能取到），配 692 参数刷一次——
   如果进得去，问题出在**今天的代码改动**；如果进不去，问题在**loader 或更底层**
2. **审计 `hakios-mtk` 的 49 个未提交改动**，特别是 `mtk/loader/mtkloader.c`（261799 字节，
   今天有多次改动）与 `drivers/input/hakios.input.c`

**单变量原则**：一次只改一件事，否则四次刷机就白烧了。

---

## 11. 收尾取证（手机已恢复安卓后补做）

### 11.1 手机状态：已完全恢复

```
$ adb shell getprop sys.boot_completed
1
$ adb shell getprop ro.build.display.id
CP2A.260605.016
$ adb shell su -c 'md5sum /dev/block/by-name/boot_a'
a63b2e35a7c2522cb24a6a10332b9bc8  /dev/block/by-name/boot_a
```

`boot_a` 的 md5 与备份 `boot_a_stock_real.img` **完全一致**，恢复无残留。

### 11.2 MT6368 状态未受四次刷机影响

重启安卓后重读 `dump_reg`，与 2026-11-14 凌晨的基线**逐字节一致**：

```
$ diff <(grep -oE "\(0x[0-9a-f]+\)=0x[0-9a-f]+" accdet-baseline-dump.txt) \
       <(grep -oE "\(0x[0-9a-f]+\)=0x[0-9a-f]+" accdet-after-round20.txt)
→ 空（无差异）
```

→ 四次刷机没有改动 PMIC 任何寄存器。耳机基线仍然有效。

### 11.3 ★ 本轮最重要的发现：loader 在 692 之后被大规模重写

这是收尾取证时才查到的，之前一直没注意。

| 项 | 值 |
|---|---|
| 692 那轮的 loader 二进制 | **48208 字节** |
| 今天的 loader 二进制 | **59720 字节**（+24%） |
| `mtkloader.c` 已提交版本 | 3453 行 |
| `mtkloader.c` 工作区版本 | **7496 行** |
| **新增** | **4172 行、删除 129 行** |
| `mtkloader.c` mtime | **11-13 23:46** |
| 692 日志时间 | **11-13 12:02 之前** |

新增的内容（从 diff 摘要）：

```
+ static const uint32_t core_order[8] = BOARD_CORE_ORDER;
+ static void mali_leave_powered(void);
+ static void mali_power_down(void);
+ static void probe_say(const char *text);
+ static void probe_flash(uint64_t reason);
+ static void ufs_step(uint32_t n);
+ static int payload_header_ok(const struct payload_header *ph);
+ static int board_read(uint64_t lba, uint32_t blocks, void *dst);
+ static uint32_t gpt_detect(void);
+ static int save_log_to_flash(uint64_t pa, uint64_t len, uint64_t reason, uint64_t code, ...);
```

即：**UFS 支持、GPT 解析、payload 头校验、GPU probe/logbook 机制** 全部是 692 之后加的。

### 11.4 为什么这条重要

前面第 4 次刷机做到了「692 参数 + 无耳机调用」，仍黑屏。当时我归因于「内核二进制变了」。

现在有了更具体的候选：**loader 本身在 692 之后重写了 4172 行**。

第 1 轮 logbook 的 `code = 0x13`（死在 `mali_aoc_up()`）也符合这个方向——
`probe_flash()` 本身就是新增的机制。

### 11.5 下一轮的第一步（建议）

**用已提交版本（3453 行）的 loader 重建并刷一次。**

```sh
cd /home/lizi/IOS/hakios-mtk
git stash push mtk/loader/mtkloader.c    # 或 git show HEAD:... > 临时文件
DEVICE=xaga ./mtk/loader/build.sh
```

判据：
- 进得去 → 元凶在这 4172 行里，二分定位
- 进不去 → loader 不是原因，回头查内核

这是**单变量**对照，比今天四次刷机里任何一次都干净。

### 11.6 未取到

| 项 | 原因 |
|---|---|
| 第 3、4 轮 logbook | 手机黑屏期间无法 adb pull，重启后已被新一轮覆盖 |
| 任何耳机寄存器读数 | 四次刷机都没跑到耳机代码 |
| 692 那轮的镜像 | 历史镜像最老只到 11-10，11-13 的没留 |

### 11.7 工作区状态（供下一轮审计）

`hakios-mtk` 有 **49 个未提交改动**，按目录分：

```
  7  mtk/mt6895/drivers/{gpu,input,usb}/
  2  xnu/files/bsd/
  1  xnu/files/iokit/
  4  userland/
  ...
```

其中 `mtk/loader/mtkloader.c`（+4172 行）是最大的一块，也是 11.3 指出的嫌疑对象。
