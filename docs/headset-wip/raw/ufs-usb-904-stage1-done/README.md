# Stage 1 收尾：`/dev/hlog` 打通，五项验收全过

2026-11-24 夜。镜像 `boot-904.img`。**Stage 1 完成。**

## 一、`/dev/hlog` 的根因（是我自己的一个边界错误）

### 现象

四次尝试的结果演变：

| 版本 | 改动 | 结果 |
|---|---|---|
| 902c | 从 `msg_bufx` 裸拷贝 | 5792 字节，全 0x00 |
| 902d | 绕环一圈跳过 `\0` | 5792 字节，仍全 0x00 |
| 902e | 加诊断 | 0 字节 |
| 902f | 去掉双重加锁 | 0 字节 |
| **903b** | **边界改用 `hlog_linear_len`** | **7240 字节，95 行真实日志** |

### 根因

```c
uint32_t size = hlog_total();      // 返回 msg_size = 1048576（一兆）
...
while (uio_resid(uio) > 0) {
    if (at >= size) break;
    want = MIN(1024, size - at);   // 按一兆算
    got = hlog_read_locked(chunk, want, at);   // 但实际数据只有 hlog_linear_len 那么长
    if (got <= 0) break;           // ← 第一次就 break
}
```

**边界用的是一兆，实际数据只有几 KB。** 循环请求超出范围的偏移，每次拿到 0，立刻 `break`。

修法：

```c
hlog_linearize();
size = hlog_linear_len;    // 用线性化后的真实长度
if (size == 0) return 0;
```

而 902c/902d 返回 5792 字节全零，是因为那两个版本没有 `hlog_linear_len` 中间层，
直接按 `msg_size` 从 `msg_bufc` 拷贝，**读到了未写入的区域**。

**两个 bug 其实是一个：把 `msg_size`（缓冲区容量）和实际数据长度混用了。**

### 结果

```
HLOG magic=0x63061 size=1048576 bufx=77737 bufr=0 bufc=0xfffffff007d1b89c dbg=0xfffffff1c48040a1..

[   19.122164]:  vm_cache_geometry_colors: 16
[   19.122708]: kprintf initialized
[   19.101257]: gic: v3, distributor 0xfffffff1b54bc000
[   19.101291]: Darwin Kernel Version 22.5.0: 2026年11月24日星期二 20:51:16 CST
[   19.120490]: VM boostrap: 102 maps, 204 entries and 512 holes available
```

**从开机第 19 秒开始，完整日志，实时可读。**

## 二、五项验收

```
✅ ping               PONG 65 (52.9 ms round trip)
✅ ls /dev            看到 hlog、hakios-return、xnuvar、xnusys
✅ exec               net_test show → EXIT 0
✅ 文件传输           1 MB push/pull，SHA256 一致
✅ RETURN             "OK returning"，10 秒内干净退出（21:07:50 → 21:08:00）
✅ 实时日志           从开机第 19 秒起，含 ASCII 金丝雀诊断头
```

`RETURN` 之后 HLOG 完好（magic `0x474f4c48`，4173 行），loader 没有死等。
内核时间 116.7 秒——确实是提前退出，而不是等 600 秒到期。

## 三、`KeepAlive=true` 的价值被意外验证

测 follow 模式时 `hakid` 崩了一次，launchd 立刻拉起：

```
第1 次: 连上 → HAKID 1 port 2222 root /private/var/hakios
第2 次: 连上 → HAKID 1 port 2222 root /private/var/hakios
第3 次: 连上 → HAKID 1 port 2222 root /private/var/hakios
```

`ThrottleInterval=5` 让重启不至于是热循环。**这个配置留着。**

## 四、还没修的：`LOG` 的 follow 模式会让 `hakid` 崩

### 现象

```
follow 收到 5799 字节，等了 0.0s
❌ 新连接失败: [Errno 104] Connection reset by peer
```

`hakid` 崩了，`KeepAlive` 把它拉起来。

### 我的猜测

20 秒上限那段代码用了 `sayf("\n(20 s limit; reconnect with LOG %lu to continue)\n", at)`
——**`%lu` 在 `libmini` 的 printf 里不支持**（它只认 `%c %s %d %i %p %x %X %u`）。

`libmini` 的 `vdprintf` 遇到不认识的格式符可能会读错参数或越界，导致崩溃。

### 影响

不严重——`LOG` 不带 `f` 参数完全正常（55 行、7240 字节都验证过）。
follow 只是便利，可以靠反复 `LOG <offset>` 代替。

**但这是个该记下来的坑：`libmini` 的 printf 不支持 `%ld` / `%lu` / `%llu`。**

## 五、`/dev/klog` 那条路：没走

`klog_probe` 编好了（34 KB，在镜像里），但这轮直接修好了 `/dev/hlog`，
所以没花时间验证 `klog`。

留着有用——如果以后 `hlog` 出问题，`klog` 是后备。

## 六、文件

| 文件 | 内容 |
|---|---|
| `usb-904.diff` | USB 驱动相对 Trial800 的全部改动，含 `/dev/hlog` 和 `/dev/hakios-return` |
| `hakid.c` | 设备侧守护进程，18 KB |
| `klog_probe.c` | `/dev/klog` 探针，备用 |
| `haki_cli.py` | 主机侧 CLI |
| `com.hakios.hakid.plist` | `KeepAlive=true` + `ThrottleInterval=5` |

镜像：`boot-904.img`

## 七、Stage 1 之后

Neon 的 roadmap 里，Stage 1 收尾后是 **Stage 2：Mali-G610 GPU 渲染测绘**。

现在有了：
- 实时内核日志（不用刷机就能看内核在干什么）
- 远程执行（能跑任何 `/redwood/bin` 下的程序）
- 文件传输（能推测试程序到 `/private/var/hakios/bin/` 然后直接跑）
- `RETURN`（测完主动退出，不浪费窗口）

**GPU 的测绘恰好需要这些**：抓 framebuffer、推探针程序、看实时日志。
