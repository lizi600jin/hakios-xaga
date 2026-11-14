# MT6895 GPUEB 分析 —— Mali-G610 CSF MCU 启动的前置条件

> 证据约定：`[DTS]` = `research/raw/mt6895.dtsi`（MT6895-Mainline/linux, 分支 `7.2-mt6895-xiaomi-xaga`）；
> `[ML]` = 同一仓库 sparse checkout 到 `/tmp/gpueb/ml` 的源码；`[VND]` = `/home/lizi/IOS/work-xaga/k-xaga-16.2`（厂商 5.10 树）；
> `[OUR]` = `/home/lizi/IOS/hakios-mtk/mtk/mt6895/drivers/gpu/hakios.mali610.c`（只读，未改动）；
> `[FW]` = `/home/lizi/IOS/work-xaga/fw/gpueb_a.img` 的反汇编（RV32，capstone 交叉验证）；
> `[LOG]` = `logs/headset-resume-20261114-220244/baseline692-window.txt`（trial 692，最后一次记录到的真机启动）。

---

## 0. 一句话结论

**GPUEB 是 MT6895 上的一颗独立 RISC-V（RV33）小核，位于 MFG0 电源域、寄存器块在 `0x13C40000`，
由 LK 的 `app_load_gpueb()` 从 `gpueb` 分区装载并跑起来；它通过 mailbox/IPI 独占地掌管 GPU 的
电源状态机（PDC/MTCMOS 请求）、两路 PLL、两路 buck、VSTACK/VSRAM 与 AVS 表。**

少了它 MCU 不跑，机制不是"没供电"，而是：**Mali 的 CSF MCU 起来后要做的第一件事是让自己的
shader/tiler 电源域从 SPM 拿到 PWR_ACK（`SHADER_READY` 置位），而这条请求要经 MFG 的 PDC/RPC
路径落地；这条路径的 owner 是 GPUEB。AP 侧无论怎么手写 `MFG_ACTIVE_POWER_CON_*`，域都停在
`0x100d`（"请求已收、没有 ACK"），`SHADER_READY` 恒 0，于是固件在写共享接口版本字之前就挂住** ——
这正是我们看到的全部四个症状。

---

## 1. GPUEB 的硬件身份

### 1.1 地址与寄存器块

`[DTS] mt6895.dtsi:2650-2714`：

```dts
gpueb: gpueb@13c40000 {
    compatible = "mediatek,gpueb";
    gpueb-support = <1>;
    mbox_count = <1>;  mbox_size = <160>;  slot_size = <4>;  ts_mbox = <0>;
    reg = <0x13c40000 0x22000>,   /* gpueb_base */
          <0x13c5fd80 0x280>,     /* mbox0_base */
          <0x13c62004 0x4>,       /* mbox0_set */
          <0x13c62074 0x4>,       /* mbox0_clr */
          <0x13c62000 0x4>,       /* mbox0_send */
          <0x13c62078 0x4>;       /* mbox0_recv */
    interrupts = <GIC_SPI 516 IRQ_TYPE_LEVEL_HIGH 0>;
    gpueb_mem_table = <0 0x1000>,      /* MEM_ID_GPUFREQ 4 KiB  */
                      <1 0x180000>;    /* MEM_ID_LOG    1.5 MiB */
}
```

- **`0x13C40000` 不是 `0x13FFF000`**。任务里给的那个地址在整份 dtsi 里不存在；`0x13FFF000` 属于
  MFG 的 debug/DFD 一带，与 GPUEB 无关。**不要用它。**
- 共享内存（`MEM_ID_GPUFREQ`）在 xaga 上是 **`0x7C600000` 起 2 MiB**：`[DTS-xaga] mt6895-xiaomi-xaga.dts:618-624`
  的 `&gpueb { gpueb_mem_addr = <0x7c600000>; gpueb_mem_size = <0x200000>; }`，
  同文件 `:200-203` 的 reserved-memory `mblock-38-me_GPUEB_SHARED`。
- IPI 通道 8 条，名字写死在 dts 里（`send_name_table` / `recv_name_table`）：
  `IPI_ID_FAST_DVFS_EVENT(0) / IPI_ID_GPUFREQ(1) / IPI_ID_SLEEP(2) / IPI_ID_TIMER(3) /
  IPI_ID_FHCTL(4) / IPI_ID_CCF(5) / IPI_ID_GPUMPU(6) / IPI_ID_FAST_DVFS(7)`。

### 1.2 它是独立 MCU，不是 GPU 的一部分

| 证据 | 内容 |
|---|---|
| `[FW]` 固件头 | `strings gpueb_a.img` 含 `tinysys-gpueb-RV33_A`；GFH 段 `0x200` 处是 `j 0xac`，`0xac` 是 Reset_Handler（设 `sp=0x13c547f0`、装 `mtvec`）。**RISC-V RV32，FreeRTOS**，不是 ARM。见 `fw/GPUEB-RE.md:66-67, 93-96` |
| `[FW]` 反汇编 | 我复扫 LUI 立即数：`0x13000000`(GPU)、`0x13fa0000`(MFGPLL)、`0x13fa1000`(MFGSCPLL)、`0x13fbf000`(MFG_TOP/ACP/GALS/I2M)、`0x13f90000`(g3d_config)、`0x1c001000`(SPM)、`0x1c01f000`、`0x1c00f000`、`0x10000000/0x10001000`(infracfg)、`0x1c804000`(SPMI) —— 它自己驱动 GPU 的 PLL 和保护器，**不经过 AP** |
| `[VND] gpufreq_mt6895.c:5159-5163` | EB 模式下 probe **跳过** `__gpufreq_init_clk()` / `__gpufreq_init_mtcmos()` / 首次 `__gpufreq_power_control(POWER_ON)`：`if (g_gpueb_support) { ... goto register_fp; }`。开关来自 DT：`gpufreq_mt6895.c:4913` 读 `gpueb-support` |
| `[VND] gpufreq_v2.c:853-880` | `gpufreq_power_control()` 在 EB 模式下**只发一条 IPI** `CMD_POWER_CONTROL`，然后 `ret = g_recv_msg.u.return_value`；AP 侧一行 SPM/PLL/PMIC 代码都不跑 |
| `[VND] gpufreq_v2.c:1913-1960` | `gpufreq_gpueb_init()`：取 `IPI_ID_GPUFREQ` 通道、`mtk_ipi_register()`、把 `MEM_ID_GPUFREQ` 的 phys/size 用 `CMD_INIT_SHARED_MEM` 交给 EB |

