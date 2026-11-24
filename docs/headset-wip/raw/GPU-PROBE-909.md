# 只读探针：12 个 GPU 功能域一个都没上电

2026-11-25 凌晨。`boot-909b.img`。

## 探针输出

```
mali: probe MFG0_PWR_CON 0xc000224d (pwr ack 3 sram pdn ack 0)
      MFG1_PWR_CON 0x1112 (pwr ack 0) EMI1_PWR_CON 0xc000024d
mali: probe MFGPLL+0x808 0x15445 (gpueb pll enable 1) +0x840 0x8000
      con0 0x15444 con1 0x832113b1 con3 0x2
mali: probe PWR_STATUS 0xF34 0xde0001fd 0xF38 0xde0001fc 0xF3C 0x3 (MFG1_12 0)
mali: probe done; gGpu is still 0, so quiesce() will not touch the block
```

## 数据含义

| 寄存器 | 值 | 含义 |
|---|---|---|
| `MFG0_PWR_CON` (0xEB8) | `0xc000224d` | pwr ack = **3**（bits 31:30 都置）→ **GPU 顶层域已上电** |
| `MFG1_PWR_CON` (0xEBC) | `0x1112` | pwr ack = **0** → MPU 域没上电 |
| `EMI1_PWR_CON` (0xEB0) | `0xc000024d` | pwr ack = 3 |
| `PWR_STATUS 0xF3C` | `0x3` | `& 0x3ffc` = **0** → **MFG1–MFG12 全都没上电** |
| `MFGPLL+0x808` | `0x15445` | bit0 = 1 → **GPUEB PLL 已使能** |

**结论：上层域有电、GPUEB 的 PLL 使能，但 12 个功能域（MPU、L2、八个着色器核）全断电。**

这解释了之前的矛盾现象：
- `GPU_ID` 有时读得出来（上层寄存器接口可达）
- 但 MCU 不执行（核没电）

## 验收

| 项目 | 要求 | 实测 |
|---|---|---|
| 探针日志落地 | ✅ | 4 行完整 |
| RETURN 时跳过 GPU MMIO | ✅ | `gGpu` 保持 0 |
| 回 Android 耗时 | 接近 27 秒 | **37 秒** |

## 实现时的两个坑

### ① `ml_io_map_with_prot` 声明不在包含路径

`osfmk/arm/machine_routines.h` 不在 `bsd/dev` 的包含路径里。按文件里已有的做法自己声明：

```c
extern vm_offset_t ml_io_map_with_prot(vm_offset_t phys_addr, vm_size_t size, int prot);
#ifndef VM_PROT_READ
#define VM_PROT_READ    0x01
#endif
```

### ② `mali_probe=1` 单独不够（这个更重要）

`hakios_mali610_init()` 在 `mali=0` 时直接 return，探针根本进不去：

```c
if (!PE_parse_boot_argn("mali", &which, sizeof(which)) || which == 0) {
    return;      // ← 探针在这里就被挡住了
}
```

改成 `mali_probe=1` 时也启动 `mali_thread`，但不设 `gMaliLevel`，所以只走探针分支：

```c
if (PE_parse_boot_argn("mali_probe", &probe, sizeof(probe)) && probe != 0) {
    (void)kernel_thread_start(mali_thread, NULL, &thread);
    thread_deallocate(thread);
}
return;
```

## 关键设计点：`gGpu` 必须保持 0

Sol 特别指出过这一点：

> 907 payload 的 quiesce() 只要看到 gGpu 非零就会向 GPU+0x700 写 DISABLE，
> 所以只在 bus_protect_release() 前返回还不够。

所以探针用局部变量映射，**不给 `gGpu` 赋值**，`quiesce()` 看到 0 就跳过所有 MMIO。

这也是为什么探针模式退出只要 37 秒，而 `mali=6` 要 5 分钟以上。

## 寄存器命名修正

上一轮我写错过，这次按厂商 `mt6895_spm_reg.h` 改名：

| 偏移 | 正确名字 | 我之前叫的 |
|---|---|---|
| `0xEB8` | `MFG0_PWR_CON` | MFG0_STATUS ✓ |
| `0xEBC` | `MFG1_PWR_CON` | MFG0_CTRL ✗ |
| `0xEB0` | `EMI1_PWR_CON` | MFG1_STATUS ✗ |

**PWR ACK 是 bits 31:30。bit 12 是 SRAM 掉电 ACK，不是电源 ACK。**

## 下一步

现在的问题是：**12 个域从来没上电过，不是"关不掉"，是"上不去"。**

两个方向：

**A. 让探针再往前走一步**，只给 MFG1（MPU）上电，看 `GPU_ID` 能不能读出来。

**B. 先查为什么 12 个域上不去**——`MFG1 pwr ack = 0` 说明写请求没生效。

已把这个问题交给 Sol 判断。

## 环境

- 镜像：`boot-909b.img`（`mali=0 mali_probe=1`）
- 回退：`boot-908.img`（`mali=0`，27 秒退出）、`boot-904.img`
- 探针代码：`hakios.mali610.c` 的 `mali_probe_read_only()`
