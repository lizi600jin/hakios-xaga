# MT6895 在 Linux 主线的支持现状

调研时间：2026-11-14（Asia/Shanghai）
目标设备：Xiaomi xaga（Redmi Note 11T Pro/Pro+、POCO X4 GT、Redmi K50i）
调研方法：抓 MARC / Openwall 邮件列表原文 + 直接读 git 仓库树 + HTTP 探测 torvalds/linux 文件存在性

---

## 0. 一句话结论

**MT6895 至今没有进入 Linux 主线（torvalds/linux）——一行都没有**；上游只有一个
Alexander Letuchiy 的 v2 补丁系列（2026-11-13，尚未合入、停在邮件列表），
而**真正可用的、寄存器级完整的硬件描述在 GitHub 的 `MT6895-Mainline/linux`（yefxx 团队，
基于 Linux 7.2）里**，那棵树已经有 7254 行的 `mt6895.dtsi`、完整的 clk/iommu/DRM/panthor
改动，是 HakiOS 唯一有实用价值的参考源。

> 可信度分级（全篇遵守）：
> - **[主线]** = 已在 torvalds/linux master 中存在（可 HTTP 取到）
> - **[邮件列表]** = 只在 lore/MARC/Openwall 的 patch 里，未合入
> - **[下游树]** = MT6895-Mainline/linux 分支里，非上游、非厂商
> - **[未确认]** = 没有找到出处

---

## 1. Alexander Letuchiy 的 patch 系列（完整信息 + 链接）

### 1.1 版本谱系

| 版本 | 日期 | 规模 | Message-ID | 状态 |
|---|---|---|---|---|
| v1 | 2026-11-05 21:05:58 | 5 patch, +219 行 | `20261105210622.523747-1-asalde.le1@gmail.com` | 被 v2 取代 |
| **v2** | **2026-11-13 19:04:56** | **9 patch, +2082 行** | `20261113-initial-mt6895-v2-0-4ab0aa9fa391@gmail.com` | **当前最新** |

**本报告主要读的是 v2。** v1 只有 166 行的 SoC DT（仅 CPU/GIC/timer/watchdog/PSCI），
v2 扩到 461 行并新增了 cache 节点、CPU capacity、idle states、PMU、系统定时器、pinctrl、keypad。

### 1.2 v2 的 9 个 patch

```
1/9  dt-bindings: arm: mediatek: Add MT6895 and Xiaomi Poco X4 GT
2/9  dt-bindings: serial: mediatek,uart: Add compatible for MT6895
3/9  dt-bindings: watchdog: mediatek,mtk-wdt: Add compatible for MT6895
4/9  watchdog: mediatek: Add support for MT6895
5/9  dt-bindings: timer: mediatek,timer: Add compatible for MT6895
6/9  dt-bindings: input: mediatek,mt6779-keypad: Add compatible for MT6895
7/9  arm64: dts: mediatek: Add MT6895 pinmux macro header file   (+1524 行)
8/9  arm64: dts: mediatek: Add MT6895 SoC                          (+461 行)
9/9  arm64: dts: mediatek: Add Xiaomi Poco X4 GT                  (+87 行)
```

v2 的 cover letter 明确写：`Based on next-20261113`，`base-commit: 84abb955b1015477aa11a96e2f3ac1b938b77f48`。

### 1.3 v2 相对 v1 的改动（cover letter 原文要点）

- MT6895 统一描述为 **Dimensity 8000/8100**（v1 只写 8100）
- watchdog binding **删掉 `mediatek,mt6589-wdt` fallback**（因为 MT6895 的 reset layout 未知），
  同时撤掉 Conor 的 Acked-by；**新增一个 patch 给 mtk_wdt 驱动加 MT6895 compatible**
- 新增：系统定时器 binding、keypad binding、pinmux 头文件
- SoC DT 新增：cache 节点、CPU capacities、idle states、CPU PMU + PPI partitions、
  DSU PMU、13 MHz 系统定时器、pinctrl、keypad
- 架构定时器 PPI 改用 `IRQ_TYPE_LEVEL_HIGH`
- **本系列依赖 MT6895 pinctrl 系列**（见 1.5）

### 1.4 作者自己报告的实测结果（POCO X4 GT / 8 GiB）

这些是**已验证**的行为，对裸机移植参考价值很高：

- 8 个 CPU 全部起来，A55 在 0-3、A78 在 4-7；CPU1-7 可通过 PSCI offline/online
- 每个 CPU 都能找到 redistributor，定时器中断能到每个核
- **TF-A 在启动后约 40 秒内拒绝 CPU_SUSPEND**；厂商内核用 SiP call 提前解除，
  上游这组 patch 没有，所以 idle state 要等 40 秒后才生效
- **DSU PMU 寄存器在 cluster 掉电后丢失**（TF-A 没开 `PRESERVE_DSU_PMU_REGS`），
  只有 cluster 保持上电时 DSU PMU 计数才准
- DSU PMU 溢出中断在 **SPI 18**
- UART0 在 **921600 baud** 工作，TX 中断正常
- 约 7 GiB 内存写入校验无错
- watchdog 停止喂狗能复位板子，ramoops 控制台日志能活过复位
- keypad 音量下键按下/松开正常，**32 ms 去抖**
- **只测了 minimal config，没测 defconfig；UART1 关闭未测**

### 1.5 依赖的 pinctrl 系列（独立提交，不在上面 9 个里）

| 项 | 内容 |
|---|---|
| 标题 | `[PATCH 0/2] pinctrl: mediatek: Add MT6895 support` |
| 日期 | 2026-11-08 |
| Message-ID | `20261108-pinctrl-mt6895-v1-0-f704c443b31a@gmail.com` |
| 规模 | 5 文件 +4573 行（`pinctrl-mt6895.c` 1617 行 + `pinctrl-mtk-mt6895.h` 2717 行） |
| base | `2637bdec0535a2044cd830eba9a79363651d9043`（next-20261102） |

它的 cover letter 里有一句很关键：

> The tables come from the MediaTek driver in the Xiaomi kernel source release [1].
> They were checked field by field against that driver and against register dumps
> from the device running the vendor kernel.
> [1] https://github.com/MiCode/Xiaomi_Kernel_OpenSource/tree/56ec519f6 （branch `xaga-s-oss`）

即：**pinctrl 的 2717 行寄存器表是从小米官方开源内核里逐字段核对 + 真机寄存器 dump 验证的**。
这条对我们是权威性最强的上游材料。

### 1.6 合入状态

**未合入。** 截至 2026-11-14：
- torvalds/linux master 里没有任何 mt6895 / mt6368 文件（见 §2 探测结果）
- MARC 上 v2 的 thread 只有 4 条，全是 cover letter 本身在 4 个 list 的交叉投递
  （linux-input / devicetree / linux-kernel / linux-serial），**没有 review 回复**
- 也就是说：**v2 处于「投出去了，还没人 review」状态**

### 1.7 链接

- v2 cover（MARC）：https://marc.info/?i=20261113-initial-mt6895-v2-0-4ab0aa9fa391%40gmail.com&q=raw
- v2 patch 8/9 全文（Openwall）：https://lists.openwall.net/linux-kernel/2026/11/13/2592
- v1 cover（MARC）：https://marc.info/?l=linux-kernel&m=179391301503224&q=raw
- pinctrl 0/2 cover（MARC）：https://marc.info/?l=linux-gpio&m=179416229778104&q=raw
- v1 → v2 的 link：https://patch.msgid.link/20261105210622.523747-1-asalde.le1@gmail.com

> 注：`lore.kernel.org` 的 `/raw` 端点对非浏览器 UA 返回 403，且带 UA 时会被 Anubis
> 反爬页拦截（"Making sure you're not a bot!"）。本报告的 patch 原文全部改从
> **MARC** 与 **Openwall (lists.openwall.net)** 取得。

---

## 2. 主线内核现状

### 2.1 探测方法

```bash
curl -s -o /dev/null -w "%{http_code}" \
  https://raw.githubusercontent.com/torvalds/linux/master/<path>
```

对照实验（已知存在的文件）确认方法有效，均返回 200：
`arch/arm64/boot/dts/mediatek/mt8195.dtsi`、
`drivers/clk/mediatek/clk-mt8195-topckgen.c`、
`drivers/pinctrl/mediatek/pinctrl-mt8195.c`、
`drivers/mfd/mt6397-core.c`、`sound/soc/codecs/mt6359.c`、
`drivers/gpu/drm/panthor/panthor_drv.c`。

### 2.2 探测结果

| 文件路径 | HTTP | 结论 |
|---|---|---|
| `arch/arm64/boot/dts/mediatek/mt6895.dtsi` | **404** | 不存在 |
| `arch/arm64/boot/dts/mediatek/mt6895-xiaomi-xaga.dts` | **404** | 不存在 |
| `drivers/clk/mediatek/clk-mt6895.c` | **404** | 不存在 |
| `drivers/pinctrl/mediatek/pinctrl-mt6895.c` | **404** | 不存在 |
| `drivers/mfd/mt6368.c` | **404** | 不存在 |
| `drivers/regulator/mt6368-regulator.c` | **404** | 不存在 |
| `sound/soc/codecs/mt6368.c` | **404** | 不存在 |

