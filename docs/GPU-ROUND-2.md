# Round 2：明确冷路径的真机基线与独立开关对照

2026-11-12，手机已由用户授权给 Codex。按 PHONE-OPS 执行，只修改 Mali 主驱动；固件头与其它驱动保持不动。发布仓库 `/home/lizi/IOS/repo/hakios-mt6895`。

## 分析依据

见 [round-1.md](GPU-ROUND-1.md)。提供的旧 logbook 实际是 cold=0，不能替代纯冷基线；本轮显式 cold=1、low=1，并记录实际 GPU stores，排除构建/参数与所分析版本错位。free-run 与 AS2/AS3 属性通过两个独立 boot-arg 控制，均默认关闭。读取 MCU 窗口的词都已在旧驱动扫描中读通，不写未知 MCU 寄存器。

驱动快照 SHA-256：`e0ae936e522ab8087fcdad621f1815679c935e095041c63a2fbde0242a53c5d3`。kernel 编译成功，make-kernel 判定只剩既有 kext symbol-set 错误；payload 与 boot image 打包成功。

## 实验记录

### trial 203：mali=3 纯冷基线（通过）

boot-args：`cpus=4 buttons=2 touchscreen=3 keylog=1 key_scan=1 fbwidth=1080 fbheight=2460 fb_disp=1 mali=3 mali_cold=1 mali_preas0=1 mali_low=1 mali_freerun=0 mali_parkattrs=0 mali_trace=1 mali_trial=203`。

镜像 MD5：`0ecb4496d3686b1d9c9977a7fd536216`。22974464 B。2026-11-12 21:58:57 +08:00 fastboot reboot 已独立执行；Sending/Writing 均 OKAY。约 205 s adb 恢复，随后 `sys.boot_completed=1`。已拉到带 trial 203 标记的新日志（169 条 mali 行、时间无回退）。

关键原始日志：

```text
000617cb 33.277316 mali: interconnect before the firmware: dcm0 0xc03f->0xc03f async 0x3c00000->0x3c00000 async1 0xf7->0xf7 global 0x3281701->0x30e7201 qchannel 0x10->0x10 rpc 0->0
000627de 33.377068 mali: firmware loaded; stopping here because mali=3
00062822 33.377073 mali: views firmware loaded, MCU not started: gpu status 0/0 irq raw 0/0x600 mcu control 0 status 0
00062896 33.377091 mali: views firmware loaded, MCU not started 0: cycle 0x1c85796/0x1c857d7/0x1c85819 enclosed 1 timestamp 0x19dcd244/0x19dcd247/0x19dcd24b enclosed 1
0006293b 33.378236 mali: views firmware loaded, MCU not started 1: cycle 0x1cd91e6/0x1cd9227/0x1cd9268 enclosed 1 timestamp 0x19dd0c43/0x19dd0c44/0x19dd0c48 enclosed 1
000629e0 33.378249 mali: trial 203 mali=3 stop: level 3 cold 1 low 1 preas0 1 freerun 0 parkattrs 0 trace 1
00062a49 33.378254 mali: write trace: 68 completed GPU stores (capacity 128)
```

结论：基线能返回 Android。MCU_CONTROL=0、MCU_STATUS=0 时，两组计数器均满足 AP-before ≤ MCU-view ≤ AP-after，且间隔约 1 ms 后继续推进；0x3090/0x3098 是硬件计数器视图的解释获得直接支持，不能当固件执行证据。`GPU_STATUS=0/0`；AP raw=0 而 MCU-view raw=0x600，支持独立 IRQ 锁存。实际 store trace 完整 68 条，无 AUTO；只是 mali=3 基线，尚未判断完整 MCU 启动。

### trial 206：mali=6 完整纯冷基线（Android 返回、MCU 仍失败）

同一内核，只将 mali=3 改为 mali=6（trial 编号随之改变），free-run/parkattrs 均关闭。镜像 MD5：`864cebe88db92ab4aa3cf5b1a41f792c`。

2026-11-12 22:04:02 +08:00 独立 reboot；随后 adb 恢复且 `sys.boot_completed=1`。

