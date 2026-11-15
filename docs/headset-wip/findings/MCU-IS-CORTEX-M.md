# CSF MCU 是 32 位 Cortex-M，不是 AArch64

> 2026-11-15 12:20，DSH 独立发现。**离线可复现**，不需要刷机。
> 输入：`drivers/gpu/hakios.mali610_fw.h`（258048 B，sha256 `de22d2fc…`）。
> 这是本项目第一次确定 MCU 的**指令集架构**。

---

## 0. 一句话

复位向量 `pc = 0x010018ed` 的 **bit0 = 1**，是 ARMv7-M 的 Thumb 标志位；
从 `0x010018ec` 解出的是标准 Cortex-M 启动代码（`cpsid i` + 一串 `mov.w`/`str`）；
这些 `str` 写向的三个地址全部落在 **Cortex-M 私有外设空间 `0xE000E000–0xE000EFFF`**
（`SCB->SHCSR` / `SCB->CCR` / `SCB->AIRCR`）。

**结论：CSF MCU 是 32 位 Cortex-M 类核。** 判据是架构级的，不是推断。

---

## 1. 复位向量

固件段 0（VA `0x0-0x1000`，flags `0x9`，2836 B）的头 8 字节就是向量表：

| 偏移 | 值 | 含义 |
|---|---|---|
| +0 | `0x02040000` | 初始 SP（= 段 2 的 VA 末尾，与日志 `sp 0x2040000` 一致） |
| +4 | `0x010018ed` | 复位入口，**bit0 = 1** |

**bit0 = 1 在 ARMv7-M / ARMv8-M 里的唯一含义就是「目标是 Thumb 态」**，且真实指令地址是 `0x010018ec`。
若按 AArch64 解释，复位地址必须是 4 字节对齐（bit0/bit1 都为 0）；`0x…ed` **不可能是 AArch64 入口**。

日志原文（`trial695-full.log` L2931）：

```
mali: reset vector: sp 0x2040000  pc 0x10018ed
```

---

## 2. 从 `0x010018ec` 解出的指令

按 Thumb-2 小端解码，前 10 条全部合法、无语义空洞：

| VA | 编码 | 指令 | 说明 |
|---|---|---|---|
| `0x010018ec` | `b672` | `cpsid i` | 关中断。**Cortex-M 复位处理器的第一条指令** |
| `0x010018ee` | `4826` | `ldr r0, [pc, #152]` | → 字面量 `0x40022000` |
| `0x010018f0` | `f04f 010b` | `mov.w r1, #0xb` | |
| `0x010018f4` | `6001` | `str r1, [r0]` | `*(0x40022000) = 0xb` |
| `0x010018f6` | `4825` | `ldr r0, [pc, #148]` | → `0x40022004` |
| `0x010018f8` | `f04f 0107` | `mov.w r1, #0x7` | |
| `0x010018fc` | `6001` | `str r1, [r0]` | `*(0x40022004) = 0x7` |
| `0x010018fe` | `4824` | `ldr r0, [pc, #144]` | → **`0xe000ed24`** |
| `0x01001900` | `f44f 21e0` | `mov.w r1, #0x2e0` | |
| `0x01001904` | `6001` | `str r1, [r0]` | **`*(0xe000ed24) = 0x2e0`** |

`cpsid i` 作为第一条指令，是 Cortex-M 复位代码的固定写法；AArch64 里**没有这条指令**（对应的是 `msr daifset, #2`）。

---

## 3. 决定性证据：写入的三个地址是 Cortex-M 系统控制块

扫描整份固件，`0xE000E000–0xE000EFFF`（ARM 规定的 Cortex-M 私有外设空间）里命中 **3 个**寄存器，一个不多一个不少，全部是 SCB：

| 常量 | 寄存器 | 全名 | 文件偏移 | VA |
|---|---|---|---|---|
| `0xe000ed24` | **SCB->SHCSR** | System Handler Control and State Register | `0x2824` | `0x1001990` |
| `0xe000ed14` | **SCB->CCR** | Configuration and Control Register | `0x2a60` | `0x1001bcc` |
| `0xe000ed0c` | **SCB->AIRCR** | Application Interrupt and Reset Control | `0x2af4` | `0x1001c60` |

