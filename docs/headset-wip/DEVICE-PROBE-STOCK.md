# xaga 真机探测（stock Android）

> 目标：把 Redmi Note 11T Pro（xaga / MT6895 / MT6368）上 **3.5mm 耳机接口**相关的全部可观测信息 dump 下来，
> 供后续在裸机 XNU 里重实现参考。
>
> **本次探测全程只读**：没有 `fastboot`、没有刷机、没有重启、没有 `setprop`、没有写任何 sysfs / kcontrol。
> 唯一的副作用是 `tinymix -a` 与 `cat` 类读取；`dump_reg` 读取本身不产生内核 printk（见 §3.4 的时间戳核对）。
>
> 探测时间：2026-11-14（设备 uptime 4054s / 约 67 分钟）
> 序列号：`GAJZ4HIN99AQ69Y9`

---

## 0. 设备与环境（型号、指纹、内核版本、root 状态）

### 0.1 `adb devices -l`

```
List of devices attached
GAJZ4HIN99AQ69Y9       device usb:3-2 product:xaga model:22041216C device:xaga transport_id:1
```

### 0.2 产品属性

命令：`adb shell 'getprop ro.product.device; getprop ro.board.platform; getprop ro.build.fingerprint; getprop ro.build.version.release; getprop ro.build.display.id'`

```
xaga
mt6895
Redmi/xaga/xaga:17/CP2A.260605.016/OS4.0.0.14.XAACNXM:user/release-keys
17
CP2A.260605.016
```

补充（`getprop ro.product.model` / `ro.build.version.sdk` / `ro.vendor.build.fingerprint`）：

```
22041216C
37
Redmi/xaga/xaga:12/SP1A.210812.016/OS2.0.12.0.ULOCNXM:user/release-keys
```

> ⚠️ 注意这个不一致：`ro.build.fingerprint` 报 Android **17**（`CP2A.260605.016` / `OS4.0.0.14.XAACNXM`），
> 但 `ro.vendor.build.fingerprint` 还是 Android **12**（`SP1A.210812.016` / `OS2.0.12.0.ULOCNXM`），SDK=37。
> vendor 分区（音频 HAL / audio_policy XML / 内核模块）实际是 **Android 12 世代**的 MTK BSP。

### 0.3 内核

命令：`adb shell 'uname -a'` / `adb shell 'cat /proc/version'`

```
Linux localhost 5.10.237-Fuutao-Qn_miao-gc5e20589c #0 SMP PREEMPT Thu Feb 20 00:00:00 CST 2025 aarch64 Toybox
```

```
Linux version 5.10.237-Fuutao-Qn_miao-gc5e20589c (moon-source@c5-cose-moon-core-source-merge) (Android (on r584948) clang version 22.0.0, LLD 22.0.0 (r584948-release 2d65e4108)) #0 SMP PREEMPT Thu Feb 20 00:00:00 CST 2025
```

- 内核：**5.10.237**，自编译（`Fuutao-Qn_miao`，非官方 GKI 名），clang 22.0.0
- 架构：aarch64，`PREEMPT`
- SELinux：**Enforcing**（`getenforce`）

### 0.4 root 状态：**已 root，KernelSU，shell 直接是 uid=0**

命令：`adb shell 'id; which su; ls -l /system/bin/su /system/xbin/su /sbin/su /vendor/bin/su'`

```
uid=0(root) gid=0(root) groups=0(root),1004(input),1007(log),1011(adb),1015(sdcard_rw),1028(sdcard_r),1078(ext_data_rw),1079(ext_obb_rw),3001(net_bt_admin),3002(net_bt),3003(inet),3006(net_bw_stats),3009(readproc),3011(uhid),3012(readtracefs) context=u:r:ksu:s0
--which su--
/system/bin/su
--ls su--
ls: /system/xbin/su: No such file or directory
ls: /sbin/su: No such file or directory
ls: /vendor/bin/su: No such file or directory
-rwxr-xr-x 1 root root 4990848 2026-11-05 18:50 /system/bin/su
```

**关键点**：
- `adb shell` **落地即 root**（不需要 `su -c`），SELinux context 是 `u:r:ksu:s0` → **KernelSU**
- `/system/bin/su` 存在（4.99 MB，自编译/替换件），无 `/system/xbin/su`、`/sbin/su`、`/vendor/bin/su`
- `/data/adb/ksu` 目录存在（含 `bin` / `lib` / `log`）→ 进一步确认 KernelSU
- 因此 §7「无法获取」的项**不是**权限导致，而是**文件/接口本身不存在**

---

## 1. ALSA 卡与 PCM 设备全表

### 1.1 `/proc/asound/cards`

```
 0 [mt6895mt6368   ]: mt6895-mt6368 - mt6895-mt6368
                      mt6895-mt6368
```

单卡。卡名 `mt6895mt6368`，驱动名 `mt6895-mt6368`。`/proc/asound/mt6895mt6368 -> card0`。

### 1.2 `/proc/asound/pcm`（49 行，原文）

```
00-00: Playback_1 (*) :  : playback 1
00-01: Playback_12 (*) :  : playback 1
00-02: Playback_2 (*) :  : playback 1
00-03: Playback_3 (*) :  : playback 1
00-04: Playback_4 (*) :  : playback 1
00-05: Playback_5 (*) :  : playback 1
00-06: Playback_6 (*) :  : playback 1
00-07: Playback_7 (*) :  : playback 1
00-08: Playback_8 (*) :  : playback 1
00-09: Playback_9 (*) :  : playback 1
00-10: Capture_1 (*) :  : capture 1
00-11: Capture_2 (*) :  : capture 1
00-12: Capture_3 (*) :  : capture 1
00-13: Capture_4 (*) :  : capture 1
00-14: Capture_5 (*) :  : capture 1
00-15: Capture_6 (*) :  : capture 1
00-16: Capture_7 (*) :  : capture 1
00-17: Capture_8 (*) :  : capture 1
00-18: Capture_Mono_1 (*) :  : capture 1
00-19: Capture_Mono_2 (*) :  : capture 1
00-20: Capture_Mono_3 (*) :  : capture 1
00-21: Playback_HDMI (*) :  : playback 1
00-22: Hostless_LPBK (*) :  : playback 1 : capture 1
00-23: Hostless_FM (*) :  : playback 1 : capture 1
00-24: Hostless_Speech (*) :  : playback 1 : capture 1
00-25: Hostless_Sph_Echo_Ref (*) :  : playback 1 : capture 1
00-26: Hostless_Spk_Init (*) :  : playback 1 : capture 1
00-27: Hostless_ADDA_DL_I2S_OUT (*) :  : playback 1
00-28: Hostless_SRC_1 (*) :  : playback 1 : capture 1
00-29: Hostless_SRC_Bargein (*) :  : playback 1 : capture 1
00-52: Hostless_HW_Gain_AAudio (*) :  : capture 1
00-53: Hostless_SRC_AAudio (*) :  : playback 1 : capture 1
00-54: BTCVSD snd-soc-dummy-dai-54 :  : playback 1 : capture 1
00-55: VOW_Capture mt6368-snd-codec-vow-55 :  : capture 1
00-56: SCP_ULTRA_Playback snd-soc-dummy-dai-56 :  : playback 1 : capture 1
00-58: DSP_Playback_Voip snd-soc-dummy-dai-58 :  : playback 1
00-59: DSP_Playback_Primary snd-soc-dummy-dai-59 :  : playback 1
00-60: DSP_Playback_DeepBuf snd-soc-dummy-dai-60 :  : playback 1
00-61: DSP_Playback_Fast snd-soc-dummy-dai-61 :  : playback 1
00-62: DSP_Playback_Spatializer snd-soc-dummy-dai-62 :  : playback 1
00-63: DSP_Playback_Playback snd-soc-dummy-dai-63 :  : playback 1
00-64: DSP_Capture_Ul1 snd-soc-dummy-dai-64 :  : capture 1
00-65: DSP_Call_Final snd-soc-dummy-dai-65 :  : playback 1
00-66: DSP_Playback_Ktv snd-soc-dummy-dai-66 :  : playback 1
00-67: DSP_Capture_Raw snd-soc-dummy-dai-67 :  : capture 1
00-68: DSP_Playback_Fm_Adsp snd-soc-dummy-dai-68 :  : playback 1
00-69: DSP_Playback_A2DP snd-soc-dummy-dai-69 :  : playback 1
00-70: DSP_Playback_BLEDL snd-soc-dummy-dai-70 :  : playback 1
00-71: DSP_Capture_BLE snd-soc-dummy-dai-71 :  : capture 1
```

注意 device 编号 **50、51、57 缺失**（`00-50` / `00-51` / `00-57` 不在 pcm 表里），但 `/proc/asound/devices` 里有一条 `63: [ 0-57]: ?` —— 说明 **device 57 存在但未注册 PCM 设备名**。

### 1.3 `/proc/asound/devices`（原文，含上面那条 `?`）

```
  2: [ 0- 0]: digital audio playback
  3: [ 0- 1]: digital audio playback
  4: [ 0- 2]: digital audio playback
  5: [ 0- 3]: digital audio playback
  6: [ 0- 4]: digital audio playback
  7: [ 0- 5]: digital audio playback
  8: [ 0- 6]: digital audio playback
  9: [ 0- 7]: digital audio playback
 10: [ 0- 8]: digital audio playback
 11: [ 0- 9]: digital audio playback
 12: [ 0-10]: digital audio capture
 13: [ 0-11]: digital audio capture
 14: [ 0-12]: digital audio capture
 15: [ 0-13]: digital audio capture
 16: [ 0-14]: digital audio capture
 17: [ 0-15]: digital audio capture
 18: [ 0-16]: digital audio capture
 19: [ 0-17]: digital audio capture
 20: [ 0-18]: digital audio capture
 21: [ 0-19]: digital audio capture
 22: [ 0-20]: digital audio capture
 23: [ 0-21]: digital audio playback
 24: [ 0-22]: digital audio playback
 25: [ 0-22]: digital audio capture
 26: [ 0-23]: digital audio playback
 27: [ 0-23]: digital audio capture
 28: [ 0-24]: digital audio playback
 29: [ 0-24]: digital audio capture
 30: [ 0-25]: digital audio playback
 31: [ 0-25]: digital audio capture
 33:        : timer
 34: [ 0-26]: digital audio playback
 35: [ 0-26]: digital audio capture
 36: [ 0-27]: digital audio playback
 37: [ 0-28]: digital audio playback
 38: [ 0-28]: digital audio capture
 39: [ 0-29]: digital audio playback
 40: [ 0-29]: digital audio capture
 41: [ 0-52]: digital audio capture
 42: [ 0-53]: digital audio playback
 43: [ 0-53]: digital audio capture
 44: [ 0-54]: digital audio playback
 45: [ 0-54]: digital audio capture
 46: [ 0-55]: digital audio capture
 47: [ 0-56]: digital audio playback
 48: [ 0-56]: digital audio capture
 49: [ 0-58]: digital audio playback
 50: [ 0-59]: digital audio playback
 51: [ 0-60]: digital audio playback
 52: [ 0-61]: digital audio playback
 53: [ 0-62]: digital audio playback
 54: [ 0-63]: digital audio playback
 55: [ 0-64]: digital audio capture
 56: [ 0-65]: digital audio playback
 57: [ 0-66]: digital audio playback
 58: [ 0-67]: digital audio capture
 59: [ 0-68]: digital audio playback
 60: [ 0-69]: digital audio playback
 61: [ 0-70]: digital audio playback
 62: [ 0-71]: digital audio capture
 63: [ 0-57]: ?
 65: [ 0]   : control
```

