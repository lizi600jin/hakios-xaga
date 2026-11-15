# CSF MCU 是 32 位 Cortex-M7，不是 AArch64

> 2026-11-15 12:35，DSH。**离线可复现**，不需要刷机。
>
> 🔴 **口径更正（Codex 2026-11-15 12:25 指出）**：本文档最初写「这是本项目第一次确定
> MCU 的指令集架构」。**这个说法过强，应改为「对已有结论的复核与扩证」。**
> `hakios-mtk/work-notes-csf-boot-others.md:420-445` **早已有同一入口的 capstone 反汇编**
> （`cpsid i` / `0x40022000=0xb` / `0xE000ED24 (SCB->SHCSR)` 都标了），
> 驱动源码 `:1365` 的 witness 注释也写了「32 bytes of Cortex-M7」——但那是**我们自己的
> 实现记录，不能当作硬件识别证明**（见 §4 证据分层）。
> 本文档新增的是：**官方固件交叉验证、完整 PPB 扫描定为 M7、以及几处旧误判的修正**。
>
> **本次修订（13:45）按 Codex 裁定改了 5 处口径：**
> 1. 标题「Cortex-M」→「**Cortex-M7**」，并给出完整 PPB 扫描证据（13 个常量，非 3 个）
> 2. 删掉「本项目第一次确定」，改为「复核与扩证」
> 3. 「完整复位处理器逐条相同」→「关键开头序列相同」，并列出 `SCR=2` 的真实差异
> 4. §11.5「`0x40022000` 未映射 ⇒ 固件失败」**整个推理作废**（MMIO 不走页表）
> 5. §11.6「双 marker 写 `0x40022000`」**方案作废**（配置寄存器，不是可读 marker RAM）
> 反汇编用 **capstone 5.0.7**（`CS_ARCH_ARM, CS_MODE_THUMB|CS_MODE_MCLASS`），**不再手工解码**。
> 输入：`drivers/gpu/hakios.mali610_fw.h`（258048 B，sha256 `de22d2fc…`）。

---

## 0. 一句话

复位向量 `pc = 0x010018ed` 的 **bit0 = 1**（ARMv7-M Thumb 标志）；
从 `0x010018ec` 反汇编出 `cpsid i` / `msr psp` / `msr control` / `isb sy` /
`cpsid f` —— 这些是 **M-class 专有指令**，AArch64 一条都没有。
字面量池里有 `0xE000ED24` = **SCB->SHCSR**，是 Cortex-M 私有外设空间。

**结论：CSF MCU 是 32 位 Cortex-M 类核。**

---

## 1. ⚠️ 本文档修正了同一天早些时候的一个错误版本

我第一版是**手工解码**的，其中两条指令的立即数/操作数**解错了**（`f04f`/`f44f` 的
modified-immediate 位域拆错）。真反汇编（capstone）给出的正确结果：

| 我先前写的（**错**） | capstone 反汇编（**对**） |
|---|---|
| `f44f 21e0  mov.w r1, #0x2e0` | `f44f e021  mov.w r1, #0x70000` |
| `f04f 010b  mov.w r1, #0xb` （Rd 也错） | `4ff00b01  mov.w r1, #0xb`（字节序：`f04f 010b` 对应 `4f f0 0b 01`） |
| 未识别 | `80f30988  msr psp, r0` |
| 未识别 | `80f31488  msr control, r0` |
| 未识别 | `bff36f8f  isb sy` |

**教训：手工拆 Thumb-2 位域不可靠，必须用反汇编器。** 已装 `capstone 5.0.7`。

---

## 2. 复位处理器的完整反汇编

`sec0[0:4] = 0x02040000`（初始 SP，等于段 2 VA 末尾，与日志 `sp 0x2040000` 一致）
`sec0[4:8] = 0x010018ed`（复位入口，**bit0 = 1**，真实地址 `0x010018ec`）

```
0x010018ec: 72b6        cpsid    i            ; 关 PRIMASK —— Cortex-M 复位固定第一条
0x010018ee: 2648        ldr      r0, [pc, #0x98]   ; -> 0x40022000
0x010018f0: 4ff00b01    mov.w    r1, #0xb
0x010018f4: 0160        str      r1, [r0]          ; *(0x40022000) = 0xb
0x010018f6: 2548        ldr      r0, [pc, #0x94]   ; -> 0x40022004
0x010018f8: 4ff00701    mov.w    r1, #7
0x010018fc: 0160        str      r1, [r0]          ; *(0x40022004) = 7
0x010018fe: 2448        ldr      r0, [pc, #0x90]   ; -> 0xe000ed24
0x01001900: 4ff4e021    mov.w    r1, #0x70000
0x01001904: 0160        str      r1, [r0]          ; SCB->SHCSR = 0x70000
0x01001906: 2348        ldr      r0, [pc, #0x8c]   ; -> 0x0203f000
0x01001908: 80f30988    msr      psp, r0           ; PSP = 0x0203f000
0x0100190c: 4ff00200    mov.w    r0, #2
0x01001910: 80f31488    msr      control, r0       ; 切到 PSP（2 = 使用 PSP + 特权）
0x01001914: bff36f8f    isb      sy
0x01001918: 1f48        ldr      r0, [pc, #0x7c]   ; -> 0x40003034
0x0100191a: 0468        ldr      r4, [r0]          ; r4 = *(0x40003034)
0x0100191c: 04f08004    and      r4, r4, #0x80
0x01001920: 2046        mov      r0, r4
0x01001922: 01f000fc    bl       #0x1003126
0x01001926: 00f057f8    bl       #0x10019d8
0x0100192a: 00f05af8    bl       #0x10019e2
```

