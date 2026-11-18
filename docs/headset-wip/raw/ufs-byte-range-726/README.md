# Trial726 · XNU 跨块字节读取

在 xaga 实机上，XNU 从 LUN 2 的字节偏移 12031 读取 770 字节。驱动用一条 READ(10) 取回覆盖此范围的两个 4096 字节逻辑块，触发一次门铃。上层缓冲区只收到 LBA 2 末尾的 257 字节和 LBA 3 开头的 513 字节。刷机试验完成后已恢复原厂。[实机驱动记录](device-726-ufs.txt) · [Trial725 基线](../ufs-mixed-length-725/README.md)

| 观测点 | 实机结果 |
|---|---|
| 底层读取 | LBA 2、3，共 8192 字节，2048/2048 个数据词有效 |
| 两块 CRC32 | `0x0d024e0c`、`0x58f10429`，均匹配冻结参考 |
| 实际交付 | 770 字节，目标容量 770 |
| 切片与调用者 CRC32 | 两处独立计算，均为 `0x8f350378` |
| 内核终态 | `control=0x2 shared=1 failed=0 delivered=770` |
| loader 返回检查 | `trial=726 state=2 allowed=1` |

刷前窗口与 Trial725 的末次采集逐字节相同。刷后日志正文为 491,149 字节，头部原因和状态码均为零，实际启动参数与冻结值逐字相符。DS 在恢复前采集了十条驱动记录。恢复后，协调人在 Android 上重新读取同一段 HLOG，4,198,400 字节与恢复前的采集完全一致。[日志与恢复摘要](package-726.json) · [冻结参数](arguments726.json)

原厂镜像写回 `boot_a` 后，全分区读回的 67,108,864 字节与原厂件逐字节相同。Android 已启动，槽位为 `_a`，`sys.boot_completed=1`，SELinux 为 `Enforcing`。[恢复记录](package-726.json)

## 读取与交付

平台在建命令前检查偏移、长度、容量、几何边界及逐块参考。只传偏移或长度、范围超过两块，或范围模式与混合读取同时启用时，平台直接拒绝请求。该轮启动入口只允许一个读取者。两个整块校验完成后，驱动把切片保存在静态暂存区，通过租约检查后复制到目标，并从目标重新计算 CRC32。[平台与测试增量](725-to-726-platform.patch) · [loader 增量](725-to-726-loader.patch)

主机夹具把真实窗口与目标逐字节比对，并检查目标两端的哨兵。实际容量从 770 改为 769 字节时，门铃数为零。读完后、提交交付前触发 STOP 时，目标保持原样。单独翻转目标的一个字节，暂存区的 CRC32 仍为 `0x8f350378`，目标的 CRC32 变为 `0x4192ff1d`，测试以失败退出。[源码摘要](source-sha256.json)

实验镜像为 23,015,424 字节，SHA-256 为 `dc2c256ca5a875194960a19c39949e289abd4f6647c18f4df1204e92c80e0ebc`。打包后的 loader 参数基串为 334 字节，追加内核版本后的完整参数含 NUL 共 395 字节。[验包记录](package-726.json)

## 复测

从本目录运行。平台脚本按顺序应用 724 至 726 的源码增量。基础算术、切片和引擎测试使用合成数据。传入已冻结的 64 KiB 参考窗口会另外运行平台的优化构建与 ASan/UBSan 构建。原始镜像、HLOG、选择器及参考块内容留在实机工作目录。

```sh
python3 -B check-platform.py
python3 -B check-platform.py --reference-window /path/to/lun2-first64k.bin
python3 -B check-hlog.py device-726-ufs.txt
python3 -B check-hlog.py device-726-ufs.txt --pre /path/to/A2-pre.bin --post /path/to/A13-post.bin
python3 -B verify-package.py /path/to/boot.img --loader /path/to/loader.bin
```

日志校验器检查完整记录的顺序，并拒绝错误的试验号、错误的切片长度、错误的调用者 CRC 和缺行记录。提供原始采集时，也会验证 HLOG 头、正文校验、有效记录摘要和实际启动参数。[日志校验器](check-hlog.py)
