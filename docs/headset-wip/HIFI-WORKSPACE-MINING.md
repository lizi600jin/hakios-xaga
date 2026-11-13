# HiFi 工作区 · xaga 耳机/音频硬件事实挖掘

> 挖掘对象：`/home/lizi/HiFi`（只读）
> 挖掘日期：2026-11-14
> 扫描范围：974 个非 fixture 文件（已排除 `.venv` / `node_modules` / `.git` / `tests/out` / `backup`），另有 `docs/headset-wip/ko/` 下 6 个内核模块作为旁证
> 证据分级：**【码】** = 代码/二进制里写的 · **【测】** = 日志里实测的 · **【文】** = 文档里声称的

---

## 0. 一句话结论

**这个工作区里关于 xaga 3.5mm 的「Android 侧框架事实」很扎实（端口、路由、采样率上限、活跃通道抓取），但「耳机检测机制」是空的** —— `accdet` / `micbias` / GPIO 号 / ADC 阈值 / 按键阻抗分档，在 `/home/lizi/HiFi` 里**没有任何数据**（`accdet`/`micbias` 只在 `work/XAGA_JACK_FOR_IOS.md` 里作为推断句出现，而那份文件的「实测」部分与现有 dump 矛盾，见 §3.2）。真正要做 jack 检测，只能靠 `docs/headset-wip/ko/mt6368-accdet.ko`（§3.3）。

---

## 1. 直接结论（能用的事实）

| # | 事实 | 级别 | 出处 |
|---|---|---|---|
| 1 | 声卡名 **`mt6895-mt6368`**（单卡 card0），平台 `MT6895Z/TCZA mt6895` | 【测】`/proc/asound/cards` 输出 | `work/xaga_baseline/adapt_report.txt:45-46`、`:11` |
| 2 | 机型 `xaga` / `22041216C` / SDK 36 / ROM `BP4A.251205.006 release-keys` / 内核 `5.10.260-XagaForge/fca92fa4` / SELinux Enforcing | 【测】 | `work/xaga_baseline/adapt_report.txt:3-5`、`build_id.txt:1`、`android_ver.txt:1`、`xaga_device.txt:1` |
| 3 | 3.5mm 在策略里是**两个独立输出端口**：`Wired Headset`(OUT_WIRED_HEADSET) 与 `Wired Headphones`(OUT_WIRED_HEADPHONE)，外加一个输入口 `Wired Headset Mic`(IN_WIRED_HEADSET) | 【码】XML | `work/xaga_baseline/audio_policy_configuration.xml:143`、`:149`、`:213` |
| 4 | 原厂给这两个端口的 profile 只有 **44100 / 48000**，格式 `PCM_32_BIT` + `PCM_16_BIT`（无 24bit profile） | 【码】XML | 同上 `:143-153` |
| 5 | 两个 Wired 端口都**不在 `attachedDevices` 里**（可插拔设备，符合预期） | 【码】XML | `:47-56`（列表里只有 Speaker/Earpiece/Built-In Mic/Built-In Back Mic/FM Tuner In/Voice Call In/Echo Ref In/Telephony Tx）、`:57` defaultOutputDevice=Speaker |
| 6 | 两个 Wired 端口的路由源集合：`primary output,deep_buffer,mmap_no_irq_out,fast,FM Tuner In,voip_rx,Voice Call In` —— **注意没有 `hifi_playback`** | 【码】XML | `:260-263` |
| 7 | `hifi_playback` / `hifi_input` 在原厂策略里是**空声明**（无 profile、无 flags） | 【码】XML | `:126`、`:127` |
| 8 | 模块曾在 xaga 上把 Wired Headset/Headphones 上限推到 384000Hz 并补 `PCM_24_BIT_PACKED` | 【文】测试报告 | `work/dist/XAGA_MTK_TEST_REPORT.md:30` |
| 9 | xaga 的 3.5mm 直插播放实测：`hifi doctor` 输出 `3.5mm耳机 (primary output) → 24bit @ 48000 Hz` | 【文】README/HOMEPAGE | `work/src/README.md:235`、`work/dist/HOMEPAGE_v2.0.2.md:153` |
| 10 | xaga **ROM 里没有 tinymix**（/vendor /system /odm /modules 全查） | 【文】+【测】 | `work/dist/XAGA_MTK_TEST_REPORT.md:19`、`work/xaga_baseline/adapt_report.txt:131`（"tinymix 不可用：本段跳过"） |
| 11 | SmartPA 型号**工作区内部自相矛盾**：测试报告/adapt 说 Goodix 定制 **TFA98xx**（`/dev/tfa_*`、无增益 kctl），代码注释说 **MTK RT5512**。两说都记在案，见 §5.8。**两处都一致的是：扬声器走 I2S3 直连 PA** | 【文】冲突 | `work/dist/XAGA_MTK_TEST_REPORT.md:18` + `work/xaga_baseline/adapt_report.txt:127-128` vs `work/src/bin/hifi:4289,:4304` + `work/src/lang/zh.cat:148` |
| 12 | xaga 扬声器链路：**I2S3 → PA 直连**，MTK 的 HW Gain / ADDA_DL **不在**扬声器通路上 | 【文】代码注释 | `work/src/bin/hifi:4289-4290`；i18n 文案 `work/src/lang/zh.cat:148-149` |
| 13 | 混音线程实测可达 **192000 Hz**（AudioOut_D + AudioOut_15 双线程），证明 MTK HIDL 链路不是只能 48k | 【测】 | `work/dist/XAGA_MTK_TEST_REPORT.md:31` |
| 14 | xaga 上 44100 混音率全链干净（对比一加13 QTI AIDL 的 192 帧周期错配） | 【文】 | `work/src/README.md:423-424` |
| 15 | 策略文件路径 `/vendor/etc/audio_policy_configuration.xml`（22270 bytes），方言判定 `aosp` | 【测】 | `work/xaga_baseline/adapt_report.txt:18`、`:34`、`:38` |
| 16 | xaga 的内核 cfg 把 `hifi_playback` 之外的 usb mixPort 特意排除（MTK xaga 真机评估结论，2026-09-25） | 【码】注释 | `work/src/payload/patch_policy.awk:352-357` |

