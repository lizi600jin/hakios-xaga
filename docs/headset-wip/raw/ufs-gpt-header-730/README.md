# Trial730 从 XNU 用户进程校验 GPT 主表头

在 xaga 上，PID 4 从只读字符设备读取 LUN 2 的 LBA 1。一次 READ(10) 交付 4,096 字节，块 CRC32 为 `28a2836c`。用户程序按表头声明的 92 字节重算 CRC32，将计算期间的 CRC 字段视为零，得到与表头一致的 `7fdb9634`。随后读取返回 EOF，累计门铃保持为 1。[实机日志](device-730-ufs.txt) · [用户程序](ufs_range_check.c) · [UEFI GPT 表头定义](https://uefi.org/specs/UEFI/2.10/05_GUID_Partition_Table_Format.html#efi-partition-table-header)

| 表头字段 | 实测值 |
|---|---:|
| 主表头 LBA | 1 |
| 备份表头声明的位置 | 31,240,191 |
| 首个、最后一个可用 LBA | 34、31,240,183 |
| 分区项数组起始 LBA | 2 |
| 分区项数量、单项字节数 | 128、128 |
| 声明的数组长度 | 16,384 字节，合 4 块 |

本轮验证覆盖主表头 CRC 和声明布局。完整分区项数组 CRC、备份表头的实际内容仍待实机验证。用户程序使用此前测得的 LUN 2 容量核对备份表头声明的位置。[冻结参数](arguments730.json)

## 设备与恢复记录

A2 基线与 Trial729 的 A13 窗口逐字节相同。A8 读回的候选前缀与冻结镜像的 23,019,520 字节一致。A8 与恢复后 A13 的 HLOG 窗口相同，正文为 491,986 字节，有效记录 SHA-256 为 `4d7ce06a356bac599928577805990ff23bf3d883746bbd53d192b5f97597681a`。loader 返回闸门为 `trial=730 state=2 allowed=1`。[日志校验](check-hlog.py)

恢复后 Android 完成启动，槽位为 `_a`，SELinux 为 Enforcing。A1 与 A12 的全长 67,108,864 字节均与原厂镜像逐字节相同。[镜像与恢复摘要](package-730.json)

## 复现

在本目录运行。用户程序的主机测试使用 zlib 独立生成表头 CRC，覆盖合法扩展表头、几何字段错误、损坏的 CRC，以及短读和缓冲区哨兵异常。[前一轮归档](../ufs-stream-range-729/README.md) · [源码摘要](source-sha256.json)

```sh
python3 -B check-platform.py --reference-window /path/to/lun2-first64k.bin \
  --libmini /path/to/userland/libmini --loader-base /path/to/loader-project
python3 -B verify-package.py /path/to/boot.img
python3 -B regress-hlog.py /path/to/A2-baseline.bin /path/to/A13-post-restored.bin
```
