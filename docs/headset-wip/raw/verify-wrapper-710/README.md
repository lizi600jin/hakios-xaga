# VERIFY-WRAPPER-710：wrapper 修复复核与参考构建差异修正

> 2026-11-16 14:15，DSH。**仅主机。无设备操作、无刷机/打镜像/播放、无整内核构建。**
> 生产树**不 commit、不 push、不覆盖既有 WIP**。

---

## 0. 三句话

**一、wrapper 修复的四处全部核实成立，两个文件 hash 与声明一致，
`bash -n` 通过、7 项测试 7/7 通过（真实退出码 0）。**

**二、我上一轮对"新旧 kernel 差异"的归因有三处错，已按 Codex 的逐字节审计修正：
不是 `__DATA`/`__BOOTDATA`，而是 `__LINKINFO.__symbolsets`（542804 B）；
并撤回"随机盐天然不可复现"的断言。**

**三、`snd_soc_dai_get_widget` 在 `soc-dai.h:406` 就是 `static inline`，
直接返回 `dai->playback_widget` —— 所以它不需要单独的 `.c` 实现。**

---

## 1. wrapper 修复核实（四处）

### 1.1 hash 与语法

| 文件 | 声称 | 实测 | 结果 |
|---|---|---|---|
| `tools/make-kernel.sh` | `95247bb011c136d22ca70b485a3388e46720464b6b40f3842bac2acdee80dbd3` | **同** | ✅ |
| `tools/test-make-kernel.py` | `c74ddf4be7a1353e0849dff46fbdaa6613946899954e146cf949e580bff1693b` | **同** | ✅ |
| `bash -n tools/make-kernel.sh` | — | **`rc=0`，无输出** | ✅ |

### 1.2 四处修复逐条核对

**① 从 `XNU_SRC` 推导默认 `KERNEL`**

```bash
build_tree=${XNU_SRC:-$default_tree}
KERNEL=${KERNEL:-$build_tree/BUILD/obj/DEVELOPMENT_ARM64_BCM2837/mach.development.bcm2837}
```

**`KERNEL` 现在跟随 `build_tree`，不再固定为默认树。**

**② 只有默认源树才 sync/config**

```bash
build_tree=$(cd "$build_tree" && pwd -P) || exit 1
export XNU_SRC="$build_tree"
if [ -d "$default_tree" ] && [ "$build_tree" = "$(cd "$default_tree" && pwd -P)" ]; then
	xnu/sync-drivers.sh "$build_tree" || exit 1
	python3 tools/hakios-config.py kernel --tree "$build_tree" || exit 1
fi
```

**注释写得很清楚**：「An explicitly selected isolated tree is a prepared snapshot:
syncing the working drivers would contaminate it.」
**还用 `pwd -P` 解析物理路径** —— 所以同一个树的不同写法也能正确识别。

**③ `--judge` 仍不构建、不复制**

```bash
if [ "$1" = --judge ]; then
	judge "$2" && echo "make-kernel: the log shows a built kernel; only the kext symbol-set checks failed"
```

**它在 sync/config/build 之前就 return，所以不会碰源、不会复制 `hakios-kernel`。**

**④ 产物复制失败现在返回失败**

```bash
named() { cp -p "$KERNEL" hakios-kernel && echo "..."; }
if [ "$status" -eq 0 ]; then
	named
	exit $?
```

**`named` 的返回值现在被 `exit $?` 采用 —— `cp` 失败会让整个脚本失败。**

**⑤ 中文 symbolset 过滤保留**

`unexpected_failures()` 里的 `[[：[:space:]]]*(Error|错误)` 仍在（`a721c62b` 那版我自己加的修复）。

---

## 2. 测试结果

| 项 | 命令 | 退出码 | 输出 |
|---|---|---|---|
| 语法 | `bash -n tools/make-kernel.sh` | **0** | 空 |
| 测试 | `python3 tools/test-make-kernel.py` | **0** | `.......` `Ran 7 tests in 0.169s` `OK` |

**7 个点 = 7 个测试全过。**

### 既有产物未被触碰（测试后实测）

