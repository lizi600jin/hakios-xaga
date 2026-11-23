# 下一轮：给 `/private/var` 64 GiB（Astra 拍板的方案 A）

2026-11-23 拟定。**尚未在任何设备上执行**——磁盘空间不足，等扩容后做。

## 一、为什么需要

Trial794 修好卷根权限后系统能进 iOS 了，但 174 MiB 的卷在 60 秒内被写满
（`freeBlocks=0`，997 个文件、5127 个目录），设置里 `Capacity` 和 `Available`
都是 Zero KB。真实使用需要 64 GiB。

## 二、真实分区布局（这台 xaga，4096 字节逻辑扇区）

```
#         start          end      sectors      GiB  name
85      3290112      3323903        33792     0.13  backup6
86      3323904     30580479     27256576   103.98  userdata
88     30580480     31236087       655608     2.50  xnusys
87     31236088     31240183         4096     0.02  flashinfo
```

物理顺序 `userdata → xnusys → flashinfo`，三段紧贴，中间零间隙。
分区号是乱的：86=userdata，87=flashinfo，88=xnusys。

`xnusys` 内部：

```
块 0 .. 608447       2.32 GiB   APFS 容器（iOS 只读根，45138 个文件）
块 608512 .. 653055   174 MiB   当前 HFS+ 卷
块 653560 ..            8 MiB   loader HLOG
```

## 三、方案 A：缩小 userdata，新建 `xnuvar`（GPT #89）

```
#86 userdata   3323904 .. 13803263    10479360 块   39.975586 GiB
#89 xnuvar    13803264 .. 30580479    16777216 块   64.000000 GiB
#88 xnusys    30580480 .. 31236087      655608 块    2.500000 GiB   ← 不动
#87 flashinfo 31236088 .. 31240183        4096 块    0.015625 GiB   ← 不动
```

`xnusys` 一字节不挪，APFS 容器零搬移，旧的 174 MiB 卷留着当回退。

若需要空闲 ≥ 64 GiB 而非分区 ≥ 64 GiB，选 65 GiB：
`#89` 起点 13541120，`#86` 末尾 13541119，其余边界不变（mkfs 后空闲 64.803 GiB）。

### 为什么不是方案 B（搬 xnusys）

即使保留内部偏移不变，B 仍要改读写窗口、GPT 范围校验、固定 CRC 和 HLOG 位置，
而且要搬 2.32 GiB。A 的改动面小得多。

## 四、GPT 生成（只在主机副本上）

```bash
python3 mtk/tools/gpt-carve.py primary.bin secondary.bin 31240192 \
    --lba-size 4096 --shrink userdata --name xnuvar --gib 64
```

只有 `#86` 和 `#89` 的表项会变。当前两份 GPT 副本的数组 CRC 是 `ce39daa8`。

## 五、写入顺序

1. **离线、`/data` 未挂载、旧 dm 映射已关闭**
2. 先写备数组 `31240184..187` 和备头 `31240191`
3. 再写主数组 `2..5` 和主头 `1`
4. 每个阶段 flush 并回读校验
5. **不要整段回写末尾 34 块**——其中 26 块是 `flashinfo`
6. 重进 recovery 核对 `by-name` 和容量
7. 对新的 `userdata` 做原厂兼容的 Format Data / FBE
8. 阻止旧 F2FS 超块在缩小的设备上被自动挂载

## 六、驱动改造

### 新节点 `/dev/xnuvar`

- 复用旧的 var block strategy、512 字节 BSD 扇区、RMW、`buf_unmap`/`buf_biodone`、
  flush、锁和租约
- `var_hfs=xnuvar` 启动，旧 `/dev/xnusys_var` 保留作回退
- 新节点以 `rel=0` 映射到物理 LBA `13803264`

### 范围校验

读写共同在 **64 位**验证整个请求、LUN 容量和 GPT 已核准的区间，
再下转 `READ/WRITE(10)` 的 32 位 LBA。越界在 doorbell 之前拒绝。

**不要**用新 base 减 `XNUSYS_LBA`，**不要**把两段连成一个许可范围。

要实查的位置：`hakios.mt6895_ufs_twoblock.c` 的 1117、1122、1164、1198、1233 行，
`ufs_two_block_engine.h` 的 204、353、700 行。旧的 ioctl 和只读根探针不获得新范围。

### 其他

- `twoblock_reference` 固定了旧 GPT CRC，要针对最终候选表核对，
  或按双表 CRC + 预期 GUID + 名字 + 边界验证
- `DKIOCGETBLOCKCOUNT` 应回 `134217728 × 512`；`d_psize` 仍是 512
- 正常字节偏移和 HFS 挂载容量路径都是 64 位
- `mtk_ufs_byte_offset` 诊断参数在 `:846` 拒绝 `> UINT32_MAX`，不适合高位验收

`47,357,696` 虽然能用 `uint32_t` 表达，但已经越过 LUN 末尾——所以 64 位校验是必须的。

## 七、HFS+ 格式化

```bash
mkfs.hfsplus -J -b 4096 -U 0 -G 0 -M 0755 -v <name> <image>
```

分配块继续用 4096：4096 × 16777216 = 64 GiB，`totalBlocks` 32 位够用。
主机已验证：offset 1024 和 68719475712 的主备卷头都是 `H+`，
初始空闲约 63.803 GiB。

## 八、验收

### 1. 跨界写入

在未建文件系统的 `xnuvar` 上写不同标记，测相对块：

```
0, 1048575, 1048576, 1048577, 8388608, 16751001, 16777215
```

全写后 flush，独立冷读，并从 Android 的 `by-name` 对照。
**本分区外的请求只在主机假控制器上验证无命令发出。**

### 2. 双门禁（覆盖完整启动）

794 保留的内核环从约 221.8 秒才开始，**单搜 `Corpse`/`CPU=0` 不足以证明前段为零**。
要用早期计数或完整输出，同时看：`lsd` 退出、SpringBoard IOSurface、撤销启动条、设置页。

### 3. 容量与持久

- `DKIOCGETBLOCKCOUNT`、`statfs("/private/var")`、挂载来源三者一致，确认是 HFS
- 64 GiB = 68.72 GB，**总量和可用分开报**
- 两轮脏重启，各用不同已 fsync 的内容比 hash，最后干净卸载
- 相同时间窗观察 `freeBlocks` 和写入量是否收敛

### 4. Android 侧

`/data` 挂载、解锁、重启、隔离哨兵，以及 APFS/flashinfo 的哈希对照。

## 九、回退

1. 先用旧的 174 MiB var 启动
2. 若要恢复旧 GPT：先导出新 var，再恢复本次保存的原双表，重建 userdata

风险由操作者的 MTK BROM 深刷兜底（他有多次救砖经验）。

## 十、执行前的准备（未完成）

- [ ] 完整备份当前双 GPT（`sdc-head-1MiB.bin` / `sdc-tail-1MiB.bin` 已有）
- [ ] 完整备份 `xnusys`（含 APFS 容器 + 旧 var + HLOG），校验 SHA256
- [ ] 备份 `flashinfo`
- [ ] 备好可恢复的 boot
- [ ] **主机磁盘空间**：当前只剩约 100 MiB，需要先扩容
