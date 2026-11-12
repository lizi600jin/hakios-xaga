# Round 6：实验 C（PDCv2 交接）—— 三次刷机，判定「PDC 未接管」

执行：DSH 主控（codex 双账号额度耗尽，按用户授权临时接管手机）
日期：2026-11-12 23:31–23:56（Asia/Shanghai）

---

## 0. 一句话结论

**PDCv2 路径走通了一半，但 PDC 硬件没有接管 mfg2..mfg12。**
`0xF3C` 仍然是 `0x6`（只有 mfg1），不是期望的 `0x3FFE`。
**F3C 判据明确否掉"只上 mfg1 + PDC 使能就能让 12 域全上电"这个假设。**

过程中抓到并修掉一个**真实的顺序 bug**（第一次刷机时 PDC 写在时钟门打开之前，17 笔 store 全部读回 0）。

---

## 1. 三次刷机（同一内核，只改 boot-args 与 pdcauto 的先后位置）

| trial | mali= | pdcauto | 镜像 md5 | 关键结果 |
|---|---|---:|---|---|
| 600 | 6（全流程） | 1 | `4ea1ae324122f5e25ff47da02af11aeb` | PDC 输出被 ring buffer 冲掉，**判定不可得** |
| 601 | 1（只上电） | 1 | `5d58bb519ea0262c67fb618a89429c1a` | PDC 写在时钟门之前 → 17 笔全读回 0 |
| 602 | 1（只上电） | 1（顺序已修） | `29cfe270954e30ad463cb5c83e9e6a13` | PDC 写入正常，但 `0xF3C` 仍 `0x6` |

驱动源码 sha256：602 为 `3cb6b5bb51b99a7526ae3ea6dd4e71c27e61ae0036eed24093d6fc9e5760e188`。

三次都正常返回 Android（`sys.boot_completed=1`，分别 23:35:34 / 23:41:12 / 23:56）。三次未用救砖包。

---

## 2. 抓到并修掉的 bug：PDC 写在时钟门打开之前

### 第一次（trial 601）的失败形态

```text
21.138828 mali: pdc auto: mfg1 up, 17 pdc words written
21.138836 mali: pdc: sc0 0 sc9 0 cg 0 st0 0 st4 0 st6 0     ← 七个数全是 0
21.342752 mali: pdc auto: 0xF3C 0x6 (want 0x3ffe) ...
```

`sc0 = 0` 而不是 `0x2000001`，说明**十七笔写全部没落地**。

### 原因

补丁脚本把 PDC 分支插在 `power_on()` 开头，而 `power_on()` 在 `mali_bring_up()` 里被调用于 **`clock_26m()` 之前**（:3808 vs :3849）。
`mfgcfg` 在 `0x13FBF000`，**物理上位于 mfg1 域内**，其 gate 是 `BG3D`，只有 `clock_26m()` 才会打开。

驱动自己在 :3853 就写着这句话：

> `mfgcfg's own gate is BG3D, and clock_26m() is what opens it. Written while the gate is shut, all seventeen stores read back as zero -- which is exactly what the first run of this did`

**补丁作者没有读到这条注释，把 PDC 放在了它警告过的位置。** 我按这条注释把 PDC 移到 `clock_26m()` 之后。

### 修正后（trial 602）

```text
21.309869 mali: pdc auto: 17 pdc words written (gate open)
21.309896 mali: pdc: sc0 0x2000001 sc9 0 cg 0x2000001 st0 0x2000001 st4 0x2000001 st6 0
21.511166 mali: pdc auto: 0xF3C 0x6 (want 0x3ffe) 0xF40 0x6 0xF34 0x1fc
21.511184 mali: pdc auto: 1 of twelve domains up via PDC
```

写入确实生效了（`sc0 = 0x2000001`、`cg = 0x2000001`、`st0/st4 = 0x2000001`），
但等 200 ms 之后 `0xF3C` 仍是 `0x6`。

---

## 3. 判据解读（这是本轮最有价值的部分）

### 3.1 `0xF3C = 0x6` 意味着什么

`0xF3C` 的 bit (n+2) 对应 MFGn，掩码 `0x3FFE`。
`0x6` = bit1|bit2 = **只有 bit2（mfg1）**，其余 11 个域全 0。

对比 `0xF34 = 0x1FC`（bits 2..8，7 个域）：
**两个寄存器对同一件事给出了不同的答案**，而且 `0xF3C` 比 `0xF34` 更悲观（1 vs 7）。

这说明 `0xF3C` 与 `0xF34` **不是同一份状态的两种视图**。可能：
- `0xF3C` 是 gpufreq 侧的镜像，只有在 gpufreq 自己的初始化路径跑过之后才会更新
- `0xF34` 是 scpsys 的实时状态，`0xF3C` 需要额外的同步动作

**证据边界**：我们只证明了 `0xF3C` 在裸机读出来是 `0x6`，**没有证明**它一定反映真实供电状态。
如果 `0xF3C` 是个需要"喂"的镜像寄存器，那用 `0x6` 否掉 PDC 就是错的。
这一点必须在下一次实验中排除（见 §5）。

### 3.2 `pdc: sc9 0` 与 `st6 0` 一直为 0