---

## 2. 硬件拓扑

### 2.1 已确证的层级

```
MT6895 (天玑8100 / MT6895Z·TCZA)
  └─ AFE (snd-soc-mt6895-afe)  ── ALSA card0 = "mt6895-mt6368"
       ├─ MT6368 codec (snd-soc-mt6368)   ← 3.5mm 耳机/耳麦挂这里
       │    ├─ AIF1 / AIF2 (mt6368-snd-codec-aif1 / -aif2)，+ VOW
       │    ├─ 内部 ADDA (ADDA / ADDA_CH34 DAI link)
       │    ├─ 耳机模拟输出：Headphone L / Headphone R
       │    ├─ HP 阻抗检测：HP Impedance / "Audio HP ImpeDance Setting"
       │    └─ jack 状态 kctl：**"Headphone Plugged In"**
       ├─ MT6368 accdet (mt6368-accdet.ko) ← jack 检测独立成一个模块
       ├─ I2S0/1/2/3/5（+6/7/8/9 GPIO 钩子）、TDM_DPTX、PCM1、PCM2、CONNSYS_I2S
       └─ 外部 SmartPA: Goodix 定制 TFA98xx（i2c:tfa98xx / speaker_amp）
            └─ "Headphone L/R Ext Spk Amp"（codec 侧同名 kctl）
```

- **【测】** 声卡与平台：`work/xaga_baseline/adapt_report.txt:11`、`:45-46`
- **【码】** ko 依赖链 `mt6895-mt6368.ko` → `depends: mtk-sp-spk-amp,snd-soc-mt6368,snd-soc-mt6895-afe,mt6368-accdet`（`modinfo`），且 `snd-soc-mt6368.ko` → `depends: mt6368-accdet,mt6685-audclk`
- **【码】** DAI link 名称与绑定（`mt6895-mt6368.ko` 的 `.rela.data.mt6895_mt6368_driver` 重定位解析）：
  - `ADDA` → cpu `mt6368-sound` / codec `mt6368-snd-codec-aif1`
  - `ADDA_CH34` → cpu `mt6368-sound` / codec `mt6368-snd-codec-aif2`
  - `PCM 1` / `PCM 2` → platform `snd-soc-dummy` + `snd-soc-dummy-dai`（**dummy 平台，说明这两条是给外部/独立 codec 预留的占位，不接 MT6368**）
  - `I2S0/1/2/3/5`、`TDM_DPTX`、`HW Gain 1/2`、`HW_SRC_1/2`、`Hostless_*`、`DSP_Playback_*`（primary/deepbuf/fast/spatializer/a2dp/ble…）全部 platform 也是 `snd-soc-dummy`
- **【码】** codec DAI 名：`mt6368-snd-codec-aif1`、`mt6368-snd-codec-aif2`、`mt6368-snd-codec-vow`；codec 侧 kctl/widget 里出现 `Headphone L`、`Headphone R`、`Headphone L Ext Spk Amp`、`Headphone R Ext Spk Amp`、`Headset Volume`、`Handset Volume`、`Lineout Volume`、`HPL Mux`/`HPR Mux`、`AIF Out Mux`、`DAC In Mux`、`ADC L/R Select`
- **【码】** AFE 的 DL/UL 寄存器表覆盖 `AFE_DL1..AFE_DL12`；AFE 里还有 `ETDM_IN0..N`、`AFE_TDM_CON1/2` 与 `aud_gpio_i2s0..i2s9_on/off`（I2S6/7/8/9 只出现在 GPIO 钩子里，没进 DAI link 表）
- **【码】** SmartPA：`snd-soc-tfa98xx.ko` 的 compatible 覆盖 `tfa9872/9873/9874/98xx`，i2c alias `speaker_amp`，有 `gpio pull high/low`、`No clock detected`、`Firmware init complete` 等日志串

### 2.2 供电 / 引脚（**未找到**）

