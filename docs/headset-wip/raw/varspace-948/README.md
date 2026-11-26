# `/private/var` 的两条查询路径都对（boot-948）

2026-11-26。`volspace`，同一进程里问两次。

## 结果

```
volspace: statfs("/private/var") = 0
volspace:   f_bsize 4096 f_blocks 16777216 f_bavail 16587157
volspace:   free bytes 67940995072

volspace: getattrlist("/private/var") = 0
volspace:   buffer length 24 (6 words)
volspace:   +00: 00000018  24          <- 总长
volspace:   +04: 00000011  17          <- ATTR_VOL_FSTYPE，HFS
volspace:   +08: d1995000  3516485632  volspace:   +12: 0000000f  15          / 0x0000000fd1995000 = 67,940,995,072
volspace:   +16: 2e66b000  778481664   \
volspace:   +20: 00000000  0           / 0x000000002e66b000 = 778,481,664
```

## 换算

| 来源 | 属性 | 值 |
|---|---|---|
| `statfs` | `f_bavail × f_bsize` | 67,940,995,072 = **63.28 GiB** |
| `getattrlist` | `ATTR_VOL_SPACEFREE`（`+08` 起 8 字节） | 67,940,995,072 = **63.28 GiB** |
| `getattrlist` | `ATTR_VOL_SPACEUSED`（`+16` 起 8 字节） | 778,481,664 = **742 MiB** |

**两条路径一字不差。**

`+04` 那个 17 是 `ATTR_VOL_FSTYPE`，HFS 的签名。

## 所以问题在哪

内核侧两条路都对。`vfs_attrlist.c:1442-1455` 的三行

```c
ATTR_PACK_CAST(&ab, off_t, vs.f_bsize * vs.f_bfree);     /* SPACEFREE  */
ATTR_PACK_CAST(&ab, off_t, vs.f_bsize * vs.f_bavail);    /* SPACEAVAIL */
ATTR_PACK_CAST(&ab, off_t, vs.f_bsize * vs.f_bused);     /* SPACEUSED  */
```

和 HFS 的 `hfs_vfs_getattr`（`hfs_vfsops.c:4100-4114`，每个字段都 `VFSATTR_RETURN` 了）
一起，把这两个值都算对了。

**`Settings` 里那个 `Available: Zero KB` 是应用层自己的事。**

可能是：
- 它读的是另一个接口（`NSURLVolumeAvailableCapacityForImportantUsageKey`
  走 `CacheDelete`，那是另一条完全不同的路径）
- 或者它读对了但算错了

## 一个值得记下来的调试顺序

这一轮先猜了三个地方——`hfs_freeblks`、`reserveBlocks`、`vfs_attrlist` 的乘法——
全都是错的，而且都是靠读代码猜的。

**写一个 34 KB 的探针，在同一进程里问两次，比读三个小时的代码有用。**

## 文件

- `volspace.c` —— 探针源码
- `docs/headset-wip/raw/varstat-930/` —— 上一轮的 `statfs` 探针
