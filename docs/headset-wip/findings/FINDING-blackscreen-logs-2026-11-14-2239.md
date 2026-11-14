# 黑屏取不到日志 · 根因与修复方案（完整版）

> 时间：2026-11-14 22:39
> 方法：读 `mtkloader.c` 全部日志写入路径，逐条判断可达性
> 结论：**不是日志机制坏了，是四条写入路径里有三条根本走不到**

---

## 0. 一句话

`XNU_WDT_SECONDS = 0` 导致 `MARK_RUNNING` 从不被设置，于是 `rescue_log()`（从 DRAM 捞内核日志的那条路）永远不执行。
黑屏时唯一能走的写入路径是 `logbook_boot_note()`，它只有 loader 自己那几百字节。

**内核日志一直在 DRAM 里（1.5 MB），没人去读。**

---

## 1. 四条日志写入路径，三条不可达

`mtkloader.c` 里所有 `save_log_to_flash()` 的调用点：

| 行 | 函数 | 触发条件 | 黑屏时可达？ |
|---|---|---|---|
| `:1998` | `logbook_boot_note()` | 跳进 XNU **之前**无条件调用（`:3716`） | ✅ **可达** |
| `:2497` | `xnu_returned()` | XNU 主动交还控制权 | ❌ 不可达（XNU 死了） |
| `:5481` | probe 模式 | `BOARD_MALI_RETURN` 之类 | ❌ 不可达（没开） |
| `:2587` | `rescue_log()` | 上一轮标记是 `MARK_RUNNING` | ❌ **不可达（根因）** |

### 1.1 `rescue_log()` 为什么不可达

`mtkloader.c:7344-7347`（主循环里的救援入口）：

```c
if (mark == MARK_RUNNING) {
    rescue_log();
    dbg_delay_ms(5000);
    chain_android();
}
```

`mtkloader.c:3579-3590`（唯一设置 `MARK_RUNNING` 的地方）：

```c
if (XNU_WDT_SECONDS) {
    uint32_t t = XNU_WDT_SECONDS > 31 ? 31 : XNU_WDT_SECONDS;
    marker_set(MARK_RUNNING);
    wr32(TOPRGU_LENGTH, ((t * 32768u / 512u) << 5) | 0x8u);
    wr32(TOPRGU_RESTART, TOPRGU_RESTART_KEY);
    wr32(TOPRGU_MODE, TOPRGU_MODE_KEY | 0x1u);
    out("watchdog: back to Android in "); out_dec(t); out(" s\n");
} else {
    marker_set(0);          ← 走这条
}
```

`board.h:799`：

```c
#define XNU_WDT_SECONDS         0
```

**`0` → `marker_set(0)` → 下一轮 `mark != MARK_RUNNING` → `rescue_log()` 不执行。**

---

## 2. `marker` 的机制本身是对的（不要重写）

`mtkloader.c:284-288`：

```c
/* Run state, kept in DRAM across the watchdog reset (three copies: a bit may flip). It must be a
 * word of DRAM that survives the reset and that neither XNU nor Android writes over. */
#ifndef MARKER_PA
#define MARKER_PA           0x80000000ULL
#endif
```

`marker_set()`（`:590-595`）写**三份副本**（`:593` `wr32(MARKER_PA + 64 * i, v)`）。

→ 设计正确：DRAM 热复位保留 + 三份冗余。

## 3. `rescue_log()` 的实现也是对的