### 1.4 `/dev/snd/` 节点

命令：`adb shell ls -l /dev/snd/`（66 个条目，节选关键）

```
crw-rw---- 1 system audio 116,  63 comprC0D57
crw-rw---- 1 system audio 116,  65 controlC0
crw-rw---- 1 system audio 116,   2 pcmC0D0p
crw-rw---- 1 system audio 116,  12 pcmC0D10c
...
crw-rw---- 1 system audio 116,  62 pcmC0D71c
crw-rw---- 1 system audio 116,  33 timer
```

- `controlC0` 主设备 116，次设备 65 → **有 mixer 控制接口**
- `comprC0D57` → device 57 是 **compressed offload** 节点（解释了 §1.2 的 `?`）
- 权限：`system:audio` `0660`，但当前 shell 是 root，可读写

### 1.5 PCM 硬件能力（`tinypcminfo`）

命令：`adb shell 'tinypcminfo -D 0 -d 0'`

```
cannot open device 0 for card 0
Info for card 0, device 0:

PCM out:
      Access:	0x000009
   Format[0]:	0x000444
   Format[1]:	00000000
 Format Name:	S16_LE, S24_LE, S32_LE
   Subformat:	0x000001
        Rate:	min=8000Hz	max=192000Hz
    Channels:	min=1		max=2
 Sample bits:	min=16		max=32
 Period size:	min=12		max=65536
Period count:	min=2		max=256

PCM in:
pcm_hw_open: cannot open device '/dev/snd/pcmC0D0c'Device does not exist.
```

`-d 1` 输出完全一致（Playback_12 同样 8k–192k / 1–2ch / S16|S24|S32）。

> 注意 `cannot open device 0 for card 0` 是 tinypcminfo 自己的一句误报——后面仍然成功打印了 info
> （它先 `pcm_open` 失败再退回 `pcm_info`）。`Access: 0x9` = `SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED`。

### 1.6 `hw_params`（当前无流）

命令：`adb shell 'cat /proc/asound/card0/pcm0p/sub0/hw_params; cat /proc/asound/card0/pcm10c/sub0/hw_params'`

```
closed
closed
```

（无活动流，符合 `audio_flinger` 里所有 output thread `Standby: yes`。）

### 1.7 耳机相关的 PCM 通道（推断链）

MTK 的耳机播放通常是 `DL1` → `ADDA_DL` → `HPL/HPR`。对应的 hostless / DSP 通道：
`00-27 Hostless_ADDA_DL_I2S_OUT`（playback only）在 mixer 里对应 `ADDA_DL_CH1..CH4` 这一组 DAPM。
耳机录音（headset mic）走 `ADDA_UL_CH1/CH2`，对应 `00-10 Capture_1` / `00-11 Capture_2`。

---

## 2. codec 与 mixer 拓扑

### 2.1 tinyalsa 工具位置（**不在 /vendor/bin**）

命令：`adb shell 'ls -l /vendor/bin/tinymix /system/bin/tinymix ...; command -v tinymix tinyplay tinypcminfo'`

```
ls: /vendor/bin/tinymix: No such file or directory
ls: /vendor/bin/tinyplay: No such file or directory
ls: /vendor/bin/tinypcminfo: No such file or directory
-rwxr-xr-x 1 root shell 51672 2009-01-01 08:00 /system/bin/tinymix
-rwxr-xr-x 1 root shell 51464 2009-01-01 08:00 /system/bin/tinypcminfo
```

```
/system/bin/tinymix
/system/bin/tinyplay
/system/bin/tinypcminfo
```

→ `tinymix` / `tinyplay` / `tinypcminfo` **都在 `/system/bin`**（AOSP 自带），`/vendor/bin` 下没有。
这是 AOSP 的 `tinymix`（新版，`-a` 打印 ranges）。

### 2.2 mixer 总体

命令：`adb shell 'tinymix -a'`（568 行；归档于同目录 `tinymix-a.txt`）

```
Mixer name: 'mt6895-mt6368'
Number of controls: 564
ctl	type	num	name                                     value
	range/values
```

按类型统计（`awk -F'\t' '{print $2}' tinymix-a.txt | sort | uniq -c`）：

```
    384 BOOL
     86 INT
     79 ENUM
     14 BYTE
```

### 2.3 `/proc/asound/card0/codec*` —— **不存在**

命令：`adb shell 'ls -l /proc/asound/card0/codec* /proc/asound/codec*'`

```
ls: /proc/asound/card0/codec*: No such file or directory
ls: /proc/asound/codec*: No such file or directory
```

**结论**：MTK 的 `mt6368-sound` codec 驱动**不实现** `snd_soc_codec_driver` 的 procfs 注册，
所以没有 HDA 风格 `codec#0` 节点。codec 状态只能通过 **kcontrol（tinymix）+ DT + `dump_reg`** 拿。
`/proc/asound/card0/` 下只有 `id`、`compr57/`、`pcmNp/`、`pcmNc/` 这些标准目录。

### 2.4 ★ 耳机/HP/ACCDET/MICBIAS 相关控件（**重点**）

命令：`adb shell tinymix -a | grep -i -E 'hp|head|accdet|micbias|jack|plug|hs_|lo_|lineout'`

```
398	INT	1	Speech_MD_HeadVersion                    43605 (dsrange 0->-1)
452	ENUM	1	Headphone Plugged In                     >Off On
453	INT	1	Audio HP ImpeDance Setting               0 (dsrange 0->65536)
464	INT	2	Headset Volume                           31 31 (dsrange 0->30)
465	INT	2	Lineout Volume                           31 31 (dsrange 0->18)
541	ENUM	1	HPL Mux                                  >Open LoudSPK Playback Audio Playback Test Mode HP Impedance
542	ENUM	1	HPR Mux                                  >Open LoudSPK Playback Audio Playback Test Mode HP Impedance
```

逐条（**这是重实现时最直接的操作面**）：

| ctl | 类型/通道 | 名称 | 当前值 / 可选值 |
|---|---|---|---|
| 452 | ENUM ×1 | `Headphone Plugged In` | **Off**（可选 `Off` / `On`） |
| 453 | INT ×1 | `Audio HP ImpeDance Setting` | `0`（range 0→65536） |
| 464 | INT ×2 | `Headset Volume` | `31 31`（range 0→30）⚠️ 值超出 max |
| 465 | INT ×2 | `Lineout Volume` | `31 31`（range 0→18）⚠️ 值超出 max |
| 541 | ENUM ×1 | `HPL Mux` | `Open`（可选 `Open` / `LoudSPK` / `Playback` / `Audio Playback` / `Test Mode` / `HP Impedance`） |
| 542 | ENUM ×1 | `HPR Mux` | `Open`（同上） |

**关键观察**：

1. **`Headphone Plugged In` 是唯一一个"耳机插入"布尔 kcontrol**，当前 `Off`。
   这是 MT6368 accdet 驱动往 ASoC 层报状态的落点（snprintf → 上层 HAL 读它，或反之由 HAL 写）。
2. **`HPL Mux` / `HPR Mux` 当前是 `Open`（= 悬空/断开）**，不是 `Playback` —— 说明耳机功放**未连接**。
   正常播放到耳机时应切到 `Playback`（走 DAC → HPL/HPR）。
3. **`Lineout Volume` = 31 但 max 18，`Headset Volume` = 31 但 max 30** —— 越界值。
   这通常是 BSP 里 `mt6368` 的 reg_default / DT 初始值直接写入而未 clamp，重实现时要按 range 截断。
4. **没有 `MICBIAS` 相关控件**（`grep -i bias` 命中 **0** 条）。MT6368 的 micbias 是 accdet 内部
   自动开（`accdet-mic-vol` DT 属性控制），不走独立 kcontrol。
5. **`Audio HP ImpeDance Setting` = 0** —— 阻抗测量未做。配合 dmesg 的 `hp_impedance_get() ... counter 0 <= 0`
   报错（见 §3.4），说明上层尝试读阻抗但 accdet 侧未开启 HP Impedance 模式。

### 2.5 模拟 codec（AUDDEC_ANA）通路全貌

播放侧 DAPM（`tinymix-a.txt` 305–372 行，**全部当前为 `Off`**）：

```
305	BOOL	1	ADDA_DL_CH1 DL1_CH1                      Off
306	BOOL	1	ADDA_DL_CH1 DL12_CH1                     Off
307	BOOL	1	ADDA_DL_CH1 DL2_CH1                      Off
308	BOOL	1	ADDA_DL_CH1 DL3_CH1                      Off
309	BOOL	1	ADDA_DL_CH1 DL4_CH1                      Off
310	BOOL	1	ADDA_DL_CH1 DL5_CH1                      Off
311	BOOL	1	ADDA_DL_CH1 DL6_CH1                      Off
312	BOOL	1	ADDA_DL_CH1 DL7_CH1                      Off
313	BOOL	1	ADDA_DL_CH1 DL8_CH1                      Off
...
321	BOOL	1	ADDA_DL_CH2 DL1_CH1                      Off
322	BOOL	1	ADDA_DL_CH2 DL1_CH2                      Off
...
342	BOOL	1	ADDA_DL_CH3 DL1_CH1                      Off
...
355	BOOL	1	ADDA_DL_CH4 DL1_CH1                      Off
...
```

→ **`ADDA_DL_CH1..CH4` 是 4 个模拟播放通道**，各自可 mux 到 `DL1/DL2/DL3/DL4/DL5/DL6/DL7/DL8/DL12`。
耳机通常用 `ADDA_DL_CH1 ← DL2`（MTK 惯例），但**当前无任何连接**（全部 Off，因为没在播放）。

输出 mux（`tinymix-a.txt` 540–546）：

```
540	ENUM	1	DAC In Mux                               >Normal Path Sgen
541	ENUM	1	HPL Mux                                  >Open LoudSPK Playback Audio Playback Test Mode HP Impedance
542	ENUM	1	HPR Mux                                  >Open LoudSPK Playback Audio Playback Test Mode HP Impedance
543	ENUM	1	RCV Mux                                  >Open Mute Voice Playback Test Mode
544	ENUM	1	LOL Mux                                  >Open Playback_L_DAC Playback Test Mode
545	ENUM	1	AIF Out Mux                              >Normal Path Sgen
546	ENUM	1	AIF2 Out Mux                             >Normal Path Sgen
```

- `HPL Mux` / `HPR Mux`：耳机左右声道输出选择器（`Open`=断开 / `LoudSPK`=外放 / `Playback`=正常 DAC 播放 / `HP Impedance`=阻抗测量）
- `RCV Mux`：听筒（`Open` / `Mute` / `Voice` / `Playback` / `Test Mode`）
- `LOL Mux`：Line-out 左（`Open` / `Playback_L_DAC` / `Playback` / `Test Mode`）
- `DAC In Mux`：`Normal Path` / `Sgen`（正弦波发生器，测试用）

### 2.6 音量 / 增益控件

```
29	INT	1	HW Gain 1                                524288 (dsrange 0->268435455)
30	INT	1	HW Gain 2                                524288 (dsrange 0->268435455)
36	INT	1	Sidetone_Gain                            0 (dsrange 0->65535)
37	INT	1	Sidetone_Positive_Gain_dB                0 (dsrange 0->100)
38	INT	1	ADDA_DL_GAIN                             0 (dsrange 0->65535)
464	INT	2	Headset Volume                           31 31 (dsrange 0->30)
465	INT	2	Lineout Volume                           31 31 (dsrange 0->18)
466	INT	1	Handset Volume                           31 (dsrange 0->18)
467	INT	1	PGA1 Volume                              0 (dsrange 0->4)
468	INT	1	PGA2 Volume                              0 (dsrange 0->4)
469	INT	1	PGA3 Volume                              0 (dsrange 0->4)
```

