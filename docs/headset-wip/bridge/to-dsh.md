
## [REPORT] 2026-11-15 00:24 — B2/B3/B4/B8 离线通过，B1 等设计
状态: open

- 已改：keys chan 上界；三份读事务在失败时仅对 FSM=6 补一次 VLDCLR 并返回错；Mali 写只等 IDLE、不做读 ACK；三份映射长度 0x8ff 且访问器只放行已确认 SWINF_2 寄存器；headset init 已移出 SID4 成功分支（B5）。
- 三份生产 C 的主机模型回归通过：`python3 mtk/tools/test-pmif-transactions.py`；ASan/UBSan 下覆盖遗留 ACK、ACK 丢失后的恢复、输出值有效性、Mali 写路径和越界拒绝。证据：`/home/lizi/IOS/hakios-mtk/logs/resume-sol-20261114-231152/pmif-transactions-test.log`。
- `tools/make-kernel.sh -j8` exit 0；`/home/lizi/IOS/hakios-mtk/logs/resume-sol-20261114-231152/pmif-independent-status.json` 保存内核 SHA256 与证据边界，构建原文见 pmif-independent-build.log。此版仍没有 B1，不作为刷机候选。
- 已读 XNU-LOCK-AVAILABILITY.md 的现有 hakios_spmi_lock 结论，未先行选择或实现锁，等 DSH 交付串行化设计。
- GPUEB 源码核对和生产 inline C 反例：`/home/lizi/IOS/hakios-mtk/logs/resume-sol-20261114-231152/gpueb-review/REVIEW.md`、counterexample-result.json；旧 RX 会被直接认作本次 ACK，冻结 now_ms 时无独立迭代上界，均仅为主机模型结果。集成应落实 stale-RX 前置、真实屏障/延时及超时后的歧义处理，不把 IRQ_CLR 当作取消 TX。
- 当前没有运行中的构建，未执行设备命令；手机已按用户交接归 Codex，首轮仍是 SID5 可读性与未插入基线，刷前另报产物和恢复方案。

---

## [REPORT] 2026-11-15 00:28 — B2/B3/B4/B8/B5 逐项复核摘要
状态: open

本轮没有改 B1 的锁，也没有新建锁；没有设备命令或刷机。下面行号对应当前文件及不可变复核快照 `/home/lizi/IOS/hakios-mtk/logs/resume-sol-20261114-231152/pmif-review/tree/`，SHA256 清单在 pmif-review/source-sha256.json。

| 项 | 当前改动 | 当前源码位置 |
|---|---|---|
| B2 | `chan >= PMIF_CHAN_COUNT` 或空输出指针，先返回 false，再做寄存器地址算术；恢复 helper 也检查 chan | keys.c:244、:246、:223 |
| B3 | keys / headset / mali 的 IDLE、读应答、读 ACK 后等待失败，检查 FSM；仅 FSM=6 时补一次 VLDCLR=1，并有界等 IDLE，然后仍返回失败；输出值只在完整读事务成功后发布；没有 SW_RST | keys.c:223、:262、:273、:284；headset.c:126；mali610.c:3057 |
| B4 | Mali 写 WDATA→ACC 后只等 IDLE，成功直接返回；不等 WFVLDCLR、不读 RDATA、不发读 ACK；读路径保留 WFVLDCLR→RDATA→ACK→IDLE | mali610.c:3072、:3089 |
| B8 | 三份 `PMIF_SIZE` 请求均改 0x8ff；同时给访问器加寄存器白名单，未知偏移读返回 0xffffffff、写不发 MMIO；Mali 原有 INIT_DONE(+0) 读取已移除；映射向整页取整仍不代表访问许可 | keys.c:93、:192、:204；headset.c:28、:89；mali610.c:3007、:3029、:3038 |
| B5 | headset init 移至 SID4 初次读的整个 if/else 之后，仍在同一 keys worker 内，不再被 SID4 成功条件门控 | keys.c:507、:508 |

复核命令（不碰设备）：

```sh
python3 /home/lizi/IOS/hakios-mtk/logs/resume-sol-20261114-231152/pmif-review/tree/mtk/tools/test-pmif-transactions.py
```

生产 C + ASan/UBSan：keys、mali、headset 均通过；包括 chan=3、非法偏移、遗留 FSM=6、一次 ACK 丢失、失败时输出值不变、Mali 写不等待 RX 等反例；原文 `/home/lizi/IOS/hakios-mtk/logs/resume-sol-20261114-231152/pmif-transactions-test.log`。

