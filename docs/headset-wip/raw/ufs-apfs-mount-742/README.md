# 里程碑 4b · 只读挂载与普通路径访问（Trial742）— 通过

2026-11-21。xaga，`boot_a`，恢复原厂后 64 MiB 逐字节相同。

## 实机结果

一次启动里，进程 1 从 `/redwood/etc/rc` 发起挂载，然后**用普通 VFS 调用**读取：

```
xnusys-blk: /dev/xnusys 608448 blocks of 4096, read only, bmajor=2 cmajor=17
xnusys-blk: mount /dev/xnusys on /redwood/lib -> 0
xnusys-walk: stat /redwood/lib        -> 0  ino=16893
xnusys-walk: stat target              -> 0  ino=61314  size=5230
xnusys-walk: read 5230 bytes
xnusys-walk: fnv=f28fe48a bytes=5230
```

设备端 FNV-1a 为 `f28fe48a`，主机对同一文件独立计算也是 `f28fe48a`。**逐字节相同。**
这与 Trial738 直接走驱动读到的是同一个文件、同一个 inode（61314）、同样 5230 字节。

## 三处卡住的地方

**一、设备注册错了命名空间。** 第一版在 `bdevsw_add` 拿到的块 major 上建了 `DEVFS_CHAR` 节点，
`open()` 一律 -1。块与字符是两个独立命名空间，节点必须建在它所属的那个里。改成块节点
`/dev/xnusys` + `cdevsw_add_with_bdev` 的字符节点 `/dev/rxnusys`。

**二、ioctl 里多做了一次 `copyin`。** `VOP_IOCTL` 已经把参数拷进内核缓冲，我又把它当用户地址拷
一次，于是每次都失败。去掉后 ioctl 立刻到达。

**三、`EBUSY` 来自设备被自己占着。** 程序打开 `/dev/rxnusys` 发 ioctl，而挂载要打开的是同一个
vnode，内核拒绝第二次打开。把挂载目标改成块节点 `/dev/xnusys` 就分开了——两个节点、两个 vnode。

## 为什么挂载不能在 bsd_init 做

Trial740 量到 `ENOENT`：`/redwood` 是第一个用户进程挂的，`bsd_init` 阶段那个目录还不存在。
所以挂载做成了设备上的 ioctl，由进程 1 在 `/redwood` 就绪之后调用。

## 主机回归

```
-O2 -Wall -Wextra -Werror   35 PASS
ASan + UBSan                35 PASS   0 违规
test-ufs-dir-enum           11 PASS
test-ufs-path-walk           8 PASS
```

## 已知未完成

- 目录枚举经由挂载点（`getdirentries64`）返回 -1。`stat` 和目标文件的 `open`/`read` 都正常，
  所以问题在目录读这一层，不在挂载本身。
- `printf` 的 `%.8s` / `0%o` 在 mini lib 里不支持，日志里显示成字面量。是显示问题，不是数据问题。
- 这是**第二个**卷挂在 `/redwood/lib` 上；阶段 4 要求的"卸载"与"写打开被拒"还没有单独取证。