### 1.3 它归 MFG0 电源域，而 MFG0 是常开的

LK 的字符串里有一句 `%s: GPUEB(MFG0) NOT power-on!!!`，说明 GPUEB 的 SRAM 在 MFG0 里。
`[VND] gpufreq_mt6895.c:3578` 注释：`MFG0 is off in ATF`（即 MFG0 由 ATF/LK 管，Linux 不管）。
`[LOG]` 我们读到的 `0x1c001eb8`（MFG0 PWR_CON）= `0xc000000d` / `0xc000224d` —— **MFG0 活着**。

**推论：XNU 启动时 GPUEB 是活着的**（LK 装好并启动它，之后没人关）。这也解释了为什么我们
一直"绕开它"却始终被它影响：它没有死，它在等指令。

### 1.4 固件怎么被启动（不是 AP 干的）

`strings dump/lk_a.img`：

```
app_load_gpueb(fdt, b_mode) == NO_ERROR
load gpueb
tinysys-gpueb-RV33_A
me_GPUEB_FW / me_GPUEB_SHARED / vm-GPUEB-reserved
%s: GPUEB load from partition failed, ret: %d
%s: GPUEB boot-up timed out!!!
%s: GPUEB_PLL = 0x%x   %s: GPUEB_MUX = 0x%x
```

即 **LK 负责：分配 `me_GPUEB_FW`、从 `gpueb_a/b` 分区读镜像、配 GPUEB_PLL/MUX、等它启动**。
Linux/panthor 从来不做这件事，只做"发 IPI 请求"。

---

## 2. 那个 commit 加了什么（`4e2c2ae8a1ddcd79019444c03d03b44771cb29cc`）

标题：`drm/panthor: xaga: GPUEB-backed devfreq, stacks clock, VSTACK rail`，作者 Hecheng Yu，2026-08-29。
**只改 3 个文件，全是 DVFS 相关，一行都没碰 MCU 启动。** 这一点很重要，见 §5。

### 2.1 `arch/arm64/boot/dts/mediatek/mt6895.dtsi`（17 行）

- `clocks` 从 1 个变 3 个：`core`(MFG_SEL_0) + **`coregroup`(MFGCFG BG3D)** + `stacks`(MFG_SEL_1)，
  `clock-names = "core","coregroup","stacks"`（patch 行 44-48）。
- 新增 `vstack-supply = <&mt6368_vbuck2>`（patch 行 52）。
- 注释明确：**"Real power management stays with gpueb; no power-domains."**（patch 行 42）
  也就是 GPU 节点**故意不挂 `power-domains`**。

### 2.2 `drivers/gpu/drm/panthor/panthor_devfreq.c`（+265 行）

- 内嵌两张"从真机 `/proc/gpufreqv2/` 抓下来的工作表"：`xaga_eb_gpu_freq[]`（6 档，950M…350M）、
  `xaga_eb_stack_freq[]`（40 档，852M…219M）（patch 行 167-178）。
- `xaga_eb_commit()`：把 **工作表索引**（不是 Hz）用 `mt6895_gpueb_commit(TARGET_GPU|TARGET_STACK, idx)`
  发给 EB；升频先 GPU 后 STACK、降频反过来（patch 行 236-266）。失败则 `xaga_eb_failed=true` 退回 CCF。
- 本地路径里额外把 `stacks` 时钟按 `xaga_stack_table[]`（率/压对）联动，**电压先升后降**（patch 行 268-325）。
- `get_cur_freq` / `get_dev_status` 在 EB 模式下改用自己记账的 `current_frequency`，因为
  `clk_get_rate()` 读的是 CCF 缓存，EB 改频后它不准（patch 行 330-353）。
- `panthor_devfreq_init()`：EB 模式下**不再** `dev_pm_opp_set_opp()`，并把初始频率硬定为 `219000000`
  （注释：`The EB restored its own boot OPP after POWER_CONTROL.`）（patch 行 370-407）。

### 2.3 `drivers/mfd/mtk-spmi-pmic.c`（1 行）

给 `mt6368-regulator` MFD cell 补 `of_compatible`，否则 DT 里的 rail 约束解析不出来（patch 行 414-420）。

**它的价值不在代码，在于三句事实性陈述**：① GPU 的电源/频率确实归 GPUEB；② GPUEB 一旦收到
`POWER_CONTROL` 就会"applies both PLLs, both bucks, both VSRAM rails and its own sequencing"；
③ EB 模式下 panthor **不能**自己 `dev_pm_opp_set_opp()`，会和 EB 打架。

---

## 3. GPUEB 上电/启动序列（可执行）

### 3.1 谁做什么

