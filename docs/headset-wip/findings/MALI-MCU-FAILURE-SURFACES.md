# Mali-G610 CSF MCU 不启动：失败面穷举与证据分级

> 审计对象：`/home/lizi/IOS/hakios-mtk/mtk/mt6895/drivers/gpu/hakios.mali610.c`（5219 行，只读）
> 上游对照：`MT6895-Mainline/linux` @ `7.2-mt6895-xiaomi-xaga`
> （`panthor_fw.c` 1567 行 sha256 前缀 `c4161c9e9694d758`；`panthor_device.c` 639 行 `fe6e69d5429a20b0`）
> 真机日志：`/home/lizi/IOS/hakios-mtk/logs/headset-resume-20261114-220244/nopdc/kernel-lines.txt`（1360 行，trial 692）
> 生成日期：2026-11-14

本文所有结论均可追到 `文件:行号` 或 URL。凡无证据的一律标「无证据」，凡推断未验证的标「未验证」。

---

## 0. 结论摘要

按证据强度排序，四条最硬的发现：

1. **`0x10000` 不是 BUS_ERROR，是 AS0 的 REQ_COMPLETE。** 驱动把 MMU 中断锁存器的 bit16 读成「walker 被总线拒绝」，并据此写了大段推论（`:3795-3800`、`:4330-4340`）。panthor 两处源码都写明 bit16-31 是 REQ_COMPLETE（`panthor_mmu.c:648-649`，第二份副本 `/tmp/csf/linux-source-7.0.0/.../panthor_mmu.c:614` 同注释）。日志 245 行 `AS0 step update mmu irq 0x10000` 恰好出现在 UPDATE 之后、且 fault status 与 fault address 全为 0（日志 246），这就是**表走查完成**的正常签名。整条「AS0 被总线拒绝」的推理链失效。

2. **所有失败signature 都指向「MCU 取指从未离开核」，而 GPUEB 从未被请求过。** panthor 在 `MCU_CONTROL=AUTO` 紧前调用 `mt6895_gpueb_power_on()`（`panthor_fw.c:1131-1143`），并在更早的 probe 阶段再调一次，注释直说「否则 panthor 读到全零 ID/features」（`panthor_device.c:256-268`）。我们的驱动**一次 IPI 都没发**，`gGpueb` 只在 `mali_gpueb=1` 时才 map（`:4287-4290`），默认日志 `gpueb: not mapped`（日志 174、585）。

3. **`JOB_INT_MASK` 的极性与驱动注释相反，但结论不变。** panthor 的 `INT_MASK` 存的是**使能集**（`panthor_device.h:582-583`、`:605-618`），我们 `:3148` 写 `0xffffffff` 实际是「全开」，与 panthor 的 `~0` 一致。因此 `0x100c` 始终读 0 **不是自己屏蔽出来的**，是硬件真的没置 bit31。驱动 `:3986-3989` 的注释描述错误，但行为正确。

4. **固件确实加载了，且映射完整。** 内嵌 `kMaliFirmware[258048]`（sha256 `de22d2fc…f7229a`，`hakios.mali610.c:43` + `hakios.mali610_fw.h:12-14`），10 个 section 全部映射，覆盖率自检「every section fully mapped」（日志 233），reset vector `sp 0x2040000 pc 0x10018ed`（日志 234）。**固件镜像本身不是根因**（但运行期无 sha256 校验，见 §2.5）。

这四条合起来把问题压缩到一个窄面：**GPU 寄存器可读、L2 可上电、页表可走查、MCU 状态机接受 AUTO，但 MCU 一条指令都没取。** 剩余候选只剩 §3 的 F5（GPUEB 仲裁）、F2（电源域终态）、F4（TRANSCFG 缺 INA_BITS）。

---

## 1. 我们的初始化序列 vs panthor，逐项对照

### 1.1 我们的序列（按执行顺序）

入口 `hakios_mali610_init()` `:5058`，起线程 `mali_thread()` `:5048`，`delay(2000*1000)` 后进 `mali_bring_up()` `:4262`。

| # | 行号 | 函数 | 做什么 |
|---|---|---|---|
| 1 | `:4267` | `mali_bring_up` | map GPU `0x13000000` / 4.6 MB |
| 2 | `:4276-4277` | 同上 | map infracfg `0x10001000`、mfgrpc `0x13f91000` |
| 3 | `:4289-4291` | 同上 | 条件 map GPUEB（**仅 `mali_gpueb=1`**）|
| 4 | `:4298` | 同上 | map mfgcfg `0x13fbf000` |
| 5 | `:4304-4313` | 同上 | `bus_protect_release()` + `bus_protect_release_verified()` |
| 6 | `:4326` | 同上 | 读 `GPU_ID`，判冷/热 |
| 7 | `:4334-4337` | 同上 | 冷路径才 map SPM / topckgen / MFGPLL |
| 8 | `:4352` | `mali_aoc_up` | 释放 SRAM 隔离（`0xF30` bit9/bit10）+ SEMA |
| 9 | `:4378` | `power_on` | 12 个 MTCMOS 域，两轮 |
| 10 | `:4396` | `bus_protect_release` | 域起来后再放一次总线保护 |
| 11 | `:4410` | `mali_vstack_rail` | 经 PMIF/SPMI 使能 MT6368 VBUCK2 |
| 12 | `:4412` | `clock_26m` | 26 MHz 晶体 → `mfg_ref_sel`，开 BG3D 门 |
| 13 | `:4452-4454` | `pdc_enable`/`pdc_report` | PDC 使能（`mali_nopdc=1` 跳过）|
| 14 | `:4464-4468` | 同上 | 读 `0xF34/0xF38/0xF3C` + `GPU_ID` |
| 15 | `:4476` | `pll_300mhz` | MFGPLL 300 MHz + mux |
| 16 | `:4486` | `mfg_set_timestamp` | 时间戳计数使能 |
| 17 | `:4529` | `mali_acp_up` | AXCOHERENCE + 8 个 GALS |
| 18 | `:4530` | `mfg_master_gates` | DCM/ASYNC/GLOBAL/QCHANNEL/RPC 门 |
| 19 | `:4532` | `mfg_gpm_arm` | I2M 保护器 4 写 + 1 回写 |
| 20 | `:4536` | `mfg_ostd_probe` | 可选 OSTD 采样 |
| 21 | `:4620` | `gpueb_watch` | 只读探测（默认 not mapped）|
| 22 | `:4621` | `gmpu_probe` | 只读 |
| 23 | `:4631` | `as_park_unused` | AS2..AS7 停用 |
| 24 | `:4643` | `gpu_irq_mask_prestart` | 三组中断 clear+mask |
| 25 | `:4647` | `gpu_ipa_arm` | IPA 配置 + 4 命令 |
| 26 | `:4679` | `gpu_l2_power_on` | coherency → L2_CONFIG → `L2_PWRON` → 等 READY → 等 POWER_CHANGED_ALL |
| 27 | `:4732` | `load_firmware` | 建页表、拷 10 个 section、映射 |
| 28 | `:4777-4800` | 同上 | 清 MMU 锁存、`as_enable(0)` |
| 29 | `:4836` | `start_mcu` | 清锁存、清 GLB_REQ.HALT、写 `MCU_CONTROL=2`、轮询 1e6 次 |
| 30 | `:4897` | 同上 | 失败后写 `MCU_CONTROL=0` |

