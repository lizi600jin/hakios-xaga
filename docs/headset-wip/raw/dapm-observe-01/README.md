# DAPM-OBSERVE-01：原厂 Android 的 DAPM 只读观测

> 2026-11-16 13:32，DSH。**未刷机、未构建、不占 trial、未播放、未改 mixer、
> 未读 codec 寄存器、未碰 spmi_access。**
> boot_a 保持 `a63b2e35…`，耳机插着不戴耳上。

---

## 0. 一句话

**debugfs 一次挂载成功，ASoC 的 DAPM 接口全部可读。30 个目标 widget 全部读到、
全部 `Off`，三个 playback PCM 全部 `closed` —— 这是 idle 状态的干净基线。**

**卸载也成功，挂载表已恢复原状。**

---

## 1. 仓库主机侧核对（任务 1）

**范围**：`docs/headset-wip` 下 `git ls-tree -r HEAD` 列出的 **346 个已跟踪文件**。
**方法**：逐文件读实际内容算 `sha1("blob <len>\0" + data)`，与 HEAD 的 blob ID 比对。

| 结果 | 数量 |
|---|---|
| **blob ID 一致** | **345** |
| **缺失（ABSENT）** | **0** |
| **内容不符** | **1** |

**唯一不符**：`docs/headset-wip/findings/GPUEB-INTEGRATION.patch`（50153 字节）

**性质**：**工作区里有未提交的改动，不是损坏。**
差异为 `@@ -41,6 +41,42 @@` → `@@ -41,13 +41,49 @@` 及插入 PMIF 注释块 ——
这是 2026-11-15 为让该补丁能在当前树上 `git apply` 而修的上下文偏移。

**按指令：不覆盖、不提交、不修复，仅单列。**

完整审计输出：`_bak-709-corruption-20261116-132442/host-blob-audit.txt`

### 关于 9 个空对象的时间戳

**只能作为线索，不写成根因已定。** 它们落在 `23:23:03.186`–`23:23:03.200`
的 40 毫秒内，与 reflog 里 709 提交的时间戳一致 —— 这**提示**一次写入瞬间的批量失败，
**但没有证据**证明是 git 写入路径、文件系统还是其他原因。

**空对象继续留在备份（`empty-objects-moved/`，9 个 `.removed` + 9 个原件副本），
绝不放回。`ACTIONS-LOG.md` 原样留存。**

---

## 2. debugfs 挂载（任务 2）

| 项 | 值 |
|---|---|
| SELinux（只查） | **`Enforcing`**，`/sys/fs/selinux/enforce` = `1` |
| 挂载前 `grep debugfs /proc/mounts` | **0 条**（确认未挂载） |
| 命令 | `mount -t debugfs debugfs /sys/kernel/debug` |
| **返回码** | **`0`** |
| stderr | 空 |
| **挂载后条目** | **`debugfs /sys/kernel/debug debugfs rw,seclabel,relatime 0 0`** |
| 挂载前挂载表 | `mounts-before.txt`（187 行） |
| 挂载后挂载表 | `mounts-after.txt` |

**SELinux 是 Enforcing 但没有阻止挂载。**

---

## 3. asoc 结构与组件路径（任务 3）

```
/sys/kernel/debug/asoc/
├── components        （文件，不是目录）
├── dais
└── mt6895-mt6368/    ← card
    ├── dapm/         ← card 级：Ext_Speaker_Amp, bias_level
    ├── mt6368-sound/dapm/   ← **codec 的 widget 都在这里**
    ├── 1e100000.mt6895-afe-pcm
    ├── snd_audio_dsp / snd_scp_ultra / mt_soc_offload_common
    ├── speaker_amp.6-0034 / speaker_amp.6-0035
    └── Playback_* / Capture_* / Hostless_*
```

**关键路径**：
`/sys/kernel/debug/asoc/mt6895-mt6368/mt6368-sound/dapm/<widget>`

**codec 组件目录下共 126 个 widget。**

---