三次实验中 `sc9` 和 `st6` 恒为 0，其余五个从 0 变成 `0x2000001`。
说明 PDC 的 17 个寄存器里至少有 2 个**从来没被正确寻址或使能**。
`st6 = 0` 尤其可疑 —— 如果 `st6` 是第二层 shader 栈的使能位，它恒 0 正好对应 mfg8..mfg12 起不来。

### 3.3 PDC 使能后 mfg5/6/10/12 控制字的变化

```text
trial 601: after pdc: mfg5 0x1112 mfg6 0x1112 mfg10 0x1112 mfg12 0x1112
trial 602: after pdc: mfg5 0x1112 mfg6 0x1112 mfg10 0x1112 mfg12 0x1112
```

对比旧路径（round-5）的 `0x100d`：`0x1112` 与 `0x100d` 差异在 bit 1/4/8，
但 `sta` 字段（bit31:30）都是 0 → **仍然没有真正上电**，只是控制字里多了几个请求位。

---

## 4. 与之前 9 轮的对比（域上电从未变化）

| 轮次 | 路径 | `0xF34` | `0xF3C` | 域数 |
|---|---|---|---|---|
| 203/206/216/226/236/246/400/401/402 | 逐个真写 mfg2..mfg12 | `0x1FC` | `0x4` | 7/12 |
| 601 | PDC，但时钟门关着 | `0x1FC` | `0x6` | 7/12（PDC 写全失败） |
| 602 | PDC，时钟门已开 | `0x1FC` | `0x6` | 7/12（PDC 写成功但没效果） |

**唯一的变化是 `0xF3C` 从 `0x4` 变成 `0x6`**，即多了 mfg1 那一位。
这更支持"`0xF3C` 是 gpufreq 侧镜像、不随 PDC 更新"的解释 —— 因为如果 PDC 真接管了，它应该跳到 `0x3FFE`。

---

## 5. 下一步（按我判断的优先级）

### P1：先验证 `0xF3C` 到底是不是可信的状态源（**不用刷机**）

在 trial 602 的内核里加一段：**同时连续读 `0xF34` / `0xF38` / `0xF3C` / `0xF40` 若干次**，
并打印 `0xF3C` 的原始值（不带 `& 0x3ffe` 掩码，看高位有什么）。

如果 `0xF3C` 高 16 位有非零值，说明它是个带状态标志的复合寄存器，低位不能直接当域状态读。

### P2：查 `st6` 为什么恒 0（**最容易有收获**）

`pdc_report()` 打印 `sc0 sc9 cg st0 st4 st6`，其中 `sc9` 与 `st6` 恒 0。
- `sc9 = 0` 可能是索引 9 对应的核不存在（G610 只有 6 个 shader core）
- **`st6 = 0` 必须查清**：如果它对应第二层栈的使能，那 mfg8..mfg12 起不来就有解释了

需要：`pdc_enable()` 里 `MALI_PDC_CORES` 的定义、`st` 系列寄存器的 offset 表、
以及 MT6895 上 PDC 的 core→domain 映射（来自 `gpufreq_mt6895.c` 或 DTS）。

### P3：按用户/web 端主控的判据回报

按 MASTER-BRIEF 的判读规则：
> 仍是 `0x1FC`/`0x4` → PDC 没接管，还缺一个使能，回报给我继续挖。

**当前是 `0x1FC` / `0x6`，属于"PDC 没接管"。** 我倾向于认为还缺的是
**PDC 侧的主动触发**（不只写 `MFG_ACTIVE_POWER_CON`，还要写 `PDC_..._REQ` 之类），
以及 `st6` 那个恒 0 的寄存器。

---

## 6. 本轮改动（驱动源码）

1. 修掉 `mali_pdcauto` 分支的位置：从 `power_on()` 开头 → `clock_26m()` 之后
2. 修正后 `pdcauto` 分支不再重复统计、不再 `return up` 提前退出，改为让 bring-up 继续走
3. 顺带加了一个独立开关 `mali_domainwait`（默认 0）：让 `domain_on()` 的两处轮询
   按 scpsys 的方式 **10 µs 间隔 / 1 秒预算**，而不是现在这种 **0.72 秒无延时紧循环**。
   **本轮三次刷机都没打开它**（`domainwait 0`），留作下一个独立变量。

### 关于 `mali_domainwait` 的实测依据

从 trial 402 日志量出：相邻两次域放弃间隔 **0.723 s**，即每域 2,000,000 次无延时读共约 0.72 s。
而 scpsys 的语义是 `MTK_POLL_DELAY_US=10` × 100,000 次 = **1.0 s**。
**0.72 s < 1.0 s**，每域少等约 28%。这意味着当前代码可能在硬件还没答复时就放弃，
然后对"仍在握手中"的域清隔离、抬复位 —— 正是驱动注释里描述的失败形态。

**证据边界**：这只证明"等待预算比 vendor 短 28%"，**不证明** 5 个域是因为等不够才失败。
要区分这两者，必须跑 `domainwait=1` 对照。

---

## 7. 证据文件

| 文件 | 内容 |
|---|---|
| `trial-600-logbook.bin` / `-mali.txt` | mali=6 全流程（PDC 输出被 ring 冲掉） |
| `trial-601-logbook.bin` / `-mali.txt` | mali=1，PDC 写在时钟门之前（全读 0） |
| `trial-602-logbook.bin` / `-mali.txt` | mali=1，PDC 顺序已修（写入生效但 F3C 未变） |
| `trial-600-flash.txt` / `-reboot.txt` | 刷入与重启原始输出 |
| `driver-final.c` | 改动后的驱动快照 |
| `driver-sha.txt` | 驱动与镜像哈希 |

