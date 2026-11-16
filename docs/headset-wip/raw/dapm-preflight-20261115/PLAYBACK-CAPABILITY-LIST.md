# EVIDENCE-709A 任务 3：原厂播放观测能力清单

> 2026-11-16 13:30，DSH。**只列能力。未播放、未改 mixer、未读 register dump、未碰 spmi_access。**

---

## 1. 播放工具

| 工具 | 路径 | 可用 |
|---|---|---|
| **`tinyplay`** | `/system/bin/tinyplay` | ✅ |
| **`tinymix`** | `/system/bin/tinymix` | ✅ |
| `tinycap` | `/system/bin/tinycap` | ✅ |

## 2. ALSA 接口

| 项 | 状态 |
|---|---|
| `/proc/asound/` | ✅ 可读（`card0`、`cards`、`devices`、`hwdep`、`mt6895mt6368`、`pcm`、`timers`、`version`） |
| `/proc/asound/cards` | ✅ `0 [mt6895mt6368]: mt6895-mt6368 - mt6895-mt6368` |
| `/dev/snd/` | ✅ **67 个节点**：`controlC0`、`pcmC0D0p`…`pcmC0D71c`、`comprC0D57`、`timer` |
| **播放 PCM** | `pcmC0D0p`、`pcmC0D1p`、`pcmC0D2p`…`pcmC0D71p` 中多个（至少 30 个 `p` 端点） |

## 3. Mixer

| 项 | 值 |
|---|---|
| Mixer 名 | **`mt6895-mt6368`** |
| 控件数 | **564** |
| `tinymix` 可枚举 | ✅（只读列出，未改任何值） |

## 4. ASoC DAPM debugfs —— ❌ **不可用**

| 检查 | 结果 |
|---|---|
| `/sys/kernel/debug/asoc` | **ABSENT** |
| `mount \| grep debugfs` | **0**（debugfs 未挂载） |
| `/sys/kernel/debug` 目录 | 存在（空） |
| root | ✅ `uid=0` |

**结论：当前 debugfs 未挂载，所以 ASoC 的 DAPM 状态（widget 电源、路径连通性）
读不到。** 要读需要 `mount -t debugfs none /sys/kernel/debug` —— **本轮没做**，
因为 [CMD] 说"只列能力"。

**这意味着：无法在 Android 侧直接观察 `CLK_BUF`/`CLKSQ`/`HP_ANA_TRIM` 这些
DAPM widget 的实际开关状态。** 只能靠 `tinymix` 控件和 `/proc/asound` 间接判断。

## 5. dumpsys

| 命令 | 可用 | 头部输出 |
|---|---|---|
| `dumpsys audio` | ✅ | `AudioService Dumpsys` / `Current time: ...` |
| `dumpsys media.audio_flinger` | ✅ | `Start begin: 01-01 08:00:50.486` |

`dumpsys audio` 已在前面的 preflight 里证明能显示 headphone 设备状态
（`DeviceInfo: type:0x8 (headphone)`、`DEVICE_STATE_AVAILABLE`）。

## 6. 本地已有源码（路径与身份）

### 6.1 在 `docs/headset-wip/raw/`（本仓）

| 文件 | 字节 | 说明 |
|---|---|---|
| `mt6368.c` | 249493 | **vendor codec 主体**（sha256 `84ebc78f`），DAPM widget/routes/trim 都在这里 |
| `mt6368.h` | 294048 | 寄存器定义 |
| `mt6368-accdet.c` | 105758 | 耳机检测驱动 |
| `mt6685-audclk/mt6685-audclk.ko` | 14496 | MT6685 AUDCLK 模块（build-id 已验证） |

### 6.2 在 `hakios-mtk/logs/headset-dapm-review-20261115/`

见同目录 `EVIDENCE-709A-SOURCES.md`，本轮新增 6 个文件：
`soc-dapm.c`、`mt6895-mt6368.c`、`mt6895-afe-pcm.c`、`mt6895-afe-common.h`、
`mt6895-afe-clk.c`、`mtk-afe-fe-dai.c`。

### 6.3 ko 身份（设备实测 hash）

见 `EVIDENCE-709A-SOURCES.md` §4。关键几条：

| 模块 | 位置 | 字节 | SHA256 前 16 位 |
|---|---|---|---|
| `mt6895-mt6368.ko` | `/vendor/lib/modules` | 83840 | `eeff98c4cc2e9b51` |
| `snd-soc-mt6368.ko` | `/vendor/lib/modules` | 277608 | `4ce5cab06d3d29aa` |
| `snd-soc-mt6895-afe.ko` | `/vendor/lib/modules` | 807656 | `0e2bedd07478839f` |
| `mt6685-audclk.ko` | **vendor_boot ramdisk** | 14496 | `a76640b621e6f48e` |

## 7. 能力小结（能不能做某件事）

| 要做的事 | 能不能 | 依据 |
|---|---|---|
| 播放一段 PCM 到默认设备 | ✅ 能（`tinyplay`） | 工具存在 |
| 指定路径播放（如强制耳机） | ✅ 可能（`tinymix` 564 控件里应有 route 控件） | **未验证具体控件名** |
| 观察 mixer 当前值 | ✅ 能 | `tinymix` 只读枚举成功 |
| 观察 DAPM widget 电源状态 | ❌ **不能** | debugfs 未挂载 |
| 观察 PCM 运行状态 | ✅ 能 | `/proc/asound/card0/pcm*/sub*/status`（**未逐个验证**） |
| 看音频服务路由决策 | ✅ 能 | `dumpsys audio` |
| 看 AudioFlinger 线程/延迟 | ✅ 能 | `dumpsys media.audio_flinger` |
| 读 codec 寄存器 | ⚠️ 需要 codec debugfs 或 spmi_access —— **两者本轮都不碰** | — |

## 8. 未验证、明确标注

| 项 | 状态 |
|---|---|
| `tinymix` 里哪个控件对应"强制耳机输出" | **未查**（不读出全 564 项的名字） |
| `/proc/asound/card0/pcm0p/sub0/status` 是否可读 | **未逐个验证** |
| debugfs 挂载后 `asoc` 是否出现 | **未试**（本轮不挂） |
| `tinyplay` 需要的 PCM 格式/速率 | **未查** |

---

## 9. 边界

**本轮完全没有：**
- 播放任何音频
- 修改任何 mixer 控件
- 挂载 debugfs
- 读任何 codec 寄存器或 regmap
- 碰 `spmi_access` sysfs

**所有结论都来自"命令是否存在 / 文件是否可读"，不是"功能是否工作"。**
