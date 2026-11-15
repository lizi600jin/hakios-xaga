# trial 696（witness 单变量）结果

> 2026-11-15 13:22，DSH。**实机执行，已恢复原厂。**
> 镜像 md5 `b750fd86b73affbeb318377b6b8c779a`，
> sha256 `48c7c042917087e4c4fade613edec47f7bef589f2ceaf42cf9dcbdfe7660556a`。

---

## 0. 一句话结果

**32 字节的最小 Cortex-M stub 也没有执行任何一条 `str`。**
复位向量确认被改成了 `0x1000001`（patch 生效），但三次读回共享页全是哨兵 `0xff5aa5f0`，
两个 marker（`0x11` / `0x22`）**都没出现**。

**同时：哨兵本身读得回来** —— 共享页映射、CPU 读写、cache flush 整条链都是好的。

→ **问题不在固件、不在段布局、不在 PROT 分支，而在「MCU 取指本身没有发生」。**

---

## 1. 日志原文（`trial696-full.log`）

```
[   34.827563]: mali: witness: vector was sp 0x2040000 reset 0x10018ed
[   34.827594]: mali: witness: code at 0x1000000, reset vector now 0x1000001
[   44.492408]: mali: witness: read1 0xff5aa5f0 0 read2 0xff5aa5f0 0 read3 0xff5aa5f0 0 trial 696
[   44.493581]: mali: witness: verdict trial 696 -- no completed first store observed (not an instruction trace)
```

| 行 | 含义 |
|---|---|
| `:2918` | 原始向量：`sp 0x2040000`，`reset 0x10018ed`（与固件一致） |
| `:2919` | **patch 生效**：向量已改为 `0x1000001`（Thumb 位 + 段 1 基址） |
| `:3237` | 三次读回全 `0xff5aa5f0`，**零 marker** |
| `:3238` | 驱动自己的判读：**没有观察到完成的第一笔 store**（明确写了这不是指令追踪） |

---

## 2. 同时能证明的事（这些是好消息）

| 项 | 值 | 含义 |
|---|---|---|
| 哨兵能读回 | `0xff5aa5f0` × 3 | 共享页**映射正确**，CPU 读写通 |
| 共享段布局 | `VA 0x4000000-0x4094000 size 0x94000` | 映射范围正确 |
| GLB 结构 | `version 0xff5aa5f0 features 0 input_va 0x4002000 output_va 0x4003000` | 与固件声明一致 |
| GLB 其余 | `groups 8 stride 0xa0 perfcnt 0 instr 0x71` | **与官方固件逐字相同** |
| span 校验 | `input_span_ok 1 output_span_ok 1` | 输入输出区间都在段内 |
| AS0 | `AS0 enabled: root 0x1a6d70000` | 地址空间激活成功 |
| 页表走查 | `coverage: every section fully mapped` | 10 段全覆盖 |
| MMU 中断 | `mmu irq 0`（启动期间） | **没有任何总线事务发生** |
| MMU fault | `faultstatus 0 faultaddress 0` | 无 fault |

---

## 3. 与 trial 695 的对比（单变量）

| 项 | 695 | 696 |
|---|---|---|
| `mali_witness` | `0` | **`1`** |
| kernel sha256 | `18bb68da…` | `18bb68da…` **同一文件** |
| payload sha256 | `0cdc1c25…` | `0cdc1c25…` **逐字节相同** |
| `MCU status` | `1` | `1` |
| `job irq` | `0` | `0` |
| `mmu irq`（启动期间） | `0` | `0` |
| `L2 up` | `trans 0 active 0 ready 0x1` | 同 |
| `shader ready` | `0` | `0` |
| 结果 | 固件不执行 | **连 32 字节 stub 也不执行** |

**695 与 696 在 MCU 侧的表现完全一致**，唯一区别是这次用了最小程序，
排除了「固件太复杂所以出错」这一整类解释。

---

## 4. 这排除掉的方向（可以正式划掉）

| 方向 | 为什么排除 |
|---|---|
| 固件本身有问题 | 32 字节自己的代码也不跑 |
| 段布局 / entry table 解析错 | 同上；且 GLB 结构解出来与官方固件逐字相同 |
| PROT 段处理错 | witness 完全不碰 PROT 段，仍不跑 |
| 共享页映射错 / cache 不一致 | 哨兵能读回，说明映射与刷新都对 |
| AS0 未激活 | `AS0 enabled: root 0x1a6d70000` |
| 页表覆盖不全 | `coverage: every section fully mapped` |
| MMU fault 导致静默停止 | `faultstatus 0`，`mmu irq 0`（连 fault 都没有） |
| 复位向量没改成功 | `reset vector now 0x1000001`，patch 明确生效 |

