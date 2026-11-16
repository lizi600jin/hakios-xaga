# xaga 真实 CPU 调频结构（实测）

> 2026-11-16 20:32，DSH。**从运行中的原厂 Android 直接测得。未改任何东西。**

---

## 🔴 结论：`hakios.cpufreq.c` 整体套错了架构

**它用的是 Qualcomm EPSS 布局，xaga 是 MediaTek。**

| 项 | `hakios.cpufreq.c` 写的 | **xaga 实测** |
|---|---|---|
| **IP** | **Qualcomm EPSS**（`qcom-cpufreq-hw.c`） | **`mediatek,cpufreq-hw`** |
| **基址** | **`0x18591000`** | **`0x0011bc10`** |
| 域大小 | `0x1000` × 3 | **`0x120`** × 3 |
| 频率编码 | **19.2 MHz 的整数倍**（`[7:0]`） | **OPP 表 + `mtk-cpufreq-hw` 驱动** |
| 驱动模块 | — | **`mediatek_cpufreq_hw`** |

**新增实测：`0x18591000` 在 xaga 的任何 xaga 驱动里都搜不到。**

---

## 1. 三个性能域的真实地址

**从 `/sys/firmware/devicetree/base/soc/performance-controller@0011bc00/reg` 直接读出：**

| 域 | 基址 | 大小 | reg-name |
|---|---|---|---|
| **performance-domain0** | **`0x0011bc10`** | `0x120` | `performance-domain0` |
| **performance-domain1** | **`0x0011bd30`** | `0x120` | `performance-domain1` |
| **performance-domain2** | **`0x0011be50`** | `0x120` | `performance-domain2` |

**nodename `performance-controller@0011bc00` 里的 `11bc00` 是节点基址，
第一域从 `+0x10` 开始（即 `0x11bc10`）。**

---

## 2. 节点属性

**路径**：`/sys/firmware/devicetree/base/soc/performance-controller@0011bc00`

| 属性 | 值 |
|---|---|
| **`compatible`** | **`mediatek,cpufreq-hw`** |
| `reg` | 三组 `(基址, 0x120)` |
| `reg-names` | `performance-domain0` / `1` / `2` |
| `#performance-domain-cells` | 有 |

**sysfs 设备**：`/sys/devices/platform/soc/11bc10.performance-controller`
**驱动模块**：`mediatek_cpufreq_hw`

---

## 3. 三个 policy 与簇的对应

| policy | 簇 | 当前频率 | 可用范围 |
|---|---|---|---|
| **policy0** | **A55 × 4** | **450 MHz** | 450 MHz – **2.00 GHz** |
| **policy4** | **A78 × 3** | **800 MHz** | 800 MHz – **2.85 GHz** |
| **policy7** | **A78 × 1（超大核）** | **500 MHz** | 500 MHz – **2.85 GHz** |

**governor**：三个都是 `sugov_ext`（schedutil 变体）

**与 `board.h` 记录的拓扑一致**：
`cpu@000-003` 是 A55（`reg=0x00/0x100/0x200/0x300`），
`cpu@100/101/102/200` 是 A78（`reg=0x400/0x500/0x600/0x700`）。

---

## 4. OPP 表位置

**`/proc/device-tree/opp-table0/`** 含 `opp00` … `opp07` 等条目，
`compatible = "operating-points-v2"`。

**这是标准 OPP v2 表，由 `mediatek,cpufreq-hw` 驱动解析。**

---

## 5. 重写 `hakios.cpufreq.c` 需要什么

### 5.1 必须换掉的

| 项 | 现值（redwood/Qualcomm） | 需改为（xaga/MediaTek） |
|---|---|---|
| **基址** | `0x18591000` | **`0x0011bc10`** |
| **域大小** | `0x1000` | **`0x120`** |
| **域数** | 3 | **3**（数量对得上） |
| 频率表偏移 | `0x100`（40 行 × 4 B） | **需查 MediaTek 布局** |
| perf_state 偏移 | `0x320` | **需查 MediaTek 布局** |
| 频率编码 | `[7:0]` × 19.2 MHz | **需查 MediaTek 编码** |

### 5.2 🔴 我还不知道的（关键缺口）

**MediaTek `cpufreq-hw` 的寄存器布局我手上没有。**

**不能照搬 Qualcomm 的 `0x100`/`0x320`** —— 那是 EPSS 的布局。

**需要从以下途径之一取得：**
1. **MediaTek 内核源码**（`drivers/cpufreq/mediatek-cpufreq-hw.c`）
2. **原厂内核模块** `mediatek_cpufreq_hw.ko` 反汇编
3. **设备树里 OPP 表与寄存器的对应关系**
4. **XagaForge 的 MT6895 内核源码**（`board.h` 里引的那个 commit）

**这是下一步必须先解决的。**

---

## 6. 我建议的下一步

**不要直接改 `hakios.cpufreq.c`** —— 缺寄存器布局，改了也是猜。

**先取得 MediaTek cpufreq-hw 的寄存器布局**，优先级：

| # | 途径 | 可行性 |
|---|---|---|
| **1** | **原厂 `mediatek_cpufreq_hw.ko` 反汇编** | **最可靠**（就是这台机器上跑的） |
| 2 | XagaForge 的 MT6895 源码 | 需要有这个 commit |
| 3 | 上游 Linux `mediatek-cpufreq-hw.c` | 参考价值，但要确认版本 |

**取得布局后**，再决定是改 `hakios.cpufreq.c` 还是新写一个 `hakios.mt6895_cpufreq.c`
（**倾向新写** —— 现有那个是 Qualcomm 的，改比重写更乱）。

---

## 7. 边界

**本文全部数据来自运行中的原厂 Android 只读查询**
（`/sys/devices/system/cpu/cpufreq/`、`/sys/firmware/devicetree/base/`、`/proc/device-tree/`）。

**没有反汇编、没有读 MediaTek 源码、没有改任何文件、没有碰 XNU。**

**「需查 MediaTek 布局」的部分是本轮的真实缺口，不是已知信息。**
