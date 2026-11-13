# 2026-11-13 · Round 16：AS0 内部写序对齐 Android（691）

691 已完整取得 AS0 顺序切换后的数据：`idle → transcfg → transtab → memattr → update`，82条GPU store与基线的差别仅为预期的三组换位及AS0 root分配PA。UPDATE后raw仍为0x10000，witness三次仍为哨兵，JOB IRQ=0，版本字未变。**这次单独改变AS0内部写序没有恢复可见store，按交接判据关闭此候选。** 不由此扩展到所有MMU或内存类问题都已排除。

本轮按17:25交接执行，只做这一条实验；结束后恢复原厂、报告推送即停手，由用户接续调度。

## 补丁与实际参数

新增默认0的 `gMaliAssOrder`，解析 `mali_assorder`。仅在 `as_enable()` 的 `as==0` 且开关为1时，三组 `AS_STEP` 改为TRANSCFG→TRANSTAB→MEMATTR；其它AS及关闭分支沿用原顺序。idle、三组写入值、barrier、每步MMU IRQ clear/read、readback和唯一UPDATE都保留，没有改AS块位置或电源路径。

本轮唯一启用的新变量为 `mali_assorder=1`。681的语义基线仍为mali=5/cold=1/preas0=0/low=0/trace=1/witness=1/ptvendor=1/scclk=1/scupd=1/sc26=0；cacheflush/resetonly/cohnone及后续hwissues/stackpwron/mcuwin/mcuseq/doorbell/pdcsettle/statussem全部为0。具体完整token及三份loader校验见 `trial-691-arg-delivery.json`。

Android依据为 `hist_boot.txt` rec0–7：CFG_LO/HI、TAB_LO/HI、ATTR_LO/HI、读STATUS、UPDATE。本轮对齐的是这三组配置写入的顺序，保留了我们已有的分步诊断clear/read，不声称完整MMIO轨迹已与Android相同。

691此前有一份statussem用途的未刷loader包；本轮重新编译/打包并以新指纹实际运行AS0写序。那份旧包不计硬件实验，690仍为原镜像重跑两次；证据没有混用。

## 生效证据与结果

```text
38.562968 mali: AS0 step idle      mmu irq 0
38.562973 mali: AS0 step transcfg  mmu irq 0
38.562977 mali: AS0 step transtab  mmu irq 0
38.562982 mali: AS0 step memattr   mmu irq 0
38.562984 mali: as_enable trial 691: AS0 applied Android order (CFG->TAB->ATTR)
38.562989 mali: AS0 readback: transtab 0x1a02fc000 memattr 0x84c8d888d8f88 transcfg 0x42000006
38.562994 mali: AS0 step update    mmu irq 0x10000
38.562999 mali: round7 AS0 after UPDATE: raw 0x10000 status 0 fault 0 addr 0 gpu_fault 0 trial 691
47.982803 mali: the start loop ended after 1000000 spins (halt seen 0, fatal seen 0)
48.028377 mali: after the start request: gpu status 0x1, gpu irq status 0, job irq status 0, mmu irq 0, as0 status 0 faultstatus 0 faultaddress 0
48.032810 mali: version word 0xff5aa5f0 (untouched; no firmware version published) trial 691
48.038544 mali: witness: read1 0xff5aa5f0 0 read2 0xff5aa5f0 0 read3 0xff5aa5f0 0 trial 691
```

前三组配置步骤后raw均为0，UPDATE后为0x10000，随后AS0 status/fault/address/GPU fault均0。readback与实际配置值相符。L2 ready=1在38.124276s，先于AS0 UPDATE；固件各节完整映射的coverage检查通过。

AUTO前control/status=0/0，请求后raw status为1；既有失败尾部DISABLE后control=0、status=1。本轮 `statussem=0`，没有再执行690的第二次DISABLE探针。依据690的语义审计，status=1不作为boot成功证据。

debug_top在AS0前后为4，请求失败后及既有DISABLE后为1b。witness三次哨兵仅说明未观察到首个store完成，不能解释为零指令执行；0x10000也不重新解释成root fetch bus error。没有为旧的“want 0x3ffc”诊断字符串改变上电路径。

## 全序列和完整性核验

全4198400字节窗口解析为263行Mali日志，trial标记唯一691。spin 500000、百万次循环结束、write trace、witness read/verdict和失败尾部返回都已取得。`log_complete:true`，实际runner在root adbd返回后等待最多150s，本轮要求当前trial的witness读回行；未只凭adbd出现就接受数据。

trace冻结在AUTO之前，**82条为冻结前GPU stores，不包含AUTO或失败尾部DISABLE**。三组配置各为一次IRQ clear加LO/HI两次store；在681归一化AS0 TRANSTAB LO/HI的PA后，期望顺序为：

```text
baseline[0:70] + baseline[76:79] + baseline[70:76] + baseline[79:82]
```

实际82条全部吻合该排列，`unexpected_differences:[]`。这比只数82条能更直接确认没有其它地址/值/顺序变化。原TSV、原序diff和明确的组换位检查保存在 `write-diff-681-691.json`；解析器同时验证五条step顺序。

## 构建与恢复

| 项目 | 指纹 |
| --- | --- |
| 691镜像MD5 | `effd256c1bf6801a0904edd32c0014c6` |
| 驱动SHA256 | `611adac6a49c0f8cecab09ee5e29352146b291825757f3446fc58e389cc9d53c` |
| 内核SHA256 | `cdc9e38282d2258fb8fd7743a32a9b1c3b13ae6f8c30a9ed55dd9d6c0c5a49a3` |
| payload SHA256 | `20509da6253da0c6361d52983601a197e9be02221f1d5346fe539ad5d58c3cf9` |
| raw SHA256 | `737251df4c6d372b4c83bcf39f89f3ef556f9d9b3e7a252bea62b9d36af98525` |

make-kernel既有错误分类门通过，只出现允许的kext symbol-set失败；同步进XNU的源码身份相同，内核包含新的Android order打印。所有实际参数按完整token核对各3份。每轮源码快照与补丁均保存，镜像及raw留本地，文本/元数据随报告发布。

logbook操作只涉及xnusys专用窗口offset2676981760、bytes4198400（分区总长2685370368），先备份、清零并读回MD5验证；完整解析且无前轮尾部。未开启mali_rpc，未访问三个禁读地址；没有增加新MMIO地址。

恢复后独立检查：2026-11-13T17:36:48.298001+08:00，`sys.boot_completed=1`，boot_a MD5=`a63b2e35a7c2522cb24a6a10332b9bc8`。runner成功退出，`outcome=experiment_captured_and_stock_restored`；证明保存在 `round-16-support/stock-final-check.json`。手机当前为原厂 Android。

报告发布为 `docs/GPU-ROUND-16.md`；证据目录 `docs/round-16-support/`。与round15一起推送，完成后停手，未开启后续实验。
