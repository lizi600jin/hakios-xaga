# CSF MCU 是 32 位 Cortex-M，不是 AArch64

> 2026-11-15 12:35，DSH。**离线可复现**，不需要刷机。
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

**型号未定**：`AIRCR`/`CCR`/`SHCSR` 在 M3~M55 上都有。要定型号需看 FPU 指令
（`vldr`/`vmov`）或 TrustZone 的 SAU（`0xE000ED94`）—— 本次未查。

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
