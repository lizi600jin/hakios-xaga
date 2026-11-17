# UFS 队列与 DMA 内存来历审查（716）

> 2026-11-17 13:15，DSH。**仅主机。未改候选/生产、未碰设备、未构建、未动 GPUEB。**
> **依据实际代码与 live DT，不采信旧注释里的结论性说法。**

---

## 0. 结论速览

| # | 问题 | 结论 |
|---|---|---|
| **1** | `UTRLBA = 0x51266400` 是谁的？ | **LK 自己用于 UFS 的传输请求链表基址** ✅ 代码明确 |
| **2** | 这块内存在 XNU 看来是什么？ | **无 DT 保留、位于 `0x48402000`–`0x6fffffff` System RAM 内** 🔴 |
| **3** | 会被 XNU 的 VM 复用吗？ | **❌ 不会** —— 在 `XNU_PHYS_BASE` 窗口**以下**，`pa_valid` 返回 false（§3.3）|
| **4** | loader 的 UTRD/UCD/数据 buf 在哪？ | `0x60100000` + 偏移，**也都在 XNU 窗口以下** |
| **5** | loader 返回后从 HLOG 还能取回队列吗？ | **🔴 不能** —— HLOG 只有寄存器值，无内存 dump（§5）|
| **6** | loader 的 `ufs_init` 能否搬进内核？ | **🔴 不能原样搬** —— 含 SMC/`0x140`/`0x144`/UIC（§5.1）|

---

## 1. 锚点：715 实测的三个字

**715 HLOG 原文：**

```
[18.421005]: mt6895-ufs: raw 0x50=0x51266400/0x51266400
[18.421060]: mt6895-ufs: raw 0x54=0/0
[18.421115]: mt6895-ufs: raw 0x58=0/0
[18.421177]: mt6895-ufs: raw 0x60=0x1/0x1
```

| 偏移 | 字段 | 采样值 |
|---|---|---|
| `0x50`/`0x54` | `UTRLBA`/`UTRLBAU` | **`0x51266400` / `0`** |
| `0x58` | `UTRLDBR` | **采样位全 0** |
| `0x60` | `UTRLRSR` | **run/stop 字段 = 1** |

**来源**：`device715/trial715-logbook-body.bin`（494290 B / `ce8fbf536fc7ebde…`）

---

## 2. `0x51266400` 的来历：LK 自己的链表

### 2.1 代码原文（`mtk/loader/ufs.h`，sha256 `ef61ba81e8cbac4b…`）

**第 1995 行：**

> 「LK left UTRLRSR=1 with the base at **0x51266400**, the controller reads descriptors from
> there for Android」

**第 749 行：**

> 「0x60100000 is memory this loader picked; **0x51266400 is the list LK itself was using for
> UFS on this very board**, so the controller demonstrably can reach it.」

**第 267 行：**

> 「**LK's own leftover descriptor at 0x51266400 reads `0x0d1600e0` as its first word, which is
> not a UTRD at all (it is a PRD table)**」

**`board.h:481`：**

> 「LK keeps its list at 0x51266400 and this controller accepts descriptors there」

### 2.2 所以

| 项 | 值 |
|---|---|
| 归属 | **LK（bootloader）为 Android 的 UFS 启动建立的传输请求链表** |
| 地址 | `0x51266400`，`UTRLBAU = 0`（下 4 GiB） |
| **4 KiB 对齐** | **否**（`mod 4096 = 0x400`）🔴 |
| 内容（据注释） | 第一个字是 **PRD 表**，**不是 UTRD** |

**🔴 「第一个字不是 UTRD」这条我只引用代码注释，没有自己读那块内存验证** ——
**我没有设备侧读该地址的证据，也没有任何一次 dump。**

---

## 3. 这块内存在 XNU 看来的归属

### 3.1 `board.h` 声明的 System RAM 范围

**`mtk/mt6895/loader/board.h:483-484`：**

> 「0x60100000 is inside **0x48402000-0x6fffffff (System RAM)**, above chain_stub's own page at
> 0x60000000, and below the first reservation in that block (0x66f00000).」

**核对：**

| 地址 | 在 `0x48402000`–`0x6fffffff` 内 |
|---|---|
| **`0x51266400`（LK 链表）** | **✅ 是** |
| `0x60100000`（loader 区域） | ✅ 是 |

### 3.2 live DT 里没有保留它

**`xaga-live.dts`（`f47d073b92b76209…`）唯一的近邻保留块：**