`mtkloader.c:2587-2635`：

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
	...
	if (!wrapped) {
		sp[n++] = (struct span){ log, len };
	} else {
		sp[n++] = (struct span){ log, head_sz };
		sp[n++] = (struct span){ log + head_sz + ring_at, ring_sz - ring_at };
		sp[n++] = (struct span){ log + head_sz, ring_at };
	}
	...
	save_log(sp, n);
}
```

它做三件事：按 Mach-O 符号表定位 `loader_log`、按 head/ring 游标重组、写盘。

**这套三段重组的逻辑是完整的。**

### 3.1 唯一的前提：`find_syms()` 要找得到符号

如果符号找不到，它打印 `"XNU log symbols not found"` 然后返回。
**这是一个需要验证的点**——但只有在 `rescue_log()` 真的被调用之后才知道。

---

## 4. 修复方案（三个，按推荐度）

### 方案 C（我推荐）：无条件设 `MARK_RUNNING`，进 Android 前改 `MARK_DONE`

**理由**：纯逻辑改动，不碰硬件看门狗（`board.h` 自己说 MT6895 的 WDT 寄存器布局未验证），
也不碰 watch 核。

改法：
```c
// mtkloader.c:3579 附近
// 原来：
if (XNU_WDT_SECONDS) { marker_set(MARK_RUNNING); ... } else { marker_set(0); }
// 改成：
marker_set(MARK_RUNNING);           // 无条件
if (XNU_WDT_SECONDS) { ... 看门狗设置 ... }
```

然后在 `chain_android()` 开头（或各条回 Android 的路径）设 `MARK_DONE`，
避免下次启动误走救援路径。

**需要确认**：`chain_android()` 有几个调用点，是否都在进去时设 `MARK_DONE`。

### 方案 B：让 `watcher_start()` 也设 `MARK_RUNNING`

watch 核超时后如果**不复位**而是能继续做事，就能走救援。
但需要确认 watch 核在裸机环境下是否安全写 UFS。

### 方案 A：打开 `XNU_WDT_SECONDS`

风险最高（WDT 寄存器布局未验证），但一旦成功，硬件看门狗本身就是恢复保障。

---

## 5. 一个更低成本的验证（先做这个）

**不改任何代码，先验证「标记机制本身能不能工作」。**

做法：把 `XNU_WDT_SECONDS` 设成 `1`（1 秒），刷一次。
- 如果 1 秒后板子复位、下次启动进 `rescue_log()` 路径 → **机制打通**，再调大
- 如果没有 → 说明 `marker` 或 `rescue_log` 本身有问题，要先修那个

**1 秒的好处**：XNU 几乎肯定来不及做什么，`rescue_log()` 会尝试去找符号并打印结果。
即使失败，`out("XNU log symbols not found\n")` 也会告诉我们卡在哪一步。

---

## 6. 与外部调研的关系

子代理报告 `REPO-SLNeko-XNU.md` 提到 MT6768 那边有一整套机制（三态标记 + pmsg 请求 + 四层日志）。

**我们的 XNU 树里其实已经有这些代码**（`loader_return.c` 三段式、`MARK_*` 三态、`rescue_log`）。
查证：

```sh
$ grep -c "LOADER_LOG_HEAD\|LOADER_LOG_RING" xnu/files/osfmk/arm64/loader_return.c
2                                    ← 三段式在
$ grep -n "MARK_RUNNING\|MARK_DONE" mtk/loader/mtkloader.c | head
289: #define MARK_RUNNING  0x4e52584eu
306: #define MARK_DONE     0x4e52584eu
                                     ← 三态在
$ grep -n "rescue_log" mtk/loader/mtkloader.c
2587: static void rescue_log(void)
7345:     rescue_log();
                                     ← 救援在
```

**所以我们不缺机制，缺的是"让它跑起来"的那一行配置。**

---

## 7. 需要 sol 判断

1. **先做 §5 的 1 秒验证**（最小成本），还是直接上方案 C
2. `chain_android()` 的调用点有哪几处，`MARK_DONE` 应该加在哪
3. `find_syms()` 在我们当前的 XNU 二进制上能不能找到符号（这决定 `rescue_log()` 是否真能工作）

---

## 8. 附带发现：两个代码缺口（子代理提出，我核实过）

### 8.1 耳机驱动没有写后校验

`hakios.mt6895_headset.c:84`：

```c
pmif_wr(PMIF_SWINF_VLDCLR, 1u);      ← 写完就不管了
```

对比 MT6768 那边的 `codec_put()` 纪律：写完读回，`(back & check) != (value & check)` 就 `broken = true` 全线停手。

**这是「写了看不到效果还得猜」的根源。** 值得补。

### 8.2 我们没有 `host_keep` 机制

`grep -rn "host_keep"` 在我们树里**零命中**。

MT6768 用它解决「head+tail 截断丢中间」的问题。
不过我们核实了 `xaga-pull-log.sh` 的读回是**完整 body**（`d[4096:4096+ln]`），不截断，
所以这条对我们当前不是问题——除非将来 `rescue_log()` 的三段重组出问题。

---

## 9. 一个已修的安全问题

`mtk/tools/xaga-pull-log.sh:15`、`:19` 写死了真机序列号 `GAJZ4HIN99AQ69Y9` 和家目录绝对路径。
`mtk/tools/xaga-auto.sh:20`、`mtk/tools/xaga-input.md:210-211` 同样。

**已全部改为从 `HAKIOS_SERIAL` 环境变量取**，`git ls-files` 全量复查已干净。

这三个文件当时是 `A`（已 add 未 commit），**尚未推到 GitHub**，所以没有泄漏。