## 4. 30 个目标 widget 的状态（全文见 `widgets-state.txt`）

### 4.1 全部读到，**全部 `Off`**

| widget | 状态 | 寄存器标注 |
|---|---|---|
| `CLK_BUF` | **Off** | — |
| `mt6368_vaud18` | **Off** | — |
| `AUDGLB` | **Off** | `R9634(0x25a2) mask 0x10` |
| **`CLKSQ Audio`** | **Off** | **`R9527(0x2537) mask 0x1`** |
| `AUDNCP_CK` | **Off** | `R8972(0x230c) mask 0x40` |
| `ZCD13M_CK` | **Off** | `R8972(0x230c) mask 0x20` |
| `AUD_CK` | **Off** | `R8972(0x230c) mask 0x2` |
| `AUDIF_CK` | **Off** | `R8972(0x230c) mask 0x4` |
| `DL Power Supply` | **Off** | — |
| `DL Digital Clock` | **Off** | — |
| `NCP` | **Off** | `R9352(0x2488) mask 0x1` |
| `SDM` | **Off** | — |
| `AFE_ON` | **Off** | `R9096(0x2388) mask 0x1` |
| `AFE_DL_SRC` | **Off** | `R9097(0x2389) mask 0x1` |
| `LDO` | **Off** | `R9635(0x25a3) mask 0x1` |
| `LDO_REMOTE` | **Off** | `R9635(0x25a3) mask 0x4` |
| `NV_REGULATOR` | **Off** | `R9635(0x25a3) mask 0x10` |
| `IBIST` | **Off** | `R9632(0x25a0) mask 0x10` |
| `HP_ANA_TRIM` | **Off** | `R9613(0x258d) mask 0x80` |
| `HP_MUTE` | **Off** | — |
| `HP_PULL_DOWN` | **Off** | — |
| `DAC In Mux` | **Off** | — |
| `DACL` | **Off** | — |
| `DACR` | **Off** | — |
| `HPL Mux` | **Off** | `in 0 out 1` |
| `HPR Mux` | **Off** | `in 0 out 1` |
| `AIF_RX` | **Off** | **`stream AIF1 Playback inactive`** |
| `Headphone L` | **Off** | `in 0 out 1` |
| `Headphone R` | **Off** | `in 0 out 1` |
| **`DC Trim`** | **Off** | **`R9624(0x2598) mask 0x40`** |

**缺失项：0 个。** 30 个全部匹配到实际 widget 名
（命令里的下划线已按实际名称还原为空格，如 `HP_ANA_TRIM` → `HP_ANA_TRIM`、
`Headphone_L` → `Headphone L`）。

### 4.2 一个重要的结构信息

**`DL Power Supply` 的 `in` 列表**列出了它的全部上游 supply：

```
in "static" "IBIST"
in "static" "NV_REGULATOR"
in "static" "LDO_REMOTE"
in "static" "LDO"
in "static" "ESD_RESIST"
in "static" "AUDIF_CK"
in "static" "AUD_CK"
in "static" "ZCD13M_CK"
in "static" "AUDNCP_CK"
in "static" "CLKSQ Audio"
in "static" "AUDGLB"
in "static" "mt6368_vaud18"
in "static" "CLK_BUF"
```

**这 13 个就是 `DL Power Supply` 的上游链** —— 与 vendor 源码的
`SUPPLY_SEQ_*` 编号顺序一致（`CLK_BUF` → `mt6368_vaud18` → `AUDGLB` → … → `CLKSQ Audio` → …）。

**`CLK_BUF` 的 `out` 列表里有 `DL Power Supply`、`VOW TX`、`AIFTX_Supply`，
说明它一开就带动三条路径。**

### 4.3 `DC Trim` 的 in/out 是空的

```
DC Trim: Off  in 0 out 0 - R9624(0x2598) mask 0x40
```

**`in 0 out 0`** —— 确认它**没有任何 DAPM 路径引用它**。
这与我之前从源码静态分析得出的判断一致（`grep -c "DC Trim" mt6368.c` = 1，
只在 widget 声明处）。