| 阶段 | 谁做 | 证据 |
|---|---|---|
| 装固件、配 GPUEB_PLL/MUX、放开 RV33 复位、等 boot-up | **LK**（`app_load_gpueb`） | `strings lk_a.img` |
| 给 GPUEB 递共享内存地址（`CMD_INIT_SHARED_MEM`） | AP 驱动，一次 | `[ML] gpueb_power.c:92-112`；`[VND] gpufreq_v2.c:1930-1960` |
| 请求 GPU 上电（`CMD_POWER_CONTROL`+`POWER_ON`） | AP 驱动，每次上电 | `[ML] gpueb_power.c:158-195`；`[VND] gpufreq_v2.c:868-877` |
| 真正执行：SPM 信号量 → buck → MTCMOS/PDC → 时钟/PLL → ACP/GALS → HWDCM → GPM → 恢复 MFG 寄存器 | **GPUEB 固件** | `[VND] gpufreq_mt6895.c:740-830`（AP 模式的同名九步，EB 模式由固件跑同一套）；`[FW] 0x13c4a2xx` 写 `0x13fbf400..0x4dc`、`0x13c4a388` 写 `0x168 |= 0xf`、`0x13c4a3a0..` 写 `0x8e0/0x8e8/0x910/0x918 |= 0x855`、`0x900/0x908/0x920/0x928 |= 0x55` |
| 改频（OPP 提交） | GPUEB，AP 只发索引 | `[ML] gpueb_power.c:118-142`（`CMD_COMMIT` + `target` + `oppidx`） |

### 3.2 AP 侧最小可执行协议（panthor 用的就是这一份）

`[ML] drivers/gpu/mediatek/gpueb/gpueb_power.c`：

```
1) 取通道:  gpueb_get_send_PIN_ID_by_name("IPI_ID_GPUFREQ")            // :83
2) 注册:    mtk_ipi_register(&gpueb_ipidev, chan, NULL, NULL, &recv)  // :87
3) 交内存:  msg.cmd_id = CMD_INIT_SHARED_MEM (0)                      // :97
            msg.u.addr.status_base = MEM_ID_GPUFREQ 的 phys
            msg.u.addr.status_size = 0x400
            msg.u.addr.debug_base  = phys + 0x400
            msg.u.addr.debug_size  = size - 0x400
            mtk_ipi_send_compl(..., IPI_SEND_POLLING, &msg, 8 words, timeout)
4) 上电:    msg.cmd_id = CMD_POWER_CONTROL (6); msg.u.power_state = POWER_ON(1)  // :169-174
5) 改频:    msg.cmd_id = CMD_COMMIT (7); msg.target = 1(GPU)/2(STACK); msg.u.oppidx  // :124-130
```

消息结构 `struct gpufreq_ipi_data { cmd_id; target; union{...}; }` = 8 个 32 位字，
见 `[ML] gpueb/include/gpufreq_ipi.h:51-78`；`GPUFREQ_IPI_DATA_LEN = sizeof/4`。
mailbox 侧：数据先写 `mbox0_base + slot*4`（`MBOX_SLOT_SIZE=4`，`[ML] mtk-mbox.h:18`），
再写 `mbox0_set`(0x13C62004) 的对应 bit 触发；EB 回写后 `mbox0_recv`(0x13C62078) 可见。
槽位分配是"按 send 表顺序累加 msg_size"，见 `[ML] gpueb_ipi.c:250-273`。

### 3.3 关键：`CMD_POWER_CONTROL` 是**必须**的，且不止一次

- `panthor_device_init()`：`pm_runtime_resume_and_get()` 之后、`panthor_devfreq_init()`/`panthor_hw_init()`
  之前就调一次 `mt6895_gpueb_power_on()`（`[ML] panthor_device.c:256-267`），注释写明
  "otherwise panthor reads all-zero ID/features"。
- `panthor_fw_start()`：在写 `MCU_CONTROL = AUTO` 的**上一行**再调一次
  （`[ML] panthor_fw.c:1131-1143`）。这一段是 hot-reset 也会走的路径（`panthor_fw_resume()` → `panthor_fw_start()`，`[ML] panthor_fw.c:1289`）。
- 之后 panthor 干脆 `pm_runtime_forbid()`（`[ML] panthor_device.c:316-322`），因为 EB 与固件
  的电源状态一旦不同步就会 CSG timeout。

---

## 4. GPUEB 与 GPU 电源域 / 时钟 / OPP 的关系

### 4.1 电源域

- MT6895 的 GPU MTCMOS 被切成 `MFG1..MFG12`，**全部**带 `MTK_SCPD_BYPASS_INIT_ON`
  （`[ML] mtk-scpsys-mt6895.c:462-567`，每个域 `.caps = MTK_SCPD_IS_PWR_CON_ON | MTK_SCPD_BYPASS_INIT_ON`），
  即"内核启动时不要动我"。父域关系 `MFG1→MFG2→{MFG3..MFG6}→…`（同文件 `:597-607`）。
- 只有 **MFG1** 有 `bp_table`，其中一条是 **MFGRPC** 上的保护：
  `BUS_PROT_IGN(MFGRPC_TYPE, 0x0040, 0x0044, 0x0040, 0x0048, MT6895_MFGRPC_AXI_PROT_EN_MFG1)`
  （`[ML] mtk-scpsys-mt6895.c:471-472`；掩码 `BIT(16..19)`，`scpsys.h:373-374`）。
  这正是我们驱动在写的 `0x13F91044 |= 0x000F0000`（`[OUR]:185`）。
- MFGRPC 就是 **GPU↔EB 的 RPC 总线**：`[DTS] mt6895.dtsi:1136-1139`
  `mfgrpc: syscon@13f91000 { compatible = "mediatek,mt6895-gpu_eb_rpc_bus", "syscon"; }`，
  并且被 scpsys 当作 `mfgrpc` phandle 引用（同文件 `:1172`）。名字里就写着 `gpu_eb`。
- MFG0 不在 Linux 的域表里（`mtk-scpsys-mt6895.c` 从 MFG1 开始），`[VND] gpufreq_mt6895.c:3578`
  注释 `MFG0 is off in ATF` —— 它归 ATF/LK，GPUEB 住在里面。

### 4.2 时钟

- `core` = `CLK_TOP_MFG_SEL_0_CK`（父 MFGPLL / 231 MHz ref）；`stacks` = `CLK_TOP_MFG_SEL_1_CK`
  （父 MFGSCPLL / 218.4 MHz ref）；`coregroup` = `CLK_MFGCFG_BG3D`（`[DTS]:2757-2760`）。