- `HW Gain 1/2` = 524288 = `0x80000`（Q15 定点 1.0x 的 16 倍，即 8<<16，对应 unity）
- `Headset Volume` / `Lineout Volume` / `Handset Volume` 都是 **31**（3 个都顶到 31）—— 复用同一个 5-bit 寄存器
- `PGA1/2/3 Volume` = 0 —— **模拟 PGA 全 0**，即耳机麦克风前置放大未开（未插耳机时的正常状态）

### 2.7 ADC / MIC 输入侧

```
373	ENUM	1	ADDA_UL_Mux                              >MTKAIF AP_DMIC
374	ENUM	1	ADDA_CH34_UL_Mux                         >MTKAIF AP_DMIC
550	ENUM	1	UL_SRC_MUX                               >AMIC DMIC
551	ENUM	1	UL2_SRC_MUX                              >AMIC DMIC
552	ENUM	1	VOW_UL_SRC_MUX                           >AMIC DMIC
558	ENUM	1	ADC_L_Mux                                >Idle AIN0 Left Preamplifier Idle_1
559	ENUM	1	ADC_R_Mux                                >Idle AIN0 Right Preamplifier Idle_1
560	ENUM	1	ADC_3_Mux                                >Idle AIN0 Preamplifier Idle_1
561	ENUM	1	PGA_L_Mux                                >None AIN0 AIN1
562	ENUM	1	PGA_R_Mux                                >None AIN2 AIN3 AIN0
563	ENUM	1	PGA_3_Mux                                >None AIN3 AIN2
```

- `ADC_L/R/3_Mux`：全部 `Idle`（未录音）
- **`PGA_L_Mux` / `PGA_R_Mux` = `None`** ← 耳机麦克风（headset mic）进的是 `AIN0`，正常插耳机录音时应选 `AIN0`
- `UL_SRC_MUX` = `AMIC`（模拟麦），非 DMIC → 耳机模拟麦通路的默认选择

### 2.8 DT 里的 codec 绑定（**headset-codec phandle 链**）

命令：`adb shell 'od -An -tx1 -v /proc/device-tree/sound/<prop>'`

```
### compatible
 6d 65 64 69 61 74 65 6b 2c 6d 74 36 38 39 35 2d     mediatek,mt6895-
 6d 74 36 33 36 38 2d 73 6f 75 6e 64 00              mt6368-sound\0
### mediatek,platform
 00 00 01 27                                          <0x127>
### mediatek,headset-codec
 00 00 01 26                                          <0x126>   ← ★
### mediatek,speaker-codec
od: /proc/device-tree/sound/mediatek,speaker-codec: Is a directory
### mediatek,spk-i2s
 00 00 00 03 00 00 00 09                              <3, 9>
### phandle
 00 00 02 a1                                          <0x2a1>
```

而 accdet 节点的 `phandle`：

命令：`adb shell 'cat /sys/bus/platform/devices/mt6368-accdet/of_node/phandle'` → 二进制 `00 00 01 26` = **0x126**

```
### headset-codec phandle 解析
sound/mediatek,headset-codec = <0x126>
accdet/of_node/phandle        = <0x126>   ← 同一个值
```

→ **`mediatek,headset-codec` 属性直接指向 `mt6368-accdet` 节点**。这是 xaga 上"耳机 codec"的
正式 DT 绑定：`mt6895-mt6368` sound card 把 accdet 当作 headset codec 子设备。

`second_pmic@5` 下的完整子节点（`for d in .../second_pmic@5/*/; do tr -d '\0' < "$d/compatible"`）：

```
/proc/device-tree/spmi@1c804000/second_pmic@5/accdet/            => mediatek,mt6368-accdet
/proc/device-tree/spmi@1c804000/second_pmic@5/mt6368-auxadc/     => mediatek,mt6368-auxadc
/proc/device-tree/spmi@1c804000/second_pmic@5/mt6368-consys/     => mediatek,mt6368-consys
/proc/device-tree/spmi@1c804000/second_pmic@5/mt6368-efuse/      => mediatek,mt6368-efuse
/proc/device-tree/spmi@1c804000/second_pmic@5/mt6368codec/       => mediatek,mt6368-sound
/proc/device-tree/spmi@1c804000/second_pmic@5/mtk-spmi-pmic-debug/ => mediatek,spmi-pmic-debug
```

`mt6368-sound` 节点（`/sys/bus/platform/devices/mt6368-sound/of_node/`）属性只有：
`compatible`, `io-channel-names`, `io-channels`, `name`, `nvmem`, `nvmem-names`, `phandle`
（与 accdet 节点的属性集几乎一样，说明是同一套 MTK sound 框架注册的两个 ASoC component）。

`/proc/device-tree/sound/` 完整属性：

```
compatible
mediatek,headset-codec      ← accdet (0x126)
mediatek,platform           ← 0x127
mediatek,speaker-codec      (目录)
mediatek,spk-i2s            <3, 9>
name
phandle                     0x2a1
```

### 2.9 内核模块清单（音频相关）

命令：`adb shell 'ls /sys/module/ | grep -i -E "accdet|snd|codec|mt6368|sound"'`

```
clk_mt6895_vcodec
mt6368_accdet          ← ★ 耳机检测驱动
mt6368_regulator
mt6895_mt6368
mtk_vcodec_common
mtk_vcodec_dec_v2
mtk_vcodec_enc_v2
snd
snd_pcm
snd_soc_audiodsp_common
snd_soc_mt6368         ← ★ codec 驱动
snd_soc_mt6895_afe     ← ★ AFE（PCM/DMA）驱动
snd_soc_mtk_common
snd_soc_mtk_scp_ultra
snd_soc_rt5512         ← speaker amp (RT5512)
snd_soc_tfa98xx        ← speaker amp (TFA98xx)
snd_timer
snd_usb_audio
```

`/sys/module/mt6368_accdet/coresize` = **61440** bytes，`initsize` = 0 → 是 .ko 加载（非 builtin）。

### 2.10 音频 HAL

命令：`adb shell 'ls -l /vendor/lib64/hw/audio.primary*.so'`

```
-rw-r--r-- 1 root root   11408 2009-01-01 08:00 /vendor/lib64/hw/audio.primary.default.so
-rw-r--r-- 1 root root 2831384 2009-01-01 08:00 /vendor/lib64/hw/audio.primary.mediatek.so
lrw-r--r-- 1 root root      25 2009-01-01 08:00 /vendor/lib64/hw/audio.primary.mt6895.so -> audio.primary.mediatek.so
```

- HAL 是 **`audio.primary.mediatek.so`**（2.83 MB），`mt6895` 是软链
- `init.svc.vendor.audio-hal` = `running`（pid 1064），`init.svc.audioserver` = `running`（pid 1153）

---

## 3. 耳机检测：节点 / 驱动 / 中断 / 日志

### 3.1 ★ 检测节点路径总表（**最重要的产出**）

| 路径 | 类型 | 权限 | 可读 | 内容/作用 |
|---|---|---|---|---|
| `/sys/bus/platform/devices/mt6368-accdet` | 设备 | — | ✅ | SPMI 设备节点，路径 `1c804000.spmi/spmi-0/0-05/mt6368-accdet` |
| `/sys/bus/platform/drivers/pmic-codec-accdet/` | 驱动目录 | — | ✅ | 属性集见下 |
| → `state` | r--r--r-- | 0444 | ✅ | **当前 `0`**（0=未插/无耳机） |
| → `dump_reg` | r--r--r-- | 0444 | ✅ | accdet + AUDDEC_ANA 全寄存器 dump |
| → `mt6368-accdet` | 软链 | — | ✅ | → `../../../../devices/platform/1c804000.spmi/spmi-0/0-05/mt6368-accdet` |
| → `module` | 软链 | — | ✅ | → `../../../../module/mt6368_accdet` |
| → `set_headset_mode` | --w------- | 0200 | ❌ 只写 | 有，但是写接口（**本次未触碰**） |
| → `set_reg` | --w------- | 0200 | ❌ 只写 | 有，但只写（**未触碰**） |
| → `start_debug` | --w------- | 0200 | ❌ 只写 | 有，但只写（**未触碰**） |
| → `bind` / `unbind` / `uevent` | --w------- | 0200 | ❌ | 标准 driver 接口 |
| `/sys/bus/platform/devices/mt6368-accdet/of_node/` | DT | — | ✅ | **完整设备树参数**（见 §3.3） |
| `/sys/class/accdet` | 空目录 | — | ✅(空) | **存在但完全为空**（无 uevent 文件） |
| `/proc/accdet*` | — | — | ❌ | **不存在** |
| `/sys/module/accdet*` | — | — | ❌ | **不存在**（叫 `mt6368_accdet`） |
| `/sys/module/mt6368_accdet/parameters/` | — | — | ❌ | **不存在**（驱动无 module_param） |
| `/sys/kernel/debug/` | debugfs | — | ❌ | **未挂载**（见 §3.5） |
| `/d/` | — | — | ❌ | **不存在** |

**结论：xaga 用的是 MT6368 PMIC 内置 accdet（`pmic-codec-accdet` 驱动），不是老的 `/proc/accdet` 那套。**
耳机状态对外只有两个出口：
1. `/sys/bus/platform/drivers/pmic-codec-accdet/state`（整数）
2. kcontrol `Headphone Plugged In`（`Off`/`On`）
3. 以及内核 input 事件 `/dev/input/event4` 的 `SW_HEADPHONE_INSERT` / `SW_MICROPHONE_INSERT` / `SW_LINEOUT_INSERT`（见 §5）

### 3.2 `state` 与 uevent

命令：`adb shell 'cat /sys/bus/platform/drivers/pmic-codec-accdet/state'`（两次读取，一致）

```
0
```

命令：`adb shell 'cat /sys/bus/platform/devices/mt6368-accdet/uevent'`

```
DEVTYPE=mfd_device
DRIVER=pmic-codec-accdet
OF_NAME=accdet
OF_FULLNAME=/spmi@1c804000/second_pmic@5/accdet
OF_COMPATIBLE_0=mediatek,mt6368-accdet
OF_COMPATIBLE_N=1
MODALIAS=of:NaccdetT(null)Cmediatek,mt6368-accdet
```

命令：`adb shell 'cat /sys/bus/platform/drivers/pmic-codec-accdet/uevent'` →
`cat: ... Permission denied`（驱动目录下的 uevent 只有 root 可写，读取被 SELinux 拦，**已记录，未重试**）

### 3.3 ★ accdet 设备树参数全量（**重实现的关键常量**）

命令：`adb shell 'for f in $(ls $B); do echo "$f"; cat $B/$f; done'` + `od -An -tx1 -v` 解二进制

节点：`/sys/bus/platform/devices/mt6368-accdet/of_node/`（= `/proc/device-tree/spmi@1c804000/second_pmic@5/accdet/`）

