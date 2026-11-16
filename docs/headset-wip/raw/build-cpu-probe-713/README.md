# BUILD-CPU-PROBE-713：只读 CPU 探针编入隔离内核

> 2026-11-16 20:51，DSH。**仅主机。未打包、未刷写、未碰设备。**

---

## 0. 结果

| 项 | 值 |
|---|---|
| **新内核** | **15321336 B** |
| **SHA256** | **`a80231ed9dc78a82bfddc71f34685808e66ce490d70ed059e37cd108638885c3`** |
| **路径** | `logs/mt6895-cpu-baseline-20261116/kernel-src/BUILD/obj/DEVELOPMENT_ARM64_BCM2837/mach.development.bcm2837` |
| **make native rc** | **2**（既有 symbolset） |
| **judge native rc** | **0**（显式 KERNEL） |

---

## 1. A 步：输入与接入核对

### 1.1 隔离树

| 项 | 值 |
|---|---|
| 路径 | `logs/mt6895-cpu-baseline-20261116/kernel-src` |
| **文件数** | **5206** |
| **构建前 BUILD** | **不存在** ✅ |
| **`kernel-inputs.sha256` 校验** | **rc=0、5206 OK、0 FAILED** ✅ |

### 1.2 与 5205 基底的实测差异

**恰好 5 变 + 1 增，与 `isolated-tree-preparation.json` 声明完全一致：**

| 文件 | 基底 SHA256（前 8） | 现状 |
|---|---|---|
| `bsd/conf/files` | `1fcc848d` | 改 |
| `bsd/dev/hakios.input.c` | `990ef7c3` | 改 |
| `bsd/dev/hakios.mt6895_headset.c` | `c66e9f5e` | 改（→ `8c0cf1c2`） |
| `bsd/sys/hakios.input.h` | `9024194d` | 改 |
| `bsd/sys/hakios.pmif.h` | `a39ec0a9` | 改（→ `f4d9a964`） |
| **`bsd/dev/hakios.mt6895_cpufreq.c`** | — | **新增** |

### 1.3 六项输入身份

| 输入 | 实测 | 期望 | 结果 |
|---|---|---|---|
| **candidate cpufreq** | 4899 B / `ef013ea9…` | `ef013ea9178e18a2…` | ✅ |
| **树内 cpufreq** | 4899 B / `ef013ea9178e18a2` | 同 | ✅ |
| **新增文件与 candidate 逐字节** | — | 相同 | ✅ `cmp` 通过 |
| **712 headset** | 71087 B / `8c0cf1c232d0ac4f` | `8c0cf1c2…` | ✅ |
| **pmif.h** | 3356 B / `f4d9a964e304ea14` | `f4d9a964…` | ✅ |
| **vendor 协议源码** | 8999 B / `3cc72e1f5b36e015` | `3cc72e1f…` | ✅ |

### 1.4 三处接入改动（`integration.diff`，29 行）

**`bsd/conf/files`（+3 行）：**
```
+# Isolated MT6895 CPU observation experiment only.
+bsd/dev/hakios.mt6895_cpufreq.c standard
```
**→ 用的是 `standard` 不是 `optional`，符合 CMD 要求 ✅**

**`bsd/dev/hakios.input.c`（+1 行）：**
```
 	hakios_cpufreq_init();
+	hakios_mt6895_cpufreq_init();
 	hakios_virtio_input_init();
```
**→ 新入口在旧 `hakios_cpufreq_init()` **之后**，符合 CMD 要求 ✅**

**`bsd/sys/hakios.input.h`（+1 行）：**
```
+void hakios_mt6895_cpufreq_init(void); /* mtk_cpu_probe=1: read-only DVFS snapshot */
```

### 1.5 生产树未动

| 检查 | 结果 |
|---|---|
| 生产树有 `bsd/dev/hakios.mt6895_cpufreq.c` | ✅ **无** |
| 生产树有 `bsd/conf/files` | ✅ **无**（该路径只在 xnu 树内） |

---

## 2. B 步：候选 130 例复跑

**按 CMD 指定参数：**

```bash
clang -std=c11 -Wall -Wextra -Werror -O1 -g -fno-omit-frame-pointer \
  -fsanitize=address,undefined test-cpufreq.c -o <隔离输出>
```

| 项 | 结果 |
|---|---|
| **build rc** | **0**（**含 `-Werror` 也零警告**） |
| **run rc** | **0** |
| stderr | **0 字节** |
| **输出** | **`PASS 130 cases; 70 read failures; 35 snapshot changes; read-only mappings`** |

**与上一轮（无 `-Werror`）输出完全相同。**

---

## 3. C 步：构建

```bash
XNU_SRC=<隔离树> ./xnu-build.sh -k -j8
```

