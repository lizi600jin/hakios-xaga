# Trial731 主 GPT 分区项数组 CRC 校验

xaga 上的 PID 4 从只读文件描述符读到 4,096 字节主表头和 16,384 字节分区项数组。用户程序确认表头将数组定位在 LBA 2，声明 128 项、每项 128 字节。完整数组的 CRC32 为 `ce39daa8`，与表头保存的值一致，随后的读取返回 EOF。主表头自身 CRC32 为 `7fdb9634`。[实机摘录](device-731-ufs.txt) · [用户程序](ufs_range_check.c)

| READ(10) 次序 | LUN 2 逻辑块 | 字节数 | 数据 CRC32 |
|---|---|---:|---|
| 1 | LBA 1 | 4,096 | `28a2836c` |
| 2 | LBA 2、3 | 8,192 | `c6984388` |
| 3 | LBA 4、5 | 8,192 | `fbe507f5` |

每次命令触发一次门铃，五个逻辑块分别通过 Android 参考 CRC 比对。驱动在三个请求全部成功后交付快照。主机故障测试依次在每次请求中引入数据损坏或停止信号，六个场景中调用方缓冲区均保持原值。此轮实机覆盖主表头与数组整体的完整性；分区项的语义及备份 GPT 内容仍待单独验证。[源码摘要](source-sha256.json) · [冻结参数](arguments731.json)

## 实机证据与恢复

A2 窗口与 Trial730 的 A13 相同。A8 读回候选镜像的 23,019,520 字节，全部匹配。A8 与原厂恢复后 A13 的 HLOG 窗口也逐字节一致，有效记录 SHA-256 为 `5bd47494d6662a45b72d7c442910c42dd5b37745fe2d4debd9d8d1ef577b5936`，正文共 495,816 字节。返回闸门记录 `trial=731 state=2 allowed=1`。[日志校验器](check-hlog.py)

A1 与 A12 的全长 67,108,864 字节均与原厂镜像逐字节相同。恢复后的 Android 报告槽位 `_a`、`boot_completed=1`、SELinux Enforcing。[镜像与恢复摘要](package-731.json)

## 复现

从本目录执行。主机测试使用 zlib 独立生成校验值，覆盖重签后将数组移至 LBA 3、声明长度错误、内容损坏和越界写等情况。[前一轮基线](../ufs-gpt-header-730/README.md)

```sh
python3 -B check-platform.py --reference-window /path/to/lun2-first64k.bin \
  --libmini /path/to/userland/libmini --loader-base /path/to/loader-project
python3 -B verify-package.py /path/to/boot.img
python3 -B regress-hlog.py /path/to/A2-baseline.bin /path/to/A13-post-restored.bin
```
