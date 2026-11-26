# 这一夜：GPU 到了「能执行」，USB 快了 175 倍

2026-11-25 夜 → 26 日。你睡前说继续推进，这是结果。

---

## 当前最稳可用基线

| 项 | 值 |
|---|---|
| 镜像 | `trial736/boot-948.img`（最新）／`boot-945.img`（USB 最优那一版） |
| boot-args | **`mali=6` 一个参数**就够了 |
| GPU | M1 固件 + M2a 一组一流 + M2b 取指执行写内存，全部默认开启 |
| USB | 下行 **5.10 MB/s**（13×）、上行 **12.31 MB/s**（175×）、0% 丢包 |
| 退出 | 自动 `RETURN` → **30–35 秒**回 Android |
| `/private/var` | 64 GiB 可用，内核侧已证明正确 |

`mali=0` 仍是零 GPU 干扰的安全模式；`mali_m2=0` 可单独关掉 M2 那两层。
两个都已验证过。

---

## GPU：从「固件在跑」到「GPU 真的执行了指令」

上一轮的终点是 M1（固件起来了、全局接口应答了）。这一夜往前走两层。

### M2a：一组一流，带 ring buffer

```
22.246475  after one doorbell, 1 ms: csg ack 0x11 cs ack 0x1
22.246492  group 0 stream 0 running, ring 0x4060000
```

一个门铃，一毫秒，组和流都起来了。

### M2b：GPU 执行指令、写回 DRAM

```
22.247635  before step B: sync 0x1000000000000000 cycles 0
22.248772  step B: sync 0x10000000cafebabe (want 同值) cycles 0x2c1b975
22.248783  PASS -- the GPU executed instructions and wrote memory
```

`sync` 变成 `0x10000000cafebabe`，`cycles` 是一个实时的硬件周期计数
（连跑三次得到 `0x2c1b975`、`0x65930c3`、`0x35dd658`，每次不同）。

**这两处写入都在 DRAM 里，是 GPU 自己写的。**

分两步做的理由：Step A 是八个 NOP，NOP 里没有会出错的东西，
所以 `extract` 从 0 走到 64 就说明取指路径本身通了。Step B 才是真指令，
到那一步才有东西可能失败，而失败原因已经被 Step A 排除一大半。

---

## USB：0.07 → 12.31 MB/s

| | 之前 | 现在 | 倍数 |
|---|---|---|---|
| 下行 | 0.40 MB/s | **5.10 MB/s** | 13× |
| 上行 | 0.07 MB/s | **12.31 MB/s** | 175× |
| 丢包 | 60% | **0%** | — |
| RTT | 1.96 ms | **1.02 ms** | — |

两处改动，缺一不可：

**① RX FIFO 从 512 字节扩到 4 KB。** 原来 `segsize 9` 正好一个高速 bulk 包，
接收侧一点余量都没有——驱动搬一个包的时候，主机发的下一个包无处可落。

**② 忙等的判断条件漏了接收侧。** 原来只看发送端点，下载时发送端点空着，
每一轮都走「睡 1 毫秒」的分支。一轮一个帧、一帧一毫秒。

---

## 三条值得单独记下来的

### 1. 这个平台上所有 `delay()` 都会睡眠

这条会影响以后每一个驱动。

```
delay() → delay_for_interval() → _clock_delay_until_deadline_with_leeway()
        → ml_delay_should_spin()
```

`ml_delay_should_spin()`（`machine_routines.c:2449`）靠 `cpu_idle_latency` 判断，
而它是 **0**：`cpu.c:807` 清成 0，唯一会写它的 `machine_routines.c:1501` 读
`in_processor_info->powergate_latency`，**全树没有任何地方填这个字段**。

所以 `ml_delay_should_spin()` 永远返回 FALSE，**每一个 `delay()` 都进定时器，
50 微秒的请求回来是毫秒级**。

以后需要真微秒级短等的地方，用 `mach_absolute_time()` 自己数：

```c
static void
usb_spin_us(uint32_t usec)
{
	uint64_t deadline;

	clock_interval_to_deadline(usec, NSEC_PER_USEC, &deadline);
	while (mach_absolute_time() < deadline) {
		;
	}
}
```

### 2. GPU CSF 的协议纪律

四个坑，全部实测踩过：

- **两级门铃是 toggle 协议。** 光写 `0x80000` 不够，还要翻转
  `glb_in->doorbell_req`，因为固件醒来会比对 `REQ` 和 `ACK`，相等就当作没有请求。
  而翻转成「已经在那里的值」也不构成请求——所以**一次提交只敲一次门铃**，
  敲第二次是空的。
