# JOB_INT_MASK 极性争议：裁决与唯一可判定实验

> 2026-11-15 02:50。三个来源给出互相矛盾的结论，DSH 逐条对源码核实后裁决。
> 每条附出处。未验证的部分明确标注。

---

## 0. 一句话

**`MASK` 的极性在 panthor 源码里是「1=启用」（Codex 对）；`STAT` 与 `RAWSTAT` 的关系
在硬件上还有一种可能（子代理 R1 的方向），但两种情况下我们都缺一个关键读数：
`RAWSTAT(0x1000)` 从未被读取。** 所以正确的下一步不是「改 mask」，而是**把 0x1000 也读出来**。

---

## 1. 三方主张

| 来源 | 主张 |
|---|---|
| **Codex**（2026-11-15 02:14） | mask 极性是「1=使能」。vendor `kbase_pm_enable_interrupts` 写 `0xffffffff`、`disable` 写 `0`；panthor `enable_events` 做 `mask \|= bits`。**我们写 `0xffffffff` 本来就对，改 0 反而屏蔽。** HALT/FATAL 读独立 `MCU_STATUS(704)`，不受 JOB mask 影响 |
| **子代理 `db018236`**（R1） | `0x100c` 是 `RAWSTAT & ~MASK`。mask bit31=1 时 `STAT` 的 bit31 恒为 0。建议改 mask=0 并轮询 `RAWSTAT` |
| **DSH 早先记录**（`MASTER-STATE.md:133` / `:234`） | 「`JOB_INT_MASK` = 使能集（写 1 = 开）」「『全屏蔽』→ 是使能集，全开」 |

## 2. 裁决依据（逐条核实）

### 2.1 Codex 的 vendor 证据 —— 成立

`drivers/gpu/mediatek/mali-valhall/gpu/arm/midgard/backend/gpu/mali_kbase_pm_driver.c`
（从 `/home/lizi/IOS/work-xaga/k-xaga` 的 HEAD 取出）：

```c
/* kbase_pm_enable_interrupts, :2145 */
kbase_reg_write(kbdev, JOB_CONTROL_REG(JOB_IRQ_MASK), 0xFFFFFFFF);

/* kbase_pm_disable_interrupts_nolock, :2167 */
kbase_reg_write(kbdev, JOB_CONTROL_REG(JOB_IRQ_MASK), 0);
```

→ **enable 写全 1，disable 写 0。这就是「1=使能」。**

### 2.2 Codex 的 panthor 证据 —— 成立

`/tmp/mt6895-mainline/drivers/gpu/drm/panthor/panthor_device.h`：
- `:608` `enable_events`：`pirq->mask |= mask;` 然后（若 ACTIVE）`gpu_write(INT_MASK, pirq->mask)`
- `:623` `disable_events`：`pirq->mask &= ~mask;`
- `:571` `_irq_suspend`：`gpu_write(INT_MASK, 0)`（屏蔽）
- `:526` handler 里：`gpu_write(INT_MASK, 0)`（处理期间屏蔽）

→ **`INT_MASK` 里 1 表示该位启用。** 与 vendor 一致。

### 2.3 子代理 R1 的引用 —— 部分引用有误

子代理引「`panthor_device.h:333-338` irq_resume 把要收的事件写进 MASK」。
实际 `:333-338` 是 `struct panthor_gpu_usage` / `struct panthor_file` 的定义，**与 IRQ 无关**。
（子代理的 `panthor_regs.h:152-156` 也不存在 —— 该目录下没有 `panthor_regs.h`。）

**引用错了，但它的结论方向不能因此否定** —— 因为 `STAT` 与 `RAWSTAT` 的确切硬件关系
没有在任何源码里被明写。

### 2.4 `STAT` 到底等于什么 —— **源码里没有明确定义**

panthor 里能看到的只有两处行为：
```c
/* :519 handler 第一句，只用 STAT 判「有没有任何中断」 */
if (!gpu_read(pirq->iomem, INT_STAT)) { ... return IRQ_NONE; }

/* :545 threaded handler 用 RAWSTAT & mask 拿具体位 */
u32 status = gpu_read(pirq->iomem, INT_RAWSTAT) & pirq->mask;
```

**注意第 545 行**：panthor **不直接读 `STAT` 取位**，而是读 `RAWSTAT` 再手动 `& mask`。
这暗示 `STAT` 的语义可能与 `RAWSTAT & MASK` 有关，但**panthor 刻意不依赖它**。

→ **`STAT = RAWSTAT & MASK` 还是 `RAWSTAT & ~MASK`，源码未定义。必须实测。**

## 3. 我们代码的确切行为（已核实）

