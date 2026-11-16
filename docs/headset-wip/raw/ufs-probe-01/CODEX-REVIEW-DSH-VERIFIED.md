# Codex 初审的四条诊断问题：我逐条核实

> 2026-11-16 21:50，DSH。**只核实，未改代码（待 Codex 额度恢复后按它的清单修）。**

**Codex 文件**：`P/logs/ufs-probe-20261116/CODEX-PRELIMINARY-REVIEW.md`（5512 B）

---

## 1. `first`/`second` 未初始化 —— ✅ **确认为真 bug**

**驱动第 143 行原文：**

```c
struct ufs_snapshot first, second;
```

**栈上未初始化。**

**后果**：`ufs_observe` 在读失败时可能只填了一部分甚至完全没填，
**但入口（第 163-177 行）仍然打印 `second` 和两个快照的原始值** ——
**打印出的是栈垃圾，不是硬件值。**

**Codex 的说法准确**：「不能用零值冒充已读硬件零」。**而且比零值更糟：是未定义内容。**

**修法（待做）**：初始化输出结构，并记录「哪些字已采集 / 哪些失败」，未采集的显式标注。

---

## 2. 全 `0xffffffff` 会被判 `OBSERVED` —— ✅ **确认为真 bug**

**我实际算了一遍：**

| 检查 | 全 0xff 时 | 结果 |
|---|---|---|
| `(word[4] & 1) == 0` | `(0xffffffff & 1) = 1` | **不触发** `NOT_ENABLED` |
| `(word[3] & DP)` | `= 1` | **不触发** `NO_DEVICE` |
| `(word[3] & UTRLRDY)` | `= 2` | **不触发** `NOT_READY` |

**→ 九个字全 `0xffffffff` 且两快照一致时，判定为 `UFS_OBSERVED`。**

**这是典型的「读失败/未映射返回全 1」。我的判定把最坏情况当成了最好情况。**

**Codex 的说法准确**：「应区分『捕获了快照』与『可信控制器状态』」。

**修法（待做）**：加身份一致性检查（`CAP`/`VER` 合理性）、全 1 负对照、未知 `VER` 保留原值而不宣称支持。

---

## 3. 读回调类型不能保证实现不写 —— ✅ **Codex 说得对，我表述过强**

**我在文档里写过：**

> ~~「零写是**类型层面**保证：读回调类型 `bool (*read)(void *, uint16_t, uint32_t *)`，
> 类型里没有写操作的位置」~~

**🔴 这个表述过强，现更正：**

| 类型能保证的 | 类型**不能**保证的 |
|---|---|
| 接口没有暴露写操作的**参数位置** | **实现内部**会不会写内存/MMIO |

**当前实现 `ufs_mmio_read` 确实只读**（唯一 `volatile` 访问在第 131 行且是读），
**但这是「审查过这个实现」，不是「类型系统保证」。**

**而且 Codex 指出一个我没想到的**：**真实 MMIO data abort 不会自动变成 `bool false`** ——
**它会 panic 或者别的行为，不是我这个 `return false` 路径。**

**修法（待做）**：把「零写」的依据改为「实现审查 + 调用链审查」，不是类型论证。

---

## 4. 双快照不同 ≠ 设备故障；`IS` 会自己变 —— ✅ **确认为设计问题**

**我把九个寄存器全放进「两快照必须一致」的检查里，包括 `IS`（`0x20`）。**

**`IS` 是 **interrupt status** —— 硬件会自己置位。**

**→ 两快照 `IS` 不同是完全正常的，但我判成 `UFS_UNSTABLE`，这会把正常现象报成故障。**

**修法（待做）**：`IS` 不参与一致性判定（或单列），只作观察值。

**另外 Codex 指出一个术语错误**：

> 「`CAP.NUTRS` 是 transfer request slots，UTRL 不是 task management request list；注释中的 task 应改正」

**我核对了：`UTRL` = **U**nified **T**ransfer **R**equest **L**ist，
它承载 task management 的是 **`UTMRL`**（`UTMRLBA`/`UTMRLRSR`）—— 是两个不同的链表。**

**我的注释写「task request list」不够准确。** 待改。

---

## 5. 它在五个问题上的判断，我核对后同意

| # | Codex 判断 | 我的核对 |
|---|---|---|
| 1 | 九个寄存器够首次快照；暂不加 `UICCMD`/`UICARG1`（命令上下文，不能替代电源/时钟/链路证据）| **同意** |
| 2 | `HCE=0` 不能单独归因时钟；先比对 loader 跳转前与 XNU 探针时刻的同组寄存器 | **同意，而且这比我原方案更严谨** |
| 3 | 先只读确认继承状态，再决定接管还是补 clock；两条写入路线都没批准 | **同意** |
| 4 | `ml_io_map` 已用 `VM_WIMG_IO`（`machine_routines.c:1645` 附近）；不要用 wcomb 或可缓存映射 | **同意，我原问题问错了方向** |
| 5 | 首轮不同时读 `ufscfg_ao`/`ufscfg_pdn`；有基址不等于知道安全偏移 | **同意** |