字面量池（`sec1` 内）：

| VA | 值 | 用途 |
|---|---|---|
| `0x1001988` | `0x40022000` | 第 1 个 str 目标 |
| `0x100198c` | `0x40022004` | 第 2 个 str 目标 |
| `0x1001990` | **`0xe000ed24`** | **SCB->SHCSR** |
| `0x1001994` | `0x0203f000` | PSP 初值（段 3 的 VA 起点） |
| `0x1001998` | `0x40003034` | 第 4 次读的目标 |

---

## 3. 三重架构证据

### 3.1 M-class 专有指令（capstone 全固件扫描）

| 指令 | 命中数 | 首次出现 |
|---|---|---|
| `cpsid i` | 4 | `0x10018ec`（复位第一条） |
| `cpsid f` | 2 | `0x1001c26` |
| `cpsie i` | 1 | `0x1003034` |
| `msr control` | 1 | **`0x1001910`** |
| `msr psp` | 1 | **`0x1001908`** |
| `mrs r0`（读 IP SR） | 2 | `0x10019b6` |

`cpsid` / `cpsie` / `msr control` / `msr psp` 都是 **ARMv7-M / ARMv8-M 专有**。
AArch64 里没有 `CONTROL`、`PSP`、`PRIMASK` 这些寄存器（对应的是 `DAIF`、`SPSel` 位、`SP_EL0`）。

### 3.2 Cortex-M 私有外设空间（`0xE000E000–0xE000EFFF`）

全固件扫描命中 **3 个，全部是 SCB**，没有遗漏也没有多出：

| 常量 | 寄存器 | 文件偏移 |
|---|---|---|
| `0xe000ed24` | **SCB->SHCSR** | `0x2824` |
| `0xe000ed14` | **SCB->CCR** | `0x2a60` |
| `0xe000ed0c` | **SCB->AIRCR** | `0x2af4` |

AArch64 的系统寄存器走 `MSR/MRS` 编码，**没有内存映射的 SCB**。

### 3.3 复位地址对齐

`0x010018ed` 的 bit0 = 1。AArch64 指令必须 4 字节对齐（bit0、bit1 都为 0），
`0x…ed` **不可能是 AArch64 入口**。ARMv7-M 里 bit0 = 1 只表示「目标是 Thumb」。

---

## 4. 🔴 顺带发现一条待查线索（**未证明，不要当结论**）

复位处理器**第一批 store 的目标**是：

- `0x40022000` ← `0xb`
- `0x40022004` ← `0x7`

而 trial 695 日志里，驱动的页表走查把这两个地址所在的区域报成**未映射**：

```
mali: walk 0x40000000: L0 0x19ddb8403 L1 0 table unset
mali: walk 0x40022000: L0 0x19ddb8403 L1 0 table unset
```

`trial695-full.log` L2927、L2928。

**这两个是驱动 `vaList[]`（`hakios.mali610.c:1228-1229`）里故意留的「探针」地址，
用途是验证走查能正确报告未映射。** 但固件复位代码真的往 `0x40022000` 写。

三种可能，目前**无法区分**：

| 可能 | 含义 | 需要什么才能证实 |
|---|---|---|
| A | 这两个 VA 由**硬件旁路**映射（不经 MMU），是 GPU 寄存器窗口 | 找 CSF 的 GPU 寄存器窗口基址文档 |
| B | 固件走 `0x40000000` 区是**物理地址直通**，与页表无关 | 同上 |
| C | 真的取不到，`str` 静默失败 —— 与「无 fault、状态卡 1」吻合 | 需要单变量试验：把 `0x40022000` 显式映射后重试 |

**注意**：这条与「MCU 不执行」可能直接相关，但也可能完全无关。
在拿到 CSF 的地址空间布局依据之前，**不许写成结论**。

---

## 5. 这修正了什么 / 没修正什么

| 项 | 结论 |
|---|---|
| CSF MCU 架构 | **32 位 Cortex-M**（已证明） |
| 复位 PC 对齐语义 | bit0 是 Thumb 标志，真实入口 `0x…ec`（已证明） |
| 页表 / 段加载 / `MCU_CONTROL` 语义 | **不受影响**，那是 GPU 的 MMU 与控制寄存器 |
| 「固件不执行」的因果 | **本文档不解释**。§4 那条线索未证明 |

### 型号：Cortex-M7 是**侧证**，不是从常量唯一识别的（三处修正）

Codex 2026-11-15 12:45 指出我这一节有三处具体错误，我逐条复核后**全部确认他对**。

#### 修正 1：13 个常量不是全在 `0x2a58`-`0x2a80`

我原来写「全部位于 `0x2a58`-`0x2a80` 一段连续字面量池内」。实测：

| 位置 | 数量 |
|---|---|
| 字面量池 `0x2a58`-`0x2a80` | **11 个** |
| `0xe000ed24` SCB->SHCSR @ `0x2824` | 1 个（**独立**） |
| `0xe000ed0c` SCB->AIRCR @ `0x2af4` | 1 个（**独立**） |

**11 + 2 = 13。** 另两个散在别处，不在该池内。

#### 修正 2：寄存器名整张表偏移了 4 字节（我错得最严重的一处）

按 ARM 官方 CMSIS `core_cm55.h` 的 SCB 结构定义，
SCB base = `0xE000ED00`，缓存维护寄存器 offset 如下：