`tools/make-kernel.sh -j8` exit 0，原文 `/home/lizi/IOS/hakios-mtk/logs/resume-sol-20261114-231152/pmif-independent-build.log`；wrapper 只接受既有 kext symbolset 失败，编译/链接无新错误。内核文件 `/home/lizi/IOS/hakios-mtk/logs/resume-sol-20261114-231152/pmif-independent-kernel.bin`，15286976 字节，SHA256 `bf926ae19a2782a63db987e5e3668363b48c67158460b65c22b41c6fb521b84b`。

边界：以上是离线代码和模型证据；B1 仍未修，B6/B7 待串行化设计处理；没有把此内核当作上机候选。

---

## [REPORT] 2026-11-15 00:42 — B7 2ms 共用预算完成，GPU fault 已有读数
状态: open

B7：新增 `xnu/files/bsd/sys/hakios.pmif.h`，三份事务各在入口只建立一次 deadline（mach_absolute_time，2ms），等待 IDLE、读应答、ACK 后验、失败恢复共用同一预算和总计 20000 次轮询兜底；恢复不另起 2ms 预算，超时仍做有限的一次条件 ACK 后返回错。没有改 B1 或先行选锁，quiesce 移位也等整份设计。

生产 C 主机回归新增两项反例：IDLE 消耗 1.2ms 后 RX 不返回，总等待仍在同一 2ms 内退出；now_ms 不前进时由硬计数退出。keys/mali/headset 均通过，原文 `/home/lizi/IOS/hakios-mtk/logs/resume-sol-20261114-231152/pmif-timed-test.log`。完整构建 exit 0，原文 pmif-timed-build.log，内核 `/home/lizi/IOS/hakios-mtk/logs/resume-sol-20261114-231152/pmif-timed-kernel.bin` SHA256 `c8791f336588e55c44422e58b5b8e99fce1e26b025568802f7f5844194fa8257`。源码哈希见 pmif-timed-status.json，复核快照在 pmif-timed-review/tree；运行其中 mtk/tools/test-pmif-transactions.py 即可。

GPU 问题核查：
- 同一 NOPDC 轮 33.914358s 的 7/12 是中间态；34.188979s 已有 F3C=0x3ffc，F34 仍 0x1fc。厂商 header 定义 F3C/F40、mask 0x3ffc，原件已保存 gpu-evidence-review/gpufreq_mt6895.h:72。
- `kGpuFaultStatus=0x03c`、address=0x040/0x044 已定义且启动后实际读取（当前 GPU 文件:372、:4102），NOPDC 44.032796s `gpu fault 0 at 0`，44.043787s 又为零；AS0 faultstatus/address 亦零，见原始 nopdc/key-lines.txt:10。不需为这些现成读点重复刷机。
- MCU_STATUS=ENABLED 并不证明已取指，不能推导出一定执行后才卡在 glb；这些零 fault 采样也不能证明未执行。GPU 文件有历史注释互相矛盾，当前成功分支实际检查 global IF 或 version 改变，不能引用旧注释当事实。
- 另一个可补观测：启动循环实际读 kJobIntStatus(0x100c)，kJobIntRaw(0x1000) 目前只见常量定义；下一次 GPU 诊断可同时记录 RAW/MASK/STATUS 与 AUTO 前后完整域状态，但未实施、未借此假定根因。

仍无设备命令或刷机，首轮仍待 B1 完成后做 SID5 只读未插入基线。

---

## [REPORT] 2026-11-15 00:51 — panthor 第二个共享区握手与 entry words 边界
状态: open

panthor 的第二个独立握手信号已找到：共享内存 output->ack 对主机 req 的位翻转作出应答，GLB_PING 是最小的一种。

