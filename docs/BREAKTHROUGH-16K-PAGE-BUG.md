# 重大发现：16K 页 / 4K 页表步进 bug（已修复）

时间：2026-11-13 03:20
发现者：子代理 760ce204 审计 witness 内存映射时揪出，主控独立验证

---

## 一、bug 是什么

`load_firmware()` 里给固件各个 section 建页表的循环：

```c
for (va = va_start; va < va_end; va += PAGE_SIZE)     // PAGE_SIZE = 16384 (16K)
    pt_map(gMcuRoot, va, mem_pa(...), leaf);
```

而 `pt_map()` 的叶索引是 4K 粒度：

```c
table[(va >> 12) & 0x1ffu] = (pa & PTE_ADDRESS) | leaf;   // 4K
```

**结果：每 16K 只写 1 个 4K 叶，剩下 12K 没有 PTE。**

## 二、为什么 PAGE_SIZE 是 16K

链式证据（全部实测/源码可查）：

1. loader 发布 `dram-size = 0xfe000000`（4.26 GB）
   —— `mtk/loader/mtkloader.c:48, 1366`
2. `arm_init.c:413-417`：`if (args->memSize > 1GB) PAGE_SHIFT_CONST = PAGE_MAX_SHIFT;`
3. `vm_param.h:60`：`PAGE_MAX_SHIFT = 14` → `PAGE_SIZE = 16384`
4. 日志实证：页表分配步进是 `0x4000`（16K），root 表在 `0x50000000`、
   sec0 在 `0x50004000`、L1/L2/L3 在 `0x50008000/0x5000c000/0x50010000`
   —— 只有 16K 页模型才能复现这组地址

## 三、覆盖度实测（修复前）

| section | 需要的 4K 叶 | 实际写入 | 覆盖率 |
|---|---|---|---|
| sec0 (0x0-0x1000) | 1 | 1 | 100% |
| sec1 (0x1000000-0x1010000) | 16 | 4 | **25%** |
| sec2 (0x2000000-0x203e000) | 62 | 16 | **25.8%** |
| sec5 (0x2800000-0x2840000) | 64 | 16 | **25%** |
| sec6 (0x3000000-0x303e000) | 62 | 16 | **25.8%** |
| sec9 (0x4000000-0x4094000) | 148 | 37 | **25%** |

## 四、这个 bug 精确解释了我们的症状

**厂商固件的复位向量 `0x010018ed` 落在 section 1 的 leaf 1（0x1001000-0x1001fff）。
leaf 1 从未被映射。**

而实际映射的叶是 0、4、8、12（因为循环每次跳 0x4000）。

→ MCU 写完 `MCU_CONTROL=AUTO`，从 `0x10018ed` 取下一条指令，
→ **该 VA 无有效 PTE**
→ 取指失败。但 Cortex-M7 对「从未成功发出的取指」不产生 data abort，
→ 所以 `AS_FAULTSTATUS = 0`、无 MMU 中断、`debug_top` 变 busy 后不变
→ **状态机接受了 AUTO 写（纯寄存器操作），但一个字都没执行**

**这与十几轮观察到的每一个症状完全吻合。**

## 五、为什么 witness 实验「看起来」能跑

witness 桩的三处关键地址**都恰好落在已映射的 leaf 0**：

| 用途 | VA | leaf | 状态 |
|---|---|---|---|
| 桩代码 | 0x1000000-0x100001f | 0 | 已映射 ✓ |
| store #1 | 0x04000000 | 0 | 已映射 ✓ |
| store #2 | 0x04000004 | 0 | 已映射 ✓ |

所以「如果它执行了，CPU 应该能看见」这个前提是成立的。
但**桩也没执行** —— 因为 MCU 连第一条指令都没取到：
复位向量 `0x10018ed` 在 leaf 1，**取指就已经断了**，永远走不到桩。

**witness 的结论「MCU 一条指令都没执行」依然成立，但原因现在有了机械解释：
不是固件问题，不是时钟问题，是页表少映射了 75%。**

## 六、修复

```c
/* 新增常量 */
#define PT_LEAF_GRANULE  0x1000ull    /* pt_map 实际索引的粒度 */

/* 循环改用它 */
for (va = va_start; va < va_end; va += PT_LEAF_GRANULE)
```

并加了覆盖度自检（`load_firmware()` 末尾），逐 4K 走一遍并对比：

```
mali: coverage: every section fully mapped
```
或
```
mali: COVERAGE secN 0x...-0x...: X of Y leaves mapped
mali: coverage: HOLES FOUND
```

## 七、验证结果（trial 660，mali=3 安全模式）

```
37.953340 mali: coverage: every section fully mapped
37.937927 mali: walk 0x1000000: L0 0x50008403 L1 0x5000c403 L2 0x50024403 L3 0x50014fc3 page
37.951135 mali: 10 sections, 9 page tables
```

