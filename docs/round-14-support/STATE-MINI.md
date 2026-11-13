# STATE-MINI · HakiOS xaga GPU

更新：2026-11-13T16:46:01+08:00。最近完成689；报告out/round-13.md与out/round-14.md。

## 手机与授权

设备GAJZ4HIN99AQ69Y9由本会话独占。685–689均已完成并恢复原厂Android；689最后独立复核16:43:17 +08:00，boot_completed=1。
stock boot_a MD5=a63b2e35a7c2522cb24a6a10332b9bc8。
备份/home/lizi/IOS/hakios-mtk/backup/boot_a_stock_real.img（64MiB）。当前本地镜像是已运行的689，不可当新trial重刷。
用户持续授权刷机、操作、每轮恢复。主问题仍未解决，不直接试mali=6，继续mali=5独立变量。

## 当前优先级

用户明确接续690：按16:50交接做失败尾部DISABLE状态读回；AS0写序尚未实施。
先前statussem原型仅编译、未打包/刷机；689实际是pdcsettle，不能混记。
690将保留既有DISABLE及2ms读回，再重复读、第二次DISABLE+1ms读回，并用有界100ms观察是否离开raw1。
此时690尚未构建/刷入。所有寄存器语义结论以原值和观测窗口为限。

## 最新结果

- 684：四quirk字写前/后均0，86=82+4写，witness仍哨兵；此路径关闭。
- 685：STACK present33、READY0、TRANS前后0，100000×delay(1us)，日志间隔0.569012s。83=82+0xe20=33；witness仍哨兵。不能推真实corestack供电。
- 686：纯读观测完成，AUTO前后各64字。3020=600不变、3034从0到1、3090/3098计数变化；其余60字均0。
  SPM两点F30=1101、F34=de0001fd、F38=de0001fc、F3C=F40=f。SC0..5 raw2000001/en1，SC6..9为0。82写，未新增MMIO写。
- 687：与686同内核只加mcuseq；ENABLE后ctl/status=1/1，AUTO后=2/1，witness仍哨兵。trace82不含ENABLE/AUTO。
- 688：AUTO后一次doorbell，JOB IRQ前后0，MCU raw1，百万次poll未见boot bit31，witness三次仍ff5aa5f0,0。trace82不含AUTO/doorbell。
- 689：只加pdcsettle等待，F3C初/末f，8稳定间隔、0变化，两日志相差9.033ms；AUTO后witness三次仍哨兵。trace82与681归一化无差异。

685–689：AUTO前raw MCU status0，请求后1，JOB/GPU/MMU IRQ=0；AS0 status/fault/address/gpu_fault=0；debug_top配置前后4，请求后与DISABLE后1b。
685–688现有DISABLE写0后约2ms：control读0、status仍1。既有0→1数据不能称整个寄存器恒1，raw1也不证明固件启动。
0xf大于健康设备0x3不能作健康排序；SC6..9为0与六核配置相符。不要为点亮0x3ffc改电源路径。
686的掩码4与原值f两点之间有显式L2_PWRON及L2 ready1；状态字无直接写不能排除命令触发，不能仅据两点断言PDC异步延迟。

## 当前产物

driver=c17402e014cd0371de4dc6125153de5a4456b780dc465e3956b7f69aa50c1e6d
kernel=36c66ca5e8fced9567b8e96ebd82b2bce7c8779b8174284e736345d321489d82
payload=b7616bc4e22822132f2ba6b1d3bf1e6596c0e480ce16537187e917922a3d8fd4
689 image MD5=a1d129a87eb9f2e37d2fa38e3409b475
源码/home/lizi/IOS/hakios-mtk/mtk/mt6895/drivers/gpu/hakios.mali610.c；精确每轮源码快照、镜像、构建日志在out/round-13-support与out/round-14-support。
raw、runner、loader证据在out/round-8-support。发布仓库/home/lizi/IOS/repo/hakios-mt6895。

## 硬规矩与证据边界

- 禁读0x13bfee00、0x13c40000、0x13F91034；禁止启用mali_rpc。手机独占、一次一个变量。
- 每轮恢复stock boot_a并核验MD5和boot_completed；root adbd返回即拉日志，不等boot_completed。
- 仅备份/清理logbook窗口offset2676981760、bytes4198400；先核验xnusys总长2685370368。禁止清整个xnusys。
- 完整解析raw，不按header len截断，检查唯一trial标记。kernel探针内联已到达路径。
- 所有实际boot-arg用loader strings完整token检查各3次；make-kernel错误分类门必须通过，不能只看旧文件。
- 每轮同步发布仓库，export https_proxy=http://127.0.0.1:7897后git push origin main。
- 4K叶修复已验证357叶/148shared叶覆盖；AS UPDATE高位IRQ不等同root fetch bus error。witness未变不是零指令执行证明。
- MCU_STATUS命名不是boot证据；以witness/boot IRQ/有效version分别记录。未将缓存、时钟、内存、电源等整个类别穷尽排除。