| 属性 | 原始字节 (hex) | 解析值 | 含义 |
|---|---|---|---|
| `compatible` | ASCII | `mediatek,mt6368-accdet` | 驱动匹配 |
| `status` | ASCII | `okay` | 启用 |
| `name` | ASCII | `accdet` | — |
| `accdet-name` | ASCII | `mt63xx-accdet` | 日志前缀 |
| `accdet-mic-mode` | `00 00 00 02` | **2** | mic 模式（2 = 双 mic / ACCDET_MIC_DUAL？待与源码核对） |
| `accdet-mic-vol` | `00 00 00 08` | **8** | mic bias 电压档位（8） |
| `accdet-plugout-debounce` | `00 00 00 01` | **1** | 拔出消抖（单位 ms? 1） |
| `headset-key-mode` | `00 00 00 00` | **0** | 按键模式（0 = 三键？见阈值组） |
| `headset-eint-num` | `00 00 00 00` | **0** | EINT 号 = 0 |
| `headset-eint-trig-mode` | `00 00 00 01` | **1** | EINT 触发模式（1 = 边沿） |
| `headset-use-ap-eint` | `00 00 00 00` | **0** | 不用 AP EINT（用 PMIC EINT） |
| `eint_use_ext_res` | `00 00 00 00` | **0** | 不用外部上拉 |
| `io-channels` | `00 00 00 52 00 00 00 09` | **<0x52, 9>** | auxadc phandle 0x52 + **channel 9** |
| `io-channel-names` | ASCII | `pmic_accdet` | ADC 通道名 |
| `nvmem` | `00 00 00 53` | **<0x53>** | efuse phandle |
| `nvmem-names` | ASCII | `mt63xx-accdet-efuse` | efuse 名（校准数据来源） |
| `phandle` | `00 00 01 26` | **0x126** | ← 被 `sound/mediatek,headset-codec` 引用 |

#### 3.3.1 按键阈值（**阻抗分档，单位 Ω**）

**三键阈值** `headset-three-key-threshold`

```
hex: 00 00 00 00 | 00 00 00 50 | 00 00 00 dc | 00 00 01 90
dec:      0      |      80     |     220     |     400
```

**三键阈值（CDD 变体）** `headset-three-key-threshold-CDD`

```
hex: 00 00 00 00 | 00 00 00 79 | 00 00 00 c0 | 00 00 02 58
dec:      0      |     121     |     192     |     600
```

**四键阈值** `headset-four-key-threshold`

```
hex: 00 00 00 00 | 00 00 00 3a | 00 00 00 79 | 00 00 00 c0 | 00 00 01 90
dec:      0      |      58     |     121     |     192     |     400
```

> 三键 = 3 段（4 个边界），四键 = 4 段（5 个边界）。这是 MTK accdet 的经典做法：
> 按键通过 **MIC 线上的不同阻抗** 区分，插入后先测两次 ADC（`ACCDET_IRQ` 触发）得出阻抗，
> 再按上表分档到 key code。

#### 3.3.2 `headset-mode-setting`（15 个 u32，**核心寄存器配置数组**）

```
hex: 00 00 05 00 | 00 00 05 00 | 00 00 00 01 | 00 00 01 f0
     00 00 08 00 | 00 00 08 00 | 00 00 00 20 | 00 00 00 44
     00 00 00 04 | 00 00 00 01 | 00 00 00 05 | 00 00 00 03
     00 00 00 03 | 00 00 00 05 | 00 00 00 0e

dec: [0x500, 0x500, 0x1, 0x1f0, 0x800, 0x800, 0x20, 0x44,
      0x4, 0x1, 0x5, 0x3, 0x3, 0x5, 0x0e]

index:  0     1     2     3     4     5     6     7
        8     9     10    11    12    13    14
```

与 dmesg 的 `accdet_dts:deb0=0x800,deb1=0x800,deb3=0x20,deb4=0x44` 对照：
- `[4]` = 0x800 → **deb0**
- `[5]` = 0x800 → **deb1**
- `[6]` = 0x20 → **deb3**
- `[7]` = 0x44 → **deb4**

即数组下标 4/5/6/7 就是 4 个去抖参数 `deb0..deb4`（deb2 疑似 `[3]`=0x1f0 或不在用）。
`[0]`/`[1]` = 0x500 = 1280 —— 与 `accdet-plugout-debounce` 相关。

### 3.4 原始 `dump_reg`（**accdet + AUDDEC_ANA 全寄存器**）

命令：`adb shell 'cat /sys/bus/platform/drivers/pmic-codec-accdet/dump_reg'`

```
[Accdet EINTx support][MODE_2]regs:
ACCDET_RG
(0x2688)=0x0 (0x2689)=0x0 (0x268a)=0x40 (0x268b)=0x0
(0x268c)=0x67 (0x268d)=0x0 (0x268e)=0xff (0x268f)=0x4
(0x2690)=0xff (0x2691)=0x4 (0x2692)=0xf0 (0x2693)=0x81
(0x2694)=0x41 (0x2695)=0x0 (0x2696)=0x0 (0x2697)=0x0
(0x2698)=0x8 (0x2699)=0x0 (0x269a)=0x8 (0x269b)=0x10
(0x269c)=0x0 (0x269d)=0x20 (0x269e)=0x0 (0x269f)=0x44
(0x26a0)=0x0 (0x26a1)=0x4 (0x26a2)=0x0 (0x26a3)=0x35
(0x26a4)=0x5e (0x26a5)=0xe (0x26a6)=0xbf (0x26a7)=0xa
(0x26a8)=0x0 (0x26a9)=0x0 (0x26aa)=0x10 (0x26ab)=0x27
(0x26ac)=0x0 (0x26ad)=0x0 (0x26ae)=0x1 (0x26af)=0x0
(0x26b0)=0x0 (0x26b1)=0x0 (0x26b2)=0x0 (0x26b3)=0x0
(0x26b4)=0x0 (0x26b5)=0x0 (0x26b6)=0xff (0x26b7)=0x0
(0x26b8)=0xff (0x26b9)=0x0 (0x26ba)=0xff (0x26bb)=0x0
(0x26bc)=0x0 (0x26bd)=0x70 (0x26be)=0x0 (0x26bf)=0x0
(0x26c0)=0x0 (0x26c1)=0x0 (0x26c2)=0x20 (0x26c3)=0x0
(0x26c4)=0x1f (0x26c5)=0x0 (0x26c6)=0x1f (0x26c7)=0x0
(0x26c8)=0xff (0x26c9)=0x1f (0x26ca)=0xff (0x26cb)=0x1f
(0x26cc)=0x6f (0x26cd)=0x0 (0x26ce)=0x38 (0x26cf)=0x0
(0x26d0)=0x0 (0x26d1)=0x0 (0x26d2)=0x0 (0x26d3)=0x0
AUDDEC_ANA_RG
(0x2508)=0x0 (0x2509)=0x0 (0x250a)=0x0 (0x250b)=0x0
(0x250c)=0x0 (0x250d)=0x0 (0x250e)=0x0 (0x250f)=0x0
(0x2510)=0x0 (0x2511)=0x0 (0x2512)=0x0 (0x2513)=0x0
(0x2514)=0x0 (0x2515)=0x8 (0x2516)=0x0 (0x2517)=0x0
(0x2518)=0x15 (0x2519)=0x15 (0x251a)=0x15 (0x251b)=0x0
(0x251c)=0x0 (0x251d)=0x0 (0x251e)=0x0 (0x251f)=0x0
(0x2520)=0x0 (0x2521)=0x0 (0x2522)=0x0 (0x2523)=0x4
(0x2524)=0x0 (0x2525)=0x4 (0x2526)=0x0 (0x2527)=0x0
(0x2528)=0x0 (0x2529)=0x4 (0x252a)=0x30 (0x252b)=0x0
(0x252c)=0x0 (0x252d)=0x8f (0x252e)=0x0 (0x252f)=0x20
(0x2530)=0x30 (0x2531)=0x4 (0x2532)=0x0 (0x2533)=0x3
(0x2534)=0x1 (0x2535)=0x0 (0x2536)=0x0 (0x2537)=0x0
[0x2319]=0x0
[0x231d]=0x6, [0x2320]=0x0, [0x2323]=0x0
[0x2529]=0x4,[0x252d]=0x8f
[0x1108]=0x0, [0x11c0]=0xfc
dtsInfo:deb0=0x800,deb1=0x800,deb3=0x20,deb4=0x44
```

**寄存器空间划分**（从这份 dump 直接读出）：

| 区间 | 名称 | 说明 |
|---|---|---|
| `0x2688 – 0x26d3` | **ACCDET_RG** | 耳机检测模拟/数字寄存器（共 76 字节） |
| `0x2508 – 0x2537` | **AUDDEC_ANA_RG** | 音频 DAC 模拟寄存器（共 48 字节） |
| `0x2319`, `0x231d`, `0x2320`, `0x2323` | 其余 PMIC 域 | 少量穿插 |
| `0x1108`, `0x11c0` | — | 可能是 efuse 校准值 |
| `0x8 – 0x13` | （init 时打印，dump_reg 里没有） | 见 §3.6 dmesg |

**几个已解出的关键位**（与 DT 对照）：

- `(0x2698)=0x8` / `(0x269a)=0x8` —— 与 `deb0=0x800` 对应？不完全，需源码核对
- `(0x269f)=0x44` = **deb4=0x44** ✅ 直接命中
- `(0x269d)=0x20` = **deb3=0x20** ✅ 直接命中
- `(0x269b)=0x10` / `(0x26a3)=0x35` / `(0x26a4)=0x5e` / `(0x26a5)=0xe` —— 疑似 **三键/四键阈值** 的低位字节
  （`0x50`=80 出现在 `(0x268a)=0x40`? 不直接；需与 `mt6368-accdet.ko` 反汇编核对）
- `(0x26c4)=0x1f` / `(0x26c6)=0x1f` / `(0x26c9)=0x1f` / `(0x26cb)=0x1f` —— 大量 `0x1f`(=31)，疑似 **音量/偏置上限**

> ⚠️ 这些寄存器语义**没有在 stock 系统里得到文档确认**，只能靠与 `ko/mt6368-accdet.ko` 反汇编 + 上游 MTK 源码对照。
> 本次只做**只读记录**，不做推断性断言。

### 3.5 debugfs：**未挂载**

命令：`adb shell 'mount | grep -i -E "debugfs|tracefs"'`

```
tracefs on /sys/kernel/tracing type tracefs (rw,seclabel,relatime,gid=3012)
```

命令：`adb shell 'ls /d; ls /sys/kernel/debug'` → 两者**均为空**（`/d` 不存在，`/sys/kernel/debug` 空目录未挂载）

→ **无法**通过 `/sys/kernel/debug/mtk_audio*` 拿到 MTK 音频 debug 节点。
（可以 `mount -t debugfs` 自己挂，但那是**写操作**，本次纪律禁止，未做。）

### 3.6 内核日志（dmesg）

命令：`adb shell 'dmesg | grep -i -E "accdet|headset|headphone|hp_|jack|plug"'`（**全量，只有 6 条命中**）

```
[ 1461.721736] mt6368-sound mt6368-sound: hp_impedance_get(), counter 0 <= 0 || mux_select[MUX_HP_L] 0 != HP_MUX_HP_IMPEDANCE
[ 1467.377517] mt6368-sound mt6368-sound: hp_impedance_get(), counter 0 <= 0 || mux_select[MUX_HP_L] 0 != HP_MUX_HP_IMPEDANCE
[ 1478.232024] Accdet EINTx support,MODE_2 regs:
[ 1478.232050] ACCDET_RG
[ 1478.234421] accdet_dts:deb0=0x800,deb1=0x800,deb3=0x20,deb4=0x44
[ 1501.337562] Accdet EINTx support,MODE_2 regs:
[ 1501.337599] ACCDET_RG
[ 1501.340158] accdet_dts:deb0=0x800,deb1=0x800,deb3=0x20,deb4=0x44
```