关键原始行（trial 602）：

```text
21.106260 mali: mfg1  MPU on: 0xc000000d
21.106267 mali: pdc auto: mfg1 up (PDC deferred until after clock_26m)
21.307329 mali: pdc auto: 0xF3C 0x6 (want 0x3ffe) 0xF40 0x6 0xF34 0x1fc
21.309869 mali: pdc auto: 17 pdc words written (gate open)
21.309896 mali: pdc: sc0 0x2000001 sc9 0 cg 0x2000001 st0 0x2000001 st4 0x2000001 st6 0
21.511166 mali: pdc auto: 0xF3C 0x6 (want 0x3ffe) 0xF40 0x6 0xF34 0x1fc
21.511184 mali: pdc auto: 1 of twelve domains up via PDC
21.511242 mali: after pdc: mfg5 0x1112 mfg6 0x1112 mfg10 0x1112 mfg12 0x1112
```

---

## 8. 追加分析：SHADER_READY 迷思（主控自查，含一次自我否定）

### 8.1 一度以为找到了根因

我们所有轮次：`shader ready 0 tiler ready 0 l2 ready 0x1`
而驱动注释说 Android 是 `SHADER_READY = 0x00130013`。

第一反应是：**"Android 的 shader 核在启动 MCU 前就 ready，我们的没有，所以根因是 shader 栈没电。"**

### 8.2 用完整 2460 条轨迹核对后，这个推论被否掉

| record | 事件 |
|---|---|
| rec136 | `MCU_CONTROL = 0x2`（AUTO） |
| rec163 | `JOB_INT_STATUS` bit31 升起（AUTO 后 **+27** 条） |
| **rec228** | **`SHADER_READY` 首次 = `0x130013`（AUTO 后 +92 条，bit31 后 +65 条）** |

六次 AUTO 全部同构：

```
AUTO@136 → bit31@163 → READY@228
AUTO@549 → bit31@565 → READY@630
AUTO@935 → bit31@951 → READY@1016
AUTO@1412→ bit31@1428→ READY@1493
AUTO@1805→ bit31@1832→ READY@1898
AUTO@2205→ bit31@2221→ READY@2286
```

**SHADER_READY 每次都是在 bit31 之后才变非零。**

### 8.3 结论（证据边界）

**`SHADER_READY = 0` 是 MCU 没起来的**结果**，不是原因。**
Android 在 AUTO 之前 `0x140` 也读 0（rec106/115/134/139/150/161…全 0）。

所以"shader 栈没电导致 MCU 取不到指"这条假设**不成立**。
我们不该为了点亮 SHADER_READY 去改上电路径。

**这条自我否定避免了下一轮走错方向**，值一次记录。

### 8.4 但 7/12 域的谜团仍在

`PWR_STATUS = 0x1FC`（7/12）、`0xF3C & 0x3FFC = 0x4`（1/12）依然无法解释。
只是它**不是** SHADER_READY 的直接原因。

另外注意：`0xF3C` 的 vendor 掩码是 **`0x3FFC`**（bits 2..13），
我早先探针里写的 `0x3FFE` 是**错的**（多算了 bit1）。
按正确掩码，`0x6 & 0x3FFC = 0x4`，与旧路径 `gpufreq 0x4` **完全相同** ——
**PDC 一个域都没拉起来**，这个判定比之前写的更确定。

---

## 9. trial 603 探针结果（含三次自我否定）

命令：`mali=1 mali_cold=1 mali_preas0=1 mali_low=1 mali_trace=1 mali_trial=603 mali_pdcauto=1`
镜像 md5 `532c1a02ce202d54179f5e5edcfad272`，回 Android 正常。

### 9.1 探针原始输出

```text
21.122986 mali: mfg1  MPU on: 0xc000000d
21.326639 mali: pdc auto: 0xF3C 0x4 (want 0x3ffe) 0xF40 0x4 0xF34 0x1fc
21.366375 mali: pdc: sc0 0x2000001 sc9 0 cg 0x2000001 st0 0x2000001 st4 0x2000001 st6 0
21.572587 mali: pdc auto: 0xF3C 0x4 (want 0x3ffe) 0xF40 0x4 0xF34 0x1fc
21.582814 mali: pwr raw: 0xF3C 0x7 0xF40 0x7 0xF34 0xde0001fd 0xF38 0xde0001fc
21.588009 mali: pdc readback: sc0=0x1 sc1=0x1 sc2=0x1 sc3=0x1 sc4=0x1 sc5=0x1 sc6=0 sc7=0 sc8=0 sc9=0
21.593196 mali: shader/bus: present 0x130013 ready 0 pwron 0 pwrtran 0 l2pwron 0 l2active 0
```

### 9.2 否定一：这颗 GPU 只有 6 个核，不是 13 个

`SHADER_PRESENT = 0x130013` → bits **0,1,4,16,17,20** = **6 位**。
`0x130013` 是 presence 位图，我此前读成"13 个核"是错的。

对应地，`pdc readback: sc0..sc5 = 0x1, sc6..sc9 = 0` —— **6 核芯片当然只有 sc0..sc5**。
`sc9 = 0` 与 `st6 = 0` 都不是缺陷，是正常现象。**我上一轮把这两个当缺陷，是误判。**