| offset | 寄存器 | 真实地址 | 我先前写的 | 对错 |
|---|---|---|---|---|
| `0x250` | ICIALLU | `0xE000EF50` | ICIALLU | ✅ |
| `0x258` | ICIMVAU | `0xE000EF58` | ICIMVAU | ✅ |
| `0x25C` | DCIMVAC | `0xE000EF5C` | DCIMVAC | ✅ |
| `0x260` | DCISW | `0xE000EF60` | DCISW | ✅ |
| `0x264` | **DCCMVAU** | `0xE000EF64` | — （**不在本次 13 项里**） | — |
| `0x268` | **DCCMVAC** | `0xE000EF68` | 我写成 DCCMVAU | ❌ |
| `0x26C` | **DCCSW** | `0xE000EF6C` | 我写成 DCCMVAC | ❌ |
| `0x270` | **DCCIMVAC** | `0xE000EF70` | 我写成 DCCSW | ❌ |
| `0x274` | **DCCISW** | `0xE000EF74` | 我写成 DCCIMVAC | ❌ |

**后四个全错。** 我们固件里出现的是 `EF50/58/5C/60/68/6C/70/74`
共 8 个，**缺 `EF64`(DCCMVAU)**，而 `EF74` 是 **DCCISW**，不是 DCCIMVAC。

#### 修正 3：这些常量**不是 M7 独占**

ARM 官方 CMSIS `core_cm55.h`（我 fetch 了原文）在 Cortex-M55 上也定义
**完全相同的地址和字段**：

> <https://github.com/ARM-software/CMSIS_6/blob/main/CMSIS/Core/Include/core_cm55.h>
> `__OM uint32_t ICIALLU;  /*!< Offset: 0x250 ... */`
> …… `__OM uint32_t DCCISW; /*!< Offset: 0x274 ... */`

所以：

| 我原来声称 | 实际 |
|---|---|
| 「这是 Cortex-M7 独有的缓存维护寄存器」 | ❌ **M55 也有同一批**，M 系列带缓存的核共用这套布局 |
| 「所以型号是 M7」 | ❌ 静态常量**不能**唯一识别型号 |
| 「所以本机有 D-cache」 | ❌ 静态代码**不能证明**本机 D-cache 存在或已开启 |

#### 那么「Cortex-M7」这个结论还站得住吗？

**站得住，但依据必须分层。** Codex 2026-11-15 12:50 指出这一点，我同意：

### 证据分层（不要把自家注释当硬件识别证明）

| 层级 | 来源 | 内容 | 能否作为「这颗核是什么」的证据 |
|---|---|---|---|
| **外部依据 ①** | `panthor_mmu.c:2890`（上游 Linux 内核） | `/* CSF MCU is a cortex M7, and can only address 4G */` | ✅ 上游对 CSF 的独立判断 |
| **外部依据 ②** | Icecream95 对 **G610**（RK3588）的实测研究 | 「The MCU ... is ... a Cortex-M7 (r1p2, with no FPU, ECC cache, nor TCM), which I have found runs off the GPU clock ... 990 MHz on RK3588」 | ✅ 同型号 GPU 的实测 |
| **实现记录**（**不算佐证**） | `hakios.mali610.c:1365` 自家注释 | 「32 bytes of Cortex-M7, assembled by clang for armv7m」 | ❌ **这是我们自己写下的，不能反过来证明硬件** |

**所以准确表述是：两项外部依据 + 一项实现记录。**

自家注释只能说明**我们当时写代码时的理解**；用它当「Cortex-M7」的证明是循环论证。
它可以证明的是「witness 代码是按 armv7m 目标汇编的」，仅此而已。

**常量本身能证明的只有**：「是 M-profile，且代码里出现缓存维护操作」。
**不能**定型号（M55 也有同一批地址，见修正 3），
**也不能**证明本机 D-cache 存在或已开启 —— 这一点**仍未实测**。

#### 对缓存假设的影响：撤回

我原来写「`DCCMVAC`/`DCCIMVAC` 的存在说明 M7 侧 D-cache 真实存在且需要维护」。
**这个推论撤回** —— 固件里出现缓存维护寄存器地址，只说明代码**准备**处理缓存，
不能证明这颗核真的有 D-cache 或已开启。Icecream95 的描述里 G610 的 M7
甚至写的是「no FPU, ECC cache, nor TCM」，缓存配置本身需独立证据。

---

## 5. 这修正了什么 / 没修正什么

| 项 | 结论 |
|---|---|
| CSF MCU 架构 | **32 位 Cortex-M**（已证明） |
| 复位 PC 对齐语义 | bit0 是 Thumb 标志，真实入口 `0x…ec`（已证明） |
| 页表 / 段加载 / `MCU_CONTROL` 语义 | **不受影响**，那是 GPU 的 MMU 与控制寄存器 |
| 「固件不执行」的因果 | **本文档不解释**。§4 那条线索未证明 |

### 型号已定：**Cortex-M7**（原先写「型号未定」是错的）

Codex 2026-11-15 12:25 指出我原来只查了 3 个指定值，不能证明全区只有三处。
我做**完整 4 字节对齐 PPB 区扫描**（`0xE000E000`-`0xE000EFFF`），命中 **13 个**，
全部位于 `0x2a58`-`0x2a80` 一段连续字面量池内（紧跟 `bx lr` 之后）：

