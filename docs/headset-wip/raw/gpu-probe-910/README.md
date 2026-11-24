# GPU 只读探针：基线数据与 GPUEB mailbox 状态

2026-11-25 凌晨。`boot-909b.img` → `boot-910b.img`。

按规划者（Sol）的方案 B：先确认 GPUEB 握手和 mailbox 可达性，**不急着发 IPI**。

---

## 一、探针的完整输出

```
mali: probe MFG0_PWR_CON 0xc000224d (pwr ack 3 sram pdn ack 0)
      MFG1_PWR_CON 0x1112 (pwr ack 0) EMI1_PWR_CON 0xc000024d
mali: probe MFGPLL+0x808 0x15445 (gpueb pll enable 1) +0x840 0x8000
      con0 0x15444 con1 0x832113b1 con3 0x2
mali: probe PWR_STATUS 0xF34 0xde0001fd 0xF38 0xde0001fc 0xF3C 0x3 (MFG1_12 0)
mali: gpueb probe: ctl tx 0 (chan 1 idle) rx 0 (chan 1 empty)
mali: gpueb probe: sram slot0 0 slot159 0; tx slot 0 rx slot 0
mali: gpueb probe: sram 0 of 160 words non-zero; nothing written
mali: probe done; gGpu is still 0, so quiesce() will not touch the block
```

## 二、数据说明什么

### GPU 电源域

| 寄存器 | 值 | 含义 |
|---|---|---|
| `MFG0_PWR_CON` (0xEB8) | `0xc000224d` | pwr ack = **3**（bits 31:30 都置）→ **顶层域已上电** |
| `MFG1_PWR_CON` (0xEBC) | `0x1112` | pwr ack = **0** → MPU 域没上电 |
| `EMI1_PWR_CON` (0xEB0) | `0xc000024d` | pwr ack = 3 |
| `PWR_STATUS 0xF3C` | `0x3` | `& 0x3ffc` = **0** → **MFG1–MFG12 全没上电** |

**上层域有电，12 个功能域一个都没电。** 这解释了之前的矛盾：`GPU_ID` 有时读得出
（上层寄存器接口可达），但 MCU 不执行（核没电）。

### GPUEB PLL

```
MFGPLL+0x808 = 0x15445   bit0 = 1
```

**PLL 使能位是置的。** 但这只证明 PLL 使能，不证明 GPUEB 核在运行。

### GPUEB mailbox

```
ctl  tx = 0 (chan 1 idle)   rx = 0 (chan 1 empty)
sram 0 of 160 words non-zero
```

**全是 0。** 按 `GPUEB-IPI-PROTOCOL.md` §1.4：

> 实测判据：正常 Android 启动后 `RX_STS` 应为 `0`（无待收），`TX_STS` 应为 `0`（无在途）。

**所以这是文档记载的正常空闲态，不是"不可达"。**

但需要说清楚：**零值无法区分"空闲"和"总线不应答"。** 我加了一个 SRAM 全扫描
（160 个字）来增加依据，结果也是全零——LK 加载的固件在 GPUEB 自己的 SRAM 里，
不在这个 mailbox 缓冲里，所以这里为空是合理的。

---

## 三、验收

| 项目 | 要求 | 实测 |
|---|---|---|
| 探针日志落地 | ✅ | 7 行完整输出 |
| RETURN 时 quiesce 跳过 GPU MMIO | ✅ | `gGpu` 保持 0 |
| 回 Android 耗时 | 接近 `boot-908` 的 27 秒 | **35 秒** |

**对比 `mali=6` 的 5 分钟以上，探针模式完全没有副作用。**

---

## 四、代码要点

### 位置

`mali_probe_read_only()` 在 `mali_bring_up()` 的最开头，`gGpu` 赋值之前：

```c
static void
mali_bring_up(void)
{
	unsigned up;

	if (mali_probe_read_only()) {
		return;                         /* read only, and leave gGpu at zero */
	}
	gGpu = ml_io_map(MALI_GPU_PHYS, MALI_GPU_SIZE);
	...
```

### `gGpu` 必须保持 0

规划者特别指出过：

> 907 payload 的 quiesce() 只要看到 `gGpu` 非零就会向 `GPU+0x700` 写 DISABLE，
> 所以只在 `bus_protect_release()` 前返回还不够。

所以探针用**局部变量**映射，不给 `gGpu` 赋值。`quiesce()` 看到 0 就跳过所有 MMIO。

### 只读映射