| 项 | 期望 | 实测 | 结果 |
|---|---|---|---|
| 709 旧 kernel | `c17cf881745511a3…` | **`c17cf881745511a3c2aa0225ca7a28f3e7b65e853688e13839319d8e6515010d`** | ✅ |
| `hakios-kernel` | `67f9f933117f9bc2…` | **`67f9f933117f9bc2b1fb6c9566cd032079a83dab218825ecb838a385d15f77e1`** | ✅ |
| 709 镜像 | `f4ff9732081e44a3…` | **`f4ff9732081e44a3cbac283631ce54dd9dbccec848eb5adee8a348af762fe413`** | ✅ |

**测试在临时假构建目录运行，没有碰真实 BUILD。**

> **注**：`make-kernel.sh` 的 `95247bb0` 是**本次构建之后**的新工具版本。
> 上一轮 BUILD-REFERENCE-710 用的是旧版 `a721c62b`。两者不能混为一谈。

---

## 3. 🔴 参考构建差异：我上一轮的三处错

**Codex 用逐字节 + section 审计（`compare-reference-kernels.py` /
`reference-kernel-diff.json`）给出了完整归因。我独立复算了，完全吻合。**

### 3.1 正确的归因

| 类别 | 字节 | 位置 |
|---|---|---|
| `LC_UUID` | **16** | file offset **4560 (0x11d0)** |
| **`__TEXT.__const` 版本字符串** | **18** | file offset 661234（段起 16832，段大小 644720） |
| **`__LINKINFO.__symbolsets`** | **542804** | file offset **11878400**（段大小 588078） |
| **合计** | **542838** | **未归因 = 0** |

### 3.2 我的三处错误

| # | 我上轮写的 | 正确 |
|---|---|---|
| 1 | 「其余 542804 字节全在 `__DATA`/`__BOOTDATA`/符号与调试元数据」 | **全在 `__LINKINFO.__symbolsets`**，不是 `__DATA`/`__BOOTDATA` |
| 2 | 没提到版本字符串 | **`__TEXT.__const` 里有 18 字节的构建日期时间** |
| 3 | 「`LC_UUID` 由链接器按随机盐生成，**天然不可复现**」 | **🔴 撤回这个断言** —— **本次没有核定 UUID 的生成算法**，不能说它"天然不可复现" |

### 3.3 我独立复算的结果（与 json 一致）

```
总差异字节: 542838
完全落在 __LINKINFO.__symbolsets 内: 542804
落在外面的段: 11 段，共 34 字节
  [0x11d0, 0x11e0) len=16   ← LC_UUID
  其余 10 段在 0xa16f2..0xa17xx 区间，共 18 字节 ← 版本字符串
```

### 3.4 版本字符串的实际内容（我直接读了字节）

```
old: @(#)VERSION: Darwin Kernel Version 22.5.0: 2026年 11月 15日 星期日 23:16:15 CST; lizi:kernel-src/BUILD/obj/DEVELOPMENT_ARM64_BCM2837
new: @(#)VERSION: Darwin Kernel Version 22.5.0: 2026年 11月 16日 星期一 13:57:43 CST; lizi:kernel-src/BUILD/obj/DEVELOPMENT_ARM64_BCM2837
```

**差异就是构建日期时间（15日 23:16:15 → 16日 13:57:43），
而 13:57:43 正是我那次构建的时间（13:56:35 开始，13:57:50 结束）。**

### 3.5 symbolsets 的细节（来自 json，我未复算这一项）

**9 个 symbolset 共 5638 条 symbol 记录，保留所有字段与重复项；
按完整记录做 multiset 比较后相同，区别只在数组顺序。**

**normalized_sha256**：`6d38fbf2f6ec9a1c3eb249f497fa0d25488c1de5610b581463536a2bff99de6b`

**所以：代码与数据字节相同，差异是 UUID + 构建时间戳 + symbolset 数组顺序。**

### 3.6 仍不追认 709 历史源链