外加：`arch/arm64/boot/dts/mediatek/Makefile` 里 `grep` 不到任何 `mt6895` 条目
（只有 mt8167/mt8173/mt8183/mt8186/mt8195/mt8196 等）。

**结论：MT6895 在 Linux 主线里支持度为零。** MT6363 的 regulator（`mt6363-regulator.c`）
在主线存在（MT8196 平台在用），但 MT6368 **完全不存在**——mfd、regulator、codec、accdet 都没有。

### 2.3 patch 停在哪

见 §1.6：v2 已投，无 review 回复，未进任何 maintainer tree。
无法确认是否进了 `mediatek` 的 next 分支（`[未确认]`，未查 linux-next 的具体树）。

---

## 3. 从 patch 提取的硬件事实

> 本节数据来源：v2 patch 8/9 原文（`lists.openwall.net/linux-kernel/2026/11/13/2592`）
> 已标注 **[邮件列表]** 的项，并用 **[下游树]** 的 `MT6895-Mainline/linux` 交叉核对。
> 交叉核对结果见 §3.3。

### 3.1 内存映射表（对裸机移植有用的部分）

| 模块 | 基址 | 大小 | compatible | 出处 |
|---|---|---|---|---|
| GIC distributor | `0x0c000000` | `0x40000` | `arm,gic-v3` | v2 p8 |
| GIC redistributor | `0x0c040000` | `0x200000` | 同上 | v2 p8 |
| pinctrl（GPIO base） | `0x10005000` | `0x1000` | `mediatek,mt6895-pinctrl` | v2 p8 |
| **topckgen** | `0x10000000` | `0x1000` | `mediatek,mt6895-topckgen` | 下游树 |
| **apmixedsys** | `0x1000c000` | `0x1000` | `mediatek,mt6895-apmixedsys` | 下游树 |
| infracfg_ao | `0x10001000` | `0x1000` | `mediatek,mt6895-infracfg-ao` | 下游树 |
| SPM（电源管理） | `0x10006000` | `0x1000` | — | 下游树（scp_infra 第二段） |
| UART0 | `0x11001000` | `0x1000` | `mediatek,mt6895-uart` + `mt6577-uart` | v2 p8 |
| UART1 | `0x11002000` | `0x1000` | 同上 | v2 p8 |
| pericfg-ao | `0x11036000` | `0x1000` | `mediatek,mt6895-pericfg-ao` | 下游树 |
| MIPI TX config | `0x11f70000` | `0x1000` | `mediatek,mt6895-mipi-tx` | 下游树 |
| eint（EINT 控制器） | `0x11e50000` | `0x1000` | `mediatek,mt6983-eint` | 下游树 |
| **watchdog** | `0x1c007000` | `0x100` | `mediatek,mt6895-wdt` | v2 p8 |
| keypad | `0x1c00e000` | `0x1000` | `mediatek,mt6895-keypad` + `mt6779-keypad` | v2 p8 |
| **系统定时器 systimer** | `0x1c011000` | `0x1000` | `mediatek,mt6895-timer` + `mt6765-timer` | v2 p8 |
| dvfsrc | `0x1c00f000` | `0x1000` | `mediatek,mt6895-dvfsrc` | 下游树 |
| scpsys（电源域） | `0x1c001000` | `0x1000` | `mediatek,mt6895-scpsys` | 下游树 |
| **SPMI** | `0x1c804000` | `0x900` | `mediatek,mt6895-spmi` | 下游树 |
| SPMI master（第二段） | `0x1c801000` | `0x100` | reg-name `spmimst` | 下游树 |
| **GPU (Mali-G610)** | `0x13000000` | `0x480000` | `arm,mali-valhall-csf` | 下游树 |
| **GPUEB** | `0x13c40000` 区段 | 见 §5 | 私有 | 下游树 |
| MFG cfg（时钟门控） | `0x13fbf000` | `0x1000` | `mediatek,mt6895-mfg` | 下游树 |
| MFGPLL ctrl | `0x13fa0000` | `0x400` | `mediatek,mt6895-mfgpll_pll_ctrl` | 下游树 |
| MFGSCPLL ctrl | `0x13fa0c00` | `0x400` | `mediatek,mt6895-mfgscpll_pll_ctrl` | 下游树 |
| GPU EB RPC | `0x13f91000` | `0x1000` | `mediatek,mt6895-gpu_eb_rpc_bus` | 下游树 |
| **UFS host** | `0x112b0000` | `0x2300` | `mediatek,mt8183-ufshci` | 下游树 |
| UFS cfg ao | `0x112b8000` | `0x1000` | `mediatek,mt6895-ufscfg-ao` | 下游树 |
| USB0 (mtu3) | `0x11201000` | `0x2e00` | `mediatek,mtu3` | 下游树 |
| **GCE（display）** | `0x1e980000` | `0x4000` | `mediatek,mt6895-gce` | 下游树 |
| GCE（multimedia） | `0x1e990000` | `0x4000` | `mediatek,mt6895-gce` | 下游树 |
| **dispsys（主）** | `0x14000000` | `0x1000` | `mediatek,mt6895-disp` | 下游树 |
| dispsys（第二 pipe） | `0x14400000` | `0x1000` | 同上 | 下游树 |
| DSI | `0x14017000` | `0x1000` | `mediatek,mt6895-dsi` | 下游树 |
| disp PWM | `0x1100e000` | `0x1000` | `mediatek,mt6895-disp-pwm` | 下游树 |
| mmsys0 | `0x14000000` | `0x1000` | `mediatek,mt6895-mmsys0` | 下游树 |
| mmsys1 | `0x14400000` | `0x1000` | `mediatek,mt6895-mmsys1` | 下游树 |
| mminfra config | `0x1e800000` | `0x1000` | `mediatek,mt6895-mminfra_config` | 下游树 |
| **disp IOMMU** | `0x1e802000` | `0x1000` | `mediatek,mt6895-disp-iommu` | 下游树 |
| **mdp IOMMU** | `0x1e810000` | `0x1000` | `mediatek,mt6895-mdp-iommu` | 下游树 |
| **AFE（音频前端）** | `0x1e100000` | `0x1000` | `mediatek,mt6895-afe` | 下游树 |
| **SCP** | `0x1c400000` | `0x180000` (TCM) | `mediatek,scp` | 下游树 |
| SCP cfg | `0x1c724000` | `0x1000` | 同上 | 下游树 |
| SCP mbox0-4 | `0x1c7fb000`..`0x1c7ff000` | `0x100` each | 同上 | 下游树 |
| **DRAMC** | `0x10230000` | `0x2000` (AO CHA) | `mediatek,mt6895-dramc` | 下游树 |
| **LVTS（温度传感器）** | `0x10315000` / `0x10316000` | `0x1000` each | `mediatek,mt6895-lvts` | 下游树 |
| **CPU 性能控制器** | `0x11bc10` | `0x120` ×3 | `mediatek,cpufreq-hw` | 下游树 |
| NEMI bus | `0x10270000` | `0x1000` | `mediatek,mt6895-nemi_bus` | 下游树 |
| SEMI bus | `0x1030e000` | `0x1000` | `mediatek,mt6895-semi_bus` | 下游树 |

**EINT 的 5 段基址**（`mediatek,mt6983-eint`）：
`0x11e50000`、`0x11ce0000`、`0x11de0000`、`0x11fe0000`、`0x1c01e000`。

**pinctrl 的 19 段基址**（v2 p8，reg-names 依次为
`base, rm, rt, rmm, bl, bm, br, brr, lb, lm, rtt, tr, tl, lt, eint-w, eint-e, eint-s, eint-n, eint-c`）：
```
0x10005000 0x11c20000 0x11c30000 0x11c40000 0x11d10000
0x11d30000 0x11d40000 0x11d50000 0x11d60000 0x11e10000
0x11ea0000 0x11eb0000 0x11f20000 0x11f30000 0x11e50000
0x11ce0000 0x11de0000 0x11fe0000 0x1c01e000
```
GPIO 数量：`gpio-ranges = <&pio 0 0 227>`，即 **227 个 GPIO**。

### 3.2 中断配置

**GIC**（v2 p8）：

```dts
gic: interrupt-controller@0c000000 {
    compatible = "arm,gic-v3";
    reg = <0 0x0c000000 0 0x40000>,      /* distributor */
          <0 0x0c040000 0 0x200000>;     /* redistributor，2 MiB */
    #interrupt-cells = <4>;
    interrupts = <GIC_PPI 9 IRQ_TYPE_LEVEL_HIGH 0>;   /* 维护中断 PPI 9 */

    ppi-partitions {
        ppi_cluster0: interrupt-partition-0 { affinity = <&cpu0 &cpu1 &cpu2 &cpu3>; };
        ppi_cluster1: interrupt-partition-1 { affinity = <&cpu4 &cpu5 &cpu6 &cpu7>; };
    };
};
```

