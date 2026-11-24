# GPU 与 USB 的实测结果

红米 Note 11T Pro（xaga），MT6895Z / 天玑 8100，Mali-G610 MP6。

最后更新：2026-11-25，镜像 `boot-910b.img`。

---

## 一、GPU：硬件识别完成，功能域未上电

### 芯片身份（实测，热路径）

```
GPU_ID        0xa8670004          Mali-G610
CSF           0x40a0412           Command Stream Frontend
L2 features   0x7130306
Shader        0x130013
MMU           0x2830             va 48 bits, pa 40 bits, address spaces 0xff
tiler 0x1   l2 0x1   stack 0x33   thread_max 0x800
```

### 固件

```
258048 bytes, header 0.1, 10 个 section
reset vector: sp 0x2040000  pc 0x10018ed
entry words at VA 0x1000000: 0x10019b1 0x10019b1 0xd00cf8df 0xf832f002
页表: 10 sections, 9 page tables, every section fully mapped
AS0: root 0x50000000  transcfg 0x42000006
```

固件完整装入并可映射，入口是真实的 ARM64 指令。

### 电源域（`boot-910b` 只读探针）

```
MFG0_PWR_CON   0xc000224d    pwr ack 3 (bits 31:30)   ← 顶层域已上电
MFG1_PWR_CON   0x1112        pwr ack 0                ← MPU 未上电
EMI1_PWR_CON   0xc000024d    pwr ack 3
PWR_STATUS 0xF3C  0x3        & 0x3ffc = 0             ← MFG1-12 全部未上电
```

**结论：GPU 顶层域有电，12 个功能域（MPU、L2、八个着色器核）一个都没上电。**

### GPUEB

```
MFGPLL+0x808   0x15445    bit0 = 1    GPUEB PLL 已使能
MFGPLL+0x840   0x8000     gpueb_pll_ctrl
MFGPLL con0 0x15444  con1 0x832113b1  con3 0x2

mbox ctl  tx 0 (idle)  rx 0 (empty)
mbox sram 0 of 160 words non-zero
```

`GPUEB-IPI-PROTOCOL.md` §1.4 记载：正常状态下 `TX_STS` 和 `RX_STS` 都应为 0。
所以这是空闲态，不是不可达。PLL 使能只证明时钟路径通，不证明核在运行。

### MCU 启动失败的具体表现

```
mali: sentinel 0xff5aa5f0 written to the version word
mali: the start loop ended after 1000000 spins (halt seen 0, fatal seen 0)
mali: MCU status 1 (raw state 1; boot unproven), job irq 0, gpu irq 0
mali: version word 0xff5aa5f0 (untouched; no firmware version published)
mali: the global interface never raised; the MCU did not boot
```

**哨兵未被改写 = 固件一指令都没执行。** `halt`/`fatal` 都没报，中断一条没来。

### 已核实的两个上游事实

**① panthor 在 MCU 启动前强制调用 GPUEB 上电**（`panthor_fw.c:1131-1146`）：

```c
if (of_machine_is_compatible("mediatek,mt6895") && mt6895_gpueb_available()) {
    ret = mt6895_gpueb_power_on();
    if (ret) return ret;              /* 失败就不启动 MCU */
}
gpu_write(fw->iomem, MCU_CONTROL, MCU_CONTROL_AUTO);
```

`mt6895_gpueb_power_on()` 内部：`INIT_SHARED_MEM` → `CMD_POWER_CONTROL(POWER_ON)`，两步都走 IPI。

**本驱动只做了 `ml_io_map`，没有发过 IPI。**

**② 原厂 LK 的 `app_load_gpueb` 不碰 Mali 的 `MCU_CONTROL`**：

装载 RV33 固件到 SRAM → 填共享内存和 GPUMPU 参数 → 向 `0x13c60600` 写 `3` →
等待 `0x13c5fd24` 读到 `0x55667788`。**这条路径里没有对 CSF `MCU_CONTROL` 的写入。**
且该 LK 在 `mode=2`（recovery）时跳过 GPUEB 加载。

### 寄存器命名（以厂商 `mt6895_spm_reg.h` 为准）

| 偏移 | 名称 | 说明 |
|---|---|---|
| `0xEB8` | `MFG0_PWR_CON` | GPU 域本体 |
| `0xEBC` | `MFG1_PWR_CON` | MPU，厂商手动拉的那个域 |
| `0xEB0` | `EMI1_PWR_CON` | 内存互连 |

`PWR ACK` 是 **bits 31:30**。`bit 12` 是 SRAM 掉电 ACK，不是电源 ACK。

### GPUEB mailbox 布局