```text
0005f2b1 33.141458 mali: views before AUTO: gpu status 0/0 irq raw 0/0x600 mcu control 0 status 0
0005f4cc 33.149348 mali: trial 206 before AUTO: level 6 cold 1 low 1 preas0 1 freerun 0 parkattrs 0 trace 1
0005f535 33.150661 mali: write trace: 82 completed GPU stores (capacity 128)
00060480 33.254110 mali: spin 0: status 1 job 0 mmu 0 fault 0 at 0 top 0x1b
00063e75 41.637929 mali: the start loop ended after 1000000 spins (halt seen 0, fatal seen 0)
00063f1f 41.640178 mali: after the start request: gpu status 0x1, gpu irq status 0, job irq status 0, mmu irq 0, as0 status 0 faultstatus 0 faultaddress 0
000640e4 41.645966 mali: version word 0xff5aa5f0 (untouched, so no instruction ran)
0006486c 42.716857 mali: MCU control written 0, status now 1, control reads 0
00064937 42.721248 mali: views after DISABLE: gpu status 0x1/0x1 irq raw 0/0x600 mcu control 0 status 0x1
```

结论：完整 cold=1 路径同样失败，现在已有独立实录支持。82 次启动前 GPU store 完整，无 reset/key/coherency/ASN/cache flush。GPU_STATUS 在 AUTO 前明确 0/0，失败后 1/1；ACTIVE=1 是在启动请求后出现，不能解释为本轮启动前置障碍。0x3020 继续与 AP raw 独立。

### trial 216：仅 free-run（Android 返回、MCU 仍失败）

同一内核，相对 206 只打开 `mali_freerun=1`，parkattrs 仍为 0。镜像 MD5：`295999eefee3bd142ad8cb9f3785e499`。

C0 与原始 cold trace 按 `(offset,value)` 多重集直接比对：61 次相同、Android-only 6 对、本机-only 21 次；其中 6 次为同 offset 值替换，其余 15 次为诊断清锁存。完整实际 82 次写序列，逐项等于 round-1 的 94 次模型去掉 12 次 reset 分支操作，模型因此获得本轮动态验证。顺序仍是 AS2..7→IRQ→IPA→AS0→L2，不宣称已与 Android 的 AS0-first 顺序相同。

2026-11-12 22:09:11 +08:00 reboot，约 218 s 后 Android 在线且 `sys.boot_completed=1`。

```text
000562b0 33.477451 mali: interconnect before the firmware: dcm0 0xc03f->0xc03f async 0x3c00000->0x3c00000 async1 0xf7->0xf7 global 0x3281701->0x30e7701 qchannel 0x10->0x10 rpc 0->0
00057bd0 33.661324 mali: trial 216 before AUTO: level 6 cold 1 low 1 preas0 1 freerun 1 parkattrs 0 trace 1
00057c39 33.661327 mali: write trace: 82 completed GPU stores (capacity 128)
00058aba 33.661490 mali: spin 0: status 1 job 0 mmu 0 fault 0 at 0 top 0x1b
0005e232 42.710243 mali: the start loop ended after 1000000 spins (halt seen 0, fatal seen 0)
0005e2dc 42.715799 mali: after the start request: gpu status 0x1, gpu irq status 0, job irq status 0, mmu irq 0, as0 status 0 faultstatus 0 faultaddress 0
0005e4a1 42.727780 mali: version word 0xff5aa5f0 (untouched, so no instruction ran)
0005ecf3 43.783680 mali: MCU control written 0, status now 1, control reads 0
```

结论：目标 GLOBAL_CON=0x030e7701 已回读，QCHANNEL bit4 也为 1，实际 82 次 GPU stores 与 trial 206 完全一致。仍无 GLOBAL_IF，哨兵未改，debug_top 永久 busy；否定“仅打开指定 free-run 位即可解除当前停滞”。不能扩大为全部 GALS/clock 路径都正确。C0 已有 bit9/19=1，本轮实际改变的是 bit8/10。

### trial 226：仅 AS2/AS3 MEMATTR（Android 返回、MCU 仍失败）