**关键：`#interrupt-cells = <4>`**，即中断说明符是
`<type number flags partition>` 四元组。移植到 XNU 时解析 DT 必须按 4 cell 处理。

**已知中断号汇总**：

| 外设 | 中断 | 类型 | 出处 |
|---|---|---|---|
| UART0 | `GIC_SPI 141` | LEVEL_HIGH | v2 p8 |
| UART1 | `GIC_SPI 142` | LEVEL_HIGH | v2 p8 |
| keypad | `GIC_SPI 106` | **EDGE_RISING** | v2 p8 |
| 系统定时器 | `GIC_SPI 280` | LEVEL_HIGH | v2 p8 |
| pinctrl/EINT | `GIC_SPI 259` | LEVEL_HIGH | v2 p8 |
| DSU PMU 溢出 | `SPI 18` | — | v2 cover |
| GPU job | `GIC_SPI 514` | LEVEL_HIGH | 下游树 |
| GPU mmu | `GIC_SPI 513` | LEVEL_HIGH | 下游树 |
| GPU gpu | `GIC_SPI 512` | LEVEL_HIGH | 下游树 |
| GPUEB mailbox0 | `GIC_SPI 516` | LEVEL_HIGH | 下游树 |
| SPMI pmif | `GIC_SPI 414` | LEVEL_HIGH | 下游树 |
| SPMI rcs | GPIO 243（EINT） | LEVEL_HIGH | 下游树 |

### 3.3 定时器

**两套并存**：

1. **架构定时器（architected timer，CNTP/CNTV）**
   ```dts
   timer {
       compatible = "arm,armv8-timer";
       interrupts = <GIC_PPI 13 IRQ_TYPE_LEVEL_HIGH 0>,   /* secure phys */
                    <GIC_PPI 14 IRQ_TYPE_LEVEL_HIGH 0>,   /* non-secure phys */
                    <GIC_PPI 11 IRQ_TYPE_LEVEL_HIGH 0>,   /* virtual */
                    <GIC_PPI 10 IRQ_TYPE_LEVEL_HIGH 0>;   /* hyp */
   };
   ```
   v2 里 PPI 用 **LEVEL_HIGH**（v1 用的是别的，v2 改的）。
   下游树额外加了 `clock-frequency = <13000000>`（**13 MHz**）。

2. **MediaTek 系统定时器（systimer）** — v2 新增
   ```dts
   systimer: timer@1c011000 {
       compatible = "mediatek,mt6895-timer", "mediatek,mt6765-timer";
       reg = <0 0x1c011000 0 0x1000>;
       interrupts = <GIC_SPI 280 IRQ_TYPE_LEVEL_HIGH 0>;
       clocks = <&clk13m>;
   };
   ```
   **注意：下游树 `MT6895-Mainline/linux` 里搜不到 `0x1c011000`** —— 那棵树没启用 systimer。
   也就是说 systimer 只在 Letuchiy 的 patch 里有，未经第二来源交叉验证。

**CPU idle states / PSCI 参数**（v2 p8 原文，`entry-method = "psci"`）：

| state | `arm,psci-suspend-param` | entry lat | exit lat | min residency | local-timer-stop |
|---|---|---|---|---|---|
| `cpu-off-l` | `0x00010001` | 50 µs | 100 µs | 1600 µs | ✅ |
| `cpu-off-b` | `0x00010002` | 50 µs | 100 µs | 1400 µs | ✅ |
| `cluster-off-l` | `0x01010101` | 100 µs | 250 µs | 2100 µs | ✅ |
| `cluster-off-b` | `0x01010102` | 100 µs | 250 µs | 1900 µs | ✅ |

**PSCI**：`compatible = "arm,psci-1.0"`，`method = "smc"`。

`suspend-param` 的解码（ARM PSCI 规范）：bit[31:24] = 0x01 表示 cluster 也关机，
低 16 位是 state ID（`0x0001` = L 状态 / `0x0002` = B 状态；`0x0101`/`0x0102` 是 cluster 级）。
**XNU 的 `cpu_idle` / PSCI 调用要按这个 param 传。**

**关键限制**：TF-A 在启动后约 40 秒内拒绝 CPU_SUSPEND（v2 cover 原文）。

### 3.4 watchdog

```dts
watchdog: watchdog@1c007000 {
    compatible = "mediatek,mt6895-wdt";     /* v2：没有 mt6589 fallback */
    reg = <0 0x1c007000 0 0x100>;
};
```

- v2 的 cover letter 明确说：**删掉了 `mediatek,mt6589-wdt` fallback，因为 MT6895 的 reset layout 未知**
  （v1 是有的，见 Openwall 的 `[PATCH 3/5]` 原文：`uses mediatek,mt6589-wdt as its fallback`）
- 同时 v2 新增 patch 4/9 给 `drivers/watchdog/mtk_wdt.c` 加一行 MT6895 compatible
- **下游树反而保留了 fallback**：
  `compatible = "mediatek,mt6895-wdt", "mediatek,mt6589-wdt", "syscon";`
  说明实际硬件上 mt6589 布局大概率可用，但上游作者出于谨慎没写

移植提示：watchdog 寄存器只有 `0x100` 字节，位于 `0x1c007000`，是 AP WDT。

### 3.5 时钟树

**两个 PLL 控制器基址**（下游树）：
- `topckgen` @ `0x10000000`（size `0x1000`）→ `mediatek,mt6895-topckgen`
- `apmixedsys` @ `0x1000c000`（size `0x1000`）→ `mediatek,mt6895-apmixedsys`

**已知的两个固定时钟**（v2/下游树都有）：
- `clk26m` = **26 MHz**（晶体，几乎所有外设的根）
- `clk13m` = **13 MHz**（systimer 用，由 26 M 分频）

**GPU 相关 PLL**（下游树，这是 G610 变频的关键）：

| PLL | 基址 | 用途 |
|---|---|---|
| `MFGPLL` | `0x13fa0000` (`0x400`) | GPU **core** 时钟源，经 `CLK_TOP_MFG_SEL_0_CK` |
| `MFGSCPLL` | `0x13fa0c00` (`0x400`) | GPU **stacks**（shader complex）时钟源，经 `CLK_TOP_MFG_SEL_1_CK` |
| mfgcfg | `0x13fbf000` (`0x1000`) | MFG 门控，含 `CLK_MFGCFG_BG3D` |

mux 名字（来自下游 dtsi 注释）：
- core: **`MFG_SEL_0`**，参考频率 **231 MHz**（MFGPLL）
- stacks: **`MFG_SEL_1`**，参考频率 **218 MHz**（MFGSCPLL）

**下游树的 clk 驱动文件清单**（证明时钟树被完整拆开过）：

```
drivers/clk/mediatek/clk-mt6895.c              ← topckgen + apmixedsys 主表
drivers/clk/mediatek/clk-mt6895-afe.c          ← 音频前端
drivers/clk/mediatek/clk-mt6895-cam.c
drivers/clk/mediatek/clk-mt6895-img.c
drivers/clk/mediatek/clk-mt6895-mdpsys.c
drivers/clk/mediatek/clk-mt6895-mfgcfg.c
drivers/clk/mediatek/clk-mt6895-mminfra.c
drivers/clk/mediatek/clk-mt6895-mmsys0.c
drivers/clk/mediatek/clk-mt6895-mmsys1.c
drivers/clk/mediatek/clk-mt6895-peri-ao.c
drivers/clk/mediatek/clk-mt6895-vcodec.c
```

### 3.6 PMIC 连接方式（SPMI）

```dts
spmi: spmi@1c804000 {
    compatible = "mediatek,mt6895-spmi";
    reg = <0x1c804000 0x900>,        /* pmif */
          <0x1c801000 0x100>;        /* spmimst（SPMI master）*/
    reg-names = "pmif", "spmimst";
    clocks = <&clk_pmif_sys>, <&clk_pmif_tmr>, <&clk_spmimst>;
    clock-names = "pmif_sys_ck", "pmif_tmr_ck", "spmimst_clk_mux";
    #address-cells = <2>;
    #size-cells = <0>;
    interrupts-extended = <&pio 243 IRQ_TYPE_LEVEL_HIGH>,              /* rcs_irq */
                          <&gic GIC_SPI 414 IRQ_TYPE_LEVEL_HIGH 0>;    /* pmif_irq */
    interrupt-names = "rcs_irq", "pmif_irq";
    interrupt-controller;
    #interrupt-cells = <1>;
};
```

**注意 SPMI 的 `#interrupt-cells = <1>`，而 GIC 是 4、pinctrl 是 2。**

**SPMI slave 地址分配（`reg = <usid 0>`）**：