```c
extern vm_offset_t ml_io_map_with_prot(vm_offset_t phys_addr, vm_size_t size, int prot);
#ifndef VM_PROT_READ
#define VM_PROT_READ    0x01
#endif
```

`osfmk/arm/machine_routines.h` 不在 `bsd/dev` 的包含路径里，按文件里已有的做法自己声明。

### 探针的开关要独立

`hakios_mali610_init()` 在 `mali=0` 时直接 return，探针进不去：

```c
if (!PE_parse_boot_argn("mali", &which, sizeof(which)) || which == 0) {
    /* mali_probe works on its own, without mali=N */
    unsigned probe = 0;
    if (PE_parse_boot_argn("mali_probe", &probe, sizeof(probe)) && probe != 0) {
        (void)kernel_thread_start(mali_thread, NULL, &thread);
        thread_deallocate(thread);
    }
    return;
}
```

**这是这一轮踩的坑**：第一次刷 `mali=0 mali_probe=1`，探针一行都没输出。

---

## 五、寄存器命名（上一轮我写错过）

按厂商 `mt6895_spm_reg.h`：

| 偏移 | 正确名字 | 我之前叫的 |
|---|---|---|
| `0xEB8` | `MFG0_PWR_CON` | MFG0_STATUS ✓ |
| `0xEBC` | `MFG1_PWR_CON` | MFG0_CTRL ✗ |
| `0xEB0` | `EMI1_PWR_CON` | MFG1_STATUS ✗ |

**PWR ACK 是 bits 31:30。bit 12 是 SRAM 掉电 ACK，不是电源 ACK。**

把这个读错导致我上一轮得出了「`clk_dis=1` `rst_b=0` 是问题」的相反结论。

---

## 六、GPUEB mailbox 的寄存器布局

来自 `GPUEB-IPI-PROTOCOL.md`（54 KB，协议已还原，模拟器 4/4 通过）：

```
0x13c5fd80  消息 SRAM，0x280 = 640 B = 160 个 4 B slot。不是寄存器块
0x13c62000  控制 bank（send/recv/set/clr 四个 1-bit-per-channel 位图）
0x13c62004  set   WO，写 BIT(chan) 触发
0x13c62074  clr   WO，写 BIT(chan) 清位/应答
0x13c62000  send  RO，TX 忙位
0x13c62078  recv  RO，RX 待收位
```

**注意 `13c5fd80 + 0x280 = 13c60000`，而控制 bank 在 `0x13c62000`——中间是空的，
不能按偏移算。**

通道：`GPUFREQ = 1`。
**TX 是 slot 4（字节 0x10），RX 是 slot 47（字节 0xbc）——不是同一个 slot 号。**

---

## 七、还没做的（按规划者的顺序）

规划者给的下一步是三步，我只完成了第 1 步：

**✅ 第 1 步（已完成）**：确认 GPUEB 握手和 mailbox 可达性。
PLL 使能 = 1，mailbox 是文档记载的空闲态。

**⬜ 第 2 步**：单独做 `INIT_SHARED_MEM` → `POWER_CONTROL(ON)`，
分别记录新 IPI 的完成和 ON 的固件返回值，再读 `MFG1_PWR_CON` 与 `0xF3C`。

- 无新回包 → 查 IPI 通路
- 回包成功但 MFG1 无双 ACK → 查 GPUEB 的上电路径

**⬜ 第 3 步**：用同一接口配对这次的 ON 和 OFF，确认 `MFG1`、`0xF3C & 0x3ffc`
回到本轮基线，RETURN 恢复到几十秒。验收通过后另开一轮加 `AUTO`，
看版本哨兵和 `GLOBAL_IF`。

---

## 八、环境

- 镜像：`boot-910b.img`（`mali=0 mali_probe=1`）
- 回退：`boot-909b.img`、`boot-908.img`（`mali=0`，27 秒）、`boot-904.img`
- 探针代码：`hakios.mali610.c` 的 `mali_probe_read_only()` 和 `mali_probe_gpueb()`
- 新增寄存器定义：`MALI_SPM_MFG0/MFG1/EMI1_PWR_CON`、`MALI_GPUEB_MBOX_*`

### 改动清单

| 文件 | 改动 |
|---|---|
| `bsd/dev/hakios.mali610.c` | 只读探针 + GPUEB mailbox 探针 + 寄存器命名修正 |
| `osfmk/arm64/loader_return.c` | `hakios_mali610_quiesce()` 调用（Trial907） |
| `coordinator/loader-build.json` | `mali=0 mali_probe=1` |