| 常量 | 寄存器 | 含义 |
|---|---|---|
| `0xe000ed0c` | SCB->AIRCR | 应用中断与复位控制 |
| `0xe000ed14` | SCB->CCR | 配置与控制 |
| `0xe000ed24` | SCB->SHCSR | 系统异常控制与状态 |
| `0xe000ed80` | NVIC SHPR 区 | 系统异常优先级 |
| `0xe000ed84` | **CPACR** | 协处理器访问控制 |
| `0xe000ef50` | **ICIALLU** | I-cache 全失效到 PoU |
| `0xe000ef58` | **ICIMVAU** | I-cache 按 MVA 失效 |
| `0xe000ef5c` | **DCIMVAC** | D-cache 按 MVA 失效到 PoC |
| `0xe000ef60` | **DCISW** | D-cache 按 set/way 失效 |
| `0xe000ef68` | **DCCMVAC** | D-cache 按 MVA clean 到 PoC（offset `0x268`） |
| `0xe000ef6c` | **DCCSW** | D-cache 按 set/way clean（offset `0x26C`） |
| `0xe000ef70` | **DCCIMVAC** | D-cache clean+invalidate 按 MVA 到 PoC（offset `0x270`） |
| `0xe000ef74` | **DCCISW** | D-cache clean+invalidate 按 set/way（offset `0x274`） |

> 表中地址按 ARM `core_cm55.h` 的 SCB offset 换算（base `0xE000ED00`）。
> 注意 `0xE000EF64`(DCCMVAU) **不出现在本固件的 13 个常量里**。

**`0xE000EF50`-`0xE000EF7F` 在 ARMv7-M 里是「实现定义」区。**
⚠️ **但不能说「M7 独占」** —— ARM 官方 `core_cm55.h` 在 Cortex-M55 上
定义了**完全相同的地址和字段**。这批常量只能说明「代码里有缓存维护操作」。

> 所以型号不只是「某个 Cortex-M」，而是 **M7 类、且带 D-cache 的核**。
> 这与 panthor 源码注释 `/* CSF MCU is a cortex M7 */`（`panthor_mmu.c:2890`）
> 以及 icecream95 原文「a Cortex-M7 (r1p2, with no FPU, ECC cache, nor TCM)」一致。

**这条对缓存假设有直接影响**：固件里存在 `DCCMVAC`/`DCCIMVAC` 这类
「按 MVA clean 到 PoC」的操作，说明 **M7 侧的 D-cache 是真实存在且需要维护的**。
「段数据缓存」假说此前用 `mali_fwnc=1`（trial 604）测过并失败，
但那是在旧映射缺陷下测的，严格说仍未在修复后的基线上重测。

---

## 6. 可复现命令

```sh
cd ~/IOS/hakios-mtk
python3 - <<'PY'
import re, hashlib
from capstone import *
src = open('drivers/gpu/hakios.mali610_fw.h').read()
m = re.search(r'kMaliFirmware\[\w+\] = \{(.*?)\};', src, re.S)
d = bytes(int(x,16) for x in re.findall(r'0x([0-9a-f]{2})', m.group(1)))
print('len', len(d), 'sha256', hashlib.sha256(d).hexdigest()[:16])

sec0, sec1 = d[896:3732], d[0xe94:0x106dc]
sp  = int.from_bytes(sec0[0:4],'little')
pc  = int.from_bytes(sec0[4:8],'little')
print('SP %#x  PC %#x  bit0=%d' % (sp, pc, pc & 1))

md = Cs(CS_ARCH_ARM, CS_MODE_THUMB | CS_MODE_MCLASS)
for i in md.disasm(sec1[0x18ec:0x1930], 0x10018ec):
    print('%#010x: %-10s %-8s %s' % (i.address, i.bytes.hex(), i.mnemonic, i.op_str))

for name, w in [('SHCSR',0xe000ed24), ('CCR',0xe000ed14), ('AIRCR',0xe000ed0c)]:
    hits = [hex(a) for a in range(0, len(d)-4, 4) if int.from_bytes(d[a:a+4],'little') == w]
    print(name, hex(w), '->', hits)
PY
```

期望（已实跑核对）：
```
len 258048 sha256 de22d2fc06bc8a85
SP 0x2040000  PC 0x10018ed  bit0=1
0x010018ec: 72b6       cpsid    i
0x010018ee: 2648       ldr      r0, [pc, #0x98]
0x010018f0: 4ff00b01   mov.w    r1, #0xb
0x010018f4: 0160       str      r1, [r0]
...
0x01001900: 4ff4e021   mov.w    r1, #0x70000
0x01001904: 0160       str      r1, [r0]
0x01001908: 80f30988   msr      psp, r0
0x01001910: 80f31488   msr      control, r0
SHCSR 0xe000ed24 -> ['0x2824']
CCR 0xe000ed14 -> ['0x2a60']
AIRCR 0xe000ed0c -> ['0x2af4']
```

---

## 7. 我自己的另一处误读（留档）

我曾以为日志 `entry words at VA 0x1000000: 0x10019b1 0x10019b1 0xd00cf8df 0xf832f002`
表示「VA 0x1000000 被映射错位 8 字节」。

**误读。** `hakios.mali610.c:1332-1333` 打印的是 `vec[2], vec[3], code[0], code[1]` ——
前两个数是 **VA 0 的第 2、3 个字**，后两个才是 VA `0x1000000`。离线核对固件
VA `0x1000000` 处确实是 `0xd00cf8df 0xf832f002`，**没有错位**。

---

## 8. 另一处外部指正的误判

