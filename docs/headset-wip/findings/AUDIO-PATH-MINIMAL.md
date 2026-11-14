# 耳机出声最小通路（MT6368 侧，裸机 XNU 可执行）

生成：2026-11-14 · 目标平台 xaga / MT6895 + MT6368（SPMI SID 5）
厂商源码：`/home/lizi/IOS/work-xaga/k-xaga` HEAD `0a8d13c1a180fed82803f56337a0b065ff704fb4`
上游对照：`https://github.com/MT6895-Mainline/linux` 分支 `7.2-mt6895-xiaomi-xaga`（下称 *mainline*）

机器可读版本：`audio-path-table.json`（**92 条**；层1 4 / 层2 16 / 层3 48 / 层4 23 / 层5 1）。
每条带 `seq`（全局执行序号）、`bus`（`spmi` / `mmio`）、`sid`、`source_kind`（`src` / `dump` / `android-dump`）。

---

## 0. 一句话结论

**SGEN 正弦验证可行**。MT6368 内部有正弦发生器（`DAC In Mux = Sgen`），
`AFE_TOP_CON0(0x238e) bit0 = 1` 就把 DAC 输入从 AIF_RX 切到内部 SGEN，
**绕开 MT6895 AFE、MTKAIF、DMA、中断、ASoC**。整条链路只剩「SPMI 写 MT6368」一件事，
路径 (a) 共 70 笔写（69 笔 SID 5 + 1 笔 SID 9）。

**修正一个常见误解**：不存在「写寄存器输出恒定 DC 电平」这条路。
`DL_SINE_ON` 是正弦发生器，源码里没有 DC 输出模式。可做的「DC 验证」实际是
**SGEN 正弦验证**——耳机听到持续单音，等效证明模拟通路通了。详见 §5.1。

**唯一可能全盘失败的前置**：MT6368 数字域要 26 MHz `CLKSQ`。厂商在
`CONFIG_MT6685_AUDCLK=m`（`xiaomi_mt6895.config` 已开）下写 **MT6685（SID 9）`0x079e bit2`**
打开 DCXO 缓冲。裸机不写这一位，SGEN 与 DAC 可能没时钟 → 【未验证】。

---

## 1. 证据分级（唯一分栏规则）

| 标记 | 含义 |
|---|---|
| **【源码】** | 值逐字抄自厂商 `mt6368.c` / `mt6368.h` / `mt6895-*.c`，附 `file:line`。与 mainline 同名文件已 diff，**除 API 改名外逻辑零差异** |
| **【真机dump】** | stock Android 上的实际读数，出处 `DEVICE-PROBE-STOCK.md` |
| **【需dump】** | 源码追不到数值，或字段在头文件里无定义。**禁止猜** |
| **【未验证】** | 有源码依据但裸机从未执行 |

---

## 2. 层 1：MT6368 上电与寄存器访问自检

### 2.1 谁负责上电

| 环节 | 责任方 | 依据 | 裸机要做 |
|---|---|---|---|
| SPMI 主机 PMIF | AP 侧 `spmi-mtk-pmif`，`mt6895-spmi` → `mt6xxx_pmif_arb`，`soc_chan=2` | `drivers/spmi/spmi-mtk-pmif-core.c:1198`、`:543` | 只用 `0x1c804000+0x880..0x8a8`（已实测） |
| PMIF 全局使能 | `INF_EN/ARB_EN/CMDISSUE_EN/INIT_DONE`（`+0x24/+0x150/+0x3b8/+0x0`） | 同上 `:1123-1127` | **在 `#if CONFIG_FPGA_EARLY_PORTING` 内**，量产由 LK 配好 → **不要重写**（且 `+0x24` 不在允许区间） |
| MT6368 芯片本体（BUCK/LDO 起振、OTP） | LK / preloader。仓库 `drivers/mfd/` 无 mt6368-core.c，只有 regulator / codec / accdet 三个驱动 | `ls-tree` 结果 | **不需要裸机做** |
| MT6368 `VAUD18` LDO | 厂商由 DAPM `SND_SOC_DAPM_REGULATOR_SUPPLY("mt6368_vaud18")` + regulator 框架开 | `mt6368.c:3258`；`mt6368-regulator.h:163-166` → `0x1cb2 bit0` | **必须裸机手开**（DTS 里无 `regulator-always-on`，`mt6895.dtsi:2393-2400`） |

**结论**：裸机只做 SPMI 读写**不够**——`VAUD18`(0x1cb2) 与码内模拟偏置都得自己写；
但**芯片级上电（BUCK/OTP/SPMI 主机）确实不用管**。

### 2.2 自检寄存器

| 寄存器 | 期望 | 出处 | 判据 |
|---|---|---|---|
| `0x231d` `AUD_TOP_INT_CON0` | `0x06` | 【真机dump】`DEVICE-PROBE-STOCK.md:736` | **首选**。`0x06`=bit1 `RG_INT_EN_ACCDET`+bit2 `RG_INT_EN_ACCDET_EINT0`（`mt6368.h:701-712`）→ 通信通 + 芯片保留关机前状态 |
| `0x2319` `AUD_TOP_RST_CON0` | `0x00` | 【真机dump】`:735` | `0x02` 说明 `RG_ACCDET_RST` 未放开（`mt6368.h:692-700`） |
| `0x0008` `HWCID0` | 【需dump】 | `mt6368.h:19`、`mt6368.c:5627-5629` | vendor 只读后打印，**源码无期望值**，只能当基线 |
| `0x2300` `AUD_TOP_ANA_ID` | 【需dump】 | `mt6368.h:77`、`mt6368-accdet.c:710` | 仅被 accdet 当合法地址下界 |

---