| 节点 | 行 | 范围 |
|---|---|---|
| **`mblock-11-aee_lk`** | **1152-1155** | **`0x50700000`–`0x50efffff`**（`0x800000`）|

**`0x51266400` **不在** `0x50700000`–`0x50efffff` 内**（它比 `0x50f00000` 高 `0x366400`）。

**我 grep 了整个 live DT 找 `0x51266400` —— 零命中。**

### 3.3 🔴 重要纠正：Android 的 System RAM **不是** XNU 的可分配范围

**我先前把 `board.h` 里「System RAM」的说法当成了 XNU 的 VM 可分配范围 —— 那是错的。**
**下面用实际代码纠正。**

#### 3.3.1 XNU 拿到的物理窗口

**`board.h:122-127`（715 冻结版）：**

```c
#ifndef XNU_PHYS_BASE
#define XNU_PHYS_BASE           0x100000000ULL
#endif
#ifndef XNU_MEM_SIZE
#define XNU_MEM_SIZE            0x100000000ULL
#endif
```

**`mtkloader.c:3591-3594` 把它们写进 boot args：**

```c
ba->physBase      = XNU_PHYS_BASE;
ba->virtBase      = KERNEL_VIRT_BASE;
ba->memSize       = XNU_MEM_SIZE;
ba->memSizeActual = XNU_MEM_SIZE;
```

#### 3.3.2 XNU 侧怎么用它

**`osfmk/arm64/arm_vm_init.c:1832`：**

```c
gPhysBase = args->physBase;
```

**同一文件第 1847 行：**

```c
gPhysSize = mem_size = ((gPhysBase + args->memSize) & ~PAGE_MASK) - gPhysBase;
```

**`osfmk/arm/pmap/pmap.c:2209-2210`：**

```c
vm_first_phys = gPhysBase;
vm_last_phys  = trunc_page(avail_end);
```

**`osfmk/arm/pmap/pmap_data.h:75-78`（`pa_valid`）：**

```c
static inline bool
pa_valid(pmap_paddr_t pa)
{
	return (pa >= vm_first_phys) && (pa < vm_last_phys);
}
```

**`osfmk/arm/pmap/pmap.c:1743`（`pmap_valid_address`）：**

```c
boolean_t pmap_valid_address(pmap_paddr_t addr) { return pa_valid(addr); }
```

#### 3.3.3 算出来的结论

| 区间 | 值 |
|---|---|
| **`vm_first_phys`** | **`0x100000000`**（= `XNU_PHYS_BASE`）|
| XNU 管理的上限 | `0x100000000 + 0x100000000` = **`0x200000000`** |
| **XNU 的物理窗口** | **`0x100000000`–`0x1FFFFFFFF`** |

**两个 UFS 地址与这个窗口的关系：**

| 地址 | 值 | 在 XNU 窗口内 | `pa_valid` |
|---|---|---|---|
| **LK 链表 `UTRLBA`** | `0x51266400` | **❌ 在窗口**以下** | **false** |
| loader UTRD/UCD/buf | `0x60100000`–`0x6090dfff` | **❌ 在窗口以下** | **false** |

**🔴 所以：**
**`0x51266400` 不在 `[vm_first_phys, vm_last_phys)` 区间内，
`pmap_valid_address()` 对它返回 `false`。**

**→ XNU 的 VM 不会把它当作可分配页。我先前说的「会被 VM 复用」这个风险**不成立**，撤回。**

**`board.h` 里那句「System RAM」是**Android/Linux 视角的** iomem 说法，
**不是 XNU 的 `memSize` 范围** —— 这正是我混淆的地方。**

---

## 4. loader 侧自己在用的三块内存

**`mtk/loader/ufs.h`（sha256 `ef61ba81…`）第 428-440 行：**

```c
#define UFS_AREA_LIST   ((volatile uint32_t *)(uintptr_t)UFS_AREA_PA)
#define UFS_AREA_UCD    ((uint8_t *)(uintptr_t)(UFS_AREA_PA + UFS_UCD_OFFSET))
#define UFS_AREA_BUF    ((uint8_t *)(uintptr_t)(UFS_AREA_PA + UFS_BUF_OFFSET))

#ifndef UFS_AREA_PA
#define UFS_AREA_PA           0x60100000ULL
#endif
#define UFS_UCD_OFFSET        0x1000u
#define UFS_BUF_OFFSET        (UFS_UCD_OFFSET + UFS_SLOTS * UCD_SIZE)
#define UFS_AREA_SIZE         (UFS_BUF_OFFSET + UFS_SLOT_BUFFER)
```

