# MT6895-Mainline/linux 深度调研

> 调研对象：`https://github.com/MT6895-Mainline/linux`，分支 `7.2-mt6895-xiaomi-xaga`（基于 Linux 7.2）
> 本地 clone：`/tmp/mt6895-mainline`（blobless，depth 50，HEAD = `25fd36e67`）
> 附带仓库：`/tmp/mt6895-quirks`（UCM + mic-switch）、`/tmp/mt6895-initramfs`（cinit + 固件）
> 所有结论均标注 文件:行号。**未读到的内容一律标为未确认，不做推测。**

---

## 0. 一句话：这个移植做到了什么

**它把 xaga 从「一块只会点亮的砖」做成了「一台能用的 Linux 手机」——而且几乎所有脏活都是靠在 dtsi 里写注释 + 一条条寄存器补丁试出来的，不是靠什么现成的 MT6895 支持。**

具体说，这个树里 **没有** 通用意义上的 "MT6895 SoC 支持"，它是**一个个子系统手工接起来的工程记录**：

- 时钟：`drivers/clk/mediatek/clk-mt6895*.c` 共 11 个文件 5409 行，topckgen 一个文件就 3160 行（`clk-mt6895.c:3160`）
- 音频：`sound/soc/mediatek/mt6895/` 14 个 C 文件 21139 行，`mt6895-afe-pcm.c` 单文件 7233 行
- GPU：`drivers/gpu/mediatek/gpueb/` 11 个文件 + panthor 里散落的 MT6895 补丁（不是独立 SoC 驱动）
- 显示：`drivers/gpu/drm/mediatek/mediatek_v2/` 133 个文件（下游 MediaTek DRM 主干搬过来的）
- 电源域：`drivers/soc/mediatek/mtk-scpsys-mt6895.c` 704 行

**对我们最重要的判断**：这个树的注释密度异常高，而且是**实战记录**（「verified on hardware」「twice, recovered over fastboot each time」「observed: …」）。这些是比代码更值钱的东西。

---

## 1. ★ 音频 / 3.5mm 耳机

### 1.1 存在什么

| 组件 | 路径 | 规模 |
|---|---|---|
| AFE（SoC 侧音频前端） | `sound/soc/mediatek/mt6895/` | 14 个文件，21139 行 |
| MT6368 codec | `sound/soc/codecs/mt6368.c` | 1642+ 行（含 3801 行 reg 头 `mt6368.h`） |
| MT6368 accdet（插孔检测） | `sound/soc/codecs/mt6368-accdet.c` | 3640 行 |
| 机器驱动（声卡） | `sound/soc/mediatek/mt6895/mt6895-mt6368.c` | 1642 行 |
| 功放 | `goodix,tfa9874` × 2，走 I2C6 0x34/0x35 | `mt6895-xiaomi-xaga.dts:806-825` |

**结论：耳机（3.5mm）走的是 MT6368 内部 codec 的 ADDA（模拟）通路，不是 I2S 外挂 codec。** 扬声器才走 I2S3 → TFA9874。

### 1.2 ★ 声卡拓扑（`hw:0/1/2` 的物理含义）

`mt6895-mt6368.c:860-901` 的 `mt6895_mt6368_dai_links[]` 数组顺序决定 ALSA 卡号，顺序是：

| 位置 | FE 名 | 通路 | 用途 |
|---|---|---|---|
| 0 | `Playback_1` | DL1 → ADDA_DL → MT6368 DAC | **3.5mm 耳机** |
| 1 | `Capture_1` | UL1 ← MTKAIF mic | 麦克风 |
| 2 | `Playback_2` | DL2 → I2S3 → TFA9874 | 扬声器 |

源码里对此有明确注释（`mt6895-mt6368.c:875-884`）：

> `Speaker stream: DL2 -> I2S3 -> TFA9874. Living on its own memif lets PipeWire expose Speakers and Headphone (DL1 -> ADDA_DL) as two independent sinks, like separate HDA pins on x86.`

UCM 侧完全对应（`quirks/ucm/mt6895-mt6368/HiFi.conf:4-6`）：

```
hw:,0  Playback_1 -> DL1 -> ADDA_DL -> MT6368 DAC   (3.5mm)
hw:,1  Capture_1  <- UL1  <- MTKAIF mic
hw:,2  Playback_2 -> DL2 -> I2S3 -> TFA9874 speakers
```

**这是硬事实**：耳机和扬声器是两条完全独立的 DMA 通路，可以同时出声，互不干扰。

### 1.3 ★★ 耳机通路的关键硬件事实（三条，都很反直觉）

**事实 A：xaga 的 codec AIF 数据输入接在 `AUD_DAT_MOSI2` 焊盘上**

`quirks/ucm/mt6895-mt6368/HiFi.conf:9-11`：
> `NOTE: on xaga the codec's AIF data input sits on the AUD_DAT_MOSI2 pad, i.e. the ADDA_CH34 lanes, so Headphone enables CH3/CH4 (and CH1/2 for parity with downstream HAL).`

对应 dts 里的 pad 说明（`mt6895-xiaomi-xaga.dts:1120-1121`）：
> `CH34/3rd-DAC downlink data lane; on xaga this pad is wired to the codec's AIF data input and carries all DL frames to the MT6368.`

**含义**：如果只使能 CH1/CH2（大多数 MTK 平台的常规做法），xaga 上是**没声音的**，因为数据实际从 CH3/CH4 出去。

**事实 B：内置麦和 3.5mm 耳机麦**共用同一条 pipe，不能同时录立体声**

`quirks/README.md`：
> `On xaga, built in mic and 3.5mm headset are connected to the same pipe, they can't record stereo stream at same time.`

**事实 C：`PGA_L_Mux` 必须在运行时切换 AIN0/AIN1**

不是由 UCM 静态设定，而是由守护进程按插孔状态动态切。`HiFi.conf:13-15`：
> `Mic input pin (PGA_L_Mux) is intentionally NOT set here: the xaga-mic-switch daemon selects AIN0 (built-in) vs AIN1 (headset, when a 4-pole headset with mic is plugged in) from the accdet SW_MICROPHONE_INSERT switch.`

### 1.4 ★★ 耳机出声的确切 mixer 序列（可直接照抄）

`quirks/ucm/mt6895-mt6368/HiFi.conf:45-54`（Headphone EnableSequence，逐条）：

```
cset "name='RCV Mux' 'Open'"
cset "name='HPL Mux' 'Audio Playback'"
cset "name='HPR Mux' 'Audio Playback'"
cset "name='Headset Volume' 20,20"
cset "name='ADDA_DL_CH1 DL1_CH1' 1"
cset "name='ADDA_DL_CH2 DL1_CH2' 1"
cset "name='ADDA_DL_CH3 DL1_CH1' 1"     ← CH3 是 xaga 实际的数据通路
cset "name='ADDA_DL_CH4 DL1_CH2' 1"     ← CH4 同上
```

**我逐条核对过这些控件名在驱动里真实存在**：

| 控件名 | 驱动位置 |
|---|---|
| `Headset Volume` | `sound/soc/codecs/mt6368.c:609` `SOC_DOUBLE_R_EXT_TLV("Headset Volume", ...)` |
| `HPL Mux` | `mt6368.c:3421` `SND_SOC_DAPM_MUX_E("HPL Mux", ...)` |
| `HPR Mux` | `mt6368.c:3425` |
| `RCV Mux` | `mt6368.c:3454` |
| `PGA_L_Mux` | `mt6368.c:3583` |
| `ADC_L_Mux` | `mt6368.c:3556` |
| `MISO0_MUX` | `mt6368.c:3536` |
| `PGA1 Volume` | `mt6368.c:621` |
| `Headset Jack` | `mt6895-mt6368.c:449-452` 附近 |

**麦克风录制序列**（`HiFi.conf:72-85`）：

```
cset "name='MISO0_MUX' 'UL1_CH1'"
cset "name='MISO1_MUX' 'UL1_CH1'"
cset "name='ADC_L_Mux' 'Left Preamplifier'"
cset "name='PGA1 Volume' 3"
cset "name='UL1_CH1 ADDA_UL_CH1' 1"
cset "name='UL1_CH2 ADDA_UL_CH1' 1"
cset "name='UL1_CH3 ADDA_UL_CH3' 1"
cset "name='UL1_CH4 ADDA_UL_CH4' 1"
```

注释说明为什么必须显式设（`HiFi.conf:73-76`）：
> `MT6368 input muxes power the whole capture DAPM path (UL SRC, MTKAIF pin mux, codec TX); register reset defaults leave them disconnected.`

**DAPM 路由**（`sound/soc/codecs/mt6368.c:3916-3929`）：

```
{"HP_Supply", NULL, "HP_PULL_DOWN"},
{"HP_Supply", NULL, "HP_MUTE"},
{"HP_Supply", NULL, "HP_DAMP"},
{"HP_Supply", NULL, "HP_ANA_TRIM"},
{"HPL Mux", NULL, "HP_Supply"},
{"HPR Mux", NULL, "HP_Supply"},
{"HPL Mux", "Audio Playback", "DACL"},
{"HPR Mux", "Audio Playback", "DACR"},
{"HPL Mux", "HP Impedance", "DACL"},
{"HPR Mux", "HP Impedance", "DACR"},
{"HPL Mux", "LoudSPK Playback", "DACL"},
{"HPR Mux", "LoudSPK Playback", "DACR"},
{"Headphone L", NULL, "HPL Mux"},
{"Headphone R", NULL, "HPR Mux"},
```

### 1.5 ★★★ 耳机功放上电的完整寄存器序列（`mtk_hp_enable`）

**这是整个音频部分最值钱的代码**：`sound/soc/codecs/mt6368.c:1102-1263`，函数 `mtk_hp_enable()`。

由 `mt_hp_event()` 在 `SND_SOC_DAPM_PRE_PMU` 时调用（`mt6368.c:1432-1481`），带 refcount 保护（`dev_counter[DEV_HP]`，>1 直接返回）。

**寄存器基址**（`mt6368.h`）：
- AUDDEC 模拟区：`MT6368_AUDDEC_ANA_CON0 = 0x2588`（`mt6368.h:378`），步长 1，CON2=0x258a、CON9=0x2591、CON20=0x259c
- ACCDET 区：`MT6368_ACCDET_CON0 = 0x2688`（`mt6368.h:435`）
- AFE 区：`MT6368_AFE_UL_DL_CON0 = 0x2388`（`mt6368.h:125`）

**上电顺序（逐条，寄存器值照抄自源码）**：

```
#if hp_hifi_mode == 0 (xaga 默认，mt6368.c:5674 "hp hifi mode, default normal mode")
  1. CON23: DRBIAS_HP = DRBIAS_5UA            # HP DR bias 优化
  2. CON25: IBIAS_ZCD = IBIAS_ZCD_3UA         # ZCD bias
  3. CON25: IBIAS_HP  = IBIAS_4UA             # HP/HS/LO bias

  4. CON20 = 0x87                              # HP damp 电路使能 (HPRN/HPLN 4K→VCM)
  5. CON9  = 0x00                              # HP feedback cap 15pF
     （若 dl_rate >= 96000：CON9 的 RG_AUDHPHFCOMPBUFGAINSEL 置 1 → 10.5pF）
  6. CON4  = 0x33                              # HPP/N STB enhance
  7. CON5  = 0xf1

  8. CON3  = 0x00                              # HP aux 输出级 使能（开始）
  9. CON2  = 0x0c
 10. CON2  = 0x3c                              # HP aux feedback loop 使能
 11. CON18 = 0x00                              # HP aux CMFB loop
 12. CON19 = 0x0c
 13. CON1  = 0x30                              # HP driver bias 电路
 14. CON0  = 0xc0
 15. CON0  = 0xf0                              # HP driver core 电路
 16. CON2  = 0xfc                              # 主输出短接到 aux 输出级

 17. hp_in_pair_current(priv, true)            # 逐步加大 HP 输入对电流 → HPM
 18. CON19 = 0x0e                              # HP 主 CMFB loop 使能
 19. CON19 = 0x02                              # HP aux CMFB loop 关闭
 20. CON2  = 0xff                              # HP 主输出级 使能
 21. hp_main_output_ramp(priv, true)           # HPL/R 主输出级 逐级 ramp
 22. hp_aux_feedback_loop_gain_ramp(priv,true) # aux feedback 增益 逐级降
 23. CON2  = 0xcf
 24. CON3  = 0x77

 25. headset_volume_ramp(DL_GAIN_N_22DB → ana_gain[HPOUTL])   # 音量从 -22dB ramp 上来

 26. CON2  = 0xc3                              # HP aux 输出级 关闭
 27. CON2  = 0x03                              # 解除 主/aux 短接
 28. usleep_range(100, 120)

 29. mt6368_set_decoder_clk(priv, true)        # AUD_CLK 使能
 30. CON1  = 0x30                              # Audio DAC 使能
 31. CON0  = 0xff
 32. CON18 = 0x0  (normal) / 0x1 (hifi)        # DAC 低噪模式
 33. CON19 = 0xf2
 34. usleep_range(100, 120)

 35. CON1 的 RG_AUDHPLMUXINPUTSEL = HP_MUX_HP   # HPL MUX → DACL
 36. CON1 的 RG_AUDHPRMUXINPUTSEL = HP_MUX_HP   # HPR MUX → DACR
 37. hp_pull_down(priv, false)                  # 解除 HPL/R 下拉到 AVSS28_AUD
```

**注意 `hp_main_output_ramp()` 和 `headset_volume_ramp()` 都是循环逐级写**——这是防 pop 音的关键，不能一步到位。`hp_main_output_ramp`（`mt6368.c:336-352`）逐级写 `RG_HPLOUTSTGCTRL_VAUDP32` / `RG_HPROUTSTGCTRL_VAUDP32`。

**`mtk_hp_disable()`**（`mt6368.c:1265-`）顺序相反：先 `hp_pull_down(true)`，再关 LO（如果是 HPSPK 模式），等等。

### 1.6 ★★ 插孔检测（accdet）的硬件事实

**它跑在哪条 IRQ 上**：xaga 用 **PMIC EINT0**，不是 AP GPIO EINT。

证据链：
- dts：`headset-use-ap-eint = <0>`（`mt6895.dtsi:2163`）、`headset-eint-num = <0>`（`mt6895.dtsi:2164`）
- 驱动：`mt6368-accdet.c:2653-2662`
  ```c
  "headset-use-ap-eint" → tmp
  if (tmp == 0)  caps |= ACCDET_PMIC_EINT_IRQ;
  else if (tmp == 1) caps |= ACCDET_AP_GPIO_EINT;
  "headset-eint-num" → tmp
  if (tmp == 0)  caps |= ACCDET_PMIC_EINT0;
  else if (tmp == 1) caps |= ACCDET_PMIC_EINT1;
  ```
- 代码里还有 xaga 专门的裁剪注释：
  - `mt6368-accdet.c:7`：`/* xaga: USB-TypeC headset path unused; tcpc stack not ported */`
  - `mt6368-accdet.c:24`：`/* AP-GPIO EINT path is not used on xaga; legacy gpio_set_debounce was removed */`
  - `mt6368-accdet.c:44-45`：`xaga: the USB-TypeC headset notifier path is unused (headset-use-ap-eint=0) and the downstream tcpc stack is not ported, so its headers are dropped.`

**状态机**：`check_cable_type()`（`mt6368-accdet.c:2023-2155`），状态枚举在 `mt6368-accdet.h:45-52`：

```
PLUG_OUT = 0, MIC_BIAS = 1, HOOK_SWITCH = 2, BI_MIC_BIAS = 3, LINE_OUT = 4, STAND_BY = 5
```

cable_type 枚举（`mt6368-accdet.h:35-41`）：
```
NO_DEVICE = 0, HEADSET_MIC = 1, HEADSET_NO_MIC = 2, HEADSET_FIVE_POLE = 3, LINE_OUT_DEVICE = 4
```

**A/B 检测的物理含义**（读 `check_cable_type` 的判定逻辑反推）：

| `cur_AB` | 判定 | 结果 |
|---|---|---|
| `ACCDET_STATE_AB_00` (0) | 在 PLUG_OUT 且 `eint_sync_flag` | `cable_type = HEADSET_NO_MIC` → `HOOK_SWITCH` |
| `ACCDET_STATE_AB_01` (1) | 在 PLUG_OUT | `cable_type = HEADSET_MIC` → `MIC_BIAS` |
| `ACCDET_STATE_AB_11` (3) | 在 PLUG_OUT | 保持 `PLUG_OUT` / `NO_DEVICE` |
| AB_00 在 MIC_BIAS/HOOK_SWITCH | 按键按下 | `multi_key_detection()` |

**AB 状态读哪个寄存器**：`ACCDET_MEM_IN_ADDR = MT6368_ACCDET_CON46 = 0x26b6`，取 bit[7:6]（`mt6368.h:4993-4996`，`ACCDET_MEM_IN_SFT = 6`，掩码 `0x3`）。定义在 `mt6368.h:5514-5518`。

**EINT 状态寄存器**：`ACCDET_IRQ_ADDR = MT6368_ACCDET_CON33 = 0x26a9`（`mt6368.h:4593-4594`），`ACCDET_EINT_IRQ_B2_B3 = (0x03 << ACCDET_EINT0_IRQ_SFT)`（`mt6368.h:5511`）。

**按键阈值**（dts，`mt6895.dtsi:2167-2169`）：
```
headset-three-key-threshold      = <0 0x50 0xdc 0x190>
headset-three-key-threshold-CDD  = <0 0x79 0xc0 0x258>
headset-four-key-threshold       = <0 0x3a 0x79 0xc0 0x190>
```

`key_check()`（`mt6368-accdet.c:1051-1076`）按这些阈值把 ADC 值映射成 `DW_KEY / UP_KEY / MD_KEY / AS_KEY`。

**按键映射到 Linux input**（`mt6368-accdet.c:3291-3294`）：
```c
SND_JACK_BTN_0 → KEY_MEDIA
SND_JACK_BTN_1 → MEDIA_NEXT_SCAN_CODE
SND_JACK_BTN_2 → MEDIA_PREVIOUS_SCAN_CODE
SND_JACK_BTN_3 → KEY_VOICECOMMAND
```

**jack 注册**（`mt6368-accdet.c:3279-3286`）：
```c
snd_soc_card_jack_new(card, "Headset Jack",
    SND_JACK_HEADSET | SND_JACK_LINEOUT | SND_JACK_MECHANICAL, &accdet->jack);
```

**mic bias 配置**（`mt6368-accdet.c:3038-3062`）——xaga 是 `accdet-mic-vol = <8>`（`mt6895.dtsi:2158`）：

```
mic_vol <= 7 : 写 RG_AUDMICBIAS1VREF，micbias1 <= 2.7V
mic_vol == 8 : RG_AUDMICBIAS1LOWPEN 置位 + RG_AUDMICBIAS1HVEN |= (3<<shift)   ← xaga 走这条
               → micbias1 = 2.8V
mic_vol == 9 : → micbias1 = 2.85V
```

**mic mode**（`mt6368-accdet.c:3064-3087`）——xaga 是 `accdet-mic-mode = <2>`（`mt6895.dtsi:2160`）：

```
MODE_1 : ACC mode        → RG_ACCDET_MODE_ANA11_MODE1 + 模拟快放电
MODE_2 : DCC 低成本模式，无内部 bias → RG_ACCDET_MODE_ANA11_MODE2 + 快放电   ← xaga 走这条
MODE_6 : DCC 低成本模式，有内部 bias → RG_ACCDET_MODE_ANA11_MODE6 + AUDMICBIAS1DCSW1PEN
```

**去抖参数**（dts，`mt6895.dtsi:2161-2162`）：
```
accdet-plugout-debounce = <1>
headset-mode-setting = <0x500 0x500 0x01 0x1f0 0x800 0x800 0x20 0x44 0x04 0x01 0x05 0x03 0x03 0x05 0x0e>
```
后者按顺序填入 `struct pwm_deb_settings`（`mt6368-accdet.h:85-102`）：`pwm_width, pwm_thresh, fall_delay, rise_delay, debounce0, debounce1, debounce3, debounce4, eint_pwm_width, eint_pwm_thresh, eint_debounce0..3, eint_inverter_debounce`。

**按键去抖的软件修正**（`mt6368-accdet.c:244-245`）：
```c
button_press_debounce    = 0x400;   /* 32ms */
button_press_debounce_01 = 0x800;   /* 64ms, 修复快速连按双键丢键 */
```