- GPIO 编号、regulator 名、MICBIAS 电压档位：`rg -i 'gpio'` 在 HiFi 全库（`work/ .research/ research/ .plan/ .agent-teams/`，排除 .git/node_modules/二进制）**只命中 `.research/src/wcd939x.c` 一个文件** —— 那是**高通 WCD939x** 的 codec 驱动（`Copyright Qualcomm`，`WCD939X_MAX_MICBIAS`、`wcd939x_micbias_control`），与 xaga 的 MT6368 **毫无关系**。xaga 的 GPIO/MICBIAS 取值：**0 命中**。
- ko 里能看到的只是**属性名**，不是取值：`headset-eint-num`、`headset-eint-level-pol`、`headset-eint-trig-mode`、`headset-use-ap-eint`、`accdet-eint-use_ext_res`、`accdet-eint-comp-vth`、`accdet-mic-mode`、`accdet-mic-vol`、`accdet-plugout-debounce`、`mediatek,soc-accdet` —— 实际数值在 DTS 里，工作区没有 DTS。

---

## 3. 耳机检测机制

### 3.1 HiFi 工作区里有的

- **【文】** 唯一一句直接描述 jack 状态的话，写在 `work/XAGA_JACK_FOR_IOS.md:74`：
  > 耳机插着时，`tinymix "Headphone Plugged In"` 回显 **`On`**
- **【文】** 同一文件 `:76-77` 声称 `AudioFlinger 的 AudioOut_15 输出设备为 0x8 AUDIO_DEVICE_OUT_WIRED_HEADPHONE，HAL format PCM_32_BIT，48000 Hz`，以及 `deep_buffer` 的 Devices = `AUDIO_DEVICE_OUT_WIRED_HEADPHONE`。
- **【码】** 模块自己抓 Wired 活跃通道的代码（这是 xaga 上 doctor 能报「3.5mm耳机(primary output) → 24bit @ 48000 Hz」的实现）：
  - `work/src/bin/probe192.sh:118` — `if(tp ~ /WIRED_HEADSET|WIRED_HEADPHONE/) { print "WIRED|" tg; next }`
  - `work/src/bin/probe192.sh:1329`、`:1340` — `[ "${WIRED_ACTIVE_N:-0}" -gt 0 ] && spk_brief="$spk_brief 3.5mm耳机(${WIRED_ACTIVE_N})"`
  - 文案 `work/src/lang/zh.cat:348`（`probe.l5095 3.5mm耳机`）、`:535`（`WIRED 是 3.5mm/模拟`）

### 3.2 ⚠️ 上面那条 "tinymix Headphone Plugged In" 的可信度问题（必须知道）

我做了交叉核对，**这份文件里的三个「实测」在 HiFi 工作区里找不到任何原始记录**：

| 声称 | 核查结果 |
|---|---|
| `tinymix "Headphone Plugged In"` → `On` | `rg 'Headphone Plugged In'` 全库 **只在 `XAGA_JACK_FOR_IOS.md:74/:79` 自身命中**，无 tinymix 输出、无 dump 文件支撑 |
| `pcm3p` RUNNING、owner `android.hardware.audio.service.mediatek` | 全库无该记录的原始日志；`pcm3p` 仅作为 adapt_report 的节点清单出现（`work/xaga_baseline/adapt_report.txt:85`） |
| `AudioOut_15` = `0x8 WIRED_HEADPHONE` / `PCM_32_BIT` / 48000 | ❌ **与工作区现有 dump 直接矛盾**：`research/bitperfect-dumpsys/real_a16_device_dump.txt:148-160` 里同名的 `AudioOut_15` 是 `Sample rate: 44100 Hz / HAL format 0x6 (PCM_24_BIT_PACKED) / Output devices: 0x4000000 (AUDIO_DEVICE_OUT_USB_HEADSET)`；而 `0x8` 在该 dump 的 `:179` 上下文里是 `AUDIO_OUTPUT_FLAG_DEEP_BUFFER`，不是设备类型。且该 dump 来自 **Redmi K20 Pro**（`research/bitperfect-dumpsys/REPORT.md:6`），压根不是 xaga |
| 「xaga 上是三态（带麦 / 不带麦 / 未插）」的推断 | 是**推断**，不是实测。策略里两个独立 devicePort 只能说明**框架层**有两条路，不能证明 codec 能区分；codec 侧确实有独立 `Headphone L/R` 与 `HP Impedance` 检测能力（【码】），但没有实测数值 |

**结论**：`work/XAGA_JACK_FOR_IOS.md` 是 2026-11-14 00:58 新写入的文件（`ls -lat` 显示它是全库最新文件，mtime 比本次挖掘早约 10 分钟，其余文件最新也只到 11-10），**不能当作一手实测证据**。它 `:36-46` 的 PCM 节点清单倒是真的（与 `adapt_report.txt:47-105` 逐项一致），但 `:74-77` 那三条要重新验证。

### 3.3 真正的一手：`docs/headset-wip/ko/mt6368-accdet.ko`（【码】二进制字符串）

> 这个 ko 不在 `/home/lizi/HiFi` 里，是我在核对目标目录时发现的旁证。它才是 jack 检测的实现本体。vermagic `5.10.226-android12-9-g8e1a18c8f218`，`intree: Y`，author `Argus Lin <argus.lin@mediatek.com>`，`description: MT6368 ALSA SoC accdet driver`。

**驱动内部机制（从符号与日志串读出）：**

