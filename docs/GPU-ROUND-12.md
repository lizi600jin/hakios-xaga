# 2026-11-13 · Round 12：hw issues 四写已执行，witness 仍无可见 store

684 按主控指定只启用 `mali_hwissues=1`。四个寄存器**写前已经全为 0，写后仍全为 0**；启动前 GPU store 轨迹为 86 条，比 681 的 82 条恰好多出 `0xf04/0xf08/0xf0c/0xf00=0`。witness 三次仍为 `0xff5aa5f0,0`，未观察到首个 store 完成。手机已恢复原厂 Android，`boot_completed=1`，boot_a MD5=`a63b2e35a7c2522cb24a6a10332b9bc8`，15:25:47 +08:00 独立复核通过。

## 684 的单变量与实现位置

已按交接顺序读完 round-11、STATE-MINI、untried-variables 和 master-2026-11-13。执行者独占 `GAJZ4HIN99AQ69Y9`，沿用 `mali=5`、完整 4K 映射、post-L2 AS0、high allocation、vendor 叶属性及 witness。

```text
mali=5 mali_cold=1 mali_preas0=0 mali_low=0 mali_trace=1 mali_witness=1
mali_ptvendor=1 mali_scclk=1 mali_scupd=1 mali_sc26=0
mali_cacheflush=0 mali_resetonly=0 mali_cohnone=0 mali_hwissues=1 mali_trial=684
```

新增 `gMaliHwIssues` 为静态零初始化，默认关闭，并解析 `mali_hwissues`。四写内联于现有可选 early reset 分支之后、`as_park_unused()` 之前；若 reset 被启用且失败，原有 return 仍阻止到达此处。顺序是先保存四个旧值，再经 `mwr()` 分别写零，`dsb sy` 后打印四个新值。

本轮以 **681 为语义基线**，`resetonly/cacheflush/cohnone` 均保持 0；boot-args 仅新增 `mali_hwissues=1` 及改变 trial 编号。684 **没有执行一次 soft reset**，因此不能称为已经验证了「reset 后补齐四写」的组合。此选择遵循交接的单变量和 §3 配套参数要求。

681 与 684 使用不同内核二进制。相对接手源码 `a918401e…`，新增实现仅为一个默认关闭开关、一处解析和上述内联分支，独立补丁见 `round-12-support/hwissues-only.patch`；不标成同内核 A/B。loader strings 按完整参数 token 检查，所有实际参数（包括 CPU/显示基础项、三个显式关闭开关和 trial）均出现 3 次，原文和计数在 `trial-684-arg-delivery.json`。

## 寄存器与判据

```text
37.759911 mali: hwissues trial 684: before shader 0 tiler 0 l2mmu 0 csf 0 -> after 0 0 0 0
37.760025 mali: protocol trial 684: features 0x1 before 0 after 0 wrote 0
37.897267 mali: coverage: every section fully mapped
37.897333 mali: round7 AS0 after UPDATE: raw 0x10000 status 0 fault 0 addr 0 gpu_fault 0 trial 684
37.897487 mali: views before AUTO: gpu status 0/0 irq raw 0/0x600 mcu control 0 status 0
46.735433 mali: MCU status 1 (enabled), job irq 0, gpu irq 0
46.736667 mali: after the start request: gpu status 0x1, gpu irq status 0, job irq status 0, mmu irq 0, as0 status 0 faultstatus 0 faultaddress 0
46.742434 mali: version word 0xff5aa5f0 (untouched; no firmware version published) trial 684
46.748776 mali: witness: read1 0xff5aa5f0 0 read2 0xff5aa5f0 0 read3 0xff5aa5f0 0 trial 684
47.811012 mali: bus idle after the failed start: qchannel 0x11 debug_sel 0x3 debug_top 0x1b (still busy after 100000 spins)
```

AUTO 前 MCU control/status 为 `0/0`；AUTO 后轮询 MCU status 为 1，JOB/GPU/MMU IRQ 均为 0。未额外增加 AUTO 后 MCU_CONTROL 的直接读回；失败尾部原有 DISABLE 写 0 后，control 读 0、status 仍 1。原日志的 `(enabled)` 是既有解释，不作为固件已经启动的证据。

`debug_top` 在 AS0 walk 前后为 `0x4`，AUTO 后 spin 样本及失败尾部为 `0x1b`；DISABLE 后等待 100000 次仍为 `0x1b`。MCU 窗口的 `0x3020` 前后均 `0x600`，失败尾部 `0x3034=1`，计数器变化见保存的 MCU 正文。

判据仍是 witness 程序的 `0x11 → 0x22 → 0xdeadbeef` 三次 store，最终目标两字应为 `0xdeadbeef,0x22`。本轮只观察到未变的哨兵，不能从中证明零指令执行。四个 quirk 字的读回只证明配置动作；由于旧值已全为 0，这一轮也没有发生「写 0 覆盖非零 quirk」的状态变化。

## 完整写轨迹与指纹