| 设备 | 地址 | compatible | 出处 |
|---|---|---|---|
| **MT6363**（主 PMIC） | `pmic@4` → `<4 0>` | `mediatek,mt6363` | 下游树 |
| **MT6368**（codec/accdet） | `second_pmic@5` → `<5 0>` | `mediatek,mt6368` | 下游树 |
| MT6319 #6 | `pmic@6` → `<6 0>` | `mediatek,mt6319-regulator`+`mt6315-regulator` | xaga dts |
| MT6319 #7 | `pmic@7` → `<7 0>` | 同上 | xaga dts |

**SPMI 关键事实**：
- PMIC 中断线（rcs_irq）走 **GPIO 243**（EINT 能力，与下游一致）
- PMIF 中断走 **GIC SPI 414**
- pmif 寄存器窗口只有 **`0x900`** 字节，spmimst 是 **`0x100`**

### 3.7 CPU 拓扑

| CPU | 核 | MPIDR reg | cluster |
|---|---|---|---|
| cpu0-3 | Cortex-A55 | `0x0` / `0x100` / `0x200` / `0x300` | 0 |
| cpu4-7 | Cortex-A78 | `0x400` / `0x500` / `0x600` / `0x700` | 1 |

- PSCI：`compatible = "arm,psci-1.0"`，`method = "smc"`
- PPI partition 按 cluster 切成两组（见 §3.2）
- cpufreq：`mediatek,cpufreq-hw` @ `0x11bc10`/`0x11bd30`/`0x11be50`，各 `0x120`

**缓存拓扑**（v2 p8）：

| 层级 | 大小 | line | sets | 说明 |
|---|---|---|---|---|
| A55 L1 I/D | 32 KB + 32 KB | 64 | 128 | 私有 |
| A78 L1 I/D | 64 KB + 64 KB | 64 | 256 | 私有 |
| L2（每核） | **128 KB** | 64 | 512 | `cache-level = <2>`，`cache-unified` |
| **L3 / DSU** | **4 MB** | 64 | **4096** | `l3_0`，`cache-level = <3>`，`cache-unified` |

注意 L2 是**每核独立 128 KB**（不是 A55/A78 各共享一块），共 8 个 L2 节点。
L3 = DSU 的 4 MB，**全 8 核共享**。

**capacity-dmips-mhz**：A55 = **380**，A78 = **1024**
（即 A78 单核性能约是 A55 的 2.69 倍，调度器用它算能效）。

**PMU**：
```dts
dsu-pmu {
    compatible = "arm,dsu-pmu";
    interrupts = <GIC_SPI 18 IRQ_TYPE_LEVEL_HIGH 0>;    /* DSU PMU = SPI 18 */
    cpus = <&cpu0> ... <&cpu7>;
};

pmu-a55 {
    compatible = "arm,cortex-a55-pmu";
    interrupts = <GIC_PPI 7 IRQ_TYPE_LEVEL_HIGH &ppi_cluster0>;   /* PPI 7，cluster0 */
};

pmu-a78 {
    compatible = "arm,cortex-a78-pmu";
    interrupts = <GIC_PPI 7 IRQ_TYPE_LEVEL_HIGH &ppi_cluster1>;   /* PPI 7，cluster1 */
};
```

**注意：两个 cluster 的 PMU 都用 PPI 7**，靠 `ppi-partitions` 按 affinity 隔离 ——
这就是为什么 GIC 必须做 partition（`#interrupt-cells = <4>` 的第 4 个 cell）。

### 3.8 v2 与下游树的交叉验证结果

对 v2 patch 8 里出现的地址，逐个在下游 `mt6895.dtsi` 里 grep：

| 地址 | 模块 | 下游树命中 |
|---|---|---|
| `0x0c000000` | GIC | ✅ 1 |
| `0x10005000` | pinctrl | ✅ 3 |
| `0x11001000` | UART0 | ✅ 1 |
| `0x1c007000` | watchdog | ✅ 1 |
| `0x1c00e000` | keypad | ✅ 1 |
| **`0x1c011000`** | **systimer** | ❌ **0（下游未启用）** |
| `0x10000000` | topckgen | ✅ 2 |
| `0x1000c000` | apmixedsys | ✅ 1 |
| `0x1c804000` | SPMI | ✅ 1 |
| `0x13000000` | GPU | ✅ 1 |

**结论：除 systimer 外，v2 的所有地址都在下游树里得到独立确认。**
这让 v2 的 SoC 内存映射可信度相当高（两个独立来源一致）。

---

## 4. MT6368 相关

### 4.1 上游状态

**主线：完全没有。** 探测结果（§2.2）：
- `drivers/mfd/mt6368.c` → 404
- `drivers/regulator/mt6368-regulator.c` → 404
- `sound/soc/codecs/mt6368.c` → 404

（对照：MT6363 的 `drivers/regulator/mt6363-regulator.c` 在主线**存在**，因为 MT8196 平台在用。）

**Letuchiy 的 patch 系列：也不含 MT6368。** 他那 9 个 patch 只到 UART/watchdog/keypad/pinctrl 级别。

### 4.2 下游树里 MT6368 的完整实现（这是唯一来源）

`MT6895-Mainline/linux` 分支里有的文件：

```
include/linux/mfd/mt6368/core.h            ← 寄存器定义
include/linux/mfd/mt6368/registers.h
include/linux/regulator/mt6368-regulator.h
drivers/regulator/mt6368-regulator.c
sound/soc/codecs/mt6368.c                  ← codec
sound/soc/codecs/mt6368.h
sound/soc/codecs/mt6368-accdet.c           ← 耳机检测
sound/soc/codecs/mt6368-accdet.h
sound/soc/mediatek/mt6895/mt6895-mt6368.c  ← 机器驱动（card）
```

MT6363 侧：
```
include/linux/mfd/mt6363/core.h
include/linux/mfd/mt6363/registers.h
include/linux/regulator/mt6363-regulator.c
drivers/regulator/mt6363-mt6895.c          ← MT6895 专用的 rail 表
Documentation/devicetree/bindings/regulator/mediatek,mt6363-regulator.yaml
```

### 4.3 MT6368 的 DT 节点（下游，SoC dtsi 里）

```dts
mt6368: second_pmic@5 {
    compatible = "mediatek,mt6368";
    reg = <5 0>;
    interrupts = <5>;
    interrupt-controller;
    #interrupt-cells = <2>;

    accdet {
        compatible = "mediatek,mt6368-accdet";
        accdet-name = "mt63xx-accdet";
        accdet-mic-vol = <8>;
        accdet-plugout-debounce = <1>;
        accdet-mic-mode = <2>;
        headset-mode-setting = <0x500 0x500 0x01 0x1f0 0x800 0x800
                                0x20 0x44 0x04 0x01 0x05 0x03 0x03 0x05 0x0e>;
        headset-use-ap-eint = <0>;
        headset-eint-num = <0>;
        headset-eint-trig-mode = <1>;
        headset-key-mode = <0>;
        headset-three-key-threshold     = <0 0x50 0xdc 0x190>;
        headset-three-key-threshold-CDD = <0 0x79 0xc0 0x258>;
        headset-four-key-threshold      = <0 0x3a 0x79 0xc0 0x190>;
        io-channels = <&mt6368_auxadc 9>;
        nvmem = <&mt6368_efuse>;
    };

    mt6368_auxadc {
        compatible = "mediatek,mt6368-auxadc";
        chip_temp   { channel = <5>;  };
        accdet      { channel = <9>;  };     ← 耳机检测 ADC 通道
        hpofs_cal   { channel = <12>; avg-num = <256>; };  ← 耳机偏置校准
        vin1_open   { channel = <19>; };
        vin2_open   { channel = <20>; };
    };

    mt6368_efuse {
        compatible = "mediatek,mt6368-efuse";
        mt6368_e_data { reg = <0x52 0xa>; };
    };

    mt6368codec: mt6368codec {
        compatible = "mediatek,mt6368-sound";
        io-channels = <&mt6368_auxadc 12>;
        io-channel-names = "pmic_hpofs_cal";
        nvmem = <&mt6368_efuse>;
    };
};
```

**对 HakiOS 有用的几个点**：
- accdet 的 `headset-mode-setting` 是 15 个 32-bit 值，直接对应 MT6368 的 accdet 寄存器序列
- 三键/四键耳机的阈值分别是 4/5 个值，这是 ADC 判定按键的边界
- `hpofs_cal` 在 auxadc **channel 12**，即耳机输出偏置校准值

### 4.4 音频拓扑（xaga board dts + quirks 仓库）

```dts
sound {
    compatible = "mediatek,mt6895-mt6368-sound";
    mediatek,platform = <&afe>;
    mediatek,headset-codec = <&mt6368codec>;
    mediatek,spk-i2s = <3 9>;          /* I2S3 出给 smart amp；I2S0 做 echo 参考 */
    mediatek,speaker-codec {
        sound-dai = <&speaker_amp_l>, <&speaker_amp_r>;
    };
};
```

