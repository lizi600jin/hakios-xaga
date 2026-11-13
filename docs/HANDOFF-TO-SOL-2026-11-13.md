# 完整交接：DSH 主控 + astra -> 6.1sol（2026-11-13 12:05）

手机 `GAJZ4HIN99AQ69Y9` 现在跑**原厂 Android**（`boot_completed=1`，slot `_a`），
boot_a 是 `backup/boot_a_stock_real.img`（md5 `a63b2e35a7c2522cb24a6a10332b9bc8`）。

用户明确要求：**先不要刷回 iOS 镜像**，让手机留在可用状态。

---

# 一、唯一的真突破：16K 页 / 4K 页表步进 bug

## 1.1 bug 本体

`load_firmware()` 建表循环：

```c
for (va = va_start; va < va_end; va += PAGE_SIZE)   // PAGE_SIZE = 16384 (16K)
    pt_map(gMcuRoot, va, mem_pa(...), leaf);
```

但 `pt_map()` 的叶索引是 4K：

```c
table[(va >> 12) & 0x1ffu] = (pa & PTE_ADDRESS) | leaf;
```

**每 16K 只写 1 个 4K 叶，75% 地址空间没有 PTE。**

## 1.2 为什么 PAGE_SIZE 是 16K（证据链）

1. `mtk/loader/mtkloader.c:48` `XNU_MEM_SIZE = 0xfe000000`（4.26GB）
2. `osfmk/arm/arm_init.c:413-417`：`memSize > 1GB` → `PAGE_SHIFT_CONST = PAGE_MAX_SHIFT`
3. `osfmk/mach/arm/vm_param.h:60`：`PAGE_MAX_SHIFT = 14` → `PAGE_SIZE = 16384`
4. 日志实证：页表地址按 `0x4000` 步进
   （root `0x50000000`、sec0 `0x50004000`、L1/L2/L3 `0x50008000/0x5000c000/0x50010000`）

## 1.3 覆盖度实测

| section | 需 4K 叶 | 实写 | 覆盖 |
|---|---|---|---|
| sec1（固件代码 0x1000000-0x1010000） | 16 | 4 | **25%** |
| sec2 | 62 | 16 | 25.8% |
| sec5 | 64 | 16 | 25% |
| sec6 | 62 | 16 | 25.8% |
| sec9（shared 0x4000000） | 148 | 37 | **25%** |

## 1.4 为什么这解释了一切

**厂商固件复位向量 `0x010018ed` 落在 sec1 的 leaf 1（0x1001000-0x1001fff）。
实际被映射的叶是 0、4、8、12 —— leaf 1 从未映射。**

→ MCU 写完 `AUTO` 后取第一条指令就断
→ Cortex-M7 对**从未成功发出的取指**不产生 data abort
→ `AS_FAULTSTATUS=0`、无 MMU 中断、`MCU_STATUS` 不变、`debug_top` 变 busy 后不复位
→ **精确吻合十几轮观察到的每一个症状**

## 1.5 为什么 witness 实验「看起来」成立

桩的三处关键地址**都恰好在已映射的 leaf 0**：

| 用途 | VA | leaf | 状态 |
|---|---|---|---|
| 桩代码 | 0x1000000-0x100001f | 0 | 已映射 ✓ |
| store #1 | 0x04000000 | 0 | 已映射 ✓ |
| store #2 | 0x04000004 | 0 | 已映射 ✓ |

所以「如果执行了 CPU 应该能看见」成立；但**桩也没执行**，因为取指在 `0x10018ed` 就断了。

**witness 的结论（MCU 一条指令都没执行）是对的，原因现在有了机械解释。**

## 1.6 修复

```c
#define PT_LEAF_GRANULE  0x1000ull    /* pt_map 实际索引的粒度 */
for (va = va_start; va < va_end; va += PT_LEAF_GRANULE)
```

并加了覆盖度自检（`load_firmware()` 末尾），输出：
```
mali: coverage: every section fully mapped
```
或
```
mali: COVERAGE secN ...: X of Y leaves mapped
mali: coverage: HOLES FOUND
```

## 1.7 验证

trial 660/662/663 **三次确认** `coverage: every section fully mapped`，都正常回 Android。

---

# 二、修复后的新现象（十几轮第一次出现）

| level | 行为 | 回 Android | MCU 启动 |
|---|---|---|---|
| 3 | L2 后停，不写 AUTO | ✅ | 未请求 |
| 5 | 写 AUTO、等 bit31，不碰 global interface | ✅ | ❌ |
| 6 | 写 AUTO、等 bit31、**调 global_interface()** | ❌ **卡死** | ❌ |

