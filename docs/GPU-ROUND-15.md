# 2026-11-13 · Round 15：DISABLE 后 MCU_STATUS 语义与完整取证

690 原镜像重跑取得完整证据：AUTO 前 MCU_STATUS=0，请求后为1；失败尾部两次 DISABLE 的 control 均读回0，status 始终为1，额外10000×10µs有界观察也未离开1。它在这条路径中呈锁存样表现，不能凭1判断固件已启动、指令正在执行或不同命令已被处理。此轮不再支持“全程常量”的说法，也没有证明其完整硬件语义或永远无法清除。

witness 三次仍为哨兵，JOB IRQ为0，版本字未发布。690 attempt 2已恢复原厂 Android，boot_completed=1、boot_a MD5独立复核通过。attempt 1缺尾部，只作为不完整取证保存，不作硬件结果。

## 只观察失败尾部

按16:50交接添加默认关闭的 `mali_statussem`，本轮置1，其它新实验开关全0。启动参数沿用681的语义基线，不改变启动序列。探针在 witness 读回及既有失败尾部 DISABLE 写0、delay(2000)、读回之后执行：

1. 顺序读 control、status，barrier后再读status，记录GPU status和debug_top。
2. 再写同一MCU_CONTROL=0，delay(1000)后读回。
3. 最多10000次读status，每次delay(10)，若离开raw1则提前结束。

delay单位为µs；第三步名义延时预算100ms，另有MMIO和日志开销。Panthor的stop也以DISABLED=0为目标，用10µs间隔、100ms超时；kbase的停止等待函数同样等待0。源码片段及指纹在 `round-15-support/disable-status-source.json`。

新增的第二次DISABLE位于启动判据之后，且在write trace冻结之后。**82条是冻结前GPU stores，不含AUTO和失败尾部DISABLE**；不能以82不变说明本轮没有额外尾部写。

## 有效观测：690 attempt 2

全4198400字节窗口解析得到281行Mali日志、唯一trial=690标记。spin 500000、start loop ended、witness及三条status semantics均存在；`log_complete:true`，status探针三个完成标志全通过。

```text
38.627961 mali: MCU status 0 before the start request
38.637643 mali: views before AUTO: gpu status 0/0 irq raw 0/0x600 mcu control 0 status 0
47.848969 mali: MCU status 1 (raw state 1; boot unproven), job irq 0, gpu irq 0
47.865696 mali: witness: read1 0xff5aa5f0 0 read2 0xff5aa5f0 0 read3 0xff5aa5f0 0 trial 690
48.915748 mali: status semantics trial 690: after DISABLE ctl 0 st 1 | after re-read st 1 | gpu_status 0x1 debug_top 0x1b
48.919010 mali: status semantics trial 690: second DISABLE ctl 0 st 1 (after 1000 us)
49.103293 mali: status semantics trial 690: bounded re-read ctl 0 st 1 after 10000 polls (delay 10 us, limit 10000)
```

| 观测点 | control | status |
| --- | --- | --- |
| AUTO前 | 0 | 0 |
| 首次DISABLE之后及再次读 | 0 | 1 / 1 |
| 第二次DISABLE、等待1ms | 0 | 1 |
| 10000次有界观察结束 | 0 | 1 |

首条至末条探针日志相隔187.545ms；第二条至末条相隔184.283ms。这是打印时间间隔，不能把名义100ms延时写成精确的端到端耗时，也不能据有限采样断言两次读之间绝无瞬时变化。

AS0 UPDATE后 raw=0x10000、status/fault/address/GPU fault=0；AUTO后GPU/JOB/MMU IRQ与AS0 status/fault/address均为0。版本字仍为 `0xff5aa5f0`，witness三次均为 `0xff5aa5f0,0`。debug_top在AS0配置前后为4，失败后与DISABLE后为1b；不把这个读数独立解释为取指或核心运行。

