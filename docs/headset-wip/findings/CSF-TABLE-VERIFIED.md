# CSF 条目表与地址空间：本地源码逐项核实

> 2026-11-15 13:10，DSH。**每一条都引自本地 panthor 源码**（`.csf-re/src/`）
> 或我自己对固件字节的解析，不是转述子代理。
>
> 本文档修正了我此前对 CSF 条目表的四处误解。

---

## 1. 条目头 bitfield（我原来是对的）

`panthor_fw.c:94-97`：

```c
#define CSF_FW_BINARY_ENTRY_TYPE(ehdr)	((ehdr) & 0xff)
#define CSF_FW_BINARY_ENTRY_SIZE(ehdr)	(((ehdr) >> 8) & 0xff)
#define CSF_FW_BINARY_ENTRY_UPDATE	BIT(30)
#define CSF_FW_BINARY_ENTRY_OPTIONAL	BIT(31)
```

`size` 在 **[15:8]**。此前有说法称 `>>16`，**逐行核对源码后否证**。

---

## 2. 🔴 更正一：第 6 个字不是对齐值，是**名字区**

我原来把 IFACE 条目的第 6 个 32 位字当成「alignment / page size」（值总是 `0x20`）。
**这是错的。** `panthor_fw.c:609`：

```c
	name_len = iter->size - iter->offset;
```

`iter->offset` 是读完 5 个 u32 字段后的位置（= 20），所以
**名字区 = 条目剩余字节**，长度 `esize - 4 - 20`。

我实跑解析，两份固件的名字直接解出来了：

| 条目 | esize | 我原以为的"第 6 字" | 实际名字区 | 解出的名字 |
|---|---|---|---|---|
| 0 | 32 | `0x00000e94` | 8 B | **`config`** |
| 1-8 | 28 | `0x000106dc` 等 | 4 B | `" \0\0\0"` |
| 9 | 32 | `0x0003f000` | 8 B | **`shared`** |

**`0x20` 就是空格字符 `' '` + 3 个 NUL 填充。** 不是对齐值。

> 名字 `config` 对应 sec0（向量表/配置），`shared` 对应 sec9（共享区），
> 与段的实际用途完全吻合，**这条验证是自洽的**。

### 2.1 这引出一个真实的兼容性差异

| 加载器 | 规则 | 我们 v0.1 的结果 |
|---|---|---|
| **panthor** | 名字区 = 剩余字节 | ✅ 全部通过 |
| **vendor kbase** | 名字区后还要跟 `virtual_exe_start`，要求 `esize >= 0x14 + strlen(name)+1 + 4` | ❌ **10 条 IFACE 全部会被拒**（esize 28 需 26 字节但只剩 4；esize 32 需 31 但只剩 8） |

**即 MTK 的 v0.1 是按 panthor 规则打包的。**
若我们照 vendor-kbase 的加载器写，会被这条卡死。

> 我们的驱动 `hakios.mali610.c:1069-1212` 按 panthor 规则解析（没有 `virtual_exe_start`），
> **所以这条不适用**，不是当前故障原因。但值得记录。

---

## 3. 🔴 更正二：flag 权限位含义（我的文档写错过，代码是对的）

`panthor_fw.c:99-118` 原文：

```c
#define CSF_FW_BINARY_IFACE_ENTRY_RD	BIT(0)
#define CSF_FW_BINARY_IFACE_ENTRY_WR	BIT(1)
#define CSF_FW_BINARY_IFACE_ENTRY_EX	BIT(2)
#define CSF_FW_BINARY_IFACE_ENTRY_CACHE_MODE_NONE		(0 << 3)
#define CSF_FW_BINARY_IFACE_ENTRY_CACHE_MODE_CACHED		(1 << 3)
#define CSF_FW_BINARY_IFACE_ENTRY_CACHE_MODE_UNCACHED_COHERENT	(2 << 3)
#define CSF_FW_BINARY_IFACE_ENTRY_CACHE_MODE_CACHED_COHERENT	(3 << 3)
#define CSF_FW_BINARY_IFACE_ENTRY_CACHE_MODE_MASK		GENMASK(4, 3)
#define CSF_FW_BINARY_IFACE_ENTRY_PROT		BIT(5)
#define CSF_FW_BINARY_IFACE_ENTRY_SHARED	BIT(30)
#define CSF_FW_BINARY_IFACE_ENTRY_ZERO		BIT(31)
```

