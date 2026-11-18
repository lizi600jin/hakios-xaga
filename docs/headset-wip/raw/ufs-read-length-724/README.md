# Trial724 · 按请求读取一块或两块

Trial723 已在 xaga 上从 LUN 2 的 LBA 2 读回两块。Trial724 在主机控制器模型上验证，同一个引擎能按每次请求选择 4096 或 8192 字节。输入仍限于 LUN 2、4096 字节逻辑块和 LBA 0 至 15。引擎补丁以提交 `60f2a7e` 中的 723 归档为基线。[723 实机交付](../../UFS-VARSTART-DELIVERY-20261118.md) · [724 补丁](723-to-724.patch)

事务开始时固定请求并生成命令，回读的描述符必须与这条命令的全部 80 字节一致。两次快照、预填与核验、采样、恢复与回读都按本次请求词数访问数据缓冲区，整次传输的 CRC32 也只覆盖有效载荷。成功路径只触发一次门铃。

| 主机模型覆盖 | 实测 |
|---|---|
| 全部合法请求 | 单块从 LBA 0 至 15，双块从 LBA 0 至 14，共 31 种。模型从已放置的 CDB 解码起点和块数，逐字节比较返回数据和恢复后的内存。所有单块用例都在数据访问回调处强制 4096 字节上界。 |
| 越界与错误长度 | 0 块、3 块、32 位最大值、跨窗口末端和擅自放宽窗口等输入均在调用硬件模型前被拒绝。 |
| 描述符与恢复 | 分别篡改 CDB 块数、UPIU 字节数和 PRD 字节计数，两种长度均在门铃前拒绝并恢复。末词采样失败时保留精确覆盖数，CRC 判定为未知。末词恢复写失败时共享租约进入 POISONED。 |
| 结果 | Clang 21.1.8 的严格编译与 ASan/UBSan 各通过 748 项检查、27 个场景和 23 次故障注入。另将预填、恢复或 CRC 范围分别改回固定 8192 字节，三种回归都在单块用例触发了预期错误。 |

## 复测

从仓库根目录运行以下命令。需要 Python 3、Git 和带 ASan/UBSan 的 Clang。脚本在临时目录复制 723 源码、应用补丁并构建，用合成数据运行。

```sh
python3 -B docs/headset-wip/raw/ufs-read-length-724/check-host.py
```

## 平台、内核与启动镜像

平台从启动参数 `mtk_ufs_blocks` 选择块数。省略时沿用两块，XNU 返回的参数文本取 `1` 或 `2`。XNU 会把裸参数解释为 `1`。非法长度在映射寄存器之前被拒绝。成功请求需为每个 LBA 找到冻结的参考 CRC，因此平台覆盖的起点如下。[平台补丁](724-platform.patch)

| 长度 | 通过的平台起点 | 实际核对 |
|---|---|---|
| 4096 字节 | 1、2、3、14、15 | 一次门铃，恢复和回读 1024 个数据词，复制区后半段的哨兵字节全部保留。 |
| 8192 字节 | 1、2、14 | 一次门铃，恢复和回读 2048 个数据词，两块按序逐块校验。 |

平台保留原有的 64 KiB 物理映射和所有权检查。每次请求按其块数限制数据区的可访问偏移。主机模型对全部 31 种几何组合逐一运行，其中 23 种因缺少参考而在映射前退出。成功用例逐字节比较描述符、两块数据缓冲区的恢复结果及交付的有效数据。末词读取失败、末词恢复失败、CRC 错误和 STOP 的处理均覆盖两种长度。公开合成夹具与本机冻结的 64 KiB 参考窗口分别通过严格编译和 ASan/UBSan；公开夹具只在临时源码副本中替换五个参考 CRC。[主机验证摘要](host-results.json)

724 内核和 loader 使用同一份租约契约。内核从不含 BUILD 的 723 源码副本执行 `XNU_SRC=<724 内核目录> MACHINE=BCM2837 xnu-build.sh -j4 build`，编译、链接和 strip 完成，退出码为 0。初次尝试默认的 `all` 目标时，额外的 IOKit/Libkern 符号集导出检查失败。正式候选来自重新建目录后执行的内核 `build` 目标。复原补丁后的平台源码、引擎和契约均与该次构建输入逐字节相同。loader 补丁应用于 723 loader 源码根目录，沿用 723 的编译选项，将 `XNU_EXTRA_ARGS` 替换为冻结的 `extra_arguments`。[loader 补丁](724-loader.patch) · [冻结参数](arguments724.json)

离线镜像选择从 LUN 2 的 LBA 3 读一块，预期 CRC32 为 `0x58f10429`。这个起点单块有参考，双块会因 LBA 4 缺参考而被拒绝。最终 boot.img 为 23,011,328 字节。独立拆包比对了带三个回填字段的 loader、原厂 Android 内核、XNUP、构建产出的 XNU 内核、ramdisk 以及包含 NUL 的 356 字节启动参数。镜像保存在本机，实机执行待验证。[打包件大小与 SHA-256](package-724.json) · [拆包核验脚本](verify-package.py)

## 平台与日志复测

在本目录运行以下命令。传给日志测试的输出目录需为尚未创建的目录。

```sh
python3 -B check-platform.py
out=$(mktemp -d)
python3 -B check-parser.py --parser ./parse-hlog-724.py --out-dir "$out/cases"
```

有原始参考窗口时运行 `python3 -B check-platform.py --reference-window /path/to/lun2-first64k.bin`。脚本先比对冻结的 SHA-256，再用生产参考表编译平台测试。两个平台补丁步骤依次为 `723-to-724.patch` 和 `724-platform.patch`，均应用在 723 归档的根目录副本。

HLOG 解析器固定本镜像的参数文件摘要，要求单块 1024/1024 个词、LBA 3 的 CRC、内核终态和 trial 724 的 RESTORED 返回记录。57 个合成用例通过。以平台测试实际输出的六条驱动记录拼接 loader 格式化器实际输出的返回记录，解析结果为 `FORMAT_PASS_ONLY`。上一轮 723 的实机采集被拒绝。[724 解析器](parse-hlog-724.py) · [解析器测试](check-parser.py)

本机可用以下命令从镜像复核打包清单。工作目录应包含 `trial724/`、`build/r2/loader.bin` 和本次 `kernel-src/BUILD/`。

```sh
python3 -B verify-package.py --work-dir "$R24" --stock "$STOCK_BOOT" > "$out/verified.json"
diff package-724.json "$out/verified.json"
```