### 9.3 否定二：`0xF3C & 0x3FFC = 0x4` 可能恰恰是"成功"

vendor 掩码 `0x3FFC`：bit2 = mfg1、bit3 = mfg2 …… `0x4` = **只有 mfg1 上电**。

而 PDCv2 制度的设计就是：**vendor 只 `pm_runtime_get_sync(mfg1)`，mfg2..mfg12 由 PDC 按需拉起，不常驻**。
`mtk-scpsys-mt6895.c` 里 12 个 MFG 域全带 `MTK_SCPD_BYPASS_INIT_ON`（`mtk-scpsys.h:22`，`mtk-scpsys.c:1355`），
即 scpsys 在 init 时**故意跳过**它们。

**所以"`PWR_STATUS` 必须满 `0x3FFC`"这个判据本身可能是错的。** 我们不该为了点亮它去改上电路径。

### 9.4 否定三：`SHADER_READY = 0` 在 `mali=1` 时完全正常

`mali=1` 在 L2 上电后就停，**从未写 AUTO**。而 Android 的 `SHADER_READY`
首次变 `0x130013` 在 **rec228**，AUTO 在 rec136、bit31 在 rec163 —— **是 MCU 起来之后的结果**。

所以 `ready=0` 在 `mali=1` 时不构成任何证据。

### 9.5 `0xF34` 是复合寄存器

`0xF34 = 0xDE0001FD`，高 16 位 `0xDE00` 非零（bits 25-28、30、31）。
低段 `& 0x3FFC = 0x1FC`（7 域）。**它不是纯粹的域位图**，高位另有状态字段。
之前把它当"纯域状态"读，这个口径也不严谨。

---

## 10. 用真实 kbase 源码核对：AS 写序不是问题

vendor 树里有完整 kbase：`/home/lizi/IOS/work-xaga/k-xaga-16.2/drivers/gpu/mediatek/mali-valhall/gpu/arm/midgard/`

`mmu/mali_kbase_mmu_hw_direct.c:384-404` 的 `kbase_mmu_hw_configure()`：

```c
kbase_reg_write(... AS_TRANSCFG_LO, transcfg);
kbase_reg_write(... AS_TRANSCFG_HI, ...);
kbase_reg_write(... AS_TRANSTAB_LO, ...);
kbase_reg_write(... AS_TRANSTAB_HI, ...);
kbase_reg_write(... AS_MEMATTR_LO, ...);
kbase_reg_write(... AS_MEMATTR_HI, ...);
write_cmd(kbdev, as->number, AS_COMMAND_UPDATE);
wait_ready(kbdev, as->number);
```

**kbase 与 Android 真机同序**（TRANSCFG→TRANSTAB→MEMATTR）。
我们用的是 TRANSTAB→MEMATTR→TRANSCFG。

但上游 panthor 用的是我们的顺序，**两种顺序在量产驱动里都存在**。
所以 AS 写序**不是**启动失败的原因，优先级应降到最低。

### 10.1 更重要的发现：AUTO 之后 Android 只读不写

`hist_boot.txt` 里 rec136（AUTO）到 rec163（bit31）之间的 **27 条访问，只有 2 次写**：

```
rec142  w 0x024 = 0x400     ← GPU_IRQ_CLEAR bit10 (POWER_CHANGED_ALL)
rec153  w 0x024 = 0x400     ← 同上
```

**这两次都是清电源变化中断，不是启动动作。**

**含义：MCU 启动完全依赖 ① 基址寄存器 ② 上电 ③ 时钟 ④ 固件在内存 ⑤ 页表 ⑥ AUTO 这一个写。**
AUTO 之后不存在任何"启动辅助写"。

而我们在 `mali=1` 已经证明前 5 项全部就绪：
`gpu_id 0xa8670004`、`CSF 0x40a0412`、`timestamp register 0x3`、`10 sections 9 page tables`、PDC 写入成功。

**所以问题必然在 AUTO 这一个写之后：硬件收到了 `MCU_CONTROL=2`，也把 `MCU_STATUS` 置为 1(ENABLED)，
但没有让它真正开始取指。**

这缩小到一个很窄的范围：**MCU 复位没释放 / MCU 时钟门 / 启动地址（栈指针）没被正确读取**。

---

## 11. 结论与下一步（按证据强度排序）

| 优先级 | 假设 | 支持证据 | 代价 |
|---|---|---|---|
| **P1** | **MCU 启动地址（栈指针）未被正确读取** —— 硬件从 VA 0 的向量表读 SP/PC，而我们的 section 0 只有 2836 B、flags `0x9`(r-- cached) | `section 0: va 0-0x1000 flags 0x9 (r-- cached) 2836 bytes`；`0x9` 的 cache 位 = `SEC_CACHED`，但 MCU 不 snoop | 一次刷机：把 section 0 改成 NON_CACHEABLE + 检查前 8 字节 |
| **P2** | MCU 复位/时钟门未释放 | MCU_STATUS=1 但 debug_top=0x1b(busy) 永不回落；写 DISABLE 不回 0 | 需要一个能读 MCU 复位位的寄存器，尚未定位 |
| P3 | 固件要求 protected-mode .bss 必须走受保护分配器 | 三个 section 被降级为普通页；`kbase_csf_protected_memory_alloc` | 静态可查 `mali_kbase_csf_firmware.c` 的 prot 处理 |
| ~~P4~~ | ~~AS 写序~~ | 已排除（kbase 与 android 同序，panthor 与我们同序，两种都存在） | — |
| ~~P5~~ | ~~域上电不满 12/12~~ | 已排除（BYPASS_INIT_ON 设计如此） | — |

