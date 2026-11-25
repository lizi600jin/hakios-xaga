# GPU 与 USB 的实测结果

红米 Note 11T Pro（xaga），MT6895Z / 天玑 8100，Mali-G610 MP6。

最后更新：2026-11-25，镜像 `boot-910b.img`。

---

## 一、GPU：MCU 已启动（`boot-923g`）

**2026-11-25 晚，MCU 第一次真正执行了固件。**

### 关键：先让 GPUEB 上电，再问 GPU

一次改动的效果，胜过此前所有在 `MCU_CONTROL` 上的尝试：

```
21.313221  mali: gpueb: asking GPUEB to power the GPU before the first read
21.313608  mali: gpueb: INIT answered 0
21.314924  mali: gpueb: POWER_ON answered 1, a reference is held for this run
21.314927  mali: gpu_id after the GPUEB request: 0xa8670004 (csf 0x40a0412)
```

**`POWER_ON` 之后 `GPU_ID` 直接可读**，于是整个手动冷启动分支（SPM、VBUCK2、MFGPLL、
十二个域）被跳过——这正是 panthor 的路径。

```
21.405227  mali: sentinel 0xff5aa5f0 written to the version word
21.405355  jobirq probe GLOBAL_IF observed by existing loop spin 7: raw 0x80000000
21.406456  mali: version word 0x1010000 (written by the firmware)
21.406472  mali: the MCU is up; talking to the global interface
21.406520  mali: global interface config + ping answered (req 0x10e ack 0x10e, job irq 0)
21.406524  mali: M1 done: the firmware is running and the global interface answered
```

| 判据 | 结果 |
|---|---|
| 版本哨兵 `0xff5aa5f0` | 被改写为 **`0x1010000`** |
| `GLOBAL_IF` bit31 | 第 **7** 次轮询置位 |
| 全局接口 ping | `req 0x10e` / `ack 0x10e` 一致 |
| 域状态 | `MFG1 0xc0000112`（ack 1/1）、`F3C 0x7`（MFG1 位） |
| 共享页 | `power_count` 1、`dvfs_state` 0 |

**此前的记录全部作废：** "MCU 从未执行"、"哨兵未被改写"、"12 个域没上电导致失败"
都是缺少 GPUEB 上电这一步时的观察，不是根因。

### 六步停机全部成功，RETURN 32 秒回 Android（`boot-924`）

`gpuebctl stop` 在 XNU 活着时执行，六步实测：

```
41.350799  stop halt: req before 0x10e
41.351247  stop halt: req after 0x10f, ringing the doorbell
41.351714  stop halt: reads 0x2 after 1000 us          ← MCU_STATUS_HALT
41.352154  stop disabled: reads 0 after 0 us           ← MCU_STATUS_DISABLED
41.352588  stop: no soft reset needed
41.353050  stop as0 parked: reads 0 after 0 us         ← AS0 已停
41.353535  stop l2 pwrtrans clear: reads 0
41.354059  stop l2 pwrtrans after off: reads 0
41.354529  stop: l2 ready now 0                        ← L2 已断电
41.355059  stop: irqs masked and cleared (raw 0 job 0 mmu 0)
41.355584  stop: releasing the GPUEB reference
41.356347  gpueb: bringup off: reply cmd=6 ret=0       ← POWER_OFF 成功
41.361873  MFG1 0x1112 (ack30 0 ack31 0) F3C 0x3       ← 完全回基线
41.363452  stop: done (mcu status 0, tiler ready 0, l2 ready 0)
```

**每一步都在 1 ms 内完成，没有一步超时，没有用到软复位锤。**

关态与 `boot-919` 的 A 点逐位一致：`MFG1 0x1112`、`F3C 0x3`、`power_count 0`。

**然后 `RETURN` → 32 秒回 Android。**