| 项 | 内容 |
|---|---|
| 匹配 | `mediatek,MT6368-accdet`（`accdet_of_match`）、`__UNIQUE_ID___addressable_mt6368_accdet_init*` |
| 必需属性 | `mediatek,soc-accdet`（缺失会打印 `Property 'mediatek,soc-accdet' missing/invalid`） |
| EINT 相关 | `headset-eint-num`、`headset-eint-level-pol`、`headset-eint-trig-mode`、`headset-use-ap-eint`、`accdet-eint-use_ext_res`、`accdet-eint-comp-vth`、`ACCDET_EINT0`、`ACCDET_EINT1`、`accdet-eint`（`accdet_eint_callback_wrapper` → 回调注册） |
| 插拔判定 | `accdet_work_callback`（工作队列）+ `accdet_init_timer` / `accdet_set_debounce`；去抖属性 `accdet-plugout-debounce`，日志 `accdet_dts:deb0=0x%x,deb1=0x%x,deb3=0x%x,deb4=0x%x` |
| 耳机类型 | `accdet HEADPHONE(3-pole) %s` / `accdet MICROPHONE(4-pole) %s` / `accdet LineOut %s`，状态枚举含 `NO_DEVICE`（`accdet %s, set cable_type = NO_DEVICE %d`） |
| 按键 | `headset-three-key-threshold`、`headset-three-key-threshold-CDD`、`headset-four-key-threshold`、`headset-key-mode`、`multi_key_detection`、`send_key_event`、`snd_jack_set_key`；日志 `accdet key thresh: mid=%dmv,voice=%dmv,up=%dmv,down=%dmv`、`accdet: cur_key = %d`；efuse 兜底 `accdet no 4-key-thrsh dts, use efuse` |
| jack 上报 | `Headset Jack`（`snd_soc_card_jack_new` 的 jack 名）、`snd_soc_jack_report`、`snd_soc_component_set_jack`；另有 `USB_3_5 Jack`（`USB_3_5 Jack create failed`、`g_usb_3_5_jack`） |
| 湿度检测 | `Moisture_INT/EXT`、`moisture_eint0/1`、`moisture_vdd`、`moisture_comp_vref2/vth`、`config_moisture_detect_2_1(_1)`、`water in then plug out, handle plugout`、`Moisture plug out detectecd` |
| 麦克风 | `accdet mic_vol=%d, plugout_deb=%d mic_mode=%d eint_pol=%d`、`%s Plug-out, no dis micbias`（**这句证明存在 micbias 开关逻辑**）、`accdet_modify_vref_volt_self` / `mt6368_accdet_modify_vref_volt` |
| 用户态接口 | 有 `accdet_fops` / `accdet_headset_status_fops` / `accdet_read` / `accdet_write` / `mt_accdet_unlocked_ioctl`（字符设备或 proc 接口），另有 `debugfs_create_dir` + `debugfs_create_file`（`accdet_debugfs_dir`、`accdet_create_attr`）；**具体路径字符串在 stripped 的 ko 里没留下，无法确认是 `/proc/accdet` 还是 `/sys/...`** |
| 导出符号 | `mt6368_accdet_init`、`mt6368_accdet_late_init`、`mt6368_accdet_modify_vref_volt`、`mt6368_accdet_read_audio_res`、`accdet_eint_callback_wrapper` |
| AB / 接地检测 | `cur_AB = %d, accdet_status = %d`、`accdet %s Invalid AB.Do nothing` —— `AB` 的具体含义（附件板 ID / AB 类偏置）在 ko 里无注释，**不猜** |

**没有找到的：**
- ADC 具体阈值数值（只有 `%dmv` 格式串，值是 DTS/efuse 给的）
- 按键阻抗分档的具体 ohm/mV 数值
- GPIO 编号
- `h2w` / `switch_class` 式的开关上报（ko 里 **没有** `h2w` 或 `switch_class` 串，只有 `snd_jack_*` 与 input 子系统 `send_key_event`）

### 3.4 codec 侧的阻抗检测（补充手段）

`snd-soc-mt6368.ko` 里有一整套 **HP 阻抗测量**能力，与 accdet 是两条独立路径：

- kctl：`HP Impedance`、`Audio HP ImpeDance Setting`
- 内部：`detect_impedance` / `calculate_impedance` / `hp_impedance_get`
- 数据源：`mt6368_get_hpofs_auxadc`（AUXADC 通道）；日志 `%s(), hp_impedance = %d, efuse = %d`、`%s(), phase %d [dc,detect]Sum %d times [%d,%d], hp_impedance %d, pick_impedance %d, AUXADC_CON10 0x%x`、`%s(), Before Calibration from EFUSE: %d, R: %d`、`%s(), resister value: R=%u(ohm)`
- 前置条件：`counter %d <= 0 || mux_select[MUX_HP_L] %d != HP_MUX_HP_IMPEDANCE`（要先切 HPL Mux 到阻抗测量态）
- 校准数据：`pmic-hp-efuse`（`pmic_hpofs_cal`），efuse 读失败会重试

> 对 iOS 的意义：**插没插、插的是 3 极还是 4 极，可以走 accdet 的 EINT + 阻抗；也可以用 codec 的 AUXADC 阻抗测量做二次确认。** 前者是中断驱动（快），后者是主动测量（准）。