## 3. 层 2：模拟前端（AFE / audio top）

### 3.1 耳机播放走谁

| 判断 | 依据 |
|---|---|
| **DAC 在 MT6368**，耳机 `HPL/HPR` 挂 `AUDDEC_ANA`（`0x2588..0x25a4`） | `mt6368.c:1103-1264` |
| **MT6895 AFE 只是上游数字源，可完全省略（走 SGEN 时）** | `DAC In Mux=Sgen` 时路径 `SGEN DL → DAC In Mux → DACL/DACR`，与 `AIF_RX` 无关（`mt6368.c:3888-3901`） |
| 配合时：MT6895 `ADDA_DL` → MTKAIF → MT6368 `AIF1_RX` → DAC | `mt6895-dai-adda.c:1093`；`mt6368.c:3888` |

### 3.2 DL Power Supply 链（16 条）

| # | 地址 | mask | 值 | delay µs | 出处 |
|---|---|---|---|---|---|
| 1 | `0x1cb2` | `0x01` | `0x01` | — | mt6368.c:5432-5434; mt6368.h:512 |
| 2 | `0x25a2` | `0x10` | `0x00` | — | mt6368.c:165-167; mt6368.h:3682 |
| 3 | `0x2537` | `0x01` | `0x01` | — | mt6368.c:154-160; mt6368.h:3202 |
| 4 | `0x230c` | `0x66` | `0x00` | — | mt6368.c:171-175 (mt6368_set_topck, mask 0x66/val 0x0); mt6368.h:589,592,595,598 |
| 5 | `0x238f` | `0xff` | `0x00` | 250 | mt6368.c:5450-5452, 5583-5584; mt6368.h:966-983 |
| 6 | `0x258d` | `0x01` | `0x01` | 250 | mt6368.c:3067-3070; mt6368.h:3457 |
| 7 | `0x25a3` | `0x05` | `0x05` | 100 | mt6368.c:5483; mt6368.h:3693-3695,3699-3701 |
| 8 | `0x25a3` | `0x10` | `0x10` | 100 | mt6368.c:5485; mt6368.h:3702-3704 |
| 9 | `0x25a0` | `0x10` | `0x00` | — | mt6368.c:3407-3410 (IBIST, invert=1); mt6368.h:3663 |
| 10 | `0x2396` | `0x02` | `0x02` | — | mt6368.c:3342-3345 (SDM_FIFO_CLK); mt6368.h:1076 |
| 11 | `0x2489` | `0xff` | `0xc8` | — | mt6368.c:3161 |
| 12 | `0x2488` | `0xff` | `0x00` | — | mt6368.c:3162 |
| 13 | `0x2488` | `0x01` | `0x01` | — | mt6368.c:3161-3163, 3347-3351 |
| 14 | `0x2388` | `0x01` | `0x01` | — | mt6368.c:3361-3363 (AFE_ON); mt6368.h:877 |
| 15 | `0x2389` | `0x01` | `0x01` | — | mt6368.c:3381-3384 (AFE_DL_SRC), 5472; mt6368.h:882 |
| 16 | `0x079e` | `0x04` | `0x04` | 400 | drivers/mfd/mt6685-audclk.c:47-50; arch/arm64/boot/dts/vendor/mediatek/mt6685.dtsi:8 |

最后一条是 **SID 9（MT6685）**，其余全是 SID 5。若 SID 9 不通可跳过（见 §7 步骤 3）。

### 3.3 关键否定事实

- `0x258d` 在层 2 写 `0x01`，层 3 又整字节写 `0xF1` —— **不是矛盾**，`0xF1` 是超集（bit7 trim_en + bits[6:4] pull-down=7 + bit0 de-res）。按序覆盖即可。
- `0x259e(CON22)` / `0x259d(CON21)` 只在**关断**时被写（`mt6368.c:3003-3004`），上电不碰。
- **层 3 有一段不属于耳机**：`mt6368.c:1106-1131` 的 10 笔写（CON13 `0x10/0x12/0x13`、CON15、CON14、ZCD_CON1/2）**整块在 `if (mux_select[MUX_HP_L] == HP_MUX_HPSPK)` 里**。xaga 耳机走 `HP_MUX_HP`，**这 10 笔不执行**。早期文档（含本文 11-14 版）把它们当通用步骤是错的。

---

## 4. 层 3：DAC / HPL / HPR 上电与音量

前置：`hp_hifi_mode = 0`（`mt6368.c:5667` 默认）、`MUX_HP_L = HP_MUX_HP`（=2，kcontrol `HPL Mux = Audio Playback`，`mt6368.c:657-667`）。

### 4.1 SDM 调制器时钟链（10 条，**必需**）

`SDM` 挂在 `DL Digital Clock CH_1_2`（route `mt6368.c:3875-3876`），`SGEN DL` 也要它（`:3893`）→ 走 SGEN 绕不过。事件 `mt_sdm_event`（`:3084-3118`）PRE_PMU 分 5 步，`0x2396` 三写**不可合并**。

| # | 地址 | mask | 值 | delay µs | 出处 |
|---|---|---|---|---|---|
| 1 | `0x2396` | `0xfd` | `0x06` | — | mt6368.c:3094-3095 (mt_sdm_event step1) |
| 2 | `0x2393` | `0xff` | `0xcb` | — | mt6368.c:3097 |
| 3 | `0x2392` | `0xff` | `0xa1` | — | mt6368.c:3098 |
| 4 | `0x2396` | `0xfd` | `0x03` | — | mt6368.c:3100-3101 |
| 5 | `0x2396` | `0xfd` | `0x0b` | — | mt6368.c:3103-3104 |
| 6 | `0x23a6` | `0xff` | `0x06` | — | mt6368.c:3130 (mt_sdm_3rd_event) |
| 7 | `0x23a3` | `0xff` | `0xcb` | — | mt6368.c:3132 |
| 8 | `0x23a2` | `0xff` | `0xa1` | — | mt6368.c:3133 |
| 9 | `0x23a6` | `0xff` | `0x03` | — | mt6368.c:3135 |
| 10 | `0x23a6` | `0xff` | `0x0b` | — | mt6368.c:3137 |

