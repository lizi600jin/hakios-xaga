# 里程碑 3 · 第一份文件内容（Trial738）— 已通过

2026-11-20。xaga，`boot_a`，一轮刷入后完整恢复原厂。

## 实机结果

一条启动里 **32 条 READ(10)**，全部 `ocs=0`。前 12 条是固定链，中间 15 条由查找决定，最后 2 条是文件本身：

```
path component 0 -> inode 61075      (/usr)
path component 1 -> inode 61297      (/usr/libexec)
path component 2 -> inode 61311      (/usr/libexec/GKCentralCache.momd)
path component 3 -> inode 61314      (.../VersionInfo.plist)
path walked blocks=15 last=61314
inode 61314 stream=61314 size=5230 found=1
extent logical=0 len=8192 phys=56172
file blocks=2 bytes=5230
```

`/tmp/vip738.bin`（设备上 56172 起两块）前 5230 字节的 sha256 为
`a66105d1635cffceb77213684bb66a1145ac76cbf6922f9a8cafcb96c8b1ea1c`，与主机提取**逐字节相同**。
文件头 `bplist00`。extent 是 8192 字节，末尾 2962 字节是块对齐填充，不是文件内容。

## 三处非显然的结构

这一轮真正花时间的不是读块，是把三个结构弄清楚：

**目录项按名字哈希排序，不按拼写。** 键是
`(对象, 类型, 名字哈希, 名字)`，内部键复制同一份哈希。只比较对象和类型会在同一目录的
第一片叶里原地打转——44 个条目的 `zoneinfo.default` 就是这样，`Pacific` 永远找不到。

哈希是 **CRC-32C（不做最终取反）**，对名字的 **UTF-32** 单元序列计算，**不含结尾 NUL**，
取低 22 位。用设备上八条真实记录验证：

```
configd 0x0eb6a1   seserviced 0x220353   ptpd 0x303b11   MST 0x3ee3a9
Moncton 0x1c5ff1   Iqaluit   0x3c8496   analytics 0x3e1955  _B0000.DAT 0x081e2d
```

**inode 的大小在扩展字段里，不在固定偏移。** 固定头之后是
`(数量, 已用字节, 表项[])`，每个表项 `(类型, 标志, 长度)`，字段本体跟在表之后、八字节对齐。
`DSTREAM`（类型 8）的头八个字节才是文件长度。按固定偏移读会读到创建时间——一个看起来
合理、但什么长度都不是的数字。表项要从计数器后**四字节**开始读，从计数器本身开始会读到
数量字节和第一个类型字节。

**大目录的条目跨多片叶。** 目录自身的记录起一片叶，按哈希排序的条目落在其后的若干片。
所以「这片叶没有」不构成答案：要看这片叶最后一个键是否已经越过 `(目录, 目录项)`，
越过才是真的没有。

## 修掉的错误

| # | 错误 | 后果 |
|---|---|---|
| 1 | `av_j_pick` 只比 `(对象,类型)` | 同目录条目跨叶时永远选第一片 |
| 2 | 猜哈希用 FNV/djb2/普通 CRC32 | 全不匹配，白跑一轮 |
| 3 | 哈希把结尾 NUL 也算进去 | 八条样本全部差一截 |
| 4 | inode 表项从计数器起读 | 类型读成数量字节 |
| 5 | 叶里没找到就无条件要下一片 | `/bin` 查 `usr` 一路翻到树尾 |
| 6 | `snapshot[]`/ring 上限仍按两级 | 五级路径越界 |
| 7 | 内核 `support/` 头没重编 | 实机跑的还是上一轮 trial |

其中第 7 个最贵：源文件改成 738，`make` 报成功，内核大小一字未变，`bsd/` 下的 `.o`
时间戳也是新的——但实机日志里 `trial=737`。删掉对象文件重建才把旧值挤出去。

## 中途换过目标

先选 `/usr/share/zoneinfo.default/Pacific/Fiji`（7955 字节，未压缩，`TZif2` 头，好看）。
它在一个 44 条目的目录里，正好撞上跨叶问题。为把「名字到叶」这一环单独验证掉，改选
`GKCentralCache.momd`（3 个条目，同叶），`VersionInfo.plist` 5230 字节、未压缩、`bplist00`。
两个目标的数据块 sha256 都在设备上核对过。跨叶处理仍然实现了，也仍有回归覆盖。

## 主机回归

```
-O2 -Wall -Wextra -Werror   33 PASS
ASan + UBSan                33 PASS   0 违规
test-ufs-path-walk           8 PASS（真实设备块，独立于桩）
```

## 原厂恢复

64 MiB 读回**逐字节等于原厂**，Android 40 秒起来，SELinux Enforcing。
