# DAPM-OBSERVE-02：idle → 正常框架播放 → idle 对照

> 2026-11-16 13:39，DSH。**未刷机、不占 XNU trial、未改驱动、未手工 tinymix、
> 未用 tinyplay、未读 codec 寄存器、未碰 spmi_access。**
> 一次正常框架播放，60 秒自然结束。设备已清理回原状。

---

## 0. 一句话

**29 个目标 widget 全部走 `Off → On(播放) → On → Off(结束)`，只有 `DC Trim` 全程 `Off`。**

**播放经 `AudioOut_15`、输出设备 `0x8 AUDIO_DEVICE_OUT_WIRED_HEADPHONE`，
帧统计 `n=1496 → 1934` 有真实推进。**

---

## 1. 播放入口（framework，非 tinyplay）

**`resolve-activity` 给定：**

```
com.miui.mediaviewer/com.miui.musicplayer.activity.AudioActivity
```

**启动命令：**

```bash
adb shell 'am start -a android.intent.action.VIEW \
  -d "file:///data/local/tmp/dapm-observe-02-reference.wav" -t "audio/x-wav"'
# rc=0
```

### 一个必须记录的坑

**首次 `am start` 报 `rc=0` 但 PCM 全 `closed`。**
原因：**屏幕锁着**，`mCurrentFocus=NotificationShade`，`isKeyguardShowing=true`。

**`wm dismiss-keyguard` 之后**：`mCurrentFocus` 变成 AudioActivity，播放恢复。

**教训：`am start` 返回 0 不等于在播放；要查焦点与 PCM/blender 状态。**

---

## 2. 测试文件（Codex 生成，原样使用）

| 项 | 值 |
|---|---|
| 主机路径 | `hakios-mtk/logs/headset-chain-20261116/reference-48k-stereo-60s.wav` |
| 字节 | **11520044** |
| SHA256 | **`c6586d5173c366fc66c37a71743919774b643f4776d6d2bccb53e088baeaba97`** |
| 设备路径 | `/data/local/tmp/dapm-observe-02-reference.wav` |
| **设备上 sha256** | **同上，一致** |
| 参数 | 48kHz / s16le / 双声道 / 60s，左 1000Hz 右 1500Hz，peak 327（≈ -40.02 dBFS），首尾 100ms 渐变 |

**未改变信号。播放结束后已删除该临时文件。**

**媒体音量保持原值**（`dumpsys audio` 记录 STREAM_MUSIC `Muted: false`，
**未升到最大**）。

---

## 3. 三阶段状态表（30 个 widget 全文见 A/B1/B2/C 四个文件）

| widget | A（前） | B1（播放5s） | B2（+2s） | C（后） |
|---|---|---|---|---|
| `AFE_DL_SRC` | Off | **On** | **On** | Off |
| `AFE_ON` | Off | **On** | **On** | Off |
| `AIF_RX` | Off | **On** | **On** | Off |
| `AUDGLB` | Off | **On** | **On** | Off |
| `AUDIF_CK` | Off | **On** | **On** | Off |
| `AUDNCP_CK` | Off | **On** | **On** | Off |
| `AUD_CK` | Off | **On** | **On** | Off |
| **`CLKSQ Audio`** | Off | **On** | **On** | Off |
| **`CLK_BUF`** | Off | **On** | **On** | Off |
| `DAC In Mux` | Off | **On** | **On** | Off |
| `DACL` | Off | **On** | **On** | Off |
| `DACR` | Off | **On** | **On** | Off |
| **`DC Trim`** | **Off** | **Off** | **Off** | **Off** ← **全程不变** |
| `DL Digital Clock` | Off | **On** | **On** | Off |
| `DL Power Supply` | Off | **On** | **On** | Off |
| `HPL Mux` | Off | **On** | **On** | Off |
| `HPR Mux` | Off | **On** | **On** | Off |
| `HP_ANA_TRIM` | Off | **On** | **On** | Off |
| `HP_MUTE` | Off | **On** | **On** | Off |
| `HP_PULL_DOWN` | Off | **On** | **On** | Off |
| `Headphone L` | Off | **On** | **On** | Off |
| `Headphone R` | Off | **On** | **On** | Off |
| `IBIST` | Off | **On** | **On** | Off |
| `LDO` | Off | **On** | **On** | Off |
| `LDO_REMOTE` | Off | **On** | **On** | Off |
| `NCP` | Off | **On** | **On** | Off |
| `NV_REGULATOR` | Off | **On** | **On** | Off |
| `SDM` | Off | **On** | **On** | Off |
| `ZCD13M_CK` | Off | **On** | **On** | Off |
| `mt6368_vaud18` | Off | **On** | **On** | Off |

**统计：29 开 + 1 不变（`DC Trim`）。零个出现 B 阶段 Off 或 C 阶段残留 On。**

---

## 4. 🔴 `DC Trim` 全程 Off —— 直接回答审定修正 2

**审定文案说：`mt6368_codec_init_reg` 末尾的 `enable_trim_buf(priv, true)` 会触发
`DC trim` widget power-down，且 init 由 codec probe 调用，
所以"不需要先播放一次，启动同步本身有触发该异步工作的路径"。**

