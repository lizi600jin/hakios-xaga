# 里程碑 6n · journal 建成并救回一次脏卷（Trial791–792b）

2026-11-23。设备已回安卓，`boot_a` 是 792b 的实验镜像。

## 一、这一轮证明了什么

`/private/var` 需要的最后一项能力——**系统在没有预警的情况下结束之后，数据还在**——现在有了。

```
791    内核自己创建 journal                          ✅
792a   写入，但不卸载，卷留在"使用中"                  ✅
792b   完全不碰闪存，直接挂载 → journal 自动回放 → 数据完好  ✅
```

内核日志里的证据：

```
jnl: xnusys_var: replay_journal: from: 276992 to: 355328 (joffset 0x4000)
jnl: xnusys_var: journal replay done.
hfs: mounted RedwoodVar on device xnusys_var
```

`276992 → 355328` 正是 792a 遗留的未提交事务的范围。严丝合缝。

## 二、791：让内核自己建 journal

卷被人为置成「已标日志但没有日志」的状态——这正是 `mkfs.hfsplus -J` 造出来的初始状态：

```
attributes        = 0x80002100   (Journaled | Unmounted)
JIB.flags         = 0x00000005   (InFS | NeedInit)
journal 区（块 4 起 8 MiB） = 全零
```

可写挂载后：

```
hfs: Initializing the journal (joffset 0x4000 sz 0x800000)...
```

卸载后：

```
attributes        = 0x80002100   Journaled=1 Unmounted=1
lastMountedVersion= 'HFSJ'        ← 从 '10.0' 变过来
JIB.flags         = 0x00000001   NeedInit 被清掉
journal header    @16384: magic 0x4a4e4c78, start == end, size 8 MiB
```

探针的全流程（挂载 → 写 → 卸载 → 重挂 → 读回）全部返回 0。

### journal 的字节序：是主机序，不是大端

一开始我以为是 bug——闪存上 `magic` 读出来是 `0x784c4e4a`，反的。查下去发现是对的：

```
hfs_journal.c:783    swap_journal_header()  用 OSSwapInt32/64
hfs_journal.h:201    #define JOURNAL_NEED_SWAP  0x00080000
hfs_journal.c:1963   jnl->flags |= JOURNAL_NEED_SWAP;
```

HFS+ 的**卷头**是大端，但 **journal** 用主机原生字节序，`endian` 字段
（`0x12345678`）就是用来标记这一点的。PowerPC 时代写大端，x86 和 ARM64 写小端。
闪存上的 `78 4c 4e 4a / 78 56 34 12` 正是标准 ARM64 格式，一字节都不用改。

## 三、792a：故意留一个脏卷

`mount rw` → 写 40 字节 → `fsync` → **不卸载**，让 loader 的 420 秒上限把它切回安卓。

闪存上留下的状态：

```
块 0  attributes = 0x80002000   Journaled=1 Unmounted=0   ← 脏
块 3  JIB.flags  = 0x00000001   NeedInit=0               ← 没有重新初始化
块 4  magic      = 0x4a4e4c78
      start = 276992  end = 355328                        ← 有未提交事务
```

HLOG 里 **0 条** `Initializing the journal` —— 证明这次挂载走的是 `journal_open`，
不是重新创建。

## 四、792b：不碰闪存，直接挂载

```
before: attributes=0x80002000  Unmounted=0  fileCount=9
        journal start=276992 end=355328  not checkpointed

[1] mount rw -> 0   OK -- it mounted a volume that was never unmounted
[2] open dirty.txt -> 3
    read -> 40
    content = Trial792a: written but never unmounted.
    it matches what 792a wrote   PASS
[3] unmount -> 0

after: attributes=0x80002100  Unmounted=1  fileCount=9
       journal start=487424 end=487424  checkpointed
PASS
```

一个没有 journal 的卷在这个状态下会拒绝可写挂载
（`cannot mount dirty non-journaled volumes`，Trial786c 见过）。
这次它挂上了，回放了，数据一字不差。

## 五、一个被证伪的判据：fsck.hfsprogs 对小卷误报

这一轮一度卡在 `fsck.hfsplus -n` 报错：

```
invalid primary VHB at 2 result 35
alternate VHB is at block 356350
** The volume RedwoodVar was found corrupt and needs to be repaired.
```

查下去发现**不是我们的问题**。用上游工具造一个全新的、从未挂载过的日志卷：

```
truncate -s 182452224 /tmp/refj.img
mkfs.hfsplus -v RedwoodVar -b 4096 -J /tmp/refj.img
```

