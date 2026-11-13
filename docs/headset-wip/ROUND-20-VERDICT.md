# Round 20 · 刷机结论：耳机驱动已排除，卡点在别处

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