相对 C0 只打开 `mali_parkattrs=1`，free-run 恢复 0。镜像 MD5：`29869c06df02ac566c216b8177c1327d`。

2026-11-12 22:14:21 +08:00 reboot，约 218 s 后 Android 已在线且 `sys.boot_completed=1`。实录相对 trial 206 只改变 0x2488、0x248c、0x24c8、0x24cc 四字，全部精确等于 Android cold trace。

```text
0005e8f2 33.162467 mali: trial 226 before AUTO: level 6 cold 1 low 1 preas0 1 freerun 0 parkattrs 1 trace 1
0005e95b 33.166827 mali: write trace: 82 completed GPU stores (capacity 128)
0005ea8b 33.187762 mali: write[3] 0x2488=0x888d8f88
0005eabc 33.191804 mali: write[4] 0x248c=0x84c8d
0005ec54 33.224101 mali: write[11] 0x24c8=0x888d8f88
0005ec86 33.228202 mali: write[12] 0x24cc=0x84c8d
0005fc51 33.541384 mali: spin 0: status 1 job 0 mmu 0 fault 0 at 0 top 0x1b
000638a4 42.363315 mali: the start loop ended after 1000000 spins (halt seen 0, fatal seen 0)
0006394e 42.365570 mali: after the start request: gpu status 0x1, gpu irq status 0, job irq status 0, mmu irq 0, as0 status 0 faultstatus 0 faultaddress 0
00063b13 42.371518 mali: version word 0xff5aa5f0 (untouched, so no instruction ran)
00064365 43.473158 mali: MCU control written 0, status now 1, control reads 0
```

结论：补齐这四字单独不足以修复启动；本轮没有归因于 AS2/3 属性的依据。不能否定它们与其它条件的组合依赖，因此再做 free-run 与属性同时打开的一轮对照。

### trial 236：free-run 与 MEMATTR 同时开启（Android 返回、MCU 仍失败）

`mali=6 mali_cold=1 mali_low=1 mali_preas0=1 mali_freerun=1 mali_parkattrs=1 mali_trace=1 mali_trial=236`。相对属性单独开启的 226，只增加 free-run；用于检查两个条件的交互。

镜像 MD5：`70988393ad3cf58debc00074f0c086ad`。2026-11-12 22:19:49 +08:00 独立 reboot；约 204 s adb 恢复，随后 sys.boot_completed=1，并已拉取新日志。

第五轮驱动 SHA-256：`910b94f8f720be42895a6418825ed93c3727321772052d29a83bdd8ae586f7d2`。203/206/216/226 使用前一版 `e0ae936e...a53c5d3`；236 使用此版本，两版差别仅为注释与三条诊断文字：将 MCU enabled 与 running 区分，把 IRQ 上半位标为 request completions，把哨兵未改描述为“未发布版本”，没有改变硬件操作。该版内核已通过相同构建判定，并重打 payload 与镜像。


```text
00057efe 32.942064 mali: interconnect before the firmware: dcm0 0xc03f->0xc03f async 0x3c00000->0x3c00000 async1 0xf7->0xf7 global 0x3281701->0x30e7701 qchannel 0x10->0x10 rpc 0->0
00059f73 33.282121 mali: trial 236 before AUTO: level 6 cold 1 low 1 preas0 1 freerun 1 parkattrs 1 trace 1
00059fdc 33.282125 mali: write trace: 82 completed GPU stores (capacity 128)
0005ae7b 33.282287 mali: spin 0: status 1 job 0 mmu 0 fault 0 at 0 top 0x1b
0005ff78 41.874452 mali: the start loop ended after 1000000 spins (halt seen 0, fatal seen 0)
00060016 41.876869 mali: after the start request: gpu status 0x1, gpu irq status 0, job irq status 0, mmu irq 0, as0 status 0 faultstatus 0 faultaddress 0
000601db 41.882677 mali: version word 0xff5aa5f0 (untouched; no firmware version published)
000609d0 42.942387 mali: MCU control written 0, status now 1, control reads 0
```

