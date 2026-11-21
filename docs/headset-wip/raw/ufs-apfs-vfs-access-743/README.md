# 里程碑 4c · 阶段 4 验收完成（Trial743）

2026-11-21。xaga，`boot_a`，恢复原厂后 64 MiB 逐字节相同，SELinux Enforcing。

## 阶段 4 的验收项全部在真机上走通

一次启动，进程 1 从 `/redwood/etc/rc` 发起挂载，随后全部用普通 VFS 调用：

```
xnusys-blk: mount /dev/xnusys on /redwood/lib -> 0

stat /redwood/lib        -> 0  ino=2
getdirentries64          -> 592 字节，12 条
   . ..  usr  bin  sbin  etc  System  var  Library  private  redwood  dev
stat target              -> 0  ino=61314
open + read              -> 5230 字节
head                     -> bplist00
fnv1a                    -> f28fe48a
```

`f28fe48a` 与主机独立算出的值相同，也与 Trial738 直接走驱动读到的一致。
目录项的类型也正确：`usr`/`bin`/`sbin`/`System`/`Library`/`private`/`redwood`/`dev` 是目录（4），
`etc`/`var` 是符号链接（10）。

## 两个此前没查出的错误

**一、`getdirentries64` 的第四个参数是出参。** 内核在里面 `copyout` 偏移量，传 0 会让每次调用
`EFAULT`，表现成 -1，看上去像"目录读不了"。它其实是缺了一个 out 参数。上一轮我据此判断
"目录读这一层有问题"，是错的——那一层一直是好的。

**二、名字打印用了 `%.*s`。** mini lib 的 printf 不支持长度限定符，日志里显示成字面量，
条目名全是 `%.*s`。数据一直是对的，是显示的问题。

## 关于 HeliumStudio-Dev/hackios-xnu-kernel

看过之后有三点值得记下：

**他们的块设备注册与最终版一致**（`bdevsw_add` + `cdevsw_add_with_bdev` + `DEVFS_BLOCK`/`DEVFS_CHAR`）。
我摸索到的写法是对的，这是独立印证。

**他们的架构不同：不写 UFS 驱动。** UEFI 不退出，高通自己的 UFSDxe 留在 core 0 服役，内核把请求
写进共享页由 loader 代为读取。我的做法是内核直接发 READ(10)。两条路都能读，他们的省掉了整套
控制器初始化，但依赖 UEFI 常驻。

**他们的 `apfs_vnops.c` 与我们逐字相同**（仅注释里 iOS/Darwin 措辞不同），`tools/apfs/` 下
`check-image.py`、`fmt_check.c`、`fnvsum.c` 三份完全相同。同一套代码。

**一处不能照搬**：他们用 `buf_dataptr()`，我用 `buf_map()`。他们不挂文件系统，没有 cluster I/O，
所以那样够用；APFS 会经 UPL 读（`ubc_create_upl_kernel`），那种 buffer 的 `buf_dataptr` 是偏移
不是地址，必须 `buf_map`。virtio_blk 的注释里写了同一件事。

## 仍未取证

- 卸载
- 写打开被拒（驱动层返回 `EROFS`，但没在挂载点上验证过）
