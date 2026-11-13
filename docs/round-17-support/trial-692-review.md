# 692 正常固件基线方案评审

结论：同意，优先于skipprot。跳过MFG0纯观测；目前只做方案审查，没有修改驱动、打包或刷入692。代码/内核/payload仍为691原物，不需要为关witness重编内核。

## 先纠正历史前提

“670–691全部开witness、从未跑过witness=0的mali=5”不成立。out/round-8.md的对照表明确：670/671是正常固件；672才相对671增加witness=1。两份build JSON没有该参数，日志无bolt_witness输出，reset vector为0x010018ed；两轮正常入口也未发布版本/JOB IRQ。

但670/671用mali_low=1、旧页表属性/时钟配置，与后来的ptvendor/scclk/scupd基线不相同。因此仍值得补“现行条件下、同一内核与payload的正常固件对照”。理由是控制条件缺口，不能宣传成项目从未试过正常固件。

## 建议的严格对照

复制trial-691-build.json的完整verified_args，只改mali_witness=1→0和trial=691→692。保留assorder=1；不同时退回assorder=0。这样692直接对照刚取得的691，启动前82条trace应在AS0 root PA归一化后相同，内核/payload/source哈希也相同。

用户给出的简写不能直接用于loader：现有gMaliLowMem默认1、gMaliScClk/gMaliScUpd/gMaliPtVendor默认0；省略mali_low=0、mali_scclk=1、mali_scupd=1、mali_ptvendor=1会同时改变多项配置，形成错误对照。

完整参数草案保存在out/trial-692-plan.json，字段叫proposed_args，不是已经验证的loader交付。它不放入runner读取的trial-692-build.json，避免误当成可刷包。

## 五项方案元信息

假设：在与691相同的硬件启动和映射策略下，关闭witness替换后，正常reset路径可能产生不同的JOB/版本/故障观测。

证据：驱动:1421-1436把32字节桩覆盖到0x1000000，vec[1]改成0x1000001并flush_all_sections；:4824-4827在gMaliWitness分支执行后才start_mcu。正常入口0x10018ed，桩的三次store仅访问0x4000000/4，不访问0x3000000的PROT区。原版witness阴性不能排除PROT处理影响正常固件路径。

反证条件与解释：若完整日志下两轮JOB/版本/故障状态相同，则本轮没有新增可观察分叉；不能据此推断witness没有掩盖任何内部路径。正常固件出现新的fault及地址，能提供正常路径已触发不同访问的证据，但不是witness机器码有bug或PROT已确定为根因的证明。若JOB bit31/有效版本出现，先核实启动成功，不自动继续skipprot。

盲区：
1. 同一个witness开关同时改变代码、reset向量和全段flush；差异归属于这个处理整体，不能仅归给32字节指令。
2. 两条路径都无fault并不证明执行轨迹相同，更不能证明所有取指/store都正常。保留“未观察到”口径。
3. 关witness后没有read1行；host取证不能继续把witness读回当必需标志，也不能只看到start-loop-ended就声称已经拿到后续版本/fault/尾部。必须等正常固件的本轮完整结果及返回段。

替代方案：若基线仍失败且没有值得追查的新分叉，再评审skipprot+witness=0，先保持其余配置与692相同；若有新fault优先审fault，不机械跑下一个。

## 对三个原候选的技术判断

优先skipprot：它是有参考实现支持的具体差异，而非泛猜顺序。Linux v6.17 Panthor panthor_fw_load_section_entry显式忽略PROT；本地kbase csf/mali_kbase_csf_firmware.c:555-561在没有PMA时也跳过。我们:1062-1066却给普通内存。SEC_PROT是protected标记，不等同普通.bss；数据长度0只能说明零初始化。本地离线复核证实三段PROT共64个4K叶，且不与witness向量、代码、store区重叠。

但skipprot成功先验仍低，尤其不应拿witness去证明正常固件兼容性。用正常入口对照后才有适配判据。条件guard保留循环尾offset推进，不使用continue；还要记录整块跳过会改变后续分配PA和页表数量，成功也不能立刻把原因只归为PROT安全语义。

换固件排在段序之前：正常入口下它能改变真正执行的初始化代码，信息量高于无依据的拷贝顺序；但先验证候选GPU架构、接口/段表与来源，不能凭版本号较新或大小不同就上机。witness仍开着时，大部分正常固件代码不会执行，替换整包的解释也混合了向量/映射/物理布局变化。

段加载顺序暂不刷：已核对当前十段VA互不重叠；数据复制/flush/页表构建都发生在AUTO前。没有证据说明改变顺序会改变同一VA最终内容；若结果变了，首先要考虑分配PA、缓存或加载器实现缺陷，而非宣称硬件要求先shared后code。691测试的是AS寄存器顺序，不能用其失败直接排除内存加载顺序；这里降级依据是当前加载器的数据依赖关系。

Panthor主源：https://github.com/torvalds/linux/blob/v6.17/drivers/gpu/drm/panthor/panthor_fw.c
本地详细数值复核：out/next-step-after-691-offline.json。
