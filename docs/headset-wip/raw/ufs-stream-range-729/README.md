# Trial729 用户态分段读取与文件偏移

xaga 上的 PID 4 在同一个只读文件描述符上交错使用 `read(2)` 和 `pread(2)`。该设备导出 LUN 2 字节偏移 12031 起的 770 字节，设备文件内从偏移 0 开始计数。[用户程序](ufs_range_check.c) · [冻结参数](arguments729.json)

| 调用 | 文件内偏移 | 请求字节 | 返回字节 | 用户 CRC32 | 累计 UFS 门铃 |
|---|---:|---:|---:|---|---:|
| 首次 pread 到末尾 | 770 | 1 | 0 | — | 0 |
| read | 0 | 256 | 256 | `9de8fce7` | 1 |
| pread，覆盖块边界 | 250 | 19 | 19 | `678d8cf7` | 1 |
| read | 256 | 259 | 259 | `a6a8c96b` | 1 |
| read，末尾短读 | 515 | 512 | 255 | `c275a37c` | 1 |
| read，文件末尾 | 770 | 1 | 0 | — | 1 |

定位读取保留了文件的顺序读偏移。用户程序逐次检查缓冲区哨兵，六次均通过，写打开返回 EROFS。底层仅提交一次 READ(10)，取回 LBA 2、3，完整切片 CRC32 为 `8f350378`。loader 返回闸门记录 `trial=729 state=2 allowed=1`。[23 条实机日志](device-729-ufs.txt)

## 采集与恢复

A2 基线与 728 的 A13 窗口相同。A8 实验镜像前缀的 23,019,520 字节全部匹配。A8 与恢复后 A13 的 4,198,400 字节 HLOG 窗口逐字节一致，有效记录 SHA-256 为 `1bba34d6e79ae16bed65c9205bd56b85e28e02036bc8b2ed6dd15eec2e93b3f5`。校验器按 XNU 与 loader 的写入顺序分别比对控制台、内核消息和末尾闸门。[日志校验](check-hlog.py)

A1 与恢复后 A12 的 67,108,864 字节均与原厂镜像逐字节相同。恢复后的 Android 报告槽位 `_a`、`boot_completed=1`、SELinux Enforcing。[镜像与实机核验摘要](package-729.json)

## 复现

从本目录执行。平台与用户程序测试需要冻结的 Android 参考窗口，loader 重建使用对应 HakiOS 项目的头文件。主机测试包含缓存填充期间的并发请求、损坏的数据、停止请求、短读和 EOF。[前一轮基线](../ufs-userspace-range-728/README.md) · [源码摘要](source-sha256.json)

```sh
python3 -B check-platform.py --reference-window /path/to/lun2-first64k.bin \
  --libmini /path/to/userland/libmini --loader-base /path/to/loader-project
python3 -B verify-package.py /path/to/boot.img
python3 -B regress-hlog.py /path/to/A2-baseline.bin /path/to/A13-post-restored.bin
```