| 位 | 含义 | 我此前可能写错的 |
|---|---|---|
| bit0 | **READ** | — |
| bit1 | **WRITE** | — |
| bit2 | **EXECUTE** | — |
| bit[4:3] | CACHE_MODE（**两位**） | — |
| bit5 | **PROT** | 不是 bit1 |
| bit30 | **SHARED** | 我写成过 "BSS" |
| bit31 | **ZERO** | ✅ 这个对 |

**驱动代码 `hakios.mali610.c:983-990` 是对的**：

```c
#define SEC_RD          (1u << 0)
#define SEC_WR          (1u << 1)
#define SEC_EX          (1u << 2)
#define SEC_CACHE_MASK  (3u << 3)
#define SEC_CACHED      (1u << 3)
#define SEC_PROT        (1u << 5)
#define SEC_SHARED      (1u << 30)
#define SEC_ZERO        (1u << 31)
```

**逐位与 panthor 一致。** 写错只出现在我的文档描述里，**代码无需修改**。

### 3.1 六个真实 flag 的正确解读

| flags | 我原写的 | **正确** |
|---|---|---|
| `0x9` | r-- cached | RD + CACHED |
| `0xd` | r-x cached | RD + **EX** + CACHED |
| `0x80000009` | r-- cached | RD + CACHED + **ZERO** |
| `0x8000000b` | rw- cached | RD + WR + CACHED + **ZERO** |
| `0x2b` | rw- cached（PROT） | RD + WR + CACHED + **PROT(bit5)** |
| `0xc000001b` | rw- nc shared | RD + WR + **CACHED_COHERENT**(cm=3) + SHARED + ZERO |

> 注意 `0xc000001b` 的 cache mode 是 **3 = CACHED_COHERENT**，不是「nc (non-cacheable)」。
> 我此前一律标成 "nc shared" 不准确；实际由驱动按 `COHERENCY_NONE` 降级成 `IDX_NC`。

---

## 4. 🔴 更正三：MCU_STATUS 的三处用途（我说窄了）

我此前写「`MCU_STATUS` 只在超时后读一次，用于打印」。
**启动判据确实不用它，但它在别处有三处用途：**

| 用途 | 位置 | 用法 |
|---|---|---|
| 启动判据 | `panthor_fw.c:1142` | ❌ **不用**，只等 `booted` |
| 超时诊断 | `panthor_fw.c:1157` | 读来打印 `Failed to boot MCU (status=%s)` |
| **停机等待** | `panthor_fw.c:1173` | `status == MCU_STATUS_DISABLED` 轮询 |
| **halt 判定** | `panthor_fw.c:1183` | `== MCU_STATUS_HALT` |

`panthor_fw.c:1178-1189`：

```c
static bool panthor_fw_mcu_halted(struct panthor_device *ptdev)
{
	...
	halted = gpu_read(ptdev->fw->iomem, MCU_STATUS) == MCU_STATUS_HALT;
	...
}
```

**准确表述：启动成功判据不用 `MCU_STATUS`；超时诊断、停机等待、halt 判定三处用。**

---

## 5. 确认无误的项（我原来的理解成立）

| 项 | 结论 |
|---|---|
| `size` 在 `[15:8]` | ✅ |
| IFACE = 5 个 u32 字段（flags/va_start/va_end/data_start/data_end） | ✅ |
| `data_start`/`data_end` 是**文件偏移** | ✅ `panthor_fw.c:568` 校验 `hdr.data.end > fw->size` 即报截断 |
| `version` 在 shared `+0x00` | ✅ |
| 我们 v0.1 的 `+0x08=0x04002000`(input_va) / `+0x0c=0x04003000`(output_va) / `+0x10=8` / `+0x14=0xa0` | ✅ 与官方固件逐字相同 |
| 共享段声明 606208 B 但只有 8192 B 数据 | ✅ **正常**，是 ZERO 段只预置骨架 |
| `type 5` 条目 | ✅ 靠 `bit31=OPTIONAL` 被静默忽略（`panthor_fw.c:794-795`），ARM 参考固件也有，非 MTK 专有 |
| PROT 不是独立 type | ✅ 是 type-0 IFACE 的 `bit5` flag |

---

## 6. 地址空间：`0x40022000` 是 MCU 私有寄存器窗口

### 6.1 分段模型

| VA 区间 | 性质 | 走 AS0 页表？ |
|---|---|---|
| `0x00000000`-`0x3FFFFFFF` | MCU 的地址空间（含 shared region） | ✅ |
| `0x04000000`-`0x07FFFFFF` | 64 MiB shared RAM | ✅ 是 AS0 的一段 |
| **`0x40000000` 起 256 KiB** | **GPU 硬件直通 MMIO** | ❌ **不走** |

原文（icecream95，Mesa panfrost 作者，我已 fetch 通读）：