- **`CS_USER_INPUT` / `CS_USER_OUTPUT` 必须各占独立的 4 KiB 页。**
  寄存器定义的原话是 *"user mode input/output **page** address"*，
  塞在同一页的不同偏移不行。
- **`ringbuf_size` 至少 4 KiB。** 按页粒计算，且必须是 2 的幂。
- **看 `CS_ACK` 不能只看 bit0。** `0x40000001` 的 bit0 是 `CS_STATE_START`，
  但 bit30 是 `CS_FATAL`——我当时只看了 bit0，把它当成功了，
  下一轮才发现 `cs_out->fatal` 是 `0x40`。

顺带：`status_endpoint_current` 和 `status_cmd_ptr` 恒为 0 是**正常的**，
它们只在主机翻转 `CSG_REQ_STATUS_UPDATE` 之后才回填。

### 3. `extract` 推进不证明你的指令被执行了

`boot-934` 看着一切都对：`extract` 从 64 推到 128，`fault 0 fatal 0`。
但 `sync` 和 `cycles` 一个字节都没变。

原因是 `cmds` 指针没加 64——Step B 的八条真指令写回了偏移 0（已经被执行过的地方），
而 64..127 里还是 `memset` 留下的全零 NOP。**CEU 老老实实执行了八条 NOP。**

`extract` 推进是真的，它证明取指在工作；**但它不证明你写进去的东西被执行了**，
中间隔着一个「写到哪个偏移」的问题。

---

## 方法论：单变量 + 独立观测量

这一夜最有用的一条，比任何技术细节都值钱。

**反例（`boot-937`）**：一次改了四处（队列深度、`memcpy`、自旋、统计打印），
设备卡在 `18d1:d00d`，网卡在但 `hakid` 不响应，回退基线重刷才恢复。
**定位不出来是哪一处。**

**正例（`boot-940` → `945`）**：

| 轮次 | 改了什么 | 结果 |
|---|---|---|
| 940 | 只改队列 8→32 | 稳，速度不变 |
| 941 | 只加自旋 | 稳，速度不变 |
| 942 | 只改 FIFO | 稳，**丢包 60%→0%**，速度不变 |
| 943 | 只改忙等条件 | **4.92 MB/s** |
| 945 | 等待 50→8 µs | **上行 12.31** |

单变量改动比一次改完再回退省时间。

**另一条：独立观测量胜过读代码猜。**

`/private/var` 的 `Available: Zero KB`，我先猜了三个地方
（`hfs_freeblks`、`reserveBlocks`、`vfs_attrlist` 的乘法），**全错**，
而且都是读代码猜的。

写一个 34 KB 的探针，在同一进程里问两次，十分钟出结果：

```
statfs       f_bavail × f_bsize = 67,940,995,072 = 63.28 GiB
getattrlist  ATTR_VOL_SPACEFREE = 67,940,995,072 = 63.28 GiB   ← 一字不差
```

**读三小时代码，不如问一次。**

---

## 已定位、暂缓的

| 项 | 状态 | 为什么暂缓 |
|---|---|---|
| `/private/var` 显示 `Zero KB` | 内核两条路径都正确（63.28 GiB），问题在用户态 | 最可能是 `NSURLVolumeAvailableCapacityForImportantUsageKey` 走 `CacheDelete`，那是另一条完全不同的路径，不是几行能收的 |
| GPU M2c（compute shader） | 未做 | 要 Valhall shader 二进制、DCD/SRT 描述符、AS1 页表，是一整条 Mesa 移植线。跑通后离实用仍差整个用户态编译器 |
| USB QMU/DMA | 未做 | 12.31 MB/s 已过决策门，不值得冒风险 |
| 首轮复位原因 | 缺证据 | 看门狗和人工复位无法区分 |

---

## 这一夜的所有提交

```
1f7bead  /private/var 的两条查询路径都对
3961e96  USB 上行再翻倍：6.11 → 12.31
f55f7f9  USB Phase 1 成功：0.38 → 4.92
2f918ec  USB Phase 1：delay() 在这个平台永远睡眠
1d71a8d  M2b：GPU 执行了第一条指令
4bdb3e4  M2a 完成：一组一流跑起来
13e44b5  M2a：CSG 和 CS 起来了
9b760e3  能力清单补上 M2a
44ba4ef  /private/var 的 statfs 实测
4f644e8  固化 mali_gpuebpower 默认开启
37b7408  自动退出打通
```

详细数据都在 `GPU-USB-RESULTS.md` 和各轮的 `raw/*/README.md` 里。

---

设备现在是干净的 Android，`boot_a` 是 `boot-948`（想继续就刷 `boot-945`，USB 最优）。
