# 里程碑 7 · `/private/var` 有了 64 GiB，而且系统能进（Trial796）

2026-11-23。设备已回安卓。这是第一次跑完整个启动：SpringBoard 出 28 个 IOSurface，
framebuffer 每秒 10 帧，屏幕 29% 不是黑的。

## 一、结果

```
轮次            卷        zone write   Corpse  CPU拒绝  IOSurface  结果
794        174 MiB         8517        0       0         23      进了，但卷 60 秒写满
795c        64 GiB         2634        0       20        0       没进
796         64 GiB           69        0      701        28      进了，设置显示 69 GB
```

`796` 的设置页：**Capacity 69 GB**，Available 仍是 Zero KB（后面说）。

`zone write` 从 2634 次降到 **69 次**，是这轮的关键——写路径从单块改成了多块。

## 二、分区：从 174 MiB 到 64 GiB

### 改了什么

```
#         start          end      sectors      GiB  name
86      3323904     13803263     10479360    39.98  userdata   （原 103.98，缩了 64）
89     13803264     30580479     16777216    64.00  xnuvar     （新建）
88     30580480     31236087       655608     2.50  xnusys     （一字节没动）
87     31236088     31240183         4096     0.02  flashinfo  （没动）
```

`xnusys` 前面那 2.32 GiB 是 APFS 容器（iOS 只读根，45138 个文件），
所以**不能把 xnusys 的起点往前挪**——那会把容器搬到别的地方去。
新建一个分区是唯一不用搬数据的做法。

### 怎么改的

`mtk/tools/gpt-carve.py` 在主机上生成候选表，只写主机副本：

```
只有 #86 和 #89 的表项变化
主备两个头的 CRC 都重算
```

写入顺序按「先备后主」：备数组 LBA 31240184..187 → 备头 31240191 →
主数组 LBA 2..5 → 主头 LBA 1，每一步 `conv=fsync` 然后回读比对 md5。
四个 md5 与主机期望值逐一匹配。

**没有整段回写末尾 34 块**——其中 26 块属于 `flashinfo`。

### Android 侧

`fastboot -w` 把 `userdata` 按新尺寸重新格式化，`boot_completed=1`，
`/data` 报 40 GiB。

**改表之后 Android 仍然用旧的 F2FS 超块挂载过一次**（自报 104 GiB，
而分区只有 40 GiB——末端越界进了 `xnuvar`）。发现后立刻进 fastboot 停掉，
先格式化再继续。这一步如果放着不管，写 `/data` 高位会破坏新分区。

## 三、写路径：单块改多块

### 原来有多慢

```
795c: 2634 次单块写 = 10.8 MiB / 55 秒
      平均每块 20 毫秒 → 0.2 MB/s
```

装一个 1 GB 的 App 要 85 分钟。

`support/ufs_read_contract.h` 里 `ur_make_write_flags()` 把长度写死成一块：

```c
c->upiu[14] = 0x10;      /* expected data length 0x00001000 = 4096 */
c->upiu[16] = 0x2a;      /* CDB[0] WRITE(10) */
```

### 上限不是 CDB

`WRITE(10)` 的 `CDB[7..8]` 能表达 65535 块 = 256 MiB。真正的天花板是
**JESD223 单条 PRD 的 256 KiB**：`prd[3] = bytes - 1` 是 18 位，最大 `0x3ffff`。

所以一次最多 **64 个 4 KiB 块**。卡在这里，`utrd[7]` 就能保持 `0x01000001`
（单条 PRD），`struct ur_command` 的 80 字节布局不用动。

### 改法

```c
ur_make_write_blocks_flags(uint64_t lba, uint32_t blocks, bool fua, struct ur_command *c)
{
    if (blocks == 0u || blocks > UR_WRITE_MAX_BLOCKS) return false;   /* 64 */
    const uint32_t bytes = blocks * UR_BLOCK_BYTES;
    c->upiu[12..15] = bytes;         /* expected data length, big-endian */
    c->upiu[23..24] = blocks;        /* CDB[7..8] transfer length */
    c->prd[3] = bytes - 1u;          /* PRD length */
}
```

驱动层在 `hakios.xnusys_blk.c` 里按 64 块切：

```c
while (done < nblk) {
    uint32_t chunk = (nblk - done > max_wblk) ? max_wblk : (uint32_t)(nblk - done);
    hakios_mt6895_ufs_zone_write_blocks(vol->first + first + done,
        chunk, mapped + done * XNUSYS_BLOCK, false);
    done += chunk;
}
```

部分写的路径保留 RMW（边缘块读改写），中间完整的块照样批量。

**留了一个开关**：`ufs_write_max_blocks=1` 退回单块，出事能降级。

### 证据它真的可行

不是猜的。loader 自己就在同一个 MT6895 UFSHCI、同一条 LK 传输队列（`0x51266400`）、
同一个 UCD（`0x60101000`）、同一块 DMA 缓冲（`0x6010e000`）、同一个 LUN 2 上
用多块 `READ(10)`/`WRITE(10)` 跑到过 8 MiB
（`loader-src/mtk/loader/ufs.h:355-368` 和 `781-790`）。

## 四、这一轮踩过的坑，全部修好了

### 1. loader 的 GPT 指纹

`mtkloader.c:2720` 读 LBA 8（512 字节扇区 = 4096 字节块的 LBA 1，GPT 主头），
要求 CRC32 等于 `0x28a2836c`——那是**改表前**那一版的值。改表之后必然失败，
屏幕停在 `ufs-read guard: predecessor READ/reference failed; manual fastboot required`。

