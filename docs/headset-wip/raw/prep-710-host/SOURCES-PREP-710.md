# PREP-710-HOST 任务 3：新增源码采集

> 2026-11-16 13:48，DSH。**只下载与验真，不做寄存器语义提取。**
> 固定提交：`0a8d13c1a180fed82803f56337a0b065ff704fb4`
> URL 前缀：`https://raw.githubusercontent.com/XagaForge/android_kernel_xiaomi_mt6895/0a8d13c1a180fed82803f56337a0b065ff704fb4/`

---

## 1. 本轮新增四份

| 文件 | 上游路径 | 字节 | SHA256 | HTTP |
|---|---|---|---|---|
| `src__mt6895-reg.h` | `sound/soc/mediatek/mt6895/mt6895-reg.h` | **230920** | `99d426ae75a27a146f6350912b6b3af80815393edf7c58bec4eb7b532a3dfbca` | 200 |
| `src__mt6895-afe-gpio.c` | `sound/soc/mediatek/mt6895/mt6895-afe-gpio.c` | **8142** | `7d2da06d5d1d55af65ddc73e1173b28ac9f1319aadb7950822394241e621172a` | 200 |
| `src__soc-dai.h` | `include/sound/soc-dai.h` | **16176** | `03c5171491ed638d413b22ba05a58fe0ec08d48f53bead29c92d36c3595ad748` | 200 |
| `src__mt6368-regulator.c` | `drivers/regulator/mt6368-regulator.c` | **23554** | `0618a93e6e4a41a5c2b71bc2069fd8d3b2a8d5020c27773127346833fb941d44` | 200 |

**四份首行验真（都不是 404 页）：**

```
src__mt6895-reg.h          /* SPDX-License-Identifier: GPL-2.0 */
src__mt6895-afe-gpio.c     // SPDX-License-Identifier: GPL-2.0
src__soc-dai.h             /* SPDX-License-Identifier: GPL-2.0
src__mt6368-regulator.c    // SPDX-License-Identifier: GPL-2.0
```

**存放位置**：`/home/lizi/IOS/hakios-mtk/logs/headset-chain-20261116/`
（与 Codex 已有的 6 份同目录，前缀 `src__` 区分）

---

## 2. 关键符号存在性（只计数，未提取语义）

| 文件 | 符号 | 次数 |
|---|---|---|
| `mt6895-reg.h` | `ADDA` | **283** |
| | `MTKAIF` | **246** |
| | `AFE_SGEN` | **1** |
| `mt6895-afe-gpio.c` | `afe_gpio` | **49** |
| `soc-dai.h` | **`snd_soc_dai_get_widget`** | **1** ← Codex 引用的那个函数 |
| | `snd_soc_dai` | 89 |
| `mt6368-regulator.c` | `regulator_ops` | 4 |
| | `mt6368_regulator` | 30 |
| | `probe` | 2 |

**`soc-dai.h` 里 `snd_soc_dai_get_widget` 只出现 1 次** —— 它是声明，
实现可能在 `soc-dai.c`（**未收集**）。

---

## 3. 未收集、明确记录

| 文件 | 状态 |
|---|---|
| `sound/soc/soc-dai.c` | **未收集** —— `snd_soc_dai_get_widget` 的实现可能在它里面 |
| `mt6895-afe-gpio.h` | 未收集 |
| 其余 Makefile 里列的 8 个对象 | 未收集（见 EVIDENCE-709A-SOURCES.md §6 的覆盖范围表） |

**这只是覆盖范围记录，不推断完整性。**

---

## 4. 与已有文件的复用核对

**Codex 已有的 6 份**（`sound__soc__soc-dapm.c`、`mt6895-mt6368.c`、
`mt6895-afe-pcm.c`、`mt6895-afe-common.h`、`mt6895-afe-clk.c`、`mtk-afe-fe-dai.c`）
**本轮未重复下载。**

**加上本轮 4 份 + 之前的 `Makefile` 与 `mt6895-dai-adda.c`，当前共 12 份：**

| # | 文件 | 来源 |
|---|---|---|
| 1 | `soc-dapm.c` | Codex 已有 |
| 2 | `mt6895-mt6368.c` | Codex 已有 |
| 3 | `mt6895-afe-pcm.c` | Codex 已有 |
| 4 | `mt6895-afe-common.h` | Codex 已有 |
| 5 | `mt6895-afe-clk.c` | Codex 已有 |
| 6 | `mtk-afe-fe-dai.c` | Codex 已有 |
| 7 | `Makefile` | 我在 EVIDENCE-709A 采 |
| 8 | `mt6895-dai-adda.c` | 我在 EVIDENCE-709A 采 |
| 9 | **`mt6895-reg.h`** | **本轮** |
| 10 | **`mt6895-afe-gpio.c`** | **本轮** |
| 11 | **`soc-dai.h`** | **本轮** |
| 12 | **`mt6368-regulator.c`** | **本轮** |