### 1.2 panthor 的序列

`panthor_device_init()` `panthor_device.c:168` → `panthor_fw_init()` `panthor_fw.c:1497`：

```
clk_init (:215) → init_power (:221) → ioremap (:228) → pm_runtime_resume (:245)
  → [GPUEB power_on] (:256-268)          ← 我们完全没有
  → devfreq_init (:270) → hw_init (:275) → pwr_init (:279) → gpu_init (:283)
  → coherency_init (:286) → mmu_init (:292)
  → fw_init (:299):
        request_job_irq (:1512-1516)
        hw_l2_power_on (:1523)
        vm_create(0x04000000, 64MB) (:1527-1531)
        fw_load (:1537)
        vm_active (:1541)
        fw_start (:1545)  → [GPUEB power_on 再调一次] (:1131-1143) → MCU_CONTROL=AUTO (:1143)
        fw_init_ifaces (:1549) → init_global_iface (:1553) → doorbell (:1085)
  → sched_init (:303)
```

### 1.3 差异表

| 项 | panthor | 我们 | 判定 |
|---|---|---|---|
| **GPUEB 上电请求** | `panthor_device.c:256-268`（probe 早期）+ `panthor_fw.c:1131-1143`（AUTO 紧前） | **无，一次 IPI 都没发** | **差异（重大）** |
| **固件来源** | `request_firmware("arm/mali/arch10.x/mali_csffw.bin")` `panthor_fw.c:806-818` | 内嵌 `kMaliFirmware` `:43` | 差异（机制，非缺陷）|
| **固件运行期校验** | 文件系统+firmware loader 契约 | 仅 magic/major/size（`:1014-1023`），**不校验 sha256** | 差异（弱项，见 §2.5）|
| **页表** | IOMMU 驱动，CPU 页 | 自建 AArch64 4K 表，低 4GB bump 分配 `:678-704` | 差异（机制）|
| **AS_TRANSCFG 值** | `0x420001c6`（含 `INA_BITS(55-48)`）`panthor_mmu.c:779-784` | `0x42000006`（**去掉 INA_BITS**）`:2902-2904` | **差异（未验证）** |
| **AS_MEMATTR** | `mair_to_memattr()` 由 CPU MAIR 推导 `:2098-2137` | 常量 `0x00084c8d888d8f88` `:877` | 差异（值一致，来源不同）|
| **AS_COMMAND** | `UPDATE` | `UPDATE` `:2950` | 一致 |
| **L2 上电** | `hw_l2_power_on()` 在 fw_load 前 `:1523` | `gpu_l2_power_on()` 在 load_firmware 前 `:4679` | 一致 |
| **coherency 写** | 非 coherent 时写 31 `panthor_gpu.c:63` | 冷路径**不写**（仅 `mali_cold=0`/`mali_cohnone=1`）`:3188-3195` | **差异（未验证）** |
| **L2_CONFIG / ASN hash** | 仅 `soc_data->asn_hash_enable` 且 ARCH<11 才写 | 冷路径不写 | 一致（G610 是 arch10，日志 185-186 证实 bit24 为 0）|
| **PWR_KEY** | probe 路径不写 | 冷路径不写 | 一致 |
| **软复位** | 仅 reset work / suspend，probe 不调 `panthor_device.c:143-145` | 冷路径不调 | 一致 |
| **SHADER_PWRON** | MCU 启动路径不写 | 不写 | 一致（任务书已排除 shader_ready）|
| **JOB_INT_MASK** | 写 `~0`（使能集）`:1128` | 写 `0xffffffff` `:3148` | 一致（注释错，行为对）|
| **MCU_CONTROL** | `AUTO`=2 `:1143` | `AUTO`=2 `:3859` | 一致 |
| **成功判据** | `JOB_INT_RAWSTAT & BIT(31)` / INT_STAT `:1109,1142-1148` | `JOB_INT_STATUS & BIT(31)` `:3913` + version 词 `:3968` | 一致 |
| **中断注册** | 4 条 GIC（job/mmu/gpu/pwr） | **0 条** | 差异（去掉了快路径，不阻止启动）|
| **PDC / MTCMOS / I2M / PMIF** | 无（交给 genpd/regulator） | 有（抄 vendor） | 差异（board-specific，无上游对照）|

红色差异（可能导致失败）：**GPUEB 缺席**、**TRANSCFG 缺 INA_BITS**、**coherency 未写**。

---

## 2. CSF 固件从哪来

### 2.1 有加载，来源是内嵌 blob

- `#include "hakios.mali610_fw.h"` `:43`
- 头文件声明 `MALI_FW_LENGTH 258048` / `MALI_FW_SHA256 "de22d2fc06bc8a859968499a6e609c27fe838be062bf5c97d92ea9f904f7229a"`（`/home/lizi/IOS/hakios-mtk/drivers/gpu/hakios.mali610_fw.h:12-14`）
- 生成器 `mtk/tools/mkmalifw.py`：从 `mali_csffw.bin` 直接转字节数组（不是 hex 字符串）