### 1.7 MTKAIF pad 的 pinctrl（耳机数据怎么进 codec）

`mt6895-xiaomi-xaga.dts:1045-1050` 有一条重要的架构级注释：

> `Combined MTKAIF states: pinctrl_select_state() is exclusive, so the per-group states used downstream cannot be stacked one after another on kernels where switching away actively restores released pins. Apply the whole interface with a single state instead.`

以及 `mt6895-xiaomi-xaga.dts:1081-1082`：
> `Keep the DL data lanes (incl. the wired MOSI2) alive so an UL request cannot strip them from a concurrent playback stream.`

**引脚对应**（`mt6895-xiaomi-xaga.dts:1053-1135`）：

| 功能 | GPIO | mux 名 |
|---|---|---|
| CLK_MOSI | 202 | `PINMUX_GPIO202__FUNC_AUD_CLK_MOSI` |
| SYNC_MOSI | 203 | `PINMUX_GPIO203__FUNC_AUD_SYNC_MOSI` |
| DAT_MOSI0 | 204 | `PINMUX_GPIO204__FUNC_AUD_DAT_MOSI0` |
| DAT_MOSI1 | 205 | `PINMUX_GPIO205__FUNC_AUD_DAT_MOSI1` |
| **DAT_MOSI2 (CH34, 关键)** | **206** | `PINMUX_GPIO206__FUNC_AUD_DAT_MOSI2` |
| DAT_MISO0 | 209 | `PINMUX_GPIO209__FUNC_AUD_DAT_MISO0` |
| DAT_MISO1 | 210 | `PINMUX_GPIO210__FUNC_AUD_DAT_MISO1` |

### 1.8 音频电源域与时钟

- AFE 基址 `0x1e100000`，长度 `0x2000`，IRQ `GIC_SPI 416`（`mt6895.dtsi:2523-2527`）
- 电源域：`MT6895_POWER_DOMAIN_AUDIO`（`mt6895.dtsi:2530`），是 `ADSP_INFRA` 的子域（`mtk-scpsys-mt6895.c:575`）
- **MT6685 audclk 提供 26MHz CLKSQ**，这是整个 MT6368 codec 数字域（HP out、mic in）的时钟源（`mt6895.dtsi:2452-2460`）：
  > `MT6685 companion PMIC (SPMI USID 9, same bus as MT6363@4 / MT6368@5). Only the audclk child is instantiated: its XO_BBCK5 DCXO buffer supplies the 26 MHz CLKSQ that clocks the whole MT6368 codec digital domain (HP out, mic in).`

- AFE 寄存器偏移（`mt6895-reg.h`）：
  - `AFE_DAC_CON0 = 0x10`
  - `AFE_CONN3 = 0x2c`（CH1 mix）、`AFE_CONN4 = 0x30`（CH2）
  - `AFE_DL1_BASE/CUR/END = 0x54/0x5c/0x64`
  - `AFE_ADDA_DL_SRC2_CON0 = 0x108`
  - `AFE_CONN21 = 0x470`、`AFE_CONN22 = 0x474`（UL）
  - `AFE_CONN52 = 0xed0`（CH3 mix）、`AFE_CONN53 = 0xed4`（CH4）
  - `I_DL1_CH1 = 5`（`mt6895-interconnection.h:17`）

- **MTKAIF 协议**：xaga 固定用 `MTKAIF_PROTOCOL_2_CLK_P2`（`mt6895-mt6368.c:439-442`）

### 1.9 机器驱动的 probe 顺序陷阱

`mt6895-mt6368.c:446-459`：

```c
/* disable ext amp connection */
snd_soc_dapm_disable_pin(dapm, EXT_SPK_AMP_W_NAME);
#if IS_ENABLED(CONFIG_SND_SOC_MT6368_ACCDET)
	/*
	 * Defer card bind until the accdet driver has probed, so that
	 * "Headset Jack" is registered together with the card instead of
	 * being silently skipped when we lose the probe race.
	 */
	if (!mt6368_accdet_ready())
		return -EPROBE_DEFER;
	mt6368_accdet_init(codec_component, rtd->card);
#endif
```

**这是个真实的踩坑**：如果 accdet 没 ready 就建卡，jack 会静默消失（不报错），耳机插拔就没反应。

---

## 2. ★ GPU / GPUEB

### 2.1 GPUEB 驱动的完整上电序列

**先说结论：GPUEB 驱动本身几乎不做上电，它只是「传话」。真正的上电在 scpsys + mfgcfg + panthor 三处配合。**

#### (a) probe 阶段（`gpueb_init.c:135-215`）

```c
gpueb_ipi_init(pdev);            // 建 mbox/pin 表，注册 IPI 设备
gpueb_reserved_mem_init(pdev);   // 拿 MEM_ID_GPUFREQ / MEM_ID_LOG 的物理地址
// gpueb_plat_service_init() 被注释掉了（init.c:167-171）
gpueb_logger_init(...)           // 仅当 gpueb-logger-support=1；xaga 是 0，跳过
gpueb_hw_voter_dbg_init();
gpueb_debug_init(pdev);
gpueb_timesync_init();
```

注意 xaga dts 里 `gpueb-logger-support = <0>`（`mt6895.dtsi:2653`），所以 logger 整条路径不走。

#### (b) 真正让 GPU 上电的调用点：三处

**① panthor probe 里，接触任何 GPU 寄存器之前**（`panthor_device.c:257-269`）：

```c
#if IS_ENABLED(CONFIG_MTK_GPUEB)
	/* MT6895: gpueb owns GPU power. Ask it to power on before touching any
	 * GPU registers, otherwise panthor reads all-zero ID/features.
	 */
	if (of_machine_is_compatible("mediatek,mt6895") && mt6895_gpueb_available()) {
		ret = mt6895_gpueb_power_on();
		...
	}
#endif
```

**注释里的因果非常明确**：不先上电，panthor 读到的 ID/features 全是 0。

**② panthor fw start（MCU 启动）前**（`panthor_fw.c:1131-1141`）—— 再要一次电，然后才写 `MCU_CONTROL = MCU_CONTROL_AUTO`。

**③ 整卡 probe 完成后，禁止 runtime PM**（`panthor_device.c:317-323`）：

```c
	/* Bring-up workaround for MT6895: keep panthor runtime-resumed while
	 * gpueb is managing the GPU power state. Autosuspend/resume cycles can
	 * leave the EB and Mali firmware out of sync and cause CSG timeouts.
	 */
	pm_runtime_forbid(ptdev->base.dev);
	pr_info("XAGA-PANTHOR: GPU runtime PM forbidden\n");
```

**这是一条极重要的实战经验**：GPUEB 管电时，panthor 不能 autosuspend，否则 CSG 超时。

#### (c) `mt6895_gpueb_power_on()` 内部做了什么（`gpueb_power.c:150-186`）

```c
mt6895_gpueb_power_control(power_on=1):
  1. mutex_lock
  2. gpueb_power_init_locked():
       a. 若 !gpueb_ipidev.ipi_inited → -EPROBE_DEFER
       b. channel = gpueb_get_send_PIN_ID_by_name("IPI_ID_GPUFREQ")   // = 1
       c. mtk_ipi_register(&gpueb_ipidev, channel, NULL, NULL, &recv_msg)
       d. shared_mem_pa   = MEM_ID_GPUFREQ 物理地址
          shared_mem_size = MEM_ID_GPUFREQ 大小 (0x1000)
       e. msg.cmd_id = CMD_INIT_SHARED_MEM (0)
          msg.u.addr.status_base = shared_mem_pa
          msg.u.addr.status_size = GPUFREQ_STATUS_MEM_SZ (0x400)
          msg.u.addr.debug_base  = shared_mem_pa + 0x400
          msg.u.addr.debug_size  = shared_mem_size - 0x400
       f. mtk_ipi_send_compl(..., IPI_SEND_POLLING, ..., IPI_TIMEOUT_MS=10000)
       g. debugfs_create_file("gpueb_gpufreq_status", ...)
       h. pr_info("XAGA-GPUEB: CMD_INIT_SHARED_MEM done")
  3. msg.cmd_id = CMD_POWER_CONTROL (6)
     msg.u.power_state = POWER_ON (1)
     mtk_ipi_send_compl(..., IPI_SEND_POLLING, ..., 10000ms)
  4. if recv_msg.u.return_value < 0 → 返回该错误
  5. pr_info("XAGA-GPUEB: power_control(1) ret=%d")
```

**关键注释**（`gpueb_power.c:168-172`）：

> `Downstream gpufreq_v2 sends ONLY CMD_POWER_CONTROL here when the EB owns DVFS; the firmware restores its own boot OPP internally. Never commit OPP indexes from this path -- they are working-table indexes and belong to the DVFS client via mt6895_gpueb_commit().`

**这条很重要**：上电路径**只能**发 `CMD_POWER_CONTROL`，绝对不能顺手 commit OPP index。

#### (d) 上电时序的真实顺序（跨文件拼出来的）

```
1. clk_mt6895_mfgcfg_probe()  [arch_initcall]
     mt6895_mfg_pdc_enable(base)      ← 开机时先开一次 PDC
     mt6895_mfg_hwdcm_enable(base)
     mt6895_mfg_acp_enable(base)
   注：此时 MFG1 电源域可能还没上，写在 MFG_TOP 的寄存器可能丢失
       （mfgcfg.c:173-181 注释明确说了这点）

2. scpsys 上 MFG1..MFG12（软件逐个上电，见 2.4）

3. panthor probe → mt6895_gpueb_power_on()
     → CMD_INIT_SHARED_MEM → CMD_POWER_CONTROL(POWER_ON)

4. panthor → panthor_hw_init → panthor_pwr_init → panthor_gpu_init
     → panthor_fw_init → panthor_fw_start → 再 mt6895_gpueb_power_on()
     → gpu_write(MCU_CONTROL, MCU_CONTROL_AUTO)
     → 等 booted（1000ms 超时）

5. mt6895_mfg_bringup_enable()  [导出但本树内无调用者！]
     注释说必须在 MFG1 上电之后重放 AOC/ACP/HWDCM/GPM
```

**⚠ 未确认点**：`mt6895_mfg_bringup_enable()` 和 `mt6895_mfg_pdc_enable_now()` 在本树里**只有定义和 EXPORT_SYMBOL，没有任何调用者**（我全仓 grep 过：只有 `clk-mt6895-mfgcfg.c:18,19,173,230,236,249`）。可能是给 out-of-tree 模块用的，或者调用点在某个我没读到的路径。**这两个函数目前是死代码，但对 HakiOS 仍然极有价值**（见 2.2 的寄存器表）。

### 2.2 ★★★ MFG_TOP 的寄存器级上电操作（`clk-mt6895-mfgcfg.c`）

**这是 GPUEB/GPU 部分最硬核的内容，全部是直接寄存器操作，可以直接搬到 XNU。**

#### MFG_TOP 基址
- `MFG_TOP = 0x13fbf000`（`clk-mt6895-mfgcfg.c:180`、`:240`）
- `MFG_RPC = 0x13f90000`（`clk-mt6895-mfgcfg.c:104`，直接 ioremap，注释说不走 DT 因为"early boot 时 DT resource translation 不可靠"）
- `SLEEP = 0x1c001000`（`clk-mt6895-mfgcfg.c:184`）

#### (a) PDCv2 使能（`mt6895_mfg_pdc_enable`，`mfgcfg.c:55-78`）

```c
/* 使能：这些寄存器的 bit0 置 1 */
en_regs[] = { 0x400, 0x418, 0x430, 0x448, 0x460, 0x478,
              0x490, 0x4a8, 0x4c0, 0x4d8,
              0x100, 0x120, 0x140, 0x118, 0x0c0, 0x098, 0x1c0 };
    val = readl(base + reg); val |= BIT(0); writel(val, base + reg);

/* 保留：这些寄存器的 bit31 置 1 */
rsv_regs[] = { 0x404, 0x41c, 0x434, 0x44c, 0x464, 0x47c,
               0x494, 0x4ac, 0x4c4, 0x4dc };
    val = readl(base + reg); val |= BIT(31); writel(val, base + reg);
```

#### (b) ⚠ PDC 的陷阱（`mfgcfg.c:196-204`，**XAGA EXPERIMENT 注释**）

> ```
> /* XAGA EXPERIMENT: do NOT enable PDCv2 here. With PDC enabled, the GPU
>  * takes over MFG4-12 and powers them off because no shader power request
>  * is active yet, leaving the domains stuck in 0x100d (requested but no
>  * PWR_ACK). Keep them software-powered via scpsys instead.
>  */
> /* mt6895_mfg_pdc_enable(base); */
> ```

**这是最值钱的一条 GPU 经验**：PDC 一开，GPU 立刻接管 MFG4-12 并因为「还没有 shader 电源请求」把它们关掉，于是卡在 `0x100d`（已请求但无 PWR_ACK）。**解决方法是让 scpsys 软件管这些域，不要在 bringup 早期开 PDC。**

配套的 `mt6895_mfg_pdc_enable_now()`（`mfgcfg.c:236-249`）注释说明了正确时机：
> `Enable PDCv2 after the shader power request has been asserted, so the GPU power state machine can complete the shader MTCMOS handshake without the PDC immediately powering the domains off (no request was active yet).`

#### (c) HWDCM 使能（`mt6895_mfg_hwdcm_enable`, `mfgcfg.c:80-124`）

```c
top + 0x10 : |= BIT(0)|BIT(1)|BIT(2)|BIT(3)|BIT(4)|BIT(5); &= ~BIT(6); |= BIT(15);
top + 0x20 : |= BIT(23)|BIT(25);
top + 0xb0 : &= ~BIT(8); &= ~BIT(10); |= BIT(13)|BIT(14)|BIT(17)|BIT(18); &= ~BIT(21);
rpc + 0x1034 : &= ~BIT(0);
```

#### (d) ACP 使能（`mt6895_mfg_acp_enable`, `mfgcfg.c:126-165`）

```c
top + 0x168 : |= BIT(0)|BIT(1)|BIT(2)|BIT(3);
top + 0x8e0 : |= 0x855;      top + 0x8e8 : |= 0x855;
top + 0x910 : |= 0x855;      top + 0x918 : |= 0x855;
top + 0x900 : |= 0x055;      top + 0x908 : |= 0x055;
top + 0x920 : |= 0x055;      top + 0x928 : |= 0x055;
```

#### (e) ★ GPM1.0 — di/dt 抑制 / MFG I2M protector（`mfgcfg.c:167-189`）

```c
writel(0x20300316, base + 0xF60);
writel(0x1800000C, base + 0xF64);
writel(0x01010802, base + 0xF68);
writel(0x000227F3, base + 0xFA8);
udelay(1);
writel(0x20300317, base + 0xF60);     ← 注意最后一位从 6 变 7（触发提交）
```

注释（`mfgcfg.c:167-169`）：
> `GPM1.0: di/dt reduction / MFG I2M protector. Downstream enables this by default (g_gpm_enable=true) and programs it on every GPU power-on.`

#### (f) ★★ AOC2.0 — 释放 VGPU SRAM 隔离（`mfgcfg.c:183-196`）

**这段是带 SPM 硬件信号量的忙等，非常关键**：

```c
sleep = ioremap(0x1c001000, 0x1000);
/* 等 SPM 信号量 */
do {
    val = readl(sleep + 0x6ac);
    val |= BIT(0);
    writel(val, sleep + 0x6ac);
} while ((readl(sleep + 0x6ac) & BIT(0)) != BIT(0));

/* 清掉 bit9/bit10（释放 SRAM isolation） */
val = readl(sleep + 0xf30);
val &= ~(BIT(9) | BIT(10));
writel(val, sleep + 0xf30);

/* 再敲一次信号量 */
val = readl(sleep + 0x6ac);
val |= BIT(0);
writel(val, sleep + 0x6ac);
```

#### (g) bringup 尾声的清零（`mfgcfg.c:210-214`）

```c
/* Downstream gpufreq also restores MFG_DUMMY_REG and MFG_SRAM_FUL_SEL_ULV
 * after power-on. On a cold boot the saved values are zero, so write zeros
 * to match the kbase cold-boot path. */
writel(0, base + 0x500);    /* MFG_DUMMY_REG */
writel(0, base + 0x80);     /* MFG_SRAM_FUL_SEL_ULV */
```

### 2.3 IPI 协议（消息结构、超时、返回码）

#### 消息结构（`gpueb/include/gpufreq_ipi.h:46-78`）

```c
struct gpufreq_ipi_data {
	enum gpufreq_ipi_cmd cmd_id;      // u32
	unsigned int target;              // u32
	union {
		int oppidx;
		int return_value;
		unsigned int freq;
		unsigned int volt;
		unsigned int power;
		unsigned int power_state;
		unsigned int mode;
		struct { unsigned long long status_base;   // ← 8 字节，注意对齐
		         unsigned long long debug_base;
		         unsigned int status_size;
		         unsigned int debug_size; } addr;
		struct { unsigned int freq; unsigned int volt; } custom;
		struct { unsigned int limiter; int ceiling_info; int floor_info; } setlimit;
	} u;
};
```

传输长度：`GPUFREQ_IPI_DATA_LEN = sizeof(struct gpufreq_ipi_data) / sizeof(unsigned int)`（`gpufreq_ipi.h:11-12`）

#### 命令码全表（`gpufreq_ipi.h:14-36`）

```
CMD_INIT_SHARED_MEM       = 0     ← 上电必发
CMD_GET_FREQ_BY_IDX       = 1
CMD_GET_POWER_BY_IDX      = 2
CMD_GET_OPPIDX_BY_FREQ    = 3
CMD_GET_LEAKAGE_POWER     = 4
CMD_SET_LIMIT             = 5
CMD_POWER_CONTROL         = 6     ← 上电必发
CMD_COMMIT                = 7     ← DVFS 提交
CMD_GET_DEBUG_OPP_INFO    = 8
CMD_GET_DEBUG_LIMIT_INFO  = 9
CMD_GET_WORKING_TABLE     = 10
CMD_GET_SIGNED_TABLE      = 11
CMD_GET_LIMIT_TABLE       = 12
CMD_SWITCH_LIMIT          = 13
CMD_FIX_TARGET_OPPIDX     = 14
CMD_FIX_CUSTOM_FREQ_VOLT  = 15
CMD_SET_STRESS_TEST       = 16
CMD_SET_AGING_MODE        = 17
CMD_SET_GPM_MODE          = 18
CMD_SET_TEST_MODE         = 19
CMD_NUM                   = 20
```

#### target 与 power state（`gpufreq_ipi.h:38-44`, `:46-50`）

```
TARGET_DEFAULT = 0, TARGET_GPU = 1, TARGET_STACK = 2, TARGET_INVALID = 3
POWER_OFF = 0, POWER_ON = 1
```

#### 超时与返回码

- `IPI_TIMEOUT_MS = 10000U`（`gpueb/include/gpueb_ipi.h:11`）
- 传输 mode：`IPI_SEND_POLLING`（`gpueb_power.c:116, 139, 162`）
- 返回值判断：`ret != IPI_ACTION_DONE && ret != IPI_DUPLEX` → 失败（`gpueb_power.c:96`）
- **固件返回码走共享内存**，不是 IPI 返回值：`gpueb_power_recv_msg.u.return_value < 0` 才算失败（`gpueb_power.c:147-149`, `:165-168`）

#### mbox 硬件配置（`mt6895.dtsi:2650-2715`）

```
mbox_count = <1>      只有一个 mailbox
mbox_size  = <160>    160 个 slot
slot_size  = <4>      每 slot 4 字节
ts_mbox    = <0>      timersync 用 mbox0

reg = <0x13c40000 0x22000>,   "gpueb_base"
      <0x13c5fd80 0x280>,     "mbox0_base"
      <0x13c62004 0x4>,       "mbox0_set"
      <0x13c62074 0x4>,       "mbox0_clr"
      <0x13c62000 0x4>,       "mbox0_send"
      <0x13c62078 0x4>;       "mbox0_recv"

interrupts = <GIC_SPI 516>;  "mbox0"
```

#### send/recv 表（`mt6895.dtsi:2659-2699`）

| ID | 名字 | send_size | recv_size | recv_opt |
|---|---|---|---|---|
| 0 | `IPI_ID_FAST_DVFS_EVENT` | 4 | 4 | 0 |
| **1** | **`IPI_ID_GPUFREQ`** | **8** | **8** | **1** |
| 2 | `IPI_ID_SLEEP` | 3 | 1 | 0 |
| 3 | `IPI_ID_TIMER` | 3 | 1 | 0 |
| 4 | `IPI_ID_FHCTL` | 9 | 1 | 1 |
| 5 | `IPI_ID_CCF` | 4 | 4 | 1 |
| 6 | `IPI_ID_GPUMPU` | 6 | 1 | 1 |
| 7 | `IPI_ID_FAST_DVFS` | 6 | 6 | 1 |

