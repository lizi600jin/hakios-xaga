# HOST-CPU-BASELINE-01：MT6895 CPU/调度基线与专用适配证据

> 2026-11-16 20:45，DSH。**仅主机。未改生产代码、未构建、未刷写。**

**🔴 纪律：不把注释命中或 logbook 零次归因成地址错误。本文只列可查证据。**

---

## 0. 三项结论速览

| # | 问题 | 结论 |
|---|---|---|
| **1** | 60 / 23 / 8 对不对 | **60 / 23 / 6**（不是 8）；**但不是 60 个都编译** |
| **2** | cpufreq 与 modem 实际状态 | **两者都在 `mt6895.config` 里显式关闭，由 stubs 提供空函数，`.o` 不存在** |
| **3** | MT6895 DVFS 一手材料 | **找到 `cpuhvfs@00114400`（`mediatek,cpufreq-hybrid`）与 `performance-controller@0011bc00`** |

---

## 1. ① 复算 60 / 23 / 6，完整集合与规则

### 1.1 复算规则（写死，可重现）

```bash
cd <kernel-src>
ls -1 bsd/dev/hakios.* | wc -l                                    # 60
grep -lE 'redwood|POCO X5 Pro' bsd/dev/hakios.* | sed 's|bsd/dev/||' | sort
grep -lE 'xaga'                bsd/dev/hakios.* | sed 's|bsd/dev/||' | sort
```

**文件匹配模式统一为 `hakios.*`（含 `.c` 与 `.h`）。**

### 1.2 结果

| 项 | 数 |
|---|---|
| **`bsd/dev/hakios.*` 总数** | **60** |
| **命中 redwood / POCO X5 Pro** | **23** |
| **命中 xaga** | **6** |
| **两者都命中** | **2** |
| **两者都不命中** | **33** |

**⚠️ 更正我上一轮的报告**：我先前报「8 个 xaga」用的是**宽松规则**
`grep -lE 'xaga|MT6895|mt6895'`（得 9 个），**不是严格的 `xaga`**。
**严格规则下是 6 个。**

### 1.3 redwood 命中全集（23）

`hakios.adreno.c`、`hakios.adsp.c`、`hakios.apfs_check.c`、`hakios.battery.c`、
`hakios.bt.c`、`hakios.buttons.c`、`hakios.camera.c`、**`hakios.cpufreq.c`**、
`hakios.goodix.c`、`hakios.haptic.c`、`hakios.ipa.c`、`hakios.mic.c`、
**`hakios.mpss.c`**、`hakios.mt6895_keys.c`、`hakios.nvt.c`、`hakios.platform.c`、
`hakios.pmic.c`、`hakios.socinfo.c`、`hakios.sound.c`、`hakios.speaker.c`、
`hakios.thermal.c`、`hakios.torch.c`、`hakios.wifi.c`

### 1.4 xaga 命中全集（6）

`hakios.mali610.c`、`hakios.mt6895_headset.c`、`hakios.mt6895_keys.c`、
`hakios.mt6895_usb.c`、`hakios.nvt.c`、`hakios.stubs.c`

### 1.5 交叉（2）

`hakios.mt6895_keys.c`、`hakios.nvt.c`

**说明**：这两个文件**同时**出现两个字符串 ——
**「注释里提到 redwood」不等于「地址是 redwood 的」**。
`mt6895_keys.c` 是 xaga 专用驱动（`mt6895.config:78` 开启），提到 redwood 应是注释对比。

---

## 2. ② 编译状态：60 个文件 ≠ 60 个驱动

### 2.1 `mt6895.config` 原文（`mtk/mt6895/configs/mt6895.config`）

**身份**：5284 B / **`b8edfb815291c9d45a06a964a919c7ebaaafbf1869b0be5d98acc4367134a2de`** / 91 行

**显式关闭的两项：**

| 行号 | 原文 |
|---|---|
| **49** | **`# CONFIG_HAKIOS_CPUFREQ is not set`** |
| **57** | **`# CONFIG_HAKIOS_MODEM is not set`** |
| 36 | `# CONFIG_MODEM_AGAIN is not set` |

**显式开启的五项（`=y`）：**

| 行号 | 配置 | 驱动文件 |
|---|---|---|
| **72** | `CONFIG_HAKIOS_MT6895_PMIF=y` | `hakios.mt6895_pmif.c` |
| **75** | `CONFIG_HAKIOS_NVT=y` | `hakios.nvt.c` |
| **78** | `CONFIG_HAKIOS_MTK6895_KEYS=y` | `hakios.mt6895_keys.c` |
| **81** | `CONFIG_HAKIOS_MT6895_HEADSET=y` | `hakios.mt6895_headset.c` |
| **86** | `CONFIG_HAKIOS_MT6895_USB=y` | `hakios.mt6895_usb.c` |
| **91** | `CONFIG_HAKIOS_MALI610=y` | `hakios.mali610.c` |

**另有 `CONFIG_CPUS=4`（第 26 行）与 `CONFIG_SOC="mt6895"`（第 25 行）。**

