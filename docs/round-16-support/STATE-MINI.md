# STATE-MINI · HakiOS xaga GPU

更新：2026-11-13T17:38:43.422294+08:00。最近完成691（mali_assorder=1）；690重跑与691均有完整日志，均已恢复原厂。本会话结束本轮后停手，交用户调度，不自动刷下一条。

## 手机

唯一设备GAJZ4HIN99AQ69Y9，当前原厂Android；没有正在运行的runner。
独立复核：2026-11-13T17:36:48.298001+08:00，sys.boot_completed=1，boot_a MD5=a63b2e35a7c2522cb24a6a10332b9bc8。
原厂备份/home/lizi/IOS/hakios-mtk/backup/boot_a_stock_real.img（64MiB）。
源码/home/lizi/IOS/hakios-mtk/mtk/mt6895/drivers/gpu/hakios.mali610.c；当前本地镜像为已运行的691，不可当新trial直接重刷。

## 结果与证据

- 690 attempt 1：原始窗口只到spin100000，缺trace/witness/statussem，不作硬件结果。不可拼接重跑尾部。原始封存在out/round-15-support/trial-690-attempt-1。
- 690 attempt 2：完全相同镜像与参数重跑，281行，log_complete=true，三个statussem尾部标志完整。AUTO前status0，请求后1，两次DISABLE control读0、status仍1，10000×10us有界观察仍1。支持本路径中锁存样表现，不证明永久锁存或寄存器未实现。MCU_STATUS=1不能当固件启动证据。
- 691：只开assorder，263行，log_complete=true。五步idle,transcfg,transtab,memattr,update；前三组IRQ0，UPDATE0x10000，AS0 status/fault/address/GPU fault0。82条trace在归一化AS0 root PA后精确吻合CFG/TAB/ATTR三组的预期换位，无其它变化。
- 691 witness三次ff5aa5f0,0，JOB IRQ0，版本ff5aa5f0。此写序调整无效。debug_top配置前后4、失败后1b，不作为取指证明。
- 689 pdcsettle：F3C初/末f，八个1ms稳定间隔、零变化；等待未恢复store。686的4→f两点之间有显式L2_PWRON，不据此独断PDC异步未稳定。
- 685 STACK控制请求无效，不推真实corestack供电；686全窗口读与687 ENABLE→AUTO、688单次doorbell均已归档。旧“want0x3ffc”不是有效失败判据，禁止为点亮该值改变电源路径。

681仍为语义基线：mali=5,cold=1,preas0=0,low=0,trace=1,witness=1,ptvendor=1,scclk=1,scupd=1,sc26=0。各新增实验开关默认0，一轮只开一个。691中statussem/pdcsettle/doorbell/mcuseq/mcuwin/hwissues/stackpwron全0。
trace冻结在AUTO/doorbell/失败尾部DISABLE之前，82不包含那些请求。witness未变仅说明未观察到首个store完成；AS UPDATE高位IRQ不等同root fetch bus error；不把某一类所有可能性宣告穷尽。

## 下一份交接仅登记，尚未执行

用户2026-11-13 17:45更正：以out/HANDOFF-TO-CODEX-2026-11-13-1740.md与p0-patches.md §2.4.2b的修正版为准。
后续若执行mali_skipprot，仅把分配块判断改成：

```c
if (va_end > va_start && !(gMaliSkipProt && (flags & SEC_PROT))) {
    /* 原有分配、复制、映射 */
}
```

不要在中间continue；load_firmware的offset += esize在循环体末尾，中间continue会跳过推进而死循环。当前源码没有gMaliSkipProt/mali_skipprot，未编译、未打包、未刷该实验。p0 §2.4.2b仍以BssDelay旧名展示同一条件形态，实际后续遵从用户的skipprot命名。
新1740交接仍列round-16报告路径；out/round-16.md现在已是691正式报告，后续另起报告，避免覆盖。不得把PROT标志直接视为普通.bss语义，也不要仅凭fault为0推出从未访问；先记录实际结果。

## 当前产物与报告

driver=611adac6a49c0f8cecab09ee5e29352146b291825757f3446fc58e389cc9d53c
kernel=cdc9e38282d2258fb8fd7743a32a9b1c3b13ae6f8c30a9ed55dd9d6c0c5a49a3
payload=20509da6253da0c6361d52983601a197e9be02221f1d5346fe539ad5d58c3cf9
691 image MD5=effd256c1bf6801a0904edd32c0014c6
报告out/round-15.md（690）与out/round-16.md（691）；精确测试源码、独立补丁、构建和恢复证明在同号support目录。
实际raw、loader/build/run JSON、trace TSV在out/round-8-support。原始raw与镜像不推送；文本证据发布到/home/lizi/IOS/repo/hakios-mt6895的docs/round-15-support、docs/round-16-support。
此前round13/14已推9d98c10，round12已推f01f1f6。最新15/16发布commit以support/publication.json为准。

## 执行规则

- 手机独占、一次一个变量；每次实际运行结束恢复原厂boot_a并核验boot_completed和MD5。
- 禁读0x13bfee00、0x13c40000、0x13F91034；禁止启用mali_rpc。
- 只备份/清理xnusys专用窗口offset2676981760、bytes4198400，先核验分区总长2685370368；零窗口MD5=34987ee855d45be16fbfd8c3f6f7827f。禁止清整个分区。
- root adbd返回后最多150s等完成标志；witness轮还需当前trial的read1；statussem轮还需三个尾部标志。log_complete=false不可当结果。
- 完整解析raw，不按header len截断，核对唯一trial。第一次690截断原因未证实：loader在Android启动前保存，不能把adbd与XNU并行轮询当已证因果。
- 所有boot-arg用loader strings完整token核验各3次；内核必须通过make-kernel既有错误分类门并校验同步源码与新日志。
- publication repo推送：export https_proxy=http://127.0.0.1:7897后git push origin main；不重写已发布历史。
