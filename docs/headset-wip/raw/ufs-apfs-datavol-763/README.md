# 里程碑 6i · 数据卷设备：两个真 bug 修好了，读仍未通（Trial763）

2026-11-22。设备已恢复原厂，`boot_a` 逐字节相同。本次会话第五次恢复。

## 修好的两个 bug

### 一、`d_psize` 返回块数，不是块大小

`set_blocksize()` 把 `d_psize` 的返回值直接存进 `vp->v_specsize`：

```c
if ((major(dev) < nblkdev) && (size = bdevsw[major(dev)].d_psize)) {
    rsize = (*size)(dev);
    vp->v_specsize = rsize;      /* 直接存，不换算 */
}
```

而 `spec_read()` 的 VBLK 分支拒绝超出页大小的值：

```c
devBlockSize = vp->v_specsize;
if (devBlockSize > PAGE_SIZE) return EINVAL;
```

我的 `xnusys_var_size` 返回 `XNUSYS_VAR_BYTES / XNUSYS_SECTOR` = **356352**，
远超 4096。所以每次读都在到达驱动之前就返回 `EINVAL`。

对照现有实现：`memdev.c` 的 `mdevsize` 返回 `mdSecsize`（512），
`virtio_blk.c` 的 `blk_dev_size` 返回 `VIRTIO_BLK_SECTOR`（512）。**都返回块大小。**

已改成 `XNUSYS_SECTOR`。

### 二、一次读失败就把整机存储锁死

`twoblock_fail()` 对**每一种**失败都调 `us_poison()`，包括那些命令根本
没发出去的情况：

| 失败原因 | 命令发了吗 | 该不该 poison |
|---|---|---|
| `admission refused` | 没发 | ❌ |
| `block reference missing` | 没发（没测量过） | ❌ |
| `block reference mismatch` | 发了，数据错 | ✅ |
| `slice destination refused` | 没发 | ❌ |
| `slice staging failed` | 没发 | ❌ |
| `slice destination too small` | 没发 | ❌ |
| `engine did not deliver` | 发了 | ✅ |
| `STOP before/after copy` | 状态机异常 | ✅ |
| `copy_done refused` | 状态机异常 | ✅ |

poison 的语义是"命令发出去了，控制器可能没恢复"。把它用在"命令没发"上，
就把一次普通失败升级成了永久锁死 —— 这就是五次手动 fastboot 的由来。

现在分成两个函数：`twoblock_refuse()`（不 poison）和 `twoblock_fail()`（照旧）。

## 测试抓到了我的一次误判

第一次拆分时我把 `block reference mismatch` 也改成了 refuse。回归立刻崩：

```
Assertion `twoblock_outcome.result == VRQ_DELIVERED && lease_record()->state == UR_POISONED' failed
```

`corrupt_last` 这个场景是"命令发了、数据错了"，必须 poison。改回去，38 项全过。

**这次是测试先抓到问题，不是真机。**

## 仍失败

修完这两个之后刷入，仍然卡在 loader guard。日志未取到（设备挂了）。

已知：读处理器现在理论上可达（字符节点 + `D_DISK` + `physio` 进 strategy +
`d_psize` 已修正）。未知：还有哪条路径会 poison，或者卡点在别处。

**下一步该做的**：先取那次启动的 HLOG，看驱动走到哪一步——
这是唯一还没拿到的证据。

## 这一轮我的流程问题

五次刷机，每次都靠刷机确认，而不是先读代码。两个 bug 都是读源码发现的，
不是测出来的。正确顺序应该是：**先读调用链，再改一处，再刷**。

## 回归

```
-O2 -Wall -Wextra -Werror   38 PASS
ASan + UBSan                38 PASS   0 违规
```