**xaga 实际只用 ID=1（GPUFREQ）。**

#### ★ 共享内存布局（`mt6895.dtsi:2711-2714` + `gpueb_power.c:97-101`）

```
gpueb_mem_table:
  <0 0x1000>     MEM_ID_GPUFREQ   ← 4 KiB
  <1 0x180000>   MEM_ID_LOG       ← 1.5 MiB

MEM_ID_GPUFREQ 内部分区（gpueb_power.c:107-110）：
  [0x000 .. 0x400)  struct gpufreq_shared_status   (status_base, status_size=0x400)
  [0x400 .. 0x1000) debug 区                       (debug_base, debug_size=0xC00)
```

**`struct gpufreq_shared_status` 的 32 个字段（顺序即偏移）**，从 `gpueb_power.c:40-52` 的 debugfs 名字表逐字抄：

| idx | 字段 | idx | 字段 |
|---|---|---|---|
| 0 | `cur_oppidx_gpu` | 16 | `cur_ceiling_gpu` |
| 1 | `cur_oppidx_stack` | 17 | `cur_floor_gpu` |
| 2 | `opp_num_gpu` | 18 | `cur_ceiling_stack` |
| 3 | `opp_num_stack` | 19 | `cur_floor_stack` |
| 4 | `cur_fgpu` | 20 | `cur_c_limiter_gpu` |
| 5 | `cur_fstack` | 21 | `cur_f_limiter_gpu` |
| 6 | `cur_vgpu` | 22 | `cur_c_limiter_stack` |
| 7 | `cur_vstack` | 23 | `cur_f_limiter_stack` |
| 8 | `cur_vsram_gpu` | 24 | `power_control` |
| 9 | `cur_vsram_stack` | 25 | `dvfs_state` |
| 10 | `cur_power_gpu` | 26 | `shader_present` |
| 11 | `cur_power_stack` | 27 | `power_count` |
| 12 | `max_power_gpu` | 28 | `aging_enable` |
| 13 | `max_power_stack` | 29 | `avs_enable` |
| 14 | `min_power_gpu` | 30 | `sb_version` |
| 15 | `min_power_stack` | 31 | `ptp_version` |

（`BUILD_BUG_ON(ARRAY_SIZE(names) != 32)` 在 `gpueb_power.c:56` 硬性保证是 32 个 `u32`。）

#### 数据表描述符

```
gpueb_mem_name_table = "MEM_ID_GPUFREQ", "MEM_ID_LOG";
```

`MEM_ID_GPUFREQ` 在**哪里**的物理地址来自 reserved-memory。xaga dts 里有明确提示（`mt6895-xiaomi-xaga.dts:617-620`）：
> `GPUEB shared memory: the live FDT reserves 2MB at 0x7c600000 as mblock-38-me_GPUEB_SHARED. gpueb_reserved_mem.c can use it directly when gpueb_mem_addr/size are present on the gpueb node.`

### 2.4 panthor 对 mt6895 的改动

#### (a) 兼容串与 IRQ（`mt6895.dtsi:2717-2741`）

```dts
compatible = "arm,mali-valhall-csf";   // G610 = arch 10 / CSF
reg = <0x13000000 0x480000>;
interrupts = <GIC_SPI 514>, <GIC_SPI 513>, <GIC_SPI 512>;
interrupt-names = "job", "mmu", "gpu";
```

**注释说明**（`mt6895.dtsi:2718-2726`）：
> stock DT 用 `"mediatek,mali","arm,mali-valhall"`，**不匹配任何 mainline 驱动**。panthor 绑 `"arm,mali-valhall-csf"`。stock 有 5 个 IRQ（大写 GPU/MMU/JOB/EVENT/PWR），但 panthor 用 `platform_get_irq_byname()` 找小写 `"gpu"/"mmu"/"job"`，所以这里**重新大小写**，编号取自 merged stock DT。

#### (b) ★ 三个时钟（`mt6895.dtsi:2734-2760`）

```dts
clocks = <&topckgen CLK_TOP_MFG_SEL_0_CK>,   // "core"
         <&mfgcfg CLK_MFGCFG_BG3D>,          // "coregroup"
         <&topckgen CLK_TOP_MFG_SEL_1_CK>;   // "stacks"
clock-names = "core", "coregroup", "stacks";
```

**注释（`mt6895.dtsi:2741-2756`）逐条拆解**：

- `core` 来自 **MFG_SEL_0**（MFGPLL / 231M ref）
- `stacks` 来自 **MFG_SEL_1**（MFGSCPLL / 218M ref），downstream gpufreq 让它跟 GPU OPP 一起缩放
- **`coregroup` 是 MFGCFG 的 BG3D gate**（downstream 的 `subsys_mfg_cg`）：一个「hw-on 但无 Linux consumer」的 gate，所以 `clk_disable_unused()` 会在 GPU 运行时把它关掉，导致 external abort 冒成 **SError 出现在 `panthor_fw_ping_work()`**
- panthor 在 `panthor_device_resume()` 开它、`suspend()` 关它
- **binding 固定名字顺序为 core, coregroup, stacks**

#### (c) ★★ MFG1..MFG12 十二个电源域（`mt6895.dtsi:2728-2732`）

> `The MT6895 GPU MTCMOS is split into MFG1..MFG12. Downstream attaches all twelve domains to the GPU node and powers them through PM runtime; the PDCv2 registers then let the GPU control the finer shader/stack domains. **Without MFG2-12 the shader cores never become ready (shader_ready stays 0) and the MCU firmware hangs before writing the shared interface version.**`

**这是 GPU 能不能起来的命门**：少了 MFG2-12，shader core 永远不 ready。

但注意 —— 本树最终的 gpu 节点里 **`power-domains` 属性是不存在的**（`mt6895.dtsi:2745` 注释："Real power management stays with gpueb; no power-domains."）。所以 MFG2-12 是靠 scpsys 自动上电（`MTK_SCPD_BYPASS_INIT_ON`），不是靠 gpu 节点挂。

#### (d) VSTACK 供电（`mt6895.dtsi:2761-2766`）

```dts
vstack-supply = <&mt6368_vbuck2>;   // MT6368 BUCK2
operating-points-v2 = <&gpu_opp_table>;
mali-supply = <&vdd_gpu>;           // 固定 stub
```

#### (e) devfreq 双路径（`panthor_devfreq.c`）

**核心设计**：优先让 GPUEB 全权做 DVFS，失败才退回 CCF+regulator。

`xaga_eb_mode()`（`panthor_devfreq.c:129-132`）：
```c
return !xaga_eb_failed && mt6895_gpueb_available();
```

`panthor_devfreq_target()`（`panthor_devfreq.c:234-296`）：
```c
if (xaga_eb_mode())
	return xaga_eb_commit(ptdev, *freq);    // ← 走 EB
/* 否则：dev_pm_opp_set_rate + 手动拉 stacks 时钟/VSTACK 电压 */
```

**`xaga_eb_commit()` 的顺序策略**（`panthor_devfreq.c:197-229`）：

```c
bool up = xaga_eb_last_gpu_idx >= 0 && gidx > xaga_eb_last_gpu_idx;

/* Mirror downstream ordering: the core domain leads on the way up
 * and trails on the way down. */
if (up)
	ret = mt6895_gpueb_commit(TARGET_GPU, gidx);      // 升频：先 GPU
else
	ret = mt6895_gpueb_commit(TARGET_STACK, sidx);    // 降频：先 STACK
if (!ret)
	ret = up ? mt6895_gpueb_commit(TARGET_STACK, sidx)
	         : mt6895_gpueb_commit(TARGET_GPU, gidx);
```

**注释 `panthor_devfreq.c:105-115`**（非常重要）：
> `XAGA: GPUEB-owned DVFS. The firmware's working tables for this silicon (GPU SegmentID 0 / STACK SegmentID 2, AVS-trimmed) were captured live from /proc/gpufreqv2/ on a stock device. **Indexes sent with CMD_COMMIT are indexes into THESE tables, not into the DT OPP table** -- the EB then applies both PLLs, both bucks and its private VSRAM rails itself.`

#### (f) ★★★ GPU 工作频率表（EB 用，**索引即命令参数**）

`xaga_eb_gpu_freq[]`（`panthor_devfreq.c:110-112`）—— 降序：
```
idx 0: 950 MHz      idx 3: 610 MHz
idx 1: 880 MHz      idx 4: 430 MHz
idx 2: 800 MHz      idx 5: 350 MHz
```

`xaga_eb_stack_freq[]`（`panthor_devfreq.c:113-119`）—— 降序，40 项：
```
852, 845, 837, 830, 822, 815, 807, 800, 773, 747,
721, 695, 668, 642, 616, 590, 572, 555, 538, 521,
504, 487, 470, 453, 436, 419, 402, 385, 368, 351,
334, 317, 304, 292, 280, 268, 255, 243, 231, 219  (MHz)
```

#### (g) ★★ STACK 频率-电压配对表（`panthor_devfreq.c:41-87`）

40 项，`rate → volt_uv`。摘关键锚点：

```
219 MHz → 500000 uV        487 MHz → 612500 uV        721 MHz → 681250 uV
368 MHz → 568750 uV        590 MHz → 650000 uV        800 MHz → 700000 uV
419 MHz → 587500 uV        695 MHz → 675000 uV        852 MHz → 743750 uV
                                                       860 MHz → 750000 uV
```

电压步进规律：从 219MHz/500mV 起，每档 +1250~6250 uV 不等，整体近似 **0.5V @ 219MHz → 0.75V @ 860MHz**。

**`stack_max_rate` 模块参数**（`panthor_devfreq.c:26-33`）：
```c
static unsigned int stack_max_rate = 852000000;   // 默认封顶 852MHz
module_param(stack_max_rate, uint, 0644);
```
注释解释为什么：本机 STACK 工作表顶到 852MHz（SegmentID 2），EB 自己做 AVS trim + VSRAM 时序，所以全表可安全暴露。
（而上面更早的一段注释 `panthor_devfreq.c:21-25` 说：**没有 regulator 控制时不能超过 bootloader 留下的 368MHz 电压包络**——这是早期阶段的话，后来有了 EB 才放宽。）

#### (h) 非 EB 路径的 VSTACK 手动缩放（`panthor_devfreq.c:266-295`）

```c
stack_rate = min(*freq, stack_max_rate);
uv = xaga_stack_volt(stack_rate);
up = stack_rate > cur_stack;
/* 升频：先抬电压，抬不动就不抬频率 */
if (up && have_vstack && regulator_set_voltage(vstack, uv, uv)) {
	dev_warn("vstack raise to %u uV failed, stacks stay %lu Hz\n", ...);
} else {
	clk_set_rate(ptdev->clks.stacks, stack_rate);
	if (!up && have_vstack) regulator_set_voltage(vstack, uv, uv);  // 降频：后降电压
}
```

**"电压领先上升、滞后下降"** 是这里的核心原则。

#### (i) 一处 devfreq 细节（`panthor_devfreq.c:315-320`）

```c
/* XAGA: with the GPUEB owning the clocks, clk_get_rate() returns a
 * stale CCF cache; the tracked value is authoritative for both paths. */
```

**即：EB 模式下不能用 `clk_get_rate()` 判断当前频率，CCF 缓存是过期的。**

#### (j) G610 的 job 超时放宽

`git log` 里有一条 `39644dd7d drm/panthor: allow 60s for jobs on G610`。

### 2.5 电源域（scpsys）

**文件**：`drivers/soc/mediatek/mtk-scpsys-mt6895.c`（704 行）

#### MFG 域层级（`mtk-scpsys-mt6895.c:597-607`）

```
MFG1 ─→ MFG2 ─┬─→ MFG3 ─→ MFG7 ─→ MFG9
              ├─→ MFG4 ─→ MFG8
              ├─→ MFG5 ─→ MFG10 ─→ MFG12
              └─→ MFG6 ─→ MFG11
```

#### 每个 MFG 域的寄存器（`mtk-scpsys-mt6895.c:462-568`）

| 域 | ctl_offs | sta_mask | sram_pdn_bits | ack_bits | caps |
|---|---|---|---|---|---|
| mfg1 | `0xEBC` | GENMASK(31,30) | GENMASK(8,8) | GENMASK(12,12) | `IS_PWR_CON_ON` |
| mfg2 | `0xEC0` | 同上 | 同上 | 同上 | `IS_PWR_CON_ON \| BYPASS_INIT_ON` |
| mfg3 | `0xEC4` | 〃 | 〃 | 〃 | 同上 |
| mfg4 | `0xEC8` | 〃 | 〃 | 〃 | 同上 |
| mfg5 | `0xECC` | 〃 | 〃 | 〃 | 同上 |
| mfg6 | `0xED0` | 〃 | 〃 | 〃 | 同上 |
| mfg7 | `0xED4` | 〃 | 〃 | 〃 | 同上 |
| mfg8 | `0xED8` | 〃 | 〃 | 〃 | 同上 |
| mfg9 | `0xEDC` | 〃 | 〃 | 〃 | 同上 |
| mfg10 | `0xEE0` | 〃 | 〃 | 〃 | 同上 |
| mfg11 | `0xEE4` | 〃 | 〃 | 〃 | 同上 |
| mfg12 | `0xEE8` | 〃 | 〃 | 〃 | 同上 |

**注意：mfg1 是唯一没有 `BYPASS_INIT_ON` 的** —— 它由 scpsys 正常管理，其余 11 个在 init 时就旁路上电。

#### mfg1 的 bus protection（`mtk-scpsys-mt6895.c:471-478`）

```c
BUS_PROT_IGN(IFR_TYPE,    0x0CA4, 0x0CA8, 0x0CA0, 0x0CAC, MT6895_TOP_AXI_PROT_EN_MD0_MFG1),
BUS_PROT_IGN(MFGRPC_TYPE, 0x0040, 0x0044, 0x0040, 0x0048, MT6895_MFGRPC_AXI_PROT_EN_MFG1),
BUS_PROT_IGN(IFR_TYPE,    0x0C64, 0x0C68, 0x0C60, 0x0C6C, MT6895_TOP_AXI_PROT_EN_EMISYS0_MFG1),
BUS_PROT_IGN(IFR_TYPE,    0x0C74, 0x0C78, 0x0C70, 0x0C7C, MT6895_TOP_AXI_PROT_EN_EMISYS1_MFG1),
```

#### 全局状态寄存器（`mtk-scpsys-mt6895.c:610-614`）

```c
.pwr_sta_offs   = 0xF34,
.pwr_sta2nd_offs = 0xF38
```

`0x10006000` 是 SPM 基址（`mt6895.dtsi:1094`）。

#### 其他关键域（供 XNU 参考）

- `MM_INFRA` 是显示/相机/编解码的总父域（`mtk-scpsys-mt6895.c:583-592`）
- `MM_INFRA → DISP`、`MM_INFRA → DISP1`（`:591-592`）
- `MM_INFRA → ISP_VCORE → ISP_MAIN → {ISP_DIP1, ISP_IPE}`（`:577-579`）
- `ADSP_INFRA → AUDIO`（`:575`）
- `DISP → VEN0`、`DISP → VEN1`（`:583-584`）

### 2.6 时钟树（MFG 相关）

#### MFGPLL / MFGSCPLL 基址（`clk-mt6895.c:296-303`）

```
MFGPLL_CON0   = 0x008       MFGSCPLL_CON0   = 0x008
MFGPLL_CON1   = 0x00C       MFGSCPLL_CON1   = 0x00C
MFGPLL_CON2   = 0x010       MFGSCPLL_CON2   = 0x010
MFGPLL_CON3   = 0x014       MFGSCPLL_CON3   = 0x014
```

两个 PLL 的 syscon 节点（`mt6895.dtsi:1141-1151`）：
```
mfgpll_pll_ctrl:   syscon@13fa0000    "mediatek,mt6895-mfgpll_pll_ctrl"
mfgscpll_pll_ctrl: syscon@13fa0c00    "mediatek,mt6895-mfgscpll_pll_ctrl"
mfgrpc:            syscon@13f91000    "mediatek,mt6895-gpu_eb_rpc_bus"
mfgcfg:            syscon@13fbf000    "mediatek,mt6895-mfg"
```

#### ★★ MFG PLL 的 VCO 下限陷阱（`clk-mt6895.c:2721-2727`）

```c
/*
 * MT6895's MFGPLL/MFGSCPLL use a higher VCO floor than the generic
 * MT6895_PLL_FMIN. The generic 1500MHz floor makes mtk_pll_calc_values()
 * pick postdiv=2 for 852MHz (VCO=1704MHz), which does not lock. With the
 * downstream 750MHz-950MHz/posdiv4 range we need postdiv=4 (VCO=3408MHz),
 * so use a 2000MHz floor for the two GPU PLLs.
 */
#define MT6895_MFG_PLL_FMIN	(2000UL * MHZ)
```

**这是一条非常具体的硬件事实**：
- 通用 `MT6895_PLL_FMIN = 1500 MHz`（`clk-mt6895.c:2684`），`FMAX = 3800 MHz`（`:2683`）
- 但 GPU 的两个 PLL 必须用 **2000 MHz** 下限，否则 852MHz 会算出 postdiv=2（VCO=1704MHz）→ **PLL 锁不住**
- 正确做法：postdiv=4 → VCO=3408MHz

#### MFG mux（`clk-mt6895.c:2176-2183`）

```c
CLK_CFG_30 /* 寄存器 */, lsb=16, width=1  →  "mfg_sel_0_sel"   (parents: mfg_ref_ck, mfgpll_ck)
CLK_CFG_30 /* 寄存器 */, lsb=17, width=1  →  "mfg_sel_1_sel"   (parents: mfgsc_ref_ck, mfgscpll_ck)
```

父表（`clk-mt6895.c:1786-1795`）：
```c
mfg_sel_0_parents[] = { "mfg_ref_ck",  "mfgpll_ck"    };
mfg_sel_1_parents[] = { "mfgsc_ref_ck","mfgscpll_ck"  };
```

分频（`clk-mt6895.c:729-732`）：
```c
FACTOR(CLK_TOP_MFG_SEL_0_CK, "mfg_sel_0_ck", "mfg_sel_0_sel", 1, 1)
FACTOR(CLK_TOP_MFG_SEL_1_CK, "mfg_sel_1_ck", "mfg_sel_1_sel", 1, 1)
```

#### mfgcfg gate（`clk-mt6895-mfgcfg.c:38-44`）

```c
static const struct mtk_gate_regs mfgcfg_cg_regs = {
	.set_ofs = 0x4,   .clr_ofs = 0x8,   .sta_ofs = 0x0,
};
GATE_MFGCFG(CLK_MFGCFG_BG3D, "mfgcfg_bg3d", "mfg_ref_ck", 0)   // shift 0
```

**这就是 `coregroup` 时钟**：MFG_TOP 偏移 0x0（状态）/0x4（置位）/0x8（清除），bit 0。

#### 时钟驱动文件清单

| 文件 | 行数 | 内容 |
|---|---|---|
| `clk-mt6895.c` | 3160 | topckgen（含所有 PLL 和 mux） |
| `clk-mt6895-mfgcfg.c` | 309 | MFG gate + MFG_TOP bringup |
| `clk-mt6895-afe.c` | 134 | AFE gate |
| `clk-mt6895-cam.c` | 488 | camera |
| `clk-mt6895-img.c` | 327 | imgsys |
| `clk-mt6895-mdpsys.c` | 238 | mdp |
| `clk-mt6895-mminfra.c` | 88 | mminfra |
| `clk-mt6895-mmsys0.c` | 121 | mmsys0（显示） |
| `clk-mt6895-mmsys1.c` | 120 | mmsys1 |
| `clk-mt6895-peri-ao.c` | 110 | peri-ao |
| `clk-mt6895-vcodec.c` | 314 | 编解码 |

---

## 3. 显示（DRM / CMDQ / IOMMU / panel）

### 3.1 显示栈组成

**两个 DRM 驱动并存**：
- `drivers/gpu/drm/mediatek/mediatek_v2/` —— **实际绑定** `dispsys_config@14000000`，133 个文件（下游 MediaTek DRM 主干）
- `drivers/gpu/drm/mediatek/`（老 mainline 栈）—— 只在部分路径用

