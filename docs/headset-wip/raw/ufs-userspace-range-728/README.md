# Trial728 用户进程首次通过 pread 读取 UFS

xaga 上的 `/redwood/bin/ufs_range_check` 以 PID 4 打开 `/dev/mt6895-ufs-range`。它在 LUN 2 的字节偏移 12031 处读取 770 字节，前 257 字节来自 LBA 2，后 513 字节来自 LBA 3。用户程序计算的 CRC32 为 `8f350378`，与内核及 Android 参考窗口一致，两侧哨兵保持完整。[实机日志摘录](device-728-ufs.txt) · [用户程序源码](ufs_range_check.c)

| 调用 | 返回值 | UFS 门铃累计次数 |
|---|---|---|
| 写打开 | EROFS，errno 30 | 首次读之前 |
| pread，偏移 12031，长度 770 | 770 字节 | 1 |
| pread，偏移 65536，长度 1 | EOF，缓冲区保持原值 | 1 |
| 再次请求原范围 | EBUSY，errno 16，缓冲区保持原值 | 1 |

`mtk_ufs_vfs=2` 注册只读字符设备，回调要求用户地址空间的 UIO。此轮覆盖上述固定范围及一次性请求预算。记录末尾的 loader 返回闸门为 `trial=728 state=2 allowed=1`。[冻结参数](arguments728.json)

## 日志取证与原厂恢复

A2 的首个窗口与 Trial727 的 A13 相同。恢复前后取得的两个 Trial728 窗口也逐字节相同。有效记录 SHA-256 为 `7a9c569b915744c96066cdc2478997f9e228ef7dfd702368277e298c74e138e2`，正文共 494,232 字节。[实机核验与镜像摘要](package-728.json)

最初的验收脚本要求用户日志和内核日志交错出现。XNU 的 `loader_return()` 先收集控制台，再由 `loader_log_append_msgbuf()` 追加内核消息缓冲区。loader 最后写入返回闸门。修正后的脚本对两个来源分别校验完整顺序、关联 PID，并检查记录末尾的闸门。用同一组原始采集复验通过，28 个反例均被拒绝。[日志校验器](check-hlog.py) · [回归测试](regress-hlog.py)

A8 读回的 23,019,520 字节与候选镜像相同。恢复后 Android 报告槽位 `_a`、`boot_completed=1` 和 SELinux Enforcing。A1 与 A12 的 67,108,864 字节分别与原厂镜像逐字节核对，SHA-256 为 `c5f8e7bde8e9f1d824d0c7915f7d701adce8142656a82c20e10ccb75e15e01fc`。[打包和恢复记录](package-728.json)

## 复现

从本目录执行。平台和用户程序测试使用同一份已知摘要的 64 KiB Android 参考窗口。loader 从仓库内的 Trial721 基线和历次补丁重建，编译使用匹配版本的项目头文件。程序测试用到该版本的 libmini 头文件。[源码摘要](source-sha256.json) · [前一轮基线](../ufs-vfs-range-727/README.md)

```sh
python3 -B check-platform.py --reference-window /path/to/lun2-first64k.bin \
  --libmini /path/to/userland/libmini --loader-base /path/to/loader-project
python3 -B verify-package.py /path/to/boot.img
python3 -B regress-hlog.py /path/to/A2-baseline.bin /path/to/A13-post-restored.bin
```