**level 6 与 level 5 分离了。卡点在 `global_interface()`。**

## 新假设（未验证）

`csg_control` 在 `gShared->mem.kva + 0x1000`（VA 0x04001000）。
- 修复前：空洞（无 PTE）
- 修复后：有效页，但 **MCU 从没写过那里**，读到的是一整页 0

`global_interface()` 先读 `control->version`，再按 version 解析 `input_va`/`output_va`。
version=0 时可能算出越界偏移 → 访问非法地址 → 卡死。

**验证方式**：`mali=5` + 探针，在 `global_interface()` 之前打印
`control->version`、`input_va`、`output_va` 的原始值。**不要直接刷 mali=6。**

---

# 三、完整实验清单（19 轮，全部真机实测）

## 3.1 astra（gpt-6-astra / ultra）做的

| trial | 变量 | 结果 |
|---|---|---|
| 203 | mali=3 纯冷基线 | ✅ 回 Android，68 stores |
| 206 | mali=6 完整纯冷基线 | ✅ 回，82 stores，MCU ❌ |
| 216 | +free-run | ✅ 回，MCU ❌ |
| 226 | +AS2/AS3 MEMATTR | ✅ 回，MCU ❌ |
| 236 | +两者组合 | ✅ 回，MCU ❌ |
| 246 | 低干扰（trace=0） | ✅ 回，MCU ❌ |
| 400 | as0first=1 | ✅ 回，82 stores 仅 AS0 块前移，MCU ❌ |
| 401 | 去 13 次诊断 MMU clear | ✅ 回，**69 stores**，MCU ❌ |
| 402 | 去 2 次 power IRQ clear | ✅ 回，**80 stores**，MCU ❌ |

## 3.2 主控（DSH）做的

| trial | 变量 | 结果 |
|---|---|---|
| 600/601/602 | PDC 交接（`mali_pdcauto`） | ❌ 抓到 PDC 写在时钟门之前的 bug；修好后写入生效但 `0xF3C & 0x3FFC` 仍 `0x4` |
| 604 | `mali_fwnc=1` 全段 NON_CACHEABLE | ❌ 仍失败 |
| 605 | `mali_fwx=1` 全段可执行可写 | ❌ 仍失败 |
| 607 | 打印复位向量 | ✅ sp=0x2040000、pc=0x10018ed **合法** |
| 608/613 | **witness**（32 字节桩） | MCU 一条指令没执行；三次 dmb 复读稳定 |
| 612 | MD0 总线保护验证 | EN 能写能读回 0xF，STA 纹丝不动 |
| 630 | `mali_scclk=1` 清 mfgsc 时钟 gate | ✅ cfg5 bit7 清了，但 MCU 仍 ❌ |
| 640 | AOC 隔离判定 | `0xF30=0x1101`，iso/latch 都是 0，**已释放** |
| 650 | MCU 窗口全扫 + ENABLE/AUTO 两步 | 124 字全 0，无隐藏闸门；ENABLE/AUTO 都写进去了，status 恒 1 |
| 660/662 | 映射修复 + mali=3 | ✅ `coverage: every section fully mapped` |
| 663 | 映射修复 + mali=5 | ✅ 回 Android，MCU ❌ |

---

# 四、已彻底排除的（别再重做）

| 假设 | 排除实验 | 证据 |
|---|---|---|
| 域上电不满 12/12 | kbase 注释 | PCSF 下 shader 由固件自己管，host 只管 L2 |
| `SHADER_READY=0` 是原因 | 完整 2460 条轨迹 | 它在 AUTO 之后才出现（rec228），是结果 |
| `debug_top=0x1b` 是 MCU busy | 401 日志 | DISABLE 之后仍 0x1b，与 MCU 状态无关 |
| mfgsc 时钟 gate | 630 | bit7 能清，MCU 仍不启动 |
| MD0 总线保护 | 612 | EN 能写能读回 0xF，STA 不变；vendor 也只当 dump 读 |
| AOC SRAM 隔离 | 640 | `0xF30=0x1101`，已释放，信号量 `0x6AC=0x1` |
| MCU_CONTROL 写值 | 650 + kbase 头文件 | `MCU_CNTRL_AUTO=(1<<1)=2`，我们写 2 与 Android 六次全同 |
| `MCU_STATUS=1` 是 ENABLED | 650 | 头文件只定义 `HALTED=(1<<1)`；位0 无定义，写什么都改不动 |
| `0x3000` 窗口有启动闸门 | 650 | 128 字全扫，只有 4 个非零，都是计数器/中断锁存 |
| 复位向量非法 | 607 | sp/pc 都合法 |
| AS 写序 | kbase 源码 | kbase/Android 与 panthor 两种顺序都存在 |