1. 初始启动顺序是 panthor_fw_start → panthor_fw_init_ifaces → panthor_fw_init_global_iface（/tmp/gpueb/ml/drivers/gpu/drm/panthor/panthor_fw.c:1545、1549、1553）。init_ifaces 在启动成功后读取 version / input_va / output_va / group_num，并逐个验证 CSG/CS 接口；这不是在 GLOBAL_IF 之前等 group_init 的协议，也没有额外的 GLB magic 等待。
2. GLB_PING 的请求位为 bit8（panthor_fw.h:214），panthor_fw_ping_work 在 fw.c:1478 把 req 对应位翻成 ack 的反值，敲全局门铃，在 :1481 等共享区 ack，100ms 上限。wait_acks 在 :1357-1378 用 READ_ONCE 轮询比较 (ack & mask)==req，最后还直接检查一次，因此成功不以收到中断为必要条件。若 CPU 只改 input、不改 output，且排除陈旧缓存/旧应答，一次真实的 ack 翻转能证明固件处理过共享区请求；比单读 ENABLED 强，也比单读一个可能预置的字段强。
3. GLB_STATE 只在接口版本 >=4.1.0 才支持（fw.c:328-332），ACTIVE 编码是0，不能把初始0当固件写回的证据；也不能对当前未发布有效 version 的接口直接套用。
4. CSG 的 START/RESUME 应答也能证明固件读过请求，但在 scheduler，而非早期 fw_start：panthor_sched.c:2394-2398 配置槽并请求 START/RESUME；:1986-2001 写 req、敲组门铃、等 output->ack，100ms；:1370-1374 只有收到 START/RESUME ack 才把组标为 ACTIVE。它依赖已发布的 CSG/CS 地址、VM、挂起区和 endpoint 配置，不适合拿来救当前尚无 version 的首轮启动。
5. 我们已经有 config+ping 应答测试（hakios.mali610.c:4206-4288），req 由 output->ack 反相生成，等共享区 ack。当前失败停在 start_mcu，没走到这个函数；所以“再加一次 ping”本身不补出可用早期信号。不能追随哨兵/0 值 input_va/output_va 写内存。

GPU 下一步建议仍由主控整合取指审计：先把 CPU reset-PC=(0x010018ed & ~1)=0x010018ec 的内容、该精确VA的软件PTE、AS0实际寄存器、JOB RAW/MASK/STATUS 的证据补齐。现有 entry words 其实前两项是 vec[2]/vec[3]，后两项是 code section 基址0x01000000，并非 reset-PC 的指令。真 trial692/NOPDC日志为 reset vector SP=0x02040000 PC=0x010018ed，四项=0x010019b1 0x010019b1 0xd00cf8df 0xf832f002，只证明CPU侧缓冲区/软件构造，不证明MCU已能按AS0取指。

另：F3C终态0x3ffc、shader0可符合启动前状态、fault0、counter递增，都支持降低重复上电实验的优先级；它们仍不足以逻辑排除MCU子块门控、可见性或取指路径问题，不把“剩下唯一原因”定案。本轮不改变GPU启动行为、不加入GPUEB或门铃实验。

---

## [REPORT] 2026-11-15 01:14 — B1+B3共享集成构建与129项首轮镜像待独立复核
状态: open

完整审阅说明：`/home/lizi/IOS/hakios-mtk/logs/resume-sol-20261114-231152/pmif-shared-integration/REVIEW.md`

B1+B3 共享集成已完成离线验证，供 DSH 独立复核。未执行任何设备命令、未刷机、未 commit。

kernel.bin SHA256：9dc40973afff3cf1551275d2da0fe80b8b4e9aa3089d32e1bcf1b26941cf191a
headset1/boot.img MD5：5e7c67f7cd8aeb90a86ff4098466812d
headset1/boot.img SHA256：8859138ecad4a56bdbe71a5c3ba5f472044556023b04ef9e27eed1aea77978c2
镜像 22986752 字节；完整参数及 loader/kernel/payload 哈希见 headset1/build.json。

实现与交付草案的差异：

- 共享实现放 mtk/mt6895/drivers/core/hakios.mt6895_pmif.c，公有头 xnu/files/bsd/sys/hakios.pmif.h；三个消费者只调用它。复用 hakios_spmi_lock()，没有另建锁；每笔事务单独加解锁，accdet_init 的 2ms/1ms 间隔在锁外。
- 使用单份 100ms deadline、全程累计最多 10000 次状态轮询；失败样本间隔至少10us，用时基忙等，暂停本身也有硬计数，冻结时钟仍能失败返回。旧 2ms/20000 实现已从活动源码和构建树消除。100ms 是轮询预算，之后可有有限的 STA/ACK 清理；不声称能界定一个自身不返回的 MMIO，也未实测硬件持锁耗时。
- mmio accessor 只承认 STA/RDATA 读、ACC/WDATA/VLDCLR 写；映射请求大小0x8ff，实际页粒度映射不授予0x900等偏移访问许可。移除了 chan 参数及越界算术。
- pmif_init 采用锁外 map、锁内发布。并发首调产生的多余映射在锁外按 VM 页粒度 unmap；map/unmap 都不在 spin 内。
- 所有失败点汇入同一 FSM=6 ACK 恢复，包括初始 IDLE 等待超时后才出现的 valid word；不重置全局控制器、不另起恢复预算。读仅在 ACK 后验 IDLE 成功后发布结果，写路径不等待 WFVLDCLR，保留 bit29 缺失会变读事务的注释。
- 严格拒绝 SID/地址/数值超宽及0x014b/0x0240写，先校验再编码，避免函数形参窄化后失去校验。
- 停核后的 headset_quiesce 只发布 atomic quiesced 标志，不取锁、不读/写PMIF、不ACK、不打印、不回放寄存器。saved_count 保留，明确没有恢复配置。回滚只在 keys worker 的失败路径执行。此项按 DSH 最新同意的“连 besteffort 也不做”实现。
- core/conf 新 driver MT6895_PMIF，三个消费者 requires；临时去掉配置的PMIF开关，三个依赖都能报错。drivers-check 的 usb 合法子系统遗漏已补，当前通过。

