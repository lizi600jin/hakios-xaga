# 候选驱动审查：`hakios.mt6895_cpufreq.c`（只读探针）

> 2026-11-16 21:05，DSH。**独立审查 + 复跑。未接入生产树、未改代码、未碰设备寄存器。**

---

## 0. 身份核验

| 文件 | 大小 | SHA256 |
|---|---|---|
| **`hakios.mt6895_cpufreq.c`** | **4899 B** | **`ef013ea9178e18a2018cf865e609f26f5ef15fdafcc680f95d38e2c165d7b6e9`** ✅ 完全匹配 |
| `test-cpufreq.c` | 5692 B | 164 行 |
| `cpufreq-arm64.o` | 10904 B | `0d96a255ebc938af…` |
| `test-cpufreq`（已编译） | 1769640 B | — |

**路径**：`P/logs/mt6895-cpu-baseline-20261116/candidate/`

---

## 1. 七项审查要点逐条核对

### ① 双快照 70 读/域 ✅

**源码：**

| 行 | 内容 |
|---|---|
| 63-65 | 3 次 read（`CPU_ENABLE`/`CPU_STATUS`/`CPU_REQUEST`） |
| 68-72 | 循环 `CPU_ROWS`=32 次 read（LUT） |
| 83 | **调用 `cpu_read_snapshot` 两次** |

**单次 snapshot = 3 + 32 = 35；双快照 = 35 × 2 = **70 次/域** ✅
三域共 **210 次**（纯读）。

### ② 32 行 / 低 12 位 ✅

| 行 | 内容 |
|---|---|
| 24 | `#define CPU_ROWS 32u` |
| 108 | `uint32_t mhz = o.second.lut[i] & 0xfffu;` — **低 12 位** ✅ |
| 125 | `(o.second.lut[o.second.request] & 0xfffu) * 1000u` — **×1000 得 kHz** ✅ |

**与 vendor 源码 `LUT_FREQ = GENMASK(11,0)`、`× 1000` 完全一致。**

### ③ 重复终止与下降方向 ✅（**本轮用真实数据验证**）

**源码 107-120 行：**

```c
for (uint32_t i = 0; i < CPU_ROWS; i++) {
    uint32_t mhz = o.second.lut[i] & 0xfffu;
    if (i != 0 && mhz == previous) break;              /* 重复即终止 */
    if (mhz == 0 || (i != 0 && mhz > previous)) {      /* 上升即 BAD_TABLE */
        result = CPU_BAD_TABLE;
        goto done;
    }
    o.count++;
    previous = mhz;
}
```

**即要求「严格降序 + 重复即停」。**

**🔴 用真实设备频率表验证（从原厂 Android 读 `scaling_available_frequencies`）：**

| policy | 项数 | 严格降序 | 首 | 末 |
|---|---|---|---|---|
| policy0 | **25** | ✅ **True** | 2000 MHz | 450 MHz |
| **policy4** | **32** | ✅ **True** | 2850 MHz | 200 MHz |
| policy7 | **29** | ✅ **True** | 2850 MHz | 500 MHz |

**三个 policy 全部满足严格降序 —— 候选的方向检查与真实表一致。**

**32 项满表边界（`policy4` 正好 32）：**
- LUT 全 32 行不重复 → 循环跑满 → `count = 32` ✅
- 读 32 行 = 偏移 `0x00`–`0x7c`；第 33 行会读 `0x80`，**被白名单拒绝** ✅

### ④ `status` / `enable` / `index` 拒绝 ✅

| 行 | 检查 | 失败结果 |
|---|---|---|
| **87-91** | 两次快照 `enable`/`status`/`request` 不一致 | `CPU_UNSTABLE` |
| **92-97** | 两次快照 32 行 LUT 任一不一致 | `CPU_UNSTABLE` |
| **98-101** | `enable != 1` | `CPU_NOT_ENABLED` |
| **103-106** | `(status & 3) != 3` | `CPU_NOT_READY` |
| **121-124** | `request >= count` | `CPU_BAD_INDEX` |

**🔴 一处我注意到但**不是问题**的地方：**

**vendor 源码（行 176-183）对 status 的处理是：**
- `bit0` 未置 → `-ENODEV` **失败**
- `bit0` 置但 `bit1` 未置 → **只警告，继续**

