# Trial732 主 GPT 分区项逐项审计

xaga 的 LUN 2 主 GPT 有 **88 个已用条目、40 个未用条目**。其中一个已用条目的起始 LBA 早于表头声明的 `FirstUsableLBA`，实机审计记录 `before=1 consistent=0`。主表头 CRC32 为 `7fdb9634`；完整分区项数组的 CRC32 为 `ce39daa8`，与表头声明一致。[实机摘录](device-732-ufs.txt)

| 检查项 | 实测数量 |
|---|---:|
| 起点早于首个可用 LBA | 1 |
| 终点晚于最后可用 LBA | 0 |
| 起止顺序颠倒 | 0 |
| 具有交集的已用条目对 | 0 |
| 唯一分区 GUID 全零的已用条目 | 0 |
| 非零唯一分区 GUID 相同的已用条目对 | 0 |

用户程序按分区类型 GUID 是否全零识别已用条目。区间按包含两端点计算，每一对条目只统计一次。程序只输出计数。首次读取触发三次 READ(10)，覆盖 LBA 1 至 5；数组读取及 EOF 复用同一份快照。`final passed=1` 表示完成了读取、校验与审计，布局检查的结果保存在单独的 `consistent` 字段。[用户程序](ufs_range_check.c) · [冻结参数](arguments732.json)

## 采集与恢复

Trial731 的 A13 与本轮 A2 的首个窗口逐字节相同。A8 读回候选镜像的 23,019,520 字节并全部匹配。A8 与恢复后 A13 的 HLOG 窗口完全相同，有效记录 SHA-256 为 `e1ebd58c73984dc3e0f70cb763700557c6c940e00ce1816f7b2cc467b0debdd8`，正文共 494,117 字节。返回闸门记录 `trial=732 state=2 allowed=1`。[日志校验器](check-hlog.py)

A1 与 A12 的全长 67,108,864 字节均与原厂镜像逐字节相同。恢复后 Android 报告槽位 `_a`、`boot_completed=1`、SELinux Enforcing。[镜像与恢复摘要](package-732.json)

## 复现

从本目录执行。独立参考程序以端点排序检查区间交集，按 GUID 频数计算重复对数。它与实机用户程序在冻结的 Android 参考窗口及 240 组固定种子的合成输入上输出相同的计数。[参考程序](check-entry-oracle.py) · [前一轮归档](../ufs-gpt-array-731/README.md)

```sh
python3 -B check-platform.py --reference-window /path/to/lun2-first64k.bin \
  --libmini /path/to/userland/libmini --loader-base /path/to/loader-project
python3 -B check-entry-oracle.py --reference-window /path/to/lun2-first64k.bin \
  --libmini /path/to/userland/libmini
python3 -B verify-package.py /path/to/boot.img
python3 -B regress-hlog.py /path/to/A2-baseline.bin /path/to/A13-post-restored.bin
```