`mt6895-xiaomi-xaga.dts:630-634`：
> `The mediatek_v2 display driver (binds on dispsys_config@14000000) is now active. The LK frame stays visible via simplefb above until the DRM driver takes over. Keep simplefb enabled during bring-up as a fallback.`

### 3.2 ★★ display main path 组件链（`mtk_drm_drv.c:1893-1913`）

```
OVL1_2L
 → OVL0 → OVL0_VIRTUAL0 → OVL0_VIRTUAL1
 → RDMA0
 → [TDSHP0 → COLOR0 → CCORR0 → CCORR1 → C3D0 → DMDP_AAL0
    → AAL0 → GAMMA0 → POSTMASK0 → DITHER0 → CM0 → SPR0
    → PQ0_VIRTUAL → MAIN0_VIRTUAL]
 → DSI0 → PWM0
 → [CHIST0, CHIST1]
```

**dual pipe 路径**（`mtk_drm_drv.c:1915-1934`）：
```
OVL3_2L → OVL1 → OVL1_VIRTUAL0 → OVL1_VIRTUAL1 → RDMA2
 → TDSHP1 → COLOR1 → CCORR2 → CCORR3 → C3D1 → DMDP_AAL1
 → AAL1 → GAMMA1 → POSTMASK1 → DITHER1 → CM1 → SPR1
 → PQ1_VIRTUAL → DLO_ASYNC4 → DLI_ASYNC0
 → PWM1 → [CHIST2, CHIST3]
```

（DVFS 由 `mtk_display_crtc.c` / `mtk_drm_crtc.c` 处理）
**注意：dual pipe 无法与 bypass PQ 同时启用**（`mtk_drm_drv.c:1916` 注释）。

### 3.3 ★★★ crtc_enable 的 15 步（`mtk_drm_crtc.c:6074-6209`）

**这是显示上电的权威顺序，每一步都有编号注释**：

```
前置：
  mtk_crtc_prepare_dual_pipe(mtk_crtc)
  mtk_crtc_attach_ddp_comp(crtc, ddp_mode, true)
  output_comp = mtk_ddp_comp_request_output(mtk_crtc)
  → SET_MMCLK_BY_DATARATE（按 datarate 设 MMCLK）

1.  power on mtcmos              mtk_drm_top_clk_prepare_enable(crtc->dev)
2.  prepare 需要的模块            mtk_crtc_ddp_prepare(mtk_crtc)
    mtk_gce_backup_slot_restore(mtk_crtc)
3.  power on cmdq client          cmdq_mbox_enable(client->chan)
                                  mtk_crtc_prepare_instr(crtc)
4.  先启动 trigger loop 保 GCE 活着  mtk_crtc_start_trig_loop(crtc)
    （若 sodi loop 且非 frame trigger → mtk_crtc_start_sodi_loop）
5.  connect path                 mtk_crtc_connect_default_path(mtk_crtc)
6.  config ddp engine            mtk_crtc_config_default_path(mtk_crtc)
7.  断开 addon 模块并配置          mtk_crtc_connect_addon_module(mtk_crtc)
8.  restore OVL setting          mtk_crtc_restore_plane_setting(mtk_crtc)
9.  Set QOS BW                   对每个 comp: PMQOS_SET_BW
10. set dirty for cmd mode       （frame trigger 且非 doze 时）
11. set vblank                   drm_crtc_vblank_on(crtc)
12. enable ESD check             mtk_disp_esd_check_switch(crtc, true)
13. enable fake vsync if need    mtk_drm_fake_vsync_switch(crtc, true)
14. set CRTC SW status           mtk_crtc_set_status(crtc, true)
15. alloc sram if last is MML    mtk_crtc_alloc_sram(mtk_crtc)
```

**第 4 步的注释值得记住**：`/* 4. start trigger loop first to keep gce alive */` —— 必须先起 trigger loop，否则 GCE 会卡。

### 3.4 ★★★ panel：`l16_36_02_0b_dsc_vdo`

**驱动**：`drivers/gpu/drm/panel/panel-l16-36-02-0b-dsc-vdo.c`（1515 行）

#### 分辨率与时序

```
FRAME_WIDTH   = 1080        FRAME_HEIGHT = 2460
PHYSICAL_WIDTH  = 67392     PHYSICAL_HEIGHT = 153504   （单位 µm → 67.4mm × 153.5mm）
DATA_RATE     = 1100        （Mbps/lane）
```

模式表（`panel-l16-36-02-0b-dsc-vdo.c:511-556`）：

| 模式 | clock (kHz) | HFP | HSA | VFP | VSA |
|---|---|---|---|---|---|
| 30 Hz | 326941 | 328 | 10 | 5141 | 8 |
| 48 Hz | 326933 | 328 | 10 | 2283 | 8 |
| 50 Hz | 326898 | 328 | 10 | 2092 | 8 |
| default | 326898 | 328 | 10 | — | — |

（所有模式 H 方向固定 1080 + 328 + 10，V 方向固定 2460，只有 VFP 变。）

#### DSC 参数（`panel-l16-36-02-0b-dsc-vdo.c:48-82`）

```
DSC_ENABLE=1, DSC_VER=17, DSC_SLICE_MODE=1, DSC_RGB_SWAP=0, DSC_DSC_CFG=34
DSC_RCT_ON=1, DSC_BIT_PER_CHANNEL=8, DSC_DSC_LINE_BUF_DEPTH=9, DSC_BP_ENABLE=1
DSC_BIT_PER_PIXEL=128, DSC_SLICE_HEIGHT=20, DSC_SLICE_WIDTH=540, DSC_CHUNK_SIZE=540
DSC_XMIT_DELAY=512, DSC_DEC_DELAY=526, DSC_SCALE_VALUE=32
DSC_INCREMENT_INTERVAL=488, DSC_DECREMENT_INTERVAL=7
DSC_LINE_BPG_OFFSET=12, DSC_NFL_BPG_OFFSET=1294, DSC_SLICE_BPG_OFFSET=1302
DSC_INITIAL_OFFSET=6144, DSC_FINAL_OFFSET=4336
DSC_FLATNESS_MINQP=3, DSC_FLATNESS_MAXQP=12, DSC_RC_MODEL_SIZE=8192
DSC_RC_EDGE_FACTOR=6, DSC_RC_QUANT_INCR_LIMIT0=11, DSC_RC_QUANT_INCR_LIMIT1=11
DSC_RC_TGT_OFFSET_HI=3, DSC_RC_TGT_OFFSET_LO=3
```

#### panel GPIO（`mt6895.dtsi:5370-5384`）

```dts
compatible = "l16_36_02_0b_dsc_vdo,lcm";
reg = <1>;                                  // DSI virtual channel 1
reset-gpios  = <&pio 41  GPIO_ACTIVE_HIGH>;  // GPIO41
leden-gpios  = <&pio 215 GPIO_ACTIVE_HIGH>;  // GPIO215
cam-gpios    = <&pio 158 GPIO_ACTIVE_HIGH>;  // GPIO158
dvdd-gpios   = <&pio 118 GPIO_ACTIVE_HIGH>;  // GPIO118
vrf18-supply = <&vrf18_reg>;                 // 1.8V，固定 stub
backlight    = <&gate_ic>;                   // KTZ8863A
```

#### ★★ panel 初始化序列（`panel-l16-36-02-0b-dsc-vdo.c:299-318`）

**reset 时序是 1→0→1，三段延时都是 10ms/1ms/10ms**：

```c
reset_gpio 拉高 → usleep_range(10ms, 10ms+20)
reset_gpio 拉低 → usleep_range(1ms,  1ms+20)
reset_gpio 拉高 → usleep_range(10ms, 10ms+20)
然后 push_table(lcm_init_setting)
```

（`panel-l16-36-02-0b-dsc-vdo.c:310-314` 注释：`XAGA-PANEL[init] reset=1 done` / `reset=0(1ms)->1 done`）

#### ★★★ panel init 命令表（`panel-l16-36-02-0b-dsc-vdo.c:208-257`）

**完整照抄，这是点屏的核心**。格式 `{cmd, len, {params}}`：

```
{0xFF, 1, {0x10}}    {0xFB, 1, {0x01}}    {0xB0, 1, {0x00}}
{0xC1, 16, {0x89,0x28,0x00,0x14,0x02,0x00,0x02,0x0E,0x01,0xE8,0x00,0x07,0x05,0x0E,0x05,0x16}}
{0xC2, 2, {0x10,0xF0}}
{0xE9, 1, {0x01}}
{0xFF, 1, {0xE0}}    {0xFB, 1, {0x01}}    {0x35, 1, {0x82}}
{0xFF, 1, {0xF0}}    {0xFB, 1, {0x01}}
{0x1C, 1, {0x01}}    {0x33, 1, {0x01}}    {0x5A, 1, {0x00}}    {0x9F, 1, {0x0E}}
{0xFF, 1, {0xD0}}    {0xFB, 1, {0x01}}    {0x53, 1, {0x22}}    {0x54, 1, {0x02}}
{0xFF, 1, {0xC0}}    {0xFB, 1, {0x01}}    {0x9C, 1, {0x11}}    {0x9D, 1, {0x11}}
{0xFF, 1, {0x25}}    {0xFB, 1, {0x01}}    {0x18, 1, {0x22}}
{0xFF, 1, {0x10}}    {0xFB, 1, {0x01}}
/* ESD Setting */
{0xFF, 1, {0x27}}    {0xFB, 1, {0x01}}    {0x40, 1, {0x20}}
{0xFF, 1, {0x10}}    {0xFB, 1, {0x01}}    {0xC0, 1, {0x03}}    {0x53, 1, {0x24}}
{0xFF, 1, {0x10}}    {0xFB, 1, {0x01}}    {0x35, 1, {0x00}}
{0x11, 0, {}}                        ← Sleep Out
{REGFLAG_DELAY, 70, {}}              ← 等 70ms
{0x29, 0, {}}                        ← Display On
{0xFF, 1, {0x27}}    {0xFB, 1, {0x01}}
{0x3F, 1, {0x01}}    {0x43, 1, {0x08}}    {0x40, 1, {0x25}}
{0xFF, 1, {0x10}}    {0xFB, 1, {0x01}}
```

**suspend 序列**（`panel-l16-36-02-0b-dsc-vdo.c:260-265`）：
```
{0x28, 1, {}}  →  {REGFLAG_DELAY, 20}  →  {0x10, 1, {}}  →  {REGFLAG_DELAY, 60}
```

#### 写命令的分派规则（`panel-l16-36-02-0b-dsc-vdo.c:110-121`）

```c
if ((int)*addr < 0xB0)
	ret = mipi_dsi_dcs_write_buffer(dsi, data, len);   // DCS
else
	ret = mipi_dsi_generic_write(dsi, data, len);      // generic
```

**这个 0xB0 分界很重要**：`0xFF`/`0xFB` 这些厂商命令必须走 generic write。

#### ★ vrf18 的 refcount 陷阱（`panel-l16-36-02-0b-dsc-vdo.c:162-168`）

> `vrf18 is a fixed always-on regulator stub (LK leaves the rail powered). regulator_is_enabled() is therefore always true and cannot be used as a guard: enable/disable must be balanced with a local refcount, otherwise the panel power cycle trips "unbalanced disables" in the regulator core.`

代码里用 `static bool vrf18_enabled` 做本地 refcount（`panel-l16-36-02-0b-dsc-vdo.c:167`, `:196-208`）。

### 3.5 背光：KTZ8863A

**节点**（`mt6895.dtsi:1612-1618`）：
```dts
gate_ic: gate_ic@11 {
	compatible = "mediatek,MI_I2C_LCD_BIAS";
	reg = <0x11>;                      // I2C6 0x11
	label = "lcd-backlight";
	max-brightness = <4095>;
	default-state = "half";
};
```

**驱动**：`drivers/gpu/drm/panel/leds-ktz8863a.c`（有 `ktz8863a_regs_conf[]` 配置表，`:65`）

**I2C6 基址**：`0x11281000`（`mt6895.dtsi:1600`），400kHz，GPIO31=SCL6 / GPIO32=SDA6（`mt6895.dtsi:1595-1598`）

### 3.6 DSI

**节点**（`mt6895.dtsi:5352-5366`）：
```dts
dsi0: dsi@14017000 {
	compatible = "mediatek,mt6895-dsi";
	reg = <0x14017000 0x1000>;
	interrupts = <GIC_SPI 0x143>;       // = 323
	clocks = <&mmsys0 CLK_MM0_FMM_DISP_DSI0>, <&mmsys0 CLK_MM0_DISP_DSI>, <&mipi_tx0>;
	clock-names = "engine", "digital", "hs";
	phys = <&mipi_tx0>;
	phy-names = "dphy";
};
```

**MIPI TX PLL**：`mipi_tx_config@11f70000`（`mt6895.dtsi:5808`）

**其他显示块基址**：
- `disp_dsc_wrap@14015000`
- `disp_merge@14016000`
- `disp_spr@14014000`
- `disp_wdma0@14018000`

### 3.7 ★★ IOMMU / SMI（踩坑最密集的地方）

#### 显示 IOMMU

`disp_iommu = 1e802000.iommu`（见 `mt6895.dtsi:6755` 的注释引用）

#### SMI larb 的 port 必须全开（`mt6895.dtsi:6730-6755`）—— **这是最详细的一条踩坑记录**

> ```
> XAGA: larb10 ports 3 and 16 are used by the imgsys DMA but were
> missing here, so their SMI_LARB_MMU_EN bit and bank stayed 0.
> Confirmed at runtime on the translation fault:
>   XAGA-IOMMU-LARB: comm=10 sub=0 port=16 dev=15120000.smi_larb10
>                    mmu=0x00000400 port_en=0 bank=0
> With translation disabled the engine's IOVA is emitted as a raw
> address: a read of IOVA 0 faults, and any other address lands in
> unrelated DRAM (the observed display/DRAM corruption).
>
> Port 12 was missed when 3 and 16 were fixed. It is
> l10_img4o_d1, a DIP *output*, and it is the dominant fault:
>   fault type=0x280 iova=0x3dc680000 pa=0x0 larb=0 port=12
>         layer=1 write            x186   (all port_en=0)
>   XAGA-IOMMU-LARB: port_en=1     x4     (ports 3 and 16)
>
> The write storm is on disp_iommu (1e802000.iommu), the IOMMU the
> display shares, so it underruns while the camera streams and
> recovers as soon as the DIP stops writing - and the DIP's own
> output is faulted away, which is why the imgsys job never
> completes. Port 12 is proven; the node's interconnect-names
> lists 20 l10_* ports, so the rest of the set the imgsys DMA
> actually drives should be enabled here too rather than fixed one
> crash at a time.
> ```

**提炼出的硬件事实**：
1. SMI larb 的每个 port 必须在 DT 里列出，否则 `SMI_LARB_MMU_EN` bit 和 bank 都是 0
2. MMU 关闭时，引擎发出的 IOVA 被当成**裸物理地址**直接用 → 读 IOVA 0 就 fault，其他地址落到无关 DRAM（造成"显示/DRAM 损坏"）
3. 相机写风暴打在 `disp_iommu` 上会让**显示** underrun（共用同一个 IOMMU）

#### ★ mmqos/interconnect 缺失 = 白屏（`mt6895.dtsi:5130-5137` 等多处重复）

同一条注释在 8 个节点上重复出现（OVL、RDMA 等），原文：

> ```
> PEARL: without these the display never votes for bandwidth.
> of_mtk_icc_get() returns NULL when the node has no
> interconnects, so every mtk_icc_set_bw() call from the ovl driver is a
> no-op and the OVL underruns on every frame (that is the white
> screen).  Ports come from this node's own iommus, the names
> from ddp_comp_str[] -- DECLARE_STR stringifies the enum.
> ```

**对应代码**：`mtk_drm_crtc.c:6184` 第 9 步 `PMQOS_SET_BW`；`mt6895.dtsi:5035` `/* PEARL: the display's global HRT bandwidth vote. */`

**事实**：显示每个 DDP 组件必须有自己的 `interconnects`，否则带宽投票是 no-op → OVL 每帧 underrun → **白屏**。

#### ★ DVFSRC 一碰就挂（`mt6895.dtsi:528-538`）

> ```
> PEARL: stand-in for the DVFSRC VCORE rail.
>
> The display votes for its multimedia step through the real dvfsrc
> regulator on the vendor tree, but that provider programs VCORE and
> hangs this board (twice, recovered over fastboot each time).  A
> fixed regulator satisfies mtk_disp_pmqos.c's probe, which is what
> gates the whole voting path -- including the bandwidth votes that
> go to MMQoS/emi_icc and never touch DVFSRC.  The voltage requests
> this allows through are no-ops on a fixed rail.
> ```

**这是极硬的经验**：真的 dvfsrc regulator 会 program VCORE 并**把板子挂死**（作者挂了两次，每次都得 fastboot 救）。用固定 regulator 顶替即可，因为真正需要的是让 `mtk_disp_pmqos.c` 的 probe 过，从而放行整条投票路径。

配套（`mt6895.dtsi:542-548`）：
> ```
> regulator-fixed rejects min != max ("Fixed regulator
> specified with variable voltages"), and a probe failure here
> leaves 14000000.dispsys_config deferred forever -- which is
> exactly the white screen.  The rail is a stand-in, so pin
> both ends to the lowest display step.
> ```

**`regulator-fixed` 的 min 必须等于 max**，否则 probe 失败 → `dispsys_config` 永远 deferred → 白屏。

#### ★ LK 的 framebuffer tag（`mt6895-xiaomi-xaga.dts:85-95`）

> ```
> LK injects this tag into its FDT; our embedded-DTB override would
> lose it. Values dumped from the working Android device:
> struct tag_videolfb { u64 fb_base; u32 islcmfound; u32 fps;
>                      u32 vram; char lcmname[]; } (LE)
> fb_base=0xfe14f000 (framebuffer region start), islcmfound=1,
> fps=6060 (x100), vram=0x1eb0000, lcmname="l16_36_02_0b_dsc_vdo_lcm_drv".
> Without it, mtk_crtc_fill_fb_para() fails and the LK framebuffer is
> never inserted into the display IOMMU -> OVL fetch fault -> RDMA
> underflow -> display pipeline stalls after the LK frame.
> ```

**结构体布局**（`tag_videolfb`，小端）：
```c
struct tag_videolfb {
	u64 fb_base;        // 0xfe14f000
	u32 islcmfound;     // 1
	u32 fps;            // 6060  (= 60.60 fps × 100)
	u32 vram;           // 0x1eb0000 (31.4 MiB)
	char lcmname[];     // "l16_36_02_0b_dsc_vdo_lcm_drv"
};
```

**后果链**：没有这个 tag → `mtk_crtc_fill_fb_para()` 失败 → LK 的 framebuffer 进不了显示 IOMMU → OVL fetch fault → RDMA underflow → 显示管线在 LK 那一帧之后**卡死**。

#### simple-framebuffer（`mt6895-xiaomi-xaga.dts:111-121`）

```dts
/* 放在 root 层而不是 /chosen，因为 LK 会重写 /chosen 并把它丢掉 */
地址 = 0xff5c3000  (LK 实际扫描输出层 mt_disp_config_fb_layer)
位于 mblock-12-framebuffer 保留区 0xfe14f000..0xfffff000 内

/* frame = stride(4352) * height(2460) = 0xa35c00 */
```

**注意 stride = 4352 = 1088 × 4**，而宽度是 1080 —— 有 8 像素对齐 padding。

### 3.8 CMDQ / GCE

**两个 GCE 引擎**（`mt6895.dtsi:2983-2989`）：
> ```
> GCE_M (0x1e990000) is the second CMDQ engine, reserved for the
> multimedia/ISP (imgsys) path.  The display GCE is @1e980000.
> Downstream uses &gce for display and &gce_m for imgsys; keeping
> them separate is required so imgsys cannot claim the display's
> CMDQ threads (mbox 0..6,22).
> ```

| 引擎 | 基址 | 用途 | mbox 线程 |
|---|---|---|---|
| 显示 GCE | `0x1e980000` | panel | 0..6, 22 |
| 多媒体 GCE | `0x1e990000` | imgsys/ISP | 其他 |

**驱动**：`drivers/mailbox/mtk-cmdq-mailbox.c`、`drivers/misc/mediatek/cmdq/`

