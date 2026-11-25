# GPU 与 USB 的实测结果

红米 Note 11T Pro（xaga），MT6895Z / 天玑 8100，Mali-G610 MP6。

最后更新：2026-11-25，镜像 `boot-910b.img`。

---

## 一、GPU：硬件识别完成，MCU 未启动

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

固件完整装入并可映射。

**注意指令集：MCU 是 Cortex-M7，跑 Thumb，不是 ARM64。**
`0x01000000` 段的实际入口是 `0x010018ec`（向量 `0x010018ed`，Thumb 位）。
早先文档写的 ARM64 是错的。

### 电源域

**冷态（`boot-910b` 只读探针，`mali=0 mali_probe=1`，未映射 GPU、未发 AUTO）：**

```
MFG0_PWR_CON   0xc000224d    bit30/31 PWR_ACK=1, PWR_ACK_2ND=1, bit12 SRAM_PDN_ACK=0
MFG1_PWR_CON   0x1112        PWR_ACK=0, 隔离与时钟禁用均置位
EMI1_PWR_CON   0xc000024d
XPU_PWR_STA(0xF3C)  0x3      & 0x3ffc = 0   ← MFG1-12 均未上电
```

**热路径（`boot-907`，`mali=6`，带时间戳）：**

```
21.837s   GPU_ID = 0（冷）→ 走冷启动路径        [hlog-boot907.txt:465]
21.872s   MFG1 = 0xc000000d（双 ACK）             [:470]
35.078s   0xF3C & 0x3ffc = 0x4（只有 MFG1）       [:932]
35.084s   GPU_ID = 0xa8670004                    [:933]
35.152s   CSF = 0x040a0412                       [:943]
45.787s   哨兵 0xff5aa5f0 未被改写                [:1247]
45.802s   MFG1 仍是 0xc000000d                   [:1254]
```

### 一个更早的反例：trial695

```
33.690s   7 of twelve domains answered
34.006s   0xF3C = 0x3ffc   ← 12 个域全开
34.260s   哨兵写入
44.279s   start loop 结束（halt seen 0, fatal seen 0）
44.354s   版本字未改写
44.357s   the global interface never raised
```

**34.006s 的 `0x3ffc` 是启动前的采样，44.354s 是到那时仍未看到版本发布。**

可以排除的解释：**"12 个域始终全关"**。
不能由此推出"一条指令都没执行"，也不能推出所有局部时钟和复位都正常。

### 寄存器语义（关键，容易读错）

`0xF34`（PWR_STA）和 `0xF3C`（XPU_PWR_STA）**不是同一组域的两份副本**：

- `0xF34` 的 `0x20` 是 **AUDIO**（`mtk-pd-chk-mt6895.c:658`），不是 MFG4
- MFG1–12 在 `XPU_PWR_STA`（`0xF3C`）里逐项对应 **bit 2 到 bit 13**（`:682-693`）
- `gpufreq_mt6895.c:3580` 正是用 `0xF3C & 0x3ffc` 检查关电
- `0xF38` / `0xF40` 是各自的第二状态字

**所以 `0xF3C & 0x3ffc` 是判断 MFG 域的正确读数，`0xF34` 的低位与 MFG 无关。**

### MFG0 / MFG1 的位定义

`mt6895_spm_reg.h:2164-2187`：`bit30 = PWR_ACK`，`bit31 = PWR_ACK_2ND`，`bit12 = SRAM_PDN_ACK`。

`0xEB8 = 0xc000224d` 确认 **MFG0 本域已上电**；
`0xEBC = 0x1112` 则是 **MFG1 双 ACK = 0**，隔离和时钟禁用均置位。

顶层 MFG0 的 ACK **不覆盖**中层子域，也不证明 MCU 的时钟或取指通。

### GPUEB

