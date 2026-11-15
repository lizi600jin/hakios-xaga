# trial 701：实体耳机插入被识别并分类成功

> 2026-11-15 20:10，DSH。**实机执行，用户首次插上实体耳机，已恢复原厂。**
> 镜像 md5 `8c34657b9f8a67341d78c87e09e06525`
> 日志 `sha256 19c21a4652a2d28f1616fbda92c52ff6c44bf91dba6e3fd6ce89ce4995cdff4e`
> 内核 `e91a9a4a…`（与 trial 700 相同），payload 逐字节相同，**只改 trial 标签**

---

## 0. 一句话

**插着实体三段无麦耳机启动，C（插入接纳）和 B（类型分类）都出现了：
`cable headphone without mic (AB 0, detector 2)`。**

**这是本项目第一次在真机上完成耳机插入检测和类型分类。**

---

## 1. 完整时序（实测）

```
[19.978708] headset: mode 2 on SID 5, serialised in keys worker
[20.163557] headset: sample IRQ 0x00 AB 3 EINT0 3 FSM 0 sync 0
[20.163812] headset: mode 2 initialised, polling every 50 ms; PCM playback not implemented
[20.462246] headset: sample IRQ 0x04 AB 3 EINT0 0 FSM 0 sync 0      <- IRQ_EINT0 置位，EINT0 归零
[20.466945] headset: EINT0 plug in; awaiting ACCDET classification  <- ★ C：插入被接纳
[20.530751] headset: sample IRQ 0x00 AB 3 EINT0 0 FSM 2 sync 1
[20.594657] headset: sample IRQ 0x01 AB 0 EINT0 0 FSM 0 sync 1      <- ★ A：AB 从 3 变 0
[20.595525] headset: cable headphone without mic (AB 0, detector 2) <- ★ B：分类完成
[26.834078] headset: six-second timer disabled mic bias
[26.896935] headset: sample IRQ 0x00 AB 3 EINT0 0 FSM 0 sync 1      <- AB 回到 3（见 §3）
```

**全程 0.13 秒完成从「插入」到「分类」**（`20.466` → `20.595`）。

---

## 2. 判据结果：PASS

用 `tools/judge-701.py --type 3pole` 判定：

| 判据 | 结果 |
|---|---|
| **C 插入接纳** | ✅ `EINT0 plug in; awaiting ACCDET classification` |
| **B 类型分类** | ✅ `cable headphone without mic (AB 0, detector 2)` |
| 与实物一致 | ✅ 用户插的是**三段无麦** |
| 初始化 | ✅ `mode 2 initialised` |
| 分类后错误 | ✅ **0** |
| 意外 plug out | ✅ **0** |
| **总判定** | **`PASS: insertion accepted (C) and classified (B) as 3pole`** |

---

## 3. `AB` 在 6 秒后回到 3 —— 这是正常策略，不是故障

### 3.1 日志顺序

```
[26.834078] headset: six-second timer disabled mic bias
[26.896935] headset: sample IRQ 0x00 AB 3 EINT0 0 FSM 0 sync 1   ← 62 毫秒后
```

### 3.2 源码原因

`hakios.mt6895_headset.c` 的六秒计时器：

```c
if (eint_sync && !micbias_disabled && inserted_ms >= 6000 &&
    (cable == CABLE_NO_MIC || ab == 0 || ab == 3)) {
	if (!configure(ACCDET_CTRL, 0x01, 0) || !clear_irq(IRQ_ACCDET) || ...) {
		return false;
	}
	micbias_disabled = true;
```

**`configure(ACCDET_CTRL, 0x01, 0)` 把 `ACCDET_CTRL` 的 bit0（`SW_EN`）清 0** —— 关掉检测器。

### 3.3 vendor 也是这么做的

`mt6368-accdet.c`：

```c
#define MICBIAS_DISABLE_TIMER (6 * HZ)          /* :213 */
```

`:209` 注释原文：

> micbias_timer: **disable micbias if no accdet irq after eint**

**所以「6 秒后关 micbias / 关检测器」是 vendor 的既定策略，我们的实现与它一致。**

### 3.4 为什么 `AB` 会变成 3

检测器被关闭（`SW_EN=0`）后，比较器失去驱动，`cur_AB` 回到未定义状态读到的默认值 3。

**`EINT0` 仍然是 0**，说明插入状态本身没有被误判。

### 3.5 所以这条不是故障

| 问题 | 答案 |
|---|---|
| 是耳机掉了？ | **不是** —— `EINT0` 仍为 0 |
| 是 state machine bug？ | **不是** —— 是计时器的既定行为 |
| 是正常策略？ | **是** —— 与 vendor 的 6 秒 micbias 关闭一致 |

---

## 4. 这证明了什么、没证明什么

### 已证明（真机、实体耳机）

| 结论 | 依据 |
|---|---|
| **EINT0 中断路径通** | `IRQ 0x04` 置位 |
| **插入被接纳** | C 行出现 |
| **AB 比较器工作** | AB 从 3 变 0 |
| **类型分类正确** | `detector 2` = 三段无麦，与实物一致 |
| **检测延迟约 0.13 秒** | `20.466` → `20.595` |
| **六秒策略正确执行** | 关闭后 AB 回默认值，EINT0 保持 |

### 未证明

| 未证明 | 为什么 |
|---|---|
| 四段带麦耳机 | 只测了三段 |
| 按键 / 线控 | 本轮没按 |
| 麦克风通路 | 只做了分类，没做录音 |
| **出声** | `PCM playback not implemented` |
| 运行中热插拔 | 本轮是启动前插好 |

---

## 5. 判据是否需要加时间窗

**不需要。**

理由：分类结论在 `20.595` 就已经产生并**内部记录**（`cable`、`detector` 变量），
六秒后的 AB 变化是**检测器关闭的副作用**，不影响已经做出的分类。

**但有一条要写清楚**：如果要验证「插入后持续保持可检测」，需要**另设实验**，
不能拿 6 秒后的 AB 读数当作「耳机被拔出」的证据。

---

## 6. 恢复记录

| 步骤 | 结果 |
|---|---|
| 刷 trial 701 | `OKAY`，0.761s |
| XNU 运行 | 20:05:10 → 20:08:42 返回 adb（约 3 分 32 秒） |
| 取日志 | `len 0x7f4e3 = 521443`，`sum 0x28913e3` **校验一致** |
| 屏幕显示 | `=== XNU Log (521443 bytes) ===` 与提取值**完全一致** |
| 设备返回 | XNU 打印 `starting Android` 后停在 backtrace，用户手动回 fastboot |
| 恢复 | 刷回原厂 `a63b2e35…` |