**覆盖度从 25% 变成 100%，手机正常回 Android。**

## 八、尚未验证的

**trial 661（`mali=6`，映射已修复）刷入后手机卡在「回 Android」阶段**，
没有取到日志，所以**尚不知道 MCU 是否因此启动**。

这是唯一还悬着的关键问题。

## 九、对其他结论的影响

这个 bug 意味着，**此前所有轮次的结论都建立在一个只映射了 1/4 地址空间的页表上**。
以下这些「已排除」的结论需要重新审视（它们当时是在坏映射下测的）：

- AS0 位置（trial 400）
- 13 次诊断 MMU clear（trial 401）
- 2 次 power IRQ clear（trial 402）
- AS2/AS3 MEMATTR（226/236）
- free-run 位（216/236）
- 固件缓存策略（604）
- 固件段可执行位（605）
- PDC（601/602）
- 域上电、时钟、总线保护、AOC

**但它们「无效」这个事实本身没错** —— 在坏映射下它们本来就该无效。
现在映射修好了，这些变量**可能值得重测**，尤其是和固件加载顺序相关的那些。

不过优先级应该是：**先确认 MCU 是否启动**，再回头重测。

---

## 十、验证更新（trial 660 / 662，均为 mali=3）

两次刷入都确认：

```
mali: 10 sections, 9 page tables
mali: coverage: every section fully mapped
mali: trial 66X mali=3 stop: level 3 cold 1 low 1 preas0 1
```

**覆盖度 25% -> 100%，且两次都正常回 Android。**

## 十一、仍未验证的核心问题

`mali=6`（真正启动 MCU）在映射修复后**还没成功取到日志**：

- trial 661（mali=6 + 映射修复）刷入后卡在「回 Android」阶段，用户手动恢复
- trial 660 / 662（mali=3）能正常回，说明**映射修复本身不导致卡死**
- 所以卡死很可能与「MCU 真的开始执行」有关 —— 这是一个**新的、有意义的信号**

**换句话说：修复前 mali=6 每次都"正常"回 Android，因为 MCU 什么都没做；
修复后 mali=6 卡住了，因为 MCU 可能真的开始运行了，然后撞上了下一个问题。**

这是十几轮以来第一次看到 mali=6 与 mali=3 的行为出现差异。

## 十二、下一步（离线也能做）

1. **加卡死诊断**：在 `chain_android()` 之前多打几个点，或在 `start_mcu()` 之后
   尽快把关键状态写进 logbook（失败路径已有一条 AOC verdict，可以扩展）
2. **怀疑对象**：MCU 真的执行后，可能撞上固件自身的初始化失败（比如 shared section
   的 `csg_control` 在 VA 0x04001000，修复前是空洞、修复后是有效页 —— 行为会变）
3. **降风险刷法**：把 `mali=5`（走到 global interface 之前停）而非 6，看是否也卡
   —— 这能区分"MCU 执行导致卡"还是"越界访问导致卡"

---

## 十三、trial 663（mali=5）结果 —— 重要区分

```
39.180379 mali: coverage: every section fully mapped
49.245663 mali: trial 663 after AUTO: level 5 ...
49.351412 mali: version word 0xff5aa5f0 (untouched)
49.352932 mali: the global interface never raised; the MCU did not boot
```

**mali=5 正常回 Android，但 MCU 仍未启动。**

### 三档行为对比（映射修复后）

| level | 行为 | 回 Android | MCU 启动 |
|---|---|---|---|
| 3 | L2 上电后停，不写 AUTO | ✅ 正常 | 未请求 |
| 5 | 写 AUTO、等 bit31，但不碰 global interface | ✅ 正常 | ❌ |
| 6 | 写 AUTO、等 bit31、调 global_interface() | ❌ **卡死** | ❌ |

**这是十几轮以来第一次看到 level 6 与 level 5 行为分离。**

说明：
1. 映射修复是**真实且有效**的（coverage 100%，且 level 3/5 都正常）
2. `MCU_CONTROL=AUTO` 之后的等待循环（level 5 也做）**不卡**
3. **卡在 `global_interface()`** —— 也就是访问 `gShared->mem.kva` 的 `control`/`input`/`output`

### 为什么 global_interface 会卡

`csg_control` 在 `gShared->mem.kva + 0x1000`（即 VA 0x04001000）。
映射修复前：那是**空洞**，读它会怎样不明。
映射修复后：那是**有效页**，但指向 sec9 的物理内存 —— 而 **MCU 从没写过那里**。

所以现在读的是**未初始化的零页**，`control->version` 读到 0，
后面按 version 解析结构体可能算出越界偏移 → 访问到不该碰的地址 → 卡死。

**这是一条全新的、有明确机制的假设。**