### 2.2 加载到哪、多长、怎么校验

- `load_firmware()` `:1009`：校验 magic `0xc3f13a6e`、major==0、size<=长度（`:1014-1023`）
- 解析 20 字节头 + entry 表（`FW_ENTRY_IFACE=0`，`:1033`）
- 每个 section `mem_alloc()` 到低窗 `0x50000000-0x5c000000`（`:690-704`），`ml_io_map_unmappable(WCOMB)` 非缓存映射
- 逐 4 KiB leaf 建表（`:1185-1191`），`PT_LEAF_GRANULE=0x1000`
- 运行期覆盖率自检（`:1274-1311`）

### 2.3 真机证据：镜像与映射都对

日志 192-235：

```
mali: firmware 258048 bytes, header 0.1, entry table ends at 768
mali: section 0: va 0-0x1000 flags 0x9 (r-- cached) 2836 bytes
mali: section 1: va 0x1000000-0x1010000 flags 0xd (r-x cached) 63560 bytes
mali: section 9: va 0x4000000-0x4094000 flags 0xc000001b (rw- nc shared) 8192 bytes
mali: walk 0: L0 ... L3 0x400001a07144c3 page
mali: 10 sections, 9 page tables
mali: coverage: every section fully mapped
mali: reset vector: sp 0x2040000  pc 0x10018ed
```

**结论：固件加载不是根因。** 但三点弱项：运行期不校验 sha256；PROT section 用普通内存映射（panthor 直接跳过，`panthor_fw.c:590-594`）；`entry table ends at 768` 是头部 size 字段，与 258048 不矛盾（size 是 entry 区长度）。

### 2.4 与 panthor 的一个实质差异

panthor 把固件 VM 建成 `0x04000000` 起、64 MiB（`panthor_fw.c:205-206`、`:1527-1531`）。我们只映射镜像实际用到的 10 个段（最大到 `0x4094000`）。**未验证**这是否影响 MCU 的栈/堆访问——`section 6/7/8` 是 0 字节的 PROT 段（日志 213-222），若固件在启动早期就往 `0x3000000` 附近写，我们给了普通内存（日志 211 明确「mapped with ordinary memory」），panthor 则是直接不映射。

---

## 3. 九个失败面

判定图例：**已排除** = 有真机读数直接否定；**待验** = 有读数但不足以判定，或需一次实验；**无法判** = 现有手段读不到。

### F1 时钟

| 项 | 内容 |
|---|---|
| 判据 | `0xF3C & 0x3ffc == 0x3ffc`；`GPU_ID == 0xa8670004`；`GPU_CYCLE_COUNT`/`GPU_TIMESTAMP` 递增；`MFG_TIMESTAMP(0x130) != 0` |
| 真机 | `0xF3C 0x3ffc`（日志 162）；`gpu_id 0xa8670004`（163）；`cycle 0xaffd4d22→0xb0027e31`、`timestamp 0x221e88b4→0x221ec24b`（577）；`timestamp register 0x3`（167）|
| 判定 | **已排除**。GPU 自身时钟在跑，PLL 已锁（`con0 0x15445` 日志 166），晶体与 mux 已切 |
| 残余 | `mfgsc_ref_ck`（CLK_CFG_5 bit7）在 `mali_scclk` 下才开；日志 164 显示 692 跑里已开（`cfg5 0x1010102`）。若 MCU 的取指走 MFGSC 域，此路已通 |
| 修复代价 | 0 行 |

### F2 电源域

| 项 | 内容 |
|---|---|
| 判据 | 每个域控制字 `(v & 0xC0000000) == 0xC0000000` 且 `!(v & (1<<8))`（SRAM_PDN）且 `!(v & (1<<12))`（SRAM_ACK）|
| 真机 | 12 域里 **7 个** 起来（日志 132）；mfg5/6/7/8/9/10/11/12 报 `0xd` 或 `0x4000000d`（日志 35/53/69/85/102/115/131）；`mali_nopdc=1` 后 mfg5/6/10/12 变 `0xc000000d`（161）——**控制字看起来是 on 的** |
| 关键歧义 | `0xc000000d` = bit31/30 置位（sta=3）但 bit0(RST_B)=1、bit2(PWR_ON)=1、bit3(PWR_ON_2ND)=0、bit8=0。`domain_on()` 的「already on」早退要求 `RST_B` 且 `!SRAM_PDN`（`:1537-1541`），`0xc000000d` **满足**该条件 → 走早退，`printf("already on")`。但日志里**没有** mfg1 的 "already on" 行——说明 mfg1 首次进入时不是这个值，是走完六步才变成 `0xc000000d`（日志 585 的 `mfg1 0xc000000d` 是终态） |
| 判定 | **待验**。硬件说 mfg1 的 sta=3（on），但 `0xF3C` 在 `mali_nopdc=1` 时读到 `0x3ffc`（全 12 域上电）而控制字仍 `0xc000000d`，两者矛盾指向「SPM 状态位不可全信」或「PDC 硬件自己拉起来了但控制字未反映」|
| 修复代价 | 若需重写域序列：约 20 行（照 `mtk-scpsys.c` 六步无早退，`:1520-1535` 已注释说明）|

### F3 复位

| 项 | 内容 |
|---|---|
| 判据 | mfg1 控制字 bit0 `SPM_PWR_RST_B=1`；GPU 侧 `RESET_CONTROL`/`GPU_CMD` 无残留；`GPU_IRQ_RAWSTAT` bit8（RESET_COMPLETED）曾置位 |
| 真机 | mfg1 `0xc000000d` → bit0=1，复位已释放（日志 585）；`gpu irq raw 0 (reset 0 ...)`（575）——**RESET_COMPLETED 从未置位**，但冷路径本来就不做软复位，不置位是预期的 |
| 判定 | **已排除**（域级复位）。CSF 子系统级复位无独立寄存器证据，标「无证据」|
| 修复代价 | 0 行 |