**注释**（`mt6895.dtsi:2770-2776`）：
> `GCE (global command engine) display instance, used by cmdq to drive the DDP components per-frame (SOF/EOF sync). LK leaves it clocked; the "gce"/"gce-timer" clocks are fixed stubs (the real sources are infracfg_ao gates). Only the display GCE is wired up here; the multimedia GCE (0x1e990000) is not used by the panel.`

**MMINFRA 相关**（`mt6895.dtsi:2783-2795`）：
```dts
mminfra_config: syscon@1e800000   "mediatek,mt6895-mminfra_config"
mminfra_debug:   mminfra-debug@1e827000
	power-domains = <&spm 6>;         // MM_INFRA
	mminfra-bkrs = <1>;
	init-clk-on;
	clocks = CLK_MMINFRA_GCE_D, CLK_MMINFRA_GCE_M, CLK_MMINFRA_SMI, CLK_MMINFRA_GCE_26M
```

---

## 4. UFS / 存储

### 4.1 节点配置（`mt6895.dtsi:1919-1968`）

```dts
ufshci: ufshci@112b0000 {
	compatible = "mediatek,mt8183-ufshci";     // ← 复用 mt8183 兼容串
	reg = <0x112b0000 0x2300>;
	interrupts = <GIC_SPI 137>;
	clocks = <&topckgen CLK_TOP_U_SEL>,
	         <&topckgen CLK_TOP_AES_UFSFDE_SEL>,
	         <&topckgen CLK_TOP_U_MEM_SEL>,
	         <&topckgen CLK_TOP_U_HAXI_SEL>;
	clock-names = "ufs_sel", "ufs_aes", "ufs_mem", "ufs_haxi";
	freq-table-hz = <0 0>, <0 0>, <0 0>, <0 0>;
	power-domains = <&spm MT6895_POWER_DOMAIN_UFS0_SHUTDOWN>;
	mediatek,ufs-disable-mcq;
	status = "okay";
};
```

**⚠ 没有 `phys` 属性！** 我核对过整个 dtsi：`phys = ` 只出现在 3 处（`u2port` USB ×2 和 `mipi_tx0`），**UFS 没有 PHY 节点，也没有 PHY 引用**。

**结论：UFS PHY 完全由 LK 配好，Linux 不碰。** 这与 UFS 的工作状态一致（UFS Works）。

### 4.2 ★★ UFS 时钟的两个必坑（`mt6895.dtsi:1923-1947`）

**完整原文**：
> ```
> Top-level UFS clocks from topckgen. The ufscfg_ao /
> ufscfg_pdn gate clocks (tx/rx symbol, unipro sys,
> hciufs, hciaes, ...) still have no mainline driver,
> so those stay as LK programmed them; but these four
> must be owned by the driver:
>
>   ufs_sel        - MUX_GATE_CLR_SET_UPD, and ufs_ck
>                    is a fixed factor of it, so it is
>                    the UFS controller's own clock.
>   aes_ufsfde_sel - MUX_GATE_CLR_SET_UPD (UFS FDE).
>
> Both are hardware-enabled by LK with enable_count
> == 0, so clk_disable_unused() switches them off and
> wedges the controller (verified on hardware: setting
> CLK_CFG_14 bit 7 kills UFS instantly). ufs_mem_sel /
> ufs_haxi_sel are plain (non-gated) muxes, listed so
> the driver's clock list matches the hardware.
>
> freq-table-hz is mandatory: without it
> ufshcd_parse_clock_info() drops the entire list.
> All-zero entries mean "don't touch the rate", which
> also leaves clock scaling disabled (ufs_sel_max_src /
> ufs_sel_min_src are deliberately absent).
> ```

**提炼成 4 条硬事实**：
1. `ufs_sel` 和 `aes_ufsfde_sel` 是 `MUX_GATE_CLR_SET_UPD` 类型，LK 已硬件使能但 `enable_count == 0`
2. 所以 `clk_disable_unused()` 会把它们关掉，**控制器立刻卡死**（硬件验证过：写 `CLK_CFG_14` bit 7 就当场杀 UFS）
3. `freq-table-hz` **必须存在**，否则 `ufshcd_parse_clock_info()` 会丢掉整个时钟列表
4. 全 0 项表示"不要动频率"，同时也意味着**时钟缩放被禁用**

### 4.3 ★ UFS0_SHUTDOWN 电源域（`mt6895.dtsi:1954-1962`）

> ```
> The UFS0_SHUTDOWN MTCMOS domain, as the downstream DT
> has it.  Without this the domain has no device at all,
> so genpd's late_init pass powers it off unconditionally
> -- which is what forced the blanket
> GENPD_FLAG_ALWAYS_ON workaround in mtk-scpsys.c.
> Attached here, the ufshci driver's runtime PM manages
> it and the workaround is unnecessary.
> ```

**事实**：没有设备挂这个域时，genpd 的 late_init 会**无条件把它关掉**。正确做法是挂到 ufshci 节点上让 runtime PM 管，而不是全局加 `GENPD_FLAG_ALWAYS_ON`。

### 4.4 驱动侧

**文件**：`drivers/ufs/host/ufs-mediatek.c`（2559 行）
**兼容串**（`ufs-mediatek.c:51-53`）：
```c
{ .compatible = "mediatek,mt8183-ufshci" },
{ .compatible = "mediatek,mt8195-ufshci" },
```
**驱动里没有任何 `mt6895` 字样的特判** —— 完全走 mt8183/mt8195 通用路径。

**reset 时序**（`ufs-mediatek.c:203-222`）：
```c
static void ufs_mtk_host_reset(struct ufs_hba *hba)
{
	reset_control_assert(hci_reset);
	reset_control_assert(crypto_reset);
	reset_control_assert(unipro_reset);
	reset_control_assert(mphy_reset);
	...
	reset_control_deassert(unipro_reset);
	reset_control_deassert(crypto_reset);
	reset_control_deassert(hci_reset);
	reset_control_deassert(mphy_reset);
	/* restore mphy setting after mphy reset */
}
```
**assert 顺序：hci → crypto → unipro → mphy；deassert 顺序：unipro → crypto → hci → mphy**（不对称，注意）。

**相关 syscon**：
- `ufscfg_ao: syscon@112b8000`
- `ufscfg_pdn: syscon@112bc000`

---

## 5. 电源 / 充电

### 5.1 ★ 充电/电量计子系统全貌

**目录**：`drivers/power/supply/mediatek/`（这是本树新加的，不是 upstream）

| 文件 | 行数 | 作用 |
|---|---|---|
| `mt6375-charger.c` | 3259 | **主充电器 + OTG boost** |
| `mt6375-gauge.c` | 4062 | 电量计 |
| `mtk_battery.c` | 3730 | 电池框架 |
| `mtk_battery_algo.c` | 1831 | 电量算法 |
| `mtk_battery_coulomb_service.c` | 525 | 库仑计服务 |
| `mtk_battery_table.h` | 4451 | 电池表 |
| `sc8551.c` | 590 | **xaga 用的电荷泵** |
| `sc8561.c` | 826 | xagapro 用的电荷泵 |
| `xaga_cp_manager.c` | 1400 | **xaga 专属快充管理** |
| `charger_class.c` | 1126 | 充电器抽象层 |

### 5.2 ★★ MT6375 充电器 / OTG

**位置**：I2C5 `0x11280000`，地址 `0x34`（`mt6895.dtsi:1429`）

**regmap 跨 6 个 I2C 从地址**（`mt6895.dtsi:1423-1428`）：
> ```
> MT6375 PMIC. Regmap is banked across six I2C slave
> addresses (PMU=0x34, TCPC=0x4e, BM=0x1a, HK1=0x4a,
> HK2=0x64, TM=0x3f); the MFD core creates dummy clients
> for all of them. GPIO 116 is the MT6375 INT pin.
> ```

| 功能块 | I2C 地址 |
|---|---|
| PMU | `0x34` |
| TCPC | `0x4e` |
| BM (battery) | `0x1a` |
| HK1 | `0x4a` |
| HK2 | `0x64` |
| TM | `0x3f` |

**INT 引脚**：GPIO 116

#### OTG regulator（`mt6375-charger.c:822-838`）

```c
static const struct regulator_desc mt6375_chg_otg_rdesc = {
	.of_match = "mt6375,otg-vbus",
	.type = REGULATOR_VOLTAGE,
	.min_uV = 4850000,       // 4.85V
	.uV_step = 25000,        // 25mV 步进
	.n_voltages = 47,        // → 最高 4.85 + 46*0.025 = 6.0V
	.linear_min_sel = 20,
	.csel_reg = MT6375_REG_OTG_C,   // 0x132
	.csel_mask = MT6375_MSK_OTG_CC, // 0x07
	.vsel_reg = MT6375_REG_OTG_V,   // 0x131
	.vsel_mask = MT6375_MSK_OTG_CV, // 0x3F
	.enable_reg = MT6375_REG_CHG_TOP1,
	.enable_mask = MT6375_MSK_OTG_EN,  // 0x04
};
```

**OTG 相关寄存器**（`mt6375-charger.c:85-110`）：
```
MT6375_REG_OTG_LBP = 0x130     MT6375_MSK_OTG_EN = 0x04
MT6375_REG_OTG_V   = 0x131     MT6375_MSK_OTG_CV = 0x3F
MT6375_REG_OTG_C   = 0x132     MT6375_MSK_OTG_CC = 0x07
```

**OTG LBP（低电池保护）范围**（`mt6375-charger.c:442`）：`2700..3800 mV，100mV 步进，4 档`

#### 默认平台参数（`mt6375-charger.c:846-866`）

```c
static const struct mt6375_chg_platform_data mt6375_chg_pdata_def = {
	.aicr = 3225,        // mA
	.mivr = 4400,        // mV
	.ichg = 2000,        // mA
	.ieoc = 150,         // mA
	.cv = 4200,          // mV
	.wdt = 40000,
	.vbus_ov = 14500,    // mV
	.vrec = 100,
	.chg_tmr = 10,       // 小时
	.dcdt_sel = 600,
	.wdt_en = false,
	.te_en = false,
	.chg_tmr_en = true,
	.chg_name = "primary_chg",
};
```

#### ★ xaga 的 OTG 用法（`mt6895-xiaomi-xaga.dts:636-645`）

> ```
> USB manual OTG:
> - dr_mode="otg" keeps both host and device paths available.
> - usb-role-switch + role-switch-default-mode="device" expose a sysfs role
>   switch for manual host/device selection (no automatic Type-C detect yet).
> - vbus-supply is the MT6375 charger OTG regulator needed to source 5V in
>   host mode.
> - The t-phy software role switch is kept, and role-switch-default-mode="device"
>   is set on the xHCI node so the PHY initializes in device mode.
> ```

**注意：没有自动 Type-C 检测**，OTG 是手动 sysfs 切。

### 5.3 ★★ SC8551 电荷泵（xaga 快充）

**位置**（`mt6895-xiaomi-xaga.dts:1355-1361`）：
> ```
> Fast-charge pumps.  xaga populates two SC8551A 2:1 charge pumps at
> 0x66 (i2c9 master, i2c7 slave); xagapro uses two SC8561 2:1/4:1 pumps
> at 0x64 (i2c9 master) and 0x65 (i2c7 slave).  Both sets of nodes live
> in the shared DTB and the part-id probe makes the wrong pair fail
> harmlessly on each board.
> ```

| 板 | 主泵 | 从泵 | 类型 |
|---|---|---|---|
| **xaga** | I2C9 `0x66` | I2C7 `0x66` | SC8551A 2:1 |
| xagapro | I2C9 `0x64` | I2C7 `0x65` | SC8561 2:1/4:1 |

### 5.4 ★★ xaga 快充管理器（`xaga_cp_manager.c`）—— 大量硬参数

**文件头注释**（`xaga_cp_manager.c:1-12`）：
> ```
> xaga / xagapro charge-pump manager (mainline)
>
> Bridges the mainline TCPM PPS/APDO power-supply interface to the
> SC8551 (xaga) charge pumps, in place of the downstream MediaTek
> adapter_class / pd_cp_manager stack.
>
> The charge pumps are always kept off until a negotiated PPS contract
> exists (or "force" is set for a controlled fixed-PDO test).  When a
> pump is switched on the MT6375 buck is stopped first so the two
> chargers never fight over the same VBUS.
> ```

**全部关键常量**（`xaga_cp_manager.c:27-72`）：

```
XAGA_CP_MODE_4_1            0       (SC8561 4:1)
XAGA_CP_MODE_2_1            1       (SC8551 & SC8561 2:1)
XAGA_MT6375_AICR            3000000 (恢复直充路径时的 AICR，单位 uA)

闭环调节：
XAGA_CPM_RES                2       (2:1 泵)
XAGA_CPM_STEP_MV            20
XAGA_CPM_REG_MS             1000
XAGA_CPM_RAMP_UA            300000
XAGA_CPM_IBUS_GAP_MA        400
XAGA_CPM_4_1_MIN_VBUS_UV    18000000   (4:1 需要 ~4*(VBAT+headroom)，下游用 18..22V APDO)
XAGA_CPM_VBUS_GAP_MV        1800       (初始窗口)
XAGA_CPM_VBUS_GAP_MIN_MV    300
XAGA_CPM_STALL_IBUS_UA      150000
XAGA_CPM_STALL_TICKS        3
XAGA_CPM_STALL_BACKOFF_MV   300
XAGA_CPM_GAP_DOWN_MV        200
XAGA_CPM_GAP_UP_MV          100
XAGA_CPM_GAP_UP_TICKS       15
XAGA_CPM_MIN_HEADROOM_MV    200
XAGA_CPM_FCC_START_UA       1500000
XAGA_CPM_SLAVE_MIN_UA       3000000    (只有目标真的需要才并联第二个泵)
XAGA_CPM_PATH_R_MOHM        60         (CP 输出到电芯的路径电阻，用于 CV 上限)
XAGA_CPM_TOPOFF_SOC         85         (85% 把 CV/收尾交回 MT6375)
XAGA_CPM_RECHARGE_SOC       75
XAGA_CPM_LOWP_MW            8000       (快充功率塌陷截止)
XAGA_CPM_LOWP_SOC           70
XAGA_CPM_LOWP_MS            12000
XAGA_CPM_TOPOFF_DWELL_MS    120000
```

**★ SC8561 的 latch 陷阱**（`xaga_cp_manager.c:49-56`）：
> ```
> A pump that latches off (VBUS_ERRORHI) leaves its enable bit set, so the
> "!enabled" retry never fires.  If one is on but drawing almost nothing
> while we are still asking for real current, shrink the operating window
> and cycle the pump so it restarts lower; creep the window back up when it
> runs clean, so the loop converges on the largest non-latching headroom.
> ```

**这是很细的硬件行为**：泵 latch 后 enable 位仍然是 1，所以靠 `!enabled` 判断永远重试不了。必须靠「开启但几乎不拉电流」来推断，然后缩小窗口 + 循环重启。

**SC8561 输入窗口限制**（`xaga_cp_manager.c:45-49`）：
> `SC8561 input window above ratio*VBAT.  The part refuses to switch if the headroom is too large (VBUS_ERRORHI), so xagapro's request is held within ~ratio*VBAT + this gap (downstream uses a 600..850 mV window).`

### 5.5 ★ MT6368 regulator（VSTACK 等）

**VBUCK2 = VSTACK = GPU stacks 域供电**（`mt6895.dtsi:2228-2237`）：
```dts
mt6368_vbuck2: VBUCK2 {
	regulator-name = "mt6368_vbuck2";
	regulator-min-microvolt = <0x493e0>;      // 300000 = 0.3V
	regulator-max-microvolt = <0x123716>;     // 1193750 ≈ 1.194V
	regulator-enable-ramp-delay = <0xc8>;     // 200us
	regulator-allowed-modes = <0x00 0x01 0x02>;
	regulator-always-on;
};
```

**其他关键 rail**：
- `mt6368_vaud18` = VAUD18，min `0x927c0`(600000) max `0x200b20`(2100000) —— **audio codec 的模拟供电**（`mt6895.dtsi:2393-2400`），在 DAPM 里是 `AIFTX_Supply` 的父（`mt6368.c:3721`）
- `mt6368_vibr` = VIBR（指纹 1.8V 用）
- `mt6368_vfp` = VFP（指纹 3.3V 用）

**★ VBUCK1 的 always-on 原因**（`mt6895.dtsi:2222-2224`）：
> `boot-enabled by firmware, no kernel consumer yet; keep regulator_init_complete from killing it (display-critical)`

**即：VBUCK1 是显示关键电源，没有 kernel consumer，必须 always-on 否则 30 秒后被 regulator core 关掉。**

### 5.6 ★ MT6319 CPU bucks（`mt6895-xiaomi-xaga.dts:1214-1233`）

> ```
> MT6319 external CPU bucks. These are the real VPROC rails; the board
> relies on the fixed 0.8V "vproc" stub for cpu-supply, so mainline has
> had no visibility or control over them. Registering them here makes the
> per-cluster CPU voltages readable (and, later, settable once a CPU OPP
> table is wired up):
>   mt6319_7 vbuck3 -> little cluster + DSU
>   mt6319_7 vbuck1 -> middle (A78 x3) cluster
>   mt6319_6 vbuck1 -> prime (A78 x1) cluster
> (mapping taken from the downstream crdroid mt6895.dts cpuhvfs node.)
>
> regulator-always-on is mandatory: on a DT system the regulator core
> actively disables enabled-but-unused regulators 30s after boot, which
> would otherwise kill the CPU rails. The wide min/max only sets apply_uV,
> which clamps solely when the live voltage is outside the range.
> ```

**CPU 供电映射（硬事实）**：

| PMIC | buck | 供电对象 |
|---|---|---|
| mt6319_7 | vbuck3 | little cluster (4×A55) + DSU |
| mt6319_7 | vbuck1 | middle cluster (3×A78) |
| mt6319_6 | vbuck1 | prime cluster (1×A78) |

**★ 关键陷阱**：DT 系统上 regulator core 会在开机 **30 秒后**主动关掉"已使能但没人用"的 regulator。所以所有关键 rail 必须 `regulator-always-on`。

### 5.7 电源相关外设地址速查

| 器件 | 总线 | 地址 | 节点行 |
|---|---|---|---|
| MT6375（充电/TCPC/ADC/电量计） | I2C5 | `0x34` | `dtsi:1429` |
| RT5133（8×LDO） | I2C5 | `0x18` | `dtsi:1526` |
| KTZ8863A（背光/bias） | I2C6 | `0x11` | `dtsi:1612` |
| bq28z610（电量计） | I2C7 | `0x55` | `dtsi:1641` |
| AW8697（触觉） | I2C1 | `0x5a` | `dtsi:1738` |
| ET7480（audio switch） | I2C1 | `0x42` | `dtsi:1722` |
| TFA9874 L | I2C6 | `0x34` | `xaga.dts:808` |
| TFA9874 R | I2C6 | `0x35` | `xaga.dts:817` |
| SN110T NFC | I2C3 | `0x28` | `xaga.dts:1428` |
| KTD2687 闪光灯 | — | `0x63` | `xaga.dts:839` |
| MT6363 主 PMIC | SPMI | USID 4 | `dtsi:2039` |
| MT6368 副 PMIC | SPMI | USID 5 | `dtsi:2141` |
| MT6685 伴生 PMIC | SPMI | USID 9 | `dtsi:2452` |

**SPMI 基址**：`spmi@1c804000`（`mt6895.dtsi:2019`），`pmif_irq = GIC SPI 414`，`rcs_irq` 在 GPIO243（`mt6895.dtsi:2030-2031`）

---

## 6. ★ dtsi 注释里的全部踩坑经验（逐条摘录）

`mt6895.dtsi` 共 242 条块注释、`mt6895-xiaomi-xaga.dts` 共 81 条。以下是**全部有价值的技术经验**，按文件行号顺序。纯寄存器标注（如 `/* mbox0 base */`）已跳过。

### 6.1 `mt6895.dtsi`（SoC 级）

---

**L2-13｜CPU 拓扑**
> `The CPU topology is the MediaTek 4-3-1 configuration:`
> `cluster0: 4x Cortex-A55 / cluster1: 3x Cortex-A78 / cluster2: 1x Cortex-A78`

---

**L219-230｜★★ 空闲功耗：mainline 原来一个 idle state 都没有**
> `MT6895 PSCI idle states, taken from the downstream crdroid mt6895.dts. mainline shipped zero idle states, so the CPUs only ever did WFI and the SoC never entered mcusysoff, which is the prime suspect for the ~1.7W idle floor (Android idles at ~0.7W with the same DRAM/display/wifi load).`
>
> `The deeper system_mem/pll/bus (dpidle) and s2idle states are intentionally left out for now: they need SPM support that mainline has not ported for mt6895. Add them only after this set is proven.`

