# trial 709：PMIF 能读 MT6685 的 SID 9，`0x079e = 0xb8`，`dcxo_bit2 = 0`

> 2026-11-15 23:25，DSH。**实机执行，已恢复原厂。**
> 镜像 md5 `2e6b93bf83f7f60446754efe8e080a34`
> 内核 `c17cf881745511a3c2aa0225ca7a28f3e7b65e853688e13839319d8e6515010d`
> 日志 `sha256 317be4a96ea3032bf8db7f3214131ce503961a3a1d2b04a106016f395a8e8b8d`

---

## 0. 一句话

**PMIF 能寻址 SID 9（此前白名单只允许 4 和 5），`0x079e` 读到 `0xb8`，
`dcxo_bit2 = 0` —— 也就是说 DCXO 输出当前是关闭的。**

**这解释了 `mt_dcxo_event` 为什么要在 `PRE_PMU` 调 `mt6685_set_dcxo(true)`。**

---

## 1. 改动（只读，写入仍被拒绝）

### 1.1 PMIF 白名单

`sys/hakios.pmif.h`：

```c
#define PMIF_SID_MT6685          9u

static inline pmif_status_t
pmif_hdr_admit(uint32_t sid, uint32_t addr, bool write)
{
	if (sid != PMIF_SID_MT6363 && sid != PMIF_SID_MT6368 &&
	    (sid != PMIF_SID_MT6685 || write)) {      /* SID 9: reads only */
		return PMIF_E_SID;
	}
	...
```

**真值表**：

| `sid` | `write` | 结果 |
|---|---|---|
| 9 | `false` | **放行** |
| 9 | `true` | **`PMIF_E_SID`** |
| 4 / 5 | 任意 | 放行（原有行为不变） |

**调用点**：`hakios.mt6895_pmif.c:218` 传 `false`（读），`:257` 传 `true`（写），
所以这条区分是精确生效的。

### 1.2 驱动侧探测

加 `mt6368_read_sid()`（与 `mt6368_read()` 同体，只是 SID 由参数给），
在 `hp_capture()` 的 preconditions 之后读一次 `0x079e`。

**代码路径里没有任何写 SID 9 的语句。**

---

## 2. 实测结果

```
[   20.643110] headset: MT6685 audclk 0x079e = 0xb8 dcxo_bit2=0 (read only)
```

| 项 | 值 | 含义 |
|---|---|---|
| 读到 | **`0xb8`** = `0b1011_1000` | **真实读数**，不是 `0x00`/`0xff` 那种总线不通的特征 |
| `dcxo_bit2` | **`0`** | **DCXO 输出关闭** |
| PMIF 状态 | 无错误行 | 事务成功 |

### 708 的判据全部仍然通过

```
HP on  readback 2588=0xff 258a=0x03 2589=0x3a ... clksq=0x01 clksq_bit0=1: verified
HP off readback 2588=0x00 258a=0x00 2589=0x30 ... clksq=0x00 clksq_bit0=0: verified
HP shutdown attempt 1/2 sequence=ok off_readback=ok: verified
restored 36 configuration bytes: ok
```

**零错误**：`verify failed` 0、`FAILED` 0、`incomplete` 0、`NOT confirmed` 0。

**说明加 SID 9 支持没有干扰耳机路径。**

---

## 3. 这证明了什么

| # | 结论 | 依据 |
|---|---|---|
| 1 | **PMIF 能寻址 SID 9** | 读回真实值 `0xb8` |
| 2 | **`0x079e` 在这个地址上确有内容** | 值非 `0x00` 也非 `0xff` |
| 3 | **`dcxo_bit2` 当前是 0** | 与 `mt_dcxo_event` 需要在 `PRE_PMU` 打开它相符 |
| 4 | **白名单的只读放开是精确的** | 写入路径仍然返回 `PMIF_E_SID` |
| 5 | **不影响耳机相关路径** | 708 判据全部仍过 |

---

## 4. 没证明什么

| 未证明 | 为什么 |
|---|---|
| `0x79e` bit2 的**硬件含义** | 只读到值，没观察到它控制什么 |
| **写它之后 DCXO 真的起振** | 没写 |
| MT6685 的**写通路可用** | 被白名单拒绝，没测 |
| `0xb8` 其余 6 位的含义 | 未分析 |
| 它与**出声**的关系 | 完全没测 |

---

## 5. 下一步的可能性（未执行）

**现在知道 SID 9 可读，逻辑上下一步是"可写"。但写需要放开白名单，
这是一个真实的风险决定，需要单独论证：**

| 项 | 说明 |
|---|---|
| **要写的** | `pmif_write8(9, 0x079e, (0xb8 \| 0x04))` — 置 bit2 |
| **依据** | `mt6685_set_dcxo` 反汇编：`update_bits(0x79e, mask=0x4, val=0x4)` |
| **风险** | 白名单是安全边界；放开写等于允许对 SID 9 任意地址写 |
| **缓解** | 可以只允许**单一地址 `0x079e`** 的写，而不是整个 SID 9 |
| **验证** | 写完再读回，看 bit2 是否变 1 |
| **副作用** | 未知 —— `0xb8` 的其余位会被 mask 保护，但 DCXO 起振的影响范围没测过 |

**我倾向的主法是：把写权限收窄到 `sid==9 && addr==0x079e` 这一个组合**，
而不是放开整个 SID 9。这样即使地址语义有误，影响面也只在一个寄存器上。

**但这需要先决定"值不值得做"** —— 因为 `CLK_BUF` 是 `SUPPLY_SEQ_CLK_BUF` #0，
理论上在 CLKSQ（#6）之前。而我们已经看到 CLKSQ 单独使能后模拟序列仍能通过。

**所以问题是：DCXO 到底是不是出声的必要条件？** 这一点现在**没有任何证据**。

---

## 6. 恢复记录

| 步骤 | 结果 |
|---|---|
| 刷 709 | `OKAY`，0.767s |
| XNU 运行 | 23:17:14 → 23:20:48 返回（约 3 分 34 秒） |
| 取日志 | `len 0x8017c = 524668`，`sum 0x28ce7c0` **校验通过** |
| `reason` | **`0x0`** 正常返回 |
| 日志抢救 | **在设备断开前取到**（用户预判会卡住，实际确实卡住） |
| Android | 第十三次卡住，用户手动回 fastboot |
| 恢复 | `boot_a` = **`a63b2e35…`**，`boot_completed=1` |
| 耳机 | 仍插着 |
| crash | **0** |