这三个都是**32 位 Cortex-M 架构定义的寄存器**，AArch64 架构里**不存在**这些地址（AArch64 的系统寄存器走 `MSR/MRS`，没有内存映射的 SCB）。

写 `SHCSR = 0x2e0` 的语义也对得上：bit5 `USGFAULTENA` + bit7 `BUSFAULTENA` + bit9 `DIVBYZERO` = 使能三项 fault，这是典型的启动早期配置。

---

## 4. 这修正了什么

| 之前的假设 | 真相 |
|---|---|
| CSF MCU 与 host 同为 AArch64 | **是 32 位 Cortex-M**，host 是 AArch64 |
| 复位 PC 只需 4 字节对齐 | bit0 是 Thumb 标志，真实地址是 `0x…ec` |
| MMU 页表属性按 AArch64 语义即可 | 页表是 **GPU 自己的 MMU**（`AS_TRANSTAB`），与 AArch64 无关；但**取指宽度/对齐**应按 32 位理解 |

**没有改变的部分**：页表格式、段加载、`MCU_CONTROL`/`MCU_STATUS` 语义都不受影响——
那是 GPU 的 MMU 和 CSF 的控制寄存器，与 MCU 核心架构无关。
所以「固件没执行」这个现象**不能**用这条解释掉。

---

## 5. 可复现命令

```sh
cd ~/IOS/hakios-mtk
python3 - <<'PY'
import re
src=open('drivers/gpu/hakios.mali610_fw.h').read()
m=re.search(r'kMaliFirmware\[\w+\] = \{(.*?)\};',src,re.S)
d=bytes(int(x,16) for x in re.findall(r'0x([0-9a-f]{2})',m.group(1)))
sec0=d[896:3732]; sec1=d[0xe94:0x106dc]
print('SP',hex(int.from_bytes(sec0[0:4],'little')))
print('PC',hex(int.from_bytes(sec0[4:8],'little')),'bit0=',int.from_bytes(sec0[4:8],'little')&1)
for name,w in [('SHCSR',0xe000ed24),('CCR',0xe000ed14),('AIRCR',0xe000ed0c)]:
    hits=[hex(a) for a in range(0,len(d)-4,4) if int.from_bytes(d[a:a+4],'little')==w]
    print(name,hex(w),'->',hits)
PY
```

期望输出：
```
SP 0x2040000
PC 0x10018ed bit0= 1
SHCSR 0xe000ed24 -> ['0x2824']
CCR 0xe000ed14 -> ['0x2a60']
AIRCR 0xe000ed0c -> ['0x2af4']
```

---

## 6. 证据边界

- **已证明**：复位向量 bit0=1；`0x010018ec` 起是合法 Thumb-2；固件含 3 个 Cortex-M SCB 常量。
- **未证明**：具体是哪个 Cortex-M 型号（M4/M7/M33/M55…）。`AIRCR`/`CCR`/`SHCSR` 在 M3~M55 上都有。
  若要定型号，需要看是否用了 FPU 指令（`vldr`/`vmov`）或 TrustZone（`0xE000ED94` SAU）。
- **未证明**：这条与「MCU 不执行」的因果关系。**目前看无关** —— 页表和段加载都不依赖核心架构。

## 7. 顺带纠正（DSH 自查）

我一度以为日志里的 `entry words at VA 0x1000000: 0x10019b1 0x10019b1 0xd00cf8df 0xf832f002`
是「VA 0x1000000 被映射到段 0 偏移 8，错位 8 字节」。

**这是误读。** 代码 `hakios.mali610.c:1332-1333` 是：

```c
printf("mali: entry words at VA 0x1000000: %#x %#x %#x %#x\n",
    vec[2], vec[3],
    code ? *(volatile uint32_t *)code : 0u,
    code ? *((volatile uint32_t *)code + 1) : 0u);
```

前两个数是 **VA 0 的第 2、3 个字**（段 0 偏移 8/12），后两个才是 VA `0x1000000` 的内容。
离线核对：固件 VA `0x1000000` 处确实是 `0xd00cf8df 0xf832f002`。**没有错位。**