此前 `mali=6` 要 5 分钟以上，而这一轮走的是完整的 panthor 路径
（GPUEB 托管上电 + MCU 启动 + 六步停机 + GPUEB 撤电），
退出反而回到了与 `mali=0` 相同的量级。

### 自动停机路径的两个发现（`boot-925` / `926`）

**一、`quiesce()` 会在每个 CPU 上执行，`printf` 在次级 CPU 上 panic。**

panic 栈（`boot-925` 真机照片）：

```
_psci_smp_stop_here+0x58
_loader_return+0x34
_hakios_mali610_quiesce+0x38
_printf+0x24
```

`loader_return()` 在每个处理器退出时都会走一遍，次级 CPU 到达 `quiesce()` 时
控制台已经不在可用状态。修法是函数第一件事就用原子变量认领工作，没认领到的
处理器直接返回，不打印也不碰寄存器——六步本来就是不可重入的，在这之前它可能
连续跑四次。

**二、自动路径的 `printf` 不出日志。**

`psci_smp_stop_others()` 在 `psci_smp.c:297` 先执行 `DAIFSet DAIFSC_ALL`，
之后才调 `quiesce()`。`boot-926` 的日志到 550.8 秒，`quiesce` / `stop` 一行都没有。

**所以退出路径上的六步无法从 hlog 观察。** 这是这些 hook 的共同处境，
`hakios_adreno_quiesce()` 也一样——它们静默工作，日志里看不到。

**验证六步只能用手动入口**（`gpuebctl stop`，在 XNU 活着时执行），
那条路径有完整的 14 行实测记录，就是上面 `boot-924` 的数据。

### 上一轮为什么会卡（`boot-923g` ）

`trial 923g` 发出 `RETURN` 后卡在苹果 logo。

它的 `quiesce()` 只写了 `MCU_CONTROL=DISABLE` 就走了——那是"MCU 从没跑过"的时代留下的代码。
MCU 真的在运行时，L2 还挂着脏缓存、AS0 还指着 `0x50000000`、三个中断块还开着，
GPUEB 在这时候被要求撤电，它等一个永远不来的总线空闲。

**`quiesce()` 从未处理过"MCU 真的在跑"的情况**——它此前只需写 `MCU_CONTROL=DISABLE`
然后走开。按 panthor 的做法，正确的停止顺序是六步（`boot-924` 起实现）：

| 步 | 动作 |
|---|---|
| 1 | `GLB_REQ.HALT` + `mem_flush` + 门铃，等 `MCU_STATUS == 2` |
| 2 | `MCU_CONTROL=DISABLE`，等 `MCU_STATUS == 0` |
| 3 | AS0 写 `TRANSTAB=0` / `MEMATTR=0` / `TRANSCFG=UNMAPPED` + `UPDATE` |
| 4 | **L2 掉电**（`L2_PWROFF`），等 `PWRTRANS=0` 且 `L2_READY=0` |
| 5 | 屏蔽并清三个中断块 |
| 6 | 最后才释放 GPUEB 电源 |

**第 4 步此前从未做过**，而它是让 AXI 端口安静下来的关键——L2 还挂着脏缓存时
要求 GPUEB 撤电，GPUEB 会等一个永远不来的空闲。

## 一·五、GPU 的历史数据（`boot-907` 及以前，MCU 未启动时）

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

### 事务层通了，但电源命令号写错（`boot-917`）

原始记录中的 `on` 和 `off` 是用户态命令名。`cmd` 来自回包的第一个字。

```
seq 1 init  sent 1 replied 1 rx 0x2 verdict 0 cmd 0x0  ret 0
    words 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000

seq 2 on    sent 1 replied 1 rx 0x2 verdict 0 cmd 0x4  ret 30
    words 00000004 00000000 0000001e 00000000 00000000 00000000 00000000 00000000

seq 3 off   sent 1 replied 1 rx 0x2 verdict 0 cmd 0x4  ret 30
    words 00000004 00000000 0000001e 00000000 00000000 00000000 00000000 00000000
```

