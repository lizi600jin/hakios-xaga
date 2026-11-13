# MT6368 accdet · 状态机规格

> 对象：`docs/headset-wip/ko/mt6368-accdet.ko`
> 目的：把「耳机插入/拔出/按键」的判定逻辑写成可以直接实现的规格
> 方法：`aarch64-linux-gnu-objdump -dr` 反汇编
>
> **验证状态**：结构与分支来自反汇编（可信）；**阈值经过的运行期数值未观测**（无耳机）。

---

## 1. 变量布局（`.bss`）

从反汇编里能确定偏移的 `.bss` 变量。`bss` 是模块的全局区基址（`adrp .bss` + 偏移）。

| 偏移 | 类型 | 推断含义 | 证据 |
|---|---|---|---|
| `+0xc8` | u32 | `headset-mode-setting` 的某成员，写进 `0x2694` 高 4 位（=4） | `0x1128` `ldp w10,w9,[x9]` → `0x1134` `orr w2, w9, w10, lsl #4` |
| `+0xcc` | u32 | 同成员，写进 `0x2694` 低 4 位（=1） | 同上 |
| `+0xd4` | u32 | 写进 `0x26a3` 高 4 位（=3） | `0x914` `ldr w20,[x9,#44]` → `0x928` `lsl w3, w9, #4` |
| `+0xd8` | u32 | 写进 `0x26a4` 低 4 位（=0xe） | `0x94c` `ldr w3,[x9]` |
| `+0xe0` | u32 | 写进 `0x26a5`（=0xe） | `0x974` `ldr w2,[x9]` |
| `+0xe8` | u32 | `headset-three-key-threshold` 数组起点 | `0x6a8c` `adrp .bss+0xe8` |
| `+0xf4` | u32 | 三键阈值的下一档 | `0x6af8` |
| `+0xf8` | u32[2] | 三键阈值的另外两档 | `0x6ad4` `adrp .bss+0xf8`，`0x6adc` `ldp w11,w12` |
| `+0xc0` | **指针** | `0x26a9` 影子所在的地址（**两级**：`*(u32**)(bss+0xc0)`） | `0x1dfc` `ldr x9,[x8,#192]` → `0x1e00` `ldr w10,[x9]` |
| `+0x168` | u32 | **四键判定的阈值上界** | `0x6a84` `ldr w9,[x8,#360]` |
| `+0x164` | u32 | 四键判定的某档 | `0x6acc` `ldr w2,[x8,#356]` |
| `+0x1b8` | u32 | 状态 / `accdet_status` | `0x1ec8` 附近 |
| `+0x240` | work | 中断下延的 work | `0x1938` `add x2, x8, #0x240` |
| `+0x190` | u32 | IRQ 号（`disable_irq_nosync` 的参数） | `0x1908` `ldr w0,[x8,#400]` |

**注意 `+0xc0` 是两级间接**：`0x1dfc` 用 `ldr x9,[x8,#192]` 先取一个**指针**，
`0x1e00` 才 `ldr w10,[x9]` 取到值。所以 `w10` 不是 `.bss+0xc0` 的内容，
而是 `.bss+0xc0` 指向的那个地址的内容（很可能是 `0x26a9` 的镜像缓冲区）。

**注意偏移的进制**：`+0xc8`、`+0xe8`、`+0x240` 是十六进制（来自 `adrp .bss+N` 的符号偏移）；
`+0x168`(360)、`+0x164`(356)、`+0x190`(400)、`+0xc0`(192) 在反汇编里显示为**十进制**
（`ldr w9,[x8,#360]`），本文已统一换算成十六进制书写。原样保留十进制便于对照反汇编。

---

## 2. 插入检测：`accdet_irq_handle`

### 2.1 入口逻辑（`0x1db4`–`0x1e44`）

```
读 0x26a9 → w9 = 当前值
w10 = **(u32 **)(bss+0xc0)      ; 影子（两级间接，见 §1）

按影子 w10 的 bit2/3/4 选检测档位 w19:
  bit2 置位 → w19 = (w9 >> 2) & 1
  bit3 置位 → w19 = (w9 >> 2) & 2
  bit4 置位 → w19 = (w9 >> 2) & 3
  都不置位  → w19 = 0

再读一次 0x26a9 → w20
读 0x26b6 → 读 0x26b8
```

**关键分支**（`0x1e7c`–`0x1e80`）：