**事实**：只做 WFI 不进 mcusysoff → 空闲 1.7W（Android 是 0.7W）。dpidle/s2idle 需要 SPM 支持，暂时没做。

---

**L303-312｜★ I2C5 时钟的真实来源**
> `i2c5 clock stubs. The real sources are i2c_sel (topckgen CLK_CFG_11 bits 8-9, default parent tck_26m = 26MHz) -> i2c_ck, gated by imp_iic_wrap_c (bit0), and the apdma gate in pericfg_ao (bit5). There is no mt6895 clock driver yet, so these fixed clocks only satisfy the i2c-mt65xx probe (clk_prepare_enable = no-op). The real gates are forced on by xaga_i2c_power_on() in setup.c (LK also uses i2c5 for the off-mode mt6375 charging animation). The main rate is used to derive SCL timing; 26MHz matches the default mux parent.`

**事实**：`i2c_sel` 在 `CLK_CFG_11` bit 8-9，默认父 `tck_26m`；gate 在 `imp_iic_wrap_c` bit0 + `pericfg_ao` bit5。

---

**L327-332｜SPMI 时钟来源**
> `SPMI (PMIF) clock stubs. The real sources are the vlp_cksys pmif muxes/gates (0x1c013000); LK already enables them to talk to the MT6363 PMIC over SPMI during boot, so fixed clocks only satisfy spmi-mtk-pmif's clk_bulk_get (prepare_enable = no-op).`

**事实**：SPMI 时钟在 `vlp_cksys` 基址 `0x1c013000`，LK 已经开好。

---

**L354-363｜★★★ DISP0/DISP1 时钟 stub 会连锁破坏 GCE**
> `DISP0/DISP1 are now provided by the real MT6895 topckgen driver (clk-mt6895.c), so the old fixed-clock stubs must NOT be present: they collide with clk_hw_register_fixed_factor("disp0_ck"/"disp1_ck") and abort registration of the whole top_divs table (including mminfra_ck), which breaks GCE/MMINFRA clock parenting.`
>
> `hf_fdisp2_ck is not a topckgen factor, so keep the fixed stub for the MM1_SIG_EMI gate parent.`

**这是一条非常隐蔽的坑**：留着旧 stub → 名字冲突 → **整个 top_divs 表注册失败**（连 `mminfra_ck` 一起）→ GCE/MMINFRA 时钟父子关系坏掉。

---

**L371-379｜★ SPI2 时钟的精确位置**
> `SPI2 clock stubs. Real sources: SPI_SEL mux (topckgen CLK_CFG_7 @0x10000080 bits 16-18) -> spi2_bclk, gated by perao0 (@0x11036000+0x3c bit 19, active-low). spi-mt65xx requires clk_set_parent(sel, parent) and clk_get_rate(spi_clk) for divider math; the embedded DTS stubs the mux input at 26MHz and setup.c forces SPI_SEL to parent 0 (tck_26m, 26MHz) + enables the gate, matching the fixed rates here.`

**事实**：`SPI_SEL` 在 `0x10000080` (topckgen) bit 16-18；gate 在 `0x11036000+0x3c` bit 19，**低有效**。

---

**L423-428｜CPU DVFS supply stub**
> `CPU DVFS supply stub: mediatek-cpufreq-hw requires a cpu-supply regulator on every CPU to probe (devm_regulator_get). Actual CPU voltage is stepped by the DVFS firmware via SPM/EMI, so a fixed always-on regulator is sufficient.`

---

**L437-446｜★★★ regulator-fixed 拒绝可变电压（两次踩到）**
> `Stub GPU regulator: panthor calls dev_pm_opp_set_regulators() with names[] = { "mali" }, which fails the probe if no mali-supply exists. A fixed regulator satisfies the OPP core; the real VGPU rail is left at whatever LK configured.`
>
> `regulator-fixed REJECTS a node with variable voltages (drivers/regulator/fixed.c:187-193: min_uV != max_uV -> -EINVAL, probe fails, regulator never registers). So min MUST equal max.`

**精确到源码行号的引用**：`drivers/regulator/fixed.c:187-193`。

---

**L464-474｜GPU OPP 表来源**
> `GPU OPP table from the downstream MT6895 gpufreq table.`
> `The current vdd_gpu is a fixed stub (LK leaves the real VGPU/VCORE rail powered), so all OPPs use the fixed stub voltage. Once a real variable VGPU regulator/DVFSRC path is ported, replace these with the downstream per-OPP microvolt values.`
> `The clock is now the real topckgen MFG_SEL_0 output, so devfreq can set the MFGPLL and mux to these rates.`

---

**L475-481｜多媒体 OPP**
> `Multimedia OPP tables. The frequencies are the topckgen mux parents that the vendor mmdvfs driver selects per voltage step; the voltages are the VCORE steps of the DVFSRC regulator. Requests are aggregated, so a client may keep a low OPP while another one holds the rail high.`

---

**L528-538｜★★★ DVFSRC 真 regulator 会挂死板子**
（见 3.7 节完整引用）

---

**L542-548｜★★★ 固定 regulator 的 probe 失败 = 永久白屏**
（见 3.7 节完整引用）

---

**L612｜SMI table 笔记**
> `table0, video domain6, pseudo-larb33, port1`

---

**L788-809｜★★★ PMU 必须单实例（KVM/Windows 相关）**
> `A single PMU instance covering all eight CPUs, as in the vendor DTS.`
>
> `The 4th cell of the interrupt specifier is the PPI partition. Keep it 0 (no partition): gic_irq_get_fwspec_info() then reports cpu_possible_mask as the affinity and arm_pmu sets supported_cpus = 0-7.`
>
> `Do not split this into per-cluster nodes (as mt8192/mt8195 do). That gives each instance a cluster-sized supported_cpus, which breaks KVM's guest PMU: kvm_pmu_probe_armpmu() latches kvm->arch.arm_pmu once, from raw_smp_processor_id(), at KVM_ARM_VCPU_PMU_V3_INIT time, but kvm_pmu_create_perf_event() creates a task-bound event (cpu = -1, task = current) that follows the vCPU. armpmu_filter() silently rejects any CPU outside supported_cpus, so a vCPU that migrates to another cluster loses its counter and the guest reads a frozen PMCCNTR_EL0 -- which trips bootmgfw.efi's divide-by-zero (brk #0xf004) on Windows.`
>
> `"arm,armv8-pmuv3" is used for the whole PMU because arm_pmu derives the PMU name from the compatible alone, and it must be unique.`

**这条极长极细**，是跑 Windows（bootmgfw.efi）时踩出来的：PMU 分 cluster 节点 → vCPU 迁移丢计数器 → Windows 除零崩溃。

---

**L837-838｜CPU DVFS 域划分**
> `MT6895 CPU DVFS hardware (SPM/EMI-driven, see mediatek-cpufreq-hw). Domain 0 = 4x A55, 1 = 3x A78, 2 = 1x A78 (regs from live DT).`

---

**L900-927｜SCP 寄存器地图**（全注释，直接可用）

```
0x1c400000 0x180000  tcm
0x1c724000 0x1000    cfg
0x1c721000 0x1000    clk
0x1c730000 0x1000    cfg core0
0x1c740000 0x1000    cfg core1
0x1c752000 0x1000    bus tracker
0x1c760000 0x40000   llc
0x1c7a5000 0x4       cfg_sec
0x1c7fb000 0x100     mbox0 base
0x1c7fb100 0x4       mbox0 set
0x1c7fb10c 0x4       mbox0 clr
0x1c7a5020 0x4       mbox0 init
0x1c7fc000 0x100     mbox1 base
0x1c7fc100 0x4       mbox1 set
0x1c7fc10c 0x4       mbox1 clr
0x1c7a5024 0x4       mbox1 init
0x1c7fd000 0x100     mbox2 base
0x1c7fd100 0x4       mbox2 set
0x1c7fd10c 0x4       mbox2 clr
0x1c7a5028 0x4       mbox2 init
0x1c7fe000 0x100     mbox3 base
0x1c7fe100 0x4       mbox3 set
0x1c7fe10c 0x4       mbox3 clr
0x1c7a502c 0x4       mbox3 init
0x1c7ff000 0x100     mbox4 base
0x1c7ff100 0x4       mbox4 set
0x1c7ff10c 0x4       mbox4 clr
0x1c7a5030 0x4       mbox4 init
```

---

**L997-1007｜★★★ SCP IPI 表不能随便删条目（极隐蔽）**
> `IPI_OUT_ELLIPTIC_ULTRA_0 / IPI_IN_ELLIPTIC_ULTRA_0 carry no Linux client, but they MUST stay in the tables: mbox_setup_pin_table() derives every pin's IRQ-status bit (pin_index) from the cumulative msg_size of the earlier pins on the same mailbox. Dropping them shifts the sensor pins from bits 17/18 to 12/13, so the stock SCP firmware's notification (bit 18) matches no pin and mtk_mbox_isr() dumps the whole pin table on every sensor message.`

**事实**：IPI pin 的 IRQ 状态位是**按前面所有 pin 的 msg_size 累加**算出来的。删掉任何一条（哪怕没有客户端）都会让后面所有 pin 的位号错位，导致中断匹配不上。

---

**L1030-1046｜SCP feature 频率表**
```
scp_feature_tbl = <feature, freq(MHz), coreid>
 0 vow 400/1    1 sensor 350/0   2 flp 26/0      3 rtos 700/0
 4 speaker 200/1  5 vcore 0/0    6 barge in 135/1  7 vow dump 10/1
 8 vow vendor M 80/1  9 vow vendor A 43/1  10 vow vendor G 22/1
11 vow dual mic 20/1  12 vow dual mic barge in 100/1  13 ultrasound 200/0

debug_timeout_reset = "enable";
```

---

**L1052-1072｜SCP 内存表**
```
scp_mem_tbl = <feature ID, size, alignment>
 0 secure dump (大小在 secure_dump_size)
 1 vow          0xca700
 2 sensor main  0x100000
 3 logger       0x180000
 4 audio        0x19000
 5 vow bargein  0xa000
 7 ultrasound   0x19000
 8 sensor supper 0x10000
 9 sensor list  0x1000
10 sensor debug 0x2000
11 sensor custom writer 0x100
12 sensor custom reader 0x100
13 elliptic ultrasound 0x8000

memorydump = <0x180000 l2tcm>, <0x03c000 l1c>, <0x003c00 regdump>,
             <0x000400 trace buffer>, <0x100000 dram>
```

---

**L1080-1090｜★★ 有意为之的地址重叠**
> `Shares 0x10005000 with pio (and the consys multi-reg) — same as stock, which also has both; unit-address/reg overlap by design.`
>
> `Shares 0x10001000 with infracfg_ao — expected DTC unique_unit_address warning (same precedent as the AFE pair).`

**事实**：MT6895 上 `0x10005000` 同时是 pio 和 consys multi-reg；`0x10001000` 同时是 infracfg_ao 和其他。DTC 会报 warning，是正常的。

---

**L1093-1095｜infracfg_ao / spm / topckgen 基址**
```
0x10001000  infracfg_ao
0x10006000  spm
0x10000000  topckgen
```

---

**L1111-1118｜SCP DVFS 参数表头**
```
/* vlp  vsram  dvfsrc_opp  spm_vcore  freq  mux  resource */
gpio-vreq = <0x480 0x7 20>;   /* SCP_VREQ_VAO */
```

---

**L1159-1165｜scpsys 提供的域**
> `MT6895 SCP/SPM power-domain controller (scpsys). Provides the DISP(19)/DISP1(20)/MM_INFRA(6) MTCMOS domains required by the display SMI/IOMMU/GCE subsystem. Clock phandles now match the downstream MT6895 DT where the corresponding mainline clock drivers exist.`

**注意编号**：`DISP = 19`、`DISP1 = 20`、`MM_INFRA = 6`。

---

**L1403-1409｜I2C5 挂什么**
> `i2c5 (I2C wrap C). Hosts the MT6375 PMIC (charger, fuel gauge, TCPC, ADC), the rt5133 LDOs, rt6160 buck and the LN8000 fast-charge pump. Stock compatible is "mediatek,mt6983-i2c"; mt6895-i2c is the mainline port of that gen (slave_addr_ver=1 -> SLAVE_ADDR1, fifo_size=16 -> FIFO for small transfers).`

**事实**：stock compatible 是 `mediatek,mt6983-i2c`；`slave_addr_ver=1` → `SLAVE_ADDR1`；`fifo_size=16`。

---

**L1423-1428｜MT6375 六个 I2C 地址**（见 5.2）

---

**L1461-1463｜MT6375 ADC 通道顺序**
> `iio channels, ordered to match the mt6375_adc_chan enum (CHGVIN, VSYS, VBAT, IBUS, IBAT, TEMPJC, TS, USBDP, USBDM, VREFTS)`

---

**L1595-1598｜I2C6 背光**
> `i2c6 (I2C wrap C). Hosts the KTZ8863A LCD bias/backlight IC. GPIO31=SCL6, GPIO32=SDA6 are muxed/powered in setup.c.`

---

**L1622-1635｜I2C7 与 bq28z610 的速度限制**
> `i2c7 (I2C wrap S). Hosts the TI bq28z610 fuel gauge (capacity), the LN8000 fast-charge pumps and sc8561. GPIO29=SCL7, GPIO30=SDA7 are muxed/powered in setup.c.`
>
> `400kHz: the bq28z610 is an SMBus part (NACKs at 1MHz)`

---

**L1658-1664｜I2C3 + LK 已经配好的引脚**
> `i2c3: SCL3 = GPIO129, SDA3 = GPIO130 (both already muxed to mode 1 by LK, with IES/PU/SMT configured), clock gate CLK_IMPS_AP_CLOCK_I2C3 (imp_iic_wrap_s bit2) comes up enabled. On xaga this bus carries the NXP SN110T NFC controller (vendor node compatible "qcom,nq-nci", reg 0x28).`

---

**L1701-1705｜I2C9 相机 PMIC + 电荷泵**
> `i2c9 (I2C wrap S). Hosts the camera PMIC (FAN53870) and the master charge pump (SC8551 on xaga / SC8561 on xagapro). GPIO131=SCL9, GPIO132=SDA9 are muxed/powered in setup.c.`

---

**L1720-1724｜I2C1 触觉 + audio switch**
> `i2c1 (I2C wrap S). Hosts the AW8697 haptic motor driver (aw8697@5a) and the et7480 audioswitch (0x42). GPIO8=SCL1, GPIO9=SDA1 are muxed/powered in setup.c (imps gate bit0).`

---

**L1825-1831｜触摸 pinctrl 移植来源**
> `NT36672C pin states, ported verbatim from downstream xaga-mt6895-touch.dtsi. The driver selects pmx_ts_active / pmx_ts_suspend around panel power cycles and pmx_ts_spimode at probe: INT (GPIO135) becomes output-low and CS (GPIO110) output-low + pull-down while the panel is down.`

**触摸引脚**：INT = GPIO135，CS = GPIO110。

---

**L1842-1847｜★ 触摸选屏（双供应商）**
> `xaga panel selection: the touch driver reads the panel's DDIC lockdown over DSI and matches lockdown[0] (tp-vendor byte) against cfg_0 (Tianma, 0x46, fw01) or cfg_1 (CSOT, 0x53, fw02).`

| 供应商 | lockdown[0] | 固件 |
|---|---|---|
| Tianma | `0x46` | fw01 |
| CSOT | `0x53` | fw02 |

---

**L1923-1947｜★★★ UFS 时钟必坑**（见 4.2 完整引用）

---

**L1954-1962｜★★★ UFS0_SHUTDOWN 域**（见 4.3 完整引用）

---

**L1988-1994｜按键控制器**
> `MTK keypad controller. Handles the volume-down key (hw-init-map index 0 = KEY_VOLUMEDOWN) wired to the KPD row/col matrix. Volume-up + power come from the MT6363 PMIC (see spmi/mtk-spmi-keys). Clocked by clk26m; the KPD block is in the always-on VLP domain (LK leaves it running).`

**按键划分**：音量-**下**走 KPD 矩阵；音量**上** + 电源走 MT6363 PMIC。

---

**L2012-2018｜SPMI 描述**
> `SPMI bus (pmif + spmi master). Compatible mt6895-spmi (same register layout as mt8195, soc_chan=2). Slave 4 is the MT6363 PMIC; the keys are polled directly (no SPMI-PMIC MFD with IRQ domain in mainline yet), so no interrupt-controller plumbing is needed here.`

---

**L2030-2031｜中断号**
> `rcs_irq: PMIC INT line wired to EINT-capable GPIO243 (matches downstream); pmif_irq: GIC SPI 414`

---

**L2222-2224｜VBUCK1 必须 always-on**（见 5.5）

---

**L2452-2460｜★★★ MT6685 提供 codec 的 26MHz CLKSQ**（见 1.8 完整引用）

---

**L2655-2657｜GPUEB mbox 参数**
```
mbox_size = <160>;   /* slots */
slot_size = <4>;     /* bytes */
ts_mbox = <0>;       /* mbox for timersync */
```

---

**L2712-2713｜GPUEB 内存表**
```
<0 0x1000>,   /* 4KB   MEM_ID_GPUFREQ */
<1 0x180000>; /* 1.5MB MEM_ID_LOG */
```

---

**L2717-2733｜★★★ GPU 兼容串/IRQ/MFG 十二域**（见 2.4 完整引用）

---

**L2741-2756｜★★★ GPU 三时钟与 coregroup SError**（见 2.4 完整引用）

---

**L2761-2763｜VSTACK**
> `VSTACK: MT6368 BUCK2, scaled with the stacks clock using the downstream gpufreq_mt6895 rate/volt pairs.`

---

**L2770-2776｜GCE 显示实例**（见 3.8）

---

**L2797-2801｜DRAMC**
> `DRAMC: read-only visibility into the DRAM controller (fmeter data-rate reporting, DDR type / mode-register info). Register layout and fmeter tuning values mirror the stock DT.`

**DRAMC 寄存器地图**（L2805-2821）：
```
0x10230000 0x2000  DRAMC AO CHA     0x10234000 0x1000  DRAMC NAO CHA
0x10240000 0x2000  DRAMC AO CHB     0x10244000 0x1000  DRAMC NAO CHB
0x10250000 0x2000  DRAMC AO CHC     0x10254000 0x1000  DRAMC NAO CHC
0x10260000 0x2000  DRAMC AO CHD     0x10264000 0x1000  DRAMC NAO CHD
0x10238000 0x2000  DDRPHY AO CHA    0x10236000 0x1000  DDRPHY NAO CHA
0x10248000 0x2000  DDRPHY AO CHB    0x10246000 0x1000  DDRPHY NAO CHB
0x10258000 0x2000  DDRPHY AO CHC    0x10256000 0x1000  DDRPHY NAO CHC
0x10268000 0x2000  DDRPHY AO CHD    0x10266000 0x1000  DDRPHY NAO CHD
0x10006000 0x1000  SLEEP BASE
```

---

**L2851-2856｜DVFSRC 工作原理**
> `The DVFSRC collects voltage and DRAM bandwidth requests from the multimedia blocks and drives the VCORE rail (MT6363 VBUCK2) itself. Clients request a voltage through their OPP table; the hardware serves the highest request.`

**硬事实**：VCORE = MT6363 的 **VBUCK2**；硬件服务**最高**请求。

---

**L2920-2926｜MMQoS**
> `MMQoS interconnect provider: collects SMI port bandwidth requests from the multimedia masters (camera, display, MDP, codecs) and drives the SMI BWL/OSTD settings and the DVFSRC EMI bandwidth requests. smi_larb6 and smi_larb19 do not exist on MT6895 and are omitted; smi_larb24 is unused.`

**事实**：MT6895 **没有** `smi_larb6` 和 `smi_larb19`；`smi_larb24` 存在但没用。

---

**L2930-2935｜★ fw_devlink 只认标准属性名**
> `The vendor consumers use #mtk-interconnect-cells, but fw_devlink only understands #interconnect-cells and uses it to order the multimedia consumers after this provider. Both describe the same one-cell layout.`

---

**L2983-2989｜两个 GCE 必须分开**（见 3.8）

---