```
0x13c5fd80   消息 SRAM, 0x280 = 640 B = 160 个 4 B slot（不是寄存器块）
0x13c62000   控制 bank: send(RO) / set(WO) / clr(WO) / recv(RO)，四个 1-bit-per-channel 位图
0x13c62004   set   0x13c62074  clr   0x13c62000  send   0x13c62078  recv
```

`0x13c5fd80 + 0x280 = 0x13c60000`，控制 bank 在 `0x13c62000`，**中间是空的，不能按偏移算**。

通道 `GPUFREQ = 1`。**TX 是 slot 4（字节 `0x10`），RX 是 slot 47（字节 `0xbc`）——不是同一 slot 号。**

---

## 二、USB：下行 4.5 MB/s

### 实测吞吐

| 尺寸 | 下行 | 上行 |
|---|---|---|
| 64 KB | 2835 KB/s | 152 KB/s |
| 256 KB | 4037 KB/s | 330 KB/s |
| 1 MB | 4570 KB/s | 365 KB/s |
| 4 MB | 3561 KB/s | 401 KB/s |

协商速度 480 Mbps（High Speed）。`0 errors / 0 dropped`，SHA256 一致。

### 优化来源

`usb_thread` 原为每轮固定睡 1 ms，改为有活动时只睡 50 µs：

```c
if (ep2_tx_active || ep2_tx_off != before) {
    delay_for_interval(50, NSEC_PER_USEC);   /* 忙 */
} else {
    delay_for_interval(1, NSEC_PER_MSEC);    /* 闲 */
}
```

**下行提升 8–11 倍。**

### 上行瓶颈位置

主机侧纯 socket 写入（不经 `hakid` 协议）也是 380–413 KB/s，
**瓶颈在手机接收路径，不在 `hakid` 也不在 TCP 分块。**

### 25 MB/s 需要 QMU

512 字节/包 ÷ 50 µs = 9.8 MB/s 理论上限，实测 4.57 说明每轮约 110 µs。
要达到 25 MB/s 每轮必须 < 20 µs。

PIO 模式下 `EP_TXPKTRDY` 一次只能提交一个包，每包要读 CSR + 128 次 32 位 MMIO 写 + 写 CSR，
本身就在 20–50 µs 量级。**25 MB/s 只能走 QMU/DMA 路径。**

---

## 三、CPU 拓扑

```
cpu 0  phys 0x0    A55 小核
cpu 1  phys 0x400  A78 大核
cpu 2  phys 0x500  A78 大核
cpu 3  phys 0x700  A78 大核（boot 核，最快）
```

`cpus=4` 跑的是 **3 颗 A78 + 1 颗 A55**，不是全小核。

未启用：`0x100`、`0x200`、`0x300`（三个 A55）、`0x600`（一个 A78）。

---

## 四、调试基建

| 能力 | 实现 |
|---|---|
| 实时内核日志 | `/dev/hlog`，从开机第 19 秒起，`pread` 按偏移读 |
| 远程执行 | `hakid` 监听 `10.0.0.2:2222`，跑 `/redwood/bin` 下程序 |
| 文件传输 | `PUT`/`GET`，1 MB SHA256 一致 |
| 主动退出 | `/dev/hakios-return`，`RETURN` 后 2 秒内核退出 |
| 窗口 | `loader_return_after=600` |

**退出耗时（`mali=0`）：** `RETURN` → 2 秒内核退出 → 30–35 秒 Android 就绪。

**`mali=6` 会让退出变成 5 分钟以上甚至失败**，因为 GPU 被拉到域全开状态后收不回来。

### 判断设备状态的正确方法

`18d1:d00d` 这个 USB ID 在 **loader 阶段也会出现**（loader 自己实现的 USB，
`iProduct` 也是 `HakiOS phone`）。所以：

| 状态 | `hakid` | ECM 网卡 |
|---|---|---|
| XNU 运行 | ✅ | ✅ |
| loader 收尾/引导 Android | ❌ | ❌ |
| Android | ❌ | ❌ |

**连得上 `hakid` 才是在 XNU 里。** 看 `lsusb` 会误判。

---

## 五、当前镜像与回退

| 镜像 | 配置 | 说明 |
|---|---|---|
| `boot-910b` | `mali=0 mali_probe=1` | 只读探针，退出 35 秒 |
| `boot-908` | `mali=0` | Stage 1 稳定版，退出 27 秒 |
| `boot-904` | `mali=0` | Stage 1 验收版 |
| `boot-804i` | — | USB 网络稳定版 |

### 探针开关

`mali_probe=1` **可以单独使用**，不需要 `mali=N`：`hakios_mali610_init()` 在
`mali=0` 时仍会为探针启动 `mali_thread`，但不设 `gMaliLevel`。

探针在 `mali_bring_up()` 最开头，**不给 `gGpu` 赋值**——`quiesce()` 看到非零就会写
`GPU+0x700`，所以只在 `bus_protect_release()` 前返回还不够。