**quirks 仓库（`MT6895-Mainline/quirks`）里的关键事实**：

1. **`ucm/mt6895-mt6368/HiFi.conf`** —— ALSA UCM 配置，把三条 PCM 分开：
   ```
   hw:0  Playback_1 → DL1 → ADDA_DL → MT6368 DAC   （3.5mm 耳机）
   hw:1  Capture_1  ← UL1 ← MTKAIF mic
   hw:2  Playback_2 → DL2 → I2S3 → TFA9874 扬声器
   ```
   喇叭是 **2× TFA9874** smart amp。

2. **一条重要的硬件事实（UCM 注释原文）**：
   > on xaga the codec's AIF data input sits on the **AUD_DAT_MOSI2 pad**,
   > i.e. the **ADDA_CH34** lanes, so Headphone enables CH3/CH4
   > (and CH1/2 for parity with downstream HAL).

   即耳机通路用的是 **ADDA 的 CH3/CH4**，不是 CH1/CH2。

3. **`mic/xaga-mic-switch.c`** —— 一个 ALSA mixer 守护进程：
   - 监听 `/dev/input/event3` 的 `SW_MICROPHONE_INSERT`（`SW_` bit 4）
   - 插上带麦耳机 → 把 `PGA_L_Mux` 切到 **AIN1**
   - 拔出 → 切回 **AIN0**（内置麦）
   - README 解释原因：**内置麦和 3.5mm 耳机麦接在同一条 pipe 上，不能同时录立体声**

   这对 HakiOS 是直接的「行为规格」：耳机检测事件 → PGA_L_Mux 在 AIN0/AIN1 之间切换。

---

## 5. Mali-G610 / panthor

### 5.1 主线 panthor 对 G610 的支持

**结论：主线 panthor 能驱动 G610，但只到「能绑上、能跑 CSF」这一层，没有 MTK 的电源/IPI 集成。**

证据：
- 主线 `drivers/gpu/drm/panthor/panthor_drv.c` 的 of_match 里有
  `{ .compatible = "arm,mali-valhall-csf" }`（第 1883 行）
- 主线 binding `Documentation/devicetree/bindings/gpu/arm,mali-valhall-csf.yaml` 已收录
  `mediatek,mt8196-mali`（第 186 行）——说明 **MediaTek + Valhall CSF 组合主线是认的**
- 主线 `panthor_gpu.c` 第 76 行：`if (GPU_ARCH_MAJOR(ptdev->gpu_info.gpu_id) < 11)`
  → 主线按 GPU_ID 的 **arch major** 做分支，G610 是 arch 10.x，走的正是这条 <11 的路径
- 主线 `panthor_devfreq.c` 存在（HTTP 200），但 **grep `gpueb` / `mediatek` 命中 0 次**
  → **主线 panthor 完全没有 MTK 专用电源管理**

（`[未确认]`：没有找到主线明确「G610 supported」的文档声明；上述结论是从代码路径推的。）

### 5.2 为什么在 MT6895 上要额外做事

下游 dtsi 的注释把问题说得很清楚（原文）：

> The MT6895 GPU MTCMOS is split into **MFG1..MFG12**. Downstream attaches all
> twelve domains to the GPU node and powers them through PM runtime; the PDCv2
> registers then let the GPU control the finer shader/stack domains.
> **Without MFG2-12 the shader cores never become ready (`shader_ready` stays 0)
> and the MCU firmware hangs before writing the shared interface version.**

**这条极其关键** —— 它就是「MCU 收到 AUTO 后不执行」的硬件级解释：
shader 域没上电，`shader_ready` 一直是 0，MCU 固件在写 shared interface version 之前就挂了。

另外注释还提到兼容性问题：

> Stock DT compatible is `"mediatek,mali","arm,mali-valhall"` which does **NOT**
> match any mainline driver; panthor binds `"arm,mali-valhall-csf"` (G610 is arch 10 / CSF).
> The stock node has 5 IRQs (GPU/MMU/JOB/EVENT/PWR, upper-case) but panthor does
> `platform_get_irq_byname()` with lowercase `"gpu"/"mmu"/"job"`, so names are re-cased here.

→ **厂商 DT 的 compatible 和中断名都不能直接用**，必须改写成
`arm,mali-valhall-csf` + 小写 `job`/`mmu`/`gpu`。

### 5.3 GPUEB —— MTK 独有的 GPU 电源/频率控制器

**GPUEB（GPU Embedded Block）是 MT6895 上独立的一个 MCU**，通过 mailbox + IPI 控制
GPU 的 PLL、buck、VSRAM 时序。Linux 侧不能直接写 CCF/regulator。

下游提交 `4e2c2ae8a1ddcd79019444c03d03b44771cb29cc`
（作者 **Hecheng Yu <kde-yyds@qq.com>**，2026-08-29）：

> `drm/panthor: xaga: GPUEB-backed devfreq, stacks clock, VSTACK rail`

改动 3 个文件 +270 行：
- `arch/arm64/boot/dts/mediatek/mt6895.dtsi` (+17/-13)
- `drivers/gpu/drm/panthor/panthor_devfreq.c` (+265)
- `drivers/mfd/mtk-spmi-pmic.c` (+1)

**它做了什么**（commit message 原文要点）：

1. **把 shader complex 的 `stacks` 时钟和 core 时钟一起 scale**，
   在 DT 里挂上 `stacks` 时钟 + `VSTACK` rail（`mt6368_vbuck2`）+ 每档电压
2. **GPUEB 可用时，devfreq 通过 IPI 提交「工作表的 OPP index」**，
   而不是本地驱动 CCF/regulator；EB 自己应用两个 PLL、两个 buck、两个 VSRAM rail
   和它自己的时序。**工作表是从真机 live 抓的**，commit 任何一步失败会自动回退到本地路径
3. 恢复 `mtk-spmi-pmic` 里 mt6368-regulator MFD cell 的 `of_compatible`，
   让 DT 里写的 rail 约束能重新 parse
4. 适配 7.2 的 panthor devfreq API：GPUEB 掌管 DVFS 时，频率记在私有 devfreq 结构里，
   通过 `get_cur_freq` 上报

#### 从该 commit 提取的 GPU / STACK 工作表（**真机实测值**）

**GPU 频率档（SegmentID 0，6 档）**：
```
950 MHz, 880 MHz, 800 MHz, 610 MHz, 430 MHz, 350 MHz
```

**STACK（shader complex）频率档（SegmentID 2，40 档，从高到低）**：
```
852 845 837 830 822 815 807 800 773 747
721 695 668 642 616 590 572 555 538 521
504 487 470 453 436 419 402 385 368 351
334 317 304 292 280 268 255 243 231 219   (MHz)
```

**STACK 的 rate/voltage 对照表（43 项，来自下游 `gpufreq_mt6895.h` 的 `g_default_stack[]`）**：

| rate (MHz) | volt (µV) | | rate (MHz) | volt (µV) |
|---|---|---|---|---|
| 219 | 500000 | | 590 | 650000 |
| 231 | 506250 | | 616 | 656250 |
| 243 | 512500 | | 642 | 662500 |
| 255 | 518750 | | 668 | 668750 |
| 268 | 525000 | | 695 | 675000 |
| 280 | 531250 | | 721 | 681250 |
| 292 | 537500 | | 747 | 687500 |
| 304 | 543750 | | 773 | 693750 |
| 317 | 550000 | | 800 | 700000 |
| 334 | 556250 | | 807 | 706250 |
| 351 | 562500 | | 815 | 712500 |
| 368 | 568750 | | 822 | 718750 |
| 385 | 575000 | | 830 | 725000 |
| 402 | 581250 | | 837 | 731250 |
| 419 | 587500 | | 845 | 737500 |
| 436 | 593750 | | 852 | 743750 |
| 453 | 600000 | | 860 | 750000 |
| 470 | 606250 | | | |
| 487 | 612500 | | | |
| 504 | 618750 | | | |
| 521 | 625000 | | | |
| 538 | 631250 | | | |
| 555 | 637500 | | | |
| 572 | 643750 | | | |

**规律**：电压以 **6250 µV (6.25 mV) 为步进**线性递增；368 MHz→568.75 mV，
852 MHz→743.75 mV。下游的做法是**升压先于升频、降压后于降频**。

**GPU 的 DT OPP 表（下游 dtsi，`gpu_opp_table`）**：
从 950 MHz 一路到 800 MHz 每档都是 `opp-microvolt = <750000>`（750 mV）——
注意这张表里 OPP 电压**并不区分档位**，真正 per-rate 的电压在 GPUEB 的工作表里。
这解释了为什么必须走 GPUEB 而不是 DT OPP。

**IPI 协议常量**（从 commit 里提取）：
```c
#define XAGA_EB_TARGET_GPU    1   /* gpufreq_ipi.h TARGET_GPU   */
#define XAGA_EB_TARGET_STACK  2   /* gpufreq_ipi.h TARGET_STACK */
```

