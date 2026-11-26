# USB Phase 1：查清了 delay() 为什么不可靠，但下行仍是 0.4 MB/s

2026-11-26 凌晨。`boot-936` / `boot-937`。

## 一个值得记下来的内核事实

**这个平台上所有 `delay()` 调用都会进定时器睡眠，不管请求多短。**

链条是这样的：

```
delay(usec)
  → delay_for_interval(usec, NSEC_PER_USEC)        clock.c:1628-1636
  → _clock_delay_until_deadline()
  → _clock_delay_until_deadline_with_leeway()      clock.c:1487
  → 问 ml_delay_should_spin(interval)              决定自旋还是睡眠
```

`ml_delay_should_spin()`（`machine_routines.c:2445-2458`）：

```c
if (cdp->cpu_idle_latency) {
        return (interval < cdp->cpu_idle_latency) ? TRUE : FALSE;
} else {
        /* Early boot, latency is unknown. Err on the side of blocking. */
        return FALSE;
}
```

而 `cpu_idle_latency` 在这个平台上是 **0**：

- `cpu.c:807` 把它清成 `0x0ULL`
- 唯一会设它的是 `machine_routines.c:1501`，读 `in_processor_info->powergate_latency`
- 全树搜索 `powergate_latency`，除了这一处读取没有任何地方写它

所以 `ml_delay_should_spin()` 永远返回 FALSE，**每一个 `delay()` 都走阻塞路径**。
一次 50 微秒的请求回来时已经是毫秒级。

`delay(50)` 和 `delay_for_interval(50, NSEC_PER_USEC)` 是同一个东西——
以为换了函数就换了行为，是错的。

## 试过的

**`usb_spin_us()`**：用 `clock_interval_to_deadline()` + `mach_absolute_time()` 直接数时间基，
绕开整条 `delay()` 路径。

```c
static void
usb_spin_us(uint32_t usec)
{
        uint64_t deadline;
        uint64_t now;

        clock_interval_to_deadline(usec, NSEC_PER_USEC, &deadline);
        do {
                now = mach_absolute_time();
        } while (now < deadline);
}
```

`boot-937` 用它替掉了主循环里的 `delay(50)`。

## 结果：没有变化

```
PUT 4 MB:  10.09 秒 → 0.40 MB/s     (boot-937，真自旋)
PUT 4 MB:  10.11 秒 → 0.40 MB/s     (boot-936，delay(50))
PUT 4 MB:  10.11 秒 → 0.40 MB/s     (改动之前)
```

**三次差在 20 毫秒以内。** 那个延时根本不是瓶颈。

## 顺带确认的

- `ECM_TX_QUEUE_DEPTH` 8 → 64，`ecm_tx_pop()` 逐字节改 `memcpy`：同样没有影响下行
- 链路是 `speed 3` = High Speed（480 Mbps），总线上完全够
- `ep2_out_service()` 一次最多处理 8 个包，看起来不是硬限

## 下一步该查什么

0.40 MB/s 恰好是 **512 字节 / 1.25 毫秒**。这个数字太整齐，不像是总线限制。

可能的地方：

1. **`ep2_fifo_read()` 的逐字节/逐字读**——如果每个字节都是一次 MMIO 读，
   512 字节就是 512 次总线访问，慢得合理
2. **每包一次 `mac_wr(U3D_RX2CSR0, ...)` + `mac_wr(U3D_EPISR, ...)`** 的开销
3. **RX FIFO 只有单缓冲**（`RX2CSR1` 的槽位数），一次只能装一个包
4. 主循环每轮只调一次 `ep2_service()`，而它内部的 8 轮受 FIFO 容量限制

**没有测量的地方就不要猜。** 下一步应该在 `ep2_out_service()` 里数：
一轮主循环处理了几个包、`ep2_fifo_read()` 花了多少时间。


## boot-943：0.38 → 4.92 MB/s（13 倍），上行 0.07 → 6.11 MB/s（87 倍）

```
PUT 4.0 MB in 0.81s → 4.92 MB/s      （原来是 10.09s → 0.40）
GET 4,194,304 B in 0.65s → 6.11 MB/s （原来是 60.89s → 0.07）
ping -i 0.02: 0% 丢包
```

### 两处改动，缺一不可

**① RX FIFO 从 512 字节扩到 4 KB**

`EP_FIFO_SEGSIZE` 是 9（2^9 = 512），正好一个高速 bulk 包的大小。
接收侧一点余量都没有：驱动把一个包搬进内存的时候，主机发的下一个包无处可落，控制器就丢掉了。

证据是 ping 的速率依赖：

```
间隔 0.5 s  → 0% 丢包
间隔 0.2 s  → 0% 丢包
间隔 0.05 s → 0% 丢包
间隔 0.05 s（20 个）→ 60% 丢包
```

低速完全不丢，高速丢 60%——典型的缓冲不足。