### P1 的具体做法

`load_firmware()` 里 section 0 的 memattr 来自 `flags & SEC_CACHE_MASK`。
驱动目前把 `SEC_CACHED` 映射为 index 0（可缓存），但 **MCU 不 snoop CPU 缓存**，
向量表若只在 cache 里、未写回 DRAM，MCU 读到的就是垃圾 —— 这正好解释
"status=1、无 fault、版本字不变、debug_top=busy"。

驱动已经对每个 section 调了 `mem_flush()`，但 **PTE 里的 cache 属性仍然是 cached**，
MCU 的读取可能走不同的路径。

**验证方法**：把 section 0（以及所有非 shared section）的 memattr 强制改成 index 5
（NON_CACHEABLE，`0x4c`），与 section 9 一致。这是一次刷机，stores 数不变。

---

## 12. trial 604/605：两次否证（缓存策略、可执行位）

### 12.1 trial 604 `mali_fwnc=1`（全部段改 NON_CACHEABLE）

镜像 md5 `c7eaf734428964798c19cedd828bb744`，回 Android 正常。

```
34.630293 mali: section 0: va 0-0x1000 flags 0x9 (r-- nc) 2836 bytes
34.631430 mali: section 0: memattr index 5
34.633766..34.655776  section 1..9: memattr index 5
44.807246 mali: version word 0xff5aa5f0 (untouched)
44.808137 mali: the global interface never raised; the MCU did not boot
```

**结论：缓存策略不是原因。** 全部 10 个 section 都变成 `index 5`(NON_CACHEABLE)，
`flags 0x9 (r-- nc)`，MCU 依然不取指。

**证据边界**：这只否定"section 被 CPU 缓存污染导致 MCU 读到脏数据"，
不等于"内存可见性完全没问题"——但我们已用 `mem_flush()` 逐段写出，所以这条可关。

### 12.2 trial 605 `mali_fwx=1`（全部段可执行 + 可写）

镜像 md5 `9869327ca2fd419c03a3ce477ef702cd`，回 Android 正常。

```
41.367351 mali: trial 605 after AUTO: ... fwnc 0 fwx 1
41.430044 mali: version word 0xff5aa5f0 (untouched; no firmware version published)
41.430049 mali: the global interface never raised; the MCU did not boot
```

**结论：`PTE_XN` 不是原因。**

### 12.3 为什么试这两条

`section 0` 的 flags 是 `0x9`，解码为 **`SEC_RD=1, SEC_WR=0, SEC_EX=0`** —— 即 **r--（只读、不可执行）**。
而驱动自己在 `bolt_witness()` 的注释里写着：

> `The vector table lives at VA zero, in section 0, and the hardware reads the stack pointer from +0 and the reset address from +4. The vendor image puts 0x010018ed there -- section 1, offset 0x18ec, thumb bit set`

也就是说：**硬件从 VA 0/+0 读 SP、从 VA 0/+4 读复位地址，而复位地址指向 section 1**。
如果 section 1 的页被标 XN，那次取指就被 MMU 拒绝 —— 而且**取指失败不会变成总线事务**，
所以 `AS_FAULTSTATUS` 仍然是 0、`debug_top` 变 busy、哨兵不变。这完全对上我们的症状。

**但 605 证明它不是原因。** 所以 `PTE_XN` 这条线索关闭。

---

## 13. 目前累积的否证清单（round-6 新增 5 条）

| # | 假设 | 实验 | 结论 |
|---|---|---|---|
| 1 | PDC 交接能让 12 域全上电 | 601/602 | ❌ PDC 写入成功但 `0xF3C & 0x3FFC` 仍 `0x4` |
| 2 | 12 域必须全上电 | — | ❌ `BYPASS_INIT_ON` 设计上就不要求（判据本身错） |
| 3 | `SHADER_READY=0` 是原因 | — | ❌ 是结果（rec228 在 bit31 之后，且是单点脉冲） |
| 4 | 固件段被 CPU 缓存污染 | 604 `fwnc=1` | ❌ 全段 NON_CACHEABLE 仍失败 |
| 5 | 固件段不可执行导致取指被拒 | 605 `fwx=1` | ❌ 全段可执行可写仍失败 |

round-1..5 已否证的：AS0 位置、13 次 MMU clear、2 次 power clear、AS2/AS3 MEMATTR、
free-run 位、AS 写序（kbase 与 Android 同序但 panthor 与我们同序，两种都存在）。

---

## 14. 把范围收窄（这是本轮真正的产出）

综合 kbase 源码与 `hist_boot.txt` 全量分析，可以确定：

**AUTO 之后 Android 什么都不做，只清两次 `GPU_IRQ_STATUS.POWER_CHANGED_ALL`（`0x24=0x400`）。**

六次 AUTO→bit31 全部同构，且 `0x704` 在该窗口内 **0 次读取**、
`0x1004`/`0x1008` 在该窗口内 **0 次写入**。

所以 MCU 启动只依赖：**① 上电 ② 时钟 ③ 固件在 DRAM ④ 页表 ⑤ 写 AUTO 这一个动作**。

