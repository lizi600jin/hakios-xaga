# xaga 主线 Linux 的 UFS 方案（对 XNU 的直接参考）

> 2026-11-16 21:30，DSH。**仅主机读源码。未改代码、未碰设备。**

**源仓库**：`https://github.com/MT6895-Mainline/linux`，分支 `7.2-mt6895-xiaomi-xaga`
**本地 clone**：`/tmp/mt6895-mainline`，HEAD **`25fd36e67`**

---

## 0. 一句话结论

**主线 Linux 在 xaga 上跑通了 UFS，而且它做的事比想象中少得多：
不碰 PHY、不碰 reset，只拿 4 个 clock + 1 个 power-domain，其余全靠 LK 已经配好的状态。**

**这对 XNU 是极好的消息 —— 意味着我们也不必做 PHY 初始化。**

---

## 1. 关键事实一：UFS PHY 由 LK 配好，Linux 不碰

### 1.1 dtsi 里没有 `phys` 属性

**`mt6895.dtsi:1919-1968` 的 `ufshci` 节点里没有 `phys`/`phy-names`。**

**整个 dtsi 里 `phys = ` 只出现 3 处**（`u2port` USB ×2、`mipi_tx0`），**UFS 没有。**

### 1.2 驱动明确容忍这种情况

**`drivers/ufs/host/ufs-mediatek.c:302-341`（`ufs_mtk_bind_mphy`）：**

```c
host->mphy = devm_of_phy_get_by_index(dev, np, 0);
...
} else if (IS_ERR(host->mphy)) {
    err = PTR_ERR(host->mphy);
    if (err != -ENODEV) { dev_info(...); }
}
if (err) host->mphy = NULL;
/*
 * Allow unbound mphy because not every platform needs specific
 * mphy control.
 */
if (err == -ENODEV)
    err = 0;                    /* ← 没有 PHY 也继续 */
```

**→ 注释原文：`Allow unbound mphy because not every platform needs specific mphy control.`**

### 1.3 这印证了我的推测

**`mt6895.config:13` 里那句「LK 已经把 UFS 配好」的推测，在主线 Linux 上被证实了。**

---

## 2. 关键事实二：reset 也是空操作

### 2.1 驱动里有 reset 逻辑

**`ufs_mtk_host_reset`（第 203-222 行）：**

```c
reset_control_assert(host->hci_reset);
reset_control_assert(host->crypto_reset);
reset_control_assert(host->unipro_reset);
reset_control_assert(host->mphy_reset);
usleep_range(100, 110);
reset_control_deassert(host->unipro_reset);
reset_control_deassert(host->crypto_reset);
reset_control_deassert(host->hci_reset);
reset_control_deassert(host->mphy_reset);
```

### 2.2 🔴 但 xaga 的 dtsi 里没有 `resets` 属性

**我 grep 了 `mt6895.dtsi:1919-1975`，`resets`/`reset-names` 计数 = 0。**

**而 `ufs_mtk_init_reset_control`（第 224-233 行）在取不到时置 NULL：**

```c
*rc = devm_reset_control_get(hba->dev, str);
if (IS_ERR(*rc)) {
    dev_info(..., "Failed to get reset control %s: %ld\n", str, PTR_ERR(*rc));
    *rc = NULL;
}
```

### 2.3 `reset_control_assert(NULL)` 是空操作

**`include/linux/reset.h:125-127`：**

```c
static inline int reset_control_assert(struct reset_control *rstc)
{
	return 0;                    /* ← 什么都不做 */
}
```

**🔴 结论：`ufs_mtk_host_reset()` 在 xaga 上完全空转。**

**→ 主线 Linux 的 UFS 路径本质上也**只是接管**，不做控制器复位。**

---

## 3. Linux 到底做了什么初始化

**剥掉 PHY 和 reset 之后，剩下的只有这些：**

