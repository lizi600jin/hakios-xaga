# Trial723 实机交付：可选起点的双块 READ(10)

MT6895 上的 XNU 内核在本次启动从参数 `mtk_ufs_start_lba=2` 取起点，向 LUN 2 发出一条 READ(10)，一次门铃，读回 8192 字节。Trial722 用相同块数从 LBA 1 开始。两轮都已完成原厂恢复。

| 实机试验 | 起点与块数 | 两块的 CRC32，按读取顺序 | 返回闸门 |
|---|---|---|---|
| 722 | 1，2 块 | `28a2836c`、`0d024e0c` | `trial=722 state=2 allowed=1` |
| 723 | 2，2 块 | `0d024e0c`、`58f10429` | `trial=723 state=2 allowed=1` |

两轮的重叠块 LBA 2 校验和一致。723 的完整启动参数与冻结文件逐字匹配，八条驱动记录顺序正确，`data_words=2048/2048`，`response_valid=0x1fff`，OCS 和失败位均为 0。内核终态为 `control=0x2 shared=1 failed=0`。这些结论覆盖本轮有界请求；线路传输、DMA 新鲜度和性能均未单独测量。[722 解析记录](raw/device-ufs-varstart-723/T722-A13-parse-report.json) · [723 解析记录](raw/device-ufs-varstart-723/T723-A13-parse-report.json)

## 采集与恢复

A2 基线前 4,198,400 字节与 T722 的 A13 窗口逐字节相同。T723 的 A13 读取同一范围，HLOG 正文为 489,763 字节，头部原因和状态码均为 0，正文整数和匹配。仅用头的 72 个有效字节和声明长度的正文比较，A2 与 A13 的有效记录摘要分别为 `6e89635719a9c9a1a8b798134acca0fb10919803c64239782479193a2a70be83` 和 `8f63096b244915a9fa09e14d49f0c97a70dddc789705176a1fff72d8e37bd3a8`。主机解析输出 `FORMAT_PASS_ONLY`，实机 PASS 另由评审结合两次原始采集作出。[结项索引与八条驱动记录](raw/device-ufs-varstart-723/R23-FINAL-INDEX.json)

实验镜像为 23,011,328 字节。设备第一次返回 Android 时，读回的 `boot_a` 仍保留完整实验镜像前缀。随后单独写回原厂件，Sending 和 Writing 均为 OKAY。再次启动后 Android 报告 `_a`、`boot_completed=1`、SELinux Enforcing；全长读回的 67,108,864 字节与原厂件逐字节相同，SHA-256 为 `c5f8e7bde8e9f1d824d0c7915f7d701adce8142656a82c20e10ccb75e15e01fc`。[打包件摘要](raw/device-ufs-varstart-723/package-723.json) · [恢复核验](raw/device-ufs-varstart-723/T723-A12-verdict.json)

A1 有五次读取未逐条留存退出码。A8 的上线记录为 211 秒，末尾 adb 计数却记为 0；接手方在恢复前单独核验了在线 xaga、当前槽位与实验分区内容。原始镜像、采集、选择器和参考块内容仅在本机留存，仓库归档核验摘要、源码与测试。[源码与复测](raw/final-ufs-varstart-source-723/README.md) · [721 前序交付](UFS-VARREAD-DELIVERY-20261118.md)

Trial724 已将一块或两块的请求长度接入平台。主机引擎覆盖首 16 个 LBA 内的 31 种合法组合，平台层接受有完整参考的 8 种组合。724 实机从 LBA 3 读回一块，CRC32 与 723 的第二块一致。手机已恢复原厂，boot_a 全长读回核验通过。[724 平台交付与复测](raw/ufs-read-length-724/README.md)

Trial725 在同一次实机启动中复用一份 UFS 租约，依次读取 4096、8192、4096 字节。三次各触发一次门铃，重叠的 LBA 3 校验和一致，原厂恢复已完成全长读回核验。[725 实机日志与复测](raw/ufs-mixed-length-725/README.md)

Trial726 在 xaga 实机上按字节偏移 12031 读取 770 字节。一条双块 READ(10) 触发一次门铃，覆盖 LBA 2 末 257 字节与 LBA 3 前 513 字节。调用者缓冲区的 CRC32 为 `0x8f350378`，与切片和独立参考一致。原厂 `boot_a` 已恢复并通过全长读回核验。[726 实机日志与复测](raw/ufs-byte-range-726/README.md)

Trial727 在 xaga 内核中通过 `vn_rdwr` 读取只读字符设备。一次双块读取交付跨块的 770 字节，返回 `error=0 residual=0`，调用者 CRC32 为 `0x8f350378`。写打开和 EOF 均未增加门铃次数。原厂 `boot_a` 已恢复并核对全长。[727 实机记录与复测](raw/ufs-vfs-range-727/README.md)

Trial728 在 xaga 上由 PID 4 的用户进程调用 `pread(2)`。同一进程从偏移 12031 读到 770 字节，CRC32 为 `8f350378`，两侧哨兵保持完整。EOF 返回 0，重复读返回 EBUSY。三次调用共触发一次 UFS 门铃。恢复后的 64 MiB `boot_a` 与原厂件逐字节相同。[728 实机日志与复测](raw/ufs-userspace-range-728/README.md)

Trial729 让同一用户文件描述符分段读取 770 字节的快照。首次 EOF 未触发门铃。普通读取 256 字节后，穿插一次从偏移 250 起的 19 字节定位读取，下一次普通读取仍从偏移 256 开始。末尾请求 512 字节返回 255，再读返回 EOF。六次调用共触发一次 READ(10)。恢复前后的两份 HLOG 相同，原厂分区已逐字节核对。[729 实机记录与复测](raw/ufs-stream-range-729/README.md)