---

## 4. 音频通路

### 4.1 播放路径（从框架到硬件）

```
App → AudioFlinger
  ├─ AudioOut_D  (primary output, MIXER)   ← 内放/听筒，xaga 实测可跑到 192000Hz
  └─ AudioOut_15 (deep_buffer,       MIXER) ← 3.5mm 直插时活跃的就是这条
       → audio.primary.mediatek.so (HIDL, audio_policy_configuration version="7.0" halVersion="3.0")
       → AFE (snd-soc-mt6895-afe) → DLx → MT6368 AIF1/AIF2 → 内部 ADDA → Headphone L/R
       → (扬声器另走 I2S3 → TFA98xx PA)
```

【测】HAL 实现：`work/dist/XAGA_MTK_TEST_REPORT.md:17` — `audio.primary.mediatek.so + audio.usb.default.so（→ libtinyalsa + libalsautils.so）`

【测】xaga 的 AudioFlinger 默认态（`work/xaga_baseline/audioflinger_pre.txt:1-5`，全文 172 字节）：
```
Sample rate: 48000 Hz
HAL frame count: 1024
HAL format: 0x3 (AUDIO_FORMAT_PCM_32_BIT)
Processing frame size: 8 bytes
Normal frame count: 1024
Frames written: 0
```

【码】策略里 3.5mm 的 sink 路由（`audio_policy_configuration.xml:260-263`）：
```xml
<route type="mix" sink="Wired Headset"
       sources="primary output,deep_buffer,mmap_no_irq_out,fast,FM Tuner In,voip_rx,Voice Call In"/>
<route type="mix" sink="Wired Headphones"
       sources="primary output,deep_buffer,mmap_no_irq_out,fast,FM Tuner In,voip_rx,Voice Call In"/>
```

### 4.2 mixer 控件名（3.5mm 相关，从 codec ko 提取）

| 类别 | 控件名（【码】原字符串） |
|---|---|
| jack 状态 | `Headphone Plugged In` |
| 模拟输出 | `Headphone L`、`Headphone R`、`Headphone L Ext Spk Amp`、`Headphone R Ext Spk Amp` |
| 音量 | `Headset Volume`、`Handset Volume`、`Lineout Volume`、`PGA1/PGA2/PGA3 Volume` |
| 路由 mux | `HPL Mux` / `HPL Select`、`HPR Mux` / `HPR Select`、`AIF Out Mux` / `AIF Out Select`、`DAC In Mux` / `DAC Select`、`ADC L Select` / `ADC R Select` / `ADC 3 Select`、`DMIC0/1/2_MUX Select` |
| 阻抗 | `HP Impedance`、`Audio HP ImpeDance Setting`（拼写照原样） |
| DAC/ADC widget | `DACL`、`DACR`、`DAC_3RD`、`ADC_L`、`ADC_R`、`ADC_T`、`ADC_3`、`AIF1 Playback/Capture`、`AIF2 Playback/Capture`、`AIFTX_Supply`、`DL Power Supply`、`Audio Playback`、`LoudSPK Playback`、`Left/Right Preamplifier` |

> ⚠️ xaga 上 **tinymix 不存在**（【文】`XAGA_MTK_TEST_REPORT.md:19`），所以这些名字在 Android 用户态拿不到；模块是自带 `bin/tinymix_min`（静态 aarch64，直接 `open /dev/snd/controlC0`）绕过的 —— 见 `work/src/bin/tinymix_min` 里的字符串 `open /dev/snd/controlC0`、`Invalid mixer control: %u`，以及 `work/src/README.md:542`、`:548`（`payload/tinyalsa_min.c` 是其源码）。

### 4.3 采样率 / 位深约束

| 层 | 约束 | 出处 |
|---|---|---|
| 策略 XML（原厂） | Wired 端口仅 **44100 / 48000**，16bit + 32bit | 【码】`audio_policy_configuration.xml:143-153` |
| 策略 XML（模块改后） | Wired 端口 10 档到 **384000**，补 24BIT_PACKED | 【文】`XAGA_MTK_TEST_REPORT.md:30` |
| 实测直插播放 | `3.5mm耳机 (primary output) → 24bit @ 48000 Hz` | 【文】`work/src/README.md:235` |
| AFE 硬件 | `mt6895_general_rate_transform` / `adda_dl_rate_transform` / `pcm_rate_transform` / `dai_memif_rate_transform`；有不支持时回落 `rate %d invalid, use 48kHz!!!` 的逻辑 | 【码】`snd-soc-mt6895-afe.ko` |
| 混音线程 | 实测可 192000Hz（MIXER_RATE=192000 生效） | 【测】`XAGA_MTK_TEST_REPORT.md:31` |
| USB HAL | libalsautils 表 96k→384k（与 3.5mm 无关，但同机验证过） | 【测】`adapt_report.txt:29-30` |

### 4.4 PCM 节点全表（【测】，`/proc/asound` 实采）