- **BG3D 是 clock gate，"enable"= 写 `clr_ofs`(0x8)**：`[ML] clk-mt6895-mfgcfg.c:27-31` 定义
  `set_ofs=0x4, clr_ofs=0x8, sta_ofs=0x0`；`clk-gate.c:175-179` 的 `mtk_clk_gate_ops_setclr`
  `enable = mtk_cg_enable → mtk_cg_clr_bit`，`is_enabled` 判"bit 为 0"。
  → **我们 `[OUR]:2164` 写 `0x13FBF008 = BG3D` 是"开门"，方向是对的。**
- GPUEB 的 PLL 是 `gpuebpll_pll_ctrl@13fa0800`（`mediatek,mt6895-gpuebpll_pll_ctrl`），
  与 `mfgpll@13fa0000`（`[DTS]:1141`）、`mfgscpll@13fa0c00`（`[DTS]:1147`）并列。
  **注意**：mainline 的 `mt6895.dtsi` 里 **没有** gpuebpll 节点（`grep -c gpuebpll` = 0），
  它只在厂商 live dts 里（`dump/xaga_live.dts:4939` 节点、`:11192` 别名 `/soc/syscon@13fa0800`）。
  见 `[VND] work-notes-mcu-clock.md:201`。

### 4.3 OPP / 电压

- GPU 的 OPP 表在 DT（`[DTS]:616-` 起，950 MHz…），但 **EB 模式下 AP 的这张表不生效**：
  `mt6895_gpueb_commit()` 发的是**固件内部工作表索引**，固件自己查 AVS 后的率/压
  （`[ML] gpueb_power.c:161-166` 注释："Indexes sent with CMD_COMMIT are indexes into THESE tables,
  not into the DT OPP table"）。
- 两条 buck：VGPU（与 VCORE co-buck，经 DVFSRC）、**VSTACK = MT6368 BUCK2**；
  另有 VSRAM 由 SRAMRC 管（`[VND] gpufreq_mt6895.c:4875-4881`：`regulator_get_optional(dev,"_vsram")`，
  注释 "VSRAM is co-buck and controlled by SRAMRC"）。
- 固件里确实有这些动作的字符串：`[FW] strings`：`fail to Buck On/Off`、`fail to enable/disable VSTACK`、
  `fail to scale Vstack`、`acquire SPM_SEMA_M4 timeout`、`fail to release SPM_SEMA_M4`、
  `[E] (0x13F91048): 0x%x, (0x10001C6C): 0x%x, (0x10001C7C): 0x%x`（EB 自己诊断 MFGRPC/EMISYS 保护位）。

---

## 5. panthor 在 `MCU_CONTROL = AUTO` 之前做了什么（逐条）

来自 `[ML] drivers/gpu/drm/panthor/`，按真实调用顺序。`panthor_fw_init()` 本体在 `panthor_fw.c:1497-1554`。

**A. 设备 probe 阶段（一次性）** — `panthor_device.c:168-330`
1. `panthor_clk_init()`：`devm_clk_get(NULL)` 取 `core`，`devm_clk_get_optional("stacks")`，`devm_clk_get_optional("coregroup")`（`:34-50`）。
2. `panthor_init_power()`（`:228`）——**注意 GPU 节点没有 `power-domains`，这里是空操作**。
3. `devm_pm_runtime_enable` + `pm_runtime_resume_and_get`（`:241-247`）→ 触发 `panthor_device_resume()`。
4. `panthor_device_resume()`（`:518-538`）：**`clk_prepare_enable(core)` → `(stacks)` → `(coregroup)`**，
   三步都开；suspend 时逆序关（`:582-588`）。
5. **`mt6895_gpueb_power_on()`**（`:256-267`，`of_machine_is_compatible("mediatek,mt6895") && mt6895_gpueb_available()`）。
6. `panthor_devfreq_init()`（`:270`）→ 里面 `devm_regulator_get_optional("vstack")`，EB 模式下不 set_opp。
7. `panthor_hw_init()` / `panthor_pwr_init()` / `panthor_gpu_init()` / `panthor_gpu_coherency_init()` /
   `panthor_gem_shrinker_init()` / `panthor_mmu_init()`。

**B. `panthor_fw_init()` 内部** — `panthor_fw.c:1497-1554`
8. `fw->iomem = ptdev->iomem + MCU_CONTROL_BASE`（`:1506`）。
9. `platform_get_irq_byname("job")` + `panthor_request_job_irq()`（`:1512-1521`）。
10. **`panthor_hw_l2_power_on(ptdev)`**（`:1523`）→ arch v10 走 `panthor_gpu_l2_power_on()`。
11. `panthor_vm_create()` 建 MCU 的 4 GiB VM（`:1527-1530`，`CSF_MCU_SHARED_REGION_START/SIZE`）。
12. `panthor_fw_load()`（`:1537`）：`request_firmware("arm/mali/arch10.x/mali_csffw.bin")`，逐个 section 校验 magic/版本/长度并映射。
13. `panthor_vm_active(fw->vm)`（`:1541`）。
14. `panthor_fw_start()`（`:1545`）→ 内部：
    14a. `booted=false`；`panthor_job_irq_enable_events(~0)`；`panthor_job_irq_resume()`（`:1127-1129`）；
    14b. **`mt6895_gpueb_power_on()`（第二次）**（`:1131-1141`）；
    14c. **`gpu_write(fw->iomem, MCU_CONTROL, MCU_CONTROL_AUTO)`**（`:1143`）；
    14d. `wait_event_timeout(..., 1000 ms)`，超时读 `MCU_STATUS` 报 `Failed to boot MCU`（`:1145-1165`）。
15. `panthor_fw_init_ifaces()`、`panthor_fw_init_global_iface()`（`:1549-1553`）。

