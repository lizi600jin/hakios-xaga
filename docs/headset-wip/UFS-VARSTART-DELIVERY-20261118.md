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

后续的 Trial724 已在主机模型上验证同一事务按请求选择一块或两块，覆盖首 16 个 LBA 内的全部 31 种合法组合。[724 主机交付与复测](raw/ufs-read-length-724/README.md)