子代理曾报「`sec1` 的 leaf attrs `0x4c3` 比其它段少了 bit38」。
实为 **bit54**（`0x400000000004c3 - 0x4c3 = 2^54`），即 **`PTE_XN`**。
`sec1` 是唯一的 `r-x` 段，**唯一不该带 XN 的段**，所以这是**正确行为**，不是异常。
`PTE_XN = 1ull << 54`，见 `hakios.mali610.c:840`。

---

## 9. 用 ARM 官方固件做的交叉验证（2026-11-15 12:40）

子代理下载了 linux-firmware 的 `arm/mali/arch10.8/mali_csffw.bin`（282624 B，v0.3，
sha256 `a27847ea11f8efb3…`），与我们的 v0.1 逐项对比。

### 9.1 两份固件的复位处理器**逐条相同**

| | 我们 v0.1 | ARM 官方 v0.3 |
|---|---|---|
| 复位 PC | `0x010018ed`（bit0=1） | `0x00801fbd`（bit0=1） |
| 首条指令 | `cpsid i` | `cpsid i` |
| 字面量 1 | `0x40022000` | `0x40022000` |
| 字面量 2 | `0x40022004` | `0x40022004` |
| 字面量 3 | `0xe000ed24`（SCB->SHCSR） | `0xe000ed24` |
| 后续 | `msr psp` / `msr control` / `isb sy` | 同 |

**结论从「我们这份固件如此」升级为「CSF 架构如此」。**
两份不同版本、不同大小、不同段布局的固件，启动代码是同一套。

#### ⚠️ 口径修正：不是「完整复位处理器逐条相同」

Codex 2026-11-15 12:32 指出这个说法过强，我复核后**确认他对**。实测差异：

| | 我们 v0.1 | ARM v0.3 |
|---|---|---|
| `SCB->SCR` (`0xe000ed10`) | **完全不碰**（全固件零命中） | **写 `= 2`**（两处字面量：`0x4724`、`0x4a8c`） |
| 设 PSP 前的序列 | 直接 `ldr r0,=SP` | 先 `*(0xe000ed10)=2`，再 `ldr r0,=SP` |
| SP 值 | `0x02040000` | `0x02030000` |
| 代码段基址 | `0x01000000` | `0x00800000` |

`SCR` bit1 是 `SLEEPONEXIT`。**这是我们两份固件之间一个真实的行为差异**，
虽然与当前故障的因果关系未证明。

**准确表述**：关键开头序列（`cpsid i` → 两个 MMIO 写 → `SHCSR` → `msr psp/control`
→ `isb`）**相同**；但**不是逐条相同**，ARM 版在设 PSP 前多一条 `SCR=2`。

另外，两份固件的**段布局也完全不同**（见 §9.4），布局差异本身是正常的。

### 9.2 VA0 段不需要可执行 —— 官方同样如此

| 固件 | `va == 0` 的段 | flags |
|---|---|---|
| 我们 v0.1 | `0x0-0x1000`，2836 B | **`0x9`（r--，无 EX）** |
| ARM v0.3 | `0x0-0x10000`，124 B | **`0x9`（r--，无 EX）** |

**ARM 自己也不把 VA0 标成可执行。**「VA0 不可执行 = 我们的 bug」这条可以正式关掉。

> 但注意：`mali_fwx=1`（trial 605）是在**旧 4 KiB 映射缺陷修复之前**跑的，
> 严格说不能作为「XN 已排除」的反证。要彻底关掉，需在 16K 映射修复后重跑一次。

### 9.3 🔴 两份固件都有「PROT 段读取」路径

复位处理器的尾部（我们 `0x01001918`，官方 `0x00801ff0`）：

```
ldr  r0, [pc, #..]   ; -> 0x40003034  = GPU_CONTROL_MCU + 0x34（即 GPU_STATUS 镜像）
ldr  r4, [r0]
and  r4, r4, #0x80   ; 取 bit7
mov  r0, r4
bl   ...             ; 若干初始化调用
cbz  r4, skip        ; bit7 == 0 -> 跳过整段
ldr  r0, [pc, #..]   ; -> 落在 PROT 段内（我们 0x0302bc78）
ldrb r1, [r0]        ; 读一个字节
cbz  r1, skip        ; 该字节为 0 -> 跳过
... ldm.w/stm.w 拷贝循环 ...
```

**含义**：`GPU_CONTROL_MCU+0x34`（= GPU_STATUS 的 MCU 侧镜像）的 **bit7** 决定固件要不要去读
**PROT 段**里的一个字节。若该字节非 0，固件会做一次从 PROT 段到 `0x02000000` 的拷贝。

#### 9.3.1 🔵 这条分支**已被排除**，不是当前故障原因

把拷贝循环的字面量池解出来，逻辑就完整了（`0x01001988`-`0x010019a8`）：

| 字面量 | 值 | 用途 |
|---|---|---|
| `0x01001998` | `0x40003034` | 守卫寄存器（GPU_STATUS 的 MCU 镜像） |
| `0x0100199c` | `0x0302bc78` | **被读的那个字节**，落在 PROT 段 6 内 |
| `0x010019a0` | `0x03800000` | 拷贝**源** |
| `0x010019a4` | `0x02000000` | 拷贝**目的** |
| `0x010019a8` | `0x0202c210` | 拷贝终点（`0x2c210` = **180752 B**，正好等于段 2 数据量） |
| `0x010019ac` | `0x0202c944` | 余下零填充终点 |

控制流：