第 6–10 条是 `SDM_3RD`（CH_3，`0x23a6/0x23a3/0x23a2`）。DACL/DACR 只挂 CH_1_2（`:3897-3901`），故这 5 条**对出声非必需**，优先级低。

### 4.2 HP supply 阶段（3 条）

DAPM supply 按 `subseq` 排序（`soc-dapm.c:1506-1510`）：`HP_PULL_DOWN`=5 早于 `CLKSQ`=6，`HP_MUTE`=21。

| # | 地址 | mask | 值 | delay µs | 出处 |
|---|---|---|---|---|---|
| 1 | `0x258d` | `0x70` | `0x70` | 100 | mt6368.c:391-397 (hp_pull_down true), 2946-2965 |
| 2 | `0x260b` | `0xff` | `0x0f1e` | — | mt6368.c:2977 (mt_hp_mute_event PRE_PMU) |
| 3 | `0x260c` | `0xff` | `0x0f1e` | — | mt6368.c:2978 |

### 4.3 `mtk_hp_enable()` 主体（35 条，`mt6368.c:1133-1264`）

| # | 地址 | mask | 值 | delay µs | 出处 |
|---|---|---|---|---|---|
| 1 | `0x259f` | `0x07` | `0x01` | — | mt6368.c:1148-1150 (hp_hifi_mode=0); mt6368.h:5643-5644 |
| 2 | `0x25a1` | `0xc0` | `0x00` | — | mt6368.c:1153-1155; mt6368.h:5660 |
| 3 | `0x25a1` | `0x03` | `0x00` | — | mt6368.c:1156-1158; mt6368.h:5653 |
| 4 | `0x259c` | `0xff` | `0x87` | — | mt6368.c:1163 |
| 5 | `0x2591` | `0xff` | `0x00` | — | mt6368.c:1167-1173 |
| 6 | `0x258c` | `0xff` | `0x33` | — | mt6368.c:1177 |
| 7 | `0x258d` | `0xff` | `0xf1` | — | mt6368.c:1178 |
| 8 | `0x258b` | `0xff` | `0x00` | — | mt6368.c:1181 |
| 9 | `0x258a` | `0xff` | `0x0c` | — | mt6368.c:1182 |
| 10 | `0x258a` | `0xff` | `0x3c` | — | mt6368.c:1184 |
| 11 | `0x259a` | `0xff` | `0x00` | — | mt6368.c:1186 |
| 12 | `0x259b` | `0xff` | `0x0c` | — | mt6368.c:1187 |
| 13 | `0x2589` | `0xff` | `0x30` | — | mt6368.c:1189; mt6368.h:3398-3403 |
| 14 | `0x2588` | `0xff` | `0xc0` | — | mt6368.c:1190 |
| 15 | `0x2588` | `0xff` | `0xf0` | — | mt6368.c:1192 |
| 16 | `0x258a` | `0xff` | `0xfc` | — | mt6368.c:1194 |
| 17 | `0x259b` | `0xff` | `0x0e` | — | mt6368.c:1200 |
| 18 | `0x259b` | `0xff` | `0x02` | — | mt6368.c:1202 |
| 19 | `0x258a` | `0xff` | `0xff` | — | mt6368.c:1205 |
| 20 | `0x258b` | `0x77` | `0x77` | 600 | mt6368.c:345-351 (hp_main_output_ramp) |
| 21 | `0x259b` | `0xf0` | `0xf0` | 600 | mt6368.c:363-364 (hp_aux_feedback_loop_gain_ramp) |
| 22 | `0x258a` | `0xff` | `0xcf` | — | mt6368.c:1212 |
| 23 | `0x258b` | `0xff` | `0x77` | — | mt6368.c:1213 |
| 24 | `0x260b` | `0x0f9f` | `0x0008` | 600 | mt6368.c:1216-1218, 434-470; mt6368.h:3789,5934 |
| 25 | `0x260c` | `0x0f9f` | `0x0008` | 600 | mt6368.c:434-470 |
| 26 | `0x258a` | `0xff` | `0xc3` | — | mt6368.c:1221 |
| 27 | `0x258a` | `0xff` | `0x03` | 100 | mt6368.c:1223-1224 |
| 28 | `0x25a2` | `0x01` | `0x01` | — | mt6368.c:1227, 177-182; mt6368.h:3673 |
| 29 | `0x2589` | `0xff` | `0x30` | — | mt6368.c:1230 |
| 30 | `0x2588` | `0xff` | `0xff` | — | mt6368.c:1231 |
| 31 | `0x259a` | `0xff` | `0x00` | — | mt6368.c:1238 |
| 32 | `0x259b` | `0xff` | `0xf2` | 100 | mt6368.c:1239, 1241 |
| 33 | `0x2589` | `0x03` | `0x02` | — | mt6368.c:1253-1255; mt6368.h:3392-3394 |
| 34 | `0x2589` | `0x0c` | `0x08` | — | mt6368.c:1258-1260; mt6368.h:3395-3397 |
| 35 | `0x258d` | `0x70` | `0x00` | 100 | mt6368.c:1263, 398-405 |