**候选代码（行 103）要求 `(status & 3) == 3`，即 **bit0 和 bit1 都要有**。**

**这比 vendor 更严格。** 后果：**若 SVS 未启用，vendor 会继续而候选会判 `CPU_NOT_READY`。**

**这不算缺陷**（探针宁可保守），但若将来上机发现 `CPU_NOT_READY` 而 Android 侧正常，
**要想到这个语义差异。**

### ⑤ 缺省 / 非法参数零映射 ✅

**源码 149-157 行：**

```c
if (!PE_parse_boot_argn("mtk_cpu_probe", &mode, sizeof(mode)) || mode == 0) {
    return;                              /* 无参数或 0 -> 直接返回 */
}
if (mode != 1u) {
    printf("mt6895-cpu: mtk_cpu_probe must be 0 or 1; no access\n");
    return;                              /* 非法值 -> 打印后返回，无映射 */
}
```

**三条路径都在 `ml_io_map`（行 161）之前返回：**

| 情形 | 行为 |
|---|---|
| 参数不存在 | `return`，**零映射** |
| `mode == 0` | `return`，**零映射** |
| `mode == 2` 及以上 | 打印警告后 `return`，**零映射** |
| `mode == 1` | 进入映射与探测 |

### ⑥ 全无控制器写 ✅（**我逐个查过**）

**全文件 `volatile` 访问只有 1 处（行 140）：**

```c
*value = *(const volatile uint32_t *)(base + offset);
```

**这是「读寄存器赋值给局部变量」，不是写寄存器。**

**全文件 `write`/`memcpy`/`memset`/`bzero`/`setbit`/`clrbit` 命中 2 次，
全部是注释：**
- 第 2 行：`* ... No controller writes.`
- 第 44 行：`/* A read callback has no write operation ... */`

**→ 没有任何一条写指令。**

**更关键**：读回调是 `bool (*read)(void *, uint32_t, uint32_t *)`（行 46）——
**函数指针类型里没有任何写操作的位置**。这是**类型层面**保证只读，不只是「碰巧没写」。

### ⑦ 地址白名单 ✅

**源码 131-142 行：**

```c
bool lut = offset <= 0x7cu && (offset & 3u) == 0;
if (!lut && offset != CPU_ENABLE && offset != CPU_REQUEST && offset != CPU_STATUS) {
    return false;
}
*value = *(const volatile uint32_t *)(base + offset);
```

**边界核对：**

| 允许项 | 值 | 校验 |
|---|---|---|
| LUT 偏移 | `≤ 0x7c` 且 4 字节对齐 | 32 行 × 4 = `0x80`，最后行 `i=31` → `0x7c` ✅ |
| `CPU_ENABLE` | `0x84` | — |
| `CPU_REQUEST` | `0x88` | — |
| `CPU_STATUS` | `0x8c` | — |
| **最大访问** | `0x8c + 4 = 0x90` | **`0x90 ≤ 0x120`（域大小）** ✅ |

**→ 所有访问都在 `[0, 0x90)` 内，远小于 `0x120` 域大小。**

---

## 2. 独立复跑结果

### 2.1 ASan/UBSan 测试（130 cases）

```bash
clang -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
  -o /tmp/test-cpufreq-asan test-cpufreq.c
```

| 项 | 结果 |
|---|---|
| build rc | **0** |
| run rc | **0** |
| stderr | **空** |
| **输出** | **`PASS 130 cases; 70 read failures; 35 snapshot changes; read-only mappings`** |

**与 Codex 声明的 130 cases 一致。**
**`70 read failures` 与 `35 snapshot changes` 是测试注入的故障用例数，不是失败。**

### 2.2 ARM64 独立对象

```bash
clang --target=aarch64-none-elf -ffreestanding -nostdlib -fno-builtin \
  -mgeneral-regs-only -mstrict-align -Os -Wall -Wextra \
  -I<shim> -c hakios.mt6895_cpufreq.c -o cpufreq-arm64-rebuild.o
```

| 项 | 结果 |
|---|---|
| **编译 rc** | **0** ✅ |
| 产物 | 3256 B（我自建 shim 头，与原件 10904 B 不同是正常的） |

**⚠️ 说明**：我用自己的 shim 头（`mach_types.h`/`pexpert.h` 等），
**与 Codex 那次的头文件不同，所以 `.o` 大小/hash 不同。**
**「能独立编译」这个结论成立，但产物不可比对。**

