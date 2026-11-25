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

## 镜像

`boot-937.img`：`mali=6`（M1 + M2a + M2b 全默认开启，PASS），
USB 用真自旋，队列 64。

M2b 在这一轮仍然 PASS，说明 USB 改动没有影响 GPU 路径。