### F4 MMU / 地址翻译

| 项 | 内容 |
|---|---|
| 判据 | AS0 `AS_TRANSTAB` 非零且低位对齐；`AS_TRANSCFG` 的 ADRMODE=6（4K）；`AS_STATUS.bit0` 走查后回 0；`MMU_INT_RAWSTAT` bits0-15 为 0（无 fault）；`AS_FAULTSTATUS==0` |
| 真机 | `transtab 0x1a0550000`、`memattr 0x84c8d888d8f88`、`transcfg 0x42000006`（日志 244）；`status 0 fault 0 addr 0`（246）；`raw 0x10000`（245）|
| **关键更正** | `0x10000` = bit16 = **AS0 的 REQ_COMPLETE**，不是 BUS_ERROR。panthor：`panthor_mmu.c:648-649`「Bits 16 to 31 mean REQ_COMPLETE」，`fault_mask = value & GENMASK(15,0)`。第二份副本 `/tmp/csf/linux-source-7.0.0/drivers/gpu/drm/panthor/panthor_mmu.c:614` 同注释。**所以 AS0 的 UPDATE 是成功完成的，没有任何 fault** |
| 残余差异 | `AS_TRANSCFG` 缺 `INA_BITS(55-va_bits)`。`va_bits=48`（日志 172 `mmu 0x2830 (va 48 bits)`）→ panthor 会写 `0x420001c6`，我们写 `0x42000006`。INA_BITS 告诉 walker 输入地址宽度，**是否影响 MCU 取指未验证** |
| 判定 | **已排除**（表本身、走查、fault）；**待验**（INA_BITS）|
| 修复代价 | 1 行（把 `transcfg |= ((55 - va_bits) & 0x3f) << 6`）|

### F5 GPUEB 仲裁 ★头号怀疑

| 项 | 内容 |
|---|---|
| 判据 | GPUEB 是否回应 IPI（`CMD_INIT_SHARED_MEM(0)` → `CMD_POWER_CONTROL(6)`）；共享内存 `0x7c600000` 的 `power_count(+0x6c)` / `power_control(+0x60)`；**不允许直读 `0x13c40000`**（任务硬约束）|
| 现状 | **一次 IPI 都没发。** 驱动无 `gpueb_power_on`、无 mailbox、无共享内存映射（`mali-gap.json` 的 `absent_from_ours`）。日志 174/585 `gpueb: not mapped` 是 `mali_gpueb=0` 默认导致（`:4287-4290`），**不是** GPUEB 死掉的证据 |
| 上游证据 | panthor 在 MT6895 上无条件（`CONFIG_MTK_GPUEB` + compatible + available）调用 `mt6895_gpueb_power_on()`：`panthor_fw.c:1131-1143`；更早在 `panthor_device.c:256-268` 调一次，注释「otherwise panthor reads all-zero ID/features」|
| 独立佐证 | `/tmp/gpueb-abi-2dn1xxy2/REPORT.md`：vendor `gpufreq_mt6895.c` 在 `gpueb-support=<1>` 时**整段跳过**自己的 `power_control`（含 MTCMOS 与 PLL），交给 EB。我们的驱动抄的是被跳过的那条路径 |
| 为什么它能解释全部症状 | EB 若仍持有 GPU 电源门/时钟门，则：寄存器块可读（AP 侧总线独立）、`GPU_ID` 有值、L2 能上电（同一域）、页表能建（CPU 侧内存）、`MCU_CONTROL` 状态机接受写（纯寄存器）、但 **MCU 取不到第一条指令**（取指通路被 EB 门住）→ 无 fault、无 version 词、sentinel 不变、`JOB_INT` 不起。与日志完全吻合 |
| 判定 | **待验**。「EB 是否 gate 了 GPU」在现有手段下**无法判**：共享内存字段是旁证不是判据（§E5），唯一判据是发一次 IPI 并观察 MCU 是否起来（§E6）|
| 修复代价 | 实现 IPI：约 80-120 行（mailbox 通道 + 32 字节消息 + 10 s 超时 + 共享内存映射）。若只需「发一次 POWER_CONTROL 试试」，最小 40 行 |

### F6 固件镜像

| 项 | 内容 |
|---|---|
| 判据 | magic、major、size、section 完整性、reset vector 合理、sha256 |
| 真机 | 全部通过（日志 192-235）；sha256 头里有但**运行期不校验** |
| 判定 | **已排除**（镜像正确且映射完整）；「sha256 未校验」是工程弱项，不是当前失败原因 |
| 修复代价 | 校验 5 行（可选）|

### F7 安全 / 保护位

| 项 | 内容 |
|---|---|
| 判据 | `GPU_MEM_FEATURES` 保护位；`JOB_INT_MASK` 是否误屏；PROT section 是否需要 protected-mode 分配器 |
| 真机 | `mmu 0x2830`（日志 172）；`JOB_INT_MASK` 语义已澄清为「使能集」（§0 第 3 条），`0xffffffff` = 全开，**不是误屏** |
| 残余 | 3 个 PROT section（`0x3000000-0x3040000`）我们用普通内存（日志 211/215/219）。panthor 无 protected allocator 时**直接跳过**这些段（`panthor_fw.c:590-594`）。若固件在启动早期写这些地址，我们给了内存（更宽容），不会更差 |
| 判定 | **已排除**（无 secure mode 要求证据；PROT 段处理不比 panthor 差）|
| 修复代价 | 0 行 |

### F8 中断

| 项 | 内容 |
|---|---|
| 判据 | MCU 是否需要 AP 先使能某个中断才肯跑 |
| 依据 | panthor 的 `fw_start()` 只做：`booted=false` → 使能全部 job 事件 → resume → GPUEB → 写 AUTO → 等 1 s（`panthor_fw.c:1121-1168`）。**没有任何「先等 AP 中断就绪」的前置**。启动是「写一个寄存器 + 轮询」 |
| 判定 | **已排除**（0 处中断注册不阻止 MCU 执行；只损失快速路径）|
| 修复代价 | 0 行（若要加 GIC 中断，约 60 行，但不解决当前问题）|