```
0x0100191a  r4 = *(0x40003034)
0x0100191c  r4 &= 0x80
...
0x01001932  cbz r4, 0x01001982      ; bit7 == 0  -> 整段跳过
0x01001934  r0 = 0x0302bc78
0x01001936  r1 = *(uint8_t*)r0
0x01001938  cbz r1, 0x01001982      ; 该字节 == 0 -> 也跳过
0x0100193a  copy 0x03800000 -> 0x02000000, 180752 B
0x01001964  zero 0x0202c210 .. 0x0202c944
0x01001982  b.w 0x01002aec
```

**关键：冷启动时这个分支必然不走。**

PROT 段 6（`0x03000000-0x0303e000`，flags `0x2b`）的**数据字节数是 0**，
所以驱动映射后整段是零填充，`*(uint8_t*)0x0302bc78 == 0`，
`cbz r1` **必然跳到 `0x01001982`**。

「整段零填充」这件事我核过两条分配路径，都成立：

| 路径 | 证据 | 是否清零 |
|---|---|---|
| `mem_alloc_low` | `:703` `memset((void *)kva, 0, size)` | ✅ |
| `mem_alloc_high` | `:735` `kmem_alloc(..., KMA_ZERO \| ...)` | ✅ |

trial 695 实际走的是 high 路径：boot-args 里 `mali_low=0`，
日志 `:3143` 的 flag dump 也是 `low 0`，与 `:724` 的分支一致。

即使 `0x3034` 的 bit7 为 1，第二个 `cbz` 仍然会跳过整段拷贝。
**两条路都通向跳过。因此这条分支在冷启动时不会执行，也不可能造成故障。**

> 这也解释了为什么 `0x03800000` 这个「未声明地址」存在于固件里却不报错 ——
> 它是给**热启动/恢复路径**用的，冷启动根本不会读它。

**结论：这条线索关闭。** 不需要为它加任何采样或映射。

### 9.4 段布局差异（两份完全不同，不是错误）

| | 我们 v0.1 | ARM v0.3 |
|---|---|---|
| IFACE 段数 | 10 | 8 |
| EX 段 VA | `0x01000000-0x01010000` | `0x00800000-0x00820000` |
| 共享段 span | `0x04000000-0x04094000`（606208 B 声明） | `0x04000000-0x04010000`（65536 B 声明） |
| 共享段数据 | 8192 B | 8192 B |
| PROT 段 | 3 个（6/7/8），0 数据字节 | 1 个（6），0 数据字节 |

**共享段声明跨度差 10 倍，但实际数据都是 8192 B。** 这解释了两份固件的差别只是
「声明区间大小不同」，实际需要的内存一样。

### 9.5 version 字的地址纠正（两份子代理独立提出，我复核确认）

固件 shared 段 `+0x00` 起是 GLB control 结构，**首字段就是 version，初值 0**：

| 偏移 | 值 | 字段 |
|---|---|---|
| `+0x00` | `0x00000000` | **version**（固件负责写入） |
| `+0x04` | `0x00000000` | features |
| `+0x08` | `0x04002000` | **input_va** |
| `+0x0c` | `0x04003000` | **output_va** |
| `+0x10` | `0x00000008` | group_num |
| `+0x14` | `0x000000a0` | group_stride |
| `+0x1c` | `0x00000071` | instr_features |

**所以 version 在 `0x04000000`，不在 `0x04002000`。** 日志里
`version 0xff5aa5f0 in 0x4002000` 的 `0x4002000` 是 **input_va**，只是被顺带打印。

**驱动读法是对的**：`:3658` 写 `sentinel->version`、`:3978` 读 `check->version`，
都指向共享段 `kva` 的**首字段**。**代码无需修改，是日志措辞和任务描述需要纠正。**

---

## 10. witness 实验的设计缺口（2026-11-15 13:15）

`mali_witness=1`（`hakios.mali610.c:1361-1385`）会把复位向量改成 `0x1000001`，
用 32 字节自己的 Cortex-M 代码覆盖段 1 开头。我用 capstone 反汇编核对，
注释里的助记符**完全准确**：

```
0x01000000: 40f20000   movw     r0, #0
0x01000004: c0f20040   movt     r0, #0x400      ; r0 = 0x04000000（共享段）
0x01000008: 40f21101   movw     r1, #0x11
0x0100000c: 0160       str      r1, [r0]        ; marker 1
0x0100000e: 40f22201   movw     r1, #0x22
0x01000012: 4160       str      r1, [r0, #4]    ; marker 2
0x01000014: 4bf6ef61   movw     r1, #0xbeef
0x01000018: cdf6ad61   movt     r1, #0xdead     ; r1 = 0xdeadbeef
0x0100001c: 0160       str      r1, [r0]        ; marker 3
0x0100001e: fee7       b        #0x100001e      ; 死循环，无返回
```

### 10.1 🔴 它与真实固件**在第一条指令就分岔**

| | 第 1 条 | 第 2 条 |
|---|---|---|
| **真实固件** | `cpsid i` | `ldr r0,=0x40022000` → `str 0xb` |
| **witness** | （无 `cpsid`） | `movw/movt r0=0x04000000` → `str 0x11` |

真实固件**复位后第一批 store 就打向 `0x40022000`**，而那个地址
**不在任何已声明段内**（见 §9.3、§4）。
witness 从头到尾**不碰** `0x40022000`，只写已经映射好的共享页 `0x04000000`。

### 10.2 所以它能证明什么、不能证明什么

