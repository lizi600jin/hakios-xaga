# DAPM 认识的六项审定修正（Codex 审定，DSH 写入）

> 2026-11-16，DSH 转写。**文案由 Codex 审定**（`hakios-mtk/logs/headset-chain-20261116/FINDINGS.md`
> §"审定的文档修正文案"），我原文落地，不改语义。
>
> **历史 trial 705/707 的 FAIL 保持不变；本文件不事后重判任何实验。**

---

## 修正 1：`SUPPLY_SEQ` 与 `_S` 后缀

**`SND_SOC_DAPM_SUPPLY_S` 的 `_S` 引入 `.subseq`，表示同一排序类别内的顺序。**

**普通 `SUPPLY` 和 `SUPPLY_S` 都能通过 reg/shift/on_val/off_val 在 PMU/PMD
自动更新寄存器；关闭反值不是 `_S` 独有。**

`soc-dapm.c` 先按 widget 类型的 up/down 表排序，再比较 subseq
（**上升升序、下降降序**）。

**因此：**

| 曾经的表述 | 修正 |
|---|---|
| ~~enum 是全部必经流程~~ | **enum 不是全部必经流程** |
| ~~是整个设备的严格线性时序~~ | **不是严格线性时序** |
| ~~`CLK_BUF` #0 绝对最先~~ | **不能这么说** —— `regulator_supply` 还可在普通 supply 之前执行 |
| — | **完整 enum 实际包含 42 个值（0..41）**，普通 HP 不需执行全部 capture/VOW 项 |

**这条直接修正我在 `SUPPLY-SEQ-MAP.md` 里的表述。**

---

## 修正 2：trim 的启动触发

**校准线程仍由 `DC Trim` 的 `POST_PMD` 回调异步调度。**（这部分我原来的分析是对的）

**但 `mt6368_codec_init_reg` 末尾调用 `enable_trim_buf(priv, true)`，
并明确注释会触发 `DC trim` widget power-down；该 init 由 codec probe 调用。**

**实际模块已内联该 init**：`.text+0x12d0..0x12e8` 是
`update_bits(reg=2598, mask=40, val=40)`。

**DAPM 新建 widget 时读取初始寄存器作为旧 power，再做初始同步，
因此不需要先播放一次 —— 启动同步本身有触发该异步工作的路径。**

| 曾经的表述 | 修正 |
|---|---|
| ~~"没播放所以没校准" 作为因果结论~~ | **不能作为因果结论** |
| — | **缺少日志仍判证据不足，不能推出 trim=0、未运行或校准成功** |
| ~~DC Trim 在声明表最后 = 全部关闭操作中的绝对最后~~ | **不等于绝对最后** |

**本轮不强制触发或移植校准流程。**

**这条修正了 `DAPM-TRIM-MECHANISM.md` 的核心推论。**

---

## 修正 3：trial 709 的结论范围

**实测只证明：**

| 已证明 | 未证明 |
|---|---|
| SID 9 指定读取成功 | — |
| `0x079e = 0xb8` | — |
| `bit2 = 0` | — |
| 原寄存器探针回收判据仍通过 | — |

**`bit2` 是指定 buffer 的软件使能字段。**

**没有波形或硬件模式证据，不宣称：**
- 整个 DCXO 停振
- 频率正确
- 导致无声

---

## 修正 4：接口名 —— 撤回"必然是普通 I2S slave"

**新取得的固定 machine 源码中：**

- `Primary Codec` 后端将 CPU **`ADDA`** 接到 **`mt6368-snd-codec-aif1`**
- init 选择 **`MTKAIF_PROTOCOL_2_CLK_P2`** 并调用 **MTKAIF 校准**

**所以撤回"下一步必然是普通 I2S slave"的既定说法。**

**这是固定源码的配置证据，后续还要核对本机 machine/AFE 模块与运行路径。**

**`afe-pcm.c:6938` 引用了独立的 `mt6895_dai_adda_register` ——
符号出现次数多不能证明源码覆盖完整，应继续按 Makefile 获取实现。**

---

## 修正 5：能力清单的表述

| 项 | 修正 |
|---|---|
| `tinyplay`/`tinymix` 存在 | **只证明工具存在** |
| ~~"默认 PCM 可播"~~ | **不能标成已验证** |
| ~~"可强制耳机路由"~~ | **不能标成已验证** |
| ~~debugfs 未挂载 → 内核不支持 DAPM debugfs~~ | **应表述为"当前尚不可观测，挂载后的 ASoC 支持未验证"** |

**（这一条我在 DAPM-OBSERVE-01 里已经实际验证了：挂载后 asoc 确实可用。）**

---

## 修正 6：恢复账本 —— `0x2537` 在哪里

**`0x2537` 在独立的 `hp_saved[]` 中，原快照恢复循环会访问它。**

**它不在 ACCDET 的通用 `saved[]` 中，这只解释 `restored 36` 计数未增加。**

| 曾经的表述 | 修正 |
|---|---|
| ~~"它完全不在恢复列表，只靠一行清零恢复"~~ | **撤回这个绝对说法** |
| — | **显式 CLKSQ off 是关闭判据前的动作；其后的 HP 快照恢复是另一阶段** |

---

## 另外三处对 DAPM-OBSERVE-01 的解释修正

**Codex 在 [CMD] 里指出我 observe-01 报告的三处解释问题：**

### ① `in`/`out` 列表不是执行顺序

**我说"13 个上游 supply，与 `SUPPLY_SEQ_*` 编号顺序一致"** ——
**这个一致是真的，但 `in`/`out` 列表本身不是执行顺序**，
**13 个上游并非有向串行链**（它们是同一个 consumer 的并列上游）。

### ② `CLK_BUF` 有三个 consumer ≠ 开启它会强制打开三条路径

**它的 `out` 列表里有 `DL Power Supply`、`VOW TX`、`AIFTX_Supply`，
只说明这三个 widget 把它当上游，不说明开 `CLK_BUF` 会强制打开它们**
（DAPM 里 upstream 被打开是因为 downstream 需要它，不是反过来）。

### ③ supply 的 `in`/`out` 计数被读取函数直接置零

**`DC Trim: Off in 0 out 0` 中的 `in 0 out 0` 不能单独用来判断"无路径"。**

**应检查文件全文列出的边**（即完整 `cat` 输出里的 `in "..." "..."` 与
`out "..." "..."` 行），而不是只看计数。

**我在 observe-01 里对 `DC Trim` 的结论依赖了 `in 0 out 0`，
这一点要按上述修正重新理解 —— 但它同时也与静态 grep 结果
（`DC Trim` 在 `mt6368.c` 只在 widget 声明处出现 1 次）一致，
所以"没有显式 route 引用"这个事实本身仍成立，只是不能仅凭计数得出。**
