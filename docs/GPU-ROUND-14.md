# 2026-11-13 · Round 14：AUTO 后单次 doorbell 与 F3C 稳定等待

688 的 AUTO 后单次 doorbell 已发出，JOB IRQ 前后为 0，百万次轮询未见 boot bit31，witness 三次仍为哨兵。689 等待 F3C 满足八个 1ms 稳定间隔，初值/末值均为 0xf，0 次变化，继续 AUTO 后仍无可见 witness store。两条均完成了指定动作；固件启动问题未解决。688/689 均已恢复原厂并独立核验 boot_completed=1 与 stock boot_a MD5。

## 688：AUTO 后一次 doorbell 已送达

新增默认关闭的 `mali_doorbell`，分支内联在 AUTO 写及现有 mcuseq 分支之后。回到 681 的语义基线：mcuseq/mcuwin=0，hwissues/stackpwron/cacheflush/resetonly/cohnone=0，只加 `mali_doorbell=1`。分支发一次 `0x80000=1`、barrier 后打印 JOB IRQ 前后值和 MCU 原始状态，没有读回 doorbell 寄存器。

```text
38.760523 mali: views before AUTO: gpu status 0/0 irq raw 0/0x600 mcu control 0 status 0
38.770608 mali: doorbell trial 688: wrote 1 to 0x80000, job_irq before 0 after 0, mcu_st 0x1
48.181955 mali: the start loop ended after 1000000 spins (halt seen 0, fatal seen 0)
48.281188 mali: MCU status 1 (raw state 1; boot unproven), job irq 0, gpu irq 0
48.282482 mali: after the start request: gpu status 0x1, gpu irq status 0, job irq status 0, mmu irq 0, as0 status 0 faultstatus 0 faultaddress 0
```

48.302351s 的 witness 三次值均为 `0xff5aa5f0,0`。第三次值后穿插了另一线程的 `process lsd…` 日志，trial 标记随后续行输出；三个数值本身完整。原片段和 byte offsets 见 `trial-688-witness-fragment.txt` / `trial-688-witness-observation.json`，没有猜填或把前轮日志拼进来。

这关闭的是“本时点单次 doorbell 足以恢复 witness”的候选；未观察到处理结果，不能区分没有处理门铃和处理后仍未完成 store。Android 首次 doorbell 发生在 boot IRQ 之后，因此本轮是主动提前触发的探索，不把它描述为已经复现 vendor 启动顺序。

trace 在 AUTO/doorbell 之前冻结，**82 是冻结前 GPU store 数，不含 AUTO 和新增 doorbell**。与 681 完整序列只差 AS0 TRANSTAB PA，归一化后零差异。新动作由分支日志和源码到达路径证明，不能从计数相同推断没有新写。688 与 681 使用不同内核，是语义基线对照。

## 689：八个稳定间隔已满足，F3C 无变化

用户最新指定先试 `mali_pdcsettle=1`。分支默认关闭，插在 trace 冻结之后、AUTO 之前；以 SPM 基址读取宏 `MALI_SPM_PWR_STATUS_GF=0xF3C`，每隔 `delay(1000)` 采样一次，值相同则增加连续计数，变化时清零并打印 F40。稳定间隔达到 8 才继续；交接给定上限为 200000 次，非稳定退出另标 NOT SETTLED。新代码无 `mwr/reg_wr/gpu_wr64` 调用，不要求域位达到 0x3ffc。

689 保持 681 语义基线，其它试验开关均为 0，只加 pdcsettle。未运行的 status 尾部补丁已从本次源码撤出；它没有被打包或刷机，不计为任何已做 trial。

```text
39.558890 mali: views before AUTO: gpu status 0/0 irq raw 0/0x600 mcu control 0 status 0
39.560062 mali: pdcsettle trial 689: before 0xf fd 0xf target 8 interval 1000 us limit 200000
39.569095 mali: pdcsettle trial 689: settled at 0xf (& 0x3ffc = 0xc) after 8 spins, stable 8 changes 0 before 0xf
49.543223 mali: after the start request: gpu status 0x1, gpu irq status 0, job irq status 0, mmu irq 0, as0 status 0 faultstatus 0 faultaddress 0
49.557068 mali: witness: read1 0xff5aa5f0 0 read2 0xff5aa5f0 0 read3 0xff5aa5f0 0 trial 689
```