| 结论 | witness 能否证明 |
|---|---|
| 复位取指发生过 | ✅ 能 |
| 执行流是线性的 | ✅ 能 |
| 能写共享页 | ✅ 能 |
| **`0x40022000` 区域可访问** | ❌ **不能** |
| 固件本体没问题 | ❌ 不能 |

**「静默 data abort、无 MMU_INT、无 fault address」正是我们观测到的形状**
（`status=1`、version 未变、无 fault）。若固件在第一条 `str` 就撞上不可访问区域，
它会在任何共享内存发布之前停下 —— **witness 成功也排除不了这个假设**。

### 10.3 建议的双 marker 版本（未实施，待 Codex 决定）

```
cpsid   i
movw/movt r0, #0x40022000
str     #0x11, [r0]        ; A: 与固件第一条完全相同的动作
movw/movt r0, #0x04000000
str     #0x22, [r0]        ; B: 已映射的共享页，作为对照
b       .
```

读回时三种结果各自说明不同的事：

| 读到 | 含义 |
|---|---|
| A 和 B 都没有 | 取指/执行就没发生（与 witness 历史失败一致） |
| 只有 B | **`0x40022000` 不可访问**，而执行路径是好的 → 直指真正原因 |
| A 和 B 都有 | 两个区域都通，问题在固件更深处 |

### 10.4 witness 的历史（不要再当成新实验）

| 来源 | 记载 |
|---|---|
| `GPU-ROUND-8.md:95` | 「**最小 witness 都未完成 store**，先为执行/存储路径取得正向证据」 |
| `MMU-REASONING-REAUDIT.md:182` | 「这正是 witness 想做的事，但 **witness 从未成功跑起来**」 |
| trial 673（Codex 记录） | witness reset `0x01000001`，三次都读到 `0xff5aa5f0` |

**单纯重开 `mali_witness=1` = 重跑 673 已失败的实验。**
要它有新信息，必须改变单变量（双 marker，或新基线上的映射/缓存差异）。

### 10.5 另一个副作用

witness 结尾是 `b .` **死循环，没有返回路径**。如果它真跑起来，MCU 会停在那里，
只能靠 `MCU_CONTROL=0` 拉回；而这块板 DISABLE 后 `MCU_STATUS` **粘滞在 1**
（日志 `:3259`），`panthor_fw_stop()` 的 `status == DISABLED` 轮询会一直失败。
**这一点在决定是否上机前必须知道。**

---

## 11. `0x40022000` 到底是什么：本地 panthor 源码能给到的边界（13:30）

**结论先说：panthor 源码里根本没有这个地址，它是固件私有的。**
下面是我能证明的四条，以及一条**必须承认的未解矛盾**。

### 11.1 MCU 的地址空间是标准 ARM 64 位 LPAE 第一阶段页表

`panthor_mmu.c:2889-2892`：

```c
	if (for_mcu) {
		/* CSF MCU is a cortex M7, and can only address 4G */
		min_va = 0;
		va_range = SZ_4G;
	}
```

`panthor_mmu.c:2919`：

```c
	vm->pgtbl_ops = alloc_io_pgtable_ops(ARM_64_LPAE_S1, &pgtbl_cfg, vm);
```

**含义**：MCU 看到的是一个平坦的 4 GiB 虚拟空间，由**标准 LPAE 页表**翻译。
**没有任何「硬件旁路」或「恒等映射」。** 未映射的地址访问会走 translation fault。

> 顺带确证：`/* CSF MCU is a cortex M7 */` 是 panthor 源码自己的注释，
> 与我们 §1-§9 从固件字节推出的结论**完全一致**。

### 11.2 panthor 给 MCU 的唯一特权是「固定用地址空间 0」

`panthor_mmu.c:753-755`：

```c
	if (vm->for_mcu) {
		drm_WARN_ON(&ptdev->base, ptdev->mmu->as.alloc_mask & BIT(0));
		as = 0;
	}
```

`for_mcu` 在整个 `panthor_mmu.c` 里只出现 **6 次**（`:354/:355/:753/:2840/:2849/:2885/:2889/:2952`），
**全部与 AS 选择或 VA 范围初始化有关，没有一处是映射或旁路。**

### 11.3 共享区是 `0x04000000` 起、64 MiB 大

`panthor_fw.c:203-204`：

```c
#define CSF_MCU_SHARED_REGION_START		0x04000000ULL
#define CSF_MCU_SHARED_REGION_SIZE		0x04000000ULL
```

范围 = `0x04000000` .. `0x08000000`。我们的固件声明共享段 `0x04000000-0x04094000`，
**落在范围内** ✓。

**`0x40022000` 不在这个范围里** —— 注意 `0x04000000`（64 MiB 处）与
`0x40022000`（1 GiB 处）是**完全不同的地址**，只差一个 `0`。

### 11.4 全仓库搜不到 `0x40000000` 区间的任何常量

在 `.csf-re/src/` 的全部 panthor 文件里搜 `0x4XXXXXXX` 八位常量，**只命中一处**：

```
kbase_mali_kbase_regmap_legacy_csf.h:38:#define IPA_CONTROL_BASE 0x40000
```

那是 `0x40000`（五位数，IPA 控制），**与 `0x40022000` 无关**。
另外三个地址在固件代码段里各**只出现一次**，且都只在复位处理器内被用一次，
**此后再没有任何代码读回它们**：

| 字面量位置 | 值 | 用途 |
|---|---|---|
| `0x01001988` | `0x40022000` | 复位第 1 个 store 目标 |
| `0x0100198c` | `0x40022004` | 复位第 2 个 store 目标 |
| `0x01001998` | `0x40003034` | 守卫读（= GPU_CONTROL_MCU+0x34） |