**布局（`UFS_SLOTS = 32`，`UCD_SIZE` 由 `UCD_PRDT + 16 * (...) ` 算出）：**

| 区域 | 基址 | 大小 |
|---|---|---|
| **UTRD 链表** | **`0x60100000`** | `32 × 32` = **1 KiB**（第 436 行注释「the list is 1 KB」）|
| **UCD 数组** | **`0x60101000`** | `32 × UCD_SIZE` |
| **数据缓冲** | `0x60100000 + 0x1000 + 32×UCD_SIZE` | `UFS_SLOT_BUFFER` |

**🔴 三块都在 `0x60100000` 起的同一条上，也都在 §3.1 的 System RAM 内。**

### 4.1 与 LK 链表的关系

**`UFS_USE_LK_LIST` 分支（第 1987 行起）下：**

- **UTRD 用 LK 的**（`ufs_slots[i].utrd = base + i * 32`，`base` 来自读到的 `UTRLBA`）
- **UCD 与数据 buf 仍在 `0x60100000+`**（第 2003-2004 行注释：「The UCD and the data buffer
  stay where they are (0x60100000+), because the area matrix proved the controller can reach
  that base」）

**→ 所以一次传输会同时用到 LK 的链表（`0x51266400`）和 loader 的 UCD/buf（`0x60100000+`）。**

**两处都在无保留的 System RAM 里。**

---

## 5. 🔴 loader 返回后还能从 HLOG 取回队列吗 —— **不能**

### 5.1 为什么不能

| 环节 | 情况 |
|---|---|
| HLOG 记的是什么 | **loader 的 `out()` 文本**，不是内存 dump |
| 队列内容在哪 | **物理内存 `0x51266400`** |
| HLOG 里有没有该地址的字节 | **没有** —— 没有任何 dump 机制 |
| XNU 启动后该内存是否还是原值 | **🔴 未知**，可能被 VM 复用 |

**→ HLOG 只能告诉我们「UTRLBA 当时是多少」，不能告诉我们链表里现在是什么。**

### 5.2 我在 715 的 HLOG 里实际查过

**715 HLOG（494290 B）里 `mt6895-ufs` 相关只有 8 条消息 + 9 对 raw，
全部是寄存器采样值，没有任何内存 dump。**

---

## 5.1 🔴 loader 的 `ufs_init` **不是只读接管** —— 不能原样搬进内核

**`ufs.h` 的头注释给人「只是接管链表、发只读 SCSI」的印象。
我查了 715 的 `-E -P` 预处理输出（`host-review/loader715-preprocessed.c`，148531 B / `b37349ce…`），
**实际代码远超这个描述**。**

### 5.1.1 三个实际的副作用路径

| # | 位置（E-P 行号） | 实际动作 |
|---|---|---|
| **1** | **551-575** | **`ufs_smc()` 走 `smc #0`**，功能号 **`0xC2000276`**，用于 `ufs_mtk_ref_clk_notify` 与 `ufs_mtk_mtcmos_ctrl` |
| **2** | **1123-1137** | **写 `0x140`（`ufs_mtk_xoufs`）与 `0x144`（refclk）** |
| **3** | **800-838** | **`ufs_uic_cmd4()` 发 UIC 命令**（写 `0x90` cmd / `0x94`/`0x98`/`0x9c` 参数，轮询 `0x20`）|

**代码原文（551-575）：**

```c
struct ufs_smc_ret { uint64_t a0, a1, a2, a3; };
static struct ufs_smc_ret ufs_smc(uint64_t fn, uint64_t a1, uint64_t a2, ...)
{
	__asm__ volatile("smc #0" ...);
}
static struct ufs_smc_ret ufs_mtk_ref_clk_notify(uint64_t on, uint64_t stage)
{
	return ufs_smc(0xC2000276ULL, (1ULL << 3), on, stage, 0, 0, 0);
}
static struct ufs_smc_ret ufs_mtk_mtcmos_ctrl(uint64_t on)
{
	return ufs_smc(0xC2000276ULL, (1ULL << 9), on, 0, 0, 0, 0);
}
```

**代码原文（1123-1137）：**

```c
ufs_mtk_xoufs_before = ufs_rd(0x140);
ufs_wr(0x140, ufs_mtk_xoufs_before | (1u << 7));
ufs_mtk_xoufs_after = ufs_rd(0x140);
...
ufs_mtk_refclk_before = ufs_rd(0x144);
ufs_wr(0x144, (1u << 0));
```