来源 `work/xaga_baseline/adapt_report.txt:47-105`。播放节点：
```
pcm0p pcm1p pcm2p pcm3p pcm4p pcm5p pcm6p pcm7p pcm8p pcm9p
pcm21p..pcm29p  pcm53p pcm54p pcm56p pcm58p..pcm63p pcm65p pcm66p pcm68p..pcm70p
```
采集节点：
```
pcm10c..pcm20c pcm22c..pcm26c pcm28c pcm29c pcm52c..pcm56c pcm64c pcm67c pcm71c
```

> 注意 `PCM 1`/`PCM 2` 这两条 DAI link 用的是 `snd-soc-dummy` 平台（见 §2.1 重定位解析），**不要**据此认为 `pcm1p/pcm2p` 就是耳机。耳机到底落在哪个 `pcmNp` 只能实测（插着耳机播放时逐个读 `.../sub0/status`）。

---

## 5. 已知坑与限制

1. **【文】xaga 没有 tinymix** —— 任何依赖 `tinymix` 用户态的诊断脚本在 xaga 上直接失效，模块靠自带 `tinymix_min` 兜底。`XAGA_MTK_TEST_REPORT.md:19`、`adapt_report.txt:131`
2. **【文】PA 增益结构性不可用**（报告口径：Goodix 定制 TFA98xx 无增益 kctl）—— `hifi pa` 在 xaga 上降级 `unknown` 属**正确行为**。`XAGA_MTK_TEST_REPORT.md:55`。注意型号本身有矛盾，见下面第 8 条。
3. **【码】MTK 的 HW Gain / ADDA_DL 不在扬声器通路**，扬声器走 I2S3 直连 PA —— 别去调 ADDA_DL 想改外放。`work/src/bin/hifi:4289-4290`、`zh.cat:148`
4. **【码】xaga 上不能把 usb 命名的 mixPort 拉进 hifi 档**（会破坏 HAL 能力协商），这是 2026-09-25 真机评估的直接结论。`work/src/payload/patch_policy.awk:352-357`
5. **【文】MTK/HIDL 方言下 44100 是安全的**，与 QTI AIDL 的 48k 时钟粒度错配不同 —— xaga 推荐 44100 组合。`work/src/README.md:423-424`
6. **【文】xaga 只用 1 个策略文件 + 1 个 HAL 库**，`r_submix` / `bluetooth` / `usb_audio_accessory_only` 三个策略皆 `SKIPPED`（no USB/wired/DIRECT output port WITH a rate profile）。`adapt_report.txt:24-26`
7. **【文】模块从未在 xaga 上测过 USB 听感**（无硬件），也没测过 3.5mm 的听感，只测了配置层与活跃通道。`XAGA_MTK_TEST_REPORT.md:54`
8. **【文档陷阱 · 工作区内部自相矛盾】xaga 的 SmartPA 型号有两套说法。**
   - `work/dist/XAGA_MTK_TEST_REPORT.md:18`、`work/xaga_baseline/adapt_report.txt:127-128` 说 **Goodix 定制 TFA98xx**（kernel log `TFA9874 tfa98`）；
   - `work/src/bin/hifi:4289`、`:4304`、`:5317` 与 `work/src/lang/zh.cat:148`（`hifi.l0146`）却说 **MTK RT5512**。
   - ko 侧两边的名字都在：`mt6895-mt6368.ko` 有枚举 `MTK_SPK_GOODIX_TFAXXXX` **和** `MTK_SPK_MEDIATEK_RT5512` / `MTK_SPK_MEDIATEK_MT6660` / `MTK_SPK_RICHTEK_RT5509` / `MTK_SPK_NOT_SMARTPA`（说明是**运行时按 DTS 选型**），同时 `snd-soc-tfa98xx.ko` 确实被加载（`depends: mtk-sp-spk-amp`）。
   - → 结论：**xaga 实际装的是哪颗，工作区给不出唯一答案**；"I2S3 → PA 直连"这条（两处说法一致）可信，"PA 是 TFA98xx 还是 RT5512"待真机确认。
9. **【文档陷阱】`work/XAGA_JACK_FOR_IOS.md` 的 §4「已在真机验证过的现象」不可当真** —— 三条描述与工作区现有 dump 矛盾，且无原始日志（见 §3.2）。
10. **【文档陷阱】`work/reports/tinymix-full-dump.txt` 不是 xaga 的** —— 首行 `Mixer name: 'sm8150-tavil-snd-card'`，3523 个控件，是 K20 Pro(raphael) 的；**全库没有任何 xaga 的 tinymix dump**。
11. **【码】codec 与 accdet 是分模块加载的**（`snd-soc-mt6368` depends `mt6368-accdet`），iOS 侧如果只搬 codec 不搬 accdet，jack 检测直接没有。

---

## 6. 明确没有找到的东西

在 `/home/lizi/HiFi` 全库（排除 `.git`/`node_modules`/`.venv`/`tests/out`）定向搜索后确认**不存在**：