---

## 3. 真实 Android CPU 基线（本轮采集）

**设备**：`boot_id` **`686cb45c-3f54-481f-ba0c-5e9945f95449`**、slot `_a`
**方式**：仅 `cat` sysfs，**未调频、未改设置、未读 devmem、未刷机**

### 3.1 CPU 拓扑

| 项 | 值 |
|---|---|
| `online` | **`0-7`** |
| `present` | **`0-7`** |
| `possible` | **`0-7`** |

### 3.2 三个 policy

| 项 | policy0 | policy4 | policy7 |
|---|---|---|---|
| **driver** | `mtk-cpufreq-hw` | `mtk-cpufreq-hw` | `mtk-cpufreq-hw` |
| **related_cpus** | **`0 1 2 3`** | **`4 5 6`** | **`7`** |
| affected_cpus | `0 1 2 3` | `4 5 6` | `7` |
| **cur_freq** | **2000000** | **500000** | **1500000** |
| scaling_min | 450000 | **200000** | 500000 |
| scaling_max | 2000000 | 2850000 | 2850000 |
| cpuinfo_min | 450000 | 200000 | 500000 |
| cpuinfo_max | 2000000 | 2850000 | 2850000 |
| **governor** | `sugov_ext` | `sugov_ext` | `sugov_ext` |
| **可用 governor** | `sugov_ext conservative powersave performance schedutil` | 同 | 同 |
| **transition_latency** | **1000**（ns） | **1000** | **1000** |
| **频率数** | **25** | **32** | **29** |

### 3.3 完整频率表

**policy0（25 项，2.00 GHz → 450 MHz）：**
```
2000000 1900000 1800000 1700000 1600000 1500000 1400000 1300000 1250000
1200000 1150000 1100000 1050000 1000000 950000 900000 850000 800000
750000 700000 650000 600000 550000 500000 450000
```

**policy4（32 项，2.85 GHz → 200 MHz）—— 正好 32 项，满表：**
```
2850000 2750000 2650000 2550000 2450000 2400000 2350000 2300000 2250000
2200000 2150000 2100000 2050000 2000000 1900000 1800000 1700000 1600000
1500000 1400000 1300000 1200000 1100000 1000000 900000 800000 700000
600000 500000 400000 300000 200000
```

**policy7（29 项，2.85 GHz → 500 MHz）：**
```
2850000 2750000 2650000 2550000 2450000 2400000 2350000 2300000 2250000
2200000 2150000 2100000 2050000 2000000 1900000 1800000 1700000 1600000
1500000 1400000 1300000 1200000 1100000 1000000 900000 800000 700000
600000 500000
```

### 3.4 与 live DT 的 CPU 分簇一致性

| policy | related_cpus | 对应 live DT 行 | `performance-domains` |
|---|---|---|---|
| policy0 | `0 1 2 3` | 570/583/596/609 | domain **0** |
| policy4 | `4 5 6` | 622/635/648 | domain **1** |
| policy7 | `7` | 661 | domain **2** |

**→ 与设备树的 4 + 3 + 1 分簇完全一致 ✅**

---

## 4. 我的两条观察（不是缺陷）

### 4.1 `status` 检查比 vendor 更严格

| | vendor | 候选 |
|---|---|---|
| `bit0`=0 | `-ENODEV` 失败 | `CPU_NOT_READY` |
| `bit0`=1, `bit1`=0 | **只警告，继续** | **`CPU_NOT_READY`** |

**若上机报 `CPU_NOT_READY` 而 Android 侧正常，应想到这个差异。**

### 4.2 `transition_latency` 实测是 1000

**vendor 源码行 158-163 读 `+0x114` 得 µs，×1000 转 ns。**
**实测 `cpuinfo_transition_latency = 1000` ns，即寄存器里应是 1 µs。**

**这说明 `+0x114` 那个寄存器**确实可读**且值为 1。
**候选驱动不读它（不在白名单里），只是观察，不影响。**

---

## 5. 边界

**本轮只做**：读候选源码 180 行、独立编译并跑测试、只用 `cat` 采 Android sysfs。

**没有做**：改候选代码、接入生产树、读写任何控制器寄存器、调频、改设置、刷机、读 devmem。

**「解码自洽」不等于「读过真实 LUT」** —— 我没有访问 `0x11bc10`。
