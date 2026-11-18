# Trial725 · 同一次启动串行切换 UFS 读取长度

Trial724 在实机上验证了一次单块读取。Trial725 将同一条队列、同一块 DMA 缓冲区和同一份 loader 租约用于三个连续的 READ(10) 请求。实验启动参数为 `mali_trial=725 mtk_ufs_twoblock=1 mtk_ufs_start_lba=3 mtk_ufs_blocks=1 mtk_ufs_mixed=1`。[冻结参数](arguments725.json) · [724 实机基线](../ufs-read-length-724/README.md)

## xaga 实机结果

同一次启动中，内核依次完成以下三条 READ(10)。每条请求只触发一次门铃，响应失败位和 OCS 均为 0，驱动记录了恢复回读与复制成功。LBA 3 的三次 CRC32 均匹配冻结参考，也与 Trial724 的单次读取一致。[十二条原始驱动日志](device-725-ufs.txt)

| 请求 | LBA | 读回字节 | 有效数据词 / 尝试数据词 | 逐块 CRC32 |
|---|---|---:|---:|---|
| 1 | 3 | 4096 | 1024 / 1024 | `0x58f10429` |
| 2 | 2、3 | 8192 | 2048 / 2048 | `0x0d024e0c`、`0x58f10429` |
| 3 | 3 | 4096 | 1024 / 1024 | `0x58f10429` |

内核终态为 `failed=0`，loader 记录 `trial=725 state=2 allowed=1`。刷前 HLOG 窗口与 724 的末次采集逐字节相同。725 的日志正文为 494,172 字节，启动参数与冻结值逐字相符。恢复前后两次采集的 4,198,400 字节窗口完全相同。[实机核验记录](device-725.json)

原厂镜像已写回 `boot_a`。重启后 Android 报告槽位 `_a`、`boot_completed=1` 和 SELinux `Enforcing`。全分区读回的 67,108,864 字节与原厂件及刷前副本逐字节相同。[恢复记录](device-725.json)

## 平台与主机验证

打开混合长度模式时，平台在映射前验证完整的三项请求及逐块参考。每次成功取得事务使用权后，平台按当前请求长度设置数据区边界。引擎分别核验描述符、快照、完成状态和恢复回读。复制、逐块 CRC 及租约状态转换成功后才开始下一次请求。试验沿用 724 的 READ(10) 事务引擎和控制器访问器。[平台与测试补丁](724-to-725-platform.patch) · [loader 租约补丁](724-to-725-loader.patch)

严格编译、ASan/UBSan 在合成窗口和已冻结的 64 KiB 参考窗口上均通过。模型从已经放置的 CDB 和 PRD 解码读取范围，在每次完成时逐字节比对描述符、完整 8192 字节数据区以及复制结果。第三次请求还检查复制区第二页保留前一次的数据。对三次请求分别注入末词数据损坏，均在对应位置结束序列并标记租约为 POISONED。分别在三次复制与 CRC 检查结束后、租约重新接受请求前加入 STOP，均以已恢复租约结束，未发出后续门铃。[复测脚本](check-platform.py) · [源码摘要](source-sha256.json)

内核以 724 源码树为基线，在干净的 BUILD 目录执行 `xnu-build.sh -j4 build`。loader 沿用 724 的构建参数，换用 725 租约与本目录冻结的 `extra_arguments`。最终包独立比对了回填后的 loader、原厂 Android 内核、XNUP、XNU 内核、ramdisk 和完整启动参数。[打包件摘要](package-725.json) · [验包脚本](verify-package.py)

## 复测

在本目录执行下列命令。平台脚本从 723 归档依次应用已冻结的 724、725 补丁，核对生产源码摘要，再编译主机夹具。有原始参考窗口时可传入 `--reference-window /path/to/lun2-first64k.bin`。

```sh
python3 -B check-platform.py
python3 -B check-hlog.py device-725-ufs.txt
```

HLOG 复测将十二条驱动记录装入合成窗口。一个正例被接受，23 个负例被拒绝，涵盖缺行、乱序、额外记录、错误 CRC、门铃数、数据覆盖、租约及旧记录。解析器在成功时返回 `FORMAT_PASS_ONLY`。[日志解析器](parse-hlog-725.py)
