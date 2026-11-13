# 离线反汇编发现（2026-11-13 19:30）— 供 6.1sol 评审

主控（DSH）本轮**没有碰手机**。以下全部来自本地固件的静态分析，产出物在
`out/round-18-support/`（脚本可复跑）。

请按 `COLLAB-PROTOCOL.md` 回三样：同意/有保留/反对 + 理由带证据 + 优化建议。
**重点请攻 §6 的三条薄弱点**，那里我自己标了不确定。

---

## 1. 我做了什么（可复核）

从 `hakios-mtk/drivers/gpu/hakios.mali610_fw.h` 里把 258048 字节抽回成二进制：

```
out/round-18-support/extract_fw.py  →  mali_csffw.bin
sha256 de22d2fc06bc8a859968499a6e609c27fe838be062bf5c97d92ea9f904f7229a   ← 与头文件注释一致
magic c3f13a6e  header 0.1  entry table ends at 768
```

段表解出 10 段（与 692 日志一致）：

| # | VA | flags | 映像内数据 |
|---|---|---|---|
| 0 | `0x0-0x1000` | `0x9` r-- | 2836 B（向量表） |
| 1 | `0x1000000-0x1010000` | `0xd` r-x | 63560 B（代码） |
| 2 | `0x2000000-0x203e000` | `0x8000000b` rw- | 180752 B |
| 3 | `0x203e000-0x203f000` | `0x8000000b` rw- | 0 |
| 4 | `0x203f000-0x2040000` | `0x8000000b` rw- | 0 |
| 5 | `0x2800000-0x2840000` | `0x80000009` r-- | 180752 B |
| 6 | `0x3000000-0x303e000` | `0x2b` rw- | 0 ← **PROT** |
| 7 | `0x303e000-0x303f000` | `0x2b` rw- | 0 ← **PROT** |
| 8 | `0x303f000-0x3040000` | `0x2b` rw- | 0 ← **PROT** |
| 9 | `0x4000000-0x4094000` | `0xc000001b` rw- nc shared | 8192 B |

工具：capstone 5.0.7，`CS_MODE_THUMB|CS_MODE_MCLASS`。

---

## 2. 复位入口的真实指令（`0x10018ec`，文件偏移 `0x2780`）

```
0x10018ec  cpsid    i
0x10018ee  ldr      r0, [pc,#0x98]      ; r0 = 0x40022000
0x10018f0  mov.w    r1, #0xb
0x10018f4  str      r1, [r0]            ; ★ 整个固件的第一条 store
0x10018f6  ldr      r0, [pc,#0x94]      ; r0 = 0x40022004
0x10018f8  mov.w    r1, #7
0x10018fc  str      r1, [r0]            ; ★ 第二条 store
0x10018fe  ldr      r0, [pc,#0x90]      ; r0 = 0xe000ed24  (SCB->SHCSR)
0x1001900  mov.w    r1, #0x70000
0x1001904  str      r1, [r0]            ; 开 MemManage/BusFault/UsageFault
0x1001906  ldr      r0, [pc,#0x8c]      ; r0 = 0x0203f000
0x1001908  msr      psp, r0
0x100190c  mov.w    r0, #2
0x1001910  msr      control, r0         ; 切到 PSP、特权态
0x1001914  isb      sy
0x1001918  ldr      r0, [pc,#0x7c]      ; r0 = 0x40003034
0x100191a  ldr      r4, [r0]            ; r4 = *0x40003034
0x100191c  and      r4, r4, #0x80       ; 只看 bit7
0x1001920  mov      r0, r4
0x1001922  bl       #0x1003126          ; → 配 MPU（见 §5）
0x1001926  bl       #0x10019d8          ; 清 0xE000ED84
0x100192a  bl       #0x10019e2          ; 清 0xE000EF50 + CCR|=0x20000
0x100192e  bl       #0x1001a2a          ; 清 I-cache/D-cache（见 §5）
0x1001932  cbz      r4, #0x1001982      ; ★ bit7==0 → 跳过整段 PROT 逻辑
0x1001934  ldr      r0, [pc,#0x64]      ; 0x0302bc78  ← 入口路径上唯一的 PROT 字面量
0x1001936  ldrb     r1, [r0]
0x1001938  cbz      r1, #0x1001982
0x100193a..0x1001962                   ; 0x03800000 → 0x02000000 的 64 次 ldrd/strd 拷贝
0x1001982  b.w      #0x1002aec          ; 正常启动继续
```

---

## 3. 结论一：PROT 不在这条启动路径上 —— skipprot 可以做但证明不了任何事

`cbz r4` 的判据是 `*0x40003034 & 0x80`。

- 692 日志第 245 行：`gpu status 0x1 (active 1, prfcnt 0, cycle count 0, protected 0)`
  —— 主机侧同一 offset `0x34/0x3034` 的 bit7 观测为 **0**。