**子分支说明**：
- 第 1–3 条（CON23/CON25）来自 `hp_hifi_mode` 三分支，这里取 `else`（hifi=0，`:1146-1158`）。
- 第 5 条 `0x2591=0x0` 是 `dl_rate < 96000` 分支（`:1172-1173`）；`>=96k` 时改为 `bit4=1`（`:1167-1171`）。
- 第 13 条 CON1 的第一笔 `0x30` 同时清了 HPL/HPR MUX 字段 —— 后面第 33/34 条再显式设 mux 值（源码 HPL 在前 1253、HPR 在后 1258）。
- 第 33/34 条（CON1 `HP_MUX_HP`）是**耳机分支专有**；`HP_MUX_HPSPK` 分支会额外写 CON13=0x17、CON14=0x01（`:1249-1250`），xaga 不走。
- 第 24/25 条是音量 ramp 的终点 8（= 0 dB），不是 0 也不是最大，见 §4.4。

### 4.4 音量

`0x260b`/`0x260c` bits[4:0] = **8 = 0 dB**（`DL_GAIN_0DB = 8`，`mt6368.c:5679`；`RG_AUDHPLGAIN_SFT=0`，`mt6368.h:3789`）。

- `0` = +8 dB（最响，**不要**）
- `30` = -22 dB（`DL_GAIN_N_22DB`）
- `0x1f` = -40 dB（= mute）

mask `0x0f9f`（`DL_GAIN_REG_MASK`，`mt6368.h:5934`）里 bits[11:7] **在头文件无定义** → 那 5 位语义【需dump】；bits[4:0] 才是增益。

### 4.5 总稳定时间

| 段 | 计算 | µs |
|---|---|---|
| 4.2 第 1 条 pull-down 8 步 | 8×150 | 1200 |
| 4.3 第 20 条 输出级 ramp 8 步 | 8×650 | 5200 |
| 4.3 第 21 条 aux 增益 ramp 16 步 | 16×650 | 10400 |
| 4.3 第 24/25 条 音量 ramp 22 步×2 | 2×22×650 | 28600 |
| 4.3 第 27 条 + 第 32 条 | 120+120 | 240 |
| 4.3 第 35 条 pull-down 8 步 | 8×150 | 1200 |
| 层 2 的 250+250+100+100+400 | — | 1100 |
| **合计** | | **≈47.9 ms** |

**给 60 ms 余量**，裸机 busy-wait。

---

## 5. 层 4：数据从哪来

### 5.1 路径 (a) SGEN 正弦验证（**推荐先做**）

`DAC In Mux` 的 `Sgen` 档 = `AFE_TOP_CON0(0x238e)` bit0 `DL_SINE_ON = 1`（`mt6368.c:712-730`）。
此时 DAC 输入接内部正弦发生器，与 AIF_RX / MTKAIF / AFE / DMA 全无关。

**SGEN 寄存器（4 条，`mt_sgen_event` PRE_PMU，`mt6368.c:1059-1101`）**

| # | 地址 | mask | 值 | delay µs | 出处 |
|---|---|---|---|---|---|
| 1 | `0x23c8` | `0xff` | `0x00` | — | mt6368.c:1080-1082 (mt_sgen_event PRE_PMU) |
| 2 | `0x23c9` | `0xff` | `0x01` | — | mt6368.c:1083-1085; mt6368.h:1585-1588 |
| 3 | `0x23ca` | `0xff` | `0x00` | — | mt6368.c:1086-1088; mt6368.h:1590-1599 |
| 4 | `0x23c8` | `0x80` | `0x80` | — | mt6368.c:3475-3476 (SGEN DL Enable widget) |

**最后两步（顺序关键）**

| # | 地址 | mask | 值 | delay µs | 出处 |
|---|---|---|---|---|---|
| 5 | `0x23c8` | `0x40` | `0x00` | — | mt6368.c:3477-3480 (SGEN MUTE widget, invert=1) |
| 6 | `0x238e` | `0x01` | `0x01` | — | mt6368.c:712-730 (DAC In Mux 'Sgen' = DL_SINE_ON) |

第 4 条（`0x23c8 bit7`）是 `SGEN DL Enable` widget，DAPM 在 PRE_PMU **之后**置位（`soc-dapm.c:1617` vs `:1630`）。
第 6 条 `0x238e bit0` **必须是路径 (a) 的最后一步** —— 之前写等于把 SGEN 接到 DAC 但参数还没配好。

**SGEN 频率/幅度未定**：`FREQ_DIV`(bits[4:0]) 与 `AMP_DIV`(bits[3:0]) 的换算表**源码里没有**
（`mt6368.h:1585-1599` 只给位宽）。`AMP_DIV=1, FREQ_DIV=0` 是**厂商唯一出现的组合**，
照抄即可；实际音高/音量【需dump/实测】。

> **关于「纯 DC」**：没有这条路径。MT6368 的 DAC 无直流输出模式，源码里也没有。
> 「DC 验证」实际能做到的是「SGEN 正弦验证」——耳机听到持续音即证明通路通。

### 5.2 路径 (b) 真 PCM：最小寄存器集

