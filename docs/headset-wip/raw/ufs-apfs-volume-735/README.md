# Trial735 从 APFS 容器定位卷超级块

xaga 上的 XNU 用八条 UFS READ(10) 找到了 `xnusys` 中第一个 APFS 卷。备份 GPT 给出分区起点 LBA 30580480，容器检查点给出事务 1。驱动依次读取容器对象映射（相对块 2394）、映射叶节点（2395）和卷超级块（2396）。最后一块的绝对 LBA 为 30582876，CRC32 为 `d85f81f9`。十一块数据的 CRC32 均与原厂 Android 采样一致。[实机日志](device-735-ufs.txt) · [六个 APFS 参考块](reference-735.json)

PID 4 从只读设备分段取得 45,056 字节，解析出卷索引 0、对象编号 1028、事务 1，并验证了 `APSB` 魔数和 Fletcher 校验和。随后读取 EOF 返回 0，门铃计数仍为 8。本轮实机验证到卷超级块为止，挂载和文件内容读取待后续验证。[用户程序](ufs_range_check.c) · [驱动与故障注入测试](734-to-735-platform.patch)

首次预检发现日志区已有一条 632 字节的早期引导记录。操作员在同一次 Android 启动中复读了 8 MiB 日志区和 64 MiB 原厂分区，两次读回各自逐字节相同后才刷入。实验返回后，读回的 23,027,712 字节与候选镜像一致。恢复原厂前后的两个 HLOG 窗口完全相同，有效正文为 503,869 字节。最终 `boot_a` 全长 67,108,864 字节与原厂镜像一致，Android 报告 `_a`、启动完成、SELinux Enforcing。[刷入前的基线](baseline-735.json) · [镜像与恢复记录](package-735.json)

## 复现

在本目录运行。镜像和原始采样使用本机留存件，`chain.bin` 按相对块 0、1、2、2394、2395、2396 排列。

```sh
python3 -B verify-package.py /path/to/trial735/boot.img
python3 -B regress-hlog.py /path/to/A2-baseline.bin /path/to/A8-post.bin
python3 -B regress-hlog.py /path/to/A2-baseline.bin /path/to/A13-post-restored.bin
python3 -B check-platform.py --reference-window /path/to/lun2-first64k.bin \
  --backup-header /path/to/backup-header.bin --backup-array /path/to/backup-array.bin \
  --nxsb /path/to/nxsb.bin --chain /path/to/chain.bin \
  --libmini /path/to/userland/libmini --loader-base /path/to/loader-src
```