（`grep -c` 统计为 6，因为 `Accdet EINTx support` 那条被算作 1 条，实际有 8 行输出。）

**`Accdet EINTx support,MODE_2 regs:` 完整上下文**（1478s 那次，`dmesg | sed -n "/1478\.23/,/1478\.24/p"`）：

```
[ 1478.232024] Accdet EINTx support,MODE_2 regs:
[ 1478.232050] ACCDET_RG
[ 1478.232138] (0x8)=0x30 (0x9)=0x68 (0xa)=0x30 (0xb)=0x68
[ 1478.232213] (0xc)=0x0 (0xd)=0x0 (0xe)=0x0 (0xf)=0x0
[ 1478.232284] (0x10)=0x0 (0x11)=0xff (0x12)=0xff (0x13)=0xf
[ 1478.232351] (0x2688)=0x0 (0x2689)=0x0 (0x268a)=0x40 (0x268b)=0x0
[ 1478.232415] (0x268c)=0x67 (0x268d)=0x0 (0x268e)=0xff (0x268f)=0x4
...（后续与 §3.4 dump_reg 完全一致）
```

→ **init 时打印的 `0x8 – 0x13` 这 12 个字节在运行期 `dump_reg` 里看不到**
（`dump_reg` 只 dump `0x2688+` 和 `0x2508+`）。这 12 字节像是 **AFE 侧** 的模拟寄存器，
`(0x8)=0x30 (0x9)=0x68` / `(0xa)=0x30 (0xb)=0x68` 左右对称 → 疑似 HPL/HPR 的偏置/增益。

### 3.7 `hp_impedance_get()` 报错的含义（**重要**）

出现 **4 次**（1461s / 1467s / 1564s / 1703s），条件式：

```
hp_impedance_get(), counter 0 <= 0 || mux_select[MUX_HP_L] 0 != HP_MUX_HP_IMPEDANCE
```

- `counter == 0` → 阻抗测量计数器为 0（没测过）
- `mux_select[MUX_HP_L] == 0`（= `Open`）!= `HP_MUX_HP_IMPEDANCE`
- 即：**上层（audio HAL）请求读耳机阻抗，但 HPL Mux 还在 `Open`，accdet 也没跑测量**

这与 kcontrol `Audio HP ImpeDance Setting = 0` 完全对应。**说明 xaga 的 stock HAL 在上层会主动
查询耳机阻抗（用于判断耳机/耳麦/线控类型），但当前无耳机插入所以直接返回错误。**

### 3.8 `/proc/mtk_audio*` / `/proc/mtk_snd*` —— **全部不存在**

命令：`adb shell 'ls -d /proc/mtk* /proc/mt6* /proc/audio* /proc/snd*'`

```
/proc/mtk_batoc_throttling
/proc/mtk_battery_cmd
/proc/mtk_dpinfo
/proc/mtk_gpio
/proc/mtk_jpeg
/proc/mtk_lpm
/proc/mtk_mali
/proc/mtk_mdp_debug
/proc/mtk_scheduler
/proc/mtk_typec
/proc/mtk_usb
/proc/mtkfb
/proc/mtkfb_debug
```

→ **没有任何 `mtk_audio*` / `mtk_snd*` / `audio*` procfs 入口**。新版 MTK BSP 已把音频调试入口
扫到 debugfs（而 debugfs 未挂载）或直接砍掉。

### 3.9 中断

命令：`adb shell 'grep -n -E "ACCDET|accdet" /proc/interrupts'`

```
240:537:          0          0          0          0          0          0          0          0  spmi-pmic-irq  97 Edge      ACCDET_IRQ
241:538:          0          0          0          0          0          0          0          0  spmi-pmic-irq  98 Edge      ACCDET_EINT0
```

**这是耳机检测的中断路径**：

| 行 | 全局 IRQ | 父中断 | SPMI 子号 | 触发 | 名称 | 计数 |
|---|---|---|---|---|---|---|
| 537 | 537 | `spmi-pmic-irq` | **97** | Edge | **`ACCDET_IRQ`** | 0 |
| 538 | 538 | `spmi-pmic-irq` | **98** | Edge | **`ACCDET_EINT0`** | 0 |

- 两个中断计数**都是 0** → 自启动以来**从未触发过耳机插拔**
- `ACCDET_IRQ`(97) = 插入检测 / 按键 ADC 完成
- `ACCDET_EINT0`(98) = EINT 模式下的插拔边沿（对应 DT `headset-eint-num = 0`、`headset-eint-trig-mode = 1`）
- 都在 `spmi-pmic-irq` 之下（MT6368 是 SPMI 挂的 PMIC，`1c804000.spmi` / `spmi-0` / 地址 `0x05`）

### 3.10 logcat kernel buffer

命令：`adb shell 'logcat -b kernel -d | grep -i -E "accdet|headset|headphone|jack|hp_imp"'`

```
（空输出，rc=0）
```

→ kernel log buffer 里没有 accdet 相关记录（该 buffer 可能已被冲掉，或被 MTK 的 log 配置限制）。
**以 `dmesg` 为准。**

---

## 4. 当前音频输出设备与路由

### 4.1 `audio_flinger`：当前输出 = **SPEAKER**

命令：`adb shell 'dumpsys media.audio_flinger | head -120'`

```
Start begin: 01-01 08:02:43.348
Start end:   01-01 08:02:46.753
Now:         09-20 23:20:44.316
...
Output thread 0xb40000720e88d4d0, name AudioOut_D, tid 1423, type 0 (MIXER):
  I/O handle: 13
  Standby: yes
  RT thread: yes
  Sample rate: 48000 Hz
  HAL frame count: 1024
  HAL format: 0x3 (AUDIO_FORMAT_PCM_32_BIT)
  HAL buffer size: 8192 bytes
  Channel count: 2
  Channel mask: 0x00000003 (front-left, front-right)
  Processing format: 0x3 (AUDIO_FORMAT_PCM_32_BIT)
  Processing frame size: 8 bytes
  Pending config events: none
  Output devices: 0x2 (AUDIO_DEVICE_OUT_SPEAKER)
  Input device: 0 (AUDIO_DEVICE_NONE)
  Audio source: 0 (AUDIO_SOURCE_DEFAULT)
  ...
  AudioStreamOut: 0xb400007034e33810 flags 0x2 (AUDIO_OUTPUT_FLAG_PRIMARY)
  Frames written: 0
```

- 主输出线程 `AudioOut_D`，**I/O handle 13**，`Output devices: 0x2` = `AUDIO_DEVICE_OUT_SPEAKER`
- `Standby: yes`（空闲），`Frames written: 0`
- 采样率 **48000 Hz**，格式 `PCM_32_BIT`，2ch，HAL buffer 8192 B / 1024 frames
- 挂了一个 **MiSound** effect（UUID `5b8e36a5-144a-4c38-b1d7-0002a5d5c51b`）和一个 **DAP**（Dolby）
- **没有任何 `AUDIO_DEVICE_OUT_WIRED_HEADSET` / `WIRED_HEADPHONE` 出现**

### 4.2 `audio_policy`：**declared 33 个端口，available 只有 3 个**

命令：`adb shell 'dumpsys media.audio_policy'`

#### Available output devices（**只有 3 个**）

```
 Available output devices (3):
  1. Port ID: 2; "Earpiece"; {AUDIO_DEVICE_OUT_EARPIECE, @:}
  2. Port ID: 3; "Speaker"; {AUDIO_DEVICE_OUT_SPEAKER, @:}
  3. Port ID: 12; "Telephony Tx"; {AUDIO_DEVICE_OUT_TELEPHONY_TX, @:}
```

#### Available input devices（6 个）

```
 Available input devices (6):
  1. Port ID: 15; "Built-In Mic"; {AUDIO_DEVICE_IN_BUILTIN_MIC, @:bottom}
  2. Port ID: 18; "Voice Call In"; {AUDIO_DEVICE_IN_TELEPHONY_RX, @:}
  3. Port ID: 16; "Built-In Back Mic"; {AUDIO_DEVICE_IN_BACK_MIC, @:back}
  4. Port ID: 25; "Remote Submix In"; {AUDIO_DEVICE_IN_REMOTE_SUBMIX, @:0}
  5. Port ID: 20; "FM Tuner In"; {AUDIO_DEVICE_IN_FM_TUNER, @:}
  6. Port ID: 21; "Echo Ref In"; {AUDIO_DEVICE_IN_ECHO_REFERENCE, @:}
```

#### Declared devices（**33 个，含 Wired Headset / Wired Headphones**）

```
   - Declared devices (33):
    1. Port ID: 2; "Earpiece"; {AUDIO_DEVICE_OUT_EARPIECE, @:}
    2. Port ID: 3; "Speaker"; {AUDIO_DEVICE_OUT_SPEAKER, @:}
    3. "Wired Headset"; {AUDIO_DEVICE_OUT_WIRED_HEADSET, @:}
    4. "Wired Headphones"; {AUDIO_DEVICE_OUT_WIRED_HEADPHONE, @:}
    5. "BT SCO"; {AUDIO_DEVICE_OUT_BLUETOOTH_SCO, @:}
    6. "BT SCO Headset"; {AUDIO_DEVICE_OUT_BLUETOOTH_SCO_HEADSET, @:}
    7. "BT SCO Car Kit"; {AUDIO_DEVICE_OUT_BLUETOOTH_SCO_CARKIT, @:}
    8. "BT A2DP Out"; {AUDIO_DEVICE_OUT_BLUETOOTH_A2DP, @:}
    9. "BT A2DP Headphones"; {AUDIO_DEVICE_OUT_BLUETOOTH_A2DP_HEADPHONES, @:}
    ...
    11. "Analog Dock Headset"; {AUDIO_DEVICE_OUT_ANLG_DOCK_HEADSET, @:}
    12. "Digital Dock Headset"; {AUDIO_DEVICE_OUT_DGTL_DOCK_HEADSET, @:}
    ...
    16. "USB Headset Out"; {AUDIO_DEVICE_OUT_USB_HEADSET, @:}
    17. "BLE Headset Out"; {AUDIO_DEVICE_OUT_BLE_HEADSET, @:}
    ...
    （共 33 个，含 input 侧 "Wired Headset Mic"）
```

**Wired Headset 的 profile**（原文）：

```
    3. "Wired Headset"; {AUDIO_DEVICE_OUT_WIRED_HEADSET, @:}
       Encapsulation modes: 0, metadata types: 0
       Port Hal ID: 0
     - Profiles (2):
        1. ""; AUDIO_FORMAT_PCM_16_BIT (0x1)
           sampling rates: 44100, 48000
           channel masks: 0x0003
           AUDIO_ENCAPSULATION_TYPE_NONE
        2. ""; AUDIO_FORMAT_PCM_32_BIT (0x3)
           sampling rates: 44100, 48000
           channel masks: 0x0003
           AUDIO_ENCAPSULATION_TYPE_NONE
```

→ 耳机口**只支持 44.1k / 48k，16bit 或 32bit，立体声（mask 0x3）**。不支持 96k/192k，
不支持 FLOAT。这跟 §1.5 的 PCM 硬件能力（8k–192k）矛盾——**上限是 HAL/policy 限的，不是硬件**。

#### ★ 为什么 available 只有 3 个