## 关于「在坏映射下测出的无效结论」

以下是在 25% 映射时测的，**「无效」这个事实没错**（坏映射下本来就该无效），
但**前提是坏的**，映射修好后值得重测：
AS0 位置（400）、13 次 MMU clear（401）、2 次 power clear（402）、
AS2/AS3 MEMATTR（226/236）、free-run（216/236）、固件缓存（604）、可执行位（605）、PDC（601/602）。

---

# 五、两个子代理的关键发现

## 5.1 witness 内存映射审计（760ce204）
- CPU 读的 `0x50104000` 与 MCU 页表 VA `0x04000000` **是同一物理页** ✓
- `ml_io_map_unmappable` 严格一一映射；WCOMB 读不需要额外 barrier
- 三次复读证据可靠，排除「读到一半」和「缓存陈旧」
- **就是它揪出了 16K/4K 页表 bug**

## 5.2 AOC vendor 调查（89d05ce1）
- **xaga 真机是 EB 模式**（`gpueb-support = <0x01>`，`dump/xaga_live.dts:9495`）
- AP 侧 `__gpufreq_aoc_control()` 在真机上是**死代码**，POWER_ON 走 IPI 给 GPUEB
- 真正放隔离的是 **GPUEB 固件**（反汇编 `fw/gpueb_a.img` 0xa650 确认）
  - 持锁重试 5001 次，超时后 dump+assert **且不写隔离寄存器**
- MT6895 与 MT6983 的 AOC 代码**逐字节相同**，没漏步骤
- `gpufreq` 等信号量是**永久自旋**（无超时），这点与固件不同

---

# 六、坑（血泪教训）

1. **新增独立函数不保证被调用**
   `bus_protect_release_verified()` 参数送达、字符串在镜像里、改无条件调用**仍零输出**；
   只有内联进 `bus_protect_release()` 才跑。**探针必须挂在日志里已出现过输出的路径上。**
2. **boot-arg 必须验证送达**：`strings -a mtk/loader/mtkloader.bin | grep -c "mali_xxx=1"` 应为 3
3. **logbook 不能按 header 的 len 截断**（header 说 79 字节，实际正文 4MB）
4. **MCU 窗口续行不带 `mali:` 前缀**，要单独正则抓
5. **禁读** `0x13bfee00` / `0x13c40000` / `0x13F91034`（一读挂死总线）
6. **ring buffer 会冲掉日志开头** → 关键探针放失败路径末尾
7. **mali=6 会卡死**（映射修复后），需手动回 fastboot
8. **`adb reboot` 可能超时**（系统半启动时），需要用 fastboot 直接刷

---

# 七、工具

| 命令 | 用途 |
|---|---|
| `mali-trial --status` | 查手机与镜像状态（不碰设备） |
| `mali-trial --trial N --args "..."` | 全流程：构建→打包→刷入→等回→取日志→生成证据 |
| `mali-trial --trial N --args "..." --dry-run` | 只看会执行什么 |
| `codex-quota` | 查两个 codex 账号额度 |
| `codex-failover --dry-run` | 判定该不该切号 |
| `codex-resume` | 额度恢复后自动交回 |
| `systemctl --user status codex-guard.service` | 守护状态 |

---

# 八、当前状态

- **手机**：`GAJZ4HIN99AQ69Y9`，**原厂 Android**，`boot_completed=1`，slot `_a`
- **boot_a**：`backup/boot_a_stock_real.img`（67108864 B，md5 `a63b2e35a7c2522cb24a6a10332b9bc8`）
- **驱动**：`/home/lizi/IOS/hakios-mtk/mtk/mt6895/drivers/gpu/hakios.mali610.c`（含 `PT_LEAF_GRANULE` 修复）
- **仓库**：`/home/lizi/IOS/repo/hakios-mt6895`
- **证据**：`/home/lizi/IOS/work-xaga/codex/out/round-6-support/`（21 个 logbook）
- **关键文档**：
  - `BREAKTHROUGH-16K-PAGE-BUG.md`（172 行，突破全过程）
  - `HANDOFF-2026-11-13.md`（103 行）
  - `CURRENT-STATE.md` / `STATE-MINI.md`

---

# 九、建议的下一步

1. **先离线验证** `global_interface()` 的越界假设（读代码，不用刷机）
2. 检查还有没有别处把 `PAGE_SIZE` 当 4K 用
   - `pt_new()` 分配表页的粒度
   - `MALI_SHARED_VA` 的对齐检查
3. 要刷机时，**先用 `mali=5` + 探针**，不要直接刷 `mali=6`
4. 用户可能想先让手机稳定一段时间 —— **刷之前先问**
