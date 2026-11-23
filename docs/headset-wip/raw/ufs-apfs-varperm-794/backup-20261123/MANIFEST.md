# xaga 分区操作前的完整备份

2026-11-23，64 GiB 方案执行前。xaga，`/dev/block/sdc`，逻辑块 4096 字节，总块 31240192。

## 文件

| 文件 | 大小 | 内容 |
|---|---|---|
| `sdc-head-1MiB.bin` | 1048576 B | 主 GPT（头和 128 项数组，块 0..255） |
| `sdc-tail-1MiB.bin` | 1048576 B | 备 GPT（块 31239936..31240191） |
| `xnusys-full-2.50GiB.bin` | 2685370368 B | **整个 xnusys 分区**，655608 块 |
| `flashinfo-16MiB.bin` | 16777216 B | GPT #87 flashinfo，4096 块 |
| `partitions-sysfs.txt` | 88 行 | 从 sysfs 导出的全部分区 start/size |
| `SHA256SUMS` | | 上面各文件的校验和 |

## 校验

```
11e3e0e75552c1f6714fd29c9a653aaed2ff6cdea33ef6afcc2063b3864f31dc  xnusys-full-2.50GiB.bin
b01ba06ab00b3cbe1be1370138ddbcc25e53d6b10af6095436163a4e73d88758  sdc-head-1MiB.bin
2a43d23613f00f1794d3cf2eaac4ce9f2e59a3a28035c50aac1460adc84759dc  sdc-tail-1MiB.bin
3d064259477f4fa10c7ac4b7223649ce...                                flashinfo-16MiB.bin
```

GPT 数组 CRC：主备都是 `ce39daa8`。

## xnusys 备份的内容确认

```
块 0        NXSB                APFS 容器超级块
            block_size 4096
            block_count 608448  （容器结束块，正好在我们窗口 608512 之前）
块 608512   H+                 我们的 HFS+ 数据卷，签名在偏移 1024
块 653560   "HLOG" 魔数        loader 的启动日志区
```

APFS 容器部分（块 0..608447，2.32 GiB）sha256 前 32 位：`4e3bc3360c6f3e86a9ced9abef1d2794`。
那是 iOS 的只读根（45138 个文件），也是这个分区里唯一没法从别处重建的东西。

注意：块 4 现在全是零。journal 在 Trial794 结束时已 checkpoint（`start == end`），
而分区偏移 0 处的块 4 是 HFS+ 卷内的 journal 头，只有卷被挂载写过之后才有内容。

## 恢复

分区表改坏时，先把这三份写回去：

```bash
# 主 GPT
dd if=sdc-head-1MiB.bin of=/dev/block/sdc bs=4096 count=256 seek=0
# 备 GPT
dd if=sdc-tail-1MiB.bin of=/dev/block/sdc bs=4096 count=256 seek=31239936
# xnusys 整个分区
dd if=xnusys-full-2.50GiB.bin of=/dev/block/by-name/xnusys bs=4096
```

`flashinfo` 不要整段回写——GPT 末尾的 34 块里有 26 块属于它，而改表只动它前面的块。

更彻底的办法是 MTK BROM 深刷（操作者有这个条件）。