### 5.1.2 这意味着什么

| 项 | 事实 |
|---|---|
| **头注释说** | 「只接管链表，发 READ-ONLY SCSI」 |
| **实际有** | **SMC 调用**（安全监控器，`0xC2000276`）|
| | **时钟寄存器写入**（`0x140` bit7、`0x144` bit0）|
| | **UIC 命令**（链路层控制）|
| **后果** | **这套 `ufs_init` 带硬件副作用，不能整段搬进 XNU** |

**🔴 用户指出的这条我完全确认：715 的 `-E -P` 里 `ufs_init` 确实包含
SMC / `0x140` / `0x144` / UIC 初始化，头注释不可信。**

### 5.1.3 对「接管 UFS」的含义

**若要给 XNU 一个 UFS 驱动，只能从 `ufs_init` 里**挑出**安全的子集**，例如：

| 可分 | 不可分 |
|---|---|
| 读 `UTRLBA`/`UTRLRSR` 等寄存器 | **SMC 调用** |
| 构造 UTRD/UCD（不写入控制器） | **`0x140`/`0x144` 时钟写** |
| 发只读 SCSI（若有明确授权） | **UIC 命令**（会改链路状态）|

**🔴 而「安全子集」的边界需要单独审查，不能靠头注释判断。**

---

## 6. 未证实项（我明确列出）

| # | 未证实 | 为什么 |
|---|---|---|
| **1** | `0x51266400` 那块内存**现在**是什么 | **没有任何设备侧 dump** |
| ~~**2**~~ | ~~XNU 是否真的把这块 RAM 分配出去了~~ | **已由 §3.3 代码排除：`pa_valid` 返回 false，VM 不分配窗口以下的地址** |
| **3** | LK 链表第一个字是 PRD 表 | **只引用 `ufs.h:267` 的注释**，我没读内存验证 |
| **4** | LK 的链表有多大、多少 slot 有效 | 未见 LK 源码里对该链表的构造 |
| **5** | `0x51266400` 为什么不是 4 KiB 对齐 | **未知** —— `mod 4096 = 0x400`，可能 LK 用了非页对齐的静态分配 |
| **6** | UCD 的 `UCD_SIZE` 确切值 | `UCD_PRDT + 16 * (UFS_SLOT_BUFFER / UFS_PRD_MAX + 1)`，**需算 `UFS_SLOT_BUFFER` 与 `UCD_PRDT` 才能确定** |
| **7** | 控制器能否 DMA 到 4 GiB 以上 | **`board.h` 说 UFS 主控只读得到的窗口，但没给 DMA 地址宽度** |
| **8** | `UTRLDBR` 采样为 0 的语义 | 见前一轮更正：**只是采样位全 0**，不推断在途请求 |

---

## 7. 现在的真正阻塞（已按 §3.3 纠正重写）

### 🔴 阻塞 1：`ufs_init` 带硬件副作用，不能整段搬

**见 §5.1** —— SMC、`0x140`/`0x144` 时钟写、UIC 命令。

**要写 XNU 的 UFS 驱动，必须先划出「哪些步骤安全」的边界，而这不是靠注释能定的。**

### 🔴 阻塞 2：LK 链表的**结构与有效性**未知

**`0x51266400` 是 LK 的数据，我们没有 LK 源码。**

**我不知道：**
- 那张链表有多少 slot 有效
- 每个 UTRD 现在指向什么
- LK 是否在跳转前清过它

**→ 这需要设备侧读内存，而当前探针白名单只有 9 个 UFS 寄存器。**

### 🟠 阻塞 3：XNU 从未给这块区域建过映射

**`pa_valid()` 对它返回 false，意味着一件事：
`ml_io_map()` 能不能映射它、映射后能不能安全读，**需要单独验证**。**

**我读过 715 的 `-E -P`，但没有追 `ml_io_map` 对低于 `physBase` 地址的处理路径。**

### ✅ 已解除的疑虑（原阻塞 2）

**原写「XNU 可能把这 4 GiB 以下 RAM 当可用页」—— §3.3 已用代码证明**不会**：
`vm_first_phys = 0x100000000`，`pa_valid` 对 `0x51266400` 返回 false。**

**这条不再是阻塞。**

---

## 8. 我的判断（明确标注是判断）

**基于 §3.3 的纠正，情况比原先想的**好**：**