### F9 硬件版本 / 兼容

| 项 | 内容 |
|---|---|
| 判据 | `GPU_ID` 的 ARCH_MAJOR/MINOR 与固件目录名匹配；`CSF_ID` 合理 |
| 真机 | `GPU_ID 0xa8670004` → ARCH_MAJOR = `0xa` = **10**，MINOR = `8` → arch10.8（日志 163）；`CSF 0x40a0412`（171）|
| 上游 | panthor 按 `arch%d.%d` 找固件（`panthor_fw.c:810-812`），即 `arch10.8/mali_csffw.bin`。`MODULE_FIRMWARE` 列表含 `arch10.8`（`panthor_fw.c:1560`）|
| 判定 | **已排除**（G610 = arch10.8，与固件匹配；固件头 major 0 也匹配 `CSF_FW_BINARY_HEADER_MAJOR_MAX=0`）|
| 修复代价 | 0 行 |

### 失败面汇总

| 面 | 判定 | 一句话 |
|---|---|---|
| F1 时钟 | 已排除 | cycle/timestamp 在跑，PLL 已锁 |
| F2 电源域 | 待验 | 控制字 sta=3 但 5 域报 no-ack；`0xF3C` 与 `0xF34` 不一致 |
| F3 复位 | 已排除 | mfg1 RST_B=1 |
| F4 MMU | 已排除（表）/ 待验（INA_BITS）| `0x10000` 是 REQ_COMPLETE，不是 fault |
| **F5 GPUEB** | **待验（头号）** | 一次 IPI 都没发 |
| F6 固件 | 已排除 | 258048 字节全映射，coverage 完整 |
| F7 安全 | 已排除 | 无 secure 要求；MASK 极性已澄清 |
| F8 中断 | 已排除 | panthor 也不需要 |
| F9 版本 | 已排除 | arch10.8 匹配 |

---

## 4. 47 处 `delay()` 审计

实测 `grep -c 'delay('` = 47（与任务书一致）。按用途分四类：

| 类 | 数量 | 行号 | 评价 |
|---|---|---|---|
| **引导前静置** | 1 | `:5054` `delay(2000*1000)` | 必要（等机器稳定），但 2 s 是否够无证据 |
| **轮询间隔（有界）** | 33 | `:2713,3241,3274,3321,3501,3604,3795,3953,4230,4699,4912` 等 | 合理，1-10 µs 与 panthor 的 `10 µs` 量级一致（`panthor_device.h:531` `10, 100000`）|
| **域上电等待** | 2 | `:1587,1646` `delay(10)` | 与 `mtk-scpsys.c` 的 `MTK_POLL_DELAY_US=10` 一致；但**仅 `mali_domainwait=1` 时生效**，默认是 200 万次空转（约 0.72 s，低于 vendor 的 1 s 预算）→ **潜在不足** |
| **PLL / 时钟稳定** | 9 | `:2162,2164,2166,2179,2182,2187(50),2189,2229,2238` | 2 µs / 50 µs，无上游对照值；**未验证** |
| **PDC 硬件等待** | 2 | `:1792,4425` `delay(200000)` | 200 ms 盲等，无收敛判据 → **盲等** |
| **MCU 启动后单次采样** | 3 | `:2336,2365,3817,4083` | 1 ms 采样间隔，用于读计数器和 WDT，合理 |
| **失败后 DISABLE 等待** | 2 | `:4898(2000),:4912(1000)` | panthor 允许 100 ms 停止（`panthor_fw.c:1177` `10, 100000`），我们 2 ms 后就读 → **偏短**，但由于本就失败，不影响判定 |

**结论**：没有一处 delay 是「MCU 不启动」的直接原因。真正的问题是 `:1587/:1646` 的默认分支不 delay（预算 0.72 s < vendor 1 s），以及 `:1792/:4425` 的 200 ms 盲等没有收敛判据。

---

## 5. 遗漏的写操作（脚本判定）

`mali-seq-diff.py` 在 panthor 的 MCU 启动路径上列了 17 项寄存器动作，**全部 PRESENT**（0 ABSENT）。真正的缺口不在寄存器层，而在**调用层**：

| 缺口 | 证据 | 影响 |
|---|---|---|
| `mt6895_gpueb_power_on()` | `panthor_fw.c:1134` | 头号怀疑，见 F5 |
| 写 EB 块 / mailbox | panthor 走 mailbox，不走 MMIO | 同 F5 |
| 运行期固件加载 | `panthor_fw.c:817` | 机制差异，非缺陷 |

寄存器层的**值差异**（脚本按名字比对，值需人工看）：

| 寄存器 | panthor | 我们 | 位置 |
|---|---|---|---|
| `AS_TRANSCFG` | `0x420001c6` | `0x42000006` | `:2902-2904` |
| `GPU_COHERENCY_PROTOCOL` | `31`（非 coherent） | 冷路径不写 | `:3188-3195` |

---

## 6. 建议实验清单（按「验证成本 × 可能性」排序）

成本定义：★ = 一次刷机 + 读日志（约 15 min）；★★ = 加代码 + 刷机；★★★ = 新子系统。

### E1 ★ 读 `JOB_INT_RAWSTAT` 而不只读 `JOB_INT_STATUS`

- **读**：`0x1000`（RAWSTAT）、`0x100c`（STATUS）、`0x028`（GPU_IRQ_MASK）、`0x02c`
- **期望**：若 RAWSTAT bit31 置位而 STATUS 不置位 → 是自己屏蔽（但 §0 已证极性正确，可能性低）；若两者皆 0 → 硬件确实没发
- **判定**：RAWSTAT bit31 = 0 → 排除「屏蔽」这条，锁定「MCU 未执行」
- **风险**：无（只读）
- **现状**：`kJobIntRaw` 在 `:358` **只被定义、全文件零次使用**（`grep -n kJobIntRaw` 仅命中定义行），日志 264/573 也只有 STATUS。所以「RAWSTAT 是否置过 bit31」这一生从未被观测过

