# MT6895 `mediatek,cpufreq-hw` 协议对照（vendor 源码逐行审查）

> 2026-11-16 21:00，DSH。**仅主机逐行审查。未执行任何寄存器/调频操作。**

---

## 0. 输入身份

| 文件 | 大小 | SHA256 | 行数 |
|---|---|---|---|
| **`mediatek-cpufreq-hw.vendor.c`** | **8999 B** | **`3cc72e1f5b36e0154ab34df932a1541a59e2f1932b160a5538866df7f2d31d8f`** | **387** |
| `xaga-live.dts`（live DT） | 513480 B | `f47d073b92b76209…` | 14212 |

**来源**：官方 `MiCode/Xiaomi_Kernel_OpenSource/xaga-s-oss/drivers/cpufreq`
**本地**：`P/logs/mt6895-cpu-baseline-20261116/mediatek-cpufreq-hw.vendor.c`

**身份核验：8999 B 与 `3cc72e1f…` 与 Codex 声明完全一致。**

---

## 1. 源码常量（逐行核对，全部与 Codex 描述一致）

| 行 | 常量 | 值 |
|---|---|---|
| **18** | `LUT_MAX_ENTRIES` | **`32U`** |
| **19** | `LUT_FREQ` | **`GENMASK(11, 0)`**（低 12 位） |
| **20** | `LUT_ROW_SIZE` | **`0x4`** |
| **21** | `CPUFREQ_HW_STATUS` | **`BIT(0)`** |
| **22** | `SVS_HW_STATUS` | **`BIT(1)`** |
| 23 | `POLL_USEC` | `1000` |
| 24 | `TIMEOUT_USEC` | `300000` |

---

## 2. 域内寄存器偏移（源码 44-51 行）

```c
static const u16 cpufreq_mtk_offsets[REG_ARRAY_SIZE] = {
	[REG_FREQ_LUT_TABLE]	= 0x0,      /* LUT 表起始 */
	[REG_FREQ_ENABLE]	= 0x84,     /* 使能 */
	[REG_FREQ_PERF_STATE]	= 0x88,     /* 目标行 */
	[REG_FREQ_HW_STATE]	= 0x8c,     /* 硬件状态 */
	[REG_EM_POWER_TBL]	= 0x90,     /* EM 功耗表 */
	[REG_FREQ_LATENCY]	= 0x114,    /* 切换延迟（µs） */
};
```

**域基址来自 live DT（`performance-controller@0011bc00` 行 2714-2720）：**

| 域 | 基址 | 域大小 | **基址 + 0x114（最大偏移）** |
|---|---|---|---|
| **0** | **`0x11bc10`** | `0x120` | `0x11bd24` ✅ 在域内 |
| **1** | **`0x11bd30`** | `0x120` | `0x11be44` ✅ |
| **2** | **`0x11be50`** | `0x120` | `0x11bf64` ✅ |

**🔴 校验通过：`0x114 + 4 = 0x118 ≤ 0x120`，所有偏移都在域大小内。**

**而 Qualcomm EPSS 用 `0x100`/`0x320` —— `0x320 > 0x120`，会在域外。这是两套完全不兼容的布局。**

---

## 3. 频率表解析（源码 223-256 行）

```c
for (i = 0; i < LUT_MAX_ENTRIES; i++) {
	data = readl_relaxed(base_table + (i * LUT_ROW_SIZE));   /* 行 238 */
	freq = FIELD_GET(LUT_FREQ, data) * 1000;                 /* 行 239 */
	if (freq == prev_freq)
		break;                                               /* 行 241-242 */
	c->table[i].frequency = freq;
	prev_freq = freq;
}
c->table[i].frequency = CPUFREQ_TABLE_END;                   /* 行 252 */
c->nr_opp = i;                                               /* 行 253 */
```

**关键事实：**

| 项 | 内容 |
|---|---|
| 表长 | **最多 32 项**（`LUT_MAX_ENTRIES`） |
| 每项 | **4 字节**（`LUT_ROW_SIZE = 0x4`） |
| **频率编码** | **低 12 位，单位 = 1000 kHz（即 MHz 整数）** |
| **终止条件** | **遇到与上一行相同的值即停止**（不是读到 0） |
| 表大小 | 32 × 4 = **128 字节**（`0x80`），而 `REG_FREQ_ENABLE` 在 `0x84` —— **正好留 4 字节间隙** ✅ |

