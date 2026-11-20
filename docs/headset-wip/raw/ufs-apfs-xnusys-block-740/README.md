# 里程碑 4a · 只读块设备（Trial740）— 设备侧成立，挂载差时序

2026-11-21。xaga，`boot_a`，三轮刷入后完整恢复原厂。

## 成立的部分

**`/dev/xnusys` 已在真机注册**：

```
xnusys-blk: /dev/xnusys 608448 blocks of 4096, read only, bmajor=2
```

它把读请求转给新加的 `hakios_mt6895_ufs_mount_read()`，后者走自己的 `VRQ_MOUNT_READ` scope：
整块、在分区内、往返校验。写一律 `EROFS`。

**挂载调用确实到达了**：

```
apfs: mounting /dev/xnusys on /redwood/lib failed: 2
```

## 没成立的部分

`kernel_mount` 返回 `ENOENT`。原因是时序：`apfs_volume_mount()` 在 `bsd_init` 里跑，
那时 ramdisk 还没挂到 `/redwood`，所以给它的挂载点不存在。

这个入口本来就是给「卷就是根」写的，不是给「在旁边挂一个卷」用的。**块设备和读取路径都不用改**，
要改的是挂载发生在什么时候。

## 这一轮真正的收获：解开了 739 的谜

上一轮内核只留 609 字节、守卫拦停，原因一直没查明。这一轮用二分隔离定位了：

| 只开 | 内核日志 | 结果 |
|---|---|---|
| `xnusys_blk=1` | 509 419 B | 正常启动 |
| `xnusys_blk=1 apfs_check=6` | 665 B（停在 loader 的 `0xfeedface` 标记） | 日志丢失 |

**`apfs_check=6` 是元凶**，不是我的枚举代码。它从 `bsd_init` 里调 `loader_return()`，
那条路上内核日志没进存储。739 那轮我恰好在试枚举的同时开了它。

所以 739 的失败和目录枚举无关，枚举本身是好的（11 项断言今天仍然全过）。

顺带找到 `apfs_volume=` 这个入口：同一套挂载，但不走 `apfs_check` 的 mode 分支，因此日志正常。

## 主机回归

```
-O2 -Wall -Wextra -Werror   35 PASS
ASan + UBSan                35 PASS   0 违规
test-ufs-dir-enum           11 PASS
test-ufs-path-walk           8 PASS
```

新增的 5 条断言覆盖：单块读、四块连续读各自落在正确位置、非整块拒绝、越界拒绝、坏命令不发布。

## 原厂恢复

64 MiB 读回**逐字节等于原厂**，Android 40 秒起来，SELinux Enforcing。
