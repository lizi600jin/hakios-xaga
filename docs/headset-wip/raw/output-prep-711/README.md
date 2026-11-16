# OUTPUT-PREP-711：HP 增益码与 SGEN 字段的主机侧核对

> 2026-11-16 19:11，DSH。**仅主机读取。未碰设备、未改生产代码、未构建、未刷写。**

**🔴 纪律：本文件只报字段名、位域、原文行号与枚举数值。
不把字段名推成 Hz、dBSPL 或电压。文中出现的 "dB" 只在引用源码原文时出现。**

> ## 🔴 本文件含三处已确认错误，更正见 [`CORRECTION-OBJ.md`](CORRECTION-OBJ.md)
>
> 1. **`DL_GAIN_REG_MASK = 0x0f9f` 不缺位** —— `(0x1f<<7)|0x1f` 精确等于 `0x0f9f`，
>    bit7 与 bit11 都在掩码内。本文 §1.2 的"截断左通道最高位"推论**错误，撤回**。
> 2. **`ZCD_CON3`/`ZCD_CON4` 是两个独立寄存器**（`0x260b` 左 / `0x260c` 右），
>    `ramp` 分别写两个地址、各用低 5 位。本文 §3.2 的"右通道可能没设置"**疑问不成立，撤回**。
> 3. **"kcontrol max=30 但允许 31" 不算矛盾**（31 是 mute/关闭专用码，不进 UI 范围属正常）。
>    **真正的矛盾是 TLV 方向与硬件枚举方向相反，且 `invert=0`** —— 见更正文件 §更正 3。
>
> **本文件正文保留原状作为错误记录，不删不改。**

---

## 0. 输入身份

| 文件 | 字节 | SHA256 |
|---|---|---|
| `raw/mt6368.c` | **249493** | **`84ebc78fead84adbea5ff7208bde3d130123637e5bc6a46069712043fa846ddc`** |
| `raw/mt6368.h` | **294048** | **`90680d8b976b0feec3921690f04c2283645c8e56b02e9fb118df9ab88a4d8bba`** |
| `P/logs/profile4-device-20261116/RESULT-TRIAL710.md` | **5215** | **`953e65c3abec688ca57eb5fc9385b9e15e03ea0890abc89befd6d25f27e0254a`** |

**完整原文定位见 `line-index.txt`（180 行）。**

---

## 1. HP 增益码 31 / 30

### 1.1 硬件枚举原文（`mt6368.h:5677-5684`）

```c
enum {
	DL_GAIN_8DB = 0,        /* 行 5678 */
	DL_GAIN_0DB = 8,        /* 行 5679 */
	DL_GAIN_N_1DB = 9,      /* 行 5680 */
	DL_GAIN_N_10DB = 18,    /* 行 5681 */
	DL_GAIN_N_22DB = 30,    /* 行 5682 */
	DL_GAIN_N_40DB = 0x1f,  /* 行 5683 = 31 */
};
```

**关键事实：**

| 码 | 枚举名 | 数值 | 备注 |
|---|---|---|---|
| **30** | `DL_GAIN_N_22DB` | **`0x1e`** | TLV 可达的最大值 |
| **31** | **`DL_GAIN_N_40DB`** | **`0x1f`** | **源码注释称其为 mute** |

**但代码里 31 有多重身份。** 见下。

### 1.2 打包写入宏（`mt6368.h:5931-5934`）

```c
#define DL_GAIN_N_10DB_REG (DL_GAIN_N_10DB << 7 | DL_GAIN_N_10DB)   /* 5931 */
#define DL_GAIN_N_22DB_REG (DL_GAIN_N_22DB << 7 | DL_GAIN_N_22DB)   /* 5932 */
#define DL_GAIN_N_40DB_REG (DL_GAIN_N_40DB << 7 | DL_GAIN_N_40DB)   /* 5933 */
#define DL_GAIN_REG_MASK 0x0f9f                                     /* 5934 */
```

**结构**：左右两通道各占 5 位，**右通道在 bit7 起**（`X << 7 | X`），
掩码 `0x0f9f` 只覆盖两段 5 位字段。

**注意**：`0x1f << 7 = 0xf80`，与 `0x1f` 或起来是 `0xf9f`，
**但掩码写的是 `0x0f9f`（高位被截到 0x0f）**。
**这说明 `DL_GAIN_REG_MASK` 只匹配 4 位高位段**，与 `DL_GAIN_N_40DB_REG = 0xf9f` 在
`regmap_update_bits` 下会**截断左通道最高位**。