验证：

- preapply-check.log：冻结基线 md5 cb0a64f5/7bd9751d/255a08d4 匹配，3文件20hunk，4处ACC全部覆盖。应用使用检查器的精确上下文，不使用 fuzz。
- final-consumer-check.log：从该冻结基线到最终消费者代码的真实 diff 重新检查，仍3文件20hunk/4ACC零遗留；checker自身7项自测通过。
- state-machine-tests.log：实际生产共享C的10项基础状态机断言、6项集成检查全过；含1600笔三线程SID4/5事务、首次映射竞争、迟到FSM6、丢ACK、写完成、整笔100ms/10us、冻结时钟与硬顶；ASan/UBSan，严格警告作错误。宿主 pthread mutex 只代替既有锁入口，不替换事务代码；这不是手机SMP实测。
- 同一测试入口同时运行实际 accdet C + 共享PMIF C；验证129项零PMIC写、vendor配置/状态机、worker回滚和停核后零PMIF访问。它不证明真实MICBIAS供电或插入功能。
- kernel-build-second.log / build-console-second.log：完整内核构建包装器exit0；只有项目既有 kext symbolset/export 检查豁免。新共享模块实际编译并链接，最终四驱动/头文件与构建树逐字节一致。初次编译的新模块缺sys/types.h已修正。

首轮镜像边界：

- make xaga_defconfig 已执行，.config及 tools/hakios-config.py args 明确有headset=1；MT6895_PMIF和三消费者均启用，mtk/mt6895/configs/mt6895.config 已按用户要求单路径git add，未提交。
- 离线镜像刻意继承已验证trial692/NOPDC的完整loader flags，再增加headset=1。GPU参数使用该对照的 mali=5/mali_cold=1/mali_preas0=0；.config默认的mali=6/cold=0/preas0=1未混入这张对照镜像。actual_loader_args含headset=1且在loader二进制中按完整NUL字符串验证，463字节。gzip解包后payload与打包输入一致。
- headset=1 原124项 + 0008/0009/1cb2/230c/231d =129项，expected-registers.json给出完整顺序；只观测VAUD18，不启用。VAUD18未开时，AB读数不能作为检测成功的证据。无耳机，所以本轮仅SID5可读性与未插入基线，不能验插入、按钮或声音；headset=2的VAUD18/时钟前置项仍待后续实现。
- 验收不能只看一致率：需形状、镜像关系、231d锚点和供电前置一起解读。不引用baseline与round20相同MD5为两次独立实测证据。
- 镜像包含本轮之前已完成的三个loader日志修复：MARK_RUNNING不依赖WDT、rescue实际写HLOG、后续probe不覆盖已保存内核日志；未开watcher或未验证硬件WDT。日志修复仍需这次之后的真机验证。

审阅从 review-tree/ 与 review-source-sha256.json 开始；integration.diff是相对于本轮冻结快照的改动，不是对git HEAD的全树WIP归因。原有三个本地耳机提交 ef54fa0/7df8a9d/4ebad21 全保留，没有改历史或push。22个intent-to-add项仍保留原状，没有进行全树add；以后如要commit必须逐路径暂存并核对内容。

---

## [OBJ] 2026-11-15 02:12 — 当前源码与旧GPU笔记混用，暂不新增实验
状态: open

针对 GPU-REFIMPL-VERIFIED.md / 051cbac 的补充核查；以下锚定当前构建树 c9b4632，不修改源码、不新增设备实验。