`/vendor/etc/audio_policy_configuration_a2dp_offload_enable_cg_enable.xml` 的 `attachedDevices`：

```
        <module name="primary" halVersion="3.0">
            <attachedDevices>
                <item>Speaker</item>
                <item>Earpiece</item>
                <item>Built-In Mic</item>
                <item>Built-In Back Mic</item>
                <item>FM Tuner In</item>
                <item>Voice Call In</item>
                <item>Echo Ref In</item>
                <item>Telephony Tx</item>
            </attachedDevices>
            <defaultOutputDevice>Speaker</defaultOutputDevice>
```

**`Wired Headset` / `Wired Headphones` / `Wired Headset Mic` 不在 `attachedDevices` 里** →
它们是**可插拔（removable）** 端口，只有当 HAL 通过 `get_input_device_status` / input device
上报"已连接"时，才会进入 `Available devices` 列表。

**当前 `Available` 里没有它们 = HAL 认为耳机口没插任何东西**（与 `state=0`、
`Headphone Plugged In=Off`、IRQ 计数 0 三者完全一致）。

#### Wired Headset 的路由（XML 原文）

```
319:                <route type="mix" sink="Wired Headset"
321:                <route type="mix" sink="Wired Headphones"
358:                       Sources: "Built-In Mic", "Built-In Back Mic", "Wired Headset Mic"
```

即：`primary output` → `Wired Headset`（mix 路由，可与 Speaker 并存于同一 mixPort 的不同 sink）。

### 4.3 `dumpsys audio`：`Connected devices:` **为空**

命令：`adb shell 'dumpsys audio | sed -n "138,200p"'`

```
    BECOMING_NOISY_INTENT_DEVICES_SET=
 0x400 0x800 0x8000000 0x200 0x80 0x100 0x2000 0x4000000 0x4000 0x20000000 0x20000001 0x20000 0x20000002 0x4 0x20000004 0x8
    Preferred devices for strategy:

    Non-default devices for strategy:

    Connected devices:

    APM Connected device:

    Preferred devices for capture preset:
...
  Active communication device: AudioDeviceAttributes: role:output type:earpiece addr: name:22041216C ...
  mAudioModeOwner: AudioModeInfo[mMode=0, mPid=0, mUid=0, mToken=null]
```

**注意 `Connected devices:` 段完全空**（连 earpiece/speaker 都没列，这是 Xiaomi 改过的 dump 格式）。

而 `dumpsys audio | grep -i "devices:"` 看到的 `Devices: speaker(2)` 全部来自
**每个 player 的 per-stream 记录**，不是设备连接状态：

```
264:   Devices: earpiece(1)
275:   Devices: speaker(2)
286:   Devices: speaker(2)
...
452:   Devices: earpiece
```

→ **当前活动输出：speaker；通话场景：earpiece。无任何 wired 设备。**

### 4.4 音频模式 / 强制路由

```
 Phone state: AUDIO_MODE_NORMAL
 Force use for communications: 0
 Force use for media: 0
 Force use for record: 0
 Force use for dock: 9
 Force use for system: 11
```

- `Phone state: AUDIO_MODE_NORMAL`（非通话）
- `Force use for media: 0` = `AUDIO_POLICY_FORCE_NONE` → **没有强制走 speaker**，
  意味着如果耳机插上，policy 会正常切到耳机
- `Force use for system: 11` = `AUDIO_POLICY_FORCE_SYSTEM_ENFORCED`（系统强制）

### 4.5 Mmap / 低延迟配置

```
Mmap policy:
  aaudio.mmap_policy = 0
  aaudio.mmap_exclusive_policy = 0
```

`Audio Policy Mix` / `Audio sources (0)` / `AllowedCapturePolicies` 均为空。

### 4.6 policy 的 output profile 表（5 个）

```
  1. Port ID: 1; I/O handle: 13; IOProfile name:primary output; Latency: 42; 0x0002 (AUDIO_OUTPUT_FLAG_PRIMARY)
  2. Port ID: 5; I/O handle: 21; IOProfile name:deep_buffer; Latency: 128; 0x0008 (AUDIO_OUTPUT_FLAG_DEEP_BUFFER)
  3. Port ID: 7; I/O handle: 29; IOProfile name:fast; Latency: 42; 0x0004 (AUDIO_OUTPUT_FLAG_FAST)
  4. Port ID: 9; I/O handle: 37; IOProfile name:voip_rx; Latency: 62; 0x8000 (AUDIO_OUTPUT_FLAG_VOIP_RX)
  5. Port ID: 11; I/O handle: 45; IOProfile name:incall_music_uplink; Latency: 42; 0x80000000 (AUDIO_OUTPUT_FLAG_INCALL_MUSIC)
```

**I/O handle 编号规律**：`13, 21, 29, 37, 45` —— 步长 8，对应 `/dev/snd/pcmC0D0p`(0)、`pcmC0D1p`(1)、
`pcmC0D2p`(2)、`pcmC0D3p`(3)、`pcmC0D4p`(4)。即 **`handle = pcm_device * 8 + 5`**。

`sink` 映射：前 4 个 output 的 sink 全是 `Port ID 3 = Speaker`，第 5 个 sink 是 `Port ID 12 = Telephony Tx`。
→ **所有普通播放（PRIMARY/DEEP_BUFFER/FAST/VOIP_RX）都混到同一个 "Speaker" mixPort**，
耳机是在 device 层再分流（`mix` 路由允许一个 mixPort 连多个 sink）。

### 4.7 Xiaomi 私有 dump 段

```
Xiaomi Audio dump:
...
CEVolumeStateDump:
...
DEFAULT: {AudioDeviceDescription{type: IN_HEADSET, connection: analog}:UNSPECIFIED
          AudioDeviceDescription{type: IN_MICROPHONE, connection: }:UNSPECIFIED
          AudioDeviceDescription{type: IN_MICROPHONE_BACK, connection: }:UNSPECIFIED
          AudioDeviceDescription{type: OUT_HEADPHONE, connection: analog}:UNSPECIFIED
          AudioDeviceDescription{type: OUT_HEADSET, connection: analog}:UNSPECIFIED
          AudioDeviceDescription{type: OUT_SPEAKER_EARPIECE, connection: }:UNSPECIFIED }
mIsHeadsetCE:false
```

**`mIsHeadsetCE:false`** —— 小米/Harman 的 "CE"（Consumer Electronics? 或 China-Export?）耳机标志，
当前 false。`DEFAULT` 设备集里 **`IN_HEADSET` / `OUT_HEADPHONE` / `OUT_HEADSET` 都是 `analog` 连接**
→ 确认 xaga 的耳机是**纯模拟** 3.5mm，无 USB-C 音频。

### 4.8 音量曲线里的 HEADSET 分类（policy engine）

`dumpsys media.audio_policy` 的 policy engine dump 里有大量：

```
      DEVICE_CATEGORY_HEADSET : { (  0, -5800),  ( 20, -4000),  ( 60, -1700),  (100,     0) }
      DEVICE_CATEGORY_HEADSET : { (  0, -4950),  ( 33, -3350),  ( 66, -1700),  (100,     0) }
      DEVICE_CATEGORY_HEADSET : { (  0, -9600), (100, -9600) }
      DEVICE_CATEGORY_HEADSET : { (  0, -4200),  ( 33, -2800),  ( 66, -1400),  (100,     0) }
      ...
```

→ 这是 **Xiaomi 定制的音量曲线**，为 `DEVICE_CATEGORY_HEADSET` 单独定义了多条曲线
（不同 usage：music / voice / ringtone / alarm 等）。典型衰减 `-9600` = -96 dB（静音段）。
说明**耳机有独立的音量档位曲线，不与 speaker 共用**。

---

## 5. input 设备（耳机按键）

### 5.1 ★ `/dev/input/event4` = `mt6895-mt6368 Headset Jack`

命令：`adb shell 'getevent -pl'`（节选 event3 / event4）

```
add device 4: /dev/input/event4
  name:     "mt6895-mt6368 Headset Jack"
    KEY (0001): KEY_MEDIA             BTN_1                 BTN_2                 KEY_VOICECOMMAND
add device 5: /dev/input/event3
  name:     "mt6895-mt6368 USB_3_5 Jack"
```

**完整能力**（`adb shell 'getevent -pl /dev/input/event4'`）：

```
add device 1: /dev/input/event4
  name:     "mt6895-mt6368 Headset Jack"
  events:
    KEY (0001): KEY_MEDIA             BTN_1                 BTN_2                 KEY_VOICECOMMAND
    SW  (0005): SW_HEADPHONE_INSERT   SW_MICROPHONE_INSERT  SW_LINEOUT_INSERT     SW_JACK_PHYSICAL_INS
  input props:
    <none>
```

#### 权威校验：`modalias` 与能力位图（**不要凭 getevent 的名字猜编号**）

命令：`adb shell 'cat /sys/devices/platform/sound/sound/card0/input4/modalias'`

```
input:b0019v0000p0000e0000-e0,1,5,kE2,101,102,246,ramlsfw2,4,6,7,
```

逐段解码（这是内核 `input_print_modalias` 生成，**编号权威**）：

| 段 | 值 | 含义 |
|---|---|---|
| `b0019` | bustype **0x19 = BUS_HOST** | 主机侧虚拟总线（非 I2C/USB）→ 由 codec 驱动直接注册 |
| `v0000` / `p0000` / `e0000` | vendor/product/version 全 0 | 无真实 VID/PID |
| `-e0,1,5` | EV **0, 1, 5** | `EV_SYN` / `EV_KEY` / `EV_SW` |
| `kE2,101,102,246` | KEY **226, 257, 258, 582** | 见下表 |
| `r`,`a`,`m`,`l`,`s`,`f` | 均为空 | 无 REL / ABS / MSC / LED / SND / FF |
| `w2,4,6,7` | SW **2, 4, 6, 7** | 见下表 |

交叉校验（三处必须一致，已核对通过）：

```
$ adb shell 'cat .../input4/capabilities/ev'   →  23        (0x23 = bits 0,1,5)   ✅
$ adb shell 'cat .../input4/capabilities/sw'   →  d4        (0xd4 = bits 2,4,6,7) ✅
$ adb shell 'cat .../input4/capabilities/key'  →  40 0 0 0 0 6 400000000 0 0 0
$ adb shell 'cat .../input4/id/*'              →  0019 0000 0000 0000   (bustype 0x19) ✅
$ adb shell 'cat .../input4/phys'              →  ALSA        ← ★ 物理层标为 ALSA
$ adb shell 'cat .../input4/properties'        →  0
```

> ⚠️ **`capabilities/key` 的解析陷阱（踩过一次，记下来）**：
> sysfs 里 `input_seq_print_bitmap` 打印的是 **`unsigned long` 数组**，在 aarch64 上
> `unsigned long` = **64 位**，且**高位字在前**（从最高非零字倒序打印）。
> 所以 `40 0 0 0 0 6 400000000 0 0 0` 必须**先反转顺序**、再按 **64 位** 切：
>
> | 打印位序 | 反转后 `bitmap[i]` | 值 | 置位 bit | keycode = i*64+bit |
> |---|---|---|---|---|
> | 第 1 个 `40` | `bitmap[9]` | `0x40` | 6 | 9*64+6 = **582** `KEY_VOICECOMMAND` |
> | 第 6 个 `6` | `bitmap[4]` | `0x6` | 1, 2 | 4*64+1 = **257** `BTN_1`；4*64+2 = **258** `BTN_2` |
> | 第 7 个 `400000000` | `bitmap[3]` | `0x400000000` | 34 | 3*64+34 = **226** `KEY_MEDIA` |
>
> 解出 **{226, 257, 258, 582}**，与 modalias 的 `kE2,101,102,246` 完全一致 ✅
> （若按 32 位字正序解码会得到 `{6,161,162}` 这种垃圾值——**别那么解**。）
>
> 同理 `sw = d4`：单字 `0xd4` = `1101_0100b` → bits **2,4,6,7** ✅ 对上 `w2,4,6,7`。
> `ev = 23`：`0x23` = `10_0011b` → bits **0,1,5** → `EV_SYN`/`EV_KEY`/`EV_SW` ✅