**这是一个我没有把握的点，标为待 Codex 判定 —— 我不下结论。**

### 1.3 31 的三个用途（`mt6368.c`）

| 行号 | 上下文 | 用法 |
|---|---|---|
| **2981** | `mt_hp_mute_event` / `POST_PMD` | `/* Set HPL/HPR gain to mute */` → `DL_GAIN_N_40DB_REG` |
| **4386** | 关闭序列 | `/* Set HPL/HPR gain to mute */` → `DL_GAIN_N_40DB_REG` |
| **430** | `is_valid_hp_pga_idx` | **接受 31 作为合法索引** |

**所以 31 在本驱动里既是"合法音量索引"，也是"mute 码"。**

### 1.4 TLV 声明（`mt6368.c:604` 与 `612-616`）

```c
static const DECLARE_TLV_DB_SCALE(hp_playback_tlv, -2200, 100, 0);   /* 行 604 */

SOC_DOUBLE_R_EXT_TLV("Headset Volume",
		   MT6368_ZCD_CON3, MT6368_ZCD_CON4, 0, 0x1E, 0,
		   snd_soc_get_volsw, mt6368_put_volsw,
		   hp_playback_tlv),                                  /* 行 613-616 */
```

| 参数 | 值 |
|---|---|
| TLV 起点 | `-2200`（源码单位 = 0.01 dB，即 **-22.00 dB**） |
| TLV 步进 | `100`（= **1.00 dB**） |
| mute 标志 | `0`（无 mute 位） |
| **kcontrol max** | **`0x1E` = 30** |

---

## 2. 🔴 TLV 与硬件枚举是否矛盾 —— **矛盾，且方向明确**

### 2.1 矛盾清单

| # | 矛盾 | 依据 |
|---|---|---|
| **1** | **kcontrol max = 30，但 `is_valid_hp_pga_idx` 允许 31** | `mt6368.c:615` vs `:428-432` |
| **2** | **TLV 是线性刻度，没有为 31 留位置** | max=0x1E，索引只到 30 |
| **3** | **31 在枚举里叫 `DL_GAIN_N_40DB`，注释说 mute；但 TLV 若线性外推只会得到 31 对应的相邻步进** | 差值与"40dB"不符 |
| **4** | `DL_GAIN_REG_MASK = 0x0f9f` 的高位段只有 4 位，而 `0x1f << 7` 需要 5 位 | 见 §1.2 |

### 2.2 能用 TLV 公式算 31 的 dB 吗 —— **不能**

**TLV 只声明了 `-2200` 起点、`100` 步进、`max=0x1E`。
索引 31 超出声明范围。**

**任何"31 = -40dB"或"31 = 某具体 dB"的说法都不能从 TLV 推出。
源码里 `DL_GAIN_N_40DB` 这个名字和 `mute` 注释是**命名与注释**，不是可计算的刻度。**

**我明确不下这个结论。**

### 2.3 profile4 用的是哪个值

**依据 `RESULT-TRIAL710.md` 与 trial710 日志**：`gain31` 被调用作 **"固定 muted reference"**
（`RESULT-TRIAL710.md` §Limits: "Gain31 remains the fixed muted reference"）。

**这与 `mt_hp_mute_event` / 关闭序列用 31 作为 mute 码一致。**

---

## 3. `headset_volume_ramp`（`mt6368.c:434-467`）

```c
static bool is_valid_hp_pga_idx(int reg_idx)                    /* 行 428-432 */
{
	return (reg_idx >= DL_GAIN_8DB && reg_idx <= DL_GAIN_N_22DB) ||
	       reg_idx == DL_GAIN_N_40DB;
}

static void headset_volume_ramp(struct mt6368_priv *priv,
				int from, int to)              /* 行 434 */
{
	int offset = 0, count = 1, reg_idx;

	if (!is_valid_hp_pga_idx(from) || !is_valid_hp_pga_idx(to)) {
		dev_warn(...);   /* 行 439-443 */
		return;
	}
	...
	while (offset > 0) {
		if (to > from) reg_idx = from + count;
		else          reg_idx = from - count;

		if (is_valid_hp_pga_idx(reg_idx)) {
			regmap_update_bits(priv->regmap, MT6368_ZCD_CON3,
					   DL_GAIN_REG_MASK, reg_idx);
			regmap_update_bits(priv->regmap, MT6368_ZCD_CON4,
					   DL_GAIN_REG_MASK, reg_idx);
			usleep_range(600, 650);            /* 行 465 */
		}
		offset--; count++;
	}
}
```