两条等待日志间隔 **9.033ms**，包括日志和 MMIO 开销。初值加八个间隔采样未观测到变化；它不是连续时间的硬件轨迹，也不证明所有依赖域都已满足。可以断言的是：**本轮所加的八个稳定间隔等待没有恢复可见 store**；不能扩展成所有 PDC/启动时序可能性均被排除。trace 仍为 **82**，与 681 仅 root PA 不同，归一化后零差异。

原 686 的两次 F3C 读数都是真实的，但有两个口径差别：38.111310s 的 `0x4` 已经 &0x3ffc；38.470831s 的 `0xf` 是完整值。对齐掩码后为 4→c，新增 bit3 对应 MFG2/L2。两次之间明确发生了软件 L2_PWRON 请求及 38.200286s 的 `L2 up: ready 1`。**没有直接写 F3C 这个状态字，不等于没有发送会触发其变化的硬件命令**，因此不能据这两个点单独证明 PDC 队列尚未稳定；689 将等待作为独立变量检验。

## 原始状态与证据边界

两轮 AUTO 前 MCU control/status 均为 `0/0`，请求后 raw status 为 1，JOB/GPU/MMU IRQ 与 AS0 status/fault/address 均为 0。debug_top 在 AS 配置前后检查为 4，请求后及 DISABLE 后为 1b；这是状态样本，不直接证明实际总线取指。

688 失败尾部已有 `MCU control written 0, status now 1, control reads 0`；685–687 同样有该读数，均在既有约2ms等待之后。与同轮 AUTO 前 status=0 合在一起，说明读数会从0变1，不能称为“始终为1的常量”。Panthor stop 实现写 DISABLE 后会有界等待 status=0，kbase stop 等待函数也以0为目标；停止状态未到达不等于寄存器未实现。源文件片段及指纹在 `disable-status-source.json`。额外的第二次 DISABLE/1ms 观测本轮尚未执行。

witness 预期仍为11→22→deadbeef三次store、最终deadbeef,22。三次哨兵只说明未观察到首个store完成，不能证明零指令执行。MCU_STATUS=1不作为固件boot成功、状态机已执行命令或核心活着的独立证据；688源码已将旧“enabled”打印改为“raw state 1; boot unproven”。

## 构建与恢复

| trial | 镜像 MD5 | 驱动 SHA256 | 冻结前 GPU stores |
| --- | --- | --- | --- |
| 688 | `c8256ae5a43714471d81ded491ef34e3` | `19dfdcd0e664a6d50932d55054eb06a92bd14c1d4db79f655afb1cb6902482b9` | 82 |
| 689 | `a1d129a87eb9f2e37d2fa38e3409b475` | `c17402e014cd0371de4dc6125153de5a4456b780dc465e3956b7f69aa50c1e6d` | 82 |

688 内核 SHA256=`ef9451dd5f4436ee21f87291328a6f26084c6b2a0ba10a14ca403e7c9dfc3d9a`，payload=`f3e64dcee79a05918b58d64148174adc3ef95c9f2df9860464d77ab00d33650f`。
689 内核 SHA256=`36c66ca5e8fced9567b8e96ebd82b2bce7c8779b8174284e736345d321489d82`，payload=`b7616bc4e22822132f2ba6b1d3bf1e6596c0e480ce16537187e917922a3d8fd4`。

两次实际使用的内核均通过 make-kernel 既有错误分类门；源码同步身份及内核包含新日志已验证。全部实际 loader 参数按完整 token 校验，出现次数各为3；参数全文及指纹在 build/arg-delivery JSON。精确688/689源码快照与独立补丁随包保存，镜像和完整raw留本地。

每轮只备份/清理并读回校验 xnusys 专用4198400字节窗口（offset2676981760、分区总长2685370368），完整解析，trial标记唯一，无前轮尾部。原始logbook SHA256和预清理/恢复步骤见trial JSON。688已独立复核原厂 Android boot_completed=1、boot_a MD5=a63b2e35a7c2522cb24a6a10332b9bc8（16:25:42 +08:00）。689亦已独立复核通过，时间为16:43:17 +08:00。

未启用mali_rpc，未访问三个禁读地址；没有为点亮0x3ffc增加电源写。报告为out/round-14.md / docs/GPU-ROUND-14.md，文本证据在docs/round-14-support。round-13（685–687）及本报告一起发布。