结论：free-run 目标值已回读，MEMATTR 四字也在完整实际写轨迹中；82 次 GPU stores 与属性单开的 226 相同，仍无 GLOBAL_IF，哨兵不变、busy 永久。两项单独或组合在本轮测量条件下都不足以解除阻塞。五次实机运行均正常返回 Android，未使用救砖包。

## 五次已完成刷机汇总

| trial | 配置（均 cold=1、low=1、preas0=1） | 镜像 MD5 | 结果 |
|---:|---|---|---|
| 203 | mali=3，free=0，attrs=0 | `0ecb4496d3686b1d9c9977a7fd536216` | Android 返回；未请求 MCU 启动；68 stores |
| 206 | mali=6，free=0，attrs=0 | `864cebe88db92ab4aa3cf5b1a41f792c` | Android 返回；MCU 失败；82 stores |
| 216 | mali=6，free=1，attrs=0 | `295999eefee3bd142ad8cb9f3785e499` | Android 返回；MCU 失败；82 stores |
| 226 | mali=6，free=0，attrs=1 | `29869c06df02ac566c216b8177c1327d` | Android 返回；MCU 失败；82 stores |
| 236 | mali=6，free=1，attrs=1 | `70988393ad3cf58debc00074f0c086ad` | Android 返回；MCU 失败；82 stores |

## 测量边界与尚未刷入的最终驱动

五次测试全部 `mali_trace=1`。其中完整启动测试的 82 行 trace 在 AUTO 前集中打印，206 的 trace header 到 spin0 约 0.103 s，226 约 0.375 s。因此上述结论准确限定为本轮测量条件，不宣称已经排除诊断打印造成的启动时序影响。

收尾已将驱动改为：AUTO 前只冻结缓冲，启动尝试结束后才打印；`mali_trace=0` 同时关闭新增 MCU/AP 包围采样。旧的 mcu_window_dump 仍保留。该低干扰版本已完成本地 kernel/payload/image 构建，只有任务书中既有的 symbol-set 错误通过 make-kernel 判定。**用户明确结束本轮，所以此版本没有刷入，没有第六次真机结果。**

最终交付驱动 SHA-256：`13110e870a7f29d06f936e6078a3321a905335385141ebd2c05b20bf8124926f`。本地未刷 trial 246 镜像 MD5：`4323368f07854e9506f548d54dc4b053`（trace=0，free=1，attrs=1）。主机 `xnu-flashed-slot_a.img` 现在是这个未刷候选；手机最后确认仍运行第五次 trial 236 后的 Android。不要混淆二者。

## 本轮成立的结论与下一步

1. 原始 cold trace 已找回并完成 67 次写逐条对账；AS1 缺席、AS2/AS3 四字属性差异真实存在，94 次旧模型又获 82 次新冷路径实录支持。
2. MCU_CONTROL=0 时，0x3090/0x3098 已与 AP cycle/timestamp 同步推进；计数器跳动不代表固件已执行。AP raw=0 而 0x3020=0x600，支持独立目的端锁存解释。
3. GPU_STATUS 明确 AUTO 前 0，失败后 1；不是本轮启动前 ACTIVE=1 卡住。MMU 高半位继续按 REQ_COMPLETE 解释。
4. free-run 与 AS2/AS3 属性在本轮单开、组合均未修复启动。完整纯冷启动仍未拿到 GLOBAL_IF，MCU 根因未解决。
5. 下一会话先验证尚未刷入的低干扰版本；之后分开比较 AS0-first 启动顺序与额外 IRQ 清除。当前 82 次写比 Android 多 13 次诊断 MMU clear、2 次 power IRQ clear，且 AS0 位于 IRQ/IPA 后；每个 AS 内写序也与 Android 不同。建议分别测试 AS0 移前（仍 82 次）、仅去 13 次 MMU clear（69 次）、仅去 2 次 power clear（80 次）；不要一开始把所有变化混在一起。power settle 清除还影响 20000-spin 等待，需要显式控制延时变量。

自用户发出收尾指令后，不再调用 adb/fastboot，也不再刷机。手机所有权交给新的 Astra 会话。完整接管说明保留在本机 `/home/lizi/IOS/work-xaga/codex/out/HANDOFF-TO-ASTRA.md`。