### 3.1 结构事实

| 项 | 值 |
|---|---|
| 遍历方式 | **逐索引步进**（`from ± count`），每步写 `ZCD_CON3` 与 `ZCD_CON4` |
| 步进延时 | **`usleep_range(600, 650)` µs** |
| 有效性过滤 | 每步都过 `is_valid_hp_pga_idx` |
| 写入方式 | `regmap_update_bits(..., DL_GAIN_REG_MASK, reg_idx)` —— **`reg_idx` 直接当值用，不做 `<<7` 打包** |

### 3.2 🔴 一个结构上的疑问点

**`headset_volume_ramp` 用 `DL_GAIN_REG_MASK`（`0x0f9f`）配合**单个** `reg_idx`（如 30 = `0x1e`）写入，
而 `DL_GAIN_N_22DB_REG` 是 `(30<<7|30)` = `0xf1e`。**

**两处写法不同：ramp 写 `0x1e`，宏写出 `0xf1e`。**

**这说明 `regmap_update_bits` 时右通道位可能没被设置。**

**我没有设备侧 readback 能区分这两种写法的实际效果，标为未证实项，交 Codex 判定。**

### 3.3 三处调用

| 行 | from | to |
|---|---|---|
| **1216** | `DL_GAIN_N_22DB`（30） | `priv->ana_gain[HPOUTL]` |
| **1314** | `priv->ana_gain[HPOUTL]` | `DL_GAIN_N_22DB`（30） |
| **4328** | `DL_GAIN_N_22DB`（30） | `priv->ana_gain[HPOUTL]` |

**所以 vendor 的 HP 上下文里，30（-22dB 档）是"工作点"，31 只在 mute/关闭时用。**

---

## 4. SGEN 的 23c8 / 23c9 / 23ca

### 4.1 地址映射（`mt6368.h:189-191`）

```c
#define MT6368_AFE_SGEN_CFG0  0x23c8
#define MT6368_AFE_SGEN_CFG1  0x23c9
#define MT6368_AFE_SGEN_CFG2  0x23ca
```

### 4.2 `AFE_SGEN_CFG0` = `0x23c8`（`mt6368.h:1565-1584`）

| 位 | 字段 | 掩码 |
|---|---|---|
| **7** | `PMIC6368_C_DAC_EN_CTL` | `0x1` |
| **6** | `PMIC6368_C_MUTE_SW_CTL` | `0x1` |
| 5 | `R_AUD_SDM_MUTE_L` | `0x1` |
| 4 | `R_AUD_SDM_MUTE_R` | `0x1` |
| 3 | `R_AUD_SDM_MUTE_L_2ND` | `0x1` |
| 2 | `R_AUD_SDM_MUTE_R_2ND` | `0x1` |

**trial710 实测 `23c8=0x80`** → **bit7 置位、其余全 0**
→ `C_DAC_EN_CTL=1`、`C_MUTE_SW_CTL=0`、四个 SDM_MUTE 全 0。

**这与 `mask=c0, val=80` 的合并写一致**（保留 DAC_EN=1、MUTE=0）。

### 4.3 `AFE_SGEN_CFG1` = `0x23c9`（`mt6368.h:1585-1589`）

```c
#define PMIC6368_C_AMP_DIV_CH1_CTL_SFT   0
#define PMIC6368_C_AMP_DIV_CH1_CTL_MASK  0xf
```

**只有一个 4 位字段 `C_AMP_DIV_CH1_CTL`（bit0-3）。**

**trial710 实测 `23c9=0x01`** → 该字段 = **1**。

### 4.4 `AFE_SGEN_CFG2` = `0x23ca`（`mt6368.h:1590-1600`）

| 位 | 字段 | 掩码 |
|---|---|---|
| 7 | `C_SGEN_RCH_INV_5BIT` | `0x1` |
| 6 | `C_SGEN_RCH_INV_8BIT` | `0x1` |
| **4:0** | **`PMIC6368_C_FREQ_DIV_CH1_CTL`** | **`0x1f`**（5 位） |