| # | 寄存器（`0x1e100000+`） | mask | 值 | 出处 |
|---|---|---|---|---|
| 1 | `0x1e100000+0x0010` | `0x01` | `0x01` | mt6895-reg.h:24-34,2747 |
| 2 | `0x1e100000+0x004c` | `0x0ff03103` | `<需dump>` | mt6895-reg.h:2761 + DL1_* 位域 |
| 3 | `0x1e100000+0x0054` | `0xffffffff` | `<phys_base>` | mt6895-reg.h:2763; mt6895-afe-pcm.c:1904 |
| 4 | `0x1e100000+0x0064` | `0xffffffff` | `<phys_end>` | mt6895-reg.h:2767; mt6895-afe-pcm.c:1906 |
| 5 | `0x1e100000+0x1b00` | `0x10` | `0x10` | mt6895-reg.h:120 (DL1_ON_SFT=4),3791 |
| 6 | `0x1e100000+0x002c` | `0x20` | `0x20` | mt6895-dai-adda.c:150 |
| 7 | `0x1e100000+0x0030` | `0x40` | `0x40` | mt6895-dai-adda.c:178-179 |
| 8 | `0x1e100000+0x0124` | `0x01` | `0x01` | mt6895-dai-adda.c:952-953; mt6895-reg.h:877 |
| 9 | `0x1e100000+0x0108` | `0x03003003` | `0x03003003` | mt6895-dai-adda.c:1266-1275; mt6895-reg.h:693,708,732,735 |
| 10 | `0x1e100000+0x0108` | `0xf0000000` | `<需dump>` | mt6895-dai-adda.c:1262-1263; mt6895-reg.h:684 |
| 11 | `0x1e100000+0x010c` | `0xffffffff` | `<gain>` | mt6895-dai-adda.c:1277-1278 |
| 12 | `0x1e100000+0x0e40` | `0x01` | `0x01` | mt6895-dai-adda.c:973-976; mt6895-reg.h:2261 |
| 13 | `0x1e100000+0x0e00` | `0xff` | `<需dump>` | mt6895-dai-adda.c:485-491 |
| 14 | `0x1e100000+0x03a0` | `0x01` | `0x01` | mt6895-afe-pcm.c:2490-2491 |
| 15 | `0x1e100000+0x03b4` | `0x01` | `0x01` | mt6895-afe-pcm.c:2494-2495 |
| 16 | `0x1e100000+0x0300` | `0xffffffff` | `<period>` | mt6895-reg.h:2888 |
| 17 | `0x1e100000+0x03a8` | `0x01` | `0x01` | mt6895-afe-pcm.c:2492-2493 |

**中断**：MT6895 AFE 在 GIC 上只占**一条**共享中断 `GIC_SPI 416`，`IRQ_TYPE_LEVEL_HIGH`
（`mt6895.dtsi:2526`）。AFE 内部 32 条（IRQ0..31）是 `AFE_IRQ_MCU_STATUS(0x3a4)` 的位，不是独立 GIC ID。
DL1 用**内部 IRQ0**（`mt6895-afe-pcm.c:2962`）。

**外部依赖（绕不过）**：
- `mt6895_afe_enable_clock()` 要 `clk_prepare_enable` 全套 CCF 时钟（`mt6895-afe-clk.c:235-290`）
- 并调 **SMC** `MTK_SIP_AUDIO_CONTROL / MTK_AUDIO_SMC_OP_DOMAIN_SIDEBANDS`（`:285-288`）
- APLL 走 `apll1_mux_setting()` + `CLK_APLL22M/APLL1_TUNER`（`:374-390`）
- DRAM 请求也走 SMC：`MTK_AUDIO_SMC_OP_DRAM_REQUEST`（`:330-334`）

→ 裸机没有 CCF，也没有 ATF 的 audio SMC 接口。路径 (b) 真正门槛不是寄存器，
是「在 XNU 里重建 MT6895 audio 电源域 + APLL + SPM sideband 握手」。

**工作量**：路径 (a) = 70 笔 SPMI 写 + 60 ms 延时，**无中断/无内存/无 SMC**；
路径 (b) 另加 17 笔 AFE MMIO（其中 8 条值待 dump）+ CCF 等价物 + SMC + 连续物理内存
+ 一个 GIC 中断服务 + 每次 xrun 清 `0x3a8`。**量级差两个数量级。**

### 5.3 (b) 可以先跳过的

| 可跳过 | 理由 |
|---|---|
| MTKAIF 双向校准（`mt6895-mt6368.c:154-322`，42 phase 扫描） | 只为找采样相位；单通道 48k 可先写死 phase=0 |
| 3rd DAC / ADDA6（CH34 通路） | 耳机只用 AIF1 |
| VOW / DSP / offload | 与耳机播放无关 |
| DMIC / ADC / 上行（`UL_SRC*`、`MIC_BIAS*`） | 只放音不录音 |
| 喇叭通路（I2S3 + TFA9874 / `DL2→I2S3`） | 3.5 mm 不走它 |
| 高通滤波 / NLE / SRC / `AFE_CONN_24BIT` / TDM / HDMI | 音质优化或无关通路 |
| `SDM_3RD`（§4.1 第 6–10 条） | DACL/DACR 只挂 CH_1_2 |

---

## 6. 层 5：与耳机检测的耦合

### 6.1 调用链（源码逐跳）

```
accdet 中断（MT6368 IRQ_ACCDET / ACCDET_EINT0）
  mtk_accdet_irq_handler_thread()           mt6368-accdet.c:2417
  -> queue_work(accdet_work)                mt6368-accdet.c:2167
  -> accdet_work_callback()                 mt6368-accdet.c:2138
  -> send_status_event(cable_type, 1)       mt6368-accdet.c:2148 / :857
       HEADSET_NO_MIC -> SND_JACK_HEADPHONE   :1119
       HEADSET_MIC    -> SND_JACK_MICROPHONE  :1132
       LINE_OUT       -> SND_JACK_LINEOUT     :1167
  -> snd_soc_jack_report(&accdet->jack, ...) :1123 / :1132 / :1152
  -> 【到此为止】
```

### 6.2 **关键结论：ACCDET 不会打开音频通路**