### 11.5 🔴 更正：我原来的推理是错的（Codex 2026-11-15 12:25 指出）

我原来在这里写：「按 panthor 源码 `0x40022000` 未映射，但固件要写它，这是矛盾」。
**这个推理不成立。** 我拿 AS0 页表走查去推断一个 MMIO 地址不可访问 —— 是错的。

原文依据（我亲自 fetch 并通读）：
<https://icecream95.gitlab.io/fun-with-csf-firmware.html> 的 MMIO 节：

> At `0x04000000` ... is a **64 MiB region of memory** which is used for
> **buffers shared** between the kernel and the MCU.
>
> From `0x40000000` is a **256 KiB region of memory mapped IO**
> (**not all of it backed by real storage**) which **controls the hardware of the GPU**.
>
> The region at `0x04000000` is just another memory region, but the
> `0x40000000` is **special Device memory**. They aren't really so related.

**所以：**

| 地址 | 性质 | 走页表吗 |
|---|---|---|
| `0x04000000`..`0x07ffffff` | 64 MiB **普通内存**，kernel↔MCU 共享缓冲 | ✅ 走 AS0 页表 |
| `0x40000000`..`0x4003ffff` | 256 KiB **Device MMIO**，控制 GPU 硬件 | ❌ **不走页表** |

`0x40022000` 落在后者。**AS0 页表走查报 `table unset` 完全正常，与它能不能访问无关。**

### 11.5.1 原文还指出 MCU 有**三层**地址解析，我漏了前两层

> To access memory, there are three layers that must be navigated:
> 1. The MCU's integrated **MPU** (memory protection unit)
> 2. The **MCU memory mappings**
> 3. The **GPU MMU**

| 层 | 控制者 | 粒度 | 我此前是否考虑 |
|---|---|---|---|
| **MPU** | MCU | 16 个区，32 字节起 | ❌ **完全没有** |
| **MCU mappings** | MCU | 8 × 128 MiB，64 MiB 对齐 | ❌ **完全没有** |
| GPU MMU (AS0) | 内核 | 4 KiB | ✅ 只考虑了这一层 |

原文还给了一段真实代码，演示 MCU 怎么通过 `0x40022100` 处的寄存器
**自己建立 128 MiB 映射**（`map_reg_base = 0x40022100`）—— 那正是
`0x40022000` 一带。**这直接证明那片区域是 MCU 可访问的 MMIO。**

### 11.5.2 修正后的结论

- **不能说**「`0x40022000` 未映射，所以固件第一条 store 就失败」。
- **能说**：`0x40022000` 是 GPU MMIO，不在任何固件声明段内，也不需要声明。
- **仍然未知**：xaga 这颗 MT6895 上，那片内部 MMIO 窗口在 MCU 启动那一刻
  是否已响应（总线/时钟/权限状态）。这需要独立硬件证据，但与「漏建 PTE」是**不同假设**。

**我原来的 §11.6「双 marker 写 `0x40022000`」方案一并作废** —— 那是配置寄存器，
固件写的是 `0xb`，不是可读 marker RAM；写 `0x11` 不是同一动作，也无法读出前后差异。

### 11.6 作废：原来的「双 marker」方案

我原来提议在 witness 里先写 `0x40022000 <- 0x11` 再写 `0x04000000 <- 0x22`。
**方案作废**（Codex 2026-11-15 12:38 指出，我复核同意）：

1. `0x40022000` 是**配置寄存器**，不是已知可读的 marker RAM。固件往里写的是 `0xb`，
   我提议写 `0x11`，**不是同一个动作**。
2. 第一笔写寄存器**没有独立可观测标记**，后一笔 shared store 不出现时，
   仍然无法分辨前面那笔有没有执行。

**如果将来要测**，正确做法是：在**已验证可写的 shared RAM** 里放前后两个 marker，
把**原样的 MMIO 序列夹在中间**，并明确屏障。即使那样，
「配置改变后影响 shared 访问」仍必须列入阴性解释。

### 11.7 与 695 基线一致的下一轮候选（Codex 选定，我未执行）

Codex 在 `logs/resume-sol-20261114-231152/trial-695-jobirq/next-witness-nopdc-proposal.json`
（同目录 `next-witness-nopdc-review.md`）里选定：**纯 shared-RAM witness**，
相对 695 **只改 `mali_witness=0`→`1`**，其余参数全部沿用。

我核过并同意的三条边界：

| 项 | 事实 |
|---|---|
| 相对 695 | 单变量（只动 witness） |
| 相对 673 | **不是**单变量：673 是 `low=1` / `fwnc=1` / PDC enabled / `F3C=0x4`，且早已完整映射 |
| 更近的参照 691 | 候选相对它只多 `nopdc=1`，witness 字节相同，**但源码与构建已变，不能称同二进制 A/B** |

退出与恢复：stub 确实没有 `cpsid`、没有返回、末尾 `e7fe`。
`MCU_CONTROL=0` 能停住已成功执行的 stub **没有正向验证**，不能作恢复保证。
`panthor_fw_stop()` 是**有界轮询**（10 µs × 100 ms），失败报一次，不是无限等。
技术上可以加「跳回原 reset `0x010018ed`」，但那是**新实验**（会进入完整固件，
可能覆盖 marker），当前不加。

**状态：held。未构建、未刷机。** 需等两个离线审计结束、且退出/恢复边界明确后再议。
