# Trial721 实机验收结果

一次启动、四次变量 LBA 的 READ(10)、干净归还、手机恢复原厂。本文记实际发生的事与能证明的边界。
设备序列号一律写 `<SERIAL>`；原始 HLOG、原始镜像与私有参考仅留本地，不入库。

## 一、结论

- **四次 READ(10) 全部完成**：LUN 2，LBA 顺序 1 → 2 → 15 → 1，每次 4096 字节，总预算 4，无重试
- **四次 CRC32 均与冻结参考一致**，`failed=0`，返回闸门记录 lease 已干净归还
- **两次独立刷写**：实验镜像与 original 恢复各一次，均 `rc=0`
- **原厂已恢复并验证**：新 `boot_id`、`_a` 槽、`boot_completed=1`、`boot_a` MD5 与恢复件一致

## 二、动作序列（A1–A13）

各动作分别下达与执行；**A1 缺逐项释放的书面记录**（见第七节偏差），其余各项由 root 逐条授权、
操作者逐项释放。

| 步 | 动作 | 时刻 | 结果 |
|---|---|---|---|
| A1 | 记录身份（只读） | 12:10:01 | 与冻结 stock 一致 |
| A2 | 只读预存 8 MiB `xnusys` 尾部 | 12:15:53 | 8388608 B，无 trial721 迹象 |
| A3 | **主机侧**核对三个镜像的 hash（不碰设备） | A6 前 | 全部匹配冻结值 |
| A4 | `adb reboot bootloader` | 12:17:56 | `rc=0`，随后 fastboot 在线 |
| A5 | `getvar current-slot`（只读） | 12:19:31 | `rc=0`，`a` |
| **A6** | **实验镜像写入 `boot_a`** | 12:23:51 | **`rc=0`，Sending/Writing 均 OKAY** |
| A7 | `fastboot reboot` | 12:25:16 | `rc=0` |
| A8 | 观察传输 ≤300 s | — | adb 在 **220 s** 出现 |
| A9 | 回 bootloader | 12:28:56 | `rc=0` |
| **A10** | **原厂恢复写入 `boot_a`** | 12:29:18 | **`rc=0`，Sending/Writing 均 OKAY** |
| A11 | `fastboot reboot` | 12:29:26 | `rc=0`，adb 40 s 后回来 |
| A12 | 恢复验证（只读四项） | 12:30:38 | 全部 PASS |
| A13 | 只读取 HLOG | 12:31:34 | `rc=0`，4198400 B |

**A10 是独立的第二次写入，不与 A6 合并。** loader 的返回闸门不会恢复 `boot_a`；
它只写日志并把控制交还 Android，所以恢复必须在每次运行后单独做，成功与否都一样。

## 三、两次刷写

| | 镜像 | 字节 | SHA-256 | rc |
|---|---|---:|---|---:|
| A6 实验 | `trial721/boot.img` | 23003136 | `498a99a40094c1f941ce598e061b1eb599690acf531eca25e402b270acf6472e` | **0** |
| A10 恢复 | `backup/boot_a_stock_real.img` | 67108864 | `c5f8e7bde8e9f1d824d0c7915f7d701adce8142656a82c20e10ccb75e15e01fc` | **0** |

A10 的 SHA-256 在刷写前复核过，与 A1 记录的分区当前 MD5 属同一原厂基线。

## 四、设备报告（四次读取）

以下为九行驱动报告的**节选**（去掉 XNU 时间戳后的原文形态；非逐字节全文）：

```
bounded variable READ10 lun2 lba0..15 bytes4096 budget4; guarded handoff required
guard magic=0x554c5345 version=1 trial=721 state=0
opened control=0x2 shared=1
request=1 lba=1  ... data_words=1024/1024 restored=1 copied=1 crc=0x28a2836c match=1
request=2 lba=2  ... data_words=1024/1024 restored=1 copied=1 crc=0x0d024e0c match=1
request=3 lba=15 ... data_words=1024/1024 restored=1 copied=1 crc=0xc71c0011 match=1
request=4 lba=1  ... data_words=1024/1024 restored=1 copied=1 crc=0x28a2836c match=1
final control=0x2 shared=1 failed=0
return-gate magic=0x554c5345 version=1 trial=721 state=2 allowed=1
```

每次请求记录里的 `control=0x4` 是 `copy_done` 之前的 DRAINED 采样，不是终态；
**终态看 `final` 行的 `control=0x2 shared=1`**。`failed=0` 单独不足以证明四次成功，
四次有序完整记录才是。

## 五、HLOG 身份与哈希

| 项 | 值 |
|---|---|
| 读取命令 | `dd if=/dev/block/by-name/xnusys bs=4096 skip=653560 count=1025`（只读，一次） |
| dd rc | **0** |
| 读长 | **4198400 B** |
| 实验前（A2，同长度窗口） | `b68599bcff24d894144c5fed6191b83d97bf5d04e6dc34d4fa50969982c4d117` |
| 实验后（A13） | `200493054b8250bc625125f522fad47b0eba0c33e6c9012d0ebf3194e103849e` |
| header | `magic 0x474f4c48`、`version 1`、`len 498972`、`reason 0`、`code 0`、`sum 0x2717432` |
| body | 498972 B，`93d91a00594577a7403017ac5f96fc1930d9145b9057f13c9beceb4326418dc0` |
| 整数和校验 | 通过 |