| 项 | 值 |
|---|---|
| 起止 | 20:48:51 → 20:50:04（**73 秒**） |
| **make native rc** | **2** |
| stdout / stderr | 128348 B / 77187 B |

### 3.1 无新增编译/链接问题

| 检查 | 结果 |
|---|---|
| `*.c:line:col: error:` 计数 | **0** |
| `error` 出现次数（两文件） | **0 / 0** |
| `undefined` 出现次数 | **0 / 0** |
| **`CC hakios.mt6895_cpufreq.o`** | **✅ 出现，编译成功** |

---

## 4. D 步：judge（显式 KERNEL）

```bash
KERNEL=<新内核> ./tools/make-kernel.sh --judge <build-combined.log>
```

| 项 | 值 |
|---|---|
| **judge native rc** | **0** |
| 判词 | `the log shows a built kernel; only the kext symbol-set checks failed` |

**🔴 `make rc=2` 与 `judge rc=0` 分开记录，未混淆。**
**只豁免既有 symbolset，无新增问题。**

---

## 5. E 步：输入复查与字符串

### 5.1 5206 输入内容未变

| 项 | 值 |
|---|---|
| 编译前 / 编译后文件数 | **5206 / 5206** |
| **内容变化** | **0** ✅ |
| **新增（排除 BUILD）** | **0** ✅ |

### 5.2 内核字符串

**五条 `mt6895-cpu` 消息全部编入：**

```
mt6895-cpu: mtk_cpu_probe must be 0 or 1; no access
mt6895-cpu: read-only probe; LUT/request values are not measured clocks
mt6895-cpu: domain %u map failed
mt6895-cpu: domain %u base=%#x enable=%#x/%#x status=%#x/%#x request=%u/%u result=%u rows=%u request_khz=%u
mt6895-cpu: domain %u row %u raw=%#x/%#x
```

| 字符串 | 出现 |
|---|---|
| `mtk_cpu_probe` | **2** |
| `mt6895-cpu` | **5** |
| `read-only probe` | **1** |

**🔴 字符串存在只证明代码被编入，不能当作运行结果。**

---

## 6. 🔴 两处必须加的限定（按 Codex 要求）

### 6.1 `status` bit1 的严格检查是**有意保守**，不是设备故障

**候选代码行 103 要求 `(status & 3) == 3`，即 bit0（CPUFREQ_HW_STATUS）
与 bit1（SVS_HW_STATUS）都要置位。**

**vendor 源码（行 176-183）的处理是：**
- bit0 未置 → `-ENODEV` 失败
- bit0 有、bit1 无 → **只警告，继续**

**所以候选比 vendor 严格。**

**🔴 明确限定：**

| 项 | 说明 |
|---|---|
| **这是刻意的保守设计** | 探针只在两位都就绪时才报 `CPU_OBSERVED` |
| **`CPU_NOT_READY` ≠ 芯片坏** | 只表示本次观察不报 READY |
| **`CPU_NOT_READY` ≠ Android 异常** | Android 侧用 vendor 语义，本就容忍 bit1 缺失 |
| **探针仍打印原始快照** | 无论 result 是什么，`first/second` 的 enable/status/request 与 32 行 raw 都会打印 |

**→ 即使结果是 `CPU_NOT_READY`，原始寄存器值仍然完整可见。**

### 6.2 `transition_latency=1000 ns` **不能**推出 `+0x114 = 1`

**我上一轮写过：**

> ~~「实测 `cpuinfo_transition_latency = 1000` ns …… 这说明 `+0x114` 那个寄存器
> **确实可读**且值为 1。」~~

**🔴 这条推论不成立，现撤回。**

**理由（按 Codex 指出）：**

| 问题 | 说明 |
|---|---|
| **运行的驱动二进制未与官方源码配对** | 我只读了 `xaga-s-oss` 的源码，没有证明设备上跑的 `mediatek_cpufreq_hw.ko` 就是这份源码编译的 |
| **sysfs 值可能来自别处** |  `cpuinfo_transition_latency` 是内核 `cpufreq` 核心的字段，可能被驱动写成常量、也可能来自 DT、也可能是别的路径 |
| **我没有访问过 `0x114`** | 探针白名单里没有它，我从未读该寄存器 |

**→ 正确表述：**「Android 报告 `cpuinfo_transition_latency = 1000` ns」是一个**观察值**，
**不能反推出任何未访问寄存器的内容。**

**原始观察记录保留在上一轮文档里，本条限定追加在此。**

---

## 7. 边界

**本轮只做**：校验 manifest、读 diff、复跑测试、构建、judge、strings。

**没有做**：打包、刷写、碰设备、改生产树、改候选代码、
用默认源树或 shared `hakios-kernel`。

**内核 hash 只是构建产物标识，不代表它被验证过能跑。**