```
if (w19 != 0)      goto 0x1f5c      ; 特殊档位，跳过普通插入处理
if ((w20 & 1) == 0) goto 0x1f5c      ; bit0 为 0 = 未插入
→ 继续执行"检测到插入"的路径
```

### 2.2 结论：`0x26a9` bit0 = 插入标志，1 = 已插入

依据：
1. 真机未插时 `0x26a9 = 0x00`（`accdet-baseline-dump.txt`）
2. 代码在 bit0 为 0 时**跳过**插入处理路径

**未验证**：bit0 从 0 变 1 的实际条件。没有耳机，无法让 bit0 置 1。

### 2.3 插入后的动作（`0x1e84` 起）

```
0x26aa: update_bits(mask 0x01, val 0x01)     ; 清中断 / 使能下一轮
queue_work_on(0x20, workqueue, bss+0x240)    ; 把重活推给 work 线程
```

然后进入一段**带超时的轮询**（`0x1ec8`–`0x1f38`）：

```
loop:
    if (0x26a9 & 0x01) == 0: break           ; 掉线了
    if (sched_clock() - start > 0x61a80):     ; 400000 ns = 400 us
        break
0x26aa: update_bits(...)
0x2323: update_bits(...)
```

`0x61a80` = 400000，配合 `sched_clock()` 的单位（ns），是 **400 微秒**的稳定等待窗口。

---

## 3. 拔出检测

`0x26a9` bit0 从 1 变 0。驱动里对应的动作：

| 寄存器 | 动作 | 位置 |
|---|---|---|
| `0x26aa` | update_bits | `0x1fc8`、`0x20c0`、`0x20e8` |
| `0x268b` | update_bits | `0x2070`、`0x211c` |
| `0x26a3` | update_bits | `0x2000`、`0x2148` |
| `0x26a4` | update_bits | `0x2034`、`0x209c` |

**未解出**：这些位的具体值与顺序（需要逐个跟踪 `w2`/`w3`，工作量比 `init` 大）。

---

## 4. 三态判定（耳机 / 耳麦 / line-out）

驱动里的字符串（`.rodata`）：

```
"accdet HEADPHONE(3-pole) %s"
"accdet MICROPHONE(4-pole) %s"
"accdet LineOut %s"
"accdet %s, set cable_type = NO_DEVICE %d"
```

三态判定的依据在 **ADC 读数**：`0x26b6` 与 `0x26b8`。

真机基线（未插）：`0x26b6 = 0xff`、`0x26b8 = 0xff`、`0x26ba = 0xff`。

`0xff` 是"悬空/满量程"的典型读数 —— 没有耳机时 MIC 线上的分压电路没有负载，ADC 顶到满。

**推断**（标「未证明」）：
- 插入后 ADC 落在某个区间 → 按 `headset-three-key-threshold` 或 `headset-four-key-threshold` 分档
- 4-pole（带麦）有 mic 负载，ADC 读数与 3-pole 不同

**完全未验证**：三态对应的具体 ADC 区间。这必须插耳机才能测。

---

## 5. 按键判定：`multi_key_detection`

### 5.1 签名与分支

`w0` 参数决定走哪套阈值：

```
if (w0 != 0)  → goto 0x6acc   ; 用四键阈值（bss+0x164 / bss+0x168）
else          → 检查 (*(u8**)(bss+0xc0))[1] 的 bit1
                置位 → 用四键阈值（bss+0xf4 / bss+0xf8）
                否则 → 用三键阈值（bss+0xe8 起）
```

反汇编（`0x6a7c-0x6a8c`）：

```
ldr  x9, [x8, #192]      ; x9 = *(void**)(bss + 0xc0)
ldrb w10, [x9, #1]       ; w10 = 那个缓冲区的第 1 字节
tbnz w10, #1, 0x6ad4     ; bit1 置位 → 四键
```

即"三键还是四键"由**同一个影子缓冲区的第 1 字节的 bit1** 决定，
而这个缓冲区很可能就是 `0x26a9` 的镜像（见 §1 的两级间接说明）。
这与 `0x26a9` 的 bit2/3/4 是"检测模式"相互印证：`accdet-mic-mode` 决定用几键。

### 5.2 判定结构（三键，`0x6adc`–`0x6b10`）

反汇编（`w9` = 阈值上界 `*(bss+0x168)`，`w11`/`w10` = 相邻两档阈值）：