| # | 做什么 | 是否必需 |
|---|---|---|
| **1** | **拿 4 个 clock 并 enable**（`ufs_sel`、`aes_ufsfde_sel`、`ufs_mem_sel`、`ufs_haxi_sel`） | **必需** |
| **2** | **拿 1 个 power-domain**（`MT6895_POWER_DOMAIN_UFS0_SHUTDOWN`）交给 runtime PM | **必需** |
| 3 | 可选 regulator | 非必需（DT 里没有 `vcc`/`supply`） |
| 4 | PHY | **不做** |
| 5 | reset | **不做**（属性缺失） |
| 6 | UFSHCI 标准流程（`hce_enable` + link startup + UIC 命令） | **必需** |

### 3.1 🔴 时钟的那个坑（硬件验证过）

**`mt6895.dtsi:1923-1947` 的注释原文：**

> "**Both are hardware-enabled by LK with `enable_count == 0`, so `clk_disable_unused()`
> switches them off and wedges the controller (verified on hardware: setting `CLK_CFG_14`
> bit 7 kills UFS instantly).**"

**即：`ufs_sel` 与 `aes_ufsfde_sel` 是 `MUX_GATE_CLR_SET_UPD` 类型，
LK 硬件使能了但引用计数是 0，Linux 的 `clk_disable_unused()` 会把它们关掉 → UFS 当场死。**

**→ 对 XNU 的含义：XNU 没有 `clk_disable_unused()` 这种机制，
所以这个坑对我们是**有利**的 —— 只要不去动 `CLK_CFG_14` bit7。**

### 3.2 power-domain 的坑

**`mt6895.dtsi:1954-1962` 注释：**

> "**Without this the domain has no device at all, so genpd's late_init pass powers it off
> unconditionally** -- which is what forced the blanket `GENPD_FLAG_ALWAYS_ON` workaround."

**→ XNU 没有 genpd，也不会有这个问题。**

---

## 4. 🔴 对照：XNU 与 Linux 的差距在哪

| 项 | Linux | **XNU（现状）** |
|---|---|---|
| PHY | 不做（LK 已配） | **也不该做** ✅ 一致 |
| reset | 不做（属性缺失） | **也不该做** ✅ 一致 |
| clock | **拿 4 个并 enable** | **❌ 没有 clock 框架** |
| power-domain | **挂 UFS0_SHUTDOWN** | **❌ 没有 genpd** |
| UFSHCI 流程 | 驱动完整实现（2559 行） | **❌ 完全没有** |
| SCSI 层 | Linux SCSI 子系统 | **❌ 没有** |
| 文件系统 | HFS+/ext4 等 | APFS 有（hackios 那套） |

### 4.1 关键判断

**K20 的 266 行 `shim/ufs.c` 之所以够用，是因为它连 clock 都不用管** ——
**它是 shim，在 ABL 刚跳转后立刻跑，那时所有 clock/power 都还是活的。**

**而 XNU 是内核，跑起来时可能已经有别的代码关掉了一些东西。**

**🔴 这是 xaga 与 K20 的**关键差异**：
K20 的 shim 是「第一时间接管」，
XNU 是「跑了一会儿之后接管」——**中间有没有人动过 UFS 的 clock，我不知道。**

---

## 5. 对 XNU 的三条可行路线（我的分析）

### 路线 1：抄 K20，纯接管（最省事）

**前提**：LK 跳转到 XNU 后，UFS 的 clock/power 都还活着，且无人关过。

**做法**：266 行左右的 UFSHCI 接管驱动，检查 `HCE`/`HCS` 就绪后接管链表，发 SCSI READ。

| 优点 | 缺点 |
|---|---|
| 代码量最小 | **强依赖「没人动过 clock」这个前提** |
| 不需要 clock/power 框架 | 若不成立则完全无效 |
| 有 K20 现成代码可抄 | |

### 路线 2：补 clock 与 power-domain 的最小实现

**做法**：除了接管，还要：
- 找到 `topckgen` 的 `CLK_TOP_U_SEL` / `CLK_TOP_AES_UFSFDE_SEL` 寄存器地址
- 确保它们处于使能态（读后写回，或至少不要关）
- 确保 `UFS0_SHUTDOWN` power-domain 是开的

