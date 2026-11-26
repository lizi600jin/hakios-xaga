# 35 分钟连续运行：没有崩，退出仍 34 秒

2026-11-26。`boot-949`，`loader_return_after=3600`。

## 结果

```
longrun: 30 minutes, a sample every 30 s
...
longrun:   samples   26
longrun:   misses    0
longrun:   restarts  0
longrun:   uptime    381 -> 2117
longrun: PASS -- same kernel, still answering, the whole time
```

**26 次采样，没有一次连不上，没有一次重启。**
`uptime` 从 381 秒一路涨到 2117 秒（35 分钟），中间没有归零过——
这是「同一个内核一直在跑」而不是「重启后又活着」的唯一判据。

跑满之后 `RETURN`，**34 秒**回 Android。

## 这一轮验证了什么

| 项 | 之前 | 现在 |
|---|---|---|
| 连续运行 | 1–10 分钟 | **35 分钟** |
| 期间可达性 | 未测 | 26/26 次 |
| 重启 | 未测 | **0 次** |
| 退出 | 30–35 秒 | **34 秒** |

GPU（M1 + M2a + M2b）和 USB（12.31 MB/s）在这 35 分钟里一直是开着跑的。

## 观测脚本

`tools/longrun.sh`，跟着一起进了仓库。

它每次采样做两件事，用**两条独立的连接**：

1. `PING`，8 秒超时 —— `uptime` 是判活的唯一依据
2. `LOG`，30 秒超时 —— 读 1 MB 的环形日志

分开的理由：第一版在同一个连接上先 PING 再 LOG，报了一连串假的 DOWN，
而设备其实答得好好的——1 MB 的日志在一条 12 MB/s 的链路上读，
socket 超时在半路就到了。

**一条链路上做两件时间尺度差很多的事，会把慢的那件的超时算到快的那件头上。**

## 失败模式的匹配也要精确

第一版用 `panic|fault|...` 数「故障行数」，在一个健康的日志上数出 18 个：
`panic_trace` 是启动时的正常输出，`gpu_fault 0` 是个零，`page faults 0` 也是个零。

改成只匹配真正意味着出事的模式（`panic(cpu`、`did not return`、`MMU FAULT`、
`mcu fatal`、`kernel trap`、`brk #`、`Unable to handle`）之后，健康日志上是 **0**。

**计数器的分母错了，比没有计数器更糟——它会让人去查不存在的问题。**

## 还没验证的

- 更长时间（几小时）会不会有内存泄漏。`kern.zone_elems` 可以查，但这一轮没采。
- 温度。电池驱动还没有，读不到。
- 充电状态变化（插拔）时的行为。

## 镜像

`trial736/boot-949.img`，`loader_return_after=3600`（一小时窗口）。
`boot-945.img` 是 USB 最优的那一版，但窗口只有 600 秒。
