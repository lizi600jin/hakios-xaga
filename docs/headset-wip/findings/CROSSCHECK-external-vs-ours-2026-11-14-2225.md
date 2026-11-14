# 交叉核对：外部调研 vs 我们项目的既有结论

> 时间：2026-11-14 22:25
> 做法：把子代理查到的外部结论，逐条与 `hakios.mali610.c` 的注释和 `board.h` 的记录对照
>
> **目的：避免重复劳动，也避免被外部结论误导。**

---

## 一、被我们的既有证据否掉的（不要重做）

### 1.1 「先查 `shader_ready`」—— 因果倒置，已否

外部建议（来自 dtsi 注释）：*Without MFG2-12 the shader cores never become ready (shader_ready stays 0) and the MCU firmware hangs...*

**我们 `hakios.mali610.c:3335-3352` 的分析更硬**：

> Across 2460 records nothing writes 0x180, and SHADER_READY goes from zero to `0x00130013` **and back again eight times in one boot** (records 228, 630, 1016, 1044, 1097, 1493, 1898, 2286).
> **It is not a rail indicator at all; it tracks firmware activity.**
> A zero there is **a consequence of the MCU not running** and cannot be used to argue that the cores have no power.

→ `shader_ready = 0` 是**结果**，不是原因。拿它当判据是因果倒置。
我们已经在 2460 条真机 trace 上验证过这一点。

**结论：这条外部建议不适用，不要照做。**

### 1.2 「MFG2-12 必须全上电」—— 部分对，但不是我们的缺陷

外部 dtsi 注释说 12 个域必须全开。
但我们查到 mainline 自己的 commit `b2fb495073b1` 实测结论是：

> Releasing them one at a time on xaga showed **exactly one domain matters: mfg1**

而且我们的历史日志：`gpufreq 0x4 (0xF3C)` = **mfg1 已上电**，MCU 仍然不动。

→ **mfg1 是必要条件但远不是充分条件。** 我们的 PDCv2 结论没错。

### 1.3 「日志缺失是格式问题」—— 错，我们已经是三段式

子代理读了 `SLNeko-XNU` 的报告后说「三段式值得照抄」。

**我们 `xnu/files/osfmk/arm64/loader_return.c:36-38` 本来就是三段式**：

```c
#define LOADER_LOG_SIZE (1536 * 1024)
#define LOADER_LOG_HEAD (256 * 1024)
#define LOADER_LOG_RING (1024 * 1024)
```

→ 日志格式没问题。问题在**根本没被写盘**（见第二节）。

---

## 二、★ 我找到的真正根因：`rescue_log()` 永远不会被调用

### 2.1 因果关系链

`mtkloader.c:3579-3590`：

```c
if (XNU_WDT_SECONDS) {
    uint32_t t = XNU_WDT_SECONDS > 31 ? 31 : XNU_WDT_SECONDS;
    marker_set(MARK_RUNNING);              ← 只有非零才设
    wr32(TOPRGU_LENGTH, ...);
    wr32(TOPRGU_RESTART, TOPRGU_RESTART_KEY);
    wr32(TOPRGU_MODE, TOPRGU_MODE_KEY | 0x1u);
    out("watchdog: back to Android in "); out_dec(t); out(" s\n");
} else {
    marker_set(0);                          ← 否则清 0
}
```

`board.h:799`：

```c
#define XNU_WDT_SECONDS         0
```

`mtkloader.c:7344-7347`（救援入口）：

```c
if (mark == MARK_RUNNING) {
    rescue_log();
    dbg_delay_ms(5000);
    chain_android();
}
```

**所以：`XNU_WDT_SECONDS = 0` → `MARK_RUNNING` 未写（被清 0）→ 下一轮启动时 `mark != MARK_RUNNING` → `rescue_log()` 不执行 → 从 DRAM 捞日志的通道完全没启用。**

### 2.2 那 XNU 死了靠什么回安卓？

靠 `XNU_WATCH_SECONDS = 120` 的第二核自旋（`board.h:751-753`）：

> a second core spins here from the moment XNU starts. If XNU dies before it reaches EARLY_BOOT, or takes an exception nothing catches, no timer of its own is ever going to fire; **this one resets the board anyway after two minutes.**

**但这条路不经过 `rescue_log()`** —— 它只是复位板子。

`watcher_start()`（`mtkloader.c:3092-3096`）：

```c
static void watcher_start(void)
{
	if (!XNU_WATCH_SECONDS) {
		return;
	}
	watcher_started = 0;
	int64_t r = psci_cpu_on(BOARD_CPU_REG(WATCH_CORE), (uint64_t)watcher_entry, 0);
	...
```

### 2.3 这完全解释了四次黑屏的日志现象

| 现象 | 解释 |
|---|---|
| logbook 只有 396 字节 | 只有 `logbook_boot_note()`（`:1998`）写的那点，因为 XNU 从没交还控制权 |
| 零内核行 | XNU 的 1.5 MB 日志缓冲区在 DRAM 里，但**没人去读** |
| `reason = 0xfeedface` | 那是 boot note 的标记 |

**日志其实一直在 DRAM 里，只是读它的那条路径被关掉了。**

### 2.4 修复方向（给 sol 判断）

有两个独立的选择，**建议都做**：

**A. 打开 `XNU_WDT_SECONDS`（让硬件看门狗 + `MARK_RUNNING` 生效）**
- 好处：`rescue_log()` 会被调用，从 DRAM 捞日志
- 风险：硬件看门狗一开，行为变化较大；而且 `board.h` 注释说过 MT6895 看门狗寄存器布局未验证

