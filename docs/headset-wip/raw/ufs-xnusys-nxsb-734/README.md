# Trial734 从 GPT 定位 xnusys 并读取 APFS 容器头

xaga 的 XNU 从 LUN 2 的备份 GPT 中找到唯一的 `xnusys` 条目，起始 LBA 为 30580480，末尾 LBA 为 31236087。前三次 READ(10) 取得并校验备份表头和数组，第四次按解析出的起始地址读取了分区的首个 4 KiB 块。该块的 CRC32 为 `787ea9ea`，字节偏移为 125257646080。[实机日志](device-734-ufs.txt)

用户进程 PID 4 校验了容器超级块的 NXSB 签名、对象类型和 Fletcher 校验和。容器声明 608,448 个 4 KiB 块，位于分区的 655,608 个块以内。原厂 Android 采样与主机 `system.apfs` 的首块逐字节相同。[参考块核验](reference-734.json) · [用户程序](ufs_range_check.c)

恢复前回读的 23,023,616 字节与候选镜像相同。恢复前后两次采集的 HLOG 窗口逐字节相同，有效正文 497,772 字节。回刷后读取的全部 67,108,864 字节与原厂镜像相同。Android 报告启动完成、槽位 `_a` 和 SELinux Enforcing。[镜像与恢复记录](package-734.json)

## 复现

以下命令在本目录运行，原始采样与候选镜像使用本机路径。

```sh
python3 -B verify-package.py /path/to/trial734/boot.img
python3 -B regress-hlog.py /path/to/A2-baseline.bin /path/to/A13-post-restored.bin
python3 -B check-platform.py --reference-window /path/to/lun2-first64k.bin \
  --backup-header /path/to/backup-header.bin --backup-array /path/to/backup-array.bin \
  --nxsb /path/to/nxsb.bin --libmini /path/to/userland/libmini --loader-base /path/to/loader-src
```
