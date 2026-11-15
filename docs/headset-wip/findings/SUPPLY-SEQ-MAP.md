# vendor MT6368 的供电顺序表（`SUPPLY_SEQ_*`，31 项）

> 2026-11-15 23:05，DSH。**源码依据**：`docs/headset-wip/raw/mt6368.h:5586-5633`
> 的 `enum { SUPPLY_SEQ_* }`，以及 `mt6368.c:3254-3330` 的 widget 定义。

---

## 0. 一句话

**vendor 用 31 个编号固定了 MT6368 所有 supply 的开关顺序。
我们现在的直接寄存器写入完全没有这个顺序概念。**

---

## 1. complete order（`mt6368.h:5586-5633`）

| # | 枚举名 | 组 |
|---|---|---|
| 0 | `SUPPLY_SEQ_CLK_BUF` | **common** |
| 1 | `SUPPLY_SEQ_LDO_VAUD18` | |
| 2 | `SUPPLY_SEQ_AUD_GLB` | |
| 3 | `SUPPLY_SEQ_DL_GPIO` | |
| 4 | `SUPPLY_SEQ_UL_GPIO` | |
| 5 | `SUPPLY_SEQ_HP_PULL_DOWN` | |
| **6** | **`SUPPLY_SEQ_CLKSQ`** | ← **CLKSQ 在这里** |
| 7 | `SUPPLY_SEQ_ADC_CLKGEN` | |
| **8** | **`SUPPLY_SEQ_TOP_CK`** | ← **AUDNCP_CK / ZCD13M_CK / AUDIF_CK 在这里** |
| **9** | **`SUPPLY_SEQ_TOP_CK_LAST`** | ← **AUD_CK 在这里** |
| 10 | `SUPPLY_SEQ_DCC_CLK` | |
| 11 | `SUPPLY_SEQ_MIC_BIAS` | |
| 12 | `SUPPLY_SEQ_DMIC` | |
| 13 | `SUPPLY_SEQ_AUD_TOP` | |
| 14 | `SUPPLY_SEQ_AUD_TOP_LAST` | |
| 15 | `SUPPLY_SEQ_DL_SDM_FIFO_CLK` | |
| 16 | `SUPPLY_SEQ_DL_SDM` | |
| 17 | `SUPPLY_SEQ_DL_NCP` | |
| 18 | `SUPPLY_SEQ_AFE` | |
| 19 | `SUPPLY_SEQ_DL_SRC` | **playback** |
| 20 | `SUPPLY_SEQ_DL_ESD_RESIST` | |
| 21 | `SUPPLY_SEQ_HP_DAMPING_OFF_RESET_CMFB` | |
| 22 | `SUPPLY_SEQ_HP_MUTE` | |
| 23 | `SUPPLY_SEQ_DL_LDO_REMOTE_SENSE` | |
| 24 | `SUPPLY_SEQ_DL_LDO` | |
| 25 | `SUPPLY_SEQ_DL_NV` | |
| **26** | **`SUPPLY_SEQ_HP_ANA_TRIM`** | ← **trim 在这里** |
| 27 | `SUPPLY_SEQ_DL_IBIST` | |
| 28-32 | `UL_PGA` / `UL_ADC` / `UL_MTKAIF` / `UL_SRC_DMIC` / `UL_SRC` | **capture** |
| 33-41 | `VOW_*` 系列 | **vow** |

---

## 2. 关键 supply 的寄存器与极性（全部源码可查）

| supply | 枚举 | 寄存器 | 位 | 使能值 | 依据 |
|---|---|---|---|---|---|
| **`CLK_BUF`** | 0 | `SND_SOC_NOPM` | — | 回调 `mt_dcxo_event` | `mt6368.c:3254` |
| **`CLKSQ Audio`** | **6** | **`0x2537`**（`AUDENC_ANA_CON47`） | bit0 | **`1`** | `:3265`、`:158` |
| **`AUDNCP_CK`** | 8 | **`0x230c`**（`AUD_TOP_CKPDN_CON0`） | **bit6** | **`0`**（pdn 极性） | `:3268`、`:173` |
| `ZCD13M_CK` | 8 | `0x230c` | bit5 | `0` | `:3271` |
| **`AUD_CK`** | **9** | `0x230c` | **bit1** | **`0`** | `:3274` |
| `AUDIF_CK` | 8 | `0x230c` | — | `0` | `:3278` |
| `AUDGLB` | 2 | `AUDDEC_ANA_CON26` | — | — | `:3259` |

