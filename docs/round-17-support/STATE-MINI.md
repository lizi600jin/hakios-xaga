# STATE-MINI · HakiOS xaga GPU

更新：2026-11-13T18:40:55.630834+08:00。最近完成692；本轮报告out/round-17.md。没有运行中的runner，后续停在方案评审，不自动刷下一轮。

## 手机

设备GAJZ4HIN99AQ69Y9；当前原厂Android。独立复核2026-11-13T18:35:56.799523+08:00，sys.boot_completed=1，boot_a MD5=a63b2e35a7c2522cb24a6a10332b9bc8。
原厂备份/home/lizi/IOS/hakios-mtk/backup/boot_a_stock_real.img（64MiB）。当前本地boot镜像是已运行692，MD5=643b2a27e0db0346af6bbccfc1ae4e66，不可当下一轮直接重刷。

## 当前结果

692完整复制691，只改witness=0和trial编号；assorder=1、low=0、ptvendor/scclk/scupd=1等全保留，source/kernel/payload完全相同。正常reset入口0x010018ed，没有witness安装/读回；264行Mali日志，log_complete=true，当前trial版本结果和尾部返回完整。
JOB IRQ0，版本ff5aa5f0，AS0 fault/address0/0、GPU fault0，debug_top失败后与DISABLE后1b，shader ready0；与691在这些采样上无分叉。82条完整GPU写序仅root PA不同，归一化后零差异。
关witness同时取消代码覆盖、向量修改和全段flush；无新fault只表示未观察到新分叉，不证明内部路径相同。MCU_STATUS=1不作为boot证据；debug_top也不证明核心执行。
692 PROT三节仍映射且可写。fault0不能推出未访问，不能据此宣布skipprot已被回答或定性MCU未取第一条指令。三节共64个4K叶，不是三个PTE。

## 当前工作方向与协作要求

先读out/COLLAB-PROTOCOL.md。每个方案必须审假设、证据、反证条件、盲区、替代方案；回复要给同意/保留/反对、理由和优化意见。可以否决不刷，连续失败要复盘。
MFG0 probe已取消；vendor F3C bit1对应MFG0，686前后f已有bit1样本，不能说从未上电，也不能据有限样本说永远常开。601/602是先开MFG1再PDC，raw6与masked4不能混为“全关”。
670/671已跑正常固件入口，672才开witness；670/671时4K步进修复已生效，不能写“从未正常入口/那时4K还没修”。692价值在现行条件匹配对照。
下一步先讨论离线固件兼容性/差异和CSF MCU时钟复位控制链，详细审查见out/trial-692-followup-review.md；不要把MFG供电或GPUEB自身状态直接当CSF MCU状态。三个真机候选的相对排序仍为skipprot、核对后的换固件、段序，但不自动执行。
skipprot尚未实现；若再提，条件guard保留循环尾offset+=esize，禁用旧continue草案。out/p0-patches.md §2.4.2b只是补丁参考，需先审匹配对照和物理布局变化。

## 产物

driver=611adac6a49c0f8cecab09ee5e29352146b291825757f3446fc58e389cc9d53c
kernel=cdc9e38282d2258fb8fd7743a32a9b1c3b13ae6f8c30a9ed55dd9d6c0c5a49a3
payload=20509da6253da0c6361d52983601a197e9be02221f1d5346fe539ad5d58c3cf9
692 raw SHA256=eb778173c3c05864e9de31aaa4f37dea46b7f40705662762836bf5c04e328b6f。
源码/home/lizi/IOS/hakios-mtk/mtk/mt6895/drivers/gpu/hakios.mali610.c。
原始raw、loader及run/build metadata在out/round-8-support；精确源码、脚本及恢复证明在out/round-17-support，报告out/round-17.md。源码/kernel/payload复用691，不需重编；构建门证据沿用round16。
发布仓库/home/lizi/IOS/repo/hakios-mt6895；此前已推8f1b6b7（round15/16），本轮commit见out/round-17-support/publication.json。

## 执行硬规矩

- 一次一个变量，手机独占；每次结束恢复stock并核验boot_completed和boot_a MD5。
- 禁读0x13bfee00、0x13c40000、0x13F91034，禁用mali_rpc。
- 仅备份/清理xnusys专用4198400字节窗口，offset2676981760，总分区2685370368；零MD5=34987ee855d45be16fbfd8c3f6f7827f，不清整个分区。
- root adbd返回后150s等完整证据。witness=0需本轮实际版本行+返回尾部；不能用AUTO前sentinel提示中的version word子串。witness/statussem轮各有专用完成标志；log_complete=false不可当结果。
- 完整解析raw、唯一trial标记，不依header len。690第一次截断原因未证，不把adbd与XNU并行当事实。
- boot参数完整token各3份；新编内核必须过make-kernel错误分类门，复用内核要核对精确哈希。
- trace82冻结在AUTO前，不含AUTO和失败尾部DISABLE；不把计数相同说成全启动过程无写差异。
- 推送export https_proxy=http://127.0.0.1:7897后git push origin main，不重写已发布历史。

## 停手时保留的下轮判据边界

用户要求本轮完成报告推送后停手，未再实施任何实验。下轮skipprot仍须先审条件guard补丁，保留正常入口witness=0。

skipprot不是能保证双向区分“未取指”和“取指后卡住”的唯一实验：若出现归属于本次访问、地址落在PROT范围的fault，会增加访问路径信息；若fault仍为0，两种情形仍可能不可区分。debug_top从4变1b也没有识别请求发起者或证明MCU已经执行指令，不能单独作为“MCU至少执行过”的证据。