实验前窗口与实验后窗口**不同**，只说明这段窗口被改写过，**单凭哈希差异不足以断定属于本次运行**。
归属由以下几点共同约束：单次启动（loader 每次运行只写一个 header）、`boot-args` 精确匹配本次冻结参数
（含 `mali_trial=721` 与 `mtk_ufs_varread=1`）、header 的 `reason`/`code` 为正常值且整数和与 body 一致、
九行驱动前缀全为 `mt6895-ufs-varread:`、以及末尾返回闸门记录 `trial=721 state=2 allowed=1`。
**这仍不是 wire trace。**

## 六、解析：原 parser rc=1，独立 v2 工具 rc=0

**两份 rc 分开记，不合并。**

- **原 parser**（`root-review-721-v1/review-device-721.py`，未修改）：`native rc=1`，
  `status=INCOMPLETE_OR_REJECTED`，原因 `unexpected/missing driver report span`。
  根因是 XNU 给八条驱动行加了 `[   18.837715]: ` 这类时间戳前缀，parser 用
  `startswith` 匹配，只认出末尾无前缀的返回闸门那一行。**拒绝报告保留。**
- **独立 v2 工具**（`root-review-721-v1/review-device-721-timestamped-span-v2.py`）：
  先调用未改的原 `review()`，只有其结论恰为 span 拒绝、且长度/体/整数和/`reason`/`code`
  全部通过时才进入适配；仅在内存中对锚定正则移除**八处**前缀，再调用原 `inspect()`
  验收全部字段。`native rc=0`，结论 `FORMAT_VALIDATED_REAL_LOG`。
- root 独立复现：未改的 `review()`/`inspect()` 在剥离后四请求、`final`、闸门全部通过。

**适配器不是绕过：** 它在原 parser 拒绝之后才工作，且复用原 parser 的字段检查；
原始 raw、body 与 rc=1 报告全部留档。

## 七、过程偏差（如实记录）

1. **A4 的一次终端回报泄漏了真实序列号。** 收到指出后用只读方式全盘清查 R21 工作区，
   **落盘文件 0 处命中**（只进了屏幕流）。
   **阶段范围要说清**：A5 当时曾先把 `getvar` 输出写临时 `.raw`、再脱敏、再删掉临时文件；
   **从 A6 起才改为 subprocess 内存捕获 + 逐字节替换、不再产生任何 `.raw`**。
   后续各项证据均复检无残留。
2. **A6 之后额外做过一次只读 `getvar partition-size:boot_a`**（返回 `0x4000000`）。
   属额外探测，单列记账，未并入任何动作。
3. **A11 之后提前读取了恢复状态**（与 A12 内容重叠），同样单列，不重复计为 A12。
4. **A1 缺少逐项释放的书面记录**，由 root 指出。
5. 首个合成夹具没有带 parser 的精确 marker，导致返回硬件形状字段；已作废留档，
   重做的夹具带 marker 并真实返回 `FORMAT_PASS_ONLY`。

## 八、能证明与不能证明

**能证明：**

- 内核能在真机上按变量 LBA 完成四次单块 READ(10)，并按预算与失败策略运行
- 四次 CRC32 与冻结参考一致；lease 在四次请求期间被持有，最后干净归还
- 实验镜像与原厂镜像可各自独立刷写，手机能恢复到原厂并正常启动

**不能证明：**

- **CRC 相等只是校验和一致，不是字节相等。** 私有参考里 16 块有 8 块共享同一 CRC，
  校验和无法唯一标识 LBA
- **没有 wire trace。** LBA 绑定依据是冻结源码与本次请求回读，不是独立线路观测
- 未排除缓存影响；未证明物理 DMA 的新鲜度
- 一次运行不给吞吐、gear/lane、队列或块层结论
- 没有多块读、没有块层注册、没有文件系统或用户态消费方

## 九、证据位置（本地，不入库）

```
R21 = logs/ufs-variable-read-20261117
revision2/logs/device/A1-identity.txt
revision2/logs/device/A2-pre-logbook.txt
revision2/logs/device/A2-pre-logbook-8MiB.bin        原始，仅本地
revision2/logs/device/A6-flash-boot_a.txt
revision2/logs/device/A10-restore-stock.txt
revision2/logs/device/A12-verdict.txt
revision2/logs/device/A13-record.txt
revision2/logs/device/A13-post-logbook.bin           原始，仅本地
revision2/logs/device/A13-parse-report.json          原 parser rc=1
revision2/logs/device/A13-parse-report-timestamped-v2.json   v2 rc=0
```

原始二进制与私有参考不入库；本文只摘录脱敏摘要。