### E2 ★ 用 `mali_as0=1` 对照组确认 `0x10000` 语义

- **做**：`mali_as0=1`（把 AS0 当 AS2..7 一样停用）
- **读**：`0x2000`（MMU raw）、`0x2418`
- **期望**：停用 AS0 后 UPDATE 也会置 bit16 → 证明 bit16 是 REQ_COMPLETE，与是否映射无关
- **判定**：若 `mali_as0=1` 也出现 `0x10000`，则 §0 第 1 条被二次确认
- **风险**：无（不启动 MCU，只到 `gMaliProgress=2` 返回）
- **价值**：把「AS0 walk 失败」这条从所有后续推理里彻底删掉

### E3 ★★ 加 `INA_BITS` 一行（F4 残余）

- **改**：`:2902-2904` 的 `transcfg` 加 `| (((55u - va_bits) & 0x3fu) << 6)`
- **读**：`0x2418` 后 `0x2000`、`0x100c`、version 词，以及 sentinel
- **期望**：若 MCU 起来 → `0x100c` bit31 置位 / version 词被改写
- **判定**：version 词 != `0xff5aa5f0` → 成功
- **风险**：低（只改一个 AS 属性；走查失败会表现为 `0x2000` bits0-15 非零）
- **代价**：1 行

### E4 ★★ 冷路径写 `GPU_COHERENCY_PROTOCOL = 31`（F1/F2 之间）

- **改**：`:3188-3195` 去掉 `!gMaliCohNone` 条件，冷路径也写 31
- **读**：`0x304`、`0x100c`、version 词
- **期望**：panthor 在非 coherent 平台无条件写 31（`panthor_gpu.c:63`、`:466`）
- **判定**：version 词变化 → 成功；不变 → 排除
- **风险**：低（日志 184 显示当前读到 `features 0x1 / protocol 0`，写 31 是朝 vendor 方向）

### E5 ★★★ 读 GPUEB 共享内存（F5 旁证，非判据）

- **做**：只 map 共享内存 `0x7c600000`（2 MiB），**不碰 `0x13c40000`**，读 `+0x60 power_control`、`+0x64 dvfs_state`、`+0x68 shader_present`、`+0x6c power_count`
- **依据**：`/tmp/gpueb-abi-2dn1xxy2/REPORT.md` 的字段偏移，且该报告明确「shared RAM 独立确认为物理 0x7c600000、2 MiB」
- **期望**：`power_control` 是**使能开关标量**（`!=0` = GPU 电源归 GPUEB 管，`==0` = AP 自己管），不是状态位（`GPUEB-IPI-PROTOCOL.md` §6.2，引自 `gpufreq_v2.c:117-139`、`gpufreq_mt6895.c:301-305`）；`power_count` 是有符号引用计数
- **判定**：这条**读不出「EB 是否 gate 了 GPU」**。`power_control != 0` 只证明「Android 曾把电源交给 EB 管」，而据同一文档 §6.2，若它为 0 则 vendor 路径根本不会发 IPI。两种取值都不能证明 GPU 当前被 gate。**E5 因此只能作旁证，不能作判据**——这与 §8「无法判」一致
- **真正的判据只有 E6**（发 IPI 并观察 MCU 是否起来）
- **风险**：中（共享内存是普通 DRAM，读它不触发 MMIO 挂死；但仍需确认该地址在 XNU 的 DRAM 映射内）
- **信息量修正**：本项原本被当作「F5 的廉价判据」，核对 `GPUEB-IPI-PROTOCOL.md` §6.2 后降级为旁证
- **注意**：报告同时指出「无 heartbeat/timestamp，`power_count` 非零可能是 Android 遗留」→ 这是**弱判据**，只能作旁证

### E6 ★★★ 发一次 `CMD_POWER_CONTROL`（F5 决定性）

协议细节已由同目录 `GPUEB-IPI-PROTOCOL.md` 还原到可照抄的精度，本节只给判定逻辑，不重复其内容。

- **做**：`CMD_INIT_SHARED_MEM(0)`（把 `0x7c600000` 的 `status_base/debug_base/size` 按 `u32[2..7]` 填好）→ 等 `IPI_ACTION_DONE` → `CMD_POWER_CONTROL(6)`，`power_state=1`
- **通道**：GPUFREQ 的 TX 是 **slot 4**（字节 `0x10`）、RX 是 **slot 47**（字节 `0xbc`），同一 `pin_index`（bit 1）；消息 SRAM 在 `0x13c5fd80`，寄存器 `send 0x13c62000 / set 0x13c62004 / clr 0x13c62074 / recv 0x13c62078`（`GPUEB-IPI-PROTOCOL.md` §1、§3、§4）
- **读**：RX slot 47 的 `u32[2]`（`return_value`）、`0x100c`、version 词
- **期望**：若 EB 是 gate 的持有者，这一步之后 MCU 应能取指
- **判定**：version 词 != sentinel → 根因确认；ACK 到了但 version 词不变 → EB 不是 gate 持有者，F5 排除
- **风险**：高。`IPI_ACTION_DONE` 依赖 GIC SPI 548（`GPUEB-IPI-PROTOCOL.md` §1），我们 0 处中断注册 → 只能轮询 `mbox0_recv` 的 bit1；EB 若不响应会阻塞到超时
- **代价**：按该文档照抄约 80-120 行（消息组装 + 轮询 + 超时）

### E7 ★ 把 `mali_domainwait=1` 设为默认（F2）

- **改**：`gMaliDomainWait` 默认 1
- **读**：12 域最终控制字、`0xF34`、`0xF3C`
- **期望**：`mtk-scpsys.c` 用 10 µs × 1 s；我们现在是 200 万次空转 ≈ 0.72 s（`:1562-1580` 注释自述）
- **判定**：若 mfg5/6 从 `0x4000000d` 变 `0xc000000d` → 之前是等得不够
- **风险**：低（只是变慢，最坏 12 域 × 1 s）
- **注意**：日志 161 显示 `mali_nopdc=1` 时 mfg5/6/10/12 已是 `0xc000000d`，所以这条**可能已被间接验证**

