# 里程碑 6h · 并发读加锁，以及一次失败的对照实验（Trial757）

2026-11-21。设备已恢复原厂，`boot_a` 逐字节相同。

## 加锁

`xnusys_strategy` 和写入/flush 的硬件调用现在各持一把互斥锁
（`lck_mtx`，参照 `hakios.virtio_blk.c` 的写法）。理由：下面的租约是单命令准入，
`us_admit()` 是从 IDLE 的 compare-and-swap，第二个线程撞上就是直接拒绝。
挂载后的卷由 page-in 路径读取，可以多个线程同时来。

## 结果

```
xnusys-conc: single reader -> 1
xnusys-conc: child done, 0 of 12 reads failed     × 4
xnusys-conc: 4 readers x 12 reads each
xnusys-conc: PASS
```

## 一个把自己卡死的错误

第一版把锁加在整个 ioctl 外面，包括 `XNUSYS_MOUNT`。结果**第一次挂载就死锁**：
日志停在 `mount ioctl seen` 之后再无输出，重启两次都一样。

原因：`kernel_mount("apfs", ...)` 挂载时要从这个设备读块，那会走回
`xnusys_strategy()`，它要同一把锁——自己等自己。

修正：锁只包住**真正碰控制器**的调用（strategy、写、flush），
MOUNT/UNMOUNT 不加，因为它们会经 VFS 重入回来。

## 对照实验没做成

按实验设计，去掉锁再跑一遍应该失败，才能证明锁有用。**但去掉锁也 PASS。**

所以这个测试没有证明锁的必要性。原因很可能是测试的压力不够：四个进程串行 fork、
每个读 12 次，读的都是同一批块，实际重叠窗口很小。真要证明，得让多个线程真正
同时发起请求，或者用 page-in 那种高并发路径打。

**这条如实记下来。** 加锁本身是防御性正确的（互斥的是单实例硬件），
但"没有它就会坏"目前是推断，不是观测。

## 回归

```
-O2 -Wall -Wextra -Werror   38 PASS
ASan + UBSan                38 PASS   0 违规
```