**B. 让 `XNU_WATCH_SECONDS` 那条路也做救援**
- 即：watch 核超时后，不要直接 `reset_board()`，而是先把 DRAM 里的日志写盘再复位
- 好处：不改看门狗，只是给已有路径加一步
- 风险：watch 核在裸机环境下写 UFS 是否安全，需要确认

**C. 不管哪条，都建议先做「只读验证」**：
刷一次，然后**主动**触发一次 `MARK_RUNNING` 检查——即验证 `marker_get()` 和 `marker_set()` 本身工作正常。

### 2.5 一个更简单的问题要先回答

**`marker` 存在哪里？** 如果它存在 RAM 里，重启就丢，那这个设计本身就有问题。
需要查 `marker_set()` / `marker_get()` 的实现。

---

## 三、外部调研中确有价值、我们应该吸收的

| 条目 | 来源 | 价值 |
|---|---|---|
| **GPUEB 握手** | `panthor_fw.c:1131-1143`（我亲自核实） | 最高。见另一份 `FINDING-gpueb-evidence` |
| **GPUEB 共享内存 `0x7c600000`** | 真机 DT（我亲自核实） | 零风险观测点 |
| GPU 中断 SPI **512=gpu / 513=mmu / 514=job**（小写名） | mainline dtsi | 中。我们的 IRQ 接线要对照 |
| GIC 是 **4 cell**，带 PPI partition | mainline dtsi | 中。XNU 侧要注意 |
| GPUEB IPI 时序：**升频 core 先、stacks 后；降频反向** | mainline commit | 中。若做 IPI 要遵守 |
| GPUEB 接管后 `clk_get_rate()` **返回脏值** | mainline commit | 低（我们不用 CCF） |
| GPU 6 档频率 950/880/800/610/430/350 + 43 组 rate/volt | mainline patch | 中。若做 OPP 可用 |
| PSCI suspend-param 4 个值 | mainline | 低（当前不做 suspend） |
| **suspend 在下游被明确关了**（"Disable suspend as not working yet"） | mainline commit | 中。「日用」要注意 |
| 分区号 `/dev/sdc86`=userdata、`/dev/sdc13`=nvdata | initramfs | 低（我们有 GPT） |
| 耳机走 **ADDA CH3/CH4**（不是 CH1/CH2），`PGA_L_Mux` 在 AIN0/AIN1 间切 | mainline UCM | **中高。与耳机任务直接相关** |

---

## 四、一处需要立刻跟进的新信息

**耳机通路走 ADDA CH3/CH4，不是 CH1/CH2。**

我们的 `PLAYBACK-SEQUENCE.md` 里写的是从 `mt_hp_event` 反汇编得到的寄存器序列，
但**没有确认过是哪一对 ADDA 通道**。

这条来自 mainline 的 UCM 配置，是**外部独立证据**，应该与我们反汇编的结论对照。

---

## 五、方法论备注

今天两次踩同一个坑：

1. 只抓单个 commit 的 diff 就断言分支状态 → 错
2. 外部结论与项目既有结论冲突时，**先信外部** → 又错（`shader_ready` 那条）

**正确顺序应该是：外部结论 → 与项目既有证据对照 → 谁的证据更硬信谁。**
`shader_ready` 那条，我们的证据（2460 条真机 trace、8 次升降）比外部的一句 dtsi 注释硬得多。

---

## 六、补充核实：`marker` 的设计是对的

`mtkloader.c:284-288`：

```c
/* Run state, kept in DRAM across the watchdog reset (three copies: a bit may flip). It must be a
 * word of DRAM that survives the reset and that neither XNU nor Android writes over. */
#ifndef MARKER_PA
#define MARKER_PA           0x80000000ULL
#endif
#define MARK_RUNNING        0x4e52584eu /* "NXRN" */
```

`marker_set()` 写三份副本（`:590-595`）：

```c
static void marker_set(uint32_t v)
{
	for (int i = 0; i < 3; i++) {
		wr32(MARKER_PA + 64 * i, v);
	}
}
```

**设计完全正确**：DRAM 复位保留 + 三份冗余。

→ **所以 `rescue_log()` 的机制是好的，唯一的问题是它从来没被触发过**，
因为 `XNU_WDT_SECONDS = 0` 让 `marker_set(MARK_RUNNING)` 走不到。

### 这意味着修复成本极低

**不需要重写任何机制，只要让 `MARK_RUNNING` 被设置。**

三个选项：

| 方案 | 改法 | 风险 |
|---|---|---|
| **A** | `board.h` 里 `XNU_WDT_SECONDS` 从 0 改成非零（如 60） | 硬件看门狗寄存器布局未验证（`board.h` 自己说 MT6895 的 WDT 只有 `compatible` 匹配 mt6589，实际值未验证） |
| **B** | 在 `watcher_start()` 里也 `marker_set(MARK_RUNNING)`，让第二核看门狗超时后走 `rescue_log()` 路径 | 需要确认 watch 核超时后**不复位**而是能继续执行——它现在应该是直接 `reset_board()` |
| **C** | 在 XNU 跳转**之前**无条件 `marker_set(MARK_RUNNING)`，进 Android 时再改成 `MARK_DONE` | **最简单、风险最低**：不改看门狗行为，只改标记逻辑。但如果 XNU 正常跑完交还控制权，`xnu_returned()` 会设 `MARK_DONE`，所以要确认这个覆盖顺序 |

**我倾向 C**：它是纯逻辑改动，不碰硬件看门狗，也不碰 watch 核。

但这是代码决策，按分工由 sol 定。
