# `/private/var` 的 statfs 实测：内核给的值是对的

2026-11-26 凌晨。`boot-930`，在 XNU 里跑 `varstat`。

## 验证工具

`userland/src/varstat.c` —— 一个 34 KB 的静态程序，调 `statfs(path, &buf)`
然后把 `struct statfs` 的每个字段按原始单位打出来。

`libmini` 为此补了 `statfs` / `fstatfs`（syscall 157 / 158）和 `struct statfs`
（照 `bsd/sys/mount.h:142-163` 一字不差地抄，布局就是 ABI）。

## 实测

```
varstat: statfs("/private/var") returned 0
varstat: f_bsize   4096
varstat: f_iosize  1048576
varstat: f_blocks  16777216
varstat: f_bfree   16620363
varstat: f_bavail  16620363
varstat: f_files   4294967279
varstat: f_ffree   4294960705
varstat: f_flags   75534336 (0x4809000)
varstat: f_type    17
varstat: fstypename hfs
varstat: mntonname  /private/var
varstat: mntfromname /dev/xnuvar
varstat: owner     0
varstat: bytes total 68719476736
varstat: bytes free  68077006848
```

## 换算

| 字段 | 值 | 含义 |
|---|---|---|
| `f_blocks` | 16777216 | × 4096 = **64.00 GiB**，与卷头 `totalBlocks` 一致 |
| `f_bavail` | 16620363 | × 4096 = **63.40 GiB** |
| `f_bfree` | 16620363 | 与 `f_bavail` 相同（非 root 也能用全部空间） |
| `f_files` | 4294967279 | `0xffffffef` = **-17 的 int32 表示**，HFS+ 的哨兵值，正常 |

`f_bavail` 比卷头的 `freeBlocks`（16679329）少 58966 块 = 230 MiB，
差额是 `reserveBlocks`（`MIN(totalBlocks/100, 250MiB/4096)` = 64000 块）的量级。

## 对照：根卷

```
varstat: statfs("/") returned 0
varstat: f_blocks  608448
varstat: f_bavail  0
varstat: fstypename apfs
varstat: mntfromname root_device
```

根卷 `f_bavail = 0` 是**真实值**（APFS 只读根），显示 0 没错。

## 结论

**内核侧完全正确。** `statfs("/private/var")` 给出 63.40 GiB 可用，
三层计算（`hfs_freeblks`、`reserveBlocks`、`vfs_attrlist.c:1447` 的乘法）也都对得上。

**`Settings` 里显示的 `Available: Zero KB` 是用户态读错了。**
`ufs-apfs-64g-796/README.md` 第五节列的三个候选层可以全部排除，
问题在更上面——读这个值、或者显示它的那一段。

下一步要查的是：谁在用户态把这个数字变成了零。
`lsd` 或 `backboardd` 拿 `statfs` 之后做了什么，或者它读的是另一个接口
（`getattrlist` 的 `ATTR_VOL_FSTYPE`/`ATTR_VOL_SPACEFREE` 之类，不是 `statfs`）。

## 文件

- `varstat.c` —— 探针源码
- `docs/headset-wip/raw/GPU-USB-RESULTS.md` —— 项目主文档

## 镜像

`boot-930.img`：`mali=6`（`mali_gpuebpower` 已默认开启），带 `varstat` 和 `gpuebctl`。