---

## 5. 剩下的解释（按可信度）

### 5.1 最强：MCU 核心没有被真正唤醒 / 没有时钟或电源到核

证据链：

1. **32 字节 stub 也不执行** —— 与代码复杂度完全无关；
2. **启动期间 `mmu irq 0`** —— 一个正在取指的核必然产生取指事务，走 AS0 页表，
   至少会产生一次请求完成；实测**一次都没有**；
3. **`MCU_STATUS = 1 (ENABLED)`** —— 按 panthor 语义，ENABLED 只说明「MCU 在跑」这个
   状态位被置了，**不说明它在取指**（`panthor_fw.c:1153` 的 `"enabled"` 字符串
   就是为了打印「失败但 status=enabled」这种组合）；
4. **`MCU_CONTROL=0` 之后 status 仍为 1** —— 不跟随状态机，说明这个寄存器
   可能并不反映核心的真实运行状态。

### 5.2 同样强：GPUEB 从未被请求

日志两次明确：

```
[   34.651658]: mali: gpueb before the address spaces: not mapped
[   46.619655]: mali: gpueb after the failed start: not mapped
```

GPUEB 是 MTK 的电源管理协处理器。仓库 `MASTER-STATE.md §4.3` 早已把它列为**头号嫌疑**，
并且完整协议已还原（`findings/GPUEB-IPI-PROTOCOL.md` 54 KB + `gpueb-ipi.h` 26 KB，
编译零警告，模拟器 4/4 通过）。

**trial 696 的结果与这个假设高度相容**：如果 MCU 的电源域要由 GPUEB 打开，
那么它当然不会执行任何指令 —— 无论代码多简单。

### 5.3 已排除：L2 未上电

`L2 up: trans 0 active 0 ready 0x1` —— `ready = 1`，L2 是起来的。
上游 panthor 在启动固件前**只给 L2 上电**（`panthor_gpu.c:302`，`panthor_fw_init:1521`），
shader 由固件自己开（`Ken-u` 笔记记录 `0 → 0x50005`），
所以 `shader ready 0` **是正常态，不是缺陷**。

---

## 6. 顺带：Android 卡住是可复现的

| | trial 695 | trial 696 |
|---|---|---|
| `surfaceflinger` 崩溃次数 | 38 | **38** |
| `boot_completed` | 未置位 | 未置位 |
| 差异 | 18 行只读诊断 | **只有 `mali_witness` 一个参数** |

**两次症状完全相同，而 696 相对 695 只改了 witness。**
所以这不是某个诊断导致的，是 **XNU 返回 Android 时 GPU 状态恢复不完整**。

用户照片显示屏幕停在 loader 打完 `starting Android` 之后 ——
**loader 已交接，是 Android 图形栈起不来。**

驱动源码里 `mali_power_down()` 的注释（`mtkloader.c:6908-6914`）已经记录过这风险：

> "Leaving it released was a boot loop — the run came back once, then reset before adb:
> domains off but SRAM still connected is a combination Android's gpufreq never creates,
> and the sleep controller reports it as a domain that will not settle."

**这条与 GPU/MCU 启动失败是两个独立问题**，但每次刷机都会撞上。

---

## 7. 恢复记录

| 步骤 | 结果 |
|---|---|
| 刷 trial 696 | `OKAY`，0.783s |
| XNU 运行 | ~150s，13:13:28 → 13:16:53 返回 adb |
| 取日志 | `HLOG @ 0x9f8f8000`，`len 0x72e8a = 470666`，`sum @64 = 0x24da28c` **校验一致** |
| 刷回原厂 | `backup/boot_a_stock_real.img`，`OKAY`，1.900s |
| Android 恢复 | `boot_completed = 1`，`SurfaceFlinger` 正常 |
| `boot_a` 最终 | **`a63b2e35a7c2522cb24a6a10332b9bc8`**（= 原厂） |

---

## 8. 下一步唯一待决问题

**MCU 为什么一次取指都不发？** 只剩两个候选，都能设计成单变量：

| 候选 | 实验 | 成本 |
|---|---|---|
| **GPUEB 未请求** | 按已还原的协议发 `CMD_POWER_CONTROL`，再启动 MCU | 中（要写 IPI 代码） |
| **核未上电/未时钟** | 读 MTK 侧 MFG 时钟与电源状态寄存器，确认是否到核 | 低（只读） |

**建议先做「只读确认」**：在 `MCU_CONTROL=AUTO` 前后读 MFG 时钟相关的
PLL/分频器实际输出使能位，确认 GPU 核心时钟是否真的在跑。
这与 witness 结果直接相关且零风险。