**提交时序（这个顺序很重要，commit 原文）**：

```c
/* Mirror downstream ordering: the core domain leads on the way up
 * and trails on the way down.
 */
if (up)
    mt6895_gpueb_commit(XAGA_EB_TARGET_GPU,   gidx);   /* 升频：先 GPU core */
else
    mt6895_gpueb_commit(XAGA_EB_TARGET_STACK, sidx);   /* 降频：先 STACK */

/* 再补另一个 domain */
mt6895_gpueb_commit(up ? XAGA_EB_TARGET_STACK : XAGA_EB_TARGET_GPU, ...);
```

即：**升频时 core 先行、stacks 跟上；降频时 stacks 先降、core 后降**。
任何一步失败 → `xaga_eb_failed = true`，整个 EB 路径永久禁用，回退到本地 CCF + regulator。

本地路径（EB 不可用时）也是同样的策略：
```c
/* Voltage leads on the way up and trails on the way down.
 * If the rail refuses to move up, refuse to raise the clock with it.
 */
if (up && regulator_set_voltage(vstack, uv, uv))
    /* 拒绝升频 —— 电压上不去就不升频 */
else {
    clk_set_rate(stacks, stack_rate);
    if (!up) regulator_set_voltage(vstack, uv, uv);   /* 降频后再降压 */
}
```

**另一条重要观察**（commit 注释）：

> XAGA: with the GPUEB owning the clocks, `clk_get_rate()` returns a **stale CCF cache**;
> the tracked value is authoritative for both paths.

→ 一旦 GPUEB 接管，**读 CCF 得到的频率是错的**，必须自己记账。
如果我们的移植在 GPUEB 跑起来后还去读时钟寄存器判断当前频率，会读到脏值。

**索引映射规则**：
- `xaga_eb_gpu_idx(freq)`：在 `xaga_eb_gpu_freq[]`（降序）里找**第一个 `freq >= table[i]`**，
  找不到就返回最后一档索引
- `xaga_eb_stack_idx(freq)`：同上，但先 `min(freq, stack_max_rate)` 截断
- `stack_max_rate` 默认 **852 MHz**（可通过 `module_param` 改），
  因为该 unit 的 STACK 工作表最高就是 852 MHz（SegmentID 2）

**GPUEB 的 DT 节点**（下游 dtsi）：
```dts
reg-names = "gpueb_base", "mbox0_base", "mbox0_set", "mbox0_clr",
            "mbox0_send", "mbox0_recv";
interrupts = <GIC_SPI 516 IRQ_TYPE_LEVEL_HIGH 0>;
interrupt-names = "mbox0";
gpueb_mem_table = <0 0x1000>,      /* 4 KB   MEM_ID_GPUFREQ */
                  <1 0x180000>;    /* 1.5 MB MEM_ID_LOG     */
```

GPUEB 驱动文件（下游）：
```
drivers/gpu/mediatek/gpueb/gpueb_init.c
                              /gpueb_ipi.c
                              /gpueb_power.c
                              /gpueb_gpumpu.c
                              /gpueb_logger.c
                              /gpueb_plat_service.c
                              /gpueb_debug.c
                              /gpueb_hwvoter_dbg.c
```

### 5.4 TILER_OOM 问题

下游提交 `be527d1f925e47dd090fd81d1592017c97ebd578`
（Hecheng Yu，2026-08-31）：`drm/panthor: xaga: fix TILER_OOM detection and grow heap past max_chunks`

**问题**（commit message 原文）：

> On MT6895 the CSF firmware can raise `CS_TILER_OOM` with the CS ACK field
> **mirroring REQ** (left behind by a CSG suspend/resume cycle). panthor's event
> detection used `(req ^ ack) & CS_EVT_MASK`, which **misses the event when REQ == ACK**,
> so the heap request was never serviced and the tiler stayed blocked on RESOURCE
> until the 5s job timeout.

**修复**：
1. 直接从 CS input 的 REQ 位检测 TILER_OOM（不走 `req ^ ack`），
   和 kbase（ARM 闭源驱动）做法一致
2. 让 tiler heap 能超过 `max_chunks` 增长（重 tiling 场景需要），
   但仍受 target-in-flight 和内存上限约束

**对我们有用**：这是 panthor 在 MT6895 上一个**真实踩过的坑**，
`REQ == ACK` 的检测缺陷是通用的（不只 xaga），值得记下。

### 5.5 其它相关下游提交

```
39644dd  drm/panthor: allow 60s for jobs on G610        （2026-11-06）
```
→ G610 上 job 超时要从默认值放宽到 60 秒。

---

## 6. MT6895-Mainline 组织（xaga 的完整 mainline 移植）

### 6.1 三个仓库

| 仓库 | 内容 | 探测 |
|---|---|---|
| `https://github.com/MT6895-Mainline/linux` | Linux 7.2 分支，分支名 `7.2-mt6895-xiaomi-xaga` | clone 成功 |
| `https://github.com/MT6895-Mainline/initramfs` | initramfs，分支 `xaga-mt6895` | README 已读 |
| `https://github.com/MT6895-Mainline/quirks` | UCM + mic-switch | clone 成功 |

**initramfs 仓库给出的分区号**（README 原文默认值，很有用）：

| 分区 | 设备节点 | 用途 |
|---|---|---|
| `BOOT_PARTITION` | `/dev/sdc86` | 在 xaga 上是 **userdata** |
| `NVDATA_PARTITION` | `/dev/sdc13` | 在 xaga 上是 **nvdata**，init 会挂上去拷 WiFi 配置 |

构建产物：`root/init` + `initramfs.cpio.lz4`（lz4 压缩）。

**与 Alexander Letuchiy 的关系**：`[未确认]` 没有找到任何直接证据表明二者是同一人或同一团队。
但可疑点：Letuchiy 的 pinctrl cover letter 引用的小米源码分支是 `xaga-s-oss`，
而下游树的主要作者是 **Hecheng Yu (kde-yyds)**、**yefxx**、**mytiantian001**。
两人邮箱不同（`asalde.le1@gmail.com` vs `kde-yyds@qq.com`）。
倾向判断：**不同团队，做的是同一台机器的两条独立路线**（Letuchiy 走上游、MT6895-Mainline 走实用）。

### 6.2 下游树的版本与规模

- `Makefile`: `VERSION=7 / PATCHLEVEL=2 / SUBLEVEL=0`，`NAME = Baby Opossum Posse`
- 分支：`7.2-mt6895-xiaomi-xaga`（HEAD）
- HEAD：`25fd36e67958d96086b8542340477948ab40c2a2`，
  `arm64: dts: mediatek: xaga: add i2c3 and the NXP SN110T NFC controller`（2026-10-30）
- 最新提交时间跨度：2026-10-21 ~ 2026-11-09（**非常活跃**）

### 6.3 树里有哪些 MT6895 文件

**DT**：
```
arch/arm64/boot/dts/mediatek/mt6895.dtsi              7254 行
arch/arm64/boot/dts/mediatek/mt6895-xiaomi-xaga.dts   1554 行
arch/arm64/boot/dts/mediatek/mt6895-xaga-usb-tcpm.dtsi
arch/arm64/boot/dts/mediatek/mt6895-pinfunc.h
```

**驱动**（部分）：
```
drivers/clk/mediatek/clk-mt6895*.c               （11 个文件）
drivers/pinctrl/mediatek/pinctrl-mt6895.c
drivers/iommu/mtk_iommu_mt6895.c / .h
drivers/interconnect/mediatek/mmqos-mt6895.c
drivers/gpu/drm/mediatek/mml/mtk-mml-tp-mt6895.c
drivers/misc/mediatek/cmdq/mailbox/cmdq-platform-mt6895.c
drivers/media/platform/mtk-isp/camsys/isp7_1/cam/mtk_cam-plat-mt6895.c
drivers/regulator/mt6363-mt6895.c
drivers/regulator/mt6368-regulator.c
sound/soc/codecs/mt6368.c / mt6368-accdet.c
sound/soc/mediatek/mt6895/mt6895-mt6368.c
drivers/gpu/mediatek/gpueb/*                     （8 个文件）
drivers/misc/mediatek/connectivity/...           （大量，MT6895 专用头文件）
```

**DT binding 文档**：
```
Documentation/devicetree/bindings/media/mediatek,mt6895-vcodec-dec.yaml
Documentation/devicetree/bindings/media/mediatek,mt6895-vcodec-enc.yaml
Documentation/devicetree/bindings/media/mediatek,mt6895-vdec-dma.yaml
Documentation/devicetree/bindings/remoteproc/mediatek,mt6895-vcp.yaml
Documentation/admin-guide/media/mt6895-vcp.rst
```

### 6.4 下游树覆盖到什么程度（从 commit 历史看）