对它跑同一个 fsck：

```
invalid primary VHB at 2 result 35
alternate VHB is at block 356350
```

**一模一样。** 所以这是 `hfsprogs 540.1` 对 174 MiB / 4096 块这种小卷的误报。
判据改成直接读闪存和 HLOG，不再依赖第三方工具。

这个对照实验还顺带证实了我们的卷头是标准的——`mkfs -J` 的初始状态
（`attributes=0x80002100`、`JIB.flags=0x5`、journal 区全零）和我们 791 的起点逐项一致，
`allocationFile` / `extentsFile` / `catalogFile` / `attributesFile` 四个 fork 的
`logicalSize`、`totalBlocks`、extents 结构也都对得上。

## 六、一个真 bug，但不是这一轮的问题：运行时时钟没校准

卷头的 `modifyDate` 写出来是 `0x7c25b095` —— 1970-01-01 00:00:21。

```
hfs_cnode.c:1962   microtime(&tv);
hfs_cnode.c:2007   HFSTOVCB(hfsmp)->vcbLsMod = tv.tv_sec;
MacOSStubs.c:123   to_hfs_time(t) { return (u32)t + MAC_GMT_FACTOR; }
```

`MAC_GMT_FACTOR` 是 2082844800（1904 到 1970 的秒数），换算没问题。
问题是 `microtime()` 返回的 `tv_sec` 是**开机后的秒数**，不是日历时间——
探针在启动后约 21 秒写入，于是 `vcbLsMod = 21`。

`kern_time.c:243` 的 `inittodr()` 注释写得很清楚：

> Assertion: The calendar has already been set up from the platform clock.

它假设日历已经设好了，自己只做合法性检查，从不读时钟。这个移植里没有任何东西
去读平台时钟，所以整个系统的墙上时间都从 0 开始。

影响：卷头和文件的时间戳都是 1970 年；macOS 会认为卷有问题。
不影响挂载、写入、卸载、journal 回放——这些都不看时间戳。

修法（未做）：`mtkloader.c:212` 的注释里已经预留了 `epoch=`，
但 loader 里没有实现，内核也没有解析它。要接通需要 loader 读一个时间源
（闪存里的一个戳，或 RTC），拼进 kernel command line，内核侧加
`PE_parse_boot_argn("epoch", ...)` 和一次 `clock_set_calendar_microtime()`。

## 七、这一轮之前的两轮

| 轮次 | 修了什么 | 证据 |
|---|---|---|
| 786 | 驱动静默拒绝任何非 4K 对齐的写，而 HFS+ 写卷头是 512 字节。改成读改写 | `f_bfree` 41447 → 41446，卷头第一次写进闪存；主机测试 211 项 |
| 788–790 | 驱动 `buf_map` 之后从不 `buf_unmap`，UPL 引用泄漏，`vclean` 卡在 `vm_object_paging_wait` | 同一探针 785c 卡 420 秒、788 返回 0；端到端写读卸载全通 |

## 八、文件

| 文件 | 内容 |
|---|---|
| `xnusys_hfsbits.c` | 就地设置 journal 位（读改写，只碰 3 个块） |
| `xnusys_hfsjrnl.c` | 791：可写挂载让内核建 journal，然后读回闪存验证 |
| `xnusys_hfsdirty.c` | 792a：写入但不卸载，故意留脏卷 |
| `xnusys_hfsreplay.c` | 792b：挂载脏卷，验证回放和数据 |

镜像在 `logs/ufs-apfs-fstree-root-736/trial736/`：
`boot-791.img`、`boot-792a.img`、`boot-792b.img`。

## 九、还没做的

1. **挂到 `/private/var` 本身**。现在探针挂在 `/private/var/tmp/<name>`。
   要挂到 `/private/var`，得处理内存盘上已有的目录树（`/private/var/vm`、
   `/private/var/run`、`/private/var/tmp` 等），需要看 `init.c` 的
   `mount_data_volume()` 并决定哪些是内存盘、哪些来自卷。
2. **卷的目录骨架**。一个真正的 `/private/var` 需要 `mobile`、`root`、
   `db`、`keychains`、`run`、`tmp` 等等，并带正确的所有者与权限。
3. **时钟校准**（见第六节）。
4. **`hfsseed` 的读回验证**仍然失败——它走 `/dev/rxnusys` 字符节点，
   受 APFS 容器边界限制读不到窗口区。写入本身是成功的。