### 2.2 实际编译产物

**从 trial712 的 `BUILD/obj/DEVELOPMENT_ARM64_BCM2837` 统计 `.o`：**

**共 15 个 `hakios*.o`：**

`hakios.apfs_check.o`、`hakios.audio.o`、`hakios.image.o`、`hakios.input.o`、
`hakios.mali610.o`、`hakios.mt6895_headset.o`、`hakios.mt6895_keys.o`、
`hakios.mt6895_pmif.o`、`hakios.mt6895_usb.o`、`hakios.net.o`、`hakios.nvt.o`、
`hakios.platform.o`、`hakios.sparse.o`、`hakios.stubs.o`、`hakios.version.o`

**🔴 关键：`hakios.cpufreq.o` 与 `hakios.mpss.o` 不在其中。**

### 2.3 stubs 提供空函数（原文）

`bsd/dev/hakios.stubs.c`（203 行）：

```c
#ifndef HAKIOS_CPUFREQ                      /* 行 133 */
void hakios_cpufreq_init(void) {}           /* 行 134 */
void hakios_ddr_quiesce(void) {}            /* 行 135 */
#endif
```

```c
#ifndef HAKIOS_MODEM                         /* 行 159 */
void hakios_mpss_init(void) {}              /* 行 160 */
void hakios_mpss_stop(void) {}              /* 行 161 */
void hakios_mpss_quiesce(void) {}           /* 行 162 */
#endif
```

### 2.4 内核里那 13 个 `cpufreq` 字符串是什么

**实测 `strings mach.development.bcm2837 | grep -i cpufreq` 得 13 条，全部是：**

```
cpufrequency / cpufrequency_min / cpufrequency_max / cpufrequency_compat
_sysctl__hw_cpufrequency(_min/_max/_compat)
___startup_SYSCTL_entry_sysctl__hw__cpufrequency(...)
_hakios_cpufreq_init
```

**🔴 前 12 条是 XNU 原生的 `hw.cpufrequency` sysctl，与 MediaTek 调频无关。**
**第 13 条 `_hakios_cpufreq_init` 就是 stubs 里那个空函数的符号。**

**所以「内核里有 cpufreq 字符串」不能推出「cpufreq 驱动在工作」。**

### 2.5 实际 boot args（trial712 logbook 原文）

**598 字节，关键项：**

| 参数 | 出现 |
|---|---|
| **`cpus=4`** | **1 次** |
| **`cpufreq`** | **0 次** |
| **`sound=`** | **0 次** |
| **`adsp=`** | **0 次** |
| `mali_trial=712` | 1 次 |
| `headset=4` | 1 次 |

**即：本轮**没有**请求 cpufreq，与 config 关闭一致。**

---

## 3. ③ MT6895 DVFS 一手材料

### 3.1 封存的 live DTS（**最权威**）

**另：该文件行 13679/13684 的 `__symbols__` 段确认 `performance` 与 `cpuhvfs` 两个 phandle 都有效。**

| 项 | 值 |
|---|---|
| 路径 | **`docs/headset-wip/raw/dapm-preflight-20261115/xaga-live.dts`** |
| 大小 | **513480 B** |
| SHA256 | **`f47d073b92b76209…`** |
| 行数 | **14212** |

**来源**：从运行中的原厂 Android `/proc/device-tree` 导出（与 `BOARD.md` 记录的
`lk_a.img` FDT、`vendor_boot.dtb` 三者相同）。

### 3.2 `performance-controller@0011bc00`（**行 2714**，至 2720）

```dts
performance-controller@0011bc00 {
    compatible = "mediatek,cpufreq-hw";
    reg = <0x00 0x11bc10 0x00 0x120
           0x00 0x11bd30 0x00 0x120
           0x00 0x11be50 0x00 0x120>;
    reg-names = "performance-domain0", "performance-domain1", "performance-domain2";
    #performance-domain-cells = <0x01>;
    phandle = <0x2e>;
};
```

### 3.3 🔴 `cpuhvfs@00114400`（**行 2753**，至 2775）—— 这才是完整 DVFS 节点

```dts
cpuhvfs@00114400 {
    compatible = "mediatek,cpufreq-hybrid";
    reg = <0x00 0x114400 0x00 0xc00      /* USRAM */
           0x00 0x11bc00 0x00 0x1400     /* CSRAM */
           0x00 0x112400 0x00 0x1800     /* ESRAM */
           0x00 0x114f40 0x00 0xc0>;
    reg-names = "USRAM", "CSRAM", "ESRAM";
    cslog-range = <0x3d0 0xfa0>;
    tbl-off = <0x04 0x4c 0x94 0xdc>;
    mcucfg-ver = <0x00>;
    apmixedsys = <0x5b>;
    clk-div-base = <0x5c>;
    pll-con = <0x20c 0x21c 0x22c 0x23c>;
    clk-div = <0xa2a0 0xa2a4 0xa2a8 0xa2e0>;
    proc1-supply = <0x5d>;
    proc2-supply = <0x5e>;
    proc3-supply = <0x5f>;
    proc4-supply = <0x5d>;
    proc3 = <0x186a>;
    nvmem-cells = <0x60>;
    nvmem-cell-names = "lkginfo";
    phandle = <0x19d>;
};
```