```
MFGPLL+0x808   0x15445    bit0 = 1    GPUEB PLL 已使能
MFGPLL+0x840   0x8000     gpueb_pll_ctrl
MFGPLL con0 0x15444  con1 0x832113b1  con3 0x2

mbox ctl  tx 0 (idle)  rx 0 (empty)
mbox sram 0 of 160 words non-zero
```

`GPUEB-IPI-PROTOCOL.md` §1.4 记载：正常状态下 `TX_STS` 和 `RX_STS` 都应为 0。

**全零邮箱只与"空闲"相容**，不能证明可达，也不能证明不可达。
**PLL 使能位只能证明这个位被置上了**，不证明核在执行，更不证明有过成功应答。

### MCU 启动失败的具体表现

```
mali: sentinel 0xff5aa5f0 written to the version word
mali: the start loop ended after 1000000 spins (halt seen 0, fatal seen 0)
mali: MCU status 1 (raw state 1; boot unproven), job irq 0, gpu irq 0
mali: version word 0xff5aa5f0 (untouched; no firmware version published)
mali: the global interface never raised; the MCU did not boot
```

**哨兵未被改写，到那时为止没有看到版本发布。** `halt`/`fatal` 都没报。

**这不能推出"一条指令都没执行"**——纯 AP/GIC 漏中断也解释不了共享版本字，
但反过来，版本字没变只说明这条可观测路径没有信号，不排除 MCU 在别处跑过。
trial696 做过最小入口标记，三次均未观察到写回，**单靠这个阴性结果分不出
取指失败与写入不可见**。

`AUTO=2` 与原厂 `kbase_csf_firmware_init+0xb14` 的写法一致，`MCU_STATUS=1` 只表示 ENABLED，
**没有理由先改这个写值。**

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

## 二·五、GPUEB 按需探针

`/dev/hakios-gpueb`（major 22，0600），接入 `mali=0` 稳定基线。
**注册和 open 都不碰硬件**，第一次 `write` 才映射 mailbox。

四个命令，各自独立，每条 10 秒预算（厂商 `gpueb_ipi.h:10` 值）：

| 命令 | 作用 |
|---|---|
| `probe` | 只读 mailbox 和共享页，不写任何东西 |
| `init` | `CMD_INIT_SHARED_MEM`，成功后本会话复用 |
| `on` | `CMD_POWER_CONTROL(POWER_ON)`，成功则记引用 |
| `off` | `POWER_OFF`，只对已持有的引用配对 |

无应答记「结果未知」，不自动重发——迟到的回包会让重发的那条无法归属。

### 握手结果：INIT / ON / OFF 三项全部成功（`boot-917`）

```
seq 1 init  sent 1 replied 1 rx 0x2 verdict 0 cmd 0x0  ret 0
    words 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000

seq 2 on    sent 1 replied 1 rx 0x2 verdict 0 cmd 0x4  ret 30
    words 00000004 00000000 0000001e 00000000 00000000 00000000 00000000 00000000

seq 3 off   sent 1 replied 1 rx 0x2 verdict 0 cmd 0x4  ret 30
    words 00000004 00000000 0000001e 00000000 00000000 00000000 00000000 00000000
```

`sent` 与 `replied` 分开记录：`replied 1` 表示**收到了本次发送的应答**，
不是读到了旧槽。`ret` 是 `w[2]` 的有符号值，`30` 即 `0x1e`，非负。

`cmd 0x4` 是 `CMD_POWER_CONTROL`。

### 共享页在 ON 前后确实变化

**ON 之前：**

```
+000: 00000000 00000000 00000005 00000028 000d6d80 000d0020 000124f8 00010c8e
+020: 000124f8 000124f8 00000000 000006c0 00000000 000006c0 00000000 000000f6
+060: 00000001 00000002 00130013 00000000 00000001 00000001 00000000 00000002
```

**ON 之后：**

```
+000: 00000003 0000001b 00000005 00000028 00068fb0 0005dfe8 0000ea60 0000d6d8
+020: 000124f8 000124f8 00000000 00000209 00000000 000006c0 00000000 000000f6
+060: 00000001 00000002 00130013 00000000 00000001 00000001 00000000 00000002
```