---

## 4. 用真实设备数据验证 LUT 解码（本轮新增实证）

**从运行中的原厂 Android 读 `scaling_available_frequencies`：**

| policy | 频率数 | 最大 | 最小 | **是否 ≤32** |
|---|---|---|---|---|
| **policy0**（A55×4） | **25** | 2000000 | 450000 | ✅ |
| **policy4**（A78×3） | **32** | 2850000 | 200000 | ✅ **正好 32** |
| **policy7**（A78×1） | **29** | 2850000 | 500000 | ✅ |

**解码自洽性检验**：按 `freq = 低12位 × 1000` 反推，低 12 位应为
`2000 / 1900 / 1800 …`（即 2000 MHz 等）。

**这些值全部 < 4096（12 位上限），且是合理的小整数 —— 解码假设自洽。**

**🔴 但这不是「读到了真实 LUT」的证据** ——
我没有读 `0x11bc10` 的物理寄存器，只验证了「若按此解码，数值不会溢出 12 位」。

---

## 5. 使能与状态机（源码 128-191、193-207 行）

### 5.1 初始化序列（`mtk_cpufreq_hw_cpu_init`）

| 行 | 动作 |
|---|---|
| 158-163 | 读 `+0x114` 得延迟（µs），`×1000` 转 ns；**为 0 时用 `CPUFREQ_ETERNAL`** |
| 165 | `policy->fast_switch_possible = true` |
| **168** | **`cpu_latency_qos_add_request()` —— 让 CPU 退出 idle-off，供 SVS 初始化** |
| **171** | **`writel_relaxed(0x1, base + 0x84)` —— 写使能** |
| **173-175** | **`readl_poll_timeout(base + 0x8c, sig, (sig & 0x3) == 0x3, 1000, 300000)`** |
| 176-180 | 若 **bit0 未置** → 打印 `cpufreq hardware of CPU%d is not enabled`，**返回 `-ENODEV`** |
| 182 | 若只有 bit0 无 bit1 → 打印 `SVS of CPU%d is not enabled`，**但继续** |

**🔴 关键区别**：

| 条件 | 行为 |
|---|---|
| **`status` bit0 = 0** | **驱动初始化失败（-ENODEV）** |
| `status` bit0 = 1 且 bit1 = 0 | **只警告，继续** |
| 两者都 = 1 | 正常 |

**即：`bit0` 是硬性要求，`bit1`（SVS）不是。**

### 5.2 退出（`cpu_exit`，行 193-207）

**`writel_relaxed(0x0, base + 0x84)` —— 写 0 到使能寄存器。**

**⚠️ 这是「paused」而非「关闭」**（注释原文 `HW should be in paused state now`）。

### 5.3 设频率（三条路径）

| 函数 | 行 | 动作 |
|---|---|---|
| `mtk_cpufreq_hw_target_index` | **94** | **`writel_relaxed(index, base + 0x88)`** |
| `mtk_cpufreq_hw_fast_switch` | **123** | 同上，但先算 index |
| `mtk_cpufreq_hw_get` | **106-107** | 读 `+0x88`，`min(index, 31)` |

**🔴 设频率只需一次写：把行号写到 `+0x88`。没有电压寄存器、没有 PLL 配置。**

**这与 `hakios.cpufreq.c` 头注释「one register write」的说法形式一致，
但寄存器地址与编码完全不同。**

---

## 6. 与 `hakios.cpufreq.c` 的逐项对照（不做能力推断）