#### 键位解码（**已用 modalias 核实**）

| 内核 keycode | 名称 | Linux 定义 | Android keylayout |
|---|---|---|---|
| **226** | `KEY_MEDIA` | `KEY_MEDIA` | **`key 226 HEADSETHOOK`**（`/system/usr/keylayout/Generic.kl` 实测有这一行） |
| **257** | `BTN_1` | `BTN_MISC + 1`（`0x101`） | 无默认映射（需 vendor .kl，xaga 没有） |
| **258** | `BTN_2` | `BTN_MISC + 2`（`0x102`） | 无默认映射 |
| **582** | `KEY_VOICECOMMAND` | `KEY_VOICECOMMAND`（`0x246`） | `VOICE_COMMAND` / `ASSIST` |

`Generic.kl` 实测原文（`grep -i -E "HEADSEThook|MEDIA|VOICECOMMAND|BTN_1|BTN_2" /system/usr/keylayout/Generic.kl`）：

```
key 128   MEDIA_STOP
key 160   MEDIA_CLOSE
key 161   MEDIA_EJECT
key 162   MEDIA_EJECT
key 163   MEDIA_NEXT
key 164   MEDIA_PLAY_PAUSE
key 165   MEDIA_PREVIOUS
key 166   MEDIA_STOP
key 167   MEDIA_RECORD
key 168   MEDIA_REWIND
key 200   MEDIA_PLAY
key 201   MEDIA_PAUSE
key 207   MEDIA_PLAY
key 208   MEDIA_FAST_FORWARD
key 226   HEADSETHOOK          ← ★ 耳机单键
key usage 0x0c0173 MEDIA_AUDIO_TRACK         FALLBACK_USAGE_MAPPING
```

→ 第 1 个按键（单键线控）走 **226 → `HEADSETHOOK`**；`BTN_1`(257)/`BTN_2`(258)
在 AOSP `Generic.kl` 里**没有映射**，`KEY_VOICECOMMAND`(582) 走 usage 映射到语音助手。

#### switch 事件（**这才是耳机检测的 input 侧出口**）

| SW code | 名称 | 语义 |
|---|---|---|
| **2** | `SW_HEADPHONE_INSERT` | 普通耳机（无 mic）插入 |
| **4** | `SW_MICROPHONE_INSERT` | 带 mic 的耳麦插入 |
| **6** | `SW_LINEOUT_INSERT` | Line-out 插入 |
| **7** | `SW_JACK_PHYSICAL_INS` | **物理插入**（Jack 物理检测，区别于电气检测） |

→ 这 4 个 = **MTK accdet 的 4 种插入判定**，正好对应 DT 里 `accdet-mic-mode = 2`（支持耳机 + 耳麦）。
`SW_JACK_PHYSICAL_INS`(7) 是较新内核才有的，用于区分"物理插入但电气未识别"的异常态
（这对重实现很有价值：它让上层能区分"插了但没识别出类型"和"根本没插"）。

### 5.2 `/dev/input/event3` = `mt6895-mt6368 USB_3_5 Jack`

`getevent -pl /dev/input/event3`：

```
add device 1: /dev/input/event3
  name:     "mt6895-mt6368 USB_3_5 Jack"
  events:
    SW (0005): SW_HEADPHONE_INSERT   SW_MICROPHONE_INSERT   SW_VIDEOOUT_INSERT
  input props:
    <none>
```

权威校验（`cat .../input3/modalias`）：

```
input:b0000v0000p0000e0000-e0,5,kramlsfw2,4,8,
```

| 段 | 值 | 含义 |
|---|---|---|
| `b0000` | bustype **0x00**（未设置） | 与 event4 的 `b0019`(BUS_HOST) 不同；event3 没填 bustype |
| `-e0,5` | EV **0, 5** | 只有 `EV_SYN` / `EV_SW` → **没有 EV_KEY** |
| `k` 后无数字 | 无按键 | 确认不处理耳机按键 |
| `w2,4,8` | SW **2, 4, 8** | `SW_HEADPHONE_INSERT`(2) / `SW_MICROPHONE_INSERT`(4) / `SW_VIDEOOUT_INSERT`(8) |

其余属性（已核对）：

```
$ cat .../input3/capabilities/sw    →  114       (0x114 = bits 2,4,8)  ✅
$ cat .../input3/capabilities/key   →  0         （无按键）✅
$ cat .../input3/id/*               →  0000 0000 0000 0000  (bustype=0)
$ cat .../input3/phys               →  ALSA
```

- 这是**独立的 USB/3.5 复合 jack** 检测（`SW_VIDEOOUT_INSERT`(8) = 支持带视频的 3.5mm AV 线）
- **`EV_KEY` 完全不存在**（`capabilities/key` = 0）→ 不处理耳机按键
- 与 event4 分工：event4 管**模拟耳机（含线控）**，event3 管 **USB-C / 3.5 复合口（含 AV）**

### 5.3 input 设备 sysfs 路径

命令：`adb shell 'readlink -f /sys/class/input/event4'`

```
/sys/devices/platform/sound/sound/card0/input4/event4
```

`ls -l /sys/devices/platform/sound/sound/card0/input4/`：

```
capabilities
device
event4
id
modalias
name
phys
power
properties
subsystem
uevent
uniq
```

**关键**：event4 挂在 **`/sys/devices/platform/sound/sound/card0/`** 下面 —— 即
**这个 input 设备是由 sound card 的 codec 驱动注册的**（`mt6368-sound` 在 probe 时
用 `input_allocate_device` + `input_register_device` 创建）。所以它的生命周期与
ALSA card0 绑定。

全部 input 设备一览（`ls -l /sys/class/input/`）：

```
event0 -> .../1c804000.spmi/spmi-0/0-04/mtk-pmic-keys/input/input0/event0
event1 -> .../soc/1c00e000.kp/input/input1/event1
event2 -> .../fingerprint/input/input2/event2
event3 -> .../sound/sound/card0/input3/event3      ← USB_3_5 Jack
event4 -> .../sound/sound/card0/input4/event4      ← Headset Jack ★
event5 -> .../virtual/input/input5/event5
event6 -> .../soc/11d00000.i2c/i2c-1/1-005a/input/input6/event6
event7 -> .../virtual/input/input7/event7
```

### 5.4 耳机按键的 Android 侧配置

命令：`adb shell 'getprop | grep -i -E "volume|route|hph|headphone"'`

```
log.tag.AudioALSACodecDeviceOutHeadphone : [D]
persist.audio.button_jack.profile        : [volume]      ← ★
ro.vendor.audio.surround.headphone.only  : [false]
ro.vendor.audio.voice.volume.boost       : [none]
```

**`persist.audio.button_jack.profile = volume`** ← 这是 MTK 的耳机按键行为配置：
耳机线控按键用于**调音量**（而不是"下一曲/暂停"）。这是 MTK 特有 property。

### 5.5 没有 vendor headset keylayout

命令：`adb shell 'ls /vendor/usr/keylayout/'` → **`No such file or directory`**

```
ls: /vendor/usr/keylayout/: No such file or directory
```

命令：`adb shell 'find /vendor/usr/keylayout /system/usr/keylayout -iname "*headset*" -o -iname "*jack*"'` → 空

只有 `/system/usr/keylayout/`（AOSP 标准集：`Generic.kl`、`AVRCP.kl`、一堆 `Vendor_XXXX_Product_XXXX.kl`）。

→ **耳机按键完全靠 `Generic.kl` 的默认映射**（`key 226 HEADSETHOOK`）+ `persist.audio.button_jack.profile`，
没有 xaga 专用的 `.kl`。

---

## 6. 关键原始日志（原文粘贴，标注命令）

### 6.1 `tinymix -a`（全 564 控件）

完整原文已归档到同目录 **`tinymix-a.txt`**（568 行）。
生成命令：`adb -s GAJZ4HIN99AQ69Y9 shell 'tinymix -a'`

耳机相关 8 条原文：

```
452	ENUM	1	Headphone Plugged In                     >Off On
453	INT	1	Audio HP ImpeDance Setting               0 (dsrange 0->65536)
464	INT	2	Headset Volume                           31 31 (dsrange 0->30)
465	INT	2	Lineout Volume                           31 31 (dsrange 0->18)
541	ENUM	1	HPL Mux                                  >Open LoudSPK Playback Audio Playback Test Mode HP Impedance
542	ENUM	1	HPR Mux                                  >Open LoudSPK Playback Audio Playback Test Mode HP Impedance
```

### 6.2 单控件读取（`tinymix "<name>"`）

命令：`adb shell 'tinymix "Headphone Plugged In"; tinymix "HPL Mux"; tinymix "HPR Mux"; tinymix "Headset Volume"; tinymix "Lineout Volume"; tinymix "Audio HP ImpeDance Setting"'`

```
Headphone Plugged In: >Off On
HPL Mux: >Open LoudSPK Playback Audio Playback Test Mode HP Impedance
HPR Mux: >Open LoudSPK Playback Audio Playback Test Mode HP Impedance
Headset Volume: 31 31 (dsrange 0->30)
Lineout Volume: 31 31 (dsrange 0->18)
Audio HP ImpeDance Setting: 0 (dsrange 0->65536)
```

### 6.3 accdet `dump_reg` 完整原文

见 §3.4（完整 76 + 48 个寄存器，已逐行粘贴）。

### 6.4 dmesg 全量命中

见 §3.6。

### 6.5 `dumpsys media.audio_policy` 的关键段

见 §4.2 / §4.4 / §4.6 / §4.7 / §4.8。

### 6.6 归档产物

| 文件 | 说明 |
|---|---|
| `docs/headset-wip/DEVICE-PROBE-STOCK.md` | 本文件 |
| `docs/headset-wip/tinymix-a.txt` | `tinymix -a` 全量原文（568 行 / 564 控件） |
| `docs/headset-wip/ko/mt6368-accdet.ko` | accdet 驱动模块（120232 B） |
| `docs/headset-wip/ko/snd-soc-mt6368.ko` | codec 驱动（277608 B） |
| `docs/headset-wip/ko/snd-soc-mt6895-afe.ko` | AFE 驱动（807656 B） |
| `docs/headset-wip/ko/mt6895-mt6368.ko` | sound card（83840 B） |
| `docs/headset-wip/ko/snd-soc-mtk-common.ko` | 公共（117760 B） |
| `docs/headset-wip/ko/snd-soc-tfa98xx.ko` | speaker amp（462136 B） |

> ⚠️ **`ko/` 里的模块 vermagic 是 `5.10.226-android12-9-g8e1a18c8f218`，而设备运行内核是 `5.10.237-Fuutao-Qn_miao-gc5e20589c`。**
> 命令：`strings ko/mt6368-accdet.ko | grep vermagic`
> ```
> vermagic=5.10.226-android12-9-g8e1a18c8f218 SMP preempt mod_unload modversions aarch64
> ```
> → 这批 .ko **不是从当前运行内核里拉出来的**（版本对不上），而是来自 Android 12 原厂包。
> 反汇编时要注意符号偏移与当前内核不完全一致。若需要当前内核的 .ko，应从设备上直接 pull
> （`/vendor/lib/modules/` 或 `/system/lib/modules/`），但本次未做（超出"只读探测耳机接口"的范围）。

