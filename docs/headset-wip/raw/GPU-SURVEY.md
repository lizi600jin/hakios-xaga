# GPU 测绘结果与 USB 吞吐的真实瓶颈

回给规划者。按 `Stage 2 路线裁决` 做了静态摸底 + 一轮合并刷机（`boot-905.img`）。

日期：2026-11-24 夜。设备：红米 Note 11T Pro（xaga），MT6895Z / 天玑 8100。

---

## 一、GPU 握手：硬件认出来了，MCU 没起来

> **修正**：第一轮刷机后我读到的是"GPU_ID = 0、八个域全失败"。那是**第一次启动**的日志。
> 设备后来重启了一次，第二次走得更远。下面这份是完整结果。

### 关键数据（Neon 要的）

```
GPU_ID        0xa8670004            ← Mali-G610，真值，不是 0 也不是 0xffffffff
CSF           0x40a0412             ← Command Stream Frontend
L2 features   0x7130306
Shader        0x130013
MMU           0x2830 (va 48 bits, pa 40 bits, address spaces 0xff)
tiler 0x1  l2 0x1  stack 0x33  thread_max 0x800
固件           258048 bytes, header 0.1, 10 个 section
域             7 of 12 answered
```

### 做了什么

`boot-args` 从 `mali=0` 改成 `mali=6`（最高档：全局接口 + 配置 + ping）。

**第一轮没生效**——我改了 `coordinator/loader-build.json`，但 `boot-args` 是编进 **loader** 的，
loader 没重编。重编后有 56 条日志。

### 成功的部分

```
mali: bus protect top axi md0 0 -> 0 (mask 0xf)
mali: bus protect mfgrpc 0xf000f -> 0xf (mask 0xf0000)
mali: bus protect emisys0 0xc0000 -> 0 (mask 0xc0000)
mali: bus protect emisys1 0xc0000 -> 0 (mask 0xc0000)
mali: bpverify md0: sta 0xffffffe0 -> 0xffffffe0 after 0 polls (OPENED)
mali: bpverify emisys0: ... (OPENED)
mali: bpverify emisys1: ... (OPENED)
mali: bpverify md0: ... (CLEARED)
mali: aoc: semaphore 0 after 100000 spins, iso_con 0x1701
mali: aoc: released, iso_con now 0x1101
```

**总线保护全部解除，AOC 电源域上电成功。**

四个域起来了：

```
mali: mfg1  MPU on: 0xc000000d   ✅
mali: mfg2  L2  on: 0xc000000d   ✅
mali: mfg3  ST0 on: 0xc000000d   ✅
mali: mfg4  ST1 on: 0xc000000d   ✅
```

### 第一轮为什么看起来全失败

```
mali: gpu_id as the loader left it 0
mali: mfg5  ST4    no ack after 2000000 reads (0x4000111e)
mali: mfg6  ST5    no ack after 2000000 reads (0x4000111e)
mali: mfg7  ST0T0  no ack ...
...（八个全失败）
mali: 7 of twelve domains answered   ← 实际是 5 个，加上 MPU/L2 是 7
```

**第一轮 `gpu_id` 读到 0**，驱动走冷启动路径。但冷启动之后仍然失败。

**第二轮不一样。** 设备重启后，`gpu_id` 第一次读就到了 `0xa8670004`——
说明 **loader 已经在 GPU 上电状态**，XNU 接手的是一颗活的 GPU。

**这里的教训**：`mali=6` 的冷启动路径和热路径结果差很多。
第一轮可能是 loader 那边冷启动没完成就跳转了。

### 真正的卡点：MCU 不启动