| 项 | `hakios.cpufreq.c`（Qualcomm EPSS） | **xaga 实际（vendor 源码 + live DT）** |
|---|---|---|
| compatible | `qcom-cpufreq-hw.c` 的 epss 变体 | **`mediatek,cpufreq-hw`** |
| 基址 | `0x18591000` | **`0x11bc10` / `0x11bd30` / `0x11be50`** |
| 域大小 | `0x1000` | **`0x120`** |
| LUT 偏移 | **`0x100`** | **`0x0`** |
| **perf_state 偏移** | **`0x320`** | **`0x88`** |
| 使能寄存器 | **无** | **`0x84`**（必须写 1） |
| 状态寄存器 | **无** | **`0x8c`**（bit0 必须为 1） |
| **频率编码** | **`[7:0]` × 19.2 MHz** | **`[11:0]` × 1000 kHz** |
| LUT 行数 | `40` | **`32`** |
| LUT 终止条件 | 未在注释中说明 | **同值即停** |

**🔴 我不从这张表推断「因此它不能工作」—— 只说字段不同。**
**真正的判据是它未被编译（`mt6895.config:49` 关闭 + stubs 空函数 + 无 `.o`）。**

---

## 7. 设备树依赖链（源码要求 ↔ live DT 实际）

### 7.1 源码对 DT 的三项硬依赖

| 源码位置 | 要求 | live DT 是否有 |
|---|---|---|
| **269-271**（`mtk_get_related_cpus`） | cpu 节点有 **`performance-domains`** 属性 | ✅ **有，8 处** |
| **343-345**（`probe`） | 同上 | ✅ |
| **301**（`devm_platform_ioremap_resource`） | `performance-controller` 的 `reg` 三段 | ✅ **行 2716** |

### 7.2 `performance-domains` 在 live DT 的分布（行 570-661）

| 行 | CPU | `performance-domains` |
|---|---|---|
| **570** | `cpu@000`（A55） | **`<0x2e 0x00>`** → **domain 0** |
| **583** | `cpu@001`（A55） | `<0x2e 0x00>` → domain 0 |
| **596** | `cpu@002`（A55） | `<0x2e 0x00>` → domain 0 |
| **609** | `cpu@003`（A55） | `<0x2e 0x00>` → domain 0 |
| **622** | `cpu@100`（A78） | **`<0x2e 0x01>`** → **domain 1** |
| **635** | `cpu@101`（A78） | `<0x2e 0x01>` → domain 1 |
| **648** | `cpu@102`（A78） | `<0x2e 0x01>` → domain 1 |
| **661** | `cpu@200`（A78） | **`<0x2e 0x02>`** → **domain 2** |

**`0x2e` = `performance-controller@0011bc00` 的 phandle（行 2719）。**

**→ 8 个 CPU 分成 3 个 domain：4 + 3 + 1，与 `board.h` 的拓扑一致。**

### 7.3 `GET`/`SET` 的行号映射（源码与 DT 一致）

**源码 `mtk_cpu_resources_init(pdev, cpu, index=args.args[0], offsets)` 用
`devm_platform_ioremap_resource(pdev, index)` —— `index` 直接就是 DT `reg` 的段号。**

**所以 domain 0/1/2 分别对应 `reg` 的第 0/1/2 段 → `0x11bc10`/`0x11bd30`/`0x11be50`。** ✅

---

## 8. 🔴 MCUPM / cpuhvfs 依赖（Codex 要求补的部分）

### 8.1 三个不同的节点，三个不同的驱动

| 节点 | 行 | compatible | 我的理解 |
|---|---|---|---|
| **`performance-controller@0011bc00`** | **2714** | **`mediatek,cpufreq-hw`** | **本文审查的这个驱动** |
| **`cpuhvfs@00114400`** | **2753** | **`mediatek,cpufreq-hybrid`** | **另一个驱动（hybrid DVFS）** |
| **`mcupm@c540000`** | **1344** | **`mediatek,mcupm`** | **第三个（MCU 电源管理协处理器）** |

### 8.2 它们的关系（我能确定的与不能确定的）

**能确定的：**