| 位置 | 行为 |
|---|---|
| `hakios.mali610.c:3131-3136` `gpu_irq_mask_prestart()` | `mwr(kJobIntMask, 0xffffffffu)` |
| 调用点 `:4641` | 无条件执行，早于 `start_mcu()` |
| 轮询 `:3897` | `uint32_t stat = mrd(kJobIntStatus);`（`0x100c`） |
| 判定 `:3986-3989` | `if (stat & kJobGlobalIf) { booted = true; break; }` |
| 解屏蔽 `:3976` | `mwr(kJobIntMask, 0u)`，在 `if (booted)` 块内 |
| `:3998-4000` 日志 | `printf("... job irq %#x ...", mrd(kMcuStatus), ..., mrd(kJobIntStatus), mrd(kIntRaw))` |
| **`kJobIntRaw = 0x1000`** | **只在 `:364` 定义，全文件零读取** |
| `SEC_ZERO`（顺带核） | `:990` 定义，全文件零引用 |

**实测读数**（`logs/headset-resume-20261114-220244/nopdc/kernel-lines.txt:573`）：
```
[44.030314]: mali: MCU status 1 (raw state 1; boot unproven), job irq 0, gpu irq 0
```
`job irq 0` = `STAT`（0x100c）读数。`gpu irq 0` = `kIntRaw`（0x020，GPU 级 raw）。

**注意 `kIntRaw` 是 `0x020`，不是 `kJobIntRaw` 的 `0x1000`。** 两者容易混。

## 4. 关键事实：`0x80000000` 不是我们的读数

| 出处 | 读数 | 是谁的 |
|---|---|---|
| `work-notes-csf-boot-others.md:166`（引 `Ken-u docs/notes.zh-CN.md`） | `job irq raw 0x80000000` | **RK3588（Ken-u），成功** |
| `logs/.../nopdc/kernel-lines.txt:573` | `job irq 0` | **我们，实测** |
| `work-notes-shared-region.md:1034` | `job irq 0x0` | **预测值**（"改完之后应该看到"），不是实测 |

→ 我们从未观测到 `0x80000000`。这是**成功的对照**，不是我们的数据。

## 5. 裁决

1. **Codex 对：`MASK` 极性是「1=使能」。** 我们写 `0xffffffff` 是对的，**不要改成 0**。
   改成 0 在「1=使能」语义下等于全屏蔽。
2. **子代理 R1 的结论方向不能否定，但它的证据链有错**（引用了不存在的文件/错误行号）。
   真正的问题不是「mask 极性」，而是「**`STAT` 与 `RAWSTAT` 的硬件关系未验证**」。
3. **`if (booted)` 里 `mask=0` 是「启动成功后关中断进入轮询模式」，不是解屏蔽。**
   Codex 2026-11-15 02:45 指出这点，DSH 接受。
4. **确凿的观测缺口**：`kJobIntRaw(0x1000)` 从未被读取。这是我们唯一的硬缺陷。

## 6. 唯一可判定的实验（只读，零风险）

**不改变任何控制流，只加读数：**

```c
/* 在 start_mcu() 的轮询循环内，与现有 stat 一起读 */
uint32_t stat = mrd(kJobIntStatus);      /* 0x100c，现有 */
uint32_t raw  = mrd(kJobIntRaw);         /* 0x1000，新增 */
/* 在 ::3986 的判定前，或循环超时后，打印两者 */
printf("mali: job int raw %#x stat %#x mask %#x\n",
       raw, stat, mrd(kJobIntMask));
```

**判据矩阵：**

| `RAWSTAT` bit31 | `STAT` bit31 | `MASK` bit31 | 结论 |
|---|---|---|---|
| 0 | 0 | 1 | **MCU 真的没置位** → 现有的「MCU 零执行」结论成立，回到取指/固件方向 |
| 1 | 0 | 1 | **硬件是 `STAT = RAWSTAT & ~MASK`** → 子代理 R1 对，改 mask 或改读 RAWSTAT 就能看到 |
| 1 | 1 | 1 | 两者都置位而我们的判定没生效 → 代码 bug（判定条件写错） |
| 0 | 1 | 1 | 不可能，说明读到了别的寄存器 → 地址错误 |

**这是唯一能把三方争议一次性钉死的实验，成本 1 行 printf + 1 行读取。**

## 7. 未验证

- `STAT` 与 `RAWSTAT` 的确切硬件关系（两种可能都未被任何源码明确写出）
- 我们那次 `job irq 0` 读数时 `MASK` 的实际值（代码是 `0xffffffff`，但没有回读确认）
- `kJobIntRaw` 读到非零值时，是否真的对应 GLOBAL_IF 语义