改成 `EP_FIFO_SEGSIZE = 12`（4 KB，八个包），TX 和 RX 的地址分别挪到
`0x2000` / `0x0000` 让两个 4 KB 窗口不重叠。字段是四位宽，上限 32 KB。

**改完丢包从 60% 降到 0%，但速度还是 0.38 MB/s。**

**② 忙等的判断条件漏了接收侧**

主循环原本是：

```c
if (ep2_tx_active || ep2_tx_off != before) {
        /* 忙：短等再看 */
} else {
        /* 闲：睡 1 毫秒 */
}
```

**只看了发送侧。** 下载的时候发送端点没有东西排队，于是每一轮都走睡眠分支。

一轮最多搬一个帧，一帧一毫秒，最好也是 1.4 MB/s。而一个 TCP 段是三个包，
所以出来是 0.4 MB/s——所有测量都对得上。

把接收包计数也算进去：

```c
uint32_t before_rx = ep2_rx_packets;
...
if (ep2_tx_active || ep2_tx_off != before || ep2_rx_packets != before_rx) {
```

`ep2_rx_packets` 只在真的有包到达时变化，所以空闲链路仍然会睡，仍然把 CPU 还回去。

### 为什么先后顺序很重要

先修 FIFO 的时候速度一点没变（0.38 → 0.38），很容易以为 FIFO 那条不是问题。
**其实两个都是问题，只是 FIFO 那个被后面这个盖住了。**

丢包率是单独的信号：它证明 FIFO 确实不够，即使吞吐被另一个瓶颈压着看不出来。
**分开测量，不要只看吞吐。**

### 中途挂过一次

`boot-937` 之后设备卡在 `18d1:d00d`，网卡在但 `hakid` 不响应。
回退到基线重刷才恢复。那轮同时改了四处（队列深度、memcpy、自旋、统计打印），
定位不出来是哪一处。

后来改成一次只改一处：`boot-940` 只改队列深度、`boot-941` 只加自旋，
两次都稳，速度都不变。**一次只改一处，比一次改完再回退省时间。**


## boot-945：上行再翻一倍（12.31 MB/s），下行 5.10

```
DOWN  PUT 3.0 MB  →  4.90 / 5.10 MB/s
UP    GET 3.0 MB  →  12.31 MB/s
ping -i 0.01: 0% 丢包, RTT 1.02 ms（原 1.96）
RETURN: 35 秒
```

### 每包硬等 50 微秒就是天花板

原来的忙等分支无条件 `usb_spin_us(50)`。一个 512 字节的包加 50 微秒的空转，
`512 / 50us = 10 MB/s` 是绝对上限，实测 6.11——正好是这个数被读写开销再削一层。

### 三个状态，不是两个

```c
if (有包移动) {
        usb_spin_us(8);          /* 链路活跃：稍等再看 */
        last_busy = now;
} else if (距上次活跃 < 250us) {
        usb_spin_us(8);          /* 会话中的间隙：不是空闲 */
} else {
        delay_for_interval(1, NSEC_PER_MSEC);   /* 真空闲：把 CPU 还回去 */
}
```

`250 us` 是主机 TCP 窗口开着时两个包之间的间隔量级。比这更短的安静不是空闲，
是一段还在继续的会话里的停顿，在那里睡一毫秒等于扔掉十几 KB 的带宽。

### 8 微秒是怎么定下来的

先试了零等待：

| 等待 | 下行 | 上行 |
|---|---|---|
| 50 us（943） | 4.92 | 6.11 |
| **0 us**（944） | **3.55** | **13.43** |
| **8 us**（945） | **5.10** | **12.31** |

**零等待对发送更好，对接收更差。** 发送侧上一个包一走下一个就在手边；
接收侧主机发一串然后等 ACK，循环若从不暂停，那些微秒全花在读空 FIFO 上，
是跟正在到达的包抢总线。

8 微秒是两边都保住的值：覆盖 FIFO 重新填充的时间，
又不到一个 512 字节包耗时的百分之一。

### RTT 从 1.96 降到 1.02

这是同一件事的另一个侧面。之前每个包都要等主循环的下一次机会，往返被拉长；
现在活跃期间循环几乎不停，往返自然短了。

## 与 TCP 理论上限的距离

High Speed bulk 的理论吞吐约 35 MB/s（480 Mbps 扣掉协议开销）。现在上行 12.31，
是理论值的 35%。剩下的差距在：
- 每一轮仍然只处理一个帧
- `ep2_in_service()` 一次只发一个包
- 每次提交都绕回 `ep0_poll()` 和 `COMMON_USB_INTR` 的 MMIO

这些是可以继续压的，但 12 MB/s 已经足够日常使用——4 MB 只要 0.3 秒。

## 镜像

`boot-937.img`：`mali=6`（M1 + M2a + M2b 全默认开启，PASS），
USB 用真自旋，队列 64。

M2b 在这一轮仍然 PASS，说明 USB 改动没有影响 GPU 路径。