我们已经逐项验证前 4 项都成立（`gpu_id`、`CSF 0x40a0412`、`timestamp 0x3`、`10 sections 9 tables`、PDC 写入）。

**结论：问题在"写 AUTO 之后，硬件没有真正让 MCU 开始取指"。
缓存、可执行位、页表内容、AS 顺序、域上电都不是原因。**

剩下的窄范围候选：
1. **MCU 的启动地址/栈指针没有被读到** —— 虽然向量表在 VA 0、`bolt_witness` 能改它，
   但我们**从未验证过 `vec[0]`/`vec[1]` 的实际值**。应加一行打印。
2. **MCU 复位/时钟使能位**在别的寄存器（非 GPU aperture），无法从 history 对账
3. **固件镜像本身**（protected-mode .bss 被降级为普通页，且 section 3/4/6/7/8 的 data 都是 **0 字节**）

### 15. 下一轮最便宜的一枪

**加一行打印，把 VA 0 的前 8 字节读出来**（`vec[0]`=SP, `vec[1]`=reset PC），
并同时打印 section 1 起始处 4 字节，确认 `0x010018ed` 是否真的在那儿。

这**不需要额外刷机**——可以和下一个实验合并：
把 `bolt_witness` 的入口打印改成无条件执行（现在它只在 `mali_witness=1` 时才跑）。

一次刷机同时得到：
- `vec[0]` / `vec[1]` 的真实值（此前从未见过）
- section 1 偏移 0x18ec 处的 4 字节（确认固件与注释一致）
- 如果 `vec[1]` 不是 `0x010018ed`，那说明我们对固件头/入口的理解就是错的

---

## 16. ★ trial 608：WITNESS 实验——MCU 一条指令都没执行（决定性）

镜像 md5 `236619f9fa6112b91abb3ae0378ca27e`，`mali_witness=1`，回 Android 正常。

### 16.1 实验设计

驱动里有一段从未跑过的 32 字节 Cortex-M7 代码（clang armv7m 汇编，llvm-objdump 校验过）：

```
f240 0000  movw r0, #0x0000
f2c0 4000  movt r0, #0x0400   ; r0 = 0x04000000 (shared page)
f240 0111  movw r1, #0x0011
6001       str  r1, [r0]      ; 第一个标记
f240 0122  movw r1, #0x0022
6041       str  r1, [r0, #4] ; 第二个标记
f64b 61ef  movw r1, #0xbeef
f6cd 61ad  movt r1, #0xdead   ; r1 = 0xdeadbeef
6001       str  r1, [r0]      ; 第三个标记（覆盖第一个）
e7fe       b    .            ; 原地停
```

它把复位向量改成指向自己，**完全绕过厂商固件**：不需要栈、不碰 `.bss`、
不依赖任何我们建的东西（除两个已由 walk 验证过的页表项）。

### 16.2 结果

```text
33.229732 mali: witness: vector was sp 0x2040000 reset 0x10018ed
33.231218 mali: witness: code at 0x1000000, reset vector now 0x1000001
33.232672 mali: sentinel 0xff5aa5f0 written to the version word
33.254206 mali: glb req 0 -> 0 (halt was already clear)
41.875206 mali: version word 0xff5aa5f0 (untouched; no firmware version published)
41.875206 ...
41.879685 mali: witness: 0xff5aa5f0 0 -- the core never executed the first store
```

**共享页第一个字仍是 `0xff5aa5f0`（哨兵），第二个字是 `0`。**
那段 7 条指令的代码，**一条都没执行**。

### 16.3 这个结果排除了什么

这是**最强的否证**：它把"固件"这一整个维度从问题里拿掉了。

| 之前所有假设 | 是否还成立 |
|---|---|
| 固件镜像有问题 | ❌ 无关：witness 完全不用固件 |
| 页表/MMU/AS 配置有问题 | ❌ 无关：witness 只用两个已验证的页表项 |
| AS 写序、AS0 位置、MMU clear | ❌ 无关 |
| 固件 section 缓存属性（604） | ❌ 无关 |
| 固件段可执行位（605） | ❌ 无关 |
| protected-mode `.bss` 降级 | ❌ 无关 |
| 向量表本身 | ✅ **已验证合法**：`sp 0x2040000`（section 4 栈顶）、`reset 0x10018ed`（section 1 内，thumb 位=1） |

**结论：MCU 核心根本没有开始取指。问题在 MCU 的执行前提（复位/时钟/总线），不在固件侧。**

### 16.4 与九轮历史的关系

回看：九轮实验全都建立在"Cortex-M7 在跑，只是固件没起来"这个前提上。
witness 证明这个前提**是错的**。

这也解释了为什么每一轮改动都"完全无效果"——我们一直在修一个**没有被执行**的东西。

### 16.5 一个新的强候选：MD0 总线保护

配套证据（来自子代理对 vendor 源码的核查 + 我复核）：

**a) `BUS_PROT_IGN` 的 tuple 是五元组**（`mtk-scpsys.h` + `mtk-scpsys.c:377-430`）：

```
set_ofs=0x0CA4  clr_ofs=0x0CA8  en_ofs=0x0CA0  sta_ofs=0x0CAC  mask=0xF
```

