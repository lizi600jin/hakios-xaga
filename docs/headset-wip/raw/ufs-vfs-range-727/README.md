# Trial727 · XNU 通过字符设备读取跨块窗口

xaga 实机上的内核调用者通过 `vn_rdwr` 读取 `/dev/mt6895-ufs-range`。偏移 12031、长度 770 的请求返回 `error=0`、`residual=0`，调用者缓冲区两侧的哨兵保持原值。该轮字符设备只接受这一个固定的数据请求。[14 条实机驱动记录](device-727-ufs.txt) · [Trial726 的字节范围基线](../ufs-byte-range-726/README.md)

| 操作 | 实机返回 | 累计门铃 |
|---|---|---|
| 写打开 | `EROFS`（30） | 0 → 0 |
| 读 770 字节 | 调用者 CRC32 为 `0x8f350378`，与 `slice` 和独立参考一致 | 0 → 1 |
| 在窗口末尾请求 4 字节 | `error=0 residual=4 unchanged=1` | 1 → 1 |

底层只发出一条双块 READ(10)。LBA 2 和 LBA 3 的 CRC32 分别为 `0x0d024e0c` 和 `0x58f10429`。驱动取前者末 257 字节、后者前 513 字节，经 `uiomove` 交付。VFS 自测最终记录 `passed=1`，loader 记录 `trial=727 state=2 allowed=1`。[源码与主机用例](726-to-727-platform.patch) · [冻结参数](arguments727.json)

## 启动与恢复

候选镜像为 23,015,424 字节，SHA-256 为 `dedab994d13c76a981852b6bfb1370d3ccaf28637aa8812227f1bcddcf7437f6`。实验启动回到 Android 后，从 `boot_a` 读回的镜像前缀与候选逐字节相同。刷前的 HLOG 与 726 的末次记录一致。727 的 HLOG 正文为 494,544 字节，原因和返回码均为零。恢复前后采集的两个 4,198,400 字节窗口逐字节相同。[镜像与实机核验摘要](package-727.json)

原厂件写回 `boot_a` 后再次启动 Android。设备报告 `_a`、`sys.boot_completed=1` 和 SELinux `Enforcing`。全分区读回的 67,108,864 字节与刷前备份、原厂件三者逐字节相同。[恢复记录](package-727.json)

## 复测

平台测试从 Trial723 的冻结源码起步，依次应用 724 至 727 的增量。loader 从 Trial721 的源码和 721→723 集成补丁起步，重建 727 的两个变更文件并核对 SHA-256。提供匹配的工程头文件后可重编 loader 并比对整份二进制。[源码摘要](source-sha256.json) · [loader 构建参数](loader-build.json)

```sh
python3 -B check-platform.py
python3 -B check-platform.py --reference-window /path/to/lun2-first64k.bin --loader-base /path/to/loader-source-tree
python3 -B verify-package.py /path/to/boot.img
python3 -B check-hlog.py /path/to/726-baseline.bin /path/to/727-post.bin
```

基础套件使用合成数据。提供冻结参考窗口后，脚本还会在优化构建与 ASan/UBSan 下执行字符设备回调，逐字节检查目标、两侧哨兵和一次读取预算。`--loader-base` 指向含 `mtk/loader/` 和 `xnuloader/identity.h` 的工程根目录。原始采集、参考块、镜像及设备标识留在本机。[复测脚本](check-platform.py) · [原始 HLOG 校验器](check-hlog.py)