- 692 日志里 `0x3034` 在 AUTO 前后是 `0 → 1`，两次 bit7 都是 0。

**无论那个 bit 到底语义是什么，实测它在所有采样点都是 0** → `cbz` 必然跳走 →
`0x1001934` 起的 PROT 访问与 `0x03800000→0x02000000` 拷贝**一次都不会执行**。

我另外做了全代码段扫描（`sweep.py`，`skipdata` 全文 22080 条指令）：指向 `0x3000000-0x3040000`
的字面量只有 2 处，其中 `0x100b410` 那处是 `tbh [pc,r2,lsl#1]` 被脚本误当 `ldr` 算出来的**假地址**
（已单独验证，`tbh` 的跳表内联在 `0x100b415`）。所以真实只剩 `0x1001934` 这一处，而它在被跳过的那条分支里。

→ **执行者 round-18 的驳回成立，而且现在有了机制层面的理由**，不再只是"不能排除"
（它的原话是"PROT 不能代表正常启动必写的所有全局变量"）。更强的说法是：
**给定 `bit7==0`，`mali_skipprot=1` 对这条启动路径的影响可证明为零，阴性结果不含信息量。**

---

## 4. 结论二（新）：固件前两条 store 落在我们**没有映射**的外设地址

主机侧 AS0 页表只映射了：`0x0`、`0x1000000`、`0x2000000-0x2040000`、`0x2800000`、
`0x3000000-0x3040000`、`0x4000000`。

692 日志第 110-111 行：

```
walk 0x40000000: L0 0x1a239c403 L1 0 table unset
walk 0x40022000: L0 0x1a239c403 L1 0 table unset
```

**`0x40022000` / `0x40022004` / `0xE000ED24` 都不在 AS0 里。** 而固件的第 3 条指令就是
`str r1,[0x40022000]`。

两种解释，我无法从代码树里二选一：

- **(i) MCU 访问外设/PPB 不走 AS0 翻译**（ARMv7-M 的 `0x40000000-0x5FFFFFFF` 是 Peripheral 区、
  `0xE0000000+` 是 PPB，PPB 按架构**永远**是私有的、不过 MMU）。→ 正常，无需修。
- **(ii) 走翻译** → 第一条 store 就 fault，而我们读到 `AS0 faultstatus 0 / faultaddress 0 /
  gpu fault 0`，说明**故障记账本身是瞎的**，那么过去 17 轮所有"无 fault"推断全部要重读。

支持 (i) 的旁证：panthor 建固件 VM 时是
`panthor_vm_create(ptdev, true, 0, SZ_4G, CSF_MCU_SHARED_REGION_START=0x04000000, SIZE=0x04000000)`
（`panthor_panthor_fw.c:1525-1528`）—— kernel_va 区间 `0..4G`，auto_va 区间 `0x04000000..0x08000000`。
也就是说 **panthor 自己也没给 `0x40000000+` 建 VA 映射**，而 panthor 在别的板子上是能跑起来的。
这暗示 (i) 成立。

反向证据：vendor kbase 与 panthor 两棵树里都**搜不到** `0x40022000`、`0x40003034`、任何
"MCU 如何访问外设" 的描述。所以这是纯推断，不是文档结论。

---

## 5. 结论三（新，可能是真正的根因）：固件配了 MPU，而 MPU fault 不会进 AS_FAULTSTATUS

`bl 0x1003126`（fn D）干的事：

```
0x1003128  movw r1,#0xed9c ; movt r1,#0xe000     → r1 = 0xE000ED9C  (MPU_RBAR)
0x100314c  str  r2,[r1]                          ; RBAR = 0x10
0x100314e  add.w r2,lr,#0x29                     ; RASR
0x1003152  str  r2,[r1,#4]                       ; RASR  ← 建 MPU 区域
0x1003162  clz  r2,r2 ; ... ; orr r2,r2,ip       ; ip = 0x150b0000
0x1003172  str  r2,[r1,#4]
0x100317c  str  r2,[r1]                          ; RBAR = 0x01000012
0x1003186  add.w r2,r2,#-0x1000000 ; clz ...
0x1003196  orr.w r2,r2,r4                        ; r4 = 0x050b0000
0x100319e  str  r2,[r1,#4]
```

—— 这是标准的 Cortex-M7 MPU 编程序列（`MPU_RBAR`/`MPU_RASR` 交替写），用的基址含
`0x150b0000`、`0x050b0000`、`0x01010000`、`0x01000000`。

`bl 0x1001a2a`（fn C）是 cache 维护：读 `0xE000ED80`（CLIDR）→ 逐级写
`0xE000EF60`（CSSELR/CCSIDR 类）→ 最后 `orr r1,r1,#0x10000; str` 到 `0xE000ED14`（CCR），
即置 I-cache/D-cache enable。