**本次差异分析不改变 709 源链的 `INSUFFICIENT` 状态。**
它只说明「当前输入 → `39709224`」与「709 → `c17cf881`」的差异构成，
**不证明两者的输入相同。**

---

## 4. `snd_soc_dai_get_widget` 的实现位置

**同一固定提交 `0a8d13c1a180fed82803f56337a0b065ff704fb4`：**

| 项 | 值 |
|---|---|
| 文件 | `sound/soc/soc-dai.c` |
| URL | `https://raw.githubusercontent.com/XagaForge/android_kernel_xiaomi_mt6895/0a8d13c1a180fed82803f56337a0b065ff704fb4/sound/soc/soc-dai.c` |
| HTTP | **200** |
| 字节 | **18394** |
| SHA256 | **`b0a3231327d14759b7167790dce6a0e14a178cbf42cdeae47d5c85717edb5983`** |
| 首行 | `// SPDX-License-Identifier: GPL-2.0`（验真通过） |
| **`snd_soc_dai_get_widget` 出现次数** | **0** |

### 🔴 它在头文件里，不在 `.c` 里

**`include/sound/soc-dai.h:406-412`：**

```c
static inline
struct snd_soc_dapm_widget *snd_soc_dai_get_widget(
	struct snd_soc_dai *dai, int stream)
{
	return (stream == SNDRV_PCM_STREAM_PLAYBACK) ?
		dai->playback_widget : dai->capture_widget;
}
```

**它是 `static inline`，直接返回 `dai->playback_widget`（或 `capture_widget`）。**

**所以：**

- **不存在单独的 `.c` 实现** —— `soc-dai.c` 里 0 命中是正常的
- **active 状态作用在 `dai->playback_widget` 上**，也就是 codec 侧的 **`AIF1 Playback`**
- **这印证了 Codex 在 observe-02 里的判定**：`AIF_RX` 自身不显示 active 是正确的，
  active 属于自动 DAI widget

---

## 5. wrapper 的正确用法（根据修复后的行为）

**构建默认树**（会自动 sync 驱动与 config）：

```bash
cd /home/lizi/IOS/hakios-mtk
./tools/make-kernel.sh -k -j8
# KERNEL 自动指向 xnu-src/xnu-8796.121.2/BUILD/.../mach.development.bcm2837
# 产物复制到 ./hakios-kernel
```

**构建隔离树**（**不** sync，保持已准备输入）：

```bash
cd /home/lizi/IOS/hakios-mtk
XNU_SRC=/home/lizi/IOS/hakios-mtk/logs/msgbuf-1m-20261115/kernel-src \
  ./tools/make-kernel.sh -k -j8
# KERNEL 现在会跟随到 <隔离树>/BUILD/.../mach.development.bcm2837
# 注意：仍会复制到 ./hakios-kernel（会覆盖默认树产物！）
```

### 🔴 一个仍需注意的点

**隔离树构建也会把产物复制到 `./hakios-kernel`** ——
`named()` 无条件执行。**所以构建隔离树会覆盖 `hakios-kernel`。**

**如果你要保持 `hakios-kernel` 不动，仍应直接调 `xnu-build.sh`**
（像 BUILD-REFERENCE-710 那样），或者构建后把它恢复。

**这不是缺陷，是 `named()` 的设计意图**（所有工具都取 `./hakios-kernel`），
**但用隔离树时要知道这个副作用。**

---

## 6. 边界

| 已证明 | 未证明 |
|---|---|
| wrapper 四处修复成立 | — |
| hash 一致、语法通过、7/7 测试通过 | — |
| 三个既有产物未被测试触碰 | — |
| 差异 542838 字节全部归因、未归因 0 | — |
| 版本字符串差异就是构建时间 | — |
| `snd_soc_dai_get_widget` 是 inline | — |
| — | **UUID 的生成算法**（未核定） |
| — | **709 历史源链**（仍 INSUFFICIENT） |
| — | 供电/SGEN 是否成立 |
| — | 710 是否已打包/可刷（**没有打包**） |

**这是"编哪棵树/拿哪份产物"的主机工具问题，不是供电补丁。无新 hardware PASS。**