`sent` 与 `replied` 分开记录：`replied 1` 表示**收到了本次发送的应答**，
不是读到了旧槽。这一层是成立的。

**但 `cmd 0x4` 不是 `CMD_POWER_CONTROL`。** 厂商 `gpufreq_ipi.h:29-49`：

```c
CMD_INIT_SHARED_MEM    = 0     CMD_SET_LIMIT          = 5
CMD_GET_FREQ_BY_IDX    = 1     CMD_POWER_CONTROL      = 6    ← 应该是这个
CMD_GET_POWER_BY_IDX   = 2     CMD_COMMIT             = 7
CMD_GET_OPPIDX_BY_FREQ = 3
CMD_GET_LEAKAGE_POWER  = 4     ← 实际发的是这个
```

seq 2 / seq 3 都是漏电功耗查询。厂商调用方按电压参数 `u.volt` 发送请求，从应答的
`u.power` 取结果，两笔均返回 **30 mW**。917 把这个正数当作电源操作成功，
先将驱动本地的 `gpueb_held` 置 1，再清 0。三笔中没有 `CMD_POWER_CONTROL`。
[厂商命令表](vendor-gpueb/gpufreq_ipi.h#L51) · [查询收发字段](vendor-gpueb/gpufreq_v2.c#L783)

命令号已改成 `6`，尚未上板。

### GPUEB 的 POWER_CONTROL 能让 MFG1 上电（`boot-919`）

命令号修正后的三笔事务，同一次启动：

```
seq 1 init  cmd 0x0  ret 0   sent 1 replied 1 rx 0x2
seq 2 on    cmd 0x6  ret 1   sent 1 replied 1 rx 0x2     ← CMD_POWER_CONTROL / POWER_ON
seq 3 off   cmd 0x6  ret 0   sent 1 replied 1 rx 0x2     ← 配对 POWER_OFF
```

`ret` 是回包 `w[2]` 的有符号值（`u.return_value`）。

**A 点与 B 点的域状态：**

| 采样点 | `MFG1_PWR_CON` (`0xEBC`) | `0xF3C` | `0xF40` |
|---|---|---|---|
| A：INIT 后、ON 前 | `0x1112` ack `0/0` | `0x3`，`&0x3ffc = 0` | `0x3` |
| B：ON 应答后 | `0xc0000112` ack `1/1` | `0x7`，`&0x3ffc = 0x4` | `0x7` |

差值正好是 `0xEBC` 的 `bit30`+`bit31`，和 `0xF3C` 的 `bit2`（= MFG1）。

**三项佐证同时成立：**

1. 命令应答：本笔 `cmd=6`、`state=1`、`signed w[2] = 1 >= 0`
2. 硬件报开：`(0xEBC >> 30) & 3 == 3`，`0xF3C` bit2 = 1，`0xF40` bit2 = 1
3. 共享页：`+064` `dvfs_state` 从 `2`（`DVFS_POWEROFF`）变 `0`，
   `+06c` `power_count` 从 `0` 变 `1`

**所以 GPUEB 的电源命令不只是应答，它真的把 MPU 域打开了。**

### C 点：配对 OFF 干净回到基线

```
587.661788 s   OFF 回包 cmd=6 ret=0
587.661836 s   共享页 dvfs_state = 2 (DVFS_POWEROFF)，power_count = 0
587.696025 s   MFG1 = 0x1112，ACK 0/0，F3C = F40 = 0x3
614.614802 s   再次读到相同的关态
```

后两个采样点之间隔了 27 秒，读数一致：**关态是稳定的，不是瞬态。**

与 A 点的 `0x1112` / `0x3` 完全相同，说明 OFF 把状态完整还原了。

### `0xEBC` 的完整差值（A → B → C）

```
A^B = 0xc0001000
    bit30  0 -> 1   PWR_ACK
    bit31  0 -> 1   PWR_ACK_2ND
    bit12  1 -> 0   SRAM_PDN_ACK
```

`bit12` 是 `SC_MFG1_SRAM_PDN_ACK`（`mt6895_spm_reg.h:2185`），它随上电撤销。
`bit8`（`SRAM_PDN` 请求位）在 A 和 B 都是 1，没有变化——这一位的语义待确认。

### PWR_CON 的位定义（容易读错）

`mt6895_spm_reg.h:2179-2187`：

| 位 | 名称 |
|---|---|
| `bit 0` | `PWR_RST_B`（复位释放，**不是**上电请求） |
| `bit 2` | `PWR_ON` |
| `bit 3` | `PWR_ON_2ND` |
| `bit 4` | `PWR_CLK_DIS` |
| `bit 30` | `PWR_ACK` |
| `bit 31` | `PWR_ACK_2ND` |

`mtk-scpsys.c:578-595` 的上电顺序：置 `PWR_ON` → 置 `PWR_ON_2ND` → 等 `PWR_ACK` → 清 `PWR_CLK_DIS`。

**域状态掩码**（`gpufreq_mt6895.h:74-76`）：

| 掩码 | 含义 |
|---|---|
| `0x6` | MFG0 + MFG1（AP 参考路径拉起 MFG1 后检查，`gpufreq_mt6895.c:3382-3385`） |
| `0x3ffe` | MFG0..MFG12 |
| `0x3ffc` | MFG1..MFG12（关电检查，`:3580`） |

MFG1 单域看 `0xF3C` **bit 2**；MFG1+MFG2 是 `0xC`。

### 共享页在两次查询前后变化（原因待定）

**较早的快照（约 33–55 秒）**

```
+000: 00000000 00000000 00000005 00000028 000d6d80 000d0020 000124f8 00010c8e
+020: 000124f8 000124f8 00000000 000006c0 00000000 000006c0 00000000 000000f6
+060: 00000001 00000002 00130013 00000000 00000001 00000001 00000000 00000002
```

**较晚的快照（55.389 秒）**

```
+000: 00000003 0000001b 00000005 00000028 00068fb0 0005dfe8 0000ea60 0000d6d8
+020: 000124f8 000124f8 00000000 00000209 00000000 000006c0 00000000 000000f6
+060: 00000001 00000002 00130013 00000000 00000001 00000001 00000000 00000002
```

| 偏移 | 厂商字段 | 较早 → 较晚 |
|---|---|---|
| `+000 / +004` | `cur_oppidx_gpu / cur_oppidx_stack` | GPU 0 → 3，STACK 0 → 27 |
| `+010 / +014` | `cur_fgpu / cur_fstack` | GPU 880 → 430 MHz，STACK 852 → 385 MHz |
| `+018 / +01c` | `cur_vgpu / cur_vstack` | GPU 750 → 600 mV，STACK 687.5 → 550 mV |
| `+028` | `cur_power_gpu` | 0 → 0 mW |
| `+02c` | `cur_power_stack` | 1728 → 521 mW |
| `+064 / +06c` | `dvfs_state / power_count` | 两次均为 2（`DVFS_POWEROFF`）和 0 |

两个采样点的共享页报告了不同的运行点和功耗值。刷新者、刷新时刻和触发条件仍待确定。
917 缺少对应时刻的 MFG1 和 XPU 域状态读数。

**`w[2] = 30` 的读法：** 按漏电查询的 ABI，它是 `u.power`，单位 mW；
请求里填的 `1/0` 在这条命令里被当作电压参数。它没有测得整块 GPU 消耗 30 mW。
固件里那条查询的正常分支直接返回常数 30 并写回 `w[2]`，**没有发布整张状态页**，
所以两次快照的差异不能归因于某一次事务。

`dvfs_state` 与 `power_count` 两次都没变，倾向"查询没有上电"。
[共享页布局](vendor-gpueb/gpufreq_ipi.h#L109) · [DVFS 状态位](vendor-gpueb/gpufreq_v2.h#L69)

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

| 偏移 | 值 | 厂商字段 |
|---|---|---|
| `0x008` | 5 | `opp_num_gpu`，GPU 工作表的档数 |
| `0x010` | 880000 kHz | `cur_fgpu` |
| `0x014` | 852000 kHz | `cur_fstack` |
| `0x018/0x020/0x024` | 0x124f8 = 75000 | `cur_vgpu / cur_vsram_gpu / cur_vsram_stack`，均为 750 mV |
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

**GPUEB 的 IPI 事务层可以打通。** `xfer` 能确认"消息已发"与"收到本次应答"两件事，
`CMD_INIT_SHARED_MEM` 拿到了本次回包。这条路径从"从未发起"变成了"事务可验证"。

**还没有做到：** 真正给 GPU 上电。`boot-917` 那两笔把命令号写成 `4`
（`CMD_GET_LEAKAGE_POWER`），是两次漏电查询，不是 `POWER_CONTROL`。
`CMD_POWER_CONTROL` 是 `6`，已改，尚未上板。

### Phase 1 验收（Neon 的 A0/A1/A2 全部通过）

| 步 | 内容 | 结果 |
|---|---|---|
| A0 | 只读 mailbox + 共享页 | 通过：`tx 0 rx 0`，共享页 32 字可读 |
| A1 | 仅 `INIT_SHARED_MEM` | 通过：`cmd 0x0 ret 0` |
| A2 | `POWER_ON` → 采样 → `POWER_OFF` | 通过：`cmd 0x6 ret 1` / `ret 0`，域状态回基线 |

**一处必须记住的坑：** `gpufreq_ipi.h:29-49` 里 `4` 是 `CMD_GET_LEAKAGE_POWER`，
`CMD_POWER_CONTROL` 是 `6`。用错命令号时固件照样应答（返回常数 `30`），
看起来像成功，实际是漏电查询。

### bring-up 路径的接入（`mali_gpuebpower=1`，默认关闭）

按 panthor 的顺序，把 GPUEB 上电放在 `load_firmware()` 之前：

```
mali_early_as0()
    if (gMaliGpuebPower)
        hakios_gpueb_bringup_power_on();     ← INIT + POWER_ON
    load_firmware();
    as_enable(0, gMcuRootPa);
```

退出侧在 `hakios_mali610_quiesce()` 里，**放在 `gGpu == 0` 提前返回之前**——
上电发生在 `gGpu` 赋值之前，若在那里返回就会跳过释放，把上电状态带进 loader。

引用由两条路径**各自记账**（`gpueb_held` 给设备节点，`gpueb_bringup_held` 给 bring-up）：
它们不是同一个所有者，释放一方不能释放另一方。

**剩下的方向，按优先级：**

**① 同一次启动中验证 GPUEB 电源命令和 MFG 域状态**

采样点固定为三个，每次连同事务序号和时刻一起保存：

| 点 | 时刻 |
|---|---|
| A | 本次 INIT 应答后、发 ON 前 |
| B | 真正 `cmd=6 / state=1` 应答、`signed w[2] >= 0`、`held` 仍为 1 时 |
| C | 配对 `cmd=6 / state=0` 应答后 |

每点记录 `MFG1_PWR_CON`（`0xEBC`）、`0xF3C`、`0xF40`、共享页 `+0x64` / `+0x6c`。

**MFG1 的核对判据是 `0xEBC[31:30]` 加 `0xF3C` bit 2**，不是十二域全开——
厂商 PDCv2 的手动上电检查是 `0xF3C & 0x6`。

参考驱动（xaga Panthor）的做法：

- `CMD_INIT_SHARED_MEM`：看传输层是否收到**本次**应答（`IPI_ACTION_DONE`）
- `CMD_POWER_CONTROL(POWER_ON)`：除应答外，还检查 `u.return_value >= 0`

907 未发起这次事务。下一轮可在 `mali=0` 下采三次，分别在本次 INIT 成功且尚未发 ON 时、
ON 应答成功且仍持有引用时、配对 OFF 应答后。每次保存时间、关联的事务序号、
`MFG1_PWR_CON`（SPM+0xEBC）、`XPU_PWR_STA`（+0xF3C）、第二状态字（+0xF40）
以及共享页的 `dvfs_state` 和 `power_count`。三次采样都可以在启动 GPU MCU 前完成。

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

## 四·五、loader 的救援分支有一个未检查的指针（2026-11-25 晚）

**现象：** 刷完镜像后连 XNU 都进不去，屏幕显示：

```
=== mtkloader: XNU did not return before the reset ===
...
*** Loader fault: vector 0x200 ESR 0x96000000 ELR 0x40003ecc FAR 0x512000000000
halting; the watchdog will restart the board
```

**根因在 `mtkloader.c:2964-2980` 的旧 msgbuf 转储：**

```c
uint64_t mb   = xnu_rd64(xsyms.msgbufp);
uint64_t w0   = xnu_rd64(mb), w1 = xnu_rd64(mb + 8), bufc = xnu_rd64(mb + 16);
int32_t size  = (int32_t)(w0 >> 32), bufx = (int32_t)w1;
if (size <= 0 || size > 1024*1024 || bufx < 0 || bufx > size) return;   /* 三个字段有检查 */
...
char c = xnu_rd8(bufc + i);        /* bufc 的地址范围从未检查 */
```

`xnu_rd8` → `xnu_va_to_pa` → `pa ? *(volatile char *)pa : 0`
只挡住 `pa == 0`，挡不住 `0x512000000000`。

**触发条件：** 上一次运行被看门狗复位（XNU 卡在退出路径没按时返回），
DRAM 里留着垃圾指针，救援分支把它当可信数据用了。

**修法**（`XNU_PHYS_BASE = 0xc0000000`，`XNU_MEM_SIZE = 0xfe000000`）：

```c
if (bufc < XNU_PHYS_BASE ||
    bufc > XNU_PHYS_BASE + XNU_MEM_SIZE - (uint64_t)size) {
        out("msgbuf pointer out of the DRAM window; not dumping\n");
        return;
}
```

HLOG 在这个函数之前已经保存成功，所以跳过转储不影响诊断数据。

**分区完整性已独立核验：** 主备 GPT 与备份逐字节一致，APFS 根容器全量哈希比对通过，
`xnusys` 2.50 GiB（`/dev/block/sdc88`）、`xnuvar` 64.0 GiB（`sdc89`）都正常。

## 四·六、还没有解决的：自动退出路径

| 路径 | 结果 |
|---|---|
| `gpuebctl stop` + `RETURN` | ✅ **32 秒** |
| 直接 `RETURN`（靠 `quiesce` 自动） | ❌ 卡住 |

`quiesce()` 在 `psci_smp_stop_others()` 之后运行——其他 CPU 已停、`DAIF` 全关、
`printf` 不进日志、`delay()` 没有时钟。六步停机需要能轮询的时钟和稳定的总线，
在这个上下文里做不完。

**已按 Adreno/Wi-Fi 的两层架构改（未验证）：**

- 四个有序退出点各加 `hakios_mali610_stop()`：
  `hakios_request_return()`、`loader_return_timer()`、`loader_stop_poll()`、`halt_all_cpus()`
- `quiesce()` 瘦身为纯 panic 兜底：先查 `gMaliStopped`，已置位直接返回；
  否则原子认领后只做裸 MMIO 写，零 `printf`、零锁
- `halt_all_cpus()` 那处已撤回（`PEHaltRestart` 不返回，是死代码）

**下次验证：** 刷入带这些改动的镜像，确认 `M1 done`，不手动 `stop` 直接发 `RETURN`，
计时回 Android。目标是 32 秒。

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