**关键字段解读（仅列字段名与数值，不推物理含义）：**

| 字段 | 值 | 我的理解程度 |
|---|---|---|
| `reg` 三段 | USRAM `0x114400`/`0xc00`、CSRAM `0x11bc00`/`0x1400`、ESRAM `0x112400`/`0x1800` | 地址已知，**用途未证实** |
| **`tbl-off`** | **`0x04 0x4c 0x94 0xdc`** | **四个域的 OPP 表偏移 —— 这是最关键的线索** |
| `pll-con` | `0x20c 0x21c 0x22c 0x23c` | 四组 PLL 控制寄存器偏移 |
| `clk-div` | `0xa2a0 0xa2a4 0xa2a8 0xa2e0` | 四组分频寄存器偏移 |
| `cslog-range` | `0x3d0 0xfa0` | 日志区范围 |
| `nvmem-cells` | `lkginfo` | 芯片漏电/速度分级信息 |

### 3.4 `eas_info`（**行 2729**，至 2735）

```dts
eas_info {
    compatible = "mediatek,eas-info";
    csram-base = <0x11bc00>;
    offs-thermal-limit = <0x1208 0x120c 0x1210 0x1214>;
    offs-cap = <0xfa0>;
};
```

### 3.5 其他相关节点

| 节点 | 行号 | 内容 |
|---|---|---|
| `mcusys_ao_cfg@0c530000` | **2743** | `0xc530000` / `0x10000` |
| `mcusys_pll1u_top@1000c000` | **2748** | `0x1000c000` / `0x1000` |
| `lkg@00114400` | **2737** | `0x114400` / `0xc00`（**与 cpuhvfs 的 USRAM 同址**） |
| `syscon@10000000` | **2811** | `mediatek,mt6895-topckgen` |

### 3.6 另一份材料：`mt6895-xiaomi-xaga.dts`

| 项 | 值 |
|---|---|
| 路径 | `docs/headset-wip/raw/mt6895-xiaomi-xaga.dts`（与 `mainline-xaga-gpu/` 下同文件相同） |
| 大小 | 43951 B |
| SHA256 | `e6d97620ed769dfe…` |
| 行数 | 1554 |
| **调频相关内容** | **行 1223 提到「mapping taken from the downstream crdroid mt6895.dts cpuhvfs node」** |

**这份是 mainline（上游）风格的 DTS，不是 vendor 的。**
**它把 MT6319 buck 映射到三个簇：**
- `mt6319_7 vbuck3` → little cluster + DSU
- `mt6319_7 vbuck1` → middle（A78 × 3）
- `mt6319_6 vbuck1` → prime（A78 × 1）

**这是电压域与簇的对应关系，但**不含寄存器布局**。**

---

## 4. 还缺什么（真实缺口）

| # | 缺什么 | 为什么重要 |
|---|---|---|
| **1** | **`tbl-off` 指向的 OPP 表里每个条目的编码格式** | 不知道一个 word 里频率/电压怎么排 |
| **2** | **如何"选一行"** —— 写哪个寄存器、写什么值 | `cpuhvfs` 节点没给这个 |
| **3** | **`performance-controller` 的 `0x120` 域内布局** | 与 `cpuhvfs` 是两个不同驱动 |
| **4** | **vendor 的 `mediatek_cpufreq_hw.ko` 或 `cpuhvfs` 源码** | 最直接，但本机没有 |
| **5** | **`lkginfo` 的格式与用途** | 影响可用频率行 |

**这些缺口我在本轮无法凭现有封存材料补齐。**

---

## 5. 与 `hakios.cpufreq.c` 的关系（不做归因）

**事实陈述：**

| 项 | `hakios.cpufreq.c` | xaga 实测 |
|---|---|---|
| `compatible` | Qualcomm EPSS（注释引用 `qcom-cpufreq-hw.c`） | **`mediatek,cpufreq-hw`** 与 **`mediatek,cpufreq-hybrid`** |
| 基址 | `0x18591000` | `0x11bc10` / `0x114400` |
| 域大小 | `0x1000` | `0x120` / `0xc00` |

**🔴 我只陈述这些字段差异，不推断「因此它不能工作」。**
**真正的判据是它根本没被编译（`mt6895.config:49` 关闭 + stubs 空函数 + 无 `.o`）。**

**「注释写 redwood」与「地址错误」是两件事 —— 本文不把前者当后者的证据。**

---

## 6. 边界

**本轮只做**：`grep` 统计、读 `mt6895.config`、查 `.o` 清单、
`strings` 内核、读 logbook 的 boot args、读封存 DTS 的调频节点。

**没有做**：反汇编、读 MediaTek 官方源码、改任何文件、构建、刷写。

**未把注释命中或 logbook 零次归因成地址错误。**