**trial710 实测 `23ca=0x00`** → 全部字段 = 0，含 `FREQ_DIV_CH1_CTL = 0`。

---

## 5. 🔴 频率 / 幅度编码：哪些未证实

**下面每一条我都明确标为未证实，不给出 Hz 或电压。**

| # | 项 | 状态 |
|---|---|---|
| **1** | `C_FREQ_DIV_CH1_CTL`（23ca bit4:0）的实际频率 | **未证实** —— 无任何公式、参考时钟或数据手册依据在手上 |
| **2** | `C_AMP_DIV_CH1_CTL`（23c9 bit3:0）的实际幅度/电压 | **未证实** —— 字段名含 "AMP_DIV"，**但从名推电压不行** |
| **3** | `23c8` 的 `SDM_MUTE_L/R` 与 `_2ND` 的区别 | **未证实** —— 两组同名后缀字段，语义未查 |
| **4** | `C_DAC_EN_CTL=1` 是否等于"DAC 已使能" | **未证实** —— 只是寄存器位为 1 |
| **5** | SGEN 输出是否到 DACL/DACR 数据通路上 | **未证实** —— 只知寄存器配置，未追踪数据流 |
| **6** | `23c9=1` / `23ca=0` 意味着"48k"或任何速率 | **未证实** —— 无参考时钟依据 |
| **7** | SGEN 输出与 `gain31` 的叠加关系 | **未证实** |

**`RESULT-TRIAL710.md` 自身也写明（§Limits）：**

> "No audible-output result, physical clock waveform, DC-offset measurement,
> calibration-quality claim or electrical-silence guarantee follows."

**以及**：

> "configured its fixed codec-local SGEN path"
> —— 只说"配置了"，没说"输出了什么"。

---

## 6. `RESULT-TRIAL710.md` 已读（5215 B）

### 6.1 它确认的内容

| 项 | 原文摘要 |
|---|---|
| 刷写 | revision2 唯一实际刷写件，r1 从未刷写 |
| 内核 | `cac498da…` 15321160 B |
| 参数串 | 572 字节全串匹配，含 `headset=4`、trial710、四个 trim |
| 设备 | chip ID 68、VAUD18 基线 01、分类 `headphone without mic` |
| 三阶段 | on38 / off38 / restored38，全唯一 |
| trim readback | **SID5 `258e=18`、`258f=8b`**，两个 gain 字段均 `1f`，**SGEN `23c8=80`** |
| SID9 079e | on=`bc`、off=`b8`、restored=`b8`，own `04`、keep `fb` |
| HP-off | `2588=00`、`258a=00`、`2589=30`、`25a2=00` |
| 关闭 | attempt 1/2 成功，phase0/down147/restore38 |
| ACCDET | 36 字节恢复 |
| HLOG | reason 0、body 528685 B、sum `0x291d3ce`（**完整整数和，非低字节**） |
| 恢复 | MD5 `a63b2e35…`、slot `_a`、`boot_completed=1` |

### 6.2 它明确排除的内容

> 无出声结果、无物理时钟波形、无 DC offset 测量、
> 无校准质量声明、无电气静音保证。
> **gain31 仍是固定 muted reference。**

### 6.3 它给下一阶段的边界

> "Any subsequent hardware trial needs its own concrete design and pre-flash announcement."

---

## 7. 关于 hp_tone=1（Codex 计划中，本轮未实现）

**CMD 提到 Codex 准备 `hp_tone=1` 可选短音，默认 profile4 保持不变。**

**我这边只记录这个计划，不做任何实现或建议。**

**与本轮证据相关的只有一点**：若将来要出声，
**SGEN 的 `FREQ_DIV`（23ca bit4:0）与 `AMP_DIV`（23c9 bit3:0）是唯一可调字段 ——
而它们的实际频率/幅度对应关系在我的证据里全部是"未证实"（见 §5）。**

---

## 8. 边界

**本轮只做了**：文件 hash 计算、`sed`/`grep` 取原文、位域结构整理、TLV 与枚举的对照。

**没有做**：设备操作、代码修改、构建、刷写、反汇编、
任何 Hz/电压/dBSPL 的数值推断。

**未证实项已在 §5 逐条列出，共 7 项。**