**已工作**（有对应提交）：
- 显示：完整 DRM（IOMMU、SMI、CMDQ mbox/GCE、mediatek_v2 DRM、panel ktz8863a、
  AFBC 32x8 扫描输出、OVL plane、带宽投票）
- 音频：ASoC + MT6368 codec + accdet + UCM + mic-switch
- 相机：ISP7_1、imgsensor（OV02B10、GC02M1）、CCD
- 视频：VCP 编解码（dec/enc）
- 存储：UFS
- 网络：WiFi/BT/consys
- 充电：bq28z610 电量计 + charger（无快充）
- 触控：novatek NVT-ts-spi / FPC / Goodix
- 振动：aw8697
- 闪光灯：Kinetic KTD2687
- NFC：NXP SN110T（i2c3）
- GPU：panthor + GPUEB

**明确已知问题**：
- `512306e arm64: configs: xaga: Disable suspend as not working yet`（2026-10-29）
  → **suspend 还不能用**

### 6.5 bootloader 的处理方式（重要，会影响我们的刷机流程）

下游提交 `1f09fe405f9e467697e60888f342cd0af71d75cf`
（Hecheng Yu，2026-08-29）：`arm64: xaga: embed minimal board DTB and override LK`

改动 `arch/arm64/kernel/Makefile` (+12) 和 `arch/arm64/kernel/setup.c` (+17)：

> **XAGA: embed the board DTB into the kernel image so setup.c can override
> LK's FDT with our own device tree. Keep it in `.rodata` (not the
> `.dtb.init.rodata` section) so `initial_boot_params` stays valid after boot.**

```
$(srctree)/arch/arm64/boot/dts/mediatek/mt6895-xiaomi-xaga.dtb: FORCE
	$(Q)$(MAKE) -C $(srctree) ARCH=arm64 LLVM=1 mediatek/mt6895-xiaomi-xaga.dtb
```

**为什么这么做**（postmarketOS wiki 的说明，经用户转述 + dts 注释印证）：

> MediaTek 的 LK 自己依赖 vendor_boot 里的 dtb 和 dtbo 分区，
> 直接把那儿的 dtb 换成从 mainline 编的**会让 LK 自己崩**。
> 所以做法是：把自定义 DTB 链进 kernel image，启动后覆盖 LK 传进来的 FDT。

xaga dts 的 `chosen` 节点注释也印证了这一点：

> XAGA cmdline: `setup.c` re-reads `/chosen/bootargs` from **THIS** embedded DTB
> (`early_init_dt_scan_chosen`) because LK passes its own baked-in cmdline
> (`setup_machine_fdt` captured it before the DTB override) and ignores
> boot.img / vendor_boot cmdlines.

**对 HakiOS 的直接启示**：如果我们也要用自定义 DTB，不能改 dtbo 分区，
要在 kernel 里覆盖 FDT（XNU 里对应 `PE_state.deviceTreeHead` 的处理时机）。

### 6.6 Letuchiy patch 与下游树的关系（一致性核对）

见 §3.8 的表格。结论：**两者在 SoC 内存映射上高度一致**（10 项里 9 项吻合），
说明 Letuchiy 的 patch 很可能也参考了同一份小米源码（他在 pinctrl cover 里
明确引用了 `MiCode/Xiaomi_Kernel_OpenSource` 的 `xaga-s-oss` 分支）。

差异：
- Letuchiy 的 SoC DT 只有 **461 行**（最小可启动），下游是 **7254 行**（全功能）
- Letuchiy **不含** GPU、显示、音频、相机、SPMI/PMIC
- Letuchiy 有 systimer（`0x1c011000`），下游没启用
- Letuchiy 的 watchdog **不带** mt6589 fallback，下游带

---

## 7. 对 HakiOS 的具体可用点

### 7.1 直接可用的硬件事实（高可信，双来源验证）

1. **GIC**：dist `0x0c000000`（256 KB）+ redists `0x0c040000`（2 MB），
   `#interrupt-cells = <4>`，PPI 按 cluster 分两组
   （cpu0-3 / cpu4-7）——**XNU 的 `arm_gic` 初始化必须支持 4 cell 解析和 partition**
2. **架构定时器**：PPI 13/14/11/10，**13 MHz**，LEVEL_HIGH
3. **UART0** `0x11001000`，**SPI 141**，**921600 baud** —— 这是我们的调试控制台，
   和 Letuchiy 实测一致
4. **watchdog** `0x1c007000`（`0x100` 字节）
5. **SPMI** `0x1c804000`（pmif，`0x900`）+ `0x1c801000`（spmimst，`0x100`），
   **SPI 414**（pmif）+ **GPIO 243**（rcs）；MT6363 在 **usid 4**，MT6368 在 **usid 5**
6. **GPU** `0x13000000`（`0x480000`），中断 **SPI 512/513/514**（gpu/mmu/job）
7. **GPUEB** mailbox，**SPI 516**，shared mem：`MEM_ID_GPUFREQ` 4 KB + `MEM_ID_LOG` 1.5 MB

### 7.2 对「MCU 收到 AUTO 后不执行」的直接答案

**最可能的原因（下游树原话）**：
> Without MFG2-12 the shader cores never become ready (`shader_ready` stays 0)
> and the MCU firmware hangs before writing the shared interface version.

即：**GPU 的 MTCMOS 被切成 MFG1..MFG12 共 12 个域，必须全部上电**（不只是 MFG1），
shader core 才会 ready。厂商内核靠 PM runtime 逐个上电 + PDCv2 寄存器控制
更细的 shader/stack 域。

**行动建议**：
1. 在 XNU 侧确认 GPU 上电序列是否覆盖 MFG1-MFG12 全部 12 个域
2. 检查 `shader_ready`（GPU 寄存器）是否变 1 —— 这是判断「卡在哪一步」的关键探针
3. 如果 shader_ready 一直是 0，问题在电源域，不在 MCU 通信
4. 参考下游的 `drivers/gpu/mediatek/gpueb/gpueb_power.c` 的时序

### 7.3 可以直接抄的数值表

| 表 | 位置（下游） | 内容 |
|---|---|---|
| GPU 频率档 | `panthor_devfreq.c` 的 `xaga_eb_gpu_freq[]` | 6 档：950/880/800/610/430/350 MHz |
| STACK 频率档 | 同上 `xaga_eb_stack_freq[]` | 40 档：852…219 MHz |
| STACK 电压表 | 同上 `xaga_stack_table[]` | 43 组 rate/volt，步进 6.25 mV |
| MT6368 耳机阈值 | SoC dtsi `accdet` 节点 | 三键/四键 ADC 阈值 |
| accdet 寄存器序列 | 同上 `headset-mode-setting` | 15 个 32-bit 值 |
| GPIO 数量 | pinctrl 节点 | 227 |

### 7.4 需要自己验证的（未确认）

- **系统定时器 `0x1c011000` / SPI 280**：只在 Letuchiy 的 patch 里，下游树没启用。
  如果 XNU 用架构定时器，可以不管；但如果要拿 systimer 做 broadcast，需要自己验
- **TF-A 的 40 秒 CPU_SUSPEND 封锁**：移植时要处理，否则开机会有一段不能 idle
- **DSU PMU 掉电丢计数**：如果要性能计数器，需要 cluster 常上电

### 7.5 许可与合规提示

- Letuchiy 的 patch 是 **GPL-2.0**（内核），pinctrl 表源自小米开源内核
  （`MiCode/Xiaomi_Kernel_OpenSource`，GPL）
- 下游树的 `mt6368.c` / `mt6368-accdet.c` 等如果是从厂商 .c 改写而来，
  同样受 GPL 约束。XNU 是 APSL，**跨许可直接抄代码有风险**；
  建议只提取**寄存器地址、位域、数值表**这类事实性信息（事实不受版权保护），
  不复制代码结构

---

## 8. 所有引用的 URL

### 8.1 邮件列表（Letuchiy 的 patch）

| 标题 | URL |
|---|---|
| `[PATCH v2 0/9] arm64: mediatek: Add initial MT6895 and Xiaomi Poco X4 GT support`（cover，MARC raw） | https://marc.info/?i=20261113-initial-mt6895-v2-0-4ab0aa9fa391%40gmail.com&q=raw |
| `[PATCH v2 8/9] arm64: dts: mediatek: Add MT6895 SoC`（全文，Openwall） | https://lists.openwall.net/linux-kernel/2026/11/13/2592 |
| `[PATCH v2 7/9] arm64: dts: mediatek: Add MT6895 pinmux macro header file`（MARC） | https://marc.info/?m=179460309385122 |
| `[PATCH 0/5] arm64: mediatek: Add initial MT6895 and Xiaomi Poco X4 GT support`（v1 cover，MARC raw） | https://marc.info/?l=linux-kernel&m=179391301503224&q=raw |
| `[PATCH 3/5] dt-bindings: watchdog: mediatek,mtk-wdt: Add compatible for MT6895`（v1，Openwall） | https://lists.openwall.net/linux-kernel/2026/11/05/3943 |
| `[PATCH 0/2] pinctrl: mediatek: Add MT6895 support`（cover，MARC raw） | https://marc.info/?l=linux-gpio&m=179416229778104&q=raw |
| Openwall 2026-11-13 linux-kernel 归档索引 | https://lists.openwall.net/linux-kernel/2026/11/13/ |
| v1 → v2 的 msgid 链接 | https://patch.msgid.link/20261105210622.523747-1-asalde.le1@gmail.com |
| v2 → pinctrl 依赖的 msgid 链接 | https://lore.kernel.org/r/20261108-pinctrl-mt6895-v1-0-f704c443b31a@gmail.com |