| 证据 | 位置 |
|---|---|
| `mt6368_soc_component_driver` **没有** `.set_jack` | `mt6368.c:5718-5728` |
| `mt6368.c` 全文无 `snd_soc_jack` / `SND_JACK` / `PIN_SWITCH` | grep 计数 0（vendor 与 mainline 都 0） |
| machine 驱动只有 `snd_soc_dapm_disable_pin(dapm, EXT_SPK_AMP_W_NAME)` | `mt6895-mt6368.c:354` |
| `mt6368_accdet_init()` 只做 `snd_soc_card_jack_new` + `snd_soc_component_set_jack` | `mt6368-accdet.c:3229-3268` |
| codec 无 `.set_jack` → `snd_soc_component_set_jack()` 直接返回 | `sound/soc/soc-jack.c` 语义 |

→ **厂商代码里「哪个回调触发音频通路打开」的答案是：没有这个回调。**
ACCDET 只上报 input 事件；真正开路由的是 **Android audio HAL 在插入后写
`HPL Mux`/`HPR Mux` kcontrol**，再由 **DAPM 的 PRE_PMU 事件**（`mt_hp_event`，`mt6368.c:1433-1487`）执行 `mtk_hp_enable()`。

**对裸机的意义**：不必模拟 accdet、不必走 jack 状态机。**直接把 `HPL/HPR Mux` 当已切到
`Audio Playback` 写寄存器即可** —— §4.3 的 35 条就是该 DAPM 状态对应的全部写入。
**耳机检测与出声在软件上完全解耦。**

### 6.3 xaga 相关 DAPM widget 全名（120 个，`mt6368.c:3252-3671`）

耳机链路实际走到的（`mt6368.c:3913-3931`）：

```
Headphone L  <-  HPL Mux  <-  DACL  <-  DAC In Mux  <-  AIF_RX / SGEN DL
Headphone R  <-  HPR Mux  <-  DACR  <-  DAC In Mux
```

`HPL Mux`/`HPR Mux` 枚举（`mt6368.c:653-667`）：`Open`(0) / `LoudSPK Playback`(1) /
**`Audio Playback`(2)** / `Test Mode`(3) / `HP Impedance`(4) → 耳机 = **2**。

链路 widget 与事件函数：

| widget | 事件函数 | 位置 |
|---|---|---|
| `CLK_BUF` | `mt_dcxo_event`（只调 MT6685 DCXO） | `mt6368.c:1035-1057` |
| `mt6368_vaud18` | 无（regulator supply） | `:3258` |
| `AUDGLB` / `CLKSQ Audio` | 无 | `:3259-3267` |
| `AUDNCP_CK` / `ZCD13M_CK` / `AUDIF_CK` | 无 | `:3268-3280` |
| `AUD_CK` | 带 `mt_delay_250_event` | `:3274-3277` |
| `AUDIO_TOP_AFE_CTL` | `mt_delay_250_event` | `:3308-3312` |
| `AUDIO_TOP_DAC_CTL` / `_PWR_CLK` / `_PDN_RESERVED` | 无 | `:3313-3330` |
| `SDM` | `mt_sdm_event` | `:3332-3335` |
| `SDM_FIFO_CLK` | 无 | `:3342-3345` |
| `NCP` | `mt_ncp_event` | `:3347-3351` |
| `AFE_ON` | 无 | `:3361-3363` |
| `DL_GPIO` | `mt_dl_gpio_event` | `:3365-3368` |
| `AIF_RX` | 无 | `:3375-3376` |
| `AFE_DL_SRC` | 无 | `:3381-3384` |
| `DL Power Supply` | 无（聚合节点） | `:3387-3388` |
| `ESD_RESIST` | `mt_esd_resist_event` | `:3390-3394` |
| `LDO` / `LDO_REMOTE` / `IBIST` | 无 | `:3395-3402,3407-3410` |
| `NV_REGULATOR` | 带 `mt_delay_100_event` | `:3403-3406` |
| `DAC In Mux` | 无（MUX） | `:3413` |
| `DACL` / `DACR` | 无 | `:3415-3417` |
| `HPL Mux` / `HPR Mux` | `mt_hp_event` | `:3422-3429` |
| `HP_Supply` | 无 | `:3431-3432` |
| `HP_PULL_DOWN` | `mt_hp_pull_down_event` | `:3433-3437` |
| `HP_MUTE` | `mt_hp_mute_event` | `:3438-3442` |
| `HP_DAMP` | `mt_hp_damp_event` | `:3443-3447` |
| `HP_ANA_TRIM` | `mt_hp_ana_trim_event` | `:3448-3452` |
| `Headphone L` / `Headphone R` | 无（OUTPUT） | `:3468-3469` |
| `SGEN DL Enable` / `SGEN MUTE` | `mt_sgen_event`（在 MUTE 上） | `:3475-3480` |
| `SGEN DL SRC` / `SGEN DL` | 无 | `:3481-3485` |
| `SDM_3RD` | `mt_sdm_3rd_event` | `:3336-3339` |

**其余 87 个 widget 全名**（上行/VOW/DMIC/喇叭/LINEOUT，耳机通路用不到，调路由时会撞名字）：