### 排序理由与「下一步只做一件」

若只能做一件：**E1**（成本 ★，只读，且能立刻关掉「自己把 bit31 屏蔽了」这个最后的分支）。E1 若确认 RAWSTAT 也为 0，则「MCU 未执行」成为唯一解释，此后所有工作都应转向 E6（发 IPI），而不是继续在 GPU 寄存器里找。

E2 紧随其后：它把「AS0 walk 失败」这条错误前提从推理链里永久删除，成本同样是 ★、零风险。

E3/E4 是「一行改动、独立变量」的两张彩票，成本 ★★，值得与 E1/E2 同一次刷机里分开跑（一次只改一个）。

E5 已降级为旁证；E6 是唯一能判定 F5 的实验，但成本 ★★★，应在 E1-E4 全部无果后再投入。

### 不建议现在做

- 加 GIC 中断注册：panthor 的启动不依赖它（§F8），先解决取指
- 重写页表 / 换 granule：coverage 已完整、walk 已成功（§F4）
- 再改 PDC：`mali_nopdc=1` 已证明不是充分原因（任务书已排除）

---

## 7. 复现命令

```bash
cd /home/lizi/IOS/repo/hakios-mt6895/docs/headset-wip
python3 mali-seq-diff.py --selftest     # 退出码 0
python3 mali-seq-diff.py                # 退出码 0；17 项寄存器动作全 PRESENT
python3 mali-seq-diff.py --json         # 机器可读
```

脚本输出（真跑，见 §8）：

```
parsed 5220 lines: 118 MMIO writes, 377 MMIO reads, 5 shared-block stores
ABSENT -- panthor does this, we never do (0 of 17):
SEQUENCE-LEVEL -- not a register write, so invisible in the trace above:
  pre-mcu  EB power request (IPI)   ABSENT   never
  pre-mcu  write to the EB block    ABSENT   never
  pre-mcu  runtime firmware load    ABSENT   never
```

---

## 8. 证据边界

- 「已排除」的定义：有真机读数**直接**否定该假设，且读数可追到日志行号。
- 「待验」：有读数但存在至少一个未分离变量（如 F2 的 `0xF3C` vs 控制字矛盾）。
- 「无法判」：现有手段读不到（如 EB 是否已上电 GPU——需 IPI 或共享内存，两者都未实现）。
- **未验证**项：`INA_BITS` 对 MCU 取指的影响；`mali_domainwait` 默认值的实际效果；EB 共享内存 `power_count` 的语义（外部报告已警告可能是 Android 遗留）。
- **无证据**项：CSF 子系统独立复位寄存器（本次审计未找到）；GPUEB 固件在 XNU 下的存活状态。
- 本报告**未**修改 `/home/lizi/IOS/hakios-mtk` 下任何文件；仅读取。

## 9. 引用

- 驱动：`/home/lizi/IOS/hakios-mtk/mtk/mt6895/drivers/gpu/hakios.mali610.c`（行号如上）
- 固件头：`/home/lizi/IOS/hakios-mtk/drivers/gpu/hakios.mali610_fw.h:12-14`
- 日志：`/home/lizi/IOS/hakios-mtk/logs/headset-resume-20261114-220244/nopdc/kernel-lines.txt`
- panthor：https://raw.githubusercontent.com/MT6895-Mainline/linux/7.2-mt6895-xiaomi-xaga/drivers/gpu/drm/panthor/panthor_fw.c
- panthor：https://raw.githubusercontent.com/MT6895-Mainline/linux/7.2-mt6895-xiaomi-xaga/drivers/gpu/drm/panthor/panthor_device.c
- panthor：https://raw.githubusercontent.com/MT6895-Mainline/linux/7.2-mt6895-xiaomi-xaga/drivers/gpu/drm/panthor/panthor_mmu.c
- panthor：https://raw.githubusercontent.com/MT6895-Mainline/linux/7.2-mt6895-xiaomi-xaga/drivers/gpu/drm/panthor/panthor_gpu.c
- GPUEB ABI 报告：`/tmp/gpueb-abi-2dn1xxy2/REPORT.md`

---

## 附录 A：`mali-seq-diff.py` 原始输出（真跑）