```
mali: the whole block settled, so power and clocks are not the problem
mali: firmware 258048 bytes, header 0.1, entry table ends at 768
mali: 10 sections, 9 page tables
mali: coverage: every section fully mapped
mali: reset vector: sp 0x2040000  pc 0x10018ed
mali: entry words at VA 0x1000000: 0x10019b1 0x10019b1 0xd00cf8df 0xf832f002
mali: firmware loaded; enabling the address spaces
mali: AS0 enabled: root 0x50000000 transcfg 0x42000006
mali: address space 0 is live; starting the MCU
mali: sentinel 0xff5aa5f0 written to the version word
mali: the start loop ended after 1000000 spins (halt seen 0, fatal seen 0)
mali: MCU status 1 (raw state 1; boot unproven), job irq 0, gpu irq 0
mali: version word 0xff5aa5f0 (untouched; no firmware version published)
mali: the global interface never raised; the MCU did not boot
```

**做到哪一步了：**

1. ✅ 电源和时钟稳定（驱动自己确认：`power and clocks are not the problem`）
2. ✅ 固件读进来了，10 个 section 全部映射，页表验证通过（`every section fully mapped`）
3. ✅ reset vector 正确（`sp 0x2040000 pc 0x10018ed`，入口字有真实指令 `0x10019b1`）
4. ✅ AS0 地址空间启用了（`root 0x50000000`）
5. ❌ **MCU 启动请求发出后，`MCU status` 一直是 1，`halt seen 0, fatal seen 0`**
6. ❌ **版本字的哨兵 `0xff5aa5f0` 没被固件改掉** —— 说明 MCU 根本没执行

**这个诊断很干净：固件在内存里是对的（有真实的 ARM64 指令），
地址空间通了，但 MCU 没有开始执行。**

**可能的原因（需要你判断）**：

1. **门铃没敲对。** 日志有 `MCU control reads 0 before the start request (doorbell disable clear)`
   —— doorbell 相关的位可能是反的。CSF 架构里，`MCU_CONTROL` 的 bit 0 是 `MCU_START`，
   但也有 `DOORBELL` 需要单独处理。
2. **`mali: glb req 0 -> 0 (halt was already clear)`** —— 全局接口的 halt 位本来就是清的，
   但也许应该先 set 再 clear，才能产生一个上升沿。
3. **`bus idle after the table walk: qchannel 0x11 debug_sel 0x3 debug_top 0x4 (idle after 0 spins)`**
   —— 但 `after the failed start: debug_top 0x1b (still busy after 100000 spins)`。
   **`0x1b` 的 bit 3 和 bit 4 置了**，说明总线上有东西在等。这可能是 MCU 在取指但卡住，
   也可能是某个 AXI 事务没完成。
4. **中断没接。** `gpu irq status 0`、`job irq status 0`、`mmu irq 0`——一条中断都没来。
   CSF 的 MCU 启动后会通过 `JOB IRQ` 报告，如果中断路径没通，驱动会以为没启动。

**第 3 点我觉得最可疑**：`debug_top 0x1b` 意味着总线忙着，而驱动却在等 MCU 状态位。

### 一个重要的架构事实

**`hakios.mali610.c`（5221 行）没有用户态接口。**

我查过：
- 没有 `cdevsw` 注册
- 没有 `devfs_make_node`
- 没有 `IOService` / `registerService`
- 没有 `copyin` / `copyout`

**它是一个纯硬件探测驱动。**

而用户态那条链路是这样的：

```
boot-arg gpu=1
    ↓
HakiosGPU 服务出现（只由 hakios.adreno.c 提供，不是 mali610）
    ↓
inject.c 的 opengl_es() 检测到服务
    ↓
dlopen /redwood/lib/libhakios.gl.dylib  → src/eagl.c（4205 行，Mesa 后端）
    ↓
CoreAnimation 用 ES 渲染器而不是软件渲染器
```

**所以即使 MCU 起来了，`gpu=1` → `HakiosGPU` → `eagl.c` 这条链路仍然接不上，
因为 `HakiosGPU` 是 Adreno 侧的，`mali610.c` 里没有对应实现。**

