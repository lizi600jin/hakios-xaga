# 2026-11-13 · Round 17：现行条件下的正常固件对照（692）

692已取得完整正常固件对照：JOB IRQ=0、版本仍为哨兵、AS0 fault/address=0，与691在这些判据上没有观察到分叉。没有新fault可供定位；本轮不证明两条路径内部相同，也不排除PROT处理问题。

本轮按trial-692-decision定案，完整复制691参数，只改mali_witness=1→0及trial编号。保留assorder=1、low=0、ptvendor/scclk/scupd=1，复用同一源码、kernel和payload；不实施MFG0 probe或skipprot。

## 对照与判据

692走原厂固件reset入口0x010018ed；691把入口改为0x01000001，运行32字节witness。关闭开关同时取消代码覆盖、向量修改、flush_all_sections三项处理，因此有差异也不能只归因于桩机器码。

观察JOB bit31、版本字是否发布有效值，以及AS_FAULTSTATUS/ADDRESS。新fault可以提供正常路径触发不同访问的证据；相同观测只能说明本轮没有观察到分叉，不证明内部执行路径相同。MCU_STATUS=1、debug_top是否回落均不作为固件成功启动的独立证据。本轮PROT仍按旧方式映射，不能把它描述成“已移除PROT后的访问测试”。

正常入口以前已经跑过：670/671是正常固件、672才加witness（round-8对照表）。670/671当时已有4K步进修复和完整coverage；定案中“4K修复还没到位”这句不准确。本轮补的是现行配置、同一内核的匹配对照，主要条件差别包括low分配、ptvendor页表属性及scclk/scupd，不是项目首次正常入口。

实际参数来自691 JSON，不照抄定案代码块里的mali_cohnune拼写；正确token仍为mali_cohnone=0。完整参数和三份token校验见trial-692-build.json及trial-692-arg-delivery.json。

## 取证完整性

用户给run-once加入version word作为可选完成标志；但AUTO前“sentinel ... written to the version word”也包含这个子串，而start loop ended又早于版本和尾部。因此本轮进一步要求：当前trial的 `mali: version word ... trial 692` 结果行，加失败尾部completed return或mali=5成功return，缺一不可；witness=0不要求read1。

保存用户版本与实际使用脚本，并保留独立补丁和哈希。离线校验六种片段：仅哨兵、仅循环结束、仅版本、错误trial均拒绝；完整失败尾部及成功返回接受。此更改只作用于主机验收，不改变手机执行内容。

## 运行结果

全4198400字节窗口解析为264行Mali日志、唯一trial=692。reset vector保留0x010018ed，日志无witness安装或读回行。完整spin500000、百万次循环结束、版本结果及失败尾部返回均存在，`log_complete:true`，normal_version_complete/normal_tail_complete均为true；witness_required=false。

```text
37.502152 mali: reset vector: sp 0x2040000  pc 0x10018ed
37.502208 mali: as_enable trial 692: AS0 applied Android order (CFG->TAB->ATTR)
37.502218 mali: AS0 step update    mmu irq 0x10000
37.502223 mali: round7 AS0 after UPDATE: raw 0x10000 status 0 fault 0 addr 0 gpu_fault 0 trial 692
37.502332 mali: views before AUTO: gpu status 0/0 irq raw 0/0x600 mcu control 0 status 0
45.703560 mali: the start loop ended after 1000000 spins (halt seen 0, fatal seen 0)
45.825888 mali: after the start request: gpu status 0x1, gpu irq status 0, job irq status 0, mmu irq 0, as0 status 0 faultstatus 0 faultaddress 0
45.833494 mali: version word 0xff5aa5f0 (untouched; no firmware version published) trial 692
47.990434 mali: shared probe trial 692: completed; returning before global_interface
```

| 观测 | 691：witness=1 | 692：witness=0 |
| --- | --- | --- |
| 实际reset入口 | 0x01000001 | 0x010018ed |
| JOB IRQ（循环采样及结束） | 0 | 0 |
| 版本字 | 0xff5aa5f0 | 0xff5aa5f0 |
| AS0 fault/address（采样及结束） | 0 / 0 | 0 / 0 |
| AS0 UPDATE后raw | 0x10000 | 0x10000 |
| 冻结前GPU stores | 82 | 82 |

只归一化AS0 TRANSTAB LO/HI分配PA后，691与692全部82条GPU写序完全相同，diff为空；原始差别只在record74的root LO。691 root=0x1a02fc000，692 root=0x1a2214000。相同程序与参数控制不等于每轮硬件瞬态和物理分配完全一致。

692的coverage完整；L2 ready=1在37.323058s，早于AS0。MCU raw status从0到1，既有DISABLE后ctl=0、status=1；debug_top配置前后为4、失败后及DISABLE后为1b，均只留原值。旧“want0x3ffc”诊断字符串不是失败判据。