> "**From `0x40000000` is a 256 KiB region of memory mapped IO** (not all of it
> backed by real storage) which controls the hardware of the GPU. …
> The region at `0x04000000` is just another memory region, but the
> **`0x40000000` is special Device memory**."

**两个地址只差一个 `0`，务必分清：`0x04000000` 是内存，`0x40000000` 是 MMIO。**

### 6.2 三个地址的识别

| MCU VA | 主机偏移 | 寄存器 | 依据 |
|---|---|---|---|
| `0x40003034` | `0x34` | **GPU_STATUS**（RO） | `panthor_gpu_regs.h:51-55` |
| `0x40003030` | `0x30` | GPU_CMD（WO） | `panthor_gpu_regs.h:42` |
| `0x40022000` / `+4` / `+0x10` | `0x22000` | **MCU_SUBSYSTEM 私有寄存器** | `mali_kbase_regmap_legacy_csf.h:35` |

### 6.3 🔵 固件复位时读的是 `PROTM_ACTIVE`

`panthor_gpu_regs.h:51-55`（本地源码，我已核对）：

```c
#define GPU_STATUS					0x34
#define   GPU_STATUS_ACTIVE				BIT(0)
#define   GPU_STATUS_PWR_ACTIVE				BIT(1)
#define   GPU_STATUS_PAGE_FAULT				BIT(4)
#define   GPU_STATUS_PROTM_ACTIVE			BIT(7)
#define   GPU_STATUS_DBG_ENABLED			BIT(8)
```

**`and r4, #0x80` 就是取 bit7 = `PROTM_ACTIVE`（保护模式激活）。**

这与 sec6/7/8 三条 `flags 0x2b`（含 `PROT` bit5）的段**完全对得上**：
固件复位时先判断「我是不是在保护模式下启动」。

> trial 695 的 `0x3034` 读到 `0x1` = `GPU_STATUS_ACTIVE`，**bit7 = 0**。
> 按此逻辑走非保护模式分支。但**固件读它的确切时刻我们从未采样**，
> 且 `0x3034` 在本次运行中变过（0→1），所以这条仍是**未观测**，不是结论。

### 6.4 明确未证实

- `0x40022000` 写入值 `0xb` 与 `0x40022004` 写入 `7` 的**字段语义**：
  Arm 未公开，kbase 在 `0x20000`-`0x40000` 区间**一个 enum 条目都没有**。**不猜测。**
- 「`0x40000000` 起对 MCU 不走 AS0」这一点，公开源码**没有一句原话直说**。
  它由三条独立证据推出（博客描述 / kbase 标为主机禁访 / 两份固件无条件先写），
  **强度高，但仍属推断。**

---

## 7. 没有 CSF_CFG 预启动步骤

`CSF_CFG` 在 panthor 全树（`fw.c/.h`、sched、mmu、gpu、hw）**零命中**，kbase 也零命中。

`panthor_fw_init` 顺序（`panthor_fw.c:1535-1551`）：

```
panthor_fw_load          只映射 + memcpy 段数据 + dma_sync，不写 control 块
panthor_vm_active(fw->vm) 激活 AS0
panthor_fw_start         写 MCU_CONTROL=AUTO，等 booted
panthor_fw_init_ifaces     ← 之后才读 version
panthor_fw_init_global_iface  ← host 第一次写 shared 区
```

**因果是反的**：host 写 shared 区的所有配置都发生在 **version 校验之后**，
所以不存在「缺一个 pre-boot config 导致 version 不发布」。

### 7.1 启动前真正需要满足的三件事

| # | 条件 | 我们做了什么 |
|---|---|---|
| a | 段按声明 VA 映射进 AS0 | ✅ 日志 `coverage: every section fully mapped` |
| b | 激活 AS0 | ✅ 日志 `AS0 enabled: root 0x1a6d70000` |
| c | **使能并 unmask job IRQ，且清 `INT_CLEAR`** | ⚠️ 见下 |

**第 (c) 条是最常漏的**：没 unmask `JOB_INT_GLOBAL_IF` 或没清 `INT_CLEAR`，
固件置了全局中断你也看不到 —— **症状恰好是 `ENABLED` 但 version = 0。**

> 我们的情况：`JOB_INT_MASK` 回读 `0xffffffff`（全使能 ✅），
> `JOB_INT_CLEAR` 写过 `0xffffffff`（write[50] ✅）。
> 但 `JOB_INT_RAWSTAT` 本身读回 **0**，即**中断根本没被置起**，
> 不是被 mask 挡住。这条 trial 695 已排除。