### 8.2 主线内核（torvalds/linux）

| 标题 | URL |
|---|---|
| torvalds/linux master 根 | https://github.com/torvalds/linux |
| mediakek DTS Makefile（用于确认无 mt6895） | https://raw.githubusercontent.com/torvalds/linux/master/arch/arm64/boot/dts/mediatek/Makefile |
| `panthor_drv.c`（含 `arm,mali-valhall-csf` match） | https://raw.githubusercontent.com/torvalds/linux/master/drivers/gpu/drm/panthor/panthor_drv.c |
| `panthor_gpu.c`（arch major <11 分支） | https://raw.githubusercontent.com/torvalds/linux/master/drivers/gpu/drm/panthor/panthor_gpu.c |
| `panthor_devfreq.c`（无 gpueb） | https://raw.githubusercontent.com/torvalds/linux/master/drivers/gpu/drm/panthor/panthor_devfreq.c |
| `arm,mali-valhall-csf.yaml`（含 `mediatek,mt8196-mali`） | https://raw.githubusercontent.com/torvalds/linux/master/Documentation/devicetree/bindings/gpu/arm,mali-valhall-csf.yaml |
| `mt6363-regulator.c`（MT6363 主线存在） | https://raw.githubusercontent.com/torvalds/linux/master/drivers/regulator/mt6363-regulator.c |
| `mt8195.dtsi`（对照实验） | https://raw.githubusercontent.com/torvalds/linux/master/arch/arm64/boot/dts/mediatek/mt8195.dtsi |

### 8.3 MT6895-Mainline（下游树）

| 标题 | URL |
|---|---|
| `MT6895-Mainline/linux` 仓库 | https://github.com/MT6895-Mainline/linux |
| GPUEB devfreq 提交 `4e2c2ae8a1ddcd79019444c03d03b44771cb29cc` | https://github.com/MT6895-Mainline/linux/commit/4e2c2ae8a1ddcd79019444c03d03b44771cb29cc |
| TILER_OOM 修复 `be527d1f925e47dd090fd81d1592017c97ebd578` | https://github.com/MT6895-Mainline/linux/commit/be527d1f925e47dd090fd81d1592017c97ebd578 |
| embed DTB override LK `1f09fe405f9e467697e60888f342cd0af71d75cf` | https://github.com/MT6895-Mainline/linux/commit/1f09fe405f9e467697e60888f342cd0af71d75cf |
| `MT6895-Mainline/initramfs` 仓库 | https://github.com/MT6895-Mainline/initramfs |
| `MT6895-Mainline/quirks` 仓库 | https://github.com/MT6895-Mainline/quirks |

### 8.4 其它

| 标题 | URL |
|---|---|
| Xiaomi 官方内核开源（`xaga-s-oss` 分支，Letuchiy 引用） | https://github.com/MiCode/Xiaomi_Kernel_OpenSource/tree/56ec519f6 |
| 第三方 Xiaomi MT6895 内核镜像 | https://github.com/15yangyyyyy/android_kernel_xiaomi_mt6895 |

---

## 附录 A：本次调研的方法与坑

| 尝试 | 结果 |
|---|---|
| `lore.kernel.org/all/<msgid>/raw`（无 UA） | **403 Forbidden**（nginx） |
| `lore.kernel.org/all/<msgid>/raw`（带浏览器 UA） | 200 但是 **Anubis 反爬页**（"Making sure you're not a bot!"），非正文 |
| `marc.info/?i=<msgid>&q=raw` | ✅ **可用**，纯文本 |
| `marc.info/?l=linux-kernel&w=2&r=1&s=mt6895&q=b`（搜索） | ❌ 无结果（搜索接口不灵） |
| `marc.info/?i=<msgid>&q=raw` 会列出该 msgid 的**所有交叉投递** | ✅ 有用，能发现投到了哪些 list |
| `lists.openwall.net/linux-kernel/<yyyy>/<mm>/<dd>/<n>` | ✅ **可用**，全文 HTML |
| `lists.openwall.net/linux-kernel/<yyyy>/<mm>/<dd>/`（索引） | ✅ 可用，能 grep 出标题 |
| `patchwork.kernel.org/api/1.2/patches/?q=mt6895` | ❌ 返回非 JSON（未深究） |
| `api.github.com/repos/...` | ❌ **rate limit**（未认证） |
| `raw.githubusercontent.com/...` | ✅ 可用（但批量请求会被限流，见下） |
| `github.com/<repo>/commit/<hash>.patch` | ✅ **可用**，直接拿完整 patch |
| `git clone --filter=blob:none --no-checkout --depth 50` | ✅ 可用，50 个 commit + 全部文件树 |
| `git fetch --deepen=2000` 对 shallow clone | ⚠️ 返回 0 但 commit 数没变（仍然是 50） |

**经验**：查 kernel patch 全文，**优先 marc.info 的 `?i=<msgid>&q=raw`**，
备选 **lists.openwall.net**。`lore.kernel.org` 的机器访问基本被反爬挡死。
查 git 仓库里的文件内容，**优先 clone（blobless）而不是 raw.githubusercontent**，
后者批量请求会 429。

---

## 附录 B：待办 / 未完成项

已在本轮补齐：
- ~~`MT6895-Mainline/initramfs`~~ → 已看 README（§6.1，拿到分区号）
- ~~v2 patch 8 的 CPU idle states 参数~~ → 已提取（§3.3，4 个 state 的 PSCI param）
- ~~`4e2c2ae` patch 后半段（IPI 提交逻辑）~~ → 已读（§5.3，提交时序 + 索引映射）

仍未完成：
1. **Letuchiy patch 9/9（board dts，87 行）未单独抓** —— 只从 cover letter 知道规模
2. **linux-next 是否已含 Letuchiy 的 patch** —— 未查
3. **未确认** MT6895-Mainline 与 Letuchiy 的人际关系
4. **未查** 是否有其它 MT6895 机型（如 Redmi K50i）的独立 dts
5. **未读** `gpueb_power.c` / `gpueb_ipi.c` 的实现细节（只有文件名和 DT 节点）
6. **未验证** MT6368 的 register 定义（`include/linux/mfd/mt6368/registers.h`）具体内容
7. 邮件列表上 **v2 之后（2026-11-14 起）是否出现 review 回复** —— 本报告截止时无

---

## 附录 C：给 HakiOS 的三条最短路径建议

按「投入产出比」排序：

### C1. 先解决 GPU 电源域（对应「MCU 不执行」）

**不要**先去调 MCU 通信协议。先确认：
1. MFG1-MFG12 十二个 MTCMOS 域是否全部上电
2. GPU 的 `shader_ready` 位是否变 1

下游 dtsi 的注释已经把因果关系写死了：
> Without MFG2-12 the shader cores never become ready (`shader_ready` stays 0)
> and the MCU firmware hangs **before writing the shared interface version**.

参考文件（下游树）：
- `arch/arm64/boot/dts/mediatek/mt6895.dtsi` 的 gpu 节点注释（约 2729-2768 行）
- `drivers/gpu/mediatek/gpueb/gpueb_power.c`

### C2. 用 Letuchiy 的 v2 做「最小可启动」基线

他的 v2 只覆盖 CPU/GIC/timer/watchdog/pinctrl/keypad/UART，**正好是裸机 bring-up 需要的全部**，
而且是**双来源验证过**的（§3.8：10 项地址 9 项与下游树吻合）。
适合拿来对照 XNU 启动早期的硬件假设。

**注意**：他的 pinctrl 表（2717 行）是从小米开源内核逐字段核对 + 真机 dump 验证的，
是**寄存器级最可信的上游材料**。

### C3. 音频先照 UCM 的「行为规格」实现，别照代码

quirks 仓库的 UCM 给了完整的行为定义（§4.4）：
- 三条独立 PCM（3.5mm / mic / 扬声器）
- 耳机走 **ADDA CH3/CH4**（不是 CH1/CH2）
- `PGA_L_Mux` 在 **AIN0（内置麦）/ AIN1（耳机麦）** 之间切换，由 accdet 的
  `SW_MICROPHONE_INSERT` 事件驱动

这些是**接口契约**，按它实现 XNU 侧的 audio 就能和现有 UCM 对上。