`BUS_PROT_IGN` 的含义是 **"不轮询清除 ACK"**（`ignore_clr_ack`），
所以 `clear_bus_protection()` 写完 CLR 就 `return 0`，**从不验证是否真的清掉**。
这就是为什么这个问题能藏九轮：它**对任何人都是静默失败**。

**b) 我们的实测（trial 602/603，两次都是同一形态）**

```text
21.088067 bus protect sta: md0 en 0 sta 0xffffffe0    ← mfg1 上电前
21.340657 bus protect sta: md0 en 0 sta 0xffffffef    ← 写完 CLR 之后
```

`sta` 的 bits 0..3 从 **`0`** 变成 **`1`**。

**c) 极性的关键判据**（`mtk-scpsys.c:377-430`）：

- `set_bus_protection()` 轮询 `(val & mask) == mask` → **建立保护后期望 bit=1**
- `clear_bus_protection()` 轮询 `!(val & mask)` → **解除保护后期望 bit=0**

所以 **bit=1 = 保护处于开启状态**。

**我们写的是 CLR（解除），结果 bits 却变成 1（开启）。** 也就是说：
**这九轮里每次"释放总线保护"，实际上要么没生效、要么状态本来就是挡着的。**

**d) 另一个可疑点：`md0 en 0`**

`en_ofs = 0x0CA0` 读出来是 **0**。如果这个 enable 位是 CLR 生效的前提，
那它一直是 0 就解释了为什么清除无效。**这是下一步最该验证的一点。**

**e) 为什么这能解释全部四个症状**

| 症状 | 总线保护挡住的解释 |
|---|---|
| `MCU_STATUS=1` 但从不取指 | 状态机接受 AUTO 写（不需要总线），但取指要走 MD0 → 被 stall |
| 无 fault、无 faultaddress | AXI stall 不是 error，**不产生 fault** |
| 版本字不变 | 写 DRAM 同样要过 MD0 |
| `AS0 UPDATE` 恒置 `MMU_BUS_ERROR(0)`=bit16 | walker 读 DRAM 页表被拒的**直接证据**（401/402/246 三份日志全有） |
| witness 一条指令都没执行 | 复位取指从 VA 0 读，同样过 MD0 |

**f) 顺带纠正**：`debug_top=0x1b` **不是 MCU 的 busy 指示**。
trial 401 日志 `bus watch after the core was switched off: 0x1b 0x1b 0x1b 0x1b` ——
写 DISABLE 之后仍是 0x1b，说明它与 MCU 状态无关。之前拿它论证"MCU 在跑但卡住"是错的。

### 16.6 证据边界（必须带着读）

- `sta` 在 mfg1 上电**前**读到 `0xffffffe0`，那时 MD0 所属块可能还没时钟，**该读数可能无效**
- 上电后读到 `0xffffffef` 才是可信值：**bits 0..3 = 1 = 保护开启**
- 我们只证明了"CLR 写完之后 sta 仍为 1"，**没有证明** CLR 写本身有没有到达寄存器
- `en 0` 与 CLR 失效之间的因果**尚未验证**

### 16.7 下一步（一次刷机，最便宜）

把 `bus_protect_release()` 改成**验证式**：

1. 先读 `en_ofs(0x0CA0)`；若相应位为 0，**先写 1 使能**
2. 写 `clr_ofs(0x0CA8) = mask`
3. **轮询 `sta_ofs(0x0CAC)` 直到 `(val & mask) == 0`**（不再跳过！）
4. 若不收敛，打印每次读到的 `sta` 和已等待毫秒数

同时把 `set_ofs(0x0CA4)` 也读出来打印，确认 tuple 方向没搞反。

**这是第一个"验证式"实验**：以前所有 bus protect 操作都是"写完就走、从不确认"，
所以九轮里没有一轮知道它到底成功没有。

---

## 17. ★★ trial 611：MD0 总线保护验证——清除**确实不落地**（本轮最硬的结论）

镜像 md5 `182746b27e7bb9684129caf0ee9bfcb8`，回 Android 正常。

### 17.1 实验设计（修正了三次失败）

这段验证代码前两次都没跑起来，原因值得记录：

1. **trial 609**：写成独立函数 + 挂在 `mali_bpverify=1` 开关后 → 参数确实在 boot-args 里
   （日志 `boot-args = ... mali_trial=609 mali_bpverify=1`），内核里也有那 6 条字符串，
   **但分支一次都没进**。
2. **trial 610**：改成无条件调用 `bus_protect_release_verified()` → 内核里字符串降到 4 条，
   **运行时依然没有任何输出**。
3. **trial 611**：干脆**内联到 `bus_protect_release()` 里**（那个函数的 4 行 `bus protect ... -> ...`
   在日志里每次都在，确定会执行）→ **立刻拿到了数据**。

**这是个重要教训**：在这份内核里，新增的独立函数不保证被调用到，写探针要挂在**已被证实会执行**的路径上。

### 17.2 实测结果

```text
33.142400 mali: bpverify md0: sta 0xffffffef -> 0xffffffef after 10000 polls (STILL SET -- clear did not land)
33.143744 mali: bpverify emisys0: sta 0xff000020 -> 0xff000020 after 0 polls (OPENED)
33.145064 mali: bpverify emisys1: sta 0xff002020 -> 0xff002020 after 0 polls (OPENED)
```

等满 **10000 × 10µs = 100 ms**（100 倍于 emisys 的收敛时间，emisys 是 **0 次轮询**就 OPENED）。

