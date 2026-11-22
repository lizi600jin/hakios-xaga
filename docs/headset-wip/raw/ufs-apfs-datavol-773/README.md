# 里程碑 6j · 十一次失败，一条被证伪的假设，一条未走通的路（Trial773）

2026-11-22。设备已恢复原厂，`boot_a` 逐字节相同。本次会话第十二次恢复。

## 一、最重要的一件事：唯一成功的那次，什么都没读

`/tmp/hlog770.bin` 里那次 439 秒的完整运行（`state=2 allowed=1`，iOS 桌面 10 fps），
**`zone_read` 被调用 0 次**：

```
zone_read          0 次
window_read        0 次
d_read entered     0 次
xnusys-direct      0 次
xnusys-varread     0 次
rc 内容：exec /sbin/launchd.ios
```

设备注册成功，但从未被读取。那次证明的是**"设备注册不影响启动"**，
不是"读路径能工作"。

**这一条推翻了我之前几次判断的基础。**

## 二、被证伪的假设

**假设**：读数据卷时 APFS 卷还挂着，两个使用者抢同一个控制器。

**实验**：`xnusys_afterunmount` 先 `unmount /redwood/lib`，再读 608512。

**结果**：仍然崩。假设不成立。

## 三、十一次失败的分类

| 类别 | 次数 | 具体 |
|---|---|---|
| 读路径实现不对 | 5 | `twoblock_run` 无 reference、`DATA_WINDOW_MODE`、`d_psize`、节点类型、`data_bytes` |
| 租约被毒化 | 3 | `twoblock_fail` 对所有失败都 poison、`refuse` 不归还、`fail` 不归还 |
| 卸载时机 | 1 | 卸载后仍崩（证伪） |
| 诊断缺失 | 2 | guard 死循环在写 HLOG 之前，日志永远拿不到 |

## 四、修好的两个真 bug

### 一、`d_psize` 返回块数而非块大小

`set_blocksize()` 把它的返回值直接存进 `v_specsize`，而 `spec_read`
拒绝超过 `PAGE_SIZE` 的值。我返回 356352，于是每次读在到达驱动前就 `EINVAL`。

对照 `memdev.c` 的 `mdevsize`（512）和 `virtio_blk.c` 的 `blk_dev_size`（512），
**都返回块大小**。

### 二、一次读失败毒化整机存储

`twoblock_fail` 对所有失败都 `us_poison`，包括命令根本没发出去的情况。
poison 的语义是"命令发了、控制器可能没恢复"，用在没发的情况上，
就是把普通失败升级成永久锁死。

更糟的是自锁：内核 `init` 里

```c
if (!ur_lease_valid(&initial) || initial.state != UR_UNUSED || ...) return;
```

一旦 `state` 变成 3（POISONED），**之后每次启动都直接返回，永远无法恢复**。

已拆成 `twoblock_refuse`（归还租约、不 poison）和 `twoblock_fail`（归还后 poison）。
校验和不匹配仍然 poison，因为那是"发了、数据错"。

回归抓到了我第一次拆错（把校验和路径也改成 refuse），38 项全过。

## 五、拿到的关键诊断

在 loader 的 guard 里加了一行，把租约的实际值打出来：

```
ufs-read guard: lease magic=0x554c5345 version=0x1 trial=0x2e4 state=0x3
```

`state=0x3` = `UR_POISONED`。这一行使方向从"VFS 接口"收窄到了"谁毒化了租约"。

同时把 guard 的 `for(;;) wfe` 改成继续执行——因为它卡在写 HLOG 之前，
十次失败一次日志都没拿到。

## 六、找到但未走通的路：`hakios.ufs.c`

内核里有个完全不同的驱动：

- 内核**不碰 UFS 控制器**
- 通过共享页写请求，由 **loader（core 0）代为读写**
- 提供 `/dev/ufs_<分区名>` 和 `/dev/rufs_<分区名>`
- 注释：写出去了就是在闪存上，因为 loader 会 flush 并逐块读回验证

**这正是 `var_store` 用的那条路**，它不碰租约，也就不会搞坏租约。

本轮已把它编进内核（`bsd/conf/files` 从 `optional` 改成 `standard`，
关掉 `hakios.stubs.c` 里的空实现），编译通过，`hakios.ufs.o` 160 KB。

**但缺 loader 侧配套**：内核要 boot-arg `ufs_shared_pa`（共享页物理地址），
而 loader 目前不提供。要跑通得改 loader：分配共享页、传地址、在 core 0 上
循环处理内核的读写请求。

## 七、这一轮的教训

**十一次"改一处 → 刷机 → 手动 fastboot"。** 其中两个 bug 是读源码读出来的，
不是测出来的。正确顺序应该是：先读调用链，再改一处，再刷。

更严重的是：**没有在成功时立刻锁定基线。** 439 秒那次成功是偶然撞上的，
我没保存当时的 boot.img 和内核组合就继续改代码，把可复现的成功状态弄丢了。

## 八、下一步

单独一轮做 `hakios.ufs.c` + loader 的共享页通道。它不碰租约，
是架构上正确的路，也是 var_store 已经验证过的路。

在它跑通之前，不再动 `xnusys_var` 的读路径。
