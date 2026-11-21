# 里程碑 6g · 注入 flush 失败，验证卸载会拒绝（Trial756）

2026-11-21。设备已恢复原厂，`boot_a` 逐字节相同。**阶段 6 的最后一项验收。**

## 结果

```
xnusys-flushfail: the volume's file before -> 1
xnusys-flushfail: unmount with the flush refused -> -1
xnusys-flushfail: the volume's file after the refusal -> 1
xnusys-flushfail: the second unmount -> 0
xnusys-flushfail: after the second unmount -> -1
xnusys-flushfail: PASS
```

内核日志把过程说得更清楚：

```
xnusys-blk: unmount /redwood/lib: the flush was refused on purpose, staying mounted
xnusys-blk: unmount: mp=... from a lookup of /redwood/lib
xnusys-blk: unmount: calling dounmount
xnusys-blk: unmount /redwood/lib -> 0
```

**第一次卸载只有 lookup 和拒绝，没有 `calling dounmount` 那一行**——这正是
"没有调用 dounmount、没有释放挂载资源"的直接证据。第二次才真的卸载。

## 三条验收项

| 项 | 要求 | 实测 |
|---|---|---|
| 第一次卸载 | 因 EIO 被拒 | `-1` |
| 挂载资源 | 未释放，原文件可读且内容正确 | 读到 7814 字节，`fnv=30630d20` |
| 第二次卸载（不重启） | 一次性故障已消耗，真实 flush 成功并卸载 | `-> 0` |

不能只看 `rc=-1` —— Trial755 就是被旧用户程序的 ioctl 不匹配骗过一次。
这次用"原文件仍能读出正确内容"来确认它真的还挂着。

## 两个自己造的坑

**一、故障被前一个程序消耗掉了。** 一次性注入，但 `rc` 里 `unmount2` 排在前面，
它先触发了那次故障。把 `flushfail` 挪到 `unmount2` 之前才对。

**二、更重要的：注入点放错了地方。** 第一版只加在 `XNUSYS_FLUSH` ioctl 里，
而**卸载是直接调驱动函数的，绕过那个检查点**。结果卸载照样成功，测试"通过"得
完全相反。修正后卸载分支自己也检查注入标志。

第二个坑是这一轮最有价值的发现：一个只测 ioctl 的测试，会漏掉真实调用路径。

## 阶段 6 现在的账

| 项 | 状态 |
|---|---|
| 主机写入演练（写入/半径/撕裂/恢复） | ✅ Trial748 |
| 真机专用测试空间（176 MiB，全零，已备份） | ✅ |
| 原厂备份 + 独立读回路径 | ✅ |
| 真机普通写 | ✅ Trial751 |
| 跨重启保留 | ✅ Trial752/753 |
| FUA 写 | ✅ Trial754 |
| 真实 SYNCHRONIZE CACHE(10) | ✅ Trial754 |
| 卸载：内部 flush、非强制、忙碌 EBUSY | ✅ Trial755 |
| 注入 flush 失败 → 拒卸载 → 重试成功 | ✅ Trial756 |
| 突然断电持久性 | 延后（按 codex 判断不冒险） |

软件注入的失败路径已验证。真实设备报错、超时恢复仍未覆盖。