**中间有没有 GPUEB 相关调用？有，两处，都在 `MCU_CONTROL` 之前**：`panthor_device.c:262` 与
`panthor_fw.c:1134`。**除了"上电请求"以外没有别的 GPUEB 交互**（不改频、不读共享状态）。

---

## 6. 我们的驱动漏了什么（对照 §5）

`[OUR]` 里与 GPUEB 相关的只有两处**只读诊断**，且默认关闭：

```
[OUR]:261       #define MALI_GPUEB_PHYS 0x13c40000ULL     ← 地址是对的
[OUR]:4286      if (gMaliGpueb) gGpueb = ml_io_map(...)     ← mali_gpueb=1 才映射
[OUR]:2326      gpueb_watch()  只读 WDT_KICK / IRQ_RAW / WDT_CON / GPR[6]
[OUR]:4634/4933 两处调用 gpueb_watch
```

`[LOG]` trial 692 的两行：`mali: gpueb before the address spaces: not mapped` /
`mali: gpueb after the failed start: not mapped` —— **这次启动连映射都没有，更没有任何 IPI**。
`grep -c ipi [OUR]` = **0**。

逐条对照 §5：

| panthor 有 | 我们有 | 差异后果 |
|---|---|---|
| ①`clk_prepare_enable(core/stacks/coregroup)` | `clock_26m()` 只写 topckgen + `MFGCBG->0x8`（`[OUR]:2164`），**并且在 `mali_cold` 分支里**（`:4330-4408` 整段只在 GPU 冷时执行） | 方向对，但"非冷启动"路径完全不碰 BG3D；`stacks` 只 ungates 不设频（见 §8 冲突 4） |
| ②`pm_runtime` 强制 resume | 无（XNU 无 runtime PM，等价于常开） | 无差异 |
| ③`mt6895_gpueb_power_on()` ×2 | **完全没有** | **核心缺失**，见下 |
| ④`devm_regulator_get_optional("vstack")` + 按率联动 | `mali_vstack_rail()`（`[OUR]:3083`）只**点一下** VBUCK2 enable（SPMI `0x240 bit2`），不按 OPP 设压、不与 stacks 频率联动 | **比想象的更糟**：`[LOG]:33.918181` `stack rail: v buck2 status 0x6a (enabled 0), vsel 0x58` —— **写完之后读回仍然是 enabled 0**。这条 rail 从头到尾没开过 |
| ⑤`panthor_hw_l2_power_on()` | 有 `gpu_l2_power_on()`（`[OUR]:3153`）；`[LOG]:2926` 显示 `L2 up: ready 0x1` | 已达成，不是瓶颈 |
| ⑥`panthor_fw_load()` 全 section 映射 | 有，`[LOG]:34.10x-34.16x` 显示 10 个 section、9 个页表、coverage 完整 | 已达成 |
| ⑦EB 模式不碰 `dev_pm_opp_set_opp` | 我们本来就没 OPP 框架 | 无差异 |

**最刺眼的一条**：`[LOG]:33.960797` `shader 0x130013 tiler 0x1 l2 0x1 stack 0x33`
（PRESENT 都在），但 `[LOG]:43.530895` `shader ready 0 tiler ready 0 l2 ready 0x1`。
**L2 起来了，shader/tiler 没起来**——这正是 `[DTS]:2727-2732` 注释所描述的状态：

> Without MFG2-12 the shader cores never become ready (shader_ready stays 0) and the
> MCU firmware hangs before writing the shared interface version.

而 `[LOG]:33.941709` 里 `after pdc: mfg5 0x100d mfg6 0x100d mfg10 0x100d mfg12 0x100d`
——`0x100d` 的含义是 **"PWR_ON 已置、ACK(bits31:30) 仍为 0"**，即请求被接收、没有完成。
`[LOG]:33.940184` 的 PDC 回读 `sc0 0x2000001 sc9 0 st6 0` 说明我们写的位**有的被硬件清掉了**
（sc9/st6），也就是那个控制器不认 AP 这个主人。

---

## 7. 与项目现有结论的冲突（本次材料最有价值的部分）

> 逐条：**项目说 X / 材料说 Y / 证据在哪 / 该怎么处理。**

### 冲突 1 「MFG7..12 不上电是设计如此（PDCv2 会管）」

- **项目说**：12 个域不必都由软件点亮，PDCv2 让 GPU 自己管；因此 7/12 域 ack 是可接受状态。
- **材料说**：`[DTS]:2727-2732`（同一份被引用的 mainline dtsi）明确写：
  "Downstream attaches all twelve domains to the GPU node and powers them through PM runtime;
  the PDCv2 registers then let the GPU control the finer shader/stack domains.
  **Without MFG2-12 the shader cores never become ready (shader_ready stays 0) and the MCU
  firmware hangs before writing the shared interface version.**"
- **判定**：**部分推翻**。"PDCv2 管"成立，"所以不用真的上电"不成立。判据是 `SHADER_READY`：
  `[LOG]:43.530895` 它是 **0**，而 `[LOG]:33.960797` 的 `PRESENT` 是 `0x130013`。
  PRESENT≠READY 这件事，我们此前当成"域没 ack"，但它其实是"域没上电"的直接读数。
- **行动含义**：不应再把 `0x100d` 当作可接受。它是 `PWR_ON=1 & ACK=0`，是"卡住"而不是"设计"。

### 冲突 2 「GPUEB 经 SPM 供电 —— 已排除」

- **项目说**：GPUEB 不负责给 GPU 上电（BkaNeko 的固件反汇编：三份固件对 SPM 只有一处**读**
  `0x1C001EBC`，从不写 MTCMOS），所以这条排除。
