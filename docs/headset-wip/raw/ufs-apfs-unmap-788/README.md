# 里程碑 6m · 卸载卡死的原因：buf_map 之后没有 buf_unmap（Trial788–790）

2026-11-23。设备已回安卓，`boot_a` 是我的实验镜像。

## 一、这一轮解决了什么

**可写挂载 → 写入 → 卸载 → 重新挂载 → 读回，整条链第一次走通。**

```
hfs-final: flash before: attributes=0x80000100 freeBlocks=41443 last=fsck
hfs-final:   Unmounted=1
hfs-final: [1] mount rw -> 0   OK
hfs-final:     free blocks = 41443
hfs-final: [2] open -> 3
hfs-final:     write 59 -> 59
hfs-final:     fsync -> 0
hfs-final:     free blocks = 41442  (a block was allocated)
hfs-final: [3] unmount ...
hfs-final:     unmount -> 0
hfs-final: [4] flash after: attributes=0x80000100 freeBlocks=41442 last=10.0
hfs-final:     Unmounted=1   the volume is clean on the flash
hfs-final: [5] remount ro -> 0
hfs-final:     open -> 3
hfs-final:     content = Trial790: a writable mount that ends with a clean unmount.
hfs-final:     unmount -> 0
hfs-final: PASS
```

内核日志里第一次出现完整的卸载流程：

```
hfs: mounted RedwoodVar on device xnusys_var
hfs: unmount initiated on RedwoodVar on device xnusys_var
hfs: unmount: flushing files
hfs: unmount: files flushed (retval=0)
hfs: unmount: starting the transaction (jnl=0)
hfs: unmount: transaction started retval=0
hfs: mounted RedwoodVar on device xnusys_var
hfs: unmount initiated on RedwoodVar on device xnusys_var
hfs: unmount: files flushed (retval=0)
```

## 二、根因

`buf_map()`（`bsd/vfs/vfs_bio.c:1159`）对一个 `B_CLUSTER` 且没有预映射 `real_bp` 的 buf，
会真的调 `ubc_upl_map()` 并在这个 UPL 上取一个引用。只有 `buf_unmap()` 会把它还回去。

`buf.h:341-345` 写得很明白：

> Otherwise, the buffer was created with `buf_alloc()` and `buf_setupl()` was subsequently
> called; `buf_map()` created the mapping. **In this case, `buf_unmap()` will unmap the buffer.**

我们的 strategy 只 map、从不 unmap。于是每读一次普通文件就泄漏一个 UPL 引用，
文件的 VM object 上 `paging_in_progress` 永不归零。

卸载时：

```
dounmount()                            bsd/vfs/vfs_syscalls.c:2704
  ├─ ubc_umount(mp)                    :86   ← UBC_PUSHALL，不设 IO_SYNC，不等
  ├─ if (!MNT_RDONLY) VFS_SYNC(...)    :88   ← 只读挂载跳过
  ├─ vflush(...)                       :108
  │    └─ vclean()                     vfs_subr.c:3003
  │         ├─ ubc_msync(..., UBC_SYNC)              :3090
  │         └─ ubc_destroy_named(vp)                 :3127
  │              └─ memory_object_destroy
  │                   └─ vm_object_destroy → 等 paging_in_progress == 0   ← 永远等
  ├─ mount_iterdrain(mp)               :118
  └─ VFS_UNMOUNT(...) → hfs_unmount    :120   ← 从没被调用过
```

`paging_in_progress` 不归零，`vclean` 就卡在 `vm_object_paging_wait`，
`vflush` 阻塞，`hfs_unmount` 永远到不了 —— 所以日志里从来没有 `hfs: unmount initiated`。

### 这个解释覆盖了全部证据

| 轮次 | 挂载 | 探针操作 | 有没有建普通文件 UPL | 结果 |
|---|---|---|---|---|
| 786 | 只读 | 打开**目录** + `getdirentries64` | 没有 | ✅ `unmount -> 0` |
| 785c | 只读 | `read()` 普通文件 56 字节 | 有 | 🔴 卡死 |
| 786b | 可写 | 写普通文件 49 字节 | 有 | 🔴 卡死 |
| 786e | 可写 | 同上 | 有 | 🔴 卡死 |
| 786f | 可写 | 同上 | 有 | 🔴 卡死 |

只读和可写都会卡，脏页和干净都会卡 —— 唯一的变量就是有没有对普通文件做过 I/O。
这也解释了为什么"每条退出路径都调了 `buf_biodone`"却还是卡：`buf_biodone` 不释放映射。

## 三、改动

`hakios.xnusys_blk.c` 里三处：

