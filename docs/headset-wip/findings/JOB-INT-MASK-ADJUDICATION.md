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

---

# 附录：只读补丁复核（2026-11-15 02:55）

Codex 交付 `logs/resume-sol-20261114-231152/gpu-jobirq-readonly-proposal/jobirq-readonly.diff`。
DSH 在**隔离副本**（`/tmp/jirq`）复核，**未改动 `/home/lizi/IOS/hakios-mtk`**。

## A. 补丁内容（55 行，1 helper + 5 个调用点）

```c
static void
job_irq_snapshot(const char *where, unsigned spins)
{
	uint32_t raw_before = mrd(kJobIntRaw);
	uint32_t mask = mrd(kJobIntMask);
	uint32_t status = mrd(kJobIntStatus);
	uint32_t raw_after = mrd(kJobIntRaw);

	printf("mali: jobirq probe trial %u %s spin %u: raw %#x/%#x stat %#x mask %#x raw_same %u\n",
	    gMaliTrial, where, spins, raw_before, raw_after, status, mask,
	    raw_before == raw_after ? 1u : 0u);
}
```

调用点：`before AUTO` / `sample`（沿用原有 4 个 spin 采样点）/ `GLOBAL_IF observed by existing loop` /
`loop end before any boot-success clear`。

## B. 我的独立复核结果

| 检查项 | 结果 | 方法 |
|---|---|---|
| `git apply --check` | **通过** | 原树 check |
| 隔离副本实际应用 | **成功** | `/tmp/jirq` 副本，应用后 grep 到 5 处调用 |
| 括号配平 | **452 `{` / 452 `}`** | 全文件计数 |
| helper 内是否有写操作 | **没有 `mwr`** | 文本检查 |
| `kJobIntRaw = 0x1000` 读取是否合法 | **合法** | `panthor_fw_regs.h:20` `JOB_INT_BASE 0x1000`；该块布局 `+0` RAWSTAT / `+4` CLEAR / `+8` MASK / `+0xc` STAT（与 `panthor_device.h:491-494` 的 `INT_RAWSTAT 0x0` / `INT_CLEAR 0x4` / `INT_MASK 0x8` / `INT_STAT 0xc` 偏移一致） |
| `mrd()` 对任意 offset 可用 | **是** | `hakios.mali610.c:567` `#define mrd(at) (*(volatile uint32_t *)(gGpu + (at)))` |
| 是否改 mask 极性 | **没有** | 全文只有 `mrd(kJobIntMask)`，无 `mwr(kJobIntMask, ...)` |
| 是否改原有启动判据 | **没有** | `:3986` 的 `if (stat & kJobGlobalIf)` 原样保留 |
| 是否清中断 | **没有** | 无 `kJobIntClear` 写 |

## C. 判据矩阵：Codex 的收窄我接受

Codex 2026-11-15 02:33 给出收窄版，比我原先那版严格。**逐条核对他引用的行号，全部成立：**

| 他引用的 | 实际内容 | 成立 |
|---|---|---|
| `mali_kbase_pm_driver.c:2128-2151` | `kbase_pm_enable_interrupts`，注释原文 **"Clear all interrupts, and unmask them all"**，`:2145` 写 `JOB_IRQ_MASK = 0xFFFFFFFF` | ✅ |
| `:2156-2171`（`:2167`） | `kbase_pm_disable_interrupts_nolock`，注释 "Mask all interrupts"，写 `0` | ✅ |
| `panthor_device.h:605-616`（`enable_events`） | `pirq->mask \|= mask` 后写硬件 | ✅ |
| `:619-630`（`disable_events`） | `pirq->mask &= ~mask` | ✅ |
| `:577-583`（`resume`） | `gpu_write(INT_MASK, pirq->mask)` 原样写 | ✅ |
| `panthor_fw.c:1128` | `panthor_job_irq_enable_events(&ptdev->fw->irq, ~0)` | ✅ |
| `hakios.mali610.c:3898` | `st = mrd(kMcuStatus)`（0x704），HALT/FATAL 查它 | ✅ |
| `:3925/:3929` | HALT/FATAL 条件用 `st`，**不经 JOB_INT_MASK** | ✅ |

**→ 「MASK 位 1 = 使能」由 vendor 注释 + panthor 三处实现双重确证。我们的 `0xffffffff` 正确。**

## D. 收窄后的判据矩阵（我采纳）

| R31 / S31 / M31 | 能说什么 | **不能**说什么 |
|---|---|---|
| `0/0/任意` | 本次采样未观察到 GLOBAL_IF | **不能**证明 MCU 零执行，也不能证明整段时间从未置位 |
| `1/0/0` | 与「使能集 mask 屏蔽该位」相容 | **不能**据此说 mask 极性反了 |
| `1/0/1` | 有待解释的视图/采样差异，先查 RAW 前后、回读和时序 | **不能**由这一组值确定 `STAT = RAW & ~MASK`，不能直接改 mask |
| `1/1/1` | 此采样观察到有效 GLOBAL_IF | 若出现于 timeout 之后，**不等于**原循环判定漏掉；要核对原 stat 条件样本/镜像身份 |
| `0/1/任意` | 先查跨读边沿/清除、视图及采样稳定性 | **不能**直接断言地址错误 |

**我原先那版矩阵把 `0/0` 直接判成「MCU 真没置位」、把 `1/0` 直接判成「硬件是 & ~MASK」——
两处都过强，接受收窄。**

## E. 补丁的一个价值点（Codex 提到、我确认）

`raw_before`/`raw_after` **夹住** `mask`/`status` 的读取，并用 `raw_same` 标记两次 RAW 是否相同。
这是我能想到的在纯轮询环境里检验「读取期间是否有边沿变化」的最省做法。

但 Codex 自己标注的边界是对的：**`raw_same` 只说明两次 RAW 相同，不承诺硬件原子快照**；
诊断本身增加 MMIO 与 printf 开销，**不能说零干扰**。

## F. 结论

1. **补丁可应用、只读、不改 mask、不改判据、不清中断。** 这是正确的实验设计。
2. **判据矩阵必须按 §D 的收窄版用**，我原先那版有两处过强，作废。
3. **补丁尚未应用**（Codex 未应用/未构建/未刷机），冻结源码与镜像哈希未变。
4. **是否实际跑这条实验需要另立 trial + 新镜像 + 完整哈希登记。**
   冻结首轮 `headset=1` 候选（`5e7c67f7cd8aeb90a86ff4098466812d`）不替换。
5. **补丁不能直接解释「共享区 version 仍是哨兵」** —— 那是独立判据，两者要分开看。