```
AUDGLB_VOW, VOW_AUD_LPW, AUD_VOW, VOW_PLL, VOW_CLK, VOW_SOURCE_CLK, VOW_DIG_CFG,
VOW_PERIODIC_CFG, AUDIO_TOP_ADC_CTL, AUDIO_TOP_ADDA6_ADC_CTL, AUDIO_TOP_PDN_AFE_TESTMODEL,
DL Digital Clock, DL Digital Clock CH_1_2, DL Digital Clock CH_3, UL_GPIO, AIF2_RX,
DAC_3RD, RCV Mux, LOL Mux, Receiver, Headphone L Ext Spk Amp, Headphone R Ext Spk Amp,
LINEOUT L, AIF1TX, AIF2TX, ADC_CLKGEN, DCC_CLK, AIF Out Mux, AIF2 Out Mux, AIFTX_Supply,
MTKAIF_TX, UL_SRC, UL_SRC_DMIC, UL_SRC_34, UL_SRC_34_DMIC, MISO0_MUX, MISO1_MUX,
MISO2_MUX, UL_SRC_MUX, UL2_SRC_MUX, VOW_UL_SRC_MUX, DMIC0_MUX, DMIC1_MUX, DMIC2_MUX,
VOW_AMIC0_MUX, VOW_AMIC1_MUX, ADC_L_Mux, ADC_R_Mux, ADC_3_Mux, ADC_L, ADC_R, ADC_3,
ADC_L_EN, ADC_R_EN, ADC_3_EN, PGA_L_Mux, PGA_R_Mux, PGA_3_Mux, PGA_L, PGA_R, PGA_3,
PGA_L_EN, PGA_R_EN, PGA_3_EN, AIN0, AIN1, AIN2, AIN3, AIN0_DMIC, AIN2_DMIC, AIN3_DMIC,
MIC_BIAS_0, MIC_BIAS_1, MIC_BIAS_2, DMIC_0, DMIC_1, VOW TX, DC Trim
```

（完整 120 个可从 `mt6368.c:3252-3671` 的 `SND_SOC_DAPM_*("<name>"` 首个字符串机械提取。）

**machine 侧 widget**：只有 `Ext_Speaker_Amp`（`mt6895-mt6368.c:113-124`），xaga 上被
`snd_soc_dapm_disable_pin()` 显式关掉（`:354`）→ 与耳机路径无关。

---

## 7. 逐步操作手册（路径 a）

> 前提：SID 5 的 SPMI 读已通（`0x231d` 读到 `0x06`）。
> 全程只用 PMIF 主机寄存器 `0x1c804000+0x880..0x8a8`（SWINF_2）。
> **不碰 PMIF 偏移 >=0x900**（会总线 abort → 整机重启，已实测 3 次）；
> **从设备侧禁写 `0x014b`、禁 RMW `0x0240`**。
> 本文件全部 75 笔 SPMI 事务的从设备地址在 `0x0000..0x260c`，SID 只有 5 与 9，已自检无越界。

### 步骤 1：读基线

```
0x231d  0x2319  0x0008  0x2300
0x1cb2  0x25a2  0x2537  0x230c  0x238f  0x258d
0x25a0  0x25a1  0x25a2  0x25a3
0x2588 .. 0x258d   0x2591  0x2595 .. 0x260c
0x2388  0x2389  0x238e  0x2392  0x2393  0x2396
0x23a2  0x23a3  0x23a6  0x23c8  0x23c9  0x23ca  0x2488  0x2489
```

判据：`0x231d==0x06` → 基线有效；`==0x00` → 芯片可能被复位，基线无意义但不影响后续写；
成片全 `0xFF`/全 `0x00` → 先怀疑读通道，**别写**。

### 步骤 2：只写第 1 条，读回

写 `0x1cb2 = 0x01`，读回应为 `0x01`。读不回 → **停**（PMIF 写坏了，或该位被硬件自动清）。

### 步骤 3：层 2 全量（16 条）

按 §3.2 表顺序。**SID 9 的 `0x079e` 若超时/总线报错 → 跳过继续**，它只是 DCXO 缓冲。

读回验证：`0x2388` bit0==1、`0x238f`==`0x00`、`0x25a2` bit4==0。三项对 → 层 2 成功。

### 步骤 4：SDM 链（§4.1 的 10 条）

`0x2396` 三写 + `0x2393`/`0x2392`；CH_3 那 5 条可选。**不做这步后面写什么都不会响。**

### 步骤 5：SGEN 参数（§5.1 前 4 条，**不含** `0x238e`）

`0x23c8=0x00` → `0x23c9=0x01` → `0x23ca=0x00` → `0x23c8 bit7=1`。读回确认。**此时无声音**。

### 步骤 6：HP 上电（§4.2 的 3 条 + §4.3 的 35 条）

**一步都不能省**（尤其 CON2 的 `0x0c→0x3c→0xfc→0xff→0xcf→0xc3→0x03` 七次写）。
每步按表 busy-wait。

读回验证：`0x2588==0xff`、`0x2589` bits[3:0]==`0x0a`（HPL=2,HPR=2）、
`0x260b`/`0x260c` bits[4:0]==8。四项对 → HP 已上电，此时耳机**安静**。

### 步骤 7：接入 SGEN → 听声

最后一笔：`0x238e bit0 = 1`（mask 0x01，值 0x01）。

**期望：耳机里出现持续单音。**

### 步骤 8：判据矩阵

| 现象 | 结论 | 下一步 |
|---|---|---|
| 听到持续单音 | **通路全通**：SPMI 写 + 模拟前端 + DAC + HP 驱动 + 时钟全对 | 收工，转路径 (b) |
| 完全安静，但读回都对 | 时钟缺失（最可能 MT6685）或 SGEN 频率超耳机/HP 带通 | 补 `0x079e bit2`；改 `0x23c9`/`0x23ca` 组合 |
| 只有「咔哒」 | HP 上电成功但 DAC 无数据 | 查 `0x238e` bit0 读回、`0x2388` bit0 |
| 噪声/爆音 | 有时钟但 SGEN 参数非法 | 回到 `0x23c9=0x01, 0x23ca=0x00` |
| 步骤 3 后读回就不对 | 某条写没落地 | 二分：只写 `0x1cb2/0x25a2/0x2537/0x230c/0x238f` 五条 |
| 写 `0x079e` 总线 abort | SID 9 不可达 | **跳过它**，其余照写 |

### 步骤 9：回退（**不要整字节写 0**，会碰未知位）