### `AUD_TOP_CKPDN_CON0`（`0x230c`）的位

vendor `mt6368_set_topck()`（`:171-175`）：

```c
regmap_update_bits(priv->regmap, MT6368_AUD_TOP_CKPDN_CON0,
		   0x66, enable ? 0x0 : 0x66);
```

**mask `0x66` = bit6 | bit5 | bit2 | bit1**：

| 位 | 信号 |
|---|---|---|
| bit1 | `AUD_CK_PDN` |
| bit2 | （mask 里含，见 `mt6368.h:598` 附近） |
| bit5 | `ZCD13M_CK_PDN` |
| bit6 | `AUDNCP_CK_PDN` |

**全部是 `PDN`（power-down）极性：`0` = 时钟运行，`1` = 掉电。**

---

## 3. 现在的状态与缺口

### 3.1 trial 705/706 实测的 preconditions

```
CKPDN=0x00  →  0x230c = 0x00  →  **bit1/2/5/6 全 0 = 时钟门控已使能** ✅
CLKSQ=0x00  →  0x2537 bit0 = 0 →  **CLKSQ 未使能** ❌
NCP=0x00    →  0x2488（AFE_NCP_CFG0），**不是时钟门控**
```

### 3.2 缺口

| # | 缺什么 | 对应 supply # |
|---|---|---|
| 1 | **CLKSQ 26MHz 时钟源** | **#6** |
| 2 | 不知道 `DL_NCP` 的实际寄存器与位 | #17 |
| 3 | 不知道 `HP_ANA_TRIM` 的响应 | #26 |
| 4 | `CLK_BUF` 依赖外部 `mt6685_set_dcxo` | #0 |

**第 1 条是我们能直接做的**：写 `0x2537` bit0 = 1。

**第 4 条需要 MT6685**，而 preflight 发现 `mt6685_audclk.ko` 文件缺失。

### 3.3 但顺序很重要

**按 vendor 的顺序，CLKSQ 是第 6 步，前面还有 6 步**：

`CLK_BUF` → `LDO_VAUD18` → `AUD_GLB` → `DL_GPIO` → `UL_GPIO` → `HP_PULL_DOWN` → **`CLKSQ`**

**我们的 706 已经做了 `HP_PULL_DOWN`（拔出处理）和一部分模拟上电，
但没有按这个编号顺序完整走过。**

---

## 4. 下一步的三条可选路径

| # | 路径 | 代价 | 风险 |
|---|---|---|---|
| **A** | 只补 CLKSQ（写 `0x2537` bit0 = 1），其余不动 | **最小** | 低 —— 只动一个位 |
| **B** | 按 `SUPPLY_SEQ` 顺序完整复现 0-18 步 | 大 | 中 —— 顺序错可能导致异常 |
| **C** | 移植 vendor DAPM 表进 XNU | 最大 | 中 —— 但最完整 |

**我倾向 A**：**先验证 CLKSQ 是唯一缺口，再决定要不要走 B。**

**理由**：706 已经证明模拟上电序列本身能跑通（`2588=0xff 258a=0x03 2589=0x3a`）；
**如果 CLKSQ 补上后仍不能出声，说明缺的不止它，那时再走 B 才有依据。**

---

## 5. 证据边界

| 已证明（源码） | 未证明 |
|---|---|
| 31 项 supply 顺序的确切编号 | 我们的直接写入是否等价于按序执行 |
| CLKSQ 在 `0x2537` bit0，`1` = 使能 | 写它之后 DAC 是否能工作 |
| `0x230c` 是 PDN 极性，`0` = 运行 | `AFE_NCP_CFG0`（`0x2488`）的语义 |
| `HP_ANA_TRIM` 排第 26 | trim 是否需要先有 CLKSQ |
| 706 已能跑通模拟上电序列 | 出声还差几步 |