| 事实 | 依据 |
|---|---|
| **`cpufreq-hw` 的源码完全不引用 MCUPM 或 cpuhvfs** | 全文 387 行无 `mcupm`/`cpuhvfs`/`hybrid` 字样 |
| **`cpufreq-hw` 只依赖 `performance-controller` 一个节点** | `of_match_table` 只有一条（行 372） |
| **`cpuhvfs` 有独立的 `tbl-off = <0x04 0x4c 0x94 0xdc>`** | live DT 行 2758（**与 `cpufreq-hw` 的 `0x0` 不同**） |
| **`cpuhvfs` 的 `tbl-off` 是四组偏移**，而 CPU 只有 3 个 domain | 多了第 4 组（可能是 GPU 或 DSU） |
| **`mcupm` 有 8 个 mailbox**（`mbox0`-`mbox7`） | live DT 行 1347-1348 |

**不能确定的（明确标为未证实）：**

| # | 未证实 |
|---|---|
| **1** | **`cpufreq-hw` 与 `cpuhvfs` 是否同时存在、谁会赢**（两个都写频率？） |
| **2** | **`cpuhvfs` 的 `tbl-off` 四组分别对应哪个域** |
| **3** | **MCUPM 是否需要在 `cpufreq-hw` 之前初始化** |
| **4** | **`status` bit0/bit1 是谁置的**（TF-A？MCUPM？cpuhvfs？） |
| **5** | **`cpuhvfs` 的 `USRAM/ESRAM` 与 `cpufreq-hw` 的 CSRAM 是否重叠** |

### 8.3 地址重叠观察（事实陈述）

| 节点 | 地址范围 |
|---|---|
| **`cpufreq-hw` domain 0/1/2** | `0x11bc10`–`0x11bf6f`（三段共 `0x360`） |
| **`cpuhvfs` 的 CSRAM** | **`0x11bc00`–`0x11bfff`**（`0x1400` 大小） |
| **`eas_info` 的 `csram-base`** | **`0x11bc00`** |

**🔴 `cpufreq-hw` 的三个域（`0x11bc10`–`0x11bf6f`）完全落在 `cpuhvfs` 的 CSRAM 范围内。**

**这不是错误 —— 是**同一个硬件块被两个驱动从不同角度访问**。
但**谁拥有它、谁先初始化，我无法判定**。**

### 8.4 `mcupm` 的关键信息（行 1344-1350）

```dts
mcupm@c540000 {
    compatible = "mediatek,mcupm";
    reg = <0x00 0xc540000 0x00 0x22000          /* mcupm_base */
           0x00 0xc56fb00 0x00 0xa0 ...>;       /* 8 个 mailbox */
    interrupts = <... 0x18-0x1f ...>;           /* 8 个中断 */
    interrupt-names = "mbox0"..."mbox7";
};
```

**另有 reserved 内存（行 1118-1122）：**

```dts
mblock-17-MCUPM-reserved {
    reg = <0x00 0x7f300000 0x00 0x100000>;      /* 1 MiB */
    no-map;
    compatible = "mediatek,MCUPM-reserved";
};
```

**SCMI 通道号（行 925）：`scmi_apmcupm = <0x0c>`。**

---

## 9. 一项 Codex 指出的、我必须承认的计数错误

**我在 `README.md` §2.1 写「显式开启的五项」，实际列了 6 行。**

**正确说法：`mt6895.config` 里 `=y` 的 MTV6895 自有驱动共 6 项：**

| 行 | 配置 |
|---|---|
| 72 | `CONFIG_HAKIOS_MT6895_PMIF=y` |
| 75 | `CONFIG_HAKIOS_NVT=y` |
| 78 | `CONFIG_HAKIOS_MTK6895_KEYS=y` |
| 81 | `CONFIG_HAKIOS_MT6895_HEADSET=y` |
| 86 | `CONFIG_HAKIOS_MT6895_USB=y` |
| 91 | `CONFIG_HAKIOS_MALI610=y` |

**「五项」是笔误，实质列了六项，已在本文件更正。**

---

## 10. 边界

**本轮只做**：读 `mediatek-cpufreq-hw.vendor.c` 全部 387 行、
读 `xaga-live.dts` 的相关节点、读 Android 侧 `scaling_available_frequencies`（只读）。

**没有做**：读写任何寄存器、写 `0x84`/`0x88`、调频、反汇编、构建、刷写。

**「解码自洽」不等于「读过真实 LUT」** —— 我没有访问 `0x11bc10`。