**但注意**：这不等于它不会被触发。`CUPDA` 的 `SND_SOC_DAPM_SUPPLY_S` 语义仍需
Codex 从 `soc-dapm.c` 确认（subseq 相关）。

---

## 5. 同一窗口的其余快照

| 文件 | 内容 |
|---|---|
| `asound-snapshot.txt` | `/proc/asound/cards`、`pcm`、`pcm0p/1p/2p` 的 `status` 与 `hw_params` |
| `dumpsys-audio.txt` | 928 行 |
| `dumpsys-audio-flinger.txt` | 614 行 |

### 关键：三个 playback PCM 全部 `closed`

```
--- pcm0p ---   status: closed   hw_params: closed
--- pcm1p ---   status: closed   hw_params: closed
--- pcm2p ---   status: closed   hw_params: closed
```

**与 widget 全部 `Off`、`AIF_RX` 显示 `stream AIF1 Playback inactive` 一致 ——
这是一个干净的 idle 基线。**

---

## 6. 源码采集（任务 4）

**同一固定提交 `0a8d13c1a180fed82803f56337a0b065ff704fb4`：**

| 文件 | 字节 | SHA256 |
|---|---|---|
| `sound/soc/mediatek/mt6895/Makefile` | **948** | `8126c83514e3b15629e2303ed4db8430ae048387e6b04200d973fbe87f3f3f69` |
| `sound/soc/mediatek/mt6895/mt6895-dai-adda.c` | **46779** | `40ddd7a6b405bd20143fb43232d0ae5f719a7c32a9fd6a5f1d5ccdad972aed50` |

**两个都 HTTP 200，都是真实源码**（`// SPDX-License-Identifier: GPL-2.0` 开头）。

### 🔴 Makefile 纠正了我一个错误判断

**`snd-soc-mt6895-afe-objs` 列了 12 个对象：**

```
mt6895-afe-pcm.o      mt6895-afe-clk.o       mt6895-afe-gpio.o
mt6895-dai-adda.o     mt6895-afe-control.o   mt6895-dai-i2s.o
mt6895-dai-hw-gain.o  mt6895-dai-src.o       mt6895-dai-pcm.o
mt6895-dai-hostless.o mt6895-misc-control.o  mt6895-dai-tdm.o
```

**`mt6895-dai-adda.c` 是独立文件，不嵌在 `afe-pcm.c` 里。**

**我之前说「ADDA 与 MTKAIF 在 afe-pcm.c 里（324/62 处），不需要单独找」是错的** ——
那只是**符号引用计数**，不代表实现位置。**我撤回那句话。**

`mt6895-dai-adda.c` 里 `mt6895_dai_adda_register` **1 处**、`ADDA` **298 处**。

### 当前搜集覆盖范围（不推断完整性）

| 已收集 | 未收集 |
|---|---|
| `mt6895-afe-pcm.c`、`mt6895-afe-clk.c`、`mt6895-afe-common.h` | `mt6895-afe-gpio.c` |
| **`mt6895-dai-adda.c`**、`mtk-afe-fe-dai.c` | `mt6895-afe-control.c` |
| `mt6895-mt6368.c`（machine）、`Makefile` | `mt6895-dai-i2s.c`、`mt6895-dai-hw-gain.c`、`mt6895-dai-src.c` |
| `soc-dapm.c`、`include/sound/soc-dapm.h` | `mt6895-dai-pcm.c`、`mt6895-dai-hostless.c`、`mt6895-misc-control.c`、`mt6895-dai-tdm.c` |

**12 个对象里我收集了 3 个（`afe-pcm`、`afe-clk`、`dai-adda`）+ 1 个头文件。
其余 8 个未收集。这只是一个覆盖范围记录，不是完整性判断。**

---

## 7. debugfs 卸载（任务 5）

