# 撤回：「XNU 在打印版本号前就死」—— 那 735 字节是 loader 预写的

> 2026-11-15 20:20，DSH。**Codex 指出，我核实后确认它对。**

---

## 0. 一句话

**703/704 日志里的 735 字节不是 XNU 的输出，是 `logbook_boot_note()` 在跳转内核
之前预写的记录。所以它不能证明「XNU 早期就死了」，也不能证明任何关于 XNU 的事。**

---

## 1. 我错在哪

我在 trial 703/704 的日志里看到：

```
mtkloader: about to enter XNU
boot args: ... mali_trial=704 ...
clock: this core measures 2000 MHz
clock: LL(A55) con 80133b13 ...
```

**然后我推断**：

> 「连 `Darwin Kernel Version` 都没打印，说明 XNU 在打印版本号之前就死了。」

**这个推断是错的。**

---

## 2. 源码依据（Codex 给的，我核实）

`mtkloader.c:1937` `logbook_boot_note()`：

```c
static char buf[768];
uint32_t n = 0;
const char *text = "mtkloader: about to enter XNU\nboot args: " XNU_BOOT_ARGS "\n";
const uint64_t pll[4] = { 0x20c, 0x21c, 0x22c, 0x23c };
const char *names[4] = { "LL(A55)", "BL(A78)", "B(A78)", "DSU" };
...
text = "clock: this core measures ";
...
```

`mtkloader.c:1919` 的签名（**Codex 修正：我原来说它是 `sum`，错了**）：

```c
static int save_log_to_flash(uint64_t pa, uint64_t len, uint64_t reason, uint64_t code,
			     uint64_t elr, uint64_t far)
```

`mtkloader.c:2054`：

```c
(void)save_log_to_flash((uint64_t)(uintptr_t)buf, n, 0xfeedfaceull, 0, 0, 0);
```

**`0xfeedface` 是第三个参数，也就是 `reason`，不是 `sum`。**
`:1889` `h->reason = reason;` 把它写进 header 的偏移 16；
真正的 `sum` 在 `:1895` `h->sum = sum;`，偏移 64，是另行计算的校验和。

**实测对照（我重新按字段解析 701 的 header）：**

| 字段 | 701（成功） | 703/704（未返回） |
|---|---|---|
| `len` | `0x7f4e3` = 521443 | `0x2df` = 735 |
| **`reason`** | **`0x0`** | **`0xfeedface`** |
| `sum` | `0x28913e3` | （不同） |

**所以 `reason = 0xfeedface` 确实标记「loader 预写、内核没回来」，但这个判据来自 `reason` 字段，不是 `sum`。**

`:3777` 在 `jump_to_kernel` 之前调用 `logbook_boot_note()`。

### 所以

| 现象 | 我原来的解释 | 实际 |
|---|---|---|
| 735 字节 | XNU 早期的输出 | **loader 预写的固定记录** |
| `reason = 0xfeedface` | ~~`sum` 哨兵，写入被打断~~ | **是 `reason` 字段的正常值，标记「loader 预写」** |
| 校验通过 | ~~说明日志完整写入~~ | **只说明 loader 这次写入成功** |
| 停在 `clock: DSU` | XNU 死在这一行 | **这就是 note 的最后一行** |

---

## 3. 决定性对照：成功运行时 note 会被覆盖

| 项 | trial 701（成功） | 703 / 704（失败） |
|---|---|---|
| 日志开头 | `cpu 0 (phys id 0x0) is a slower core` | `mtkloader: about to enter XNU` |
| **boot note 残留** | **0** | **1** |
| 日志长度 | 521443 | **735** |

**`loader_return.c:392-393`：**

```c
if (!loader_return_entered) {
	loader_log_append_msgbuf();
}
```

**只有 XNU 成功进入返回路径并把内核 msgbuf 追加进去，才会覆盖掉那 735 字节。**

**703/704 从未返回，所以 note 原样留着。**

### 🔴 但这里要收窄（Codex 第二处修正）

**「boot note 还在」只能证明「没有看到 XNU 日志成功落盘」，**
**不能断言「XNU 未曾进入返回路径」。**

区别在于：XNU 可能进入了返回路径，但在**写 flash 之前**就失败了
（例如 `loader_log_append_msgbuf()` 之后、`save_log_to_flash` 之前出错）。
这种情况下 note 同样会留着。

**所以准确的说法是**：
- ✅ 能说：XNU 的内核日志没有成功落盘
- ❌ 不能说：XNU 没有进入返回路径

---

## 4. 所以正确的结论是什么

### 能说的

| 结论 |
|---|
| 703/704 里 XNU **没有完成返回流程**（否则日志会被覆盖） |
| 我们**不知道** XNU 执行到了哪一步 |
| 连「XNU 是否打印过 Darwin 版本行」都**无法判断** |

### 不能说的（我之前说过的，全部撤回）

| 撤回 |
|---|
| ~~「XNU 在打印版本号之前就死了」~~ |
| ~~「这比 Mali 驱动早得多」~~ |
| ~~「说明不是 GPUEB 的问题」~~ |
| ~~「连内存初始化都没完成」~~ |

**这些全部无依据。**

---

## 5. Codex 的另一条判断

> H1 宏及 `_Static_assert` 没有早期执行副作用。

**这是对的** —— `_Static_assert` 是编译期检查，生不成运行时代码；
四个宏只是 `#define`，只在使用处展开。

**所以补丁不可能是「早期挂死」的原因。**

---

## 6. 真正的问题变成什么

**703/704 的现象是：XNU 运行后没有返回，板子重启再进 XNU。**

**这与 GPUEB 补丁的因果关系现在是「未知」，不是「无关」。**

### 要区分它们，需要一条不依赖 XNU 返回的信息

| 可能的办法 | 说明 |
|---|---|
| **让 XNU 更早导出日志** | 例如缩短 `loader_return_after` |
| **用屏幕输出判断** | 屏幕上如果出现 Darwin 版本行，就说明跑过了那里 |
| **串口输出** | 我们的 UART 被改写成跳进 `loader_log_putc`，这条路自己堵住了 |

**第三条是历史决定造成的限制** —— `mtkloader.c:3443-3450` 把内核的
`uart_tx_data` 改成跳转到 `loader_log_putc`，所以内核的串口输出不再到物理 UART。

---

## 7. 方法学教训

**我把「loader 写的东西」当成了「内核写的东西」。**

判据应该是：**日志的第一个字符属于谁**。
`mtkloader:` / `boot args:` / `clock:` 这三个前缀全是 loader 的，
看到它们就说明内核还没有覆盖过这份日志。

**这条以后每次取日志都该先做。**