```
0x260b mask 0x0f9f <- 0x0f9f   (mute)
0x260c mask 0x0f9f <- 0x0f9f
0x2588 mask 0xff   <- 0x00     (DAC + HP 全关)
0x258a mask 0xff   <- 0x00
0x259b mask 0xff   <- 0x00
0x259c mask 0xff   <- 0x00     (damp off)
0x238e mask 0x01   <- 0x00     (DAC 回 Normal Path)
0x23c8 mask 0xff   <- 0x00
```

先 mute 再断电，避免 POP。不要求还原到步骤 1 的基线——vendor 也没有完整恢复路径
（`mtk_hp_disable` 只关必要位，`mt6368.c:1266-1363`）。

---

## 8. 需要真机 dump 的清单（**唯一一份，不与上面混**）

| 项 | 为什么必须 dump | 总线 |
|---|---|---|
| `0x0008` HWCID0 | 源码无期望值 | SPMI SID5 |
| `0x2300` AUD_TOP_ANA_ID | 源码无期望值 | SPMI SID5 |
| `0x260b`/`0x260c` bits[11:7] | `DL_GAIN_REG_MASK=0x0f9f` 用了这 5 位，`mt6368.h` 无位定义 | SPMI SID5 |
| `0x23c9` AMP_DIV 换算 | 头文件只有位宽，无换算表 | SPMI SID5 |
| `0x23ca` FREQ_DIV 换算 | 同上 | SPMI SID5 |
| `AFE_DL1_CON0(0x4c)` 的 MODE/MINLEN/PBUF | 源码只有位域，无枚举表 | **AP MMIO** |
| `AFE_ADDA_DL_SRC2_CON0(0x108)` RATE[31:28] | `MTK_AFE_ADDA_DL_RATE_48K` 宏定义未取到 | **AP MMIO** |
| `AFE_ADDA_DL_SRC2_CON1(0x10c)` 增益 | `MTK_AFE_ADDA_DL_GAIN_NORMAL` 定义未取到 | **AP MMIO** |
| `AFE_ADDA_MTKAIF_CFG0(0xe00)` DL 值 | `mt6895-dai-adda.c:485-491` 是条件分支 | **AP MMIO** |

**注意**：`0x4c/0x108/0x10c/0xe00/0x300` 都在 MT6895 AFE（AP MMIO），与 MT6368 的 SPMI
是不同总线。**路径 (a) 一个都不需要。**

---

## 9. 本文的已知局限（明确标注，不要当已解决）

| 局限 | 影响 | 状态 |
|---|---|---|
| 全部 70 笔 SPMI 写**从未在裸机执行过** | 任一笔失败都要现场二分 | 【未验证】 |
| `0x079e`(SID 9) 是否必需 | 不写可能无 26 MHz → 全链无声 | 【未验证】 |
| `0x23c9`/`0x23ca` 的 AMP/FREQ 换算 | 声音可能过小/超出耳机频响 | 【需dump】 |
| `0x260b/0x260c` bits[11:7] | 用 vendor 的 `0x0f9f` mask 会连带写未知位 | 【需dump】 |
| `mt6368_vaud18` 在裸机上的实际电压 | DTS 未给 `regulator-always-on`，也未给 `regulator-init-microvolt`；vendor 靠 regulator 框架取默认档。裸机只写 enable 位，**实际输出电压档位取自芯片复位值** | 【未验证】 |
| HP DC trim（`mt_hp_ana_trim_event`） | 需要 eFuse + AUXADC 校准（`get_hp_dctrim_offset`，`mt6368.c:4477+`），本文**未纳入**；不做的后果是左右声道有 DC offset（可能轻微「咔哒」或静态偏流偏大），不影响出声 | 省略 |
| `HP_ANA_TRIM` widget 的 POST_PMU 事件 | 同上，事件函数 `mt_hp_ana_trim_event`（`mt6368.c:3013-3052`）在 POST_PMU 写 `0x258e`/`0x258f`（CON6/CON7）的 HPL/HPR trim 值；裸机跳过则这两位保持复位值 | 省略 |

**结论**：本文给的是「能出声的最小集」，不是「音质达标集」。DC trim / HP_ANA_TRIM
属于校准类，可在出声验证通过后再补。

---

## 10. 与既有文档的差异

| 既有结论 | 本文修正 |
|---|---|
| `PLAYBACK-SEQUENCE.md` §2 的 HP 上电序列只有 10 条 | 耳机分支实际 **35 条**（另有 3 条 HP supply、10 条 SDM） |
| 同文把 CON13 `0x10/0x12/0x13`、CON15、CON14、ZCD_CON1/2 当通用步骤 | 这 10 笔**只在 `HP_MUX_HPSPK` 分支**（`mt6368.c:1106-1131`）。xaga 走 `HP_MUX_HP`，**不执行** |
| 同文 §3 SGEN 开只有 8 条 | 补 `0x238e bit0`（不写就不发声）与 `0x23c8 bit7/bit6`（两个 DAPM widget） |
| 同文 §4.2 说 `mt_hp_damp_event` 无条件清 `0x259c/0x259d` | 只在 `POST_PMD`（关断）发生，上电不碰 |
| 早期推测「写 DC 恒定电平」 | **不存在 DC 输出模式**。可做的是 SGEN 正弦 |

---

## 11. 一句话给实现者

**按 §3.2（16 条）→ §4.1（10 条）→ §5.1 前 4 条 → §4.2+§4.3（38 条）顺序写，
最后一步才置 `0x238e bit0`，等 60 ms，耳机应出声。
唯一可能让整条链哑掉的是 MT6685 的 `0x079e bit2`（DCXO 26M，SID 9）；SID 9 不通就先跳过它试一次。**