**🔴 第 2 条它提的方法比我好**：我原想「XNU 探一次看 HCE」，
**它说要先有 loader 跳转前的同组寄存器作对照** —— 否则 `HCE=0` 无法区分「LK 关了」与「XNU 关的」。

---

## 6. 🔴 它指出的 loader 事实：我漏了一大块

**Codex 原文：**

> 「`P/mtk/loader/mtkloader.c` 约 4180 行打印 LK 状态，随后代码已有 taking the list over、
> fDeviceInit、LUN 选择和 NOP 相关流程。`board.h` 也描述了自有 UFS 接管实现。」

**我核实了，`board.h:336-348` 原文：**

> 「**LK brings it up before it loads anything**: the link is started, the gear and lanes are set,
> the device and unit descriptors are read, and **the controller is left running with LK's own
> transfer request list in UTRLBA**. `mtk/loader/ufs.h` **takes that list over**
> (`UTRLRSR 0`, `UTRLBA` to our own list, `UTRLRSR 1`) and issues **`READ(10)` to LUN 2** …
> So **the loader can read the flash after all**, and the system image comes off it into DRAM.」

**🔴 这是决定性的：**

| 事实 | 意义 |
|---|---|
| **LK 已经在跑 UFS**（link started、gear/lanes set、descriptors read）| **「LK 保留状态」不再是推断，是 board.h 的明确记载** |
| **`mtk/loader/ufs.h`（141239 B）已经实现了接管 + `READ(10)` 到 LUN 2** | **接管代码已存在，不需要从零写** |
| **loader 已经能读 flash，系统镜像就是这样进 DRAM 的** | **读盘能力已在 loader 侧验证过** |

**同段还写清了缺口：**

> 「**It does not give the kernel a block device.** A `/dev/emmc0` or `/dev/ufs0` needs
> **the kernel's own driver to inherit the live controller** — ours
> (`drivers/storage/hakios.ufshc.c`) is written for the Qualcomm case where UEFI's UFSDxe
> stays resident and services a shared page, **and nothing here provides that.**」

**→ 缺口精确到一句话：loader 能读，内核不能；需要一个「继承活控制器」的内核驱动。**

---

## 7. 它对我 Linux 结论的收窄，我接受

**Codex 原文：**

> 「不能由此推出整个驱动只做三件事、完全没有 reset 或仅被动接管。同提交 `ufs-mediatek.c` 仍包含：
> `ufs_mtk_setup_clocks -> ufs_mtk_pwr_ctrl -> ref-clock` 处理、
> `hce_enable_notify` 中的 variant 处理、`pre_link`/`post_link` 及 DME 设置、
> 单独的 **`ufs_mtk_device_reset`**（通过平台控制路径复位设备，不依赖上述 `reset_control` 句柄）。」

**🔴 我确实说得太绝对了。** 我只检查了 `phys` 和 `resets` 两个属性缺失，
**没有追 `ufs_mtk_setup_clocks`、`ref-clock`、`pre_link`/`post_link`、`ufs_mtk_device_reset`。**

**特别是 `ufs_mtk_device_reset` —— 它走平台控制路径，不依赖 `reset_control`，
所以「DT 没有 resets 属性」并不能推出「Linux 不做设备复位」。**

**Codex 还指出**：这是 MT6895-Mainline 的移植分支，**不应笼统称作「upstream Linux 已证明」。** 同意。

---

## 8. 我的当前状态

**待命中，不改代码。** 按 Codex 的清单，需要做的是：

| # | 待修 |
|---|---|
| 1 | 初始化 `first`/`second`，记录有效字/失败位置，未采集显式标注 |
| 2 | 加全 1 负对照与 `CAP`/`VER` 合理性检查；未知 `VER` 保留原值 |
| 3 | 把「零写」依据改为实现与调用链审查；承认 MMIO abort 不走 `return false` |
| 4 | `IS` 移出一致性判定；修正 `UTRL` 术语 |
| 5 | 失败路径/无效响应的有意义测试 |
| 6 | 追 `mtkloader.c` 的完整 UFS 调用图，明确 XNU 继承的是哪一阶段状态 |
| 7 | 用真实 XNU 头文件做对象编译 |

**Codex 的推进顺序建议**：**先完成 CPU713 打包审查与只读试验，UFS 继续主机修正。**
