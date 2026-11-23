# xnuvar 驱动改造（Trial795，主机完成，未上机）

2026-11-23。64 GiB 方案的第一步：在主机上把驱动改好、测好，再动 GPT。

## 做了什么

### 1. `/dev/xnuvar`，第二个数据卷

`hakios.xnusys_blk.c` 里原来的 `xnusys_var` 逻辑只有一处依赖自己的窗口——三个常量
`XNUSYS_VAR_FIRST` / `_BLOCKS` / `_BYTES`。所以把它改成吃一个描述符：

```c
struct xnusys_vol {
	uint64_t	first;		/* flash block this device calls block 0 */
	uint64_t	blocks;
	const char     *name;
};
```

两个实例：

```c
static const struct xnusys_vol xnusys_var_vol = {
	XNUSYS_DISK_LBA + XNUSYS_VAR_FIRST, XNUSYS_VAR_BLOCKS, "xnusys_var"   /* 31188992, 44544 */
};
static const struct xnusys_vol xnuvar_vol = {
	XNUVAR_PART_FIRST, XNUVAR_PART_BLOCKS, "xnuvar"                        /* 13803264, 16777216 */
};
```

`xnuvar` 有自己的一整套 open/close/size/read/write/ioctl 和 bdevsw/cdevsw，
注册成 `/dev/xnuvar`（`DEVFS_BLOCK`，`0640`）。旧的 `/dev/xnusys_var` 保留，可以回退。

### 2. 寻址语义：从「相对偏移」改成「绝对 LBA」

这一处是必须改的，也是唯一会让整个方案悄无声息出错的地方。

原来：

```c
r.lba = (uint32_t)(XNUSYS_LBA + block);   // block 是 xnusys 内的偏移
```

`xnuvar` 在 xnusys **前面**，`13803264 - 30580480` 是负数，没有一个偏移能同时服务两个卷。
所以加法移到了卷描述符里，底层只要绝对块号：

```c
r.lba = (uint32_t)block;
```

`xnusys_var_vol.first` 现在是 `30580480 + 608512 = 31188992`（绝对值），
`window_read()` 里那处 `XNUSYS_LBA + at` 也去掉了。APFS 的 `mount_read()` 不动。

### 3. 范围守卫：两个区间，不是一个跨度

`zone_read` 和 `zone_write_fua` 原来都写死 `608448..653559`。改成：

```c
static inline bool
twoblock_block_is_ours(uint64_t block)
{
	if (block >= UINT64_C(608448)  && block <= UINT64_C(653559))  return true;
	if (block >= XNUVAR_PART_LBA_FIRST && block <= XNUVAR_PART_LBA_LAST) return true;
	return false;
}
```

引擎里的 `zone_write_allowed()` 同步改。**没有写成 min/max 跨度**，因为两段之间
隔着 5.1 GiB 的 userdata——写成 `>= 608448 && <= 30580479` 会把整个 userdata 放进来。

### 4. `DKIOCGETBLOCKCOUNT`

返回 `XNUVAR_PART_BYTES / XNUSYS_SECTOR` = `68719476736 / 512` = **134217728**。
分母和分子都是 64 位。32 位乘法在这里的结果是 0（`16777216 << 12` 的低 32 位全零），
文件系统会因此认为卷没有大小。

## 范围测试

`tests/test-xnuvar-range.c`，34 项，全通过：

```
all 34 range checks passed
```

覆盖：

- 旧窗口两端和外侧（608447 拒绝、608448 接受、653559 接受、653560 拒绝）
- **中间 5.1 GiB 空洞**：从 653560 到 13803263 每 7919 块采一次，一次都不能被接受
- 新卷两端（13803264 和 30580479 接受，13803263 和 30580480 拒绝）
- 新卷中段（1 GiB、22 GiB 处）
- 块 0、块 1（GPT 头）、userdata 新的首尾
- 卷描述符的数值，以及 `13803263 + 1 == 13803264`、`30580479 + 1 == 30580480` 两条接缝
- 64 位与 32 位扇区数之差

测试里的常量是从两个头文件抄的，不是 include 进来的——因为 include 驱动要拖进整个内核。
抄一份会在数字变动时失败，而那正是它该做的事。

## 内核

```
mach.development.bcm2837   15455000 B   20:41
✅ 含 _xnuvar_vol / _xnuvar_open / "carries the %llu MiB volume"
✅ 无警告
```

`make-kernel.sh` 的 `exit 2` 是 kext 符号集检查，这个配置上一向如此，不是编译失败。

## 还没做的

- 没有上机。GPT 未改，分区表还是原样。
- `xnuvar` 分区现在还不存在（GPT #89 是候选表里的表项）。
- 跨 4 GiB 边界的实机读写要等分区建好之后。
- HFS+ 还没在新分区上格式化。

## 文件

| 文件 | 内容 |
|---|---|
| `hakios.xnusys_blk.c.diff` | 卷描述符、xnuvar 节点、注册 |
| `hakios.mt6895_ufs_twoblock.c.diff` | 绝对 LBA、双区间守卫 |
| `ufs_two_block_engine.h.diff` | `zone_write_allowed` |
| `test-xnuvar-range.c` | 34 项范围测试 |