1. timestamp 使能已实现：hakios.mali610.c:2665 的 mfg_set_timestamp() 写 gMfgcfg+0x130=3，:4497 调用。NOPDC kernel-lines.txt:167 回读0x3，:577 记录 timestamp 0x221e88b4 -> 0x221ec24b；不能再把“当前没有写13FBF130”列为待做。
2. 四个hw-issues寄存器不能凭厂商某段轨迹写零就排除：冻结镜像cold=1、hwissues=0，没有执行四写，当前值仍需实际观测。:3518 gpu_hw_issues() 已改为只读保留；:4594 后 gMaliHwIssues 分支才是显式四写零。
3. 当前文件没有 kGpuCmdCycleCountStart / kGpuStatusCcActive 两个宏，:335 是PDC定义；此前引用了其它快照。0x103可作为后续受控计数器实验，但它是写操作，不能标成只读或零风险；已有计数递增不会因缺本轮enable自动变假，也不能直接等同PLL已锁。
4. 当前PROT段没有跳过：:1069 明写 mapped with ordinary memory。NOPDC :213-222实际映射section6/7/8的三个flags=0x2b段，日志记录10 sections。旧笔记“只加载7段”不适用于此版，不代表这个差异已被证明是故障原因。
5. Ken-u/Mali.cpp:444 total=sizeof kFirmware，:447打印total。因此M1日志274432是固件数组字节数。“不是文件大小”需撤回。当前/tmp/kmali/mali_csffw.inc与/tmp/kenmali/mali_csffw.inc均不存在，历史同字节笔记不能替代当前对拍。本机NOPDC :192是258048/header0.1，而Ken-u M1记录header0.3，需区分版本/快照。

证据路径：/home/lizi/IOS/hakios-mtk/mtk/mt6895/drivers/gpu/hakios.mali610.c；/home/lizi/IOS/hakios-mtk/logs/headset-resume-20261114-220244/nopdc/kernel-lines.txt；/tmp/kenmali/Mali.cpp。

结论：先不往冻结的129项首轮镜像加入GPU实验，暂不能归结为“唯一剩下0x103”。本轮仅核查，源码、镜像及设备未改动。

---

通信记录 2026-11-15 02:13：旧句柄term_65478f6a-0846-40d4-b817-55c9be904afd已不在terminal list中；屏幕内容核验后，已补发到当前主控term_53a1b430-618f-470d-963f-0644cb6b0929，CLI返回accepted=true（不等于已读）。

## [OBJ-ACK] 2026-11-15 02:16 — PROT差异保留为候选；主控通道已恢复
状态: open

确认 PROT 是当前实现与 panthor 的真实行为差异，但尚未建立失败因果：

- 当前 hakios.mali610.c:1069 只打印并继续普通页映射；panthor_fw.c:588-592 明确跳过。不能再说当前实现也跳过。
- PROT 标记及 data_size=0 不能证明普通启动的全局变量必须放在其中；将其直接称为普通 .bss、认定首个全局写必触达，是注释中的推断。需要同一固件的 reset/初始化访问证据，或受控实验。
- panthor 是通用驱动：panthor_fw.c:812 按GPU架构组成 arm/mali/archX.Y/mali_csffw.bin，不是固定使用一份RK3588专用固件。因此既不能直接把它的成功套到本机，也不宜用“它只是RK3588固件”一笔排除参考价值。
- 将来如比较“跳过PROT / 普通页映射”，只改这一变量，保持固件字节、其余页表属性、时钟及启动参数一致；在首轮129项基线之外另立实验。目前不改冻结源码、镜像或设备。
- timestamp函数中 never been written here 等过期文字及PROT注释的因果断言，列入后续版本清理；本轮保留冻结文件哈希。

通信：旧主控term_65478f6a-0846-40d4-b817-55c9be904afd已经失效。当前主控为term_53a1b430-618f-470d-963f-0644cb6b0929，上一条核查已返回accepted=true；无须再等旧通道。建议主控同步更新协议中的固定handle。

---

## [DONE] 2026-11-15 02:18 — 明确收发方向，Codex会话未更换
状态: open

本会话环境ORCA_TERMINAL_HANDLE及pane/tab匹配确认：Codex是term_049b4b6b-b3fd-4afb-b8c2-797a5c7cb17d，未换会话。term_53a1b430-618f-470d-963f-0644cb6b0929是DSH方副终端，是此前补发的接收端，不是Codex的新handle。原先“新主控终端”措辞不够明确，现以双方方向为准。DSH→Codex继续使用049b4b6b；此前Codex→DSH补发走53a1b430，均返回accepted=true。

另记证据边界：同为arch10.8及相同固件查找路径不等于固件字节相同；仍需实际输入SHA256对拍，才能排除版本差异。冻结源码及镜像未动。

---
