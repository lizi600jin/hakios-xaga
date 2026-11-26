# `RETURN` 卡死的真凶：`loader_return.c` 在关核之后调了 `printf()`

2026-11-26，`boot-975`。这是我在 `boot-974` 上遇到的那个「背光写完 `RETURN` 就死」的真正原因。

**不在背光驱动里。** 背光驱动一行没改（`md5 a6b18c0b…`，两份一致），
只改了 `osfmk/arm64/loader_return.c` 的 4 行，问题就没了。

## 现象回顾

`boot-974`：

```
OK returning            ← hakid 应答了
之后 ping 100% 丢包
hakid 五次连接全部 OSError
USB 还挂 18d1:d00d，iProduct = "HakiOS phone"（卡在 XNU，没回 loader）
```

`RETURN` 命令收到了，是之后的清理路径卡住。

## 根因

`loader_return()` 里有四句调试打印（Trial928 加的），在**关掉其他处理器之后**：

```c
printf("return: stage A, about to stop the other processors\n");
psci_smp_stop_others();                     /* ← 其他核 PSCI_CPU_OFF，DAIFSet ALL */
printf("return: stage B, the others are off ...\n");   /* ← 这里开始就危险了 */
```

`printf()` 会走到 `serial_console.c` 的 `console_ring_try_empty(false)`，而那里是：

```c
} else if (__probable(!in_debugger) && !fail_fast) {
        lck_mtx_lock(&console_ring.flush_lock);      /* ← 阻塞式互斥锁 */
}
...
lck_ticket_lock(&console_ring.write_lock, &console_lck_grp);   /* ← 又一个 */
```

**如果任何一个已经被 PSCI 关掉的核刚好持有这把锁 —— 比如它正在打一行 printf ——
CPU 0 就会带着 `DAIFSet ALL` 永久阻塞在里面。**

而 `boot-974` 里恰好有很多 printf 在跑：`hakios_mali610_stop` 连续打约 18 行、
`battery_thread` 每 10 秒一行、`backlightctl` 刚打完、我自己加的 `i2c: bl:` 每次写都打。

**背光不是原因，它只是让「有一行 printf 正在写」的概率变高了。**

`boot-973` 之所以没事，是因为那次 RETURN 碰巧没有撞上。
这并不是「973 安全、974 不安全」，而是**两边都有这个竞态，只是 974 更容易触发**。

## 修法

那四句改成 `loader_log_puts()`：

```c
loader_log_puts("return: stage A, about to stop the other processors\n");
psci_smp_stop_others();
loader_log_puts("return: stage B, the others are off ...\n");
```

`loader_log_puts` → `loader_log_putc`，它用的是**自旋锁加逃生**：

```c
for (uint32_t tries = 0; !locked && tries < 10000000; tries++) {
        locked = os_atomic_cmpxchg(&loader_log_lock, 0, 1, acquire);
}
loader_log_put_locked(c);
if (locked) { os_atomic_store(&loader_log_lock, 0, release); }
```

**拿不到锁就带着 `locked = false` 直接写。** 一万次尝试之后放弃等待，不会永久阻塞。
这和 `lck_mtx_lock` 是本质区别：一个是「等不到就算了」，一个是「永远等下去」。

日志照样进 HLOG，只是不走 console 那条有锁的路径。

## 验证

`boot-975`，同一套六步背光测试，然后 RETURN：

```
status    -> 1649
raw 800   -> 800   (0x04=0x00 0x05=0x64)
raw 400   -> 400   (0x04=0x00 0x05=0x32)
raw 1900  -> 1900  (0x04=0x04 0x05=0xed)
raw 0     -> 200   (0x04=0x00 0x05=0x19)   钳位
restore   -> 1649  (0x04=0x01 0x05=0xce)
```

**RETURN 第 1 次：31 秒。**
**RETURN 第 2 次（先 `raw 700` 再 RETURN，最容易复现卡死的组合）：30 秒。**

两次都干净回到 Android。

## 单变量

| | `hakios.mt6895_i2c.c` | `loader_return.c` | RETURN |
|---|---|---|---|
| `boot-974` | `a6b18c0b…` | 4 处 `printf` | 卡死 |
| `boot-975` | `a6b18c0b…`（**未改**） | 4 处 `loader_log_puts` | **31 / 30 秒** |

只有 `loader_return.c` 变了。

## 我判断错的地方

我在 `boot-974` 之后怀疑过三个方向：背光写撞上清理路径、`i2c6_lock` 死锁、
MMIO 映射失效。**三个都错。**

我当时注意到「`OK returning` 打出来了，之后 ping 不通」，并推断是
`halt_all_cpus` 之后没人处理中断 —— 方向对了一半，但没想到是
**`halt` 之后自己调了 printf**。

一个更值得记的教训：`boot-973` 的 RETURN 正常，我就把它当成「安全基线」。
**它只是没撞上那个竞态。** 把「碰巧通过」当成「没有问题」，会让排查一直停在错误的对比上。

## 镜像

`trial736/boot-975.img` —— 背光可调 + RETURN 正常，**当前基线**。
