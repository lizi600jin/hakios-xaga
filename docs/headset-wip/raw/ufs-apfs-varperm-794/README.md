# 里程碑 6p · 卷根权限 0000：`/private/var` 终于能进 iOS 了（Trial794）

2026-11-23。设备已回安卓。磁盘空间不足，这轮的产物以 diff 和文字归档。

## 一、结果

```
轮次                  Corpse   CPU拒绝  IOSurface  SpringBoard  EACCES
790 (ramfs 基线)         0       701        2        1472        0
792b (ramfs 基线)        0       701        2        1466        0
793a (HFS+ 坏)           5      3360        0          30      144
794 (HFS+ 修好)          0         0       11        1498        0
```

操作者的确认：「这次没有进度条，正常进 ios 里了」。

`CPU monitor` 拒绝从 701（连 ramfs 基线都有这个数）降到 **0**，
`IOSurface` 从 2 涨到 **11**，`EACCES` 从 144 降到 **0**。

## 二、根因：卷根权限是 0000

Astra 从三份日志里定位出来的，链条很短：

```c
// init.c:854
memset(&args, 0, sizeof(args));        // hfs_mask = 0，不是 VNOVAL

// hfs_vfsops.c:1367
if (args->hfs_mask != (mode_t)VNOVAL) {
    hfsmp->hfs_dir_mask = args->hfs_mask & ALLPERMS;    // 0 & 0777 = 0
}

// hfs_catalog.c:3830
if ((bsd->fileMode & S_IFMT) == 0) {   // 卷根没有类型位
    attrp->ca_mode = S_IFDIR | (hfsmp->hfs_dir_mask & ACCESSPERMS);   // S_IFDIR | 0
}
```

**`/private/var` 的卷根于是成了权限 `0000` 的目录。** `lsd`、`backboardd` 都以
`mobile`（501）运行，连遍历都做不到。

ramfs 的卷根是 `0755`，所以同一个 `var.skel` 在 ramfs 上没事，在 HFS+ 上就出事。

### 症状的形状

`lsd` 第一个进程（`lsd[23]`）的 20 行日志里，19 行是
`failed lookup: name = com.apple.mobileactivationd`——它在反复重试，
因为打不开 `/private/var/db/lsd`（skel 把那个目录建成 `755 501:501`，
但父目录 `0000` 走不进去）。90 秒后 launchd 清掉它，然后是 10 次 Corpse。

`backboardd` 更早：在 `lsd` 启动之前的 23.9 秒就报 `EACCES`，整轮 144 次。

之前我怀疑过 `mobileactivationd` 缺失是根因，那不是——它在两份镜像的
23 个 job / 106 个 MachService 里**都没注册**，792b 一样缺，
只是 ramfs 上 `lsd` 7 次之后就绕过去了。

## 三、修复：两处，都在 `init.c`

### 1. 让卷用文件系统自己的权限

```c
args.uid  = (uint32_t)-1;    // VNOVAL
args.gid  = (uint32_t)-1;
args.mask = (uint16_t)-1;
args.pad  = (uint16_t)-1;
```

`hfs_mount.h` 的 `hfs_mount_args` 里 `hfs_mask` 是 `mode_t`（4 字节），
而 `init.c` 的结构体把它声明成 `uint16_t mask` 加一个 `uint16_t pad`，
所以两半都要填 `0xFFFF` 才是 `-1`。

传 `VNOVAL` 之后内核走另一条分支：

```c
hfsmp->hfs_dir_mask = UNKNOWNPERMISSIONS & ALLPERMS;   // 0705
```

### 2. 显式把卷根设成 0755

```c
if (chmod("/private/var", 0755) != 0) {
    printf("  /private/var: the root could not be made reachable: %s\n", strerror(errno));
}
```

`UNKNOWNPERMISSIONS` 是 `0705`（属主 rwx、组无、其他人 r-x），
可遍历但组没有权限。`0755` 对齐 ramfs 的行为，也让 `mobile` 所在的组能进去。

## 四、另一个必须改的地方：`var.skel` 的源头

`build.sh` 从 `sysimage/var.skel` 复制，然后追加 `var/` 目录下的文件、
`mobile/tmp` 和两条时区链接。所以：

- **要删 `/private/var/vm` 必须改 `sysimage/var.skel`**，改 `root/etc/var.skel` 会被构建覆盖
- 删掉之后 `build.sh` 生成 119 条（111 - 1 + 8 plist + 1 mobile/tmp + 2 链接）

`/private/var/vm` 必须不存在，否则第 19 秒的 `vm_swapfile_create_thread` 会在那里
建 swapfile。iOS 分支的 `MAX_SWAP_FILE_SIZE` 是 **128 MiB**（不是 OSX 的 1 GiB），
而卷只有 174 MiB。

## 五、修好之后暴露的新问题：卷 60 秒写满

```
totalBlocks = 44544    (174 MiB)
freeBlocks  = 0
非零块      = 43325    (169.2 MiB)
fileCount   = 997      (骨架只有 8)
folderCount = 5127     (骨架只有 110)
```

操作者在设置里看到 `Capacity: Zero KB / Available: Zero KB`。

占空间的两段：

```
连续块 14221..32755   72.4 MiB
连续块 34002..41804   30.5 MiB
```

里面是 `*** NXCreateHashTable: invalid style`、`CACHES: %sclass %s: dropping cache`
（Objective-C runtime 的格式串，说明是 dyld shared cache 的字符串表提取结果）
和 `BiometricKit`、`UIWindowScene`、`BKSProcessAssertion`（SpringBoard/backboardd 日志）。

**174 MiB 对真实 iOS 远远不够。** 这是下一轮要解决的。

## 六、这一轮的方法论收获

- **双门禁**：存储层（挂载+持久+空间）和 iOS 启动层（Corpse=0、CPU 拒绝=0、
  SpringBoard 出 IOSurface、无 BootUI 卡死）必须同时过。
  793 就是只看了存储层、漏了启动层。
- **时间戳有三种**：`[  N.N]:` 是内核相对时间，`1970-01-01 00:00:SS` 是 UTC 壁钟，
  `com.apple.xpc.launchd|1970-01-01 08:00:SS` 是 +0800 壁钟。
  混着读会得出完全错误的结论（我在同一份日志上错了三次）。
- **主机能读的不要靠推理**：`modprobe hfsplus` 之后 `mount -o ro,loop` 就能
  直接看卷里的文件，比手工解析 B-tree 可靠得多。

## 七、文件

| 文件 | 内容 |
|---|---|
| `init.c.diff` | 两处修复（VNOVAL + chmod 0755） |
| `var.skel.diff` | 删掉 `/private/var/vm` |

镜像：`logs/ufs-apfs-fstree-root-736/trial736/boot-794.img`。