`libhakios.gl.dylib` **也不在镜像里**（只有 `src/gl_osmesa.c`，Mesa 的软件后端）。

**这是 Stage 2 真正的缺口，比 MCU 启动更根本。**

---

## 二、USB 吞吐：下行 8–11 倍，上行没变

### 改动

`usb_thread` 原来每轮固定睡 1 ms：

```c
usb_service_once(...);
delay_for_interval(1, NSEC_PER_MSEC);   // 每轮 1 ms
```

改成有活动时只睡 50 µs：

```c
if (ep2_tx_active || ep2_tx_off != before) {
    delay_for_interval(50, NSEC_PER_USEC);   // 忙
} else {
    delay_for_interval(1, NSEC_PER_MSEC);    // 闲
}
```

### 实测（同一镜像、同一时刻）

| 尺寸 | 下行 改前 | 下行 改后 | 提升 |
|---|---|---|---|
| 64 KB | 376 KB/s | 2835 KB/s | **7.5×** |
| 256 KB | 397 KB/s | 4037 KB/s | **10.2×** |
| 1 MB | 403 KB/s | 4570 KB/s | **11.3×** |
| 4 MB | 406 KB/s | 3561 KB/s | **8.8×** |

| 尺寸 | 上行 改前 | 上行 改后 |
|---|---|---|
| 4 MB | 399 KB/s | 401 KB/s（**无变化**） |

`0 errors / 0 dropped`，SHA256 一致。

### 上行为什么没变

**我测了主机侧纯 socket 写入（不经 hakid 协议）：1 MB = 413 KB/s，4 MB = 380 KB/s。**

**瓶颈不在 hakid，不在 TCP 分块，在手机侧的接收路径。**

`ep2_out_service` 每轮最多收 8 个包（`for (round = 0; round < 8u; round++)`），
每收满一帧就 `hakios_net_input_unit()` 交给协议栈。

**可能的原因**：

1. `ep2_out_service` 的 8 轮上限，收满就 `break`
2. `hakios_net_input_unit()` 是同步调用，协议栈处理慢就阻塞整个 USB 线程
3. **RX2CSR0 的 `EP_RXPKTRDY` 清除到下一个包可读之间有硬件延迟**

### 关于另一位开发者的 25 MB/s

**这个参照值很有价值，它说明我们的 4.5 MB/s 不是天花板。**

我算了一下当前的数学：

```
512 字节/包 ÷ 50 µs = 9.8 MB/s 理论
实测 4.57 MB/s → 每轮实际约 110 µs（50 µs 睡眠 + 60 µs 处理）
```

**要达到 25 MB/s，每轮必须 < 20 µs。**

而 PIO 的根本限制是：`EP_TXPKTRDY` 一次只能提交一个包，
每提交一个包都要读 CSR + 写 FIFO（512 字节 = 128 次 32 位写）+ 写 CSR。

**128 次 MMIO 写本身就在 20–50 µs 量级。PIO 到不了 25 MB/s。**

**结论：25 MB/s 只能是 QMU/DMA 路径。** Neon 之前在 `stage1_decisions.md` 里说
"QMU 留到性能瓶颈触发时再做"——**现在就是那个时刻**。

代码里已经有 QMU 的痕迹（22 处提及，`U3D_TXQCSR2`、`qfcr 0x1ff01ff` gen2 qmu），
但 `DMAREQEN` 一直是 0（PIO 模式）。

---

## 三、CPU 核型：Neon 的怀疑不成立

```
GenericARMCPU: cpu 0 (phys id 0x0) registered
GenericARMCPU: cpu 1 (phys id 0x400) registered
GenericARMCPU: cpu 2 (phys id 0x500) registered
GenericARMCPU: cpu 3 (phys id 0x700) registered

cpu 0 (phys id 0x0) is a slower core: the scheduler uses it after cpu 3 (phys id 0x700)
GenericARMPE: the slower cores started too: 4 processors running
```