| 缺失项 | 搜索方式 | 结果 |
|---|---|---|
| `accdet` / `ACCDET` | `rg -i accdet`（全库） | **仅 1 个文件命中**：`work/XAGA_JACK_FOR_IOS.md` 自己（`:89`、`:112`、`:135`，是**推断式描述**，非数据）。无任何寄存器/节点/参数记录 |
| `micbias` / `mic_bias` | `rg -i micbias`（全库） | **2 个文件**：`work/XAGA_JACK_FOR_IOS.md:89`（推断句）+ `.research/src/wcd939x.c`（**高通 WCD939x 驱动**，与 xaga 无关） |
| `gpio` | `rg -i gpio`（`work/ .research/ research/ .plan/ .agent-teams/`） | **仅 `.research/src/wcd939x.c`**（高通驱动）。xaga 侧 0 命中 |
| `h2w` / `switch_class` | `rg -i 'h2w\|switch_class'` | **0 命中**（连 ko 里都没有，MT6368 走 `snd_jack_*` 而非 switch class） |
| 耳机按键阻抗分档（ohm / mV 阈值表） | `rg -i 'ohm|千欧|mv'` 相关上下文 | 未找到 |
| jack 检测的设备树节点（`accdet` / `h2w` / `headset-detect`） | 同上 | 未找到 |
| MT6368 / MT6895 寄存器手册、ACCDET 寄存器组 | — | 未找到 |
| xaga 内核源码树 | — | 只有版本号字符串 `5.10.260-XagaForge/fca92fa4` |
| xaga 的 `tinymix` 输出 / `getevent` 输出 / `dmesg` 全文 | `rg -i getevent`、`rg -i dmesg` | **无一份 xaga 的**。`getevent` 只在一份 AOSP 中文文档的目录导航里出现（`.research/src/a-src.txt:888`，"getevent 工具"一行）；`dmesg` 命中的是 `work/reports/smartpa-probe.md`（raphael）、`work/src/bin/probe192.sh`（代码里的命令串）、`work/reports/t1_forensics/*`（K20/未知机型墓碑） |
| 3.5mm 插入事件的 logcat 记录 | — | 未找到 |
| xaga 的 `/proc/asound/card0/pcmNp/sub0/status` 快照 | — | 未找到（只有节点**清单**，没有 status 内容） |
| xaga 的 `tinymix`/`mixer` 控件 dump | `rg -il tinymix` | 两份 dump 都是别的机器（见 §7.3） |

另外，`work/*.txt` 共 29 份，归属逐份核对如下：

- **14 份**带「机型」行：其中 **13 份**是 `raphael / Redmi K20 Pro SDK 36`（7 份 `doctor_v19*.txt`、1 份 `doctor_sp44100_spb32.txt`、4 份 `probe_*.txt`、`k20_doctor.txt`），**1 份**是 `OP5D0DL1 / PJZ110`（一加13，`work/op13_doctor.txt`）
- **15 份**无「机型」行：`app1*.txt`(2) + `app2*.txt`(5) + `app3*.txt`(5) + `app4_kugou_hires.txt` + `hifi_probe_app.txt` + `snapshot_baseline.txt`。它们也**没有 `xaga`/`mt6895`/`mt6368`/`raphael`/`sm8150` 任何标记**，无法归属；从 `sdk:33`/`sdk:30`/`deviceIds:[27]` 看是 Android 12/13 设备，**与 xaga 的 SDK 36 不符**

**结论：29 份里没有一份属于 xaga。** xaga 的全部第一手记录只有 `work/xaga_baseline/` 那 8 个文件。

---

## 7. 文件清单

### 7.1 高价值（必读）

| 路径 | 大小 | 说明 |
|---|---|---|
| `work/xaga_baseline/adapt_report.txt` | 6444 B | ⭐ 最有用。`hifi adapt` 全量输出：平台/声卡名/PCM 节点全表/SmartPA 型号/策略扫描结论 |
| `work/xaga_baseline/audio_policy_configuration.xml` | 21167 B | ⭐ xaga 原厂策略（321 行）。端口、路由、profile 全在这 |
| `work/dist/XAGA_MTK_TEST_REPORT.md` | 5421 B | ⭐ xaga 真机测试报告（26 项）。设备画像 / SmartPA / tinymix 缺失 / 未测项 |
| `work/xaga_baseline/audioflinger_pre.txt` | 172 B | AudioFlinger 默认态：48k / PCM_32 / frame count 1024 |
| `work/xaga_baseline/build_id.txt` | 29 B | `BP4A.251205.006 release-keys` |
| `work/xaga_baseline/android_ver.txt` | 3 B | `16` |
| `work/xaga_baseline/xaga_device.txt` | 5 B | `xaga` |
| `work/xaga_baseline/libalsautils.so` | 28080 B | 原厂 USB HAL（与 3.5mm 无关，USB DAC 用） |
| `work/xaga_baseline/libalsautils-v31.so` | 68489 B | 同上，v31 变体 |

### 7.2 中等价值（有 xaga 段落）