trace为82条，与681完整写序仅AS0 TRANSTAB分配PA不同，归一化这两字后零差异。固件启动仍无直接可见证据；后续以witness store、JOB bit31及版本字是否改写核对，MCU_STATUS=1不作成功判据。哨兵不变仅说明未观察到首个store完成，不是零指令执行的证明。

## attempt 1作废与重跑身份

首轮690原始窗口在393216字节处截断，最后Mali行为39.175237s的spin 100000；没有spin 500000、start loop ended、witness read或任何status semantics。恢复原厂后再次读取同一窗口，内容逐字节一致。原始数据、元数据和当时脚本均封存在本地 `round-15-support/trial-690-attempt-1/`；发布其文本和哈希，不拼接两次片段。

首轮raw SHA256为 `3780bbd3a7e93b0f16f88ca9bae8efa2e2fd2d369ada69af5d5e557cf5166fb5`。第二次运行先备份该窗口再清零，备份SHA与首轮相同，零窗口MD5校验为 `34987ee855d45be16fbfd8c3f6f7827f`。两个attempt使用相同五项构建文件哈希、相同参数、逐字节相同镜像；`attempt=2`只是主机元数据。

此前曾打包一个statussem用途的691，但从未刷入；按用户指示原镜像重跑仍用690。`trial-691-not-run*.json`只描述当时那份未刷的status包，不代表691编号永远未运行；后续AS0写序691属于round16。

用户给run-once增加150s完成标志等待；执行时进一步要求statussem轮的三个尾部标志全部出现，才接受完整。精确使用脚本SHA256为 `cbeb4b2076d5aefdc2ce81aef145b013fa8d0b8493c9e6378a9e3dcc31210827`，保存在本轮目录。下轮另要求当前trial的witness读回行，其脚本版本单独保存在round16。

**完成标志修复了验收漏洞，尚未坐实第一次截断的根因。** loader源码在XNU返回后、Android启动前保存logbook，先写header再写body/tail，保存失败返回值未在调用点处理。因此不把“Android adbd出现时XNU仍在轮询”当成已证事实；保留 `logbook-save-source.json` 供后续定位，未改loader。

## 构建、原始证据与恢复

| 项目 | 指纹 |
| --- | --- |
| 两次同镜像MD5 | `49ca1e2e25bce54b7fed488273335dcd` |
| 驱动SHA256 | `0d3c32cea9159e6991796e9586a3518c791b35bc1fc6e15249301d89222b85b6` |
| 内核SHA256 | `04e66412e2cdba1223c709cfec6773c517ae5dd3501d0bbc6fcd9b3b3b3d2852` |
| payload SHA256 | `b29690cc8cbca4737dfb3f34a2c3144155ed2aa9ebf9cdd34614674c4ab4f1f7` |
| 有效raw SHA256 | `3b86e80ba594ca4398e8ae08bf3c26446bd871c02f46259484b71631c367e1a2` |

内核通过既有make-kernel错误分类门，只允许已有kext symbol-set失败；同步源码身份和新日志字符串均已检查。loader完整token各出现3次。精确测试源码 `hakios.mali610.trial-690.c`、独立尾部补丁、构建日志、解析器、两次捕获身份和恢复证明随文本证据发布；完整raw及镜像留本地。

只清理专用logbook窗口：xnusys总长2685370368、offset2676981760、长度4198400；未清理分区其它区域。未开启mali_rpc，未访问三个禁读地址，没有为0x3ffc新增电源写。

attempt 2恢复后独立检查时间为2026-11-13 17:21:25 +08:00：`sys.boot_completed=1`，boot_a MD5=`a63b2e35a7c2522cb24a6a10332b9bc8`。恢复证明见 `round-15-support/stock-final-check.json`。

报告发布为 `docs/GPU-ROUND-15.md`；证据目录 `docs/round-15-support/`。与round16的AS0写序报告一起推送。