**实测结果：**

| 阶段 | `DC Trim` |
|---|---|
| A（播放前，已开机约 14 小时） | **Off** |
| B1（播放中） | **Off** |
| B2（播放中） | **Off** |
| C（播放后） | **Off** |

**即使在完整播放过程中，`DC Trim` 也没有变成 On。**

**这与审定文案的关系要说清：**

- **它不否定文案**：文案说的是"有触发路径"，不是"一定会触发"
- **它补充了一个实测事实**：这次播放从头到尾，`DC Trim` 的 DAPM 状态没有变化
- **但它不能证明 trim 没跑** —— `DC Trim` 的 `POST_PMD` 回调触发的是**异步线程**
  （`kthread_run`），线程可能在 widget 状态变化之外执行，DAPM 状态不反映线程运行

**所以正确表述：`DC Trim` 的 DAPM 状态在本次播放全程为 Off；
这不等于 trim 校准线程没有运行，也不等于它运行了。**

---

## 5. 播放确实发生的证据（帧推进 + 路由）

### 5.1 `AudioOut_15` 线程

| 字段 | B2（播放中） |
|---|---|
| `Standby` | **`no`** |
| `Sample rate` | **48000 Hz** |
| `Channel mask` | `0x3 (front-left, front-right)` |
| **`Output devices`** | **`0x8 (AUDIO_DEVICE_OUT_WIRED_HEADPHONE)`** ✅ |
| **`Timestamp stats`** | **`n=1934`**（B1 时 `n=1496`）→ **帧推进 +438** |
| **`Last write occurred`** | **55 ms**（新鲜） |

### 5.2 C 阶段

`Standby: yes`（已回待机），`Last write occurred (msecs): 9796`（9.8 秒前最后一次写）。

### 5.3 三个 playback PCM 全部 `closed`

**注意：PCM 的 `status` 一直显示 `closed`，即使播放中也是。**

**所以 `pcm0p` 等不是本次播放的实际端点** —— 与 [CMD] 的提醒一致
（"不预设 pcm0p 是实际播放端点"）。

**真实证据是 `AudioOut_15` 的 `Standby: no` + `n` 递增 + `Last write` 新鲜 + 耳机路由。**

---

## 6. 路由确认：有线耳机

| 证据 | 值 |
|---|---|
| `AudioOut_15` 的 `Output devices` | **`0x8 (AUDIO_DEVICE_OUT_WIRED_HEADPHONE)`** |
| `AudioOut_D` 的 `Output devices` | `0x2 (AUDIO_DEVICE_OUT_SPEAKER)`，但 `Standby: yes` |
| `dumpsys audio` 的 `Active communication device` | `type:headphone` |

**播放走的是耳机线程，不是扬声器线程。**

---

## 7. 清理与最终状态

| 步骤 | 结果 |
|---|---|
| `am force-stop com.miui.mediaviewer` | 已执行 |
| 删除 `/data/local/tmp/dapm-observe-02-reference.wav` | **已删除**（`No such file or directory`） |
| 删除 `/data/local/tmp/obs02-screen.png` | 已删除 |
| `umount /sys/kernel/debug` | 已执行，`grep -c debugfs /proc/mounts` = **0** |
| `boot_a` | **未改动**（本任务无 boot 分区操作） |
| 长按电源 / force / 杀系统服务 | **均未做** |

---

## 8. 证据边界

### 已证明

| # | 结论 |
|---|---|
| 1 | **框架播放路径可用**（`am start` + AudioActivity） |
| 2 | **播放真实发生**（`n=1496→1934`、`Last write 55ms`、`Standby: no`） |
| 3 | **路由到有线耳机**（`0x8 WIRED_HEADPHONE`） |
| 4 | **29 个 widget 在播放时 On、结束后 Off** |
| 5 | **`DC Trim` 全程 Off** |
| 6 | **`AIF_RX` 在 B 阶段是 `On` 但仍显示 `stream AIF1 Playback inactive`** |

### 未证明

| 未证明 | 说明 |
|---|---|
| **widget 的 On 意味着电气上真的通电** | 只是 DAPM 软件状态 |
| **`DCXO` 电气必要性** | 只知道 `CLK_BUF` 在播放时 On |
| **实际出声** | 耳机不戴耳上，没有听觉确认 |
| **`DC Trim` 的校准是否运行** | DAPM 状态不反映异步线程 |
| **`AIF_RX` inactive 但 On 意味着走 ADDA 还是 SGEN** | 需要 Codex 从源码判 |

### 🔴 `AIF_RX` 这条值得单独交给 Codex

**B 阶段 `AIF_RX: On  in 1 out 2`，但最后一行仍是 `stream AIF1 Playback inactive`。**

**这与审定修正 4 呼应**：machine 源码里 `Primary Codec` 后端是把 CPU `ADDA` 接到
`mt6368-snd-codec-aif1`，且 init 选择 `MTKAIF_PROTOCOL_2_CLK_P2`。

**所以实际数据流可能不走 AIF1 DAI，而走 ADDA/MTKAIF 路径。** 这需要 Codex 判定。