```
$ python3 mali-seq-diff.py --selftest
selftest ok: parse, comment strip, multi-line join, name resolution, function attribution, read/write/shared split, verdicts, normalisation, arg splitting
exit=0

$ python3 mali-seq-diff.py
parsed 5220 lines: 118 MMIO writes, 377 MMIO reads, 5 shared-block stores

PHASE      REGISTER               KIND     VERDICT  OURS
----------------------------------------------------------------------------------------------------
probe      GPU_ID                 read     PRESENT  mali_bring_up:4326, mali_bring_up:4327, mali_bring_up:4330 (+5)
probe      GPU_COHERENCY_PROTOCOL write    PRESENT  gpu_l2_power_on:3191
l2         L2_PWRON               write    PRESENT  gpu_l2_power_on:3253, gpu_l2_power_on:3272
l2         L2_PWRTRANS            read     PRESENT  gpu_l2_power_on:3174, gpu_l2_power_on:3238, gpu_l2_power_on:3250 (+4)
l2         L2_READY               read     PRESENT  gpu_l2_power_on:3174, gpu_l2_power_on:3255, gpu_l2_power_on:3269 (+3)
fw-load    AS_TRANSTAB            write    PRESENT  as_enable:2929, as_enable:2934, as_park_unused:3462 (+1)
fw-load    AS_MEMATTR             write    PRESENT  as_enable:2930, as_enable:2935, as_park_unused:3463 (+1)
fw-load    AS_TRANSCFG            write    PRESENT  as_enable:2928, as_enable:2936, as_park_unused:3465 (+1)
fw-load    AS_COMMAND             write    PRESENT  as_command:2724, as_enable:2950
fw-start   JOB_INT_MASK           write    PRESENT  gpu_irq_mask_prestart:3148, start_mcu:3992
fw-start   MCU_CONTROL            write    PRESENT  start_mcu:3853, start_mcu:3859, mali_bring_up:4897 (+1)
fw-start   JOB_INT_STATUS         read     PRESENT  start_mcu:3869, start_mcu:3873, start_mcu:3913 (+4)
fw-boot    JOB_INT_CLEAR          write    PRESENT  gpu_irq_mask_prestart:3147, start_mcu:3991
fw-boot    CSF_DOORBELL(0)        write    PRESENT  start_mcu:3871, global_interface:4224
fw-boot    core_en_mask           shared   PRESENT  global_interface:4208
fw-boot    req                    shared   PRESENT  start_mcu:3772, global_interface:4213
fw-boot    ack_irq_mask           shared   PRESENT  global_interface:4211

ABSENT -- panthor does this, we never do (0 of 17):

EXTRA -- we touch these, panthor's MCU-start path does not (157 names, writes and shared stores only):
  [write] GPU_IRQ_CLEAR                       10, first at :3145 in gpu_flush_caches,gpu_irq_mask_prestart,gpu_l2_power_on
  [write] MMU_INT_CLEAR                        8, first at :2920 in as_enable,as_park_unused,gpu_irq_mask_prestart
  [write] GPU_CMD                              3, first at :3496 in gpu_flush_caches,gpu_soft_reset,start_mcu
  [write] MALI_CLK_CFG_5_CLR                   3, first at :2204 in pll_300mhz
  [write] GPU_IRQ_MASK                         2, first at :3146 in gpu_irq_mask_prestart,start_mcu
  [write] MALI_CLK_CFG_4_CLR                   2, first at :2158 in clock_26m
  [write] MALI_CLK_UPDATE                      2, first at :2161 in clock_26m,pll_300mhz
  [write] MALI_MFGPLL_CON3                     2, first at :2178 in pll_300mhz
  [write] MALI_MFG_I2M_CFG_00                  2, first at :2604 in mfg_gpm_arm
  [write] MALI_MFG_QCHANNEL_CON                2, first at :2274 in mfg_bus_idle,mfg_master_gates
  [write] MALI_SPM_BUCK_ISO                    2, first at :1718 in mali_aoc_up
  [write] MALI_SPM_SEMA_M4                     2, first at :1706 in mali_aoc_up
  [write] MMU_INT_MASK                         2, first at :3150 in gpu_irq_mask_prestart,start_mcu
  [write] kIpaCmd                              2, first at :3397 in gpu_ipa_arm
  [write] GPU_L2_CONFIG                        1, first at :3224 in gpu_l2_power_on
  [write] GPU_PWR_KEY                          1, first at :3172 in gpu_l2_power_on
  [write] MALI_CLK_CFG_30_CLR                  1, first at :2163 in clock_26m
  [write] MALI_CLK_CFG_30_SET                  1, first at :2188 in pll_300mhz
  [write] MALI_CLK_CFG_4_SET                   1, first at :2160 in clock_26m
  [write] MALI_CLK_CFG_5_SET                   1, first at :2205 in pll_300mhz
  [write] MALI_MFGCFG_CLR                      1, first at :2165 in clock_26m
  [write] MALI_MFGPLL_CON0                     1, first at :2186 in pll_300mhz
  [write] MALI_MFGPLL_CON1                     1, first at :2183 in pll_300mhz
  [write] MALI_MFG_ASYNC_CON                   1, first at :2514 in mfg_master_gates
  [write] MALI_MFG_ASYNC_CON_1                 1, first at :2517 in mfg_master_gates
  [write] MALI_MFG_AXCOHERENCE                 1, first at :2574 in mali_acp_up
  [write] MALI_MFG_DCM_CON_0                   1, first at :2510 in mfg_master_gates
  [write] MALI_MFG_DEBUG_SEL                   1, first at :2276 in mfg_bus_idle
  [write] MALI_MFG_GLOBAL_CON                  1, first at :2526 in mfg_master_gates
  [write] MALI_MFG_I2M_CFG_01                  1, first at :2605 in mfg_gpm_arm
  [write] MALI_MFG_I2M_CFG_02                  1, first at :2606 in mfg_gpm_arm
  [write] MALI_MFG_I2M_CFG_03                  1, first at :2607 in mfg_gpm_arm
  [write] MALI_MFG_RPC_AO_CLK_CFG              1, first at :2533 in mfg_master_gates
  [write] MALI_MFG_TIMESTAMP                   1, first at :2661 in mfg_set_timestamp
  [write] gals[i].at                           1, first at :2579 in mali_acp_up
  [write] kCsfConfig                           1, first at :4617 in mali_bring_up
  [write] kIpaCfg                              1, first at :3392 in gpu_ipa_arm
  [write] kIpaClear                            1, first at :3401 in gpu_ipa_arm
  [write] kL2MmuConfig                         1, first at :4616 in mali_bring_up
  [write] kPoll[q].clr_off                     1, first at :1992 in bus_protect_release
  [write] kProt[i].clr_off                     1, first at :2110 in bus_protect_release_verified
  [write] kShaderConfig                        1, first at :4614 in mali_bring_up
  [write] kStackPwrOn                          1, first at :4692 in mali_bring_up
  [write] kTilerConfig                         1, first at :4615 in mali_bring_up

  113 further names are reads panthor also performs (or diagnostics unique to this driver); 31 are register arguments the parser could not fold (helper wrappers).

SEQUENCE-LEVEL -- not a register write, so invisible in the trace above:
  pre-mcu  EB power request (IPI)   ABSENT   never  [panthor_fw.c:1134; panthor_device.c:262]
  pre-mcu  write to the EB block    ABSENT   never  [no panthor equivalent: panthor goes through the mailbox, not MMIO]
  pre-mcu  runtime firmware load    ABSENT   never  [panthor_fw.c:817; ours embeds the blob instead (hakios.mali610.c:43)]

not comparable by construction (board-specific, no panthor equivalent):
  MTCMOS/SPM domain words  panthor uses genpd; no register writes of its own
  PDC enables              panthor never powers the block; vendor-only step
  I2M protector            vendor-only step
  PMIF/SPMI regulator      panthor uses the regulator framework
exit=0
```