| 偏移 | 变化 |
|---|---|
| `+000` | `0` → `3` |
| `+004` | `0` → `0x1b` (27) |
| `+010` | `0x000d6d80` (880000) → `0x00068fb0` (429488) |
| `+014` | `0x000d0020` (852000) → `0x0005dfe8` (385000) |
| `+018` | `0x000124f8` (75000) → `0x0000ea60` (60000) |
| `+028` | `0` → `0x209` |
| `+02c` | `0x6c0` → `0x209` |

**GPUEB 不只是应答了，它的状态真的变了。** 频率量级的字段从 880/852 降到 429/385。

### probe 实测输出（`boot-913`，`mali=0`）

```
gpueb: probe: ctl tx 0 rx 0 (nothing written)
gpueb: shared before anything:
+000: 00000000 00000000 00000005 00000028 000d6d80 000d0020 000124f8 00010c8e
+020: 000124f8 000124f8 00000000 000006c0 00000000 000006c0 00000000 000000f6
+040: 00000000 00000000 00000000 00000027 00000000 00000000 00000000 00000000
+060: 00000001 00000002 00130013 00000000 00000001 00000001 00000000 00000002
```

**共享页不是空的**——GPUEB 的共享结构里已有完整数据。

| 偏移 | 值 | 含义（待确认） |
|---|---|---|
| `0x008` | 5 | power_count 或 dvfs 相关 |
| `0x010` | 880000 | 频率量级 |
| `0x014` | 852000 | 频率量级 |
| `0x018/0x020/0x024` | 0x124f8 = 75000 | 频率量级 |
| `0x068` | **0x00130013** | **shader_present，与 `GPU_ID` 的 `shader 0x130013` 一致** |

**这引出一个还待确认的问题：`INIT_SHARED_MEM` 是否已经在 LK 阶段做过。**
如果是，再发一次可能覆盖固件正在用的数据。

## 三·五、GPU 的结论边界

**可以写入的结论：**

> 热路径下 MFG1 有上电确认（`0xc000000d`），`AUTO` 之后未观察到版本发布，
> 也未观察到 `GLOBAL_IF`。停止位置和根因待定。

**已被证据排除的方向：**

- 「12 个功能域没上电导致 MCU 失败」——trial695 里 `0xF3C = 0x3ffc` 全开，MCU 仍不执行
- 「纯 AP/GIC 中断丢失」——907 的 JOB RAW/STAT 多次为 0、MASK 全 1，版本哨兵也没变

**已经核实的：**

**GPUEB 握手可以打通。** `INIT_SHARED_MEM`、`POWER_CONTROL(ON)`、`POWER_CONTROL(OFF)`
三项都收到了本次应答，共享页在 ON 前后确实变化。这条路径从"从未发起"变成了"验证通过"。

**剩下的方向，按优先级：**

**① 同一次启动中、`AUTO` 之前的 GPUEB 握手（首个待办已解决，见上）**

参考驱动（xaga Panthor）的做法：

- `CMD_INIT_SHARED_MEM`：看传输层是否收到**本次**应答（`IPI_ACTION_DONE`）
- `CMD_POWER_CONTROL(POWER_ON)`：除应答外，还检查 `u.return_value >= 0`

**907 没有这次事务，应记为「未发起」。**
现有记录里还没有出现过一次握手超时——也就是说这条路从来没被测过。

**② 若两步都成功而启动仍超时，查 AS0 取指路径**

- 向量页 VA 0
- 实际取指 `0x010018ec` 所在的页
- 版本字 `0x04000000` 对应的可写共享页
- **AP 与 MCU 是否指向同一物理页**

trial696 做过最小入口标记，三次均未观察到写回。
**单靠这个阴性结果分不出「取指失败」与「写入不可见」。**

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