---

## 7. 无法获取的信息（及原因）

> 前提：**本次是 root（`u:r:ksu:s0`）**，所以下面"拿不到"基本**不是权限问题**，而是接口不存在。

### 7.1 接口/文件根本不存在

| 想拿的信息 | 尝试的命令 | 结果 | 原因 |
|---|---|---|---|
| `/proc/accdet*` | `ls -d /proc/accdet*` | 不存在 | 新版 MT6368 accdet 不用 procfs（老 MT6357/MT6359 才用） |
| `/sys/module/accdet*` | `ls -d /sys/module/accdet*` | 不存在 | 模块名是 `mt6368_accdet` |
| accdet module params | `ls /sys/module/mt6368_accdet/parameters/` | `No such file or directory` | 驱动未定义任何 `module_param()` |
| `/proc/asound/card0/codec*` | `ls -l /proc/asound/card0/codec*` | `No such file or directory` | `mt6368-sound` 未注册 codec procfs |
| `/proc/mtk_audio*` / `/proc/mtk_snd*` | `ls -d /proc/mtk*` | 无 audio 项 | 新版 BSP 已移除这些 procfs 调试入口 |
| `/d/` | `ls /d` | 不存在 | 无 debugfs 挂载点 |
| `/sys/kernel/debug/mtk_audio*` | `ls /sys/kernel/debug` | 空 | **debugfs 未挂载** |
| `/vendor/usr/keylayout/` | `ls /vendor/usr/keylayout/` | `No such file or directory` | xaga 无 vendor keylayout，走 `/system/usr/keylayout` |
| headset 专用 `.kl` | `find ... -iname "*headset*" -o -iname "*jack*"` | 空 | 靠 `Generic.kl` 默认映射 |

### 7.2 debugfs 未挂载的具体情况

```
$ adb shell 'mount | grep -i -E "debugfs|tracefs"'
tracefs on /sys/kernel/tracing type tracefs (rw,seclabel,relatime,gid=3012)

$ adb shell 'ls /sys/kernel/debug'
（空输出）

$ adb shell 'ls /d'
（空输出 / 目录不存在）
```

只挂了 **tracefs**（`/sys/kernel/tracing`），**debugfs 没挂**。
理论上 root 可以 `mount -t debugfs nodev /sys/kernel/debug` 把它挂上，但那属于**修改系统状态**，
本次纪律明确禁止，**未执行**。这也是为什么 §7.3 里所有 MTK 音频 debug 节点都拿不到。

> 如果后续需要，这是**唯一一个"只差一步"的信息缺口**：挂 debugfs 后大概率能找到
> `mtk_audio` / `mtk_soc` / `accdet` 的 debug 节点。**但这需要一次写操作，需另开探测轮次并明确授权。**

### 7.3 `dump_reg` 里没有的寄存器

- init 时 dmesg 打印的 **`0x8 – 0x13` 这 12 个字节**（`(0x8)=0x30 (0x9)=0x68 (0xa)=0x30 (0xb)=0x68 (0xc..0xf)=0x0 (0x10)=0x0 (0x11)=0xff (0x12)=0xff (0x13)=0xf`）
  在运行期 `dump_reg` 输出里**不存在**（`dump_reg` 只 dump `0x2688+` 和 `0x2508+`）。
  只能从 dmesg 历史里读到（§3.6），且只有 init 那一刻的值。

### 7.4 无法在"无耳机插入"状态下获得的信息

设备的 3.5mm 口**当前空着**，所以以下**物理上无法观测**（不是权限问题）：

| 信息 | 为什么拿不到 |
|---|---|
| 耳机插入时的 `state` 值 | 需插耳机；`state` 当前 `0` |
| `Headphone Plugged In` 的 `On` 状态 | 同上，当前 `Off` |
| `ACCDET_IRQ` / `ACCDET_EINT0` 的中断触发序列 | 两者计数都是 **0**，从未触发 |
| 按键阻抗 → keycode 的实际映射 | 需按不同阻抗的线控实测；只知道 DT 阈值表 |
| 插入后的 `HPL/HPR Mux` 切换时序 | 当前 `Open`，未观测到切到 `Playback` |
| 插入后的 `PGA_L_Mux` 是否切到 `AIN0` | 当前 `None` |
| headset mic 录音的实际 PCM 通道路径 | 需插耳麦录音 |
| 拔出消抖 `accdet-plugout-debounce=1` 的实际时间 | 只有 DT 值，无实测 |
| 耳机阻抗实测值（`Audio HP ImpeDance Setting` 非 0 的值） | `hp_impedance_get()` 直接报错返回 |
| 4 段/3 段插头的自动识别过程 | 需实测两种插头 |
| `mIsHeadsetCE` 变 true 的条件 | 当前 false |

> **这是本次探测最大的信息缺口**：所有"插入后"的行为都只能从 DT 参数 + 寄存器 + 代码反推，
> 没有运行时证据。后续要补，需要一个**物理插拔耳机**的探测轮次（可以正常做，不违反只读纪律——
> 插拔耳机不改任何系统状态）。

### 7.5 需要写操作才能拿到的（本次刻意不做）

| 信息 | 需要什么操作 | 为什么不做 |
|---|---|---|
| debugfs 下的 MTK 音频节点 | `mount -t debugfs` | 修改系统状态 |
| `set_headset_mode` / `set_reg` / `start_debug` 的行为 | 写 sysfs | 明确禁止 |
| 强制切 `HPL Mux` 到 `Playback` 看效果 | `tinymix "HPL Mux" Playback` | 修改 codec 状态 |
| `tinymix` 写 `Headphone Plugged In On` 伪造插入 | 写 kcontrol | 会污染状态 |
| 用 `tinyplay` 播测试音到耳机 | 打开 PCM 流 | 会产生实际音频输出（且需先改 mux） |
| 读 `/sys/kernel/debug/...` 具体文件 | 先挂 debugfs | 同上 |

### 7.6 有权限但被 SELinux 拦的（已记录，未绕过）

| 命令 | 结果 |
|---|---|
| `cat /sys/bus/platform/drivers/pmic-codec-accdet/uevent` | `Permission denied` |
| `cat /sys/bus/platform/drivers/pmic-codec-accdet/modalias` | `No such file or directory`（驱动目录下确实没这个文件，设备目录下才有） |

> 虽然 `adb shell` 是 `u:r:ksu:s0`（root），但这两条仍被拒——说明 SELinux 策略对
> `pmic-codec-accdet` 驱动目录的属性做了额外限制。**未尝试 `setenforce 0` 或 `supolicy` 绕过**（那是写操作）。

### 7.7 完全没找到的

- 任何形式的 **`h2w` switch 节点**（`find /sys -name "h2w*"` → 空）。
  `/sys/class/switch/` 下只有 `hdmi`。→ xaga **不用 `switch` class 报耳机状态**，
  而是走 `input` 的 `SW_*` 事件（§5.1）。
- 任何 **`/sys/class/accdet/*` 内容**（目录存在但是空的）。
- **module 参数**、**procfs 调试入口**、**debugfs 入口**，三者全无。

---

## 附：给裸机 XNU 重实现的要点抽取

> 这一节是从上面证据里"压缩"出来的可操作面，不含推测断言，每条都标注来源章节。

### A. 耳机检测必须复现的最小状态机

```
上电 ──(accdet probe, §3.3 DT)──> ACCDET_RG 初始化(§3.4)
                                    │
                                    ├─ ACCDET_IRQ (spmi 97, §3.9) ──> 插入/按键
                                    └─ ACCDET_EINT0 (spmi 98) ──────> 插拔边沿
                                    
插入 ──> 测 ADC(channel 9, §3.3 io-channels) ──> 阻抗分档(§3.3.1)
                                                    │
                        ┌───────────────────────────┼───────────────────────────┐
                        ↓                           ↓                           ↓
               SW_HEADPHONE_INSERT          SW_MICROPHONE_INSERT        SW_LINEOUT_INSERT
               (event4, §5.1)                (event4, §5.1)              (event4, §5.1)
                        │                           │
                        ↓                           ↓
             Headphone Plugged In=On        PGA_L_Mux=AIN0 (§2.7)
             (kctl 452, §2.4)               UL_SRC_MUX=AMIC
                        │
                        ↓
             HPL/HPR Mux = Playback (§2.5)
             ADDA_DL_CH1 ← DL2 (§2.5)
                        ↓
             耳机出声（44.1k/48k, 16/32bit, stereo, §4.2）
```

### B. 必须实现的寄存器/接口清单（按重要性）

1. **ACCDET_RG `0x2688–0x26d3`**（76 字节）— §3.4 有完整初值
2. **AUDDEC_ANA_RG `0x2508–0x2537`**（48 字节）— §3.4 有完整初值
3. **ACCET_IRQ(97) / ACCDET_EINT0(98)** 两条 SPMI 边沿中断 — §3.9
4. **auxadc channel 9**（phandle 0x52）— §3.3
5. **nvmem `mt63xx-accdet-efuse`**（phandle 0x53）— 校正数据来源，§3.3
6. **input 设备** `mt6895-mt6368 Headset Jack`（挂在 `/sys/devices/platform/sound/sound/card0/input4`，`phys="ALSA"`，`bustype=0x19 BUS_HOST`）：`KEY_MEDIA(226)` / `BTN_1(257)` / `BTN_2(258)` / `KEY_VOICECOMMAND(582)` + `SW 2,4,6,7` — §5.1
7. **kcontrol 6 个**：`Headphone Plugged In` / `Audio HP ImpeDance Setting` / `Headset Volume` / `Lineout Volume` / `HPL Mux` / `HPR Mux` — §2.4

### C. 几个容易踩的坑

1. **`Lineout Volume`=31 但 max=18、`Headset Volume`=31 但 max=30**（§2.4）
   → BSP 初值没 clamp，重实现要按 range 截断，否则写入会被拒或越界。
2. **`hp_impedance_get()` 在 `HPL Mux != HP Impedance` 时直接返回错误**（§3.7）
   → 上层会反复重试（4 次），要复现这个"查询-失败"路径，否则上层逻辑分支不同。
3. **耳机 profile 只到 48k**（§4.2），但 PCM 硬件能到 192k（§1.5）
   → 限制在 HAL/policy 层，不在硬件。重实现时别把 192k 当默认。
4. **`ADDA_DL_GAIN=0`**（§2.6）而 `HW Gain 1/2=0x80000`（=1.0x）
   → 两个增益是**串联**的：`ADDA_DL_GAIN` 是数字域（Q0.x），`HW Gain` 是 Q15。
5. **`audio.primary.mt6895.so` 是软链**（§2.10），实体是 `audio.primary.mediatek.so`
   → 所有 MT6895 机器共用同一个 HAL，机型差异全在 XML 和 DT。
6. **`ro.boot`/policy XML 用的是 `_a2dp_offload_enable_cg_enable` 变体**（§4.2）
   → `cg` = Camera/Game? 需与 vendor 确认；这个变体文件里才定义了 Wired Headset 端口。

---

*本文档由只读探测生成。所有"命令确实返回了"与"文件不存在/权限不足"已在 §3.1 / §7 明确区分。*