1. **每个出口加 `buf_unmap(bp)`**，位置在 `buf_biodone(bp)` 之前。共 9 处。
2. **删掉写分支里重复的 `buf_map`**。`buf_map` 在读写分岔之前已经调过一次，
   写分支又调一次 —— 同一个 UPL 映射两次会失败，而旧代码把这个失败当作致命错误，
   于是在一个已经映射的 buf 上丢掉引用。
3. **`buf_map` 失败的分支不 unmap** —— 什么都没映射，就没有东西要还。

逐出口核对：

```
行 657, 668, 674   在 buf_map 之前（参数检查），不需要 unmap   ✅
行 679             buf_map 失败，不 unmap                      ✅
行 722 … 841       其余 9 个出口，unmap 紧跟 biodone           ✅
```

## 四、A/B：同一个探针，同一个卷，只差一个 unmap

Trial788 用的是 785c 的原样操作 —— 只读挂载，读 `probe.txt` 的 56 字节，然后卸载：

```
hfs-ab: mount ro -> 0
hfs-ab: open /private/var/tmp/hfs-ab/probe.txt -> 3
hfs-ab: read 56 -> 56
hfs-ab: content = Trial785b: written through HFS Plus on the data volume.
hfs-ab: close -> 0
hfs-ab: calling unmount -- this is where 785c stopped
hfs-ab: unmount -> 0
hfs-ab: PASS
```

785c 在最后一步卡了 420 秒，788 返回 0。

## 五、跨启动持久化

重启后在闪存上找到三次不同启动写的文件，卷头仍然干净：

```
卷头: attributes=0x80000100
  bit8  Unmounted (干净) = True
  bit13 Journaled       = False
freeBlocks = 41442   lastMounted = '10.0'

块 10056: 'Trial785b: written through HFS Plus on the data volume.'
块 10059: 'Trial787: does the volume header reach the flash?'
块 10060: 'Trial790: a writable mount that ends with a clean unmount.'
```

## 六、这一轮之前修掉的两个 bug

### 驱动拒绝 512 字节写（Trial786）

`xnusys_var_strategy_locked` 对非 4K 对齐的写直接返回 `EROFS`，而且不打印任何东西。
HFS+ 写卷头是 512 字节（偏移 1024），所以**每一次卷头写都被静默拒绝**，
挂载层只看到 `unmount -> EROFS`。

改成读改写（读整块 → 覆盖请求覆盖的部分 → 整块写回）之后：

```
786b: free blocks before the write = 41447
      free blocks after the write  = 41446  (moved by 1)  PASS
```

主机测试 `test-rmw.c` 211 项全过，含 200 组随机偏移和长度。

### `XNUSYS_WRITE_BLOCK` 是 `_IOW`，`ok` 从不拷回用户态

驱动里 `a->ok = 1` 到不了调用者。`xnusys_hfsseed` 早期版本不检查 `ok`，
所以"写入成功"一直是假的；加上检查后 12 块全报 `ok=0`，
而内核日志显示 12 次写入都是 `ocs=0 restored=1`（真的成功了）。

改成 `_IOWR`，用户态用 `0xC0000000`。

## 七、还没做的

- **`hfsseed` 的读回验证仍然失败**（`readback short (0)`）。它的读回走 `/dev/rxnusys`
  字符节点，读不到窗口区（那条路受 APFS 容器边界限制）。写入本身是成功的 ——
  闪存状态证明。要修的话，读回应该走 `/dev/xnusys_var` 块节点，而且要在没挂载时读。
- **一直是非日志卷**。卷本来是 journaled 的（`0x80002100`、JIB `flags=0x5` 含
  `kJIJournalNeedInitMask`），但 journal 区域全零，只读挂载报
  `journal is dirty`。当时的处理是清掉 journal 标志。现在卸载能干净收尾了，
  可以重新试让内核自己 `journal_create()`。
- **`/private/var` 还没有真挂上去**。现在只是探针挂到 `/private/var/tmp/<name>`。
  要挂到 `/private/var` 本身，得看 `init.c` 里的 `mount_data_volume()`，
  并且先处理"内存盘上已经有东西"这件事。

## 八、文件

| 文件 | 内容 |
|---|---|
| `hakios.xnusys_blk.c` | 驱动，含 `buf_unmap` 修复 |
| `xnusys_hfsab.c` | A/B 探针：只读 + 读 56 字节 + 卸载 |
| `xnusys_hfsfinal.c` | 端到端：挂载/写/卸载/查闪存/重挂/读回 |
| `xnusys_hfsdisk.c` | 挂载后从设备重读卷头（受 EBUSY 限制，未成功） |
| `test-rmw.c` | 读改写的主机测试，211 项 |

镜像在 `logs/ufs-apfs-fstree-root-736/trial736/`：`boot-788.img`（A/B）、
`boot-790.img`（端到端）。