trace 在 AUTO 前冻结，因此 **86 是启动前已完成的 GPU store 数，不含 AUTO 本身及失败尾部**。完整序列与 681 比较，仅将 AS0 TRANSTAB LO/HI 的物理分配地址归零：

```text
insert at start: 0xf04=0, 0xf08=0, 0xf0c=0, 0xf00=0
remaining normalized sequence: identical (82 stores)
```

未归一化差异也保存在 JSON：除四写外，只有 `0x2400` 从 `0xee5b0000` 到 `0x9fc0c000` 的 AS0 root PA 变化。684 root=`0x19fc0c000`。没有新增 `0x304`、reset 或 cache command 写。

| 产物 | 684 指纹 |
| --- | --- |
| boot 镜像 MD5 | `cdd641286e77be61f973fa55da843d6c` |
| 驱动 SHA256 | `e9300e29bcc0c41b8d28f4644e3c8f1ef74ff5982c6495ea7e3a8a21e4822aab` |
| 内核 SHA256 | `2c31b71aea82aef777ca4b44a442cc8ab77f958d7476e3b39cd0bdf0a9d5b532` |
| payload SHA256 | `6b95a459cc8c5cc2d08901df38d4a8bd36c8fe6fcabe00e6844c1b30e62d46c0` |
| 原始 logbook SHA256 | `40875cad9b706e84125ca52214e1e0a1de785ceafa25bab6919ae25d34dc2373` |

内核编译通过 `tools/make-kernel.sh` 的既有错误分类门：仅接受已知 kext symbol-set 检查错误，没有用旧文件存在作为成功判据。源码、同步进 XNU 的源码及发布树源码 SHA256 一致；实际内核包含新分支日志。构建、payload、loader、pack 日志和校验 JSON 随报告保存。

## 补齐 681–683 的发布证据

这三轮在接手前已跑完。本会话只离线重解析其完整原始日志，补上未发布的构建/参数/正文/写序列及恢复元数据，没有重刷这三轮。它们的驱动 SHA256 均为 `a918401ea06da499b428ba38ac65c6e22dd3bf6f38df1f8acd55ce1fcf91b484`，内核 SHA256 均为 `abd289518ee8f4e96ae87ab4c1b5b31c84e3ba5b4bf16fb3f699b72650526a49`，源码快照保存在 support。

| trial | 动作 | 启动前 store 数 | 镜像 MD5 | witness |
| --- | --- | --- | --- | --- |
| 681 | 恢复 `mali_sc26=0`，作为本轮基线 | 82 | `6d87dbe7977444569c92273265c12d90` | `0xff5aa5f0,0` |
| 682 | 只加 `mali_cohnone=1` | 83 | `2627ff26c9f216acfbf17244c3f6aed1` | `0xff5aa5f0,0` |
| 683 | cacheflush + resetonly + cohnone 收尾组合 | 89 | `c442351f5a4ecc11e52babddb59bac1b` | `0xff5aa5f0,0` |
| 684 | 只加 `mali_hwissues=1` | 86 | `cdd641286e77be61f973fa55da843d6c` | `0xff5aa5f0,0` |

681→682 归一化后仅增加 `0x304=0x1f`。681→683 仅增加 reset 三写、`0x304=0x1f`、cache 三写，分别有完成日志；其失败仍不能归因于单项。681 的 GPU 写序列与 678 归一化后相同，该比较不覆盖 GPU aperture 外的时钟 mux 写。

## logbook、恢复与下一步

684 沿用已验证 runner：先备份专用 logbook，再只清理并回读验证分区尾部 `offset=2676981760`、`bytes=4198400` 的窗口，分区总长校验为 `2685370368`。前一窗口 SHA256 正好等于 683 logbook，清零 MD5=`34987ee855d45be16fbfd8c3f6f7827f`。解析完整 4198400 字节，不按 header len 截断；所有 Mali trial 标记只有 684，没有外来尾部。

root adbd 返回后立即拉日志；本次返回阶段 `boot_completed=False`。随后 root dd 写回已验 MD5 的 stock boot_a，fsync、分区 MD5 复核、sync、sysrq restart；runner 完成且独立 adb 复核均确认原厂 Android `boot_completed=1`、boot_a MD5 正确。没有访问三个禁读地址，也未启用 `mali_rpc`。

结论限于：在 681 基线上补四个零写，未使 witness 产生可见 store；不支持「这四字当前存在非零 quirk，清零即能修复」的解释。主问题未解决。主控表中下一项为独立验证 `STACK_PWRON_LO(0xe20)` 与 `STACK_READY_LO(0xe10)`，再是 `mali_mcuwin=1` 全窗口只读观测；本会话尚未执行这些动作。

本地报告为 `out/round-12.md`，发布报告为 `docs/GPU-ROUND-12.md`，文本证据在 `docs/round-12-support/`。完整 raw logbook 留在 `out/round-8-support/`，684 镜像留在 `out/round-12-support/trial-684-boot-a.img`。离线重算：`python3 out/round-12-support/analyze.py 684 --input-dir out/round-8-support --baseline 681`。