| 路径 | 大小 | 说明 |
|---|---|---|
| `work/src/bin/hifi` | 234619 B | 主控制器。`4285-4310` 段有 xaga 的 I2S3→PA、ADDA_DL 不在扬声器通路的注释 |
| `work/src/payload/patch_policy.awk` | 29964 B | 策略改写引擎。`320` 行判 WIRED_HEADSET/HEADPHONE；`352-357` 有 xaga 的 usb mixPort 排除结论 |
| `work/src/bin/probe192.sh` | 84052 B | doctor 实现体。`118` 抓 WIRED 活跃设备，`1329/1340` 报「3.5mm耳机」 |
| `work/src/README.md` | 58810 B | `229` xaga 验证行；`233-236` 3.5mm 端口/自检输出；`423-424` MTK 44100 结论 |
| `work/dist/HOMEPAGE_v2.0.2.md` | 12411 B | `147-153` xaga 行 + v2.0.2 3.5mm 新增验证段 |
| `work/src/lang/zh.cat` / `en.cat` | 87472 / 95224 B | i18n。`l0146-0148` 讲 ADDA_DL/I2S3；`probe.l5095` = 3.5mm耳机 |
| `work/MODULE_BRIEF.md` | 7614 B | 模块全貌（代码地图/P0 安全网/PA 后端注册表） |
| `.research/headphone-quality.md` | 34612 B | 耳机链路音质上限调研。**§5「内置 3.5mm 与 USB 尾巴差异」是通用机制，不是 xaga 实测** |
| `work/BIT_PERFECT-兼容性说明.md` | 8707 B | AIDL/HIDL 方言门槛（xaga 是 HIDL → BIT_PERFECT 不可用） |

### 7.3 低价值 / 易误用（已核对来源）

| 路径 | 大小 | 说明 |
|---|---|---|
| `work/XAGA_JACK_FOR_IOS.md` | 7129 B | ⚠️ 2026-11-14 00:58 新写入，**§4 实测三条与现有 dump 矛盾**，只能当"待验证线索" |
| `work/reports/tinymix-full-dump.txt` | 253153 B | ⚠️ **K20 Pro** 的 dump（`sm8150-tavil-snd-card`，3523 控件），不是 xaga |
| `work/tests/fixtures/op13_tinymix_dump.txt` | 33989 B | ⚠️ 一加13 的 dump（`sun-mtp-snd-card`） |
| `work/src/bin/tinymix_min` | 34128 B | 静态 aarch64 tinymix 替代品，直接 `open /dev/snd/controlC0` |
| `work/*.txt`（29 份） | — | 13 份 K20 Pro + 1 份 OP13 + 15 份无归属（Android 12/13），**无一 xaga**，详见 §6 末尾 |
| `work/reports/t1_forensics/REPORT_LIVE_20261017.md` | 32653 B | K20 Pro 的现场取证（端口清单里含 Wired Headset/Headphones，但不是 xaga） |
| `research/bitperfect-dumpsys/real_a16_device_dump.txt` | 36856 B | K20 Pro 的 AudioFlinger dump（`REPORT.md:6` 自述）。`AudioOut_15` = USB_HEADSET，别当 xaga 用 |
| `work/.research/device-adaptation.md` | 24400 B | MTK 平台适配推断，**大量标注【外推】**，不能当事实 |

### 7.4 旁证（不在 HiFi 工作区，但在目标目录里）

| 路径 | 大小 | 说明 |
|---|---|---|
| `docs/headset-wip/ko/mt6368-accdet.ko` | 120232 B | ⭐ **jack 检测实现本体**。`mediatek,MT6368-accdet`，3-pole/4-pole 判定、按键阈值、EINT、湿度检测、micbias 控制逻辑全在这 |
| `docs/headset-wip/ko/snd-soc-mt6368.ko` | 277608 B | codec 驱动。`Headphone Plugged In`、`HP Impedance`、HPL/HPR Mux、ADDA/AIF1/AIF2 |
| `docs/headset-wip/ko/mt6895-mt6368.ko` | 83840 B | machine driver。DAI link 表（ADDA/ADDA_CH34/PCM1/PCM2/I2S0-5/TDM_DPTX…） |
| `docs/headset-wip/ko/snd-soc-mt6895-afe.ko` | 807656 B | AFE。`AFE_DL1..DL12`、ETDM、I2S GPIO 钩子、rate transform |
| `docs/headset-wip/ko/snd-soc-mtk-common.ko` | 117760 B | MTK 平台公共层 |
| `docs/headset-wip/ko/snd-soc-tfa98xx.ko` | 462136 B | TFA98xx 功放。compatible 覆盖 tfa9872/9873/9874/98xx |

---

## 附：给 iOS 移植的三条行动建议（基于上面的事实）

1. **jack 检测直接照 `mt6368-accdet.ko` 抄，不要照 HiFi 工作区猜。** 关键点：`mediatek,soc-accdet` 必需属性、EINT（`headset-eint-num`/`-level-pol`/`-trig-mode`/`headset-use-ap-eint`）、去抖 `accdet-plugout-debounce`、三/四键阈值 `headset-{three,four}-key-threshold`、3-pole vs 4-pole 判定、`snd_soc_component_set_jack` 上报链。
2. **两条独立的检测路径可互补：** accdet EINT（中断、快）+ codec `HP Impedance`（AUXADC 主动测、准）。iOS 若中断接驳困难，可先用阻抗测量轮询。
3. **播放通路先打通 `MT6368 AIF1 → ADDA → Headphone L/R` 这一条**（DAI link 名 `ADDA` / `ADDA_CH34`），扬声器那条是 `I2S3 → TFA98xx`，两条互不相干。`PCM 1`/`PCM 2` 挂的是 `snd-soc-dummy`，别拿来当耳机通路。