- **材料说**：**这条排除本身是对的，但它推不出"GPUEB 不参与"**。
  ① `[ML] panthor_fw.c:1131-1141` 在 `MCU_CONTROL=AUTO` 前**必须**调 `mt6895_gpueb_power_on()`；
  ② `[ML] gpueb_power.c:185-189` 注释：EB 模式下 downstream 只发 `CMD_POWER_CONTROL`，
     固件内部恢复自己的 boot OPP；
  ③ `[FW] strings` 有 `fail to Buck On`、`fail to enable VSTACK`、`acquire SPM_SEMA_M4 timeout`,
     说明固件确实在做 buck/VSTACK/SPM 信号量这一层（只是 MTCMOS 的 ACK 由 PDC 硬件完成）。
- **判定**：**结论方向对、推论错**。GPUEB 不是"供电者"，它是**电源状态机的 owner/仲裁者**：
  它不亲手写 MTCMOS，但它决定 PDC 何时被允许工作、buck/VSTACK/AVS 用哪一档、以及 GPU 的
  RPC 通道（MFGRPC）归谁。少了 `CMD_POWER_CONTROL`，这套状态机就停在 power-off，
  GPU 自己发起的域请求没有仲裁者应答 → `0x100d`。
- **行动含义**：把"GPUEB 经 SPM 供电"这条从"已排除"改成"**已澄清**：不写 MTCMOS，但必须发
  IPI 请求"。这不推翻它，但它不再是一条可以忽略的排除项。

### 冲突 3 「`0x13C40000` 是禁读地址，一碰挂总线」

- **项目说**：三个地址禁读，`0x13c40000` 是其中之一；碰了就掉 USB。
- **材料说**：它是 **GPUEB 的主寄存器块**（`[DTS]:2650,2703-2706`），不是野地址。但——**我们至今
  没有找到"读它会挂"的机制证据**，也没找到 mainline 里给它开时钟门的代码：
  `[ML] gpueb` 目录下 10 个 .c 文件 **grep `clk_get`/`clk_enable` 全部 0 命中**；
  `[DTS]` 的 gpueb 节点**没有 `clocks` 属性**（我逐字检查过该节点，`clocks`/`power-domains` 都不存在）。
  而 `[VND] gpueb_init.c` 的 probe 也只有 IPI/mbox/logger/timesync，无 clk 操作。
- **判定**：**地址归属推翻，挂死原因未找到**。合理解释（**未验证**）：GPUEB 的时钟由 LK 开、
  由 ATF 在某个时点收；XNU 里没有 CCF，GPUEB 的 clock gate 若在 LK 跳转前被关，读它就会
  AXI 挂死。**这条必须实测**（见 §8 的 V0）。
- **行动含义**：不要再用"禁读"当结论；改成"未确认为何挂，先用最小读探测"。

### 冲突 4 「MFGSCPLL 不需要为 MCU 补」

- **项目说**：MFGSCPLL 只是 shader stack 的频率源，Miss 它不阻断 MCU。
- **材料说**：`[DTS]:2741-2760` 把 `stacks` 列为 GPU 节点**三个必需时钟之一**，panthor 无条件
  `clk_prepare_enable(stacks)`；但**没有任何地方给 stacks 设频率**（`panthor_devfreq` 只在
  devfreq target 里改）。commit message 也只说"scale it along with the core clock"。
- **判定**：**不冲突，但要精确化**：`stacks` 时钟**必须 enable**（gate 要开），**不必**为 MCU 设定
  某个频率。我们 `[OUR]:2203-2233` 的 `mfg_sel_1` 处理是"可选实验"（`mali_scclk`/`mali_scupd`），
  这没错，但它不该被当成"MCU 前置条件"，也不该被完全跳过——**至少要把门开开**。

### 冲突 5 「`coregroup`（BG3D）是隐藏的第三个必需时钟，我们可能没开」

- **项目（父代理）怀疑**：我们没开 BG3D。
- **材料说**：`[OUR]:2164` `reg_wr(gMfgcfg, MALI_MFGCFG_CLR, MALI_MFGCFG_BG3D)` 就是"开门"
  （setclr 语义：clr_ofs=enable，`[ML] clk-gate.c:175-179` + `clk-mt6895-mfgcfg.c:27-31`），
  而且 `[LOG]:33.9x` 有 `clock: cfg4 ... mfgcfg ...` 的打印，说明这次跑了。
  **但是**：`clock_26m()` 只在 `mali_cold` 分支里（`[OUR]:4330-4408`）。
- **判定**：**怀疑不成立（开是开了），但存在真实漏洞**：非冷启动路径（loader 已上电）**完全不执行**
  `clock_26m()`，因此 BG3D 在那些 run 里保持 loader 留下的状态；而 `[DTS-xaga]:48-57` 的注释说
  `clk_ignore_unused` 被移除后，`clk_disable_unused` 会**关掉**没人 claim 的 `mfgcfg_bg3d`
  （那是 Linux 侧；XNU 侧无此机制，但 loader 的行为未知）。

### 冲突 6 「PWR_STATUS 0xF3C 应读 0x3FFE」

- **项目说**：`0xF3C` 期望 `0x3FFE`（MFG0-12 全 up）。
- **材料说**：`[VND] gpufreq_mt6895.c:3578` 只在**关电**后校验 `MFG_1_12_PWR_MASK`，且注释
  `MFG0 is off in ATF`；正常工作态 Android 的 `0xF3C` 实测是 `0x3`
  （`fw/../research` 引用的 BkaNeko E0 快照，`docs/GPUEB.zh-CN.md` §14.2）。
  `[LOG]:33.943256` 我们读到 `0xF3C = 0x4`。