| 项 | 值 |
|---|---|
| 命令 | `umount /sys/kernel/debug` |
| **返回码** | **`0`** |
| stderr | 空 |
| 卸载后 `grep debugfs /proc/mounts` | **空**（已恢复） |
| 卸载后 `/sys/kernel/debug/asoc` | **`No such file or directory`** |

**一次正常 umount 就成功，没有忙、没有 force/lazy、没杀进程。**

---

## 8. 判据与边界

### 已证明

| # | 结论 | 依据 |
|---|---|---|
| 1 | **debugfs 可挂载、可卸载** | 两次 `rc=0`，挂载表前后可比 |
| 2 | **ASoC DAPM 接口全部可读** | 126 个 widget 可枚举，30 个目标全部读到 |
| 3 | **codec 路径** | `.../mt6895-mt6368/mt6368-sound/dapm/` |
| 4 | **idle 基线干净** | 30 widget 全 Off + 3 个 PCM 全 closed + `AIF1 Playback inactive` |
| 5 | **`DL Power Supply` 的 13 个上游** | `in` 列表原文，与 `SUPPLY_SEQ_*` 顺序一致 |
| 6 | **`DC Trim` 的 `in 0 out 0`** | 无 DAPM 路径引用它（与静态分析一致） |
| 7 | **`mt6895-dai-adda.c` 是独立对象** | Makefile 的 `objs` 列表 |

### 没有证明（严格边界）

| 未证明 | 说明 |
|---|---|
| **widget 的 `On` 意味着硬件时钟真的振荡** | 只是 DAPM 软件状态 |
| **本轮的 `Off` 说明电气上确实关闭** | 同上，只是软件状态 |
| **`DCXO` 的电气必要性** | 完全没测 |
| **能否强制耳机输出** | 没查 route 控件 |
| **默认 PCM 能否播放** | 没播 |

### 🔴 按 [CMD] 要把能力清单两项改为未验证

上一轮 `PLAYBACK-CAPABILITY-LIST.md` 里写的这两项，现在明确标注为**未验证**：

| 项 | 旧表述 | 修正 |
|---|---|---|
| `tinyplay` 存在 | ~~"能播放一段 PCM"~~ | **只证明工具存在；"默认 PCM 能播放"未验证** |
| `tinymix` 564 控件 | ~~"指定路径播放 ✅ 可能"~~ | **"能强制耳机"未验证** |

---

## 🔴 三处解释修正（2026-11-16，Codex 指出）

**§4.2 与 §4.3 里我对 `in`/`out` 列表的三处解读有误，按审定文案更正：**

### ① `in`/`out` 列表不是执行顺序

我写「13 个上游 supply，顺序与 `SUPPLY_SEQ_*` 编号一致」。**顺序一致是真的**，
**但 `in`/`out` 列表本身不是执行顺序，13 个上游并非有向串行链** ——
它们是同一个 consumer（`DL Power Supply`）的**并列**上游。

### ② `CLK_BUF` 有三个 consumer ≠ 开启它会强制打开三条路径

我写「它一开就带动三条路径」。**这是错的。**
`out` 列表里的 `DL Power Supply`、`VOW TX`、`AIFTX_Supply` 只说明
**这三个 widget 把它当上游**，不说明开 `CLK_BUF` 会强制打开它们 ——
DAPM 里 upstream 被打开是因为 downstream 需要它，不是反过来。

### ③ supply 的 `in`/`out` 计数被读取函数直接置零

我写「`DC Trim` 的 `in 0 out 0` 确认它没有任何 DAPM 路径引用它」。
**不能仅凭计数得出这个结论** —— 该计数被读取函数直接置零。
**应检查文件全文列出的边**（`cat` 输出里的 `in "..." "..."` / `out "..." "..."` 行）。

**`DC Trim` 的实际全文输出只有首行 `DC Trim: Off in 0 out 0 - R9624(0x2598) mask 0x40`，
没有任何 `in`/`out` 边行** —— 所以「没有显式 route 引用」这个事实仍成立，
**只是不能仅凭 `in 0 out 0` 计数得出，而是靠"全文无边行"这一点。**
