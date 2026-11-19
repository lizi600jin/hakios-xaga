# Trial733 从 XNU 读取 LUN 2 的备份 GPT

xaga 上的 XNU 使用三次 READ(10) 取得了备份 GPT 表头和完整分区项数组。内核核对了五个 4 KiB 块，每块 CRC32 均与原厂 Android 的独立采样匹配。用户态 PID 4 重新计算了 92 字节表头及 16,384 字节分区项数组的 CRC32，结果分别为 `ba3342fe` 和 `ce39daa8`。[实机日志](device-733-ufs.txt) · [Android 采样校验](reference-733.json)

| 内容 | 起始 LBA | 块数 | 实机字节偏移 | 请求数据 CRC32 |
|---|---:|---:|---:|---|
| 备份表头 | 31240191 | 1 | 127959822336 | 4b13d623 |
| 条目数组前半 | 31240184 | 2 | 127959793664 | c6984388 |
| 条目数组后半 | 31240186 | 2 | 127959801856 | fbe507f5 |

表头指回主表 LBA 1。备份数组中有 88 个已用项和 40 个空项，其中一项开始于首个可用 LBA 之前，条目范围之间的交集数为零。原厂采样中的主备数组也逐字节相同。[主 GPT 实测](../ufs-gpt-entries-732/README.md) · [用户程序](ufs_range_check.c)

刷入后读回的 23,019,520 字节与候选镜像完全一致。恢复前后两次读取的 HLOG 窗口逐字节相同，有效正文 493,689 字节。回刷后读回的全部 67,108,864 字节与原厂镜像相同，Android 报告槽位 `_a`、启动完成和 SELinux Enforcing。[镜像与恢复记录](package-733.json)

## 复现

以下命令在本目录运行。镜像和现场采样均使用本地路径。

```sh
python3 -B verify-package.py /path/to/trial733/boot.img
python3 -B regress-hlog.py /path/to/A2-baseline.bin /path/to/A13-post-restored.bin
python3 -B check-platform.py --reference-window /path/to/lun2-first64k.bin \
  --backup-header /path/to/backup-header.bin --backup-array /path/to/backup-array.bin \
  --libmini /path/to/userland/libmini --loader-base /path/to/loader-src
```