- **判定**：**"期望 0x3FFE"这个判据是错的，而且位解码本身也有问题。**
  `MFG_1_12_PWR_MASK = 0x3FFC` 只有 12 位（bit 2..13），要覆盖 MFG1..MFG12 就必须是
  **`MFGn = bit(n+1)`**（MFG0=bit1，MFG1=bit2…MFG12=bit13），此时 `MFG_0_1_PWR_MASK = 0x6`
  （bit1,2）**恰好自洽**。而 `[OUR]:1772` 的注释写的是 `bit (n+2) is MFGn` —— **偏了一位**，
  照它算 MFG12 会落到 bit14，超出掩码。
  按正确解码：`[LOG]:33.943256` 的 `0xF3C = 0x4` = **只有 MFG1 在位**，MFG2-12 全 0；
  Android 的 `0x3` = **MFG0 + bit0**，MFG1-12 全 0（与 BkaNeko E0 快照一致）。
  但这就与 `[LOG]` 同一时刻直接读到的 `0x1c001eb8`(MFG0 PWR_CON) = `0xc000000d`（ACK=11）
  **矛盾**——MFG0 明明上了电，`0xF3C` 的 bit1 却是 0。
  → 说明 `0xF3C` 很可能**不是** SPM 的那份状态，而是 gpufreq 另一份拷贝（`[OUR]:1772` 自己也说
  "0xF34/0xF38 与 0xF3C/0xF40 是两拨人各写各的"）。
- **行动含义**：**放弃 `0xF3C` 当判据**。改用三样东西：①`0x1c001eb8+i*4` 的 PWR_CON 逐域解码
  （ACK=bits31:30，健康模板 `0xC000224D`）；②`0xF34/0xF38` 的 `sta_mask`；③**`SHADER_READY`**
  （最直接、语义最不含糊，`[LOG]` 已证它是 0）。

---

## 8. 建议的验证方案（单变量，一次刷机验一条）

> 原则：每条只改一个变量；每条都有"是/否"的硬判据；不写 SPM、不碰真机 shell（由主代理执行）。

### V0（最高优先级，成本最低）：GPUEB 是否活着 + 能否收 IPI
**改什么**：`mali_gpueb=1`（已有开关，`[OUR]:4286/5100`），只做**只读**：
`gpueb_watch()` 读 `0x2061C`(WDT_KICK) 两次、`0x2033C`(IRQ_RAW)、`0x20618`(WDT_CON)、
`0x22078`(mbox0_recv)、`0x1FD34`(GPR[6])。
**判据**：
- `kick` 两次不同 → GPUEB 在跑（LK 启动的，符合预期）；
- 两次相同 → 它被停住/时钟被关，那么"读它挂死"也有了解释；
- 无论哪种，**这一步就能回答"禁读地址"之谜**，且不写任何寄存器。
**风险**：若真会挂总线，会丢一次 boot。建议先只读 `0x2061C` 一个字（不要连读 5 个），
把风险压到最小。

### V1（决定性）：发 `CMD_INIT_SHARED_MEM` + `CMD_POWER_CONTROL`
**改什么**：在 `start_mcu()` 里、`mwr(kMcuControl, kMcuControlAuto)`（`[OUR]:3858`）**之前**，
插入一次手写 IPI：
1. 把 `0x13C5FD80..0x13C5FFFF` 的 mbox 区清零（一个 mbox，160 slots × 4 B）；
2. 槽位 = send 表累加：`IPI_ID_GPUFREQ` 是 index 1，前面只有 `FAST_DVFS_EVENT`(4 words)，
   故 **slot 1，offset 4 words**（`[ML] gpueb_ipi.c:250-273` 的算法；`send_table` 第 2 项是 `<1 0 8>`）；
3. 写 8 words 到 `0x13C5FD80 + 4*4`：`{cmd_id=0, target=0, status_base=0x7C600000,
   status_size=0x400, debug_base=0x7C600400, debug_size=0x1FC00, 0, 0}`；
4. 写 `0x13C62004 = (1<<1)` 触发；轮询 `0x13C62078` bit1 或 recv 区返回；
5. 再发 `{cmd_id=6, target=0, power_state=1}`，同样触发、轮询；
6. 然后才写 `MCU_CONTROL=AUTO`。
**判据（三选一即可定论）**：
- `SHADER_READY(0x13000208)` 从 0 变 `0x130013` → **机制坐实**，MCU 应能起来；
- IPI 返回 `return_value < 0` → GPUEB 回了拒绝码，说明协议/内存地址不对，但**证明它在听**；
- 触发后 `mbox0_recv` 永远不动 → GPUEB 没在听（时钟/中断/固件没起），转 V0 的结论。

> ⚠️ 顺序注意：panthor 是 `power_on()` 之后再 `clk_prepare_enable`，我们反过来。按 §5 的顺序，
> `CMD_POWER_CONTROL` 应当在**时钟已经开着**之后发（`[ML] panthor_device.c:528-538` 在 `:262` 之前）。
> 我们 `clock_26m()` 在 `[OUR]:4408`，`start_mcu()` 在其后，位置天然是对的。

### V2（若 V1 的 IPI 能通、但 SHADER_READY 仍 0）：先修 VSTACK，再谈联动
**先看清现状**：`[LOG]:33.918181` `stack rail: v buck2 status 0x6a (enabled 0), vsel 0x58`。
`0x6A` = `0110_1010`，bit2=0 且**写 `0x241`(=EN_SET) 也没让它变 1**。三个可能：
①我们写的 SPMI 地址不对（MT6368 在 SPMI bus 5，`0x240` 是 VBUCK2 的 EN 还是别的 buck 的）；
②EN_SET 需要先解 `VBUCK2` 的 `SHDN`/`FPWM` 之类的前置；③这颗 buck 由 SRAMRC 或 EB 独占，
AP 写被丢。**这一步不查清楚，V1 就算通了也可能因为 stacks 没电压而白跑。**
**改什么（仍单变量）**：只做**读**——把 `0x0240`/`0x0241`/`0x024E`/`0x024F` 以及相邻
`0x0242..0x024D` 全 dump，再读 `0x024C`(MODE) 与 `0x0248`(?)，看这颗 buck 现在到底什么状态；
同时读 MT6368 的 `0x0000`(CHIP_ID) 确认 bus/地址对齐没写错。
**判据**：能读到一个可解释的 buck 状态（例如 `status` 的 bit0..3 与 `[VND] gpufreq` 写的
`vsel` 值对得上）→ 地址对了，问题在"谁允许我们写"；全 `0xFF` 或全 0 → 地址/总线不对。