| 序 | 判断 | 依据 |
|---|---|---|
| **1** | **按 `pa_valid` 的判据，XNU 的 VM 不把它当可分配页** | 代号：`vm_first_phys = 0x100000000 > 0x51266400`（§3.3）|
| **2** | 所以 LK 留下的链表**可能仍然是完好的** | 逻辑上，未被分配 ≠ 未被改写，但至少 VM 不会主动碰它 |
| **3** | 但**「可能完好」不等于「一定完好」** | 我没有读那块内存的证据 |

**真正要做的是：**

| 序 | 做什么 | 前提 |
|---|---|---|
| **1** | **单独审查 `ufs_init` 里哪些写是安全的** | 见 §5.1，这是最硬的阻塞 |
| **2** | 一个**明确授权的只读内存探针**，读 `0x51266400` 附近 | 需要新 CMD |
| **3** | 验证 `ml_io_map` 对低于 `physBase` 的地址能否安全映射 | 主机可做 |

**🔴 我的结论：**
**原先「VM 可能复用」的风险**已被代码排除**；
**但「LK 链表现在是什么」仍然未知，而且 `ufs_init` 不能整段复用这个阻塞更根本。**

---

## 9. 我读过的材料与 hash

| 材料 | 路径 | 大小 | SHA256 |
|---|---|---|---|
| **loader UFS 实现** | `trial715/loader-src/mtk/loader/ufs.h` | 141239 B | **`ef61ba81e8cbac4b5fbb0412c95211056f6f9c00296a134a2066d3298404586c`** |
| **板级常量与 DRAM 说明** | `trial715/loader-src/mtk/mt6895/loader/board.h` | 48016 B | 见归档 |
| **live DT** | `docs/headset-wip/raw/dapm-preflight-20261115/xaga-live.dts` | 513480 B | `f47d073b92b76209…` |
| **715 HLOG body** | `device715/trial715-logbook-body.bin` | 494290 B | `ce8fbf536fc7ebde…` |

---

## 10. 边界

**未做**：设备操作、内存读、构建、刷写、改候选/生产、动 GPUEB。

**§3.3 的「会被 VM 复用」是**推断**，依据是「在 System RAM 内 + DT 无保留 + 内核在 4 GiB 以上」，
**我没有 XNU 分配日志或页表 dump 来证明它实际发生了。**

**§6 的八条未证实项都是真缺口，不是表述谨慎。**

---

## 11. 本轮的两次更正（用户指出）

### 更正 1：把 Android 的 System RAM 当成了 XNU 的可分配范围 ❌

**我原来写：**

> ~~「`0x51266400` 在 `0x48402000`–`0x6fffffff` System RAM 内 + DT 无保留
> → XNU 的 VM 可能把它分配给任何内核分配请求」~~

**错在：** **`board.h` 那句「System RAM」是 Android/Linux 的 iomem 说法，
不是 XNU 的 `memSize` 范围。**

**实际：**

| 项 | 代码位置 | 值 |
|---|---|---|
| `XNU_PHYS_BASE` | `board.h:123` | **`0x100000000`** |
| `XNU_MEM_SIZE` | `board.h:126` | **`0x100000000`** |
| 写入 boot args | `mtkloader.c:3591-3594` | `ba->physBase` / `ba->memSize` |
| XNU 取用 | `arm_vm_init.c:1832/1847` | `gPhysBase`、`gPhysSize` |
| VM 边界 | `pmap.c:2209-2210` | `vm_first_phys = gPhysBase` |
| 判据 | `pmap_data.h:75-78` | `pa_valid = (pa >= vm_first_phys) && (pa < vm_last_phys)` |

**→ XNU 窗口是 `0x100000000`–`0x1FFFFFFFF`，两个 UFS 地址都在**以下**，
`pmap_valid_address()` 返回 `false`。**

**「会被 VM 复用」这个风险**不成立，已撤回**。**

### 更正 2：`ufs_init` 不是只读接管 ❌

**我原来（以及 `ufs.h` 头注释）给人的印象是「只接管链表、发只读 SCSI」。**

**实际在 715 的 `-E -P` 输出里查到：**

| 行 | 内容 |
|---|---|
| **551-575** | **`ufs_smc()` → `smc #0`，功能号 `0xC2000276`** |
| **1123-1137** | **写 `0x140`（bit7）与 `0x144`（bit0）** |
| **800-838** | **`ufs_uic_cmd4()` UIC 命令**（写 `0x90`/`0x94`/`0x98`/`0x9c`）|

**→ 这套 `ufs_init` 带硬件副作用，不能整段搬进 XNU。头注释不可信。**

**这条是比我原先列的所有阻塞都更根本的一条。**