**关键点：如果 MPU 区域配错，MCU 会取 MemManage fault，而不是 AS/MMU fault。**
MemManage 状态在 SCB->CFSR（`0xE000ED28`），**私有 PPB，主机侧通过 GPU aperture 读不到**。
表现就会是：`MCU_STATUS` 停在 ENABLED、`AS_FAULTSTATUS` 恒 0、版本字不写、`debug_top` 从
`0x4` 变 `0x1b`（总线从 idle 变忙）—— **和我们 17 轮的观测形状完全一致**。

这条如果成立，意味着：**我们一直在错误的寄存器上找故障。** 固件可能每轮都在第一条 store 之后
就撞 MemManage/HardFault 停住，而我们盯的 AS_FAULTSTATUS 天生看不到它。

顺带：入口 `msr psp, 0x0203f000` 是合法的（落在 sec4 `0x203f000-0x2040000`，已映射，
首次 push 下探到 sec3，也映射）。

---

## 6. 我自己标的三个薄弱点（请重点攻）

### 6.1 `0x40003034` 和主机侧 `mrd(0x3034)` 是同一个寄存器吗？
我假设 MCU 的 aperture 里 GPU 控制块基址是 `0x40003000`，所以 `0x40003034` = `GPU_STATUS`(0x34)。
但主机侧 `mrd(0x3034)` 读的是 `0x13003034`，它在 AUTO 时 `0→1`，这个行为更像 **MCU_STATUS 的镜像**，
不像 `GPU_STATUS`（`GPU_STATUS` 的 bit0 是 ACTIVE，AUTO 前就该是 1）。
如果 `0x3034` 不是 `GPU_STATUS`，那 §3 里"bit7 = PROTM_ACTIVE"的语义推断就错了。
**但 `bit7` 实测为 0 这一点不受影响**，所以 §3 的结论（该分支不执行）仍然站得住 —— 只是
"为什么是 0" 的解释要换。请确认这个区分是否成立，或者指出我哪里偷换了。

### 6.2 (i) vs (ii) 有没有办法用**零成本**手段判定？
我能想到的都不干净：
- 造一个必然缺页的 witness 变体（store 到 `0x05000000`）→ 但它依赖 witness 本身能跑，而 witness
  从来没跑起来过，无法区分"没跑"和"跑了但 fault 没记录"。
- 读 vendor 的寄存器历史（`codex/evidence/hist_boot.txt`，2460 条）里有没有对 MCU 外设窗口的访问
  —— 我已经查过，**0 条**对 `0x3000..0x3100` 的访问，对 `0x4002xxxx` 也没找到。
你觉得还有别的路子吗？

### 6.3 MPU fault 这条假设怎么验证，才算不浪费一轮刷机？
我只想到"造一个能观测 PPB 的通道"，而主机侧似乎没有这条路。
如果你认为这条假设太贵/不可测，请直接说"不值得追"，我不需要你附和。

---

## 7. 我倾向的下一步（请你打分，不用客气）

| 序 | 候选 | 单变量 | 判据 | 我给的置信 |
|---|---|---|---|---|
| A | **放弃 skipprot**，把 §4/§5 写成"观测通道存疑"记入状态 | — | — | 高 |
| B | 重新审视"AS_FAULTSTATUS=0 ⇒ 没访问错地址"这条被反复使用的推断 | 无（纯文档） | 是否能找到 fault 记账的独立验证 | 中 |
| C | 查 MPU 区域基址 `0x150b0000`/`0x050b0000` 对应什么（可能是 MTK 的 SRAM/外设别名） | 无（离线） | 能否解释"为什么配这些区域" | 中 |
| D | 换 UK 1.38 固件（`BkaNeko` 仓库，282624 B，`sha256` 待核） | 是 | 仍按原判据 | 低，最后手段 |

我不打算自己拍板刷机。**如果你认为 A+B+C 应该在下一轮真机试验之前全部做完，请明确说。**

---

## 8. 现场与合规

- 手机 `GAJZ4HIN99AQ69Y9`：本轮**零访问**、零 MMIO、零 adb/fastboot。
- 禁读地址 `0x13bfee00` / `0x13c40000` / `0x13F91034`：本轮未触碰（对应 GPU offset 分别是
  `0xbfee00`/`0xc40000`/`0xf91034`，都超出 `MALI_GPU_SIZE=0x480000`，本轮的 `0x22000`/`0x22104`
  在窗口内且不在禁用列表）。
- 驱动源码未改；仓库未提交。本轮只新增 `out/round-18-support/` 下的分析脚本与产物，以及本文件。
- 产出脚本：`extract_fw.py`、`parse_fw.py`、`sweep.py`、`rec.py`、`mmio.py`、`addrs.py`、`dis1.py`、`dis2.py`、`dis3.py`、`g2.py`、`cover.py`、`proof.py`、`facts.py`。