**L3015-3022｜SMI 树声明顺序**
> `SMI (smart multimedia interface) common/sub-common tree for the display/MDP/sysram path, matching downstream MT6895. Suppliers are declared before consumers so the SMI driver can probe the whole tree without deferred-probe reordering. LARBs reference both their top-level common and their local display sub-common; the downstream SMI driver links both.`

---

**L3365-3373｜★★★ 相机 DFS 无电压缩放（实测报错原文）**
> `XAGA: vmm_proxy_label is a bare node (no compatible, no driver), so regulator_get() can never resolve it and the camsys ran "DFS without voltage scaling" - measured:`
> ```
>   mtk-cam 1a000000.camisp: can't get dvfsrc-vcore
>   seninf-core 1a010000.seninf_top: can't get dvfsrc-vmm
> ```
> `Point it at the real dvfsrc regulator, as the display/IPE consumers already do.`

---

**L3379-3390｜CAM_SEL 时钟树**（注释即时钟索引）
```
clocks: 0 CLK_TOP_CAM_SEL         names: "cam"
        1 CLK_TOP_MAINPLL_D4_D2          "TOP_MAINPLL_D4_D2"
        2 CLK_TOP_UNIVPLL_D4_D2          "TOP_UNIVPLL_D4_D2"
        3 CLK_TOP_UNIVPLL_D6             "TOP_UNIVPLL_D6"
        4 CLK_TOP_MAINPLL_D4             "TOP_MAINPLL_D4"
        5 CLK_TOP_UNIVPLL_D4             "TOP_UNIVPLL_D4"
```

---

**L5035｜显示全局 HRT 带宽投票**
> `PEARL: the display's global HRT bandwidth vote.`

---

**L5130-5137 / L5157-5164 / L5184-5191 / L5218-5225 / L5413-5420 / L5451-5458 / L5478-5485 / L5505-5512｜★★★ 同一段注释重复 8 次**
> `PEARL: without these the display never votes for bandwidth. of_mtk_icc_get() returns NULL when the node has no interconnects, so every mtk_icc_set_bw() call from the ovl driver is a no-op and the OVL underruns on every frame (that is the white screen). Ports come from this node's own iommus, the names from ddp_comp_str[] -- DECLARE_STR stringifies the enum.`

**重复 8 次说明作者被这个问题咬了 8 次。**

---

**L5865｜SMI table**
> `table0, video domain7, pseudo-larb33, port2`

---

**L5873｜VCP 不自动启动**
> `VCP remains auto_boot=false; codec opens explicitly boot it.`

---

**L5887｜编解码 SMI**
> `Both cores use table0/domain0; port list follows vendor port-def.`

---

**L5972-5974｜LVTS 时钟**
> `Fixed 26 MHz reference for the LVTS bus clock. The real CLK_IFRAO_THERM gate reads gated on this board while register access works regardless, so a fixed clock is enough.`

---

**L5981-5983｜★★ 热校准必须从 LK devinfo 取，不能读 efuse**
> `Bootloader device info table (/chosen/atag,devinfo). This is the authoritative LVTS calibration source on MT6895; the raw efuse window at the same offsets returns zeroed per-sensor counts.`

**硬事实**：MT6895 的 LVTS 校准数据只能从 `/chosen/atag,devinfo` 拿，**efuse 里同样的偏移读出来是全 0**。

---

**L5994-5995｜LVTS 两个 bank**
> `MT6895 LVTS: two register banks, AP at 0x10315000 (3 controllers, 7 lit sensors) and MCU at 0x10316000 (5 controllers).`

---

**L6093-6097｜HCP 通道**
> `Mainline port: MTK HCP (imgsys <-> userspace daemon message channel). The daemon (libimgsys_daemon.so startRED) opens /dev/mtk_hcp; the driver reserves the isp71 mblocks and relays GCE packets.`

---

**L6106-6134｜★★★ imgsys 移植的 7 条适配规则**
> ```
> Mainline port: imgsys (DIP) SMI sub-commons, LARBs and the imgsys_fw node, adapted from the downstream MT6895 DT.
>
> Mainline adaptations:
>  - soc@0 uses single-cell addresses/sizes, so reg is <0xADDR 0xSIZE>.
>  - &spm replaces the downstream &scpsys power-domain controller.
>  - &gce replaces the downstream &gce_m; the downstream &gce_m_sec
>    mailboxes do not exist on mainline and are dropped.
>  - &disp_iommu is the mainline display IOMMU (same port macros).
>  - Only the &imgsys_main_clk clock provider exists on mainline.  The
>    downstream dip_top_dip1/dip_nr_dip1/wpe1_dip1/wpe2_dip1/wpe3_dip1/
>    ipesys clock providers are not yet described in the mainline DT, so
>    only the &imgsys_main_clk clock entries are kept and clock-names is
>    trimmed to match.
>  - dma-ranges is restored with the 1/1 cell layout.  An entry on this bus is
>    3 cells, <child-addr parent-addr size>: child and size come from soc@0's
>    single cells, the parent address from &soc's parent.  The downstream DT
>    carries the 6-cell <2,2,2> form for a 16 GiB identity window
>    (0x4_0000_0000); with a single size cell the maximum expressible is
>    0xffffffff, i.e. a 4 GiB identity window, which is exactly the aperture
>    these ports get from the IOMMU.  Dropping it leaves the device's
>    dma_range_map empty, so phys_to_dma() reports DMA_MAPPING_ERROR for every
>    address.
>  - operating-points-v2 and interconnects/interconnect-names are restored:
>    opp_table_img/opp_table_ipe are provided above and the mmqos provider
>    exists, so imgsys MMDVFS and its per-port QoS votes now work.
>  - dvfsrc-vmm-supply is kept because vmm_proxy_label exists on mainline.
> ```

**其中 `dma-ranges` 那条最值钱**：单 cell 的 size 最大只能表达 4GiB，正好等于 IOMMU 给的 aperture。**不写这一条，`phys_to_dma()` 会对所有地址报 `DMA_MAPPING_ERROR`。**

---

**L6290-6300｜★★★ 缺 ME 平台设备的三个后果**
> ```
> XAGA: the ME (IPE motion estimation) platform device.  Without this
> node ipesys_me_probe() never runs, so:
>   - me_dev stays NULL and ipesys_me_debug_dump() NULL-derefs inside
>     the CMDQ timeout workqueue (the oops at ipesys_me_debug_dump+0x38,
>     call trace imgsys_cmdq_task_cb -> cmdq_thread_handle_timeout_work),
>     which aborts the timeout cleanup and wedges the pipe;
>   - M4U_LARB12_PORT4/PORT5 are never registered in disp_iommu, so the
>     ME's RDMA/WDMA have no bank and issue IOVA 0;
>   - the ME engine's own CQ-done (event 221 / hw_comb 0x800) never fires.
> ```

---

**L6330-6348｜imgsys 寄存器地图**（全注释）
```
 0 0x15000000 0x4000   IMGSYS_TOP
 1 0x15020000 0x10000  IMGSYS_TRAW
 2 0x15040000 0x10000  IMGSYS_LTRAW
 3 0x15640000 0x10000  IMGSYS_XTRAW
 4 0x15100000 0x10000  IMGSYS_DIP
 4 0x15150000 0x10000  IMGSYS_DIP_NR
 5 0x15210000 0x10000  IMGSYS_PQDIP_A
 6 0x15510000 0x10000  IMGSYS_PQDIP_B
 7 0x15200000 0x10000  IMGSYS_WPE_EIS
 8 0x15500000 0x10000  IMGSYS_WPE_TNR
 9 0x15600000 0x10000  IMGSYS_WPE_LITE
10 0x15220000 0x00100  IMGSYS_WPE1_DIP1
11 0x15320000 0x10000  IMGSYS_ME
12 0x15005000 0x01500  IMGSYS_ADL_A
13 0x15007000 0x01500  IMGSYS_ADL_B
14 0x15520000 0x00100  IMGSYS_WPE2_DIP1
15 0x15620000 0x00100  IMGSYS_WPE3_DIP1
16 0x15110000 0x00100  IMGSYS_DIP_TOP
17 0x15130000 0x00100  IMGSYS_DIP_TOP_NR
```

---

**L6587-6596｜★★★ imgsys QoS 缺 path = 完全不投带宽（带实测值）**
> ```
> XAGA: the driver's imgsys_qos_path[] table has
> IMGSYS_M4U_PORT_MAX (=120) entries and resolves each
> by name in mtk_imgsys_mmqos_init(); without these two
> of_mtk_icc_get() returns ERR_PTR(-ENODATA) and
> mtk_imgsys_mmqos_set_by_scen() then calls
> mtk_icc_set_bw(ERR_PTR(-61), ...) so the imgsys votes
> no DRAM bandwidth at all (observed: "path=ffffffffffffffc3,
> bw=0/0").  Cell values taken from the vendor DT.
> ```

**实测输出**：`path=ffffffffffffffc3, bw=0/0` —— **-61 = -ENODATA 被当指针用**，所以一次带宽都没投。

---

**L6730-6755｜★★★ SMI larb port 必须全开**（见 3.7 完整引用）

### 6.2 `mt6895-xiaomi-xaga.dts`（板级）

---

**L25｜I2C 编号**
> `match stock bus numbering so mt6375 etc. land on i2c-5`

---

**L35-83｜★★★ cmdline 的四个关键决策（整份文档信息密度最高的一段）**

> `XAGA cmdline: setup.c re-reads /chosen/bootargs from THIS embedded DTB (early_init_dt_scan_chosen) because LK passes its own baked-in cmdline (setup_machine_fdt captured it before the DTB override) and ignores boot.img / vendor_boot cmdlines.`

**事实**：LK 传的是自己烧进去的 cmdline，`setup_machine_fdt` 在内核拿到覆盖后的 DTB 之前就抓了它，所以 boot.img/vendor_boot 的 cmdline 全被忽略。必须从内嵌 DTB 的 `/chosen/bootargs` 重新读。

> `printk.devkmsg=on: /dev/kmsg writes default to a 10-per-5s ratelimit that was silently dropping cinit's bursty GPT/diag output (also patched in kernel/printk/printk.c default).`

> `NOTE: console=ttyGS0 is DELIBERATELY absent — its CON_PRINTBUFFER console kept replaying the whole ring (USB re-enumeration), looping the log and stalling boot. The cinit shell still works (it writes directly to /dev/ttyGS0, no console param needed).`

**这条很反直觉**：`console=ttyGS0` 绝对不能加，因为 USB 重新枚举时 `CON_PRINTBUFFER` 会重放整个 ring → 日志循环 → 启动卡死。

> `clk_ignore_unused: REMOVED (2026-09-14) so the clock core gates bootloader-left clocks again. The flag suppressed the only late_initcall pass that does that, leaving ~414 hw-enabled clocks with no Linux consumer.`

> `Wiring the drivers to own their clocks (mt6895.dtsi: ufsci clocks, gpu coregroup) fixed the two casualties that pass caused directly -- without them clk_disable_unused() cuts ufs_sel (taking ufs_ck with it) and mfgcfg_bg3d, so UFS never enumerates and panthor's FW ping takes an SError.`

**这段把 UFS 和 GPU 的两个坑直接串起来了**：`clk_disable_unused()` 会关掉 `ufs_sel`（连带 `ufs_ck`）和 `mfgcfg_bg3d` → UFS 枚举不出来 + panthor 的 FW ping 收 SError。

> `Remaining problem, still under investigation: the gating pass cuts ~272 gateable clocks that no driver claims (cam 67, aud 40, mdp/mdp1 56, img 18, mm 31, vde/ven2 20, plus one-off top muxes). Those deferrals make fw_devlink_drivers_done() walk the devlink class list in a state it does not survive -- it Oopses in class_for_each_device()/klist_next() at late_initcall+CONFIG_DRIVER_DEFERRED_PROBE_TIMEOUT (10 s) and never releases device_links_write_lock(), which stalls every later device-link/PM user (WiFi's runtime-PM power-on, then panthor). deferred_probe_timeout=0 is a diagnostic to confirm that: deferred_probe_initcall() only schedules the crashing work when the timeout is > 0.`

**未解决的问题（重要）**：约 272 个没人认领的 gateable clock 被关掉后，`fw_devlink_drivers_done()` 在 10 秒超时点 Oops（`class_for_each_device()`/`klist_next()`），且**不释放 `device_links_write_lock()`** → 之后所有 device-link/PM 用户全卡住。

> `Capture the dead boot with read-lastboot-log.sh (XAGR ring in log_store at 0x7ffbf000, copied to expdb by LK); cinit also dumps the whole 2 MB kernel log to vendor_boot_b, readable with read-cinit-dump.sh. ramoops now lives in LK's own 0x48090000 window (inherited from LK's cmdline), which stock keeps across a hard reset.`

**调试手段**：XAGR ring 在 `log_store = 0x7ffbf000`，LK 拷到 expdb；ramoops 在 `0x48090000`。

> `spidev.bufsiz=65536: the FPC sensor streams a whole 112x88x2 (or 176x112) frame as one READ_IMAGE transfer, which is far more than spidev's 4096-byte default message limit. The parameter is S_IRUGO (read-only), so it must be set here.`

---

**L85-95｜★★★ LK 的 videolfb tag**（见 3.7 完整引用）

---

**L111-121｜simple-framebuffer 位置**（见 3.7）

---

**L157-163｜保留内存**
> `Full MT6895 reserved-memory layout, taken verbatim from the live device tree (/sys/firmware/fdt) of the running stock Android build. These regions are owned by ATF/TEE/SCP/SSPM, modem, GPU and the display; the kernel MUST NOT allocate from them or early boot crashes on memory corruption.`

---

**L284-304｜★★★ 8GiB vs 12GiB 的地址不能硬编码**
> ```
> Bootloader firmware window at the top of DRAM:
>   security_tee_ree   end-16M .. end-4M
>   security_tee_log   end-4M  .. end-4M+128K
>   me_cmdq_reserved   end-2M  .. end-1M
>   dramc-rk1          end-8K
>
> The absolute base is RAM-size dependent: 0x23f000000 on the 8 GiB
> board, 0x33f000000 on the 12 GiB board (both are
> memblock_end_of_DRAM() - 16 MiB).  Do NOT hardcode it.  Linux's
> early_pgtable_alloc() also allocates top-down from the end of DRAM,
> so the live linear-map L3 tables land inside this window and an
> MDP/GCE DMA out of me_cmdq_reserved overwrites them.  The old
> hardcoded 0x23f...... values (a snapshot of the 8 GiB stock DT) left
> the 12 GiB board's window unreserved, which corrupted page tables
> only after heavy buffered I/O had populated the linear map.
>
> A dynamic region (size/alignment, no reg) is placed by memblock,
> which allocates top-down by default, so it lands exactly on
> memblock_end_of_DRAM() - 16 MiB for either RAM size.
> ```

**两个硬事实**：
1. 固件窗口基址 = `memblock_end_of_DRAM() - 16 MiB`，8GiB 板是 `0x23f000000`，12GiB 板是 `0x33f000000`
2. Linux 的 `early_pgtable_alloc()` **也是从 DRAM 尾部往下分配**，所以活的 linear-map L3 页表就落在这个窗口里；MDP/GCE 从 `me_cmdq_reserved` DMA 出去会**覆盖页表**
3. 正确做法：用**动态 region（size/alignment，无 reg）**，让 memblock 自己从顶部往下放

---

**L343-349｜no-map 的必要性**
> `no-map: this region (which contains the live LK scanout framebuffer at 0xff5c3000) must be excluded from the kernel's RAM map. Otherwise simplefb's request_mem_region()/ioremap() fail (-ENOMEM, "cannot reserve video memory" / ioremap_p2p WARN).`

---

**L368-377｜ramoops 窗口**
> `896 KiB hole between LK's aee_debug_kinfo (0x48080000..0x4808ffff) and minirdump (0x48170000..0x4817ffff). LK points ramoops here (ramoops.mem_address=0x48090000) and the window is what stock preserves across a hard reset. Keep it no-map so Linux never allocates it, but deliberately give it no "ramoops" compatible: the layout is driven by LK's command line, inherited in setup.c.`

**ramoops 窗口**：`0x48090000` 起，896 KiB（到 `0x48170000`）。

---

**L411-445｜固件区补洞（含具体地址）**

```
xaga-firmware-window（首块）:
  SSPM AP shared memory  +  GPUEB shared memory 下半窗口
Unmap2 gap: 原始 GZ log 与 MCUPM 之间的空隙
第二块: 9 MiB GZ 区尾部，接到已有 SSPM 节点
LK debug MET buffer
```

> `dramc-rk0 (rank-0 calibration scratch, 8 KiB at the end of rank 0) is covered by xaga-firmware-window on the single-rank 8 GiB board. On the 12 GiB board rank 0 ends at 0x1c0000000; that copy is written once by the bootloader and never touched at runtime, so it needs no reservation here.`

---

**L448-453｜WiFi 保留内存**
> `MediaTek CONNAC2X2 SOC7_0 connectivity: consys / wifi / connfem. Nodes and properties copied from downstream mt6895.dts and cust_mt6895_connfem.dtsi. memory-region references are added so the downstream fullmac driver can find the reserved EMI/DMA areas without depending on LK runtime fixups.`

---

**L617-620｜★★ GPUEB 共享内存地址**
> `GPUEB shared memory: the live FDT reserves 2MB at 0x7c600000 as mblock-38-me_GPUEB_SHARED. gpueb_reserved_mem.c can use it directly when gpueb_mem_addr/size are present on the gpueb node.`

**`MEM_ID_GPUFREQ` 的物理基址 = `0x7c600000`**（整个 2MB 窗口里）。

---

**L630-634｜显示驱动切换**
> `The mediatek_v2 display driver (binds on dispsys_config@14000000) is now active. The LK frame stays visible via simplefb above until the DRM driver takes over. Keep simplefb enabled during bring-up as a fallback.`

---

**L636-645｜USB OTG 手动切**（见 5.2）

---

**L829-868｜★★ KTD2687 闪光灯的 7-bit 溢出**
> ```
> Kinetic KTD2687 dual-channel flash LED driver (rear camera flashlight).
>
> Both current outputs drive the same two-string flashlight package and the
> downstream driver always programs the same current into both channels, so
> the chip is modelled as a single LED here.  HWEN is GPIO119, held high by
> the "hwen_high" pinctrl state (the chip ignores I2C while HWEN is low).
>
> Torch brightness in the downstream stack is a *combined* current in
> microamperes: the MediaTek core takes the total and the driver programs
> half of it into each string.  Android's camera HAL advertises 1..377 on
> that scale (default 250), which is exactly where the 7-bit per-string
> torch register (0x7f = 188.5 mA) saturates - at 378 the shifted value
> overflows the field and wraps the register back to 0, i.e. the torch goes
> dark.  led-max-microamp below is that same combined full-scale, so the
> LED class exposes the identical 1..377 numbering to userspace.
> ```

**硬事实**：
- HWEN = GPIO119，低电平时芯片完全不理 I2C
- 手电筒电流是**双串合计**，每串写一半
- 每串 7-bit 寄存器（`0x7f` = 188.5 mA）在 **378** 时溢出回 0 → **手电筒变黑**
- 所以 Android HAL 的上限是 377

> `/* 377 x 1000 uA combined, i.e. 188.5 mA into each of the two strings. */`
> `/* Flash strobe: 1.5 A combined (750 mA per string, register 0x3f). The chip itself can reach 3 A combined; raise this if captures need more. */`
> `/* Hardware flash safety timer range, 10 ms .. 400 ms. */`

**闪光**：1.5A 合计（750mA/串，寄存器 `0x3f`）；芯片上限 3A 合计；安全定时器 10–400ms。

---

**L876｜AFE pinctrl 来源**
> `Audio AFE pinmux / pinctrl states (from downstream xiaomi-mt6895-common.dtsi)`

---

**L1045-1050｜★★ pinctrl 不能叠加**
> `Combined MTKAIF states: pinctrl_select_state() is exclusive, so the per-group states used downstream cannot be stacked one after another on kernels where switching away actively restores released pins. Apply the whole interface with a single state instead.`

---

**L1081-1082｜★ DL 数据道不能掉**
> `Keep the DL data lanes (incl. the wired MOSI2) alive so an UL request cannot strip them from a concurrent playback stream.`

---

**L1120-1121｜★★★ MOSI2 是 xaga 的实际数据通路**（见 1.3）

---

**L1214-1233｜★★★ MT6319 CPU bucks + 30 秒关电陷阱**（见 5.6）

---

