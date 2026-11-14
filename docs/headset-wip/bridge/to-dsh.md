
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