```
w11 = threshold[0]
w12 = threshold[1]
if (w12 <= w9)  goto next          ; 不满足 → 试下一档
if (w11 >  w9)  goto next
w2 = 4                             ; ← 命中这一档，返回 key code 4
goto 0x6b48

next:
w10 = threshold[2]
if (w11 <= w9) goto check3
if (w10 >  w9) goto check3
w2 = 1                             ; ← key code 1
```

**模式**：把 ADC 读数 `w9` 依次跟相邻阈值比较，落在哪两个阈值之间就得出对应的 key code。

返回值（`w2`）是 key code，取值至少包含 `1`、`2`、`4`、`8`。

### 5.3 与 DT 阈值表的对应

DT `headset-three-key-threshold = <0 80 220 400>`（4 个数 → 3 段）
DT `headset-four-key-threshold = <0 58 121 192 400>`（5 个数 → 4 段）

对应 `.bss+0xe8` 起的数组。**未验证**：数组里存的是原始 DT 值还是换算过的 ADC 值。
Android 侧的阈值单位是 **ohm**（阻抗），而这里的比较对象是 ADC 读数 ——
中间还有一次阻抗 → ADC 的换算，**该换算未找到**。

→ **这是按键功能的完全阻断点**：没有换算公式，无法从 ADC 读数得出阻抗，也就无法套用 DT 阈值。

---

## 6. 中断

| SPMI 子号 | 名字 | 用途 | 真机计数 |
|---|---|---|---|
| 97 | `ACCDET_IRQ` | 插入检测 / 按键 ADC 完成 | 0 |
| 98 | `ACCDET_EINT0` | EINT 模式下的插拔边沿 | 0 |

都在 `spmi-pmic-irq` 之下，父链是 `mtk-eint 243` → `rcs_irq`。

驱动侧的清除：写 `0x26aa` 的 bit0（`0x1e88-0x1e94`：`update_bits(mask 0x01, val 0x01)`）。

**未验证**：`0x26aa` bit0 到底是"清除"还是"使能"。`val 0x01` 是**写 1**，
在 MTK 的 PMIC 寄存器里"写 1 清除"和"写 1 使能"都常见，无法从代码单独判定。

---

## 7. 给裸机实现的状态机（推荐形态）

```
状态: ABSENT（无耳机） / PRESENT_3POLE / PRESENT_4POLE / PRESENT_LINEOUT

初始化:
    accdet_init_once()      /* 12 个写，已对拍 */
    accdet_init()           /* 11 个写，已对拍 */

循环（每 50 ms）:
    v = spmi_read8(5, 0x26a9)
    if (v & 0x01) == 0:
        if state != ABSENT: state = ABSENT; report()
        continue
    /* 已插入 */
    adc = spmi_read8(5, 0x26b6) | (spmi_read8(5, 0x26b8) << 8)
    if state == ABSENT:
        state = classify(adc)      /* ← 映射规则未解出 */
        report()
    key = classify_key(adc)        /* ← 换算公式未解出 */
    if key: report_key(key)
```

**能实现的**：状态机的骨架、`0x26a9` 的读取、插入/拔出判定。
**不能实现的**：`classify()` 与 `classify_key()` —— 缺 ADC→阻抗换算，且无实测数据。

→ **有耳机之前，按键和三态分类做不了。插入/拔出判定可以做。**

---

## 8. 未解出清单（按对实现的重要性排序）

| # | 项 | 阻断什么 | 需要什么才能解 |
|---|---|---|---|
| 1 | ADC 读数 → 阻抗的换算 | 三态分类、按键 | 真机带耳机读数，或厂商 datasheet |
| 2 | 三态对应的 ADC 区间 | 三态分类 | 同上 |
| 3 | `0x26aa` bit0 的语义（清除 vs 使能） | 中断处理 | 真机中断触发一次即可区分 |
| 4 | 拔出时 4 个寄存器的完整写入序列 | 干净的拔出处理 | 逐个跟踪 `0x1f5c`–`0x2300` |
| 5 | `mt_hp_event` 走哪条分支 | 发声路径 | DAPM 运行期值 |
| 6 | SGEN 关闭的完整序列 | 干净地停止发声 | 跟踪 `0x9c70` 起 |
| 7 | `0x26c2`/`0x26c4`/... 等未知非零寄存器的来源 | 完整复位 | 未知 |

**前 3 项都需要真机（第 1/2 需要耳机，第 3 只需要一次插拔事件）。**
第 4/6 项是纯反汇编工作，可以继续做。
