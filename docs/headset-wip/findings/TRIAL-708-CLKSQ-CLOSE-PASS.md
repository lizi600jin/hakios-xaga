# trial 708：CLKSQ 上下电完整通过

> 2026-11-15 23:10，DSH。**实机执行，已恢复原厂。**
> 镜像 md5 `3455554c42eeaa694e6830d7ef353fb3`
> 内核 `007a0e9f3b433f6cbe1108a8f1508768378d02ae7fb01ee2eff75c9ec0035cf8`
> 日志 `sha256 a1b5b8a76e2abcb5e53a716ba53c6060889c80069b323f836a37207fa92ccbd0`

---

## 0. 一句话

**707 的缺陷修好后，CLKSQ 的上电与下电都通过：
`clksq_bit0` 从 `0 → 1 → 0`，`attempt 1/2` 一次过，`restored 36 bytes: ok`。**

---

## 1. 707 错在哪，708 怎么修的

### 707 的错误

**我在 `hp_enable()` 加了 CLKSQ 写入，但 `hp_disable_sequence()` 里没有对应复位。**

结果：`off` 阶段 `clksq_bit0` 仍是 `1`，`hp_verify(false)` 要求 `0` → 必然 FAILED。

### 根因：只照抄 `mtk_hp_disable()` 是不够的

**vendor 的 `mtk_hp_disable()`（`mt6368.c:1266`）里根本没有 CLKSQ。**

**CLKSQ 由 DAPM 的 supply widget 管**（`mt6368.c:3265-3267`）：

```c
SND_SOC_DAPM_SUPPLY_S("CLKSQ Audio", SUPPLY_SEQ_CLKSQ,
		      MT6368_AUDENC_ANA_CON47,
		      RG_CLKSQ_EN_SFT, 0, NULL, SND_SOC_DAPM_PRE_PMU),
```

**`SND_SOC_DAPM_SUPPLY_S` 的 `_S` 后缀意味着 DAPM 用同一组
`(reg, shift, invert)` 在 PMD 时自动写反值** —— 所以 vendor 不需要在
`mtk_hp_disable()` 里写它，DAPM 框架替它做了。

### 708 的修法

**在 `hp_disable_sequence()` 末尾加 `hp_update(0x2537, 0x01, 0)`。**

**位置依据**：`CLKSQ` 是 `SUPPLY_SEQ_CLKSQ` **#6**，DAPM 下电按依赖逆序，
所以它**最后**关闭 —— 在 `HP_MUTE`（#22）之后。

---

## 2. 实测结果

```
[ 20.704891] HP on  readback 2588=0xff 258a=0x03 2589=0x3a 25a2=0x11
             gain=0x1f/0x1f gain_mask=0x1f gain_fields=0x1f/0x1f
             clksq=0x01 clksq_bit0=1: verified

[169.795170] HP off readback 2588=0x00 258a=0x00 2589=0x30 25a2=0x10
             gain=0x1f/0x1f gain_mask=0x1f gain_fields=0x1f/0x1f
             clksq=0x00 clksq_bit0=0: verified

[169.800121] HP shutdown attempt 1/2 sequence=ok off_readback=ok: verified
[169.810584] HP shared restored reg=0x259f baseline=0x22 read=0x22 ... verified
[169.820513] HP shared restored reg=0x25a1 baseline=0x55 read=0x55 ... verified
[169.830495] HP shared restored reg=0x25a2 baseline=0x10 read=0x10 ... verified
[169.835444] HP off and original configuration restored
[169.841531] restored 36 configuration bytes: ok
```

### 判据逐条

| 判据 | 期望 | 实测 | 结果 |
|---|---|---|---|
| on `clksq_bit0` | `1` | **`1`** | ✅ |
| on 三联值 | `ff`/`03`/`3a` | **全中** | ✅ |
| on `25a2` bit0 | `1` | `0x11` | ✅ |
| **off `clksq_bit0`** | **`0`** | **`0`** | ✅ |
| off 三联值 | `00`/`00`/`30` | **全中** | ✅ |
| **attempt 编号** | **只 1** | **`1/2`** | ✅ |
| 三条 shared restored | 等于 baseline | `22`/`55`/`10` | ✅ |
| 恢复确认 | 原文出现 | ✅ | ✅ |
| 恢复字节 | `ok` | **`ok`** | ✅ |

### 零错误检查

**`verify failed` 0、`FAILED` 0、`incomplete` 0、`NOT confirmed` 0。**

---

## 3. `restored 36` 为什么没变成 37

**因为 `hp_update()` 传 `remember=false`，不写入 `saved[]`。**

`update_bits()`（`:163-186`）只在 `remember` 为真时才记录：

```c
if (remember) {
    for (slot = 0; slot < saved_count && saved[slot].addr != addr; slot++) {}
    ...
    saved_count++;
}
```

**所以 `0x2537` 不在恢复列表里，`saved_count` 仍是 36。**

**它的复位靠 `hp_disable_sequence()` 里那一行显式写入，不靠恢复路径。**

**这是正确的设计** —— CLKSQ 的 baseline 是 0，下电写 0 就等于恢复原状，
不需要再走一次恢复。

---

## 4. 这轮证明了什么、没证明什么

### 已证明（实测）

| 结论 | 依据 |
|---|---|
| **`0x2537` bit0 可写、可读回** | `0 → 1 → 0`，每步 verified |
| **CLKSQ 使能不破坏模拟上电序列** | 三联值 + gain 全部仍 verified |
| **CLKSQ 能正确关闭** | off readback `clksq_bit0=0` verified |
| **706 的判据仍成立** | 三条 shared restored、`36 bytes: ok` |
| **我的理解是对的**：CLKSQ 不在 `mtk_hp_disable()` 里，由 DAPM 管 | 源码 `:3265-3267` + 实测 |

### 未证明（严格边界）

| 未证明 | 原因 |
|---|---|
| **26 MHz 真的在振荡** | 只读回寄存器位，没测频率 |
| **DAC 真的在转换** | 没有音频数据流过 |
| **出声** | 本轮不做 PCM/AFE |
| **CLKSQ 是出声的必要条件** | 没有对照实验 |

---

## 5. 恢复记录

| 步骤 | 结果 |
|---|---|
| 刷 708 | `OKAY`，0.747s |
| XNU 运行 | 23:03:47 → 23:07:19 返回（约 3 分 32 秒） |
| 取日志 | `len 0x7f061 = 520289`，`sum 0x28739f5` **校验通过** |
| `reason` | **`0x0`** 正常返回 |
| Android | 第十二次卡住（用户手动回 fastboot 途中设备已自行进入 Android） |
| 恢复 | `boot_a` = **`a63b2e35…`** 原厂，`boot_completed=1` |
| 耳机 | 仍插着（`type:0x8 (headphone)`） |
| crash | `F DEBUG` **0**（那 7 行是 emdlogger/imsma 的正常启动日志） |