| 优点 | 缺点 |
|---|---|
| 更接近 Linux 的实际做法 | 需要 SPM/power-domain 寄存器知识 |
| 不依赖「没人动过」 | 需要 LIVE DT 的 SPM 映射 |

### 路线 3：完整照搬 Linux（工作量最大）

**需要**：clock 框架、genpd、SCSI 层、UFS 驱动全套。

**→ 不建议。XNU 没有这些基础设施。**

---

## 6. 我建议的第一步：只读探针

**在选路线之前，先回答一个事实问题：LK 跳转后，UFS 控制器还在不在？**

**探针设计**（与刚做完的 CPU 探针同构）：

| 项 | 内容 |
|---|---|
| 映射 | `0x112b0000`，大小 `0x2300`，**只读** |
| 读 | `CAP(0x00)`、`VER(0x08)`、`IS(0x20)`、`HCS(0x30)`、`HCE(0x34)`、`UTRLBA(0x50)`、`UTRLBAU(0x54)`、`UTRLDBR(0x58)`、`UTRLRSR(0x60)` |
| 判断 | `HCE` bit0（host enable）、`HCS` bit0（`DP`，device present）、bit1（`UTRLRDY`）、bit3（`UCRDY`） |
| 输出 | 全部原始值 + 三项就绪判断 |
| **绝不写** | 不写 `UTRLRSR`/`UTRLBA`/`UTRLDBR`，不发 SCSI，不碰 clock |
| 参数 | `mtk_ufs_probe=1` |

**它能一次回答三个问题：**

1. **控制器还在不在**（`HCE` bit0）
2. **设备还在不在**（`HCS` bit0 `DP`）
3. **链表就绪没有**（`HCS` bit1 `UTRLRDY`）

**成本：一个 200 行左右的驱动 + 一次刷写。风险：纯读，零写。**

---

## 7. 还需要 Codex 判定的

| # | 问题 |
|---|---|
| 1 | 只读探针的寄存器白名单够不够（是否需要读 UIC 命令相关） |
| 2 | 若 `HCE` 已关，下一步查哪里（clock？power-domain？还是 LK 行为） |
| 3 | 路线 1（抄 K20）vs 路线 2（补 clock） |
| 4 | **XNU 侧 `ml_io_map` 映射 `0x112b0000` 是否需要先确保该区域时钟开着** |
| 5 | APFS 挂载：用 hackios 的 `bsd/miscfs/apfs`（那是能读 `system.apfs` 的那套） |

---

## 8. 附：主线 Linux 调研文档里另外两条与存储相关的

**来自 `work-xaga/research/DEEP-mt6895-mainline.md`：**

| 项 | 内容 | 出处 |
|---|---|---|
| **未确认第 4 条** | **「UFS 的 PHY 具体寄存器配置完全未知」** —— dtsi 无 ufsphy 节点、驱动不碰，**本树里没有任何信息** | 文档第 2690 行附近 |
| **未确认第 5 条** | **「UFS 是否真工作在 HS-G4 / 几 lane，没找到」** —— `freq-table-hz` 全 0，无 gear/lane 信息 | 同上 |
| **P0 第 10 条** | UFS 无 PHY 节点，且 `ufs_sel`/`aes_ufsfde_sel` 被关闭会当场杀 UFS | 文档第 2625 行附近 |
| **P3 第 38 条** | UFS reset 顺序 assert hci→crypto→unipro→mphy，deassert unipro→crypto→hci→mphy（不对称） | `ufs-mediatek.c:203-222` |

**🔴 但 P3 第 38 条那个 reset 顺序，在 xaga 上因为 DT 没有 `resets` 属性，实际不起作用。**

---

## 9. 边界

**本轮只做**：读 `/tmp/mt6895-mainline` 源码、读 `mt6895.dtsi`、读 `include/linux/reset.h`、
读已有调研文档。

**没有做**：改代码、构建、碰设备、读写寄存器。

**「LK 保留 UFS 状态」现在有了更强的间接证据（Linux 不碰 PHY/reset 也能用），
但仍是**推断**，不是直接观测。**
