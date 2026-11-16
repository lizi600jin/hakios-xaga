# DAPM-OBSERVE-02：idle → 正常框架播放 → idle 对照

> 2026-11-16 13:39，DSH。**未刷机、不占 XNU trial、未改驱动、未手工 tinymix、
> 未用 tinyplay、未读 codec 寄存器、未碰 spmi_access。**
> 一次正常框架播放，60 秒自然结束。设备已清理回原状。

---

## 0. 一句话

**29 个目标 widget 全部走 `Off → On(播放) → On → Off(结束)`。`DC Trim` 在四次采样中都是 `Off`。**

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

## 2.5 四个采样点的实际主机时间（原文，不复述间隔）

| 采样 | uptime | host 时间 |
|---|---|---|
| **A**（播放前） | 51329.01 | **2026-11-16T13:37:41+08:00** |
| **B1**（播放中第 1 组） | 51343.39 | **2026-11-16T13:37:55+08:00** |
| **B2**（播放中第 2 组） | 51355.17 | **2026-11-16T13:38:07+08:00** |
| **C**（播放后） | 51414.27 | **2026-11-16T13:39:06+08:00** |

**相邻间隔**：A→B1 **14 秒**、B1→B2 **12 秒**、B2→C **59 秒**。

**这四段间隔是实测，不是我原来写的"恰好再 2 秒"或"结束后 15 秒内"。**
**采样点之外的状态没有被观察。**

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

| 阶段 | `DC Trim`（四次采样） |
|---|---|
| A（播放前，host 13:37:41） | **Off** |
| B1（播放中，host 13:37:55） | **Off** |
| B2（播放中，host 13:38:07） | **Off** |
| C（播放后，host 13:39:06） | **Off** |

**🔴 修正后的表述（Codex 审定）：**

**只能说「A/B1/B2/C 四次采样均为 Off」，不能说「连续全程无瞬态」。**

理由：四次采样之间有空隙（A→B1 间隔 14 秒、B1→B2 间隔 12 秒、B2→C 间隔 59 秒），
**采样点之外的瞬态开合没有被观察到**。

**当前没有证据表明本次播放触发了校准，也没有需求因此强制重新校准。**

**仍然成立**：`DC Trim` 的 `POST_PMD` 回调触发的是异步线程（`kthread_run`），
DAPM 状态不反映线程运行 —— 所以「四次采样 Off」既不证明 trim 没跑，也不证明它跑了。

---

## 5. 播放确实发生的证据（帧推进 + 路由）

### 5.1 `AudioOut_15` 线程

**同一线程指针 `0xb400007a20fcf4d0`、`I/O handle: 21`（B2 与 C 都是这一个）：**

| 字段 | B2（host 13:38:07） | C（host 13:39:06） | **delta** |
|---|---|---|---|
| `Standby` | **`no`** | `yes` | — |
| `Sample rate` | **48000 Hz** | — | — |
| `Output devices` | **`0x8 (AUDIO_DEVICE_OUT_WIRED_HEADPHONE)`** | — | — |
| **`Frames written`** | **3870720** | **5943296** | **+2072576** |
| **`Total writes`** | **1890** | **2902** | **+1012** |
| `Last write occurred` | 55 ms | 23405 ms | — |

**帧继续推进的真实证据**：`Frames written` 3870720 → 5943296（**+2,072,576 帧**），
`Total writes` 1890 → 2902（**+1012 次**）。

**🔴 撤回**：原 README 用 `Timestamp stats n=1496 → 1934` 说"帧推进 +438" ——
**`n` 不是音频帧数**，该推论作废。**B1 的完整 audio_flinger dump 未归档，
所以 B1 的 `n=1496` 不能当独立帧证据，标缺失。**

### 5.2 C 阶段

`Standby: yes`（已回待机），**`Last write occurred (msecs): 23405`**（C 采样时距最后一次写 23.4 秒）。

> 原 README 写 `9796` 是错的 —— 那是**上一次播放**留下的值，不是本次。已按 C 归档原文更正。

### 5.3 三个 playback PCM 全部 `closed`

**注意：PCM 的 `status` 一直显示 `closed`，即使播放中也是。**

**所以 `pcm0p` 等不是本次播放的实际端点** —— 与 [CMD] 的提醒一致
（"不预设 pcm0p 是实际播放端点"）。

**真实证据是 `AudioOut_15` 的 `Standby: no` + `Frames written` 与 `Total writes` 递增 + `Last write` 新鲜 + 耳机路由。**

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
| 6 | **`AIF_RX` 在 B 阶段 `On`；其上游 `AIF1 Playback`（自动 DAI widget）显示 inactive** |

### 未证明

| 未证明 | 说明 |
|---|---|
| **widget 的 On 意味着电气上真的通电** | 只是 DAPM 软件状态 |
| **`DCXO` 电气必要性** | 只知道 `CLK_BUF` 在播放时 On |
| **实际出声** | 耳机不戴耳上，没有听觉确认 |
| **`DC Trim` 的校准是否运行** | DAPM 状态不反映异步线程 |
| ~~`AIF_RX` inactive 意味着走 ADDA 还是 SGEN~~ | **已撤回** —— active 状态属 `AIF1 Playback` widget，不构成绕过证据 |

### 🔴 `AIF_RX` 的解释已修正（Codex 审定）

**我原来的表述是「`AIF_RX` 是 On 但仍显示 inactive，所以实际数据流可能绕过 AIF1」。
这条撤回。**

**Codex 的判定：**

1. **`AIF_RX` 是普通 aif widget。**
2. **`soc-dapm.c:4402..4438` 的 active 更新对象，是 `snd_soc_dai_get_widget()` 返回的
   自动 DAI widget** —— codec 这边名称是 **`AIF1 Playback`**，**不是 `AIF_RX`**。
3. **B1 原文里有 `in  "static"  "AIF1 Playback"` → `AIF_RX` 这条边**，
   说明 `AIF_RX` 的上游正是 `AIF1 Playback`。
4. **ADDA/MTKAIF 与 codec AIF1 不是互斥路径** —— 不能因为 `AIF_RX` 自身不显示 active
   就推断绕过了 AIF1。
5. **Codex 同时撤回了上一条 [CMD] 里「要求 `AIF_RX` 自身 active」的判据** ——
   那条判据不准确，**不据此判本实验失败**。

**修正后的结论**：`AIF_RX` 在播放时 `On`，其上游 `AIF1 Playback` 才是承载 active
状态的自动 DAI widget。**这不构成「绕过 AIF1」的证据。**

> 原始 B1 行（见 `B1-playing.txt`）：
> ```
> AIF_RX :: AIF_RX: On  in 1 out 2| stream AIF1 Playback inactive|
>   out  "Normal Path" "DAC In Mux"| in  "static" "AIF1 Playback"|
>   in  "static" "DL_GPIO"| in  "static" "DL Digital Clock CH_1_2"|
> ```