### 17.3 这个结果的三层意义

**第一层：代码和硬件通路都没问题。**
`emisys0/1` 用同一批代码、同一种写法、同一时刻，**0 次轮询**就 `OPENED`。
这证明：写操作能到达 infracfg、mask 位序正确、轮询逻辑正确、STA 读数可信。

**第二层：md0 的清除是真的不落地。**
不是"慢"，不是"代码写错"——100 ms 内 10000 次轮询，值一个字没变。

**第三层：与手机 dump 的对照，这是九轮里唯一一条"手机通、我们不通"的差异。**

| 寄存器 | 手机（Android 正常跑 GPU） | 本机（trial 611） | 判定 |
|---|---|---|---|
| `0xCA0` md0 EN | `0x00000000` | `0` | 相同 |
| `0xCA4` md0 SET | `0x00000000` | （未读） | — |
| `0xCA8` md0 CLR | `0x00000000` | `0` | 相同 |
| **`0xCAC` md0 STA** | **`0xFFFFFFE0`**（bits0-3 = **0**） | **`0xFFFFFFEF`**（bits0-3 = **1**） | **唯一差异：手机 OPEN，我们 BLOCKED** |
| `0xC60/0xC64/0xC68` emisys0 | `0x003C0000` | `0` | 不同 |
| `0xC6C` emisys0 STA | `0xFF3C0020`（bits18,19=**1**） | `0xFF000020`（bits18,19=**0**） | 手机是**挡着**的 |
| `0xC7C` emisys1 STA | `0xFF3C2020`（bits18,19=1） | `0xFF002020`（bits18,19=0） | 同上 |

**关键推论**：手机上 emisys 是**挡着**的，GPU 照样正常工作 →
**"STA 位为 1 = 端口被挡死"不能推广到所有端口组**。
只有 **md0** 是"手机通 / 我们不通"的对照，也只有它的清除不落地。

### 17.4 为什么这个问题能藏九轮

`BUS_PROT_IGN` 的语义是 **"永不轮询清除 ACK"**（`mtk-scpsys.h:24-27` → `mtk-scpsys.c:412-418`
的 `if (ignore_ack) return 0;`）。

也就是说 **vendor 自己也不验证这个清除**。一个"写下去、不生效、没人检查"的操作，
在这个平台上是完全静默的。九轮里我们每次都写两次 CLR，每次都假设它成功了。

### 17.5 它能不能解释 witness 的结果

| 观测 | md0 被挡的解释 | 强度 |
|---|---|---|
| witness 7 条指令一条都没执行 | 取指从 VA 0 读指令，指令在 DRAM，必经 MD0 → stall | **强**（这是最直接的解释） |
| `MCU_STATUS=1` | 状态机接受 AUTO 写（纯寄存器操作，不过总线） | 强 |
| 零 fault / 零 faultaddress | AXI **stall** 不是 error，不产生 fault | 强 |
| 版本字 `0xff5aa5f0` 不变 | 写共享页（DRAM）同样过 MD0 | 强 |
| `AS0 UPDATE` 恒置 `MMU_BUS_ERROR(0)` = bit16 | walker 读 DRAM 页表被拒 | 中（AS2..AS7 的 unmapped 也报同族位，非专属指纹） |
| `debug_top=0x1b` 恒 busy | channel 挂在永不完成的 transaction 上 | 弱（DISABLE 之后仍 0x1b，与 MCU 状态无关） |

**四个主要症状全部吻合，且这是唯一能同时解释"状态机有反应、总线无动作"这一组合的假设。**

### 17.6 证据边界（必须带着读）

- 我们证明了 **`md0` 的 CLR 写入后 100 ms 内 STA 不变**，**没有证明** CLR 写本身是否到达了寄存器。
  （但 emisys 用的同一条路径能立刻生效，所以"写不进去"的可能性很低。）
- **没有证明** MD0 就是"MFG↔DRAM 的通路"。这个名字来自驱动自己的注释，
  vendor 侧的对应关系是 `0xCA0 = MD_MFGSYS_PROTECT_EN_0`、`0xCAC = MD_MFGSYS_PROTECT_RDY_STA_0`
  （`clkchk-mt6895.c:177-178`），**"MD" 指向 modem 侧还是 MFG 侧，尚未从 vendor 源码确认**。
- 与手机的对照成立，但手机 dump 的采集时机（GPU 工作态 vs 空闲态）未核实。

### 17.7 下一步：两个方向，成本都很低

**方向 A（先做，纯只读）：确认 MD0 到底属于谁。**
在 `infracfg` 里把 `0xCAC` 附近整片读出来打印，并对照
`clkchk-mt6895.c` 的完整寄存器名表，确认 `0xCA0/0xCA8/0xCAC` 这组的官方名字与语义。
**不需要改任何上电行为。**

**方向 B（一次刷机）：把 `0xCA4`（SET 寄存器）读出来。**
当前从没读过 `0xCA4`。如果它非零，说明有人用 SET 显式布防了 md0，
那 CLR 不生效就可能是"SET 和 CLR 打架"——那时应该先查**谁在 SET**，
而不是继续重试 CLR。

**不建议**现在就去改 `EN` 寄存器（`0xCA0`）试图强行打开：
手机读数也是 `0`，强行改成 1 与手机不一致，风险高于收益。