### V2b（在 V2 读通之后）：按 OPP 设压 + 联动
把 `mali_vstack_rail()` 扩成"按 `stacks` 目标频率设压"，照 commit 的 `xaga_stack_table[]`
（patch 行 102-144）：`368 MHz→568750 µV`、`852 MHz→743750 µV`，**升频先升压、降频后降压**。
**判据**：`SHADER_READY` 是否抬头；MFG5/6/10/12 的 PWR_CON 是否从 `0x100d` 变 `0xC000224D`。

### V3（诊断，不作为修复）：读 MFGRPC 状态字
`0x13F91048`（sta_ofs）与 `0x13F9103C`（en_ofs）。固件自己也打印这两个（`[FW] strings`:
`[E] (0x13F91048): 0x%x`）。若 `sta` 的 bit16..19 恒为 1，说明 MFG1 的 MFGRPC 保护从未真正解除，
那 GPU 的域请求根本出不去 —— 这条与 V1 正交，可并行做。

---

## 9. 未确认的地方（明确标注，不要当结论用）

1. **`0x13C40000` 为什么会挂总线** —— 未找到机制证据。mainline 与厂商的 gpueb 驱动都**没有**
   任何 `clk_get/clk_enable`；dts 节点也没有 `clocks`。是"LK 开了、某处关了"还是"AXI 保护没解"，
   未知。V0 是唯一能回答的实验。
2. **`0x1C001F3C` 的身份与位定义**。`[VND] gpufreq_mt6895.h:72-76` 给的
   `PWR_STATUS_OFS=0xF3C`、`MFG_0_1_PWR_MASK=0x6`、`MFG_1_12_PWR_MASK=0x3FFC` 三条里，
   掩码本身要求 `MFGn = bit(n+1)`，而 `[OUR]:1772` 的注释写作 `bit(n+2)`（偏一位）。
   更麻烦的是实测：同一时刻 `0xF3C=0x4`（按上式 = 仅 MFG1 在位）却与 `0x1c001eb8`(MFG0)
   `ACK=11` 冲突。**`0xF3C` 到底是不是 SPM 状态、还是 gpufreq 自己的影子寄存器，未确认。**
3. **GPUEB 到底有没有写 PDC**。`[FW] 0x13c4a2xx` 那段确实在对 `base(0x13fbf000)+0x400/0x418/…/0x4DC`
   做 `lr.w → ori 1 → sw`（还有 0x404/0x41C/… 的 bit31），与厂商 `__gpufreq_pdc_control(POWER_ON)`
   逐条对应。但这只能证明"固件里有这段代码"，**不能证明 `CMD_POWER_CONTROL` 会走到它**（需要
   `power_count==1` 之类的门）。我标为**强推断**。
4. **`CMD_INIT_SHARED_MEM` 的 `status_size` / `debug_size` 精确值**。mainline 写 `0x400` 和
   `size-0x400`；厂商 `gpufreq_v2.c:1930-1960` 用的是 `GPUFREQ_SHARED_STATUS_SIZE`（结构体大小）。
   两者可能不等。V1 先用 mainline 的值，若返回负码再换。
5. **IPI 槽位偏移**。我按 `gpueb_ipi.c:250-273` 推出 `IPI_ID_GPUFREQ` 在 `offset=4 words`，
   但 `mtk-mbox.h:18` 的 `MBOX_SLOT_SIZE=4` 是**字节**，而 `slot_ofs = slot * MBOX_SLOT_SIZE`，
   `config` 里 `slot_size=<4>` 也是字节 —— 单位链条我只能核到"slot 以 4 字节为单位"。
   实际 send 表里 `msg_size` 已经是 word 数，两处相乘/相加的**具体字节地址需要上机核对一次**
   （先读一段 mbox 看固件留下的痕迹）。
6. **`shader_ready` 与 `MFG2-12` 的因果关系强度**。dtsi 注释是**作者的结论**，不是硬件手册。
   但它与我们 `[LOG]:43.530895` 的 `shader ready 0` 完全吻合，所以我认为值得当作首要假设。
7. **MFG0 是否在 XNU 期间被 ATF 关掉**。`[VND] gpufreq_mt6895.c:3578` 只说"ATF 里 MFG0 是关的"，
   没说是哪个阶段。若 GPUEB 住在 MFG0 而 MFG0 被关，那"读 0x13C40000 挂死"就解释得通。
   **这条与 §9.1 是同一个问题的两面。**

---

## 10. 一页速查

```
GPUEB   = RV32 tinysys 协处理器，MFG0 域，寄存器 0x13C40000(+0x22000)
mbox    = 0x13C5FD80 (数据) / 0x13C62000(set 0x04, clr 0x74, send 0x00, recv 0x78)
IRQ     = GIC SPI 516
共享内存 = 0x7C600000 + 2 MiB（MEM_ID_GPUFREQ 前 4 KiB）
固件     = 分区 gpueb_a / gpueb_b，由 LK 的 app_load_gpueb() 装并启动
IPI     = 8 通道；IPI_ID_GPUFREQ=1；命令 0=INIT_SHARED_MEM, 6=POWER_CONTROL, 7=COMMIT
panthor 在 MCU=AUTO 前必做：clk(core,stacks,coregroup) → gpueb POWER_ON → L2 power on
                            → VM → fw load → vm_active → gpueb POWER_ON(again) → AUTO
我们在哪：从来没发过任何 IPI；gpueb 连 map 都没做（mali_gpueb 未给）
症状对应：shader_ready=0  ⇒  dtsi:2727-2732 描述的正是这个 ⇒ 固件不写版本字
```
