# trial 707：CLKSQ 写入成功，下电未复位（我的实现缺陷）

> 2026-11-15 23:02，DSH。**实机执行，已恢复原厂。**
> 镜像 md5 `b93ebf94c02cd53e97eebbcf5e1ee00e`
> 内核 `6eeda2b9597db418204b5dc9a5f9c1281e3de7e2993a33db7f9dc17eb16bc10a`
> 日志 `sha256 83dd4ab5202307ce5a4afa129f3805886ca703f323a3bb447550976924eb9e60`

---

## 0. 一句话

**CLKSQ（`0x2537` bit0）在真机上可写、可读回，上电验证通过。
但下电序列里我漏了对应复位，所以 `hp_verify(false)` 永远 FAILED，
恢复也停在 `36 bytes: incomplete`。**

**这是实现缺陷，不是硬件问题。**

---

## 1. 单变量确认

| 项 | 与 706 的差异 |
|---|---|
| boot-args | **只改 `mali_trial=706→707`** |
| 驱动 | 三处：`hp_saved[]` 加 `0x2537`、`hp_enable()` 加一行写、`hp_verify` 加 CLKSQ 检查 |

---

## 2. 上电：CLKSQ 成功

```
[20.591435] headset: HP baseline 0x2537 = 0x00 own 0x01
[20.616500] headset: HP on readback 2588=0xff 258a=0x03 2589=0x3a 25a2=0x11
            gain=0x1f/0x1f gain_mask=0x1f gain_fields=0x1f/0x1f
            clksq=0x01 clksq_bit0=1: verified
```

| 判据 | 期望 | 实测 | 结果 |
|---|---|---|---|
| `clksq_bit0` | `1` | **`1`** | ✅ |
| `0x2588` | `0xff` | `0xff` | ✅ |
| `0x258a` | `0x03` | `0x03` | ✅ |
| `0x2589` | `0x3a` | `0x3a` | ✅ |
| `0x25a2` bit0 | `1` | `0x11` | ✅ |

**关键**：加入 CLKSQ 后，原有的模拟上电序列**全部仍然通过**。

---

## 3. 下电：FAILED，原因是我的代码

```
[169.762554] HP off readback 2588=0x00 258a=0x00 2589=0x30 25a2=0x10
             gain=0x1f/0x1f clksq=0x01 clksq_bit0=1: FAILED
[169.767664] HP shutdown attempt 1/2 sequence=ok off_readback=FAILED: FAILED
[169.808439] HP off readback ... clksq=0x01 clksq_bit0=1: FAILED
[169.813495] HP shutdown attempt 2/2 sequence=ok off_readback=FAILED: FAILED
[169.824695] restored 36 configuration bytes: incomplete
```

### 根因

**`hp_disable_sequence()` 里没有写 `0x2537` 的语句。**
而 `hp_verify(false)` 要求 `!(clksq & 0x01)`，CLKSQ 保持 `1`，所以必然 FAILED。

**两次尝试都是同样原因，不是偶发。**

### 其他部分仍然正确

| 项 | 实测 | 结果 |
|---|---|---|
| `2588` | `0x00` | ✅ |
| `258a` | `0x00` | ✅ |
| `2589` | `0x30`（低四位 0） | ✅ |
| `25a2` | `0x10`（bit0=0） | ✅ |
| gain | `0x1f/0x1f` | ✅ |

**只有 CLKSQ 一项没复位。**

---

## 4. 修复方法（下一轮）

**在 `hp_disable_sequence()` 里加上 `hp_update(0x2537, 0x01, 0)`。**

位置：按 vendor 逆序，CLKSQ 是第 6 步，所以下电时应该在 `hp_pull_down` 之后、
增益复位之前或之后（需要看 vendor `mtk_hp_disable` 的顺序）。

**这一条我下一轮会先核对 vendor 的下电顺序再改，不凭感觉插。**

---

## 5. 这轮证明了什么

| 已证明（实测） | 未证明 |
|---|---|
| **`0x2537` bit0 可写可读回** | 26 MHz 真的在振荡 |
| **CLKSQ 使能后模拟上电序列仍全通** | DAC 真的在转换 |
| 下电缺 CLKSQ 复位会失败 | 出声 |
| 我的实现有遗漏 | CLKSQ 是否出声的必要条件 |

---

## 6. 恢复记录

| 步骤 | 结果 |
|---|---|
| 刷 707 | `OKAY`，0.784s |
| XNU 运行 | 22:54:59 → 22:58:31 返回（约 3 分 32 秒） |
| 取日志 | `len 0x7edde = 519646`，`sum 0x2866970` **校验通过** |
| `reason` | **`0x0`** 正常返回 |
| Android | 第十一次同样卡住，用户手动回 fastboot |
| 恢复 | 刷回原厂 `a63b2e35…`，验证通过 |
| 耳机 | 仍插着（`type:0x8 (headphone)`） |
| crash | **0** |