raw SHA256=`eb778173c3c05864e9de31aaa4f37dea46b7f40705662762836bf5c04e328b6f`。


## 产物身份

| 项目 | 指纹 |
| --- | --- |
| 692镜像MD5 | `643b2a27e0db0346af6bbccfc1ae4e66` |
| 驱动SHA256 | `611adac6a49c0f8cecab09ee5e29352146b291825757f3446fc58e389cc9d53c` |
| 内核SHA256 | `cdc9e38282d2258fb8fd7743a32a9b1c3b13ae6f8c30a9ed55dd9d6c0c5a49a3` |
| payload SHA256 | `20509da6253da0c6361d52983601a197e9be02221f1d5346fe539ad5d58c3cf9` |

没有重编内核；直接验证上述前三个文件与691逐字节哈希相同。既有构建门和源码同步证据沿用round-16-support。重新打包loader和boot镜像，全部实际参数按完整token校验各3份，刷前再次核对产物哈希。

trace冻结在AUTO之前，82条预期仅指冻结前GPU stores；不能据此断言AUTO后无额外写。对照将以691完整82条序列核对，只归一化AS0 TRANSTAB LO/HI分配PA，不只比较计数。

仅备份、清零并校验xnusys专用4198400字节窗口，offset2676981760、分区总长2685370368；不清分区其它部分。完整解析raw，不按header len截断。禁读三地址与mali_rpc禁用规则保持。

恢复后独立复核：2026-11-13T18:35:56.799523+08:00，sys.boot_completed=1，boot_a MD5=`a63b2e35a7c2522cb24a6a10332b9bc8`。runner成功退出，手机现为stock Android。见round-17-support/stock-final-check.json。

报告发布为docs/GPU-ROUND-17.md，文本证据在docs/round-17-support；完整raw及boot镜像留本地。

## 下一步评审（未执行）

692补齐了现行正常固件条件的对照。若继续三候选，仍倾向先评审skipprot，而非无依据地重排加载顺序。理由是Panthor显式忽略PROT、kbase无PMA也跳过；当前驱动却用普通内存映射。它是具体实现差异，不代表已经找到因果。正常入口下阴性结果仍可能受共同的启动阻塞限制，不能把候选整体宣判排除。

建议下一份方案的假设缩为“没有PMA时，跳过PROT映射是否改变正常入口的JOB/版本/fault观测”。支持依据为本地`k-xaga-16.2/drivers/gpu/mediatek/mali-valhall/gpu/arm/midgard/csf/mali_kbase_csf_firmware.c:555-561`和[Linux v6.17 Panthor加载分支](https://github.com/torvalds/linux/blob/v6.17/drivers/gpu/drm/panthor/panthor_fw.c)；反证条件为完成映射变更且完整日志下目标判据仍无改善；盲区包括共同阻塞点、跳过分配引起的PA/页表布局改变、fault采样不可见的访问。替代方案是先核对新固件的GPU架构、入口、段表和接口，再决定是否换包；段序只有出现加载内容/映射相关证据才提升优先级。

若做skipprot，只改分配块条件，不在循环中continue，确保offset+=esize照常推进。日志要明确三个PROT范围被跳过；离线预期去掉64个4K叶、保留vector/code/shared映射与权限，并据实际策略区分“省去分配”和“仅不映射”。实际patch及判据先审，再打包；本轮结束不自动运行下一条。此前连续失败已在692前做过方案复盘，不以“还有未试开关”作为自动刷机理由。

## 692之后新增推理的边界

主控提出“692 fault=0证明没有写PROT，故skipprot已被部分回答”。本报告不采纳这个推论：692的sec6/7/8仍是有效、可写映射，合法写入也可以fault=0。它没有执行取消映射的干预。未出现版本/JOB/store也不能把故障确定在第一条指令之前。

详细讨论见round-17-support/trial-692-followup-review.md。建议下一阶段先做离线固件兼容性比较和CSF MCU实际clock/reset控制链审计，再决定真机实验；暂不增加未知GPUEB读或自动接续skipprot。MFG域供电、GPUEB自身状态与CSF MCU复位/执行状态不可未经证据互相替代。

## 停手时保留的下轮判据边界

用户要求本轮完成报告推送后停手，未再实施任何实验。下轮skipprot仍须先审条件guard补丁，保留正常入口witness=0。

skipprot不是能保证双向区分“未取指”和“取指后卡住”的唯一实验：若出现归属于本次访问、地址落在PROT范围的fault，会增加访问路径信息；若fault仍为0，两种情形仍可能不可区分。debug_top从4变1b也没有识别请求发起者或证明MCU已经执行指令，不能单独作为“MCU至少执行过”的证据。