**L1270-1276｜★★★ ISP/CAM vcore 30 秒后被关**
> `Supplies the ISP/CAM vcore rail. Without this the regulator core cuts it 30s after boot (regulator_init_complete), isp_main and cam_main then lose PWR_ACK and every camera register window stops decoding.`

**后果链**：30 秒后 vcore 被关 → `isp_main`/`cam_main` 丢 `PWR_ACK` → **所有相机寄存器窗口停止解码**。

---

**L1311-1314｜快充管理器**
> `Fast-charge manager. Consumes the TCPM PPS/APDO power supply and drives the SC8551 charge pumps within the xaga power limits.`

---

**L1334-1338｜双板共 DTB**
> `xagapro envelope (SC8561 4:1 pumps). The board DTB is shared, so the driver selects this child node when the LK hwid sku reads "xagapro".`

---

**L1355-1361｜电荷泵地址**（见 5.3）

---

**L1428-1434｜NFC**
> `NXP SN110T (NFC + eSE) on i2c3 at 0x28. Vendor describes it as "qcom,nq-nci" with nq-nci.ko behind /dev/nq-nci; we bind the in-tree netlink driver for now to see how far an NCI 2.0 part gets.`
> `Pins from cust_mt6895_nfc_nxp.dtsi: GPIO18 = VEN, GPIO128 = FWDN, GPIO143 = IRQ, GPIO194 = CLK_REQ (SRCLKENAI0, not wired up yet).`

**NFC 引脚**：VEN=GPIO18, FWDN=GPIO128, IRQ=GPIO143, CLK_REQ=GPIO194（**未接**）。

---

**L1450-1461｜★★ SCP 传感器 + 坐标系修正**
> `SCP sensor hub. This node has no MMIO resources of its own: the SCP owns the physical ALS (tcs3408) and IMU (lsm6dsoq) and streams samples over IPI, so the transport driver binds here only to pick up the board's sensor mounting description.`
>
> `In the IIO/Android device frame +x is to the right of the screen, +y towards the top and +z out of the screen. The lsm6dsoq package on xaga is mounted rotated 180 degrees about the screen normal relative to that frame, which made portrait report as reverse-portrait and landscape as reverse-landscape, so x and y are negated here.`

**传感器**：ALS = `tcs3408`，IMU = `lsm6dsoq`（都挂在 SCP 上，走 IPI，无 MMIO）。
**★ 安装方向**：lsm6dsoq 绕屏幕法线**旋转 180°**，所以 x/y 要取反。

---

**L1474-1484｜★★ 指纹双供应商**
> ```
> Fingerprint sensor.
>
> xaga/xagapro ship with either an FPC or a Goodix side-mounted sensor
> behind the same SPI3_A pads. The pads are muxed to the hardware MTK
> SPI3 controller, which is bound by the fpc1540 SPI driver (FPC part).
> Pins and rails match the downstream crdroid xaga device tree:
>   reset = GPIO216, IRQ = GPIO136
>   SPI3_A: CLK=GPIO217, CS=GPIO218, MI=GPIO219, MO=GPIO220
>   FPC rail = mt6368_vibr @1.8V, Goodix rail = mt6368_vfp @3.3V
> ```

**指纹引脚（硬事实）**：
| 信号 | GPIO |
|---|---|
| reset | 216 |
| IRQ | 136 |
| CLK | 217 |
| CS | 218 |
| MI (MISO) | 219 |
| MO (MOSI) | 220 |

**电源**：FPC 用 `mt6368_vibr` @1.8V；Goodix 用 `mt6368_vfp` @3.3V。

---

**L1511-1516｜SPI3 控制器**
> `Hardware MTK SPI3 controller (spi3@11013000, enumerated as spi0). The four SPI3_A pads are muxed to the controller via fp_pins_default. The FPC1540 sensor driver owns power, reset, IRQ and the SPI protocol; finger-detect IRQ is GPIO136 and the 1.8 V rail is mt6368_vibr.`

---

**L1536-1541｜★★ 电源键长按 = 冷复位（ramoops 会坏）**
> `Power key: the PMIC's single-key long-press reset is a cold reset (DRAM is not kept refreshed, so ramoops comes back corrupted). Keep the PON reset restricted to power+homekey so it cannot fire on its own, and let mtk-spmi-keys do a warm emergency restart after 9 s instead.`

---

**L1546-1551｜★★★ WDT 外部复位会腐蚀 ramoops**
> `The WDT's external reset output is wired to the PMIC and turns an AP_WDT timeout into a full cold reset, which is what rots the ramoops window (~13% of bits) before pstore can be read. Keep the reset internal to the SoC so LK sees a normal warm AP_WDT reboot and the DRAM contents survive.`

**硬事实**：WDT 的外部复位输出接到 PMIC，AP_WDT 超时会变成**整机冷复位**，把 ramoops 窗口**腐蚀约 13% 的位**。必须让复位保持在 SoC 内部。

---

## 7. 对 HakiOS 的直接可用点（按优先级）

### P0 — 立刻能用，影响「能不能起来」

| # | 内容 | 出处 |
|---|---|---|
| 1 | **MFG_TOP 全套上电寄存器**：AOC2.0 的 SPM 信号量忙等（`0x1c001000+0x6ac` bit0）、清 `+0xf30` bit9/10、PDC 的 17+11 个寄存器、ACP/HWDCM/GPM 的精确值 | `clk-mt6895-mfgcfg.c:55-232` |
| 2 | **PDC 绝对不能早开**：开了 GPU 会接管 MFG4-12 并因无 shader 请求把它们关掉，卡在 `0x100d` | `clk-mt6895-mfgcfg.c:209-214` |
| 3 | **GPUEB 上电 IPI 只需两条命令**：`CMD_INIT_SHARED_MEM(0)` → `CMD_POWER_CONTROL(6)`，地址 `0x13c40000`，mbox0 `0x13c5fd80`，set/clr/send/recv `0x13c62004/74/00/78`，IRQ SPI 516 | `gpueb_power.c:70-113, 158-196`、`mt6895.dtsi:2650-2715` |
| 4 | **`MEM_ID_GPUFREQ` 在 `0x7c600000`**，前 `0x400` 是 32×u32 的 `gpufreq_shared_status`，后 `0xC00` 是 debug | `xaga.dts:617-620`、`gpueb_power.c:97-101` |
| 5 | **MFG PLL 的 VCO 下限必须是 2000MHz**，否则 852MHz 算出 postdiv=2（VCO=1704M）锁不住；正确是 postdiv=4 → VCO=3408M | `clk-mt6895.c:2721-2727` |
| 6 | **MFG 十二个电源域层级 + 每域 ctl_offs**（mfg1=0xEBC … mfg12=0xEE8，sta_mask=GENMASK(31,30)，sram_pdn=bit8，ack=bit12） | `mtk-scpsys-mt6895.c:462-568, 597-607` |
| 7 | **panel init 完整命令表**（含 `0x11`/`0x29` 和 70ms 延时）与 reset 1→0→1 时序 | `panel-l16-36-02-0b-dsc-vdo.c:206-318` |
| 8 | **DSI 写命令的 0xB0 分界**：`< 0xB0` 走 DCS write，`>= 0xB0` 走 generic write | `panel-l16-36-02-0b-dsc-vdo.c:110-121` |
| 9 | **LK videolfb tag 的结构与值**（`fb_base=0xfe14f000`, `islcmfound=1`, `fps=6060`, `vram=0x1eb0000`, lcmname）；没有它显示管线会在 LK 那一帧之后卡死 | `xaga.dts:85-95` |
| 10 | **UFS 无 PHY 节点**（PHY 由 LK 配好），且 `ufs_sel`/`aes_ufsfde_sel` 被 `clk_disable_unused()` 关掉会**当场杀 UFS**（`CLK_CFG_14` bit7） | `mt6895.dtsi:1919-1947` |

### P1 — 耳机/音频（用户最关心）

| # | 内容 | 出处 |
|---|---|---|
| 11 | **耳机三条 mixer**（`HPL/HPR Mux` = `Audio Playback` + `Headset Volume 20,20` + **`ADDA_DL_CH3/CH4`**）；CH3/CH4 是 xaga 实际通路，只开 CH1/CH2 没声音 | `HiFi.conf:45-54`、`xaga.dts:1120-1121` |
| 12 | **`mtk_hp_enable` 的 37 步寄存器序列**，含 `CON20=0x87`、`CON2` 的 `0x0c→0x3c→0xfc→0xff→0xcf→0x03`、`CON0=0xc0→0xf0→0xff`、两次 `usleep(100,120)` | `mt6368.c:1102-1263` |
| 13 | **耳机寄存器基址**：AUDDEC `0x2588`（CON0）步进 1；ACCDET `0x2688`；AFE `0x2388` | `mt6368.h:378, 435, 125` |
| 14 | **accdet 走 PMIC EINT0**（不是 AP GPIO），因为 `headset-use-ap-eint=0` | `mt6895.dtsi:2163`、`mt6368-accdet.c:2653-2662` |
| 15 | **AB 状态判定表**：`AB=00`→无麦耳机，`AB=01`→带麦耳机，`AB=11`→拔出；读 `CON46(0x26b6)` bit[7:6] | `mt6368-accdet.c:2023-2155`、`mt6368.h:4993-4996` |
| 16 | **按键阈值**（3键 `<0 0x50 0xdc 0x190>`、4键 `<0 0x3a 0x79 0xc0 0x190>`）与映射（BTN_0=MEDIA, BTN_1=NEXT, BTN_2=PREV, BTN_3=VOICE） | `mt6895.dtsi:2167-2169`、`mt6368-accdet.c:3291-3294` |
| 17 | **内置麦与耳机麦共用一条 pipe**，不能同时录立体声；靠 `PGA_L_Mux` 切 AIN0/AIN1 | `quirks/README.md`、`xaga-mic-switch.c` |
| 18 | **mic bias = 2.8V**（`mic_vol=8` → `AUDMICBIAS1HVEN |= 3<<shift`）；**mic mode 2 = DCC 无内部 bias** | `mt6368-accdet.c:3038-3087` |
| 19 | **MTKAIF 引脚表**（CLK=202, SYNC=203, MOSI0/1/2=204/205/**206**, MISO0/1=209/210），且 DL 道不能被 UL 请求剥掉 | `xaga.dts:1053-1135` |

### P2 — 显示/存储/电源

| # | 内容 | 出处 |
|---|---|---|
| 20 | **crtc_enable 15 步**，特别是第 4 步「先起 trigger loop 保 GCE 活着」 | `mtk_drm_crtc.c:6074-6209` |
| 21 | **显示每个 DDP 组件必须有 `interconnects`**，否则带宽投票是 no-op → OVL 每帧 underrun → **白屏** | `mt6895.dtsi:5130-5137` ×8 |
| 22 | **DVFSRC 真 regulator 会挂死板子**（作者挂过两次）；固定 regulator 顶替即可 | `mt6895.dtsi:528-538` |
| 23 | **`regulator-fixed` 的 min 必须 == max**，否则 probe 失败 → `dispsys_config` 永久 deferred → 白屏 | `mt6895.dtsi:542-548`、`:437-446` |
| 24 | **SMI larb 每个 port 都要列**，否则 `SMI_LARB_MMU_EN=0`，IOVA 被当裸物理地址用 | `mt6895.dtsi:6730-6755` |
| 25 | **两个 GCE 必须分开**（显示 `0x1e980000` / 多媒体 `0x1e990000`），mbox 线程 0..6,22 归显示 | `mt6895.dtsi:2983-2989` |
| 26 | **MT6375 OTG regulator 参数**：4.85V 起、25mV 步进、47 档、enable=`CHG_TOP1` bit2、V=`0x131` mask 0x3F、C=`0x132` mask 0x07 | `mt6375-charger.c:822-838` |
| 27 | **xaga 双 SC8551A 电荷泵**：主 I2C9 `0x66`，从 I2C7 `0x66`（xagapro 是 SC8561 `0x64`/`0x65`） | `xaga.dts:1355-1361` |
| 28 | **SC8561 latch 陷阱**：latch 后 enable 位仍是 1，只能靠「开了但不拉电流」判断 | `xaga_cp_manager.c:49-56` |
| 29 | **所有关键 rail 必须 `regulator-always-on`**：DT 系统上 regulator core 会在 **30 秒后**关掉没人用的 regulator（VBUCK1 显示关键、ISP/CAM vcore、MT6319 CPU bucks） | `xaga.dts:1214-1233`、`mt6895.dtsi:2222`、`:1270-1276` |
| 30 | **WDT 外部复位 → 冷复位 → ramoops 腐蚀 13%**；电源键长按也是冷复位。必须让复位留在 SoC 内 | `xaga.dts:1536-1541`、`:1546-1551` |
| 31 | **LVTS 校准只能从 `/chosen/atag,devinfo` 读**，efuse 同偏移是全 0 | `mt6895.dtsi:5981-5983` |
| 32 | **固件窗口基址 = `memblock_end_of_DRAM() - 16MiB`**（8G→`0x23f000000`，12G→`0x33f000000`），不能硬编码；且 Linux 的页表也从这里往下长，会被 DMA 撞坏 | `xaga.dts:284-304` |

### P3 — 需要时才用

| # | 内容 | 出处 |
|---|---|---|
| 33 | GPU 工作频率表（GPU 6 档 / STACK 40 档）与 STACK 电压配对表（219M@500mV → 860M@750mV） | `panthor_devfreq.c:41-119` |
| 34 | `CMD_COMMIT` 的索引是 **EB 工作表**索引，不是 DT OPP 索引 | `panthor_devfreq.c:105-115` |
| 35 | 升降频顺序：**升频 GPU 先行、降频 STACK 先行**；电压领先上升、滞后下降 | `panthor_devfreq.c:197-229, 271-301` |
| 36 | GPUEB 模式下不能 autosuspend（会 CSG 超时），也不能信 `clk_get_rate()` | `panthor_device.c:317-323`、`panthor_devfreq.c:315-320` |
| 37 | `coregroup` 时钟（`mfgcfg` 0x0/0x4/0x8 bit0）被关掉会让 `panthor_fw_ping_work()` 收 SError | `mt6895.dtsi:2747-2756` |
| 38 | UFS reset 顺序：assert hci→crypto→unipro→mphy，deassert unipro→crypto→hci→mphy（不对称） | `ufs-mediatek.c:203-222` |
| 39 | 触摸双供应商判定：lockdown[0] `0x46`=Tianma(fw01) / `0x53`=CSOT(fw02) | `mt6895.dtsi:1842-1847` |
| 40 | 指纹 SPI3 引脚全表 + 双电源（vibr@1.8V / vfp@3.3V） | `xaga.dts:1474-1484` |
| 41 | MT6375 六个 I2C 从地址（0x34/0x4e/0x1a/0x4a/0x64/0x3f） | `mt6895.dtsi:1423-1428` |
| 42 | KTD2687 闪光灯 7-bit 溢出点 = 378（>377 手电变黑），HWEN=GPIO119 | `xaga.dts:829-868` |
| 43 | IMU（lsm6dsoq）安装时绕 Z 轴转 180°，x/y 需取反 | `xaga.dts:1450-1461` |
| 44 | SCP IPI pin 的 IRQ 位号是按前面 pin 的 msg_size 累加的，删条目会让后面全错位 | `mt6895.dtsi:997-1007` |
| 45 | gpu 固件版本：`arm/mali/arch10.8/mali_csffw.bin` | `initramfs/root/lib/firmware/` |

---

## 8. 未确认 / 没找到

以下是**我在本树里没找到、或找到但不能确定**的：

1. **`mt6895_mfg_bringup_enable()` / `mt6895_mfg_pdc_enable_now()` 的调用者不存在**
   全仓 grep 只有定义（`clk-mt6895-mfgcfg.c:173`、`:236`）和 `EXPORT_SYMBOL_GPL`，**没有任何调用点**。可能是给 out-of-tree 模块用的，也可能是我没覆盖到的路径。
   → 但函数体的寄存器操作本身是完整且可用的。

2. **`drivers/gpu/mediatek/gpueb/gpueb_reserved_mem.c` 里 `MEM_ID_GPUFREQ` 的实际物理地址来源没完全追**
   dts 里没有给 `gpueb_mem_addr`/`gpueb_mem_size`（只有 `gpueb_mem_table` 描述大小）。`xaga.dts:617-620` 的注释说保留区在 `0x7c600000`，但代码里怎么从保留内存拿到这个地址，我只读到 `gpueb_get_reserve_mem_phys_by_name()` 这个接口，没读它内部实现。

3. **`gpueb_plat_service.c` 被注释掉了**
   `gpueb_init.c:167-171` 里 `gpueb_plat_service_init()` 整段注释。不确定是否影响功能（当前 GPU Works，所以大概不影响）。

4. **UFS 的 PHY 具体寄存器配置完全未知**
   dtsi 里没有 ufsphy 节点，驱动也不碰。所以「LK 到底把 UFS PHY 配成了什么」在本树里**没有任何信息**。这对 XNU 是个缺口——我们得从 LK 或下游内核拿。

5. **UFS 是否真的工作在 HS-G4 / 几 lane，没找到**
   `freq-table-hz = <0 0>,<0 0>,<0 0>,<0 0>` 全是 0，注释说"don't touch the rate"。没有 gear/lane 信息。

6. **`mediatek_v2` 显示栈我没读完**（133 个文件）
   只精读了 `mtk_drm_crtc.c` 的 crtc_enable、`mtk_drm_drv.c` 的 ddp 组件表、`mtk_disp_dsc.c`/`mtk_disp_ovl.c`/`mtk_disp_rdma.c` 的 XAGA 诊断打印。**DSC 的 PPS 打包细节、OVL 的 layer 配置、RDMA 的 FIFO 配置都只看了皮毛。**

7. **CMDQ 的指令集 / GCE 线程优先级没读**
   `drivers/misc/mediatek/cmdq/` 没深入。`mtk-cmdq-mailbox.c` 只扫了文件名。

8. **WiFi / BT（CONNAC2X2 SOC7_0）完全没深入**
   dts 里有完整节点（`xaga.dts:448-805`，含 ConnFem ANTSEL 映射表），但驱动（`drivers/net/wireless/mediatek/` 之类）我没读。任务清单里也没要求，但它是 Works 的功能之一。

9. **相机（imgsys）只读了注释，没读驱动**
   `mt6895.dtsi` 里关于 imgsys 的注释极其丰富（L6093-6755），但 `drivers/media/platform/mediatek/` 我没打开。Camera 是 Broken，所以优先级低。

10. **`stack_max_rate` 的两段注释互相矛盾，我没能确定最终结论**
    - `panthor_devfreq.c:21-25`：没有 regulator 控制时不能超过 bootloader 留的 368MHz 电压包络
    - `panthor_devfreq.c:26-33`：默认 852MHz，因为 EB 会自己做 AVS trim + VSRAM 时序
    后一段应该是后来的结论（有了 EB 之后），但两段都留在代码里。

11. **GPU 的 `power-domains` 属性在最终 gpu 节点里不存在**
    注释说 "Real power management stays with gpueb; no power-domains."（`mt6895.dtsi:2745`），但另一段注释又说 downstream 把十二个域挂在 GPU 节点上。**本树最终选择了不挂**，靠 scpsys 的 `BYPASS_INIT_ON` 自动上电。这个设计是否能覆盖 suspend/resume，我没验证。

12. **`gpueb_timesync.c` / `gpueb_hwvoter_dbg.c` / `gpueb_debug.c` / `gpueb_gpumpu.c` / `gpueb_logger.c` 只扫了文件名和行数，没精读。**

13. **accdet 的 moisture（进液）检测路径完全没读**
    `mt6368-accdet.c` 里有大量 `moisture_*` 函数（`config_moisture_detect_1_0`、`adjust_moisture_setting` 等），dts 里 `moisture_detect_enable` 我没确认 xaga 的值。

14. **`et7480` audio switch 驱动没找到**
    dts 注释说 I2C1 0x42 是 et7480 audioswitch（`mt6895.dtsi:1722`），但我在 `sound/soc/codecs/` 里没看到对应驱动文件。可能没移植。

15. **`vdd_gpu` 的真实 VGPU 电压值未知**
    dtsi 里是固定 stub（`mt6895.dtsi:447`），注释说 LK 把真实 VGPU/VCORE 留在某个值，但**具体是多少没写**。

---

*调研完成。所有结论基于 `/tmp/mt6895-mainline`（HEAD `25fd36e67`）、`/tmp/mt6895-quirks`、`/tmp/mt6895-initramfs` 三个 clone 的实际源码阅读，本地 dtsi 已核对与仓库逐字节一致。*