天玑 8100 的 MPIDR：A55 = `0x000`–`0x300`，A78 = `0x400`–`0x700`。

**`cpus=4` 跑的是 3 颗 A78 大核（0x400、0x500、0x700）+ 1 颗 A55 小核（0x0）。**

**不是全小核。** 大核已经在用了，界面 9 fps 是软件渲染的固有速度。

**没启用的是**：`0x100`、`0x200`、`0x300`（三个 A55）和 `0x600`（一个 A78）。

`machine_routines.c` 的注释说：`cpus=8` 时「cores 3, 2, 1 配 4, 5, 6，
7 是 boot 核和最快的核，独自一个」。

**我的建议**：开 `cpus=8` 的风险低（PSCI CPU_ON 是标准路径），
但**对软件渲染帮助有限**——着色器是标量运算，多 4 个小核帮不上。
可以顺手试，但别期待翻倍。

---

## 四、lockdownd / afcd：查不到

```
LS /usr/libexec                  → 0 项
LS /System/Library/LaunchDaemons → 0 项
```

两个目录都是空的。但 `/redwood/LaunchDaemons` 里有 23 个 plist——
**这个移植把系统做成了 `/redwood` 前缀的扁平结构，不是标准的 iOS 目录布局。**

所以"lockdownd 在不在"这个问题，用标准路径查不出答案。
要查得先搞清 `/redwood` 下二进制的组织方式。

**留档结论**：标准 iOS 路径下不存在，需要按 `/redwood` 结构重查。

---

## 五、需要你判断的三件事

### 1. GPU 的下一步是补电源序列，还是先补用户态接口？

我倾向**先补电源序列**（让 GPU_ID 能读出来），因为：

- 电源序列是纯内核的事，一轮刷机能验
- 用户态接口（`HakiosGPU` + `eagl.c` + dylib 编进镜像）是大工程

但反过来想：**即使 GPU 起来了，没有用户态接口也用不上。**
如果目标是"界面变流畅"，两件事都得做，顺序可以商量。

**如果是"先证明硬件活着"，那补电源序列就够了。**

### 2. MCU 不启动，你有具体线索吗？

现在的状态很干净——电源、时钟、固件、页表、reset vector 全对，
就是 MCU 不执行。我列的四个可能：

1. **门铃（doorbell）位搞反了**：`MCU control reads 0 before the start request
   (doorbell disable clear)` —— CSF 里 `MCU_CONTROL` 的 bit 0 是 START，
   但 DOORBELL 可能需要单独的上升沿
2. **`glb req 0 -> 0 (halt was already clear)`** —— 全局接口 halt 位本来就是清的，
   也许应该先 set 再 clear 才有边沿
3. **`debug_top 0x1b`（bit 3/4 置位）** —— 总线忙着，但驱动在等 MCU 状态位。
   可能 MCU 在取指但卡住，也可能是某个 AXI 事务没完成
4. **一条中断都没来**（gpu/job/mmu irq 全 0）—— 如果中断路径没通，
   驱动会误判"没启动"

**最有用的信息是**：Mali-G610（CSF 架构）的 MCU 启动序列里，
`MCU_CONTROL` 的准确位定义，以及启动前后 `JOB IRQ` 应该看到什么。

### 3. USB 上到 25 MB/s 要上 QMU，现在做还是等？

**现在的 4.5 MB/s 已经能用了**（1 MB 文件 0.2 秒）。
25 MB/s 是锦上添花，不是瓶颈。

但如果你觉得后面要频繁传大文件（抓 framebuffer、推 dylib），
那 QMU 值得投一轮。

---

## 六、环境与文件

- 当前镜像：`boot-905.img`（`mali=6`，USB 按需休眠）
- 回退：`boot-904.img`（Stage 1 稳定版）、`boot-804i.img`
- 关键教训：**改 `boot-args` 必须重编 loader**，`coordinator/loader-build.json`
  改完不会自动进 loader
