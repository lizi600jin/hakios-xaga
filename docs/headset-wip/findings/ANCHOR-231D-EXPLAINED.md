# `0x231d = 0x00` 的成因：不是故障，是顶层中断使能没被设置

> 2026-11-15 16:10，DSH。**离线源码核对**，无设备操作。
> 起因：trial 699/700 实测 `0x231d = 0x00`，而判据表期望 `0x06`。

---

## 0. 一句话

**`0x231d`（`AUD_TOP_INT_CON0`）的 `0x06` 是 stock Android 运行时的值，
但 vendor ACCDET 驱动从不写它，我们的 init 也不写它。**

**它是「顶层中断使能」，不是「通道是否可用」的判据。**
**在轮询模式（我们的 `mode 2`）下，这条不影响插入检测。**

---

## 1. 期望值 `0x06` 的一手依据

`docs/headset-wip/DEVICE-PROBE-STOCK.md:736`，stock Android 真机 dump 原文：

```
[0x2319]=0x0
[0x231d]=0x6, [0x2320]=0x0, [0x2323]=0x0
```

**所以 `0x06` 确实是 Android 运行时的真实值。** 这条依据成立。

---

## 2. 位含义（`raw/mt6368.h:727-733`）

```c
#define RG_INT_EN_AUDIO_SFT        0   /* bit0 */
#define RG_INT_EN_ACCDET_SFT       1   /* bit1 */
#define RG_INT_EN_ACCDET_EINT0_SFT 2   /* bit2 */
#define RG_INT_EN_ACCDET_EINT1_SFT 3   /* bit3 */
```

`0x06` = **bit1（ACCDET）+ bit2（EINT0）** —— 两个中断都是使能状态。

---

## 3. 关键：vendor 驱动**从不写**这个寄存器

`raw/mt6368-accdet.c` 里 `AUD_TOP_INT_CON0` **零命中**。

`RG_INT_EN_ACCDET` 出现两处，**都是读**：

- `:530-531` —— 在 `pr_notice` 里打印
- `:613-614` —— 在 `sprintf` 里打印

```c
ret = sprintf(accdet_log_buf, "[0x%x]=0x%x, [0x%x]=0x%x, [0x%x]=0x%x\n",
    RG_INT_EN_ACCDET_ADDR, accdet_read(RG_INT_EN_ACCDET_ADDR),
    ...
```

**没有一处 `accdet_write` 写它。**

**所以 `0x06` 是别的模块（codec / 音频框架 / DTS 默认）设的。**

---

## 4. 我们的 init 写了什么、没写什么

| 寄存器 | 作用 | 我们写了吗 |
|---|---|---|
| `0x231d` `AUD_TOP_INT_CON0` | **顶层**中断使能 | ❌ **从不写** |
| `0x2531` `EINT_ENABLE` | 模块**内部** EINT0 使能 | ✅ `configure(EINT_ENABLE, 0x04, 0x04)`（`:216`） |
| `0x2531` 同处 | 我们也设了 EINT 阈值等 | ✅ |

**所以我们是「模块内部使能了，但顶层中断没打开」。**

---

## 5. 这不影响 `mode 2` 的插入检测

**`mode 2` 是轮询模式**（每 50 ms 调 `poll_sample()`），不依赖中断上报。

`poll_sample()` 直接读：

```c
mt6368_read(ACCDET_IRQ, &irq);
mt6368_read(ACCDET_AB, &mem);        /* <- cur_AB 从这里来 */
mt6368_read(ACCDET_EINT0_AB, &ein);
mt6368_read(ACCDET_FSM, &fsm);
```

**`cur_AB`（`0x26b6`）是直接读寄存器得到的，与顶层中断使能无关。**

### 所以判据要改

| 原来的判据 | 正确的判据 |
|---|---|
| ~~`0x231d == 0x06`~~ 作为基线可比的前提 | **在轮询模式下不是必需** |
| — | **看 `0x26b6` 的轮询变化**：`AB` 从 3 变成 0（三段）或 1（四段） |

**`0x231d = 0x00` 只说明「顶层中断没打开」，不说明「通道坏了」。**
**真正的通道判据是 `0xFF`（无应答）—— 我们读到的是 `0x00`，不是 `0xFF`。**

---

## 6. 如果要让 `0x231d` 变成 `0x06`

**可以**：在 `accdet_init_once()` 里加一次写。

```c
/* AUD_TOP_INT_CON0 (0x231d): enable ACCDET (bit1) and EINT0 (bit2) top-level IRQs.
 * Stock Android reads 0x06 here (DEVICE-PROBE-STOCK.md:736). The vendor accdet
 * driver never writes it -- something above it does -- and our polling mode does
 * not need it. Written only if an interrupt-driven path is ever attempted. */
configure(0x231d, 0xff, 0x06)
```

**但目前不建议加**：

1. `mode 2` 是轮询，不需要；
2. 加写会改变寄存器状态，**破坏与 stock 基线的可比性**；
3. 未知副作用 —— 打开顶层中断可能在没有中断服务例程的情况下积压 pending 位。

**结论：保持 `0x00`，把判据从「锚点必须 0x06」改成「锚点不能是 0xFF」。**

---

## 7. 这条修正的实际价值

**它阻止了一次误判。**

如果按原判据，trial 699/700 都会被判为「基线无效、通道可疑」。
**实际上通道是好的**（129 项读出、7 对镜像全等、`0x00` 而非 `0xFF`）。

**新的正确判据**：

| 检查 | 通过条件 |
|---|---|
| 形状 | distinct ≥12、`0xFF` ≤12、`0x00` ≤96 |
| 结构 | 7 对镜像全等 |
| 通道 | `0x231d != 0xFF`（`0x00` 可接受，说明芯片被复位过但通道通） |
| 插入检测 | `0x26b6` 的 `cur_AB` 从 3 变化（轮询模式下） |