新值 `0xec726acc`。顺便把「命令没应答」和「应答了但内容不符」分开打印：
一句话涵盖两种失败，之前为此浪费了整轮启动。

### 2. 内核里三处 GPT 指纹

```c
GPT_ARRAY_MODE  lba == 4u    0xa0d23c1c → 0x7bbefbac
backup[0] 备头               0x4b13d623 → 0x8fc33f83
backup[3] 备数组+2           0xa0d23c1c → 0x7bbefbac
```

`chain[]` 那九项**全部没变**——改 GPT 没碰 xnusys 任何一个字节，逐块验过。

### 3. 范围守卫把相对偏移当绝对地址

我第一版写成 `block >= 608448 && block <= 653559`，那是 xnusys **内部**的偏移。
按新 GPT，绝对块 608448..653559 落在 **`super`（#77）** 里。

内层守卫当时拦住了，没有写坏任何东西，但一个点名别的分区的范围检查不该留。
现在两段都是绝对：`31188928..31234039`（xnusys 的 zone）和 `13803264..30580479`（xnuvar）。

### 4. `window_read` 的上界

`if (offset / VR_BLOCK_BYTES + len / VR_BLOCK_BYTES > 653560) return false;`

这是 xnusys 内的偏移。新卷的块 0 是绝对 13803264，被它直接拒绝——
文件系统连自己的卷头都读不到。

### 5. 引擎的 `VRQ_DATA_WINDOW_READ`

只收旧窗口。扩成两个不重叠的区间，**没有写成 min/max 跨度**：
两段之间是 60 万块 APFS 容器，写成一个跨度会把整个容器的读放行。

### 6. `DKIOCGETPHYSICALBLOCKSIZE`

`hfs_mountfs` 在 `hfs_vfsops.c:1149` 问这个，只接受 `ENOTSUP` 或 `ENOTTY`，
别的都变成 `ENXIO` 然后挂载直接结束。第一版 795b 就是这么失败的：

```
hfs_mountfs: DKIOCGETPHYSICALBLOCKSIZE failed
hfs_mount: hfs_mountfs returned error=6 for device xnuvar
```

旧设备（174 MiB 卷）没遇到，因为它的 `default` 分支返回 `ENOTTY`；
我给 `xnuvar` 写的 `default` 用了 `eno_ioctl()`，那个返回 `ENXIO`。

两处都改了：实现这个 ioctl（返回 512），`default` 改回 `ENOTTY`。

### 7. 卷根权限（794 就修了，这里记一笔）

`init.c` 的 `memset(&args, 0, ...)` 让 `hfs_mask = 0`，
而 `hfs_vfsops.c:1367` 只跳过 `VNOVAL`，于是 `hfs_dir_mask = 0`，
`hfs_catalog.c:3830` 给没有类型位的卷根合成 `S_IFDIR | 0` = 权限 **0000**。

`lsd` 和 `backboardd` 都以 `mobile`(501) 运行，连遍历都做不到。
改成 `uid/gid/mask/pad` 全填 `VNOVAL`，再 `chmod("/private/var", 0755)`。

`sysimage/var.skel` 也删了 `/private/var/vm`——不删的话第 19 秒建 swapfile，
iOS 分支的 `MAX_SWAP_FILE_SIZE` 是 128 MiB。

## 五、还没解决的：Available 显示 Zero KB

卷头说得很清楚：

```
totalBlocks = 16777216  = 64.00 GiB
freeBlocks  = 16679329  = 63.63 GiB   实际只用了 382 MiB
```

三层都算不出 0：

| 层 | 值 |
|---|---|
| `hfs_statfs` 的 `f_bavail` | `hfs_freeblks(mp,1)` = `16679329 - 64000` = 16615329 = 63.38 GiB |
| `reserveBlocks` | `MIN(totalBlocks/100, 250MiB/4096)` = 64000 = 250 MiB |
| `vfs_attrlist.c:1447` 的乘法 | `4096 × 16615329`，`off_t` 64 位，不溢出 |

**794 那次显示 Zero KB 是真话**（174 MiB 卷确实写满了，`freeBlocks=0`）。
796 是 63 GiB 可用却显示 0，性质完全不同。

系统能正常读写（382 MiB 数据落盘，没有 ENOSPC），所以这是显示层的问题，
不影响实际存储。要查的话，下一步是在 iOS 里直接对 `/private/var` 做一次 `statfs`，
看内核给的值到底是什么。

## 六、文件

| 文件 | 内容 |
|---|---|
| `hakios.xnusys_blk.c.diff` | 双卷描述符、`/dev/xnuvar`、多块写循环、两个 ioctl 修复 |
| `hakios.mt6895_ufs_twoblock.c.diff` | 绝对 LBA、双区间守卫、`window_read` 上界、`zone_write_blocks` |
| `ufs_two_block_engine.h.diff` | `VRQ_DATA_WINDOW_READ` 双窗口、`zone_write_allowed` 双区间 |
| `ufs_read_contract.h` | 完整文件，`ur_make_write_blocks_flags` 在这里 |
| `init.c.diff` | 卷根权限 |
| `mtkloader-guard.diff` | loader 的 GPT 指纹 |
| `test-ufs-multiblock-write.c` | 多块写测试，含假控制器端到端 |
| `test-xnuvar-layers.c` | 走真实 `vrq_encode` 的两层守卫测试 |
| `test-xnuvar-range.c` | 纯算术的范围测试 |

镜像：`logs/ufs-apfs-fstree-root-736/trial736/boot-796.img`
