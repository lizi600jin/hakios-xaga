# Codex 账号切换交接：xaga 启动复现、PDC 单变量结果、耳机源码

时间：2026-11-14 22:59，Asia/Shanghai。用户因本账号额度仅剩 4%，要求保存交接并由主控切换 Codex 账号。**本代理停止工作；尚未启动新代理、未切换账号。**

## 0. 接手先看这几条

1. **手机已恢复原厂安卓，ADB 在线、`sys.boot_completed=1`。** 最后核实见 `nopdc/device-final.txt`。最后一次操作是恢复原厂 `boot_a` 并重启，没有待完成的刷机或构建。
2. **今天已找回真实 trial 692 镜像，且用当前源码与 692 的真实编译参数重建出了逐字节相同的 loader。** 不要再回退 `git show HEAD:mtk/loader/mtkloader.c`；HEAD 是另一个 SoC 的 MT6768 loader。
3. **真实 692 原镜像能进 XNU；只换成今天的新内核也能进 XNU。** 先前“新内核必坏／耳机导致进不去”的结论均无证据。
4. **最高优先级 PDC 单变量实验已完成一次：`mali_nopdc=1` 生效，MFG5/6/10/12 恢复 PWR_ACK，`0xF3C=0x3ffc`，但 MCU 仍不发布固件版本，哨兵仍为 `0xff5aa5f0`。** PDC 早开确实影响电源域，却不足以单独解释 MCU 不启动。
5. **日志缺失已发现直接证据：完整内核日志写盘后，GPU 关电探针再次写 HLOG，覆盖头部及首个 body 块。** 不要只按当前 HLOG 的 79 字节长度判断“零内核行”。完整 4 MiB 窗口已保存。
6. **不再自动刷实验镜像。** 用户要求单次实验后暂停；本轮 PDC 已刷一次，随后仅恢复原厂。下一轮须按主控／用户当前指令推进。
7. 用户最新提供完整耳机源码，本代理已局部核对。**没有实现 `headset=2`，也没有恢复 headset 调用点。** 原始目标仍是 3.5mm 插入识别与解码播放；当前没有任何耳机寄存器的 XNU 真机读数。

## 1. 工作区、设备与安全约束

主仓库：`/home/lizi/IOS/hakios-mtk`，约 49 个跨 GPU/loader/UFS/input/audio 的未提交改动。**不要全树 stash/reset/checkout；这些改动包含 xaga 能启动所需内容。**

设备序列号：`GAJZ4HIN99AQ69Y9`。脚本用 `HAKIOS_SERIAL` 环境变量；不要重新写死进可提交脚本。

原厂恢复镜像：

```
/home/lizi/IOS/hakios-mtk/backup/boot_a_stock_real.img
size 67108864
md5 a63b2e35a7c2522cb24a6a10332b9bc8
```

硬约束：

- 不碰 Android `spmi_access` sysfs，已多次搞掉设备。
- MT6368 是 SPMI SID **5**；SID 4 是另一颗主 PMIC。
- 禁止写 `0x014b`（全局 shutdown），禁止对 `0x0240` 做 RMW。
- 新 PMIF 操作只允许 `0x1c804000 + 0x880..0x8a8`；`0x900` 以上有过总线 abort／重启。
- GPUEB MMIO `0x13c40000` 的门控条件未确认，**不加 `mali_gpueb=1`，不直接读 IRQ_RAW/WDT**。
- 不为验证日志而开启未经验证的硬件 WDT（包括用户提出的 1 秒 WDT）。
- 所有 PMIF/mailbox 轮询都必须有超时。

设备运行特点：XNU 正常返回后，屏幕常停在黄色 `starting Android`，但 root ADB 已在线，能捞日志，`boot_completed` 为空。此时 `adb reboot bootloader` 不能可靠返回 fastboot。先捞日志，再直接让用户手动进 fastboot，恢复原厂后 Android 能正常完成启动。照片中的 `_loader_return_timer`／`_psci_smp_stop_here` backtrace 不等于 panic；本轮明确 `reason 0 (end) code 0`、`no panic string`。

## 2. 全部证据根目录

```
/home/lizi/IOS/hakios-mtk/logs/headset-resume-20261114-220244/
```

主要文件：

- `baseline692.img`：真实 692 镜像副本。
- `baseline692-result.json`、`baseline692-window.txt`、`logbook-after692.bin`。
- `current-kernel-same692loader.img`：只更换内核的对照镜像。
- `currentkernel-result.json`、`currentkernel-window.txt`、`logbook-after-currentkernel.bin`。
- `loader-rebuild-result.json`：692 loader 字节相同比较。
- `nopdc/`：本轮 PDC 单变量全部源码备份、构建参数、镜像、前后 raw logbook、结果和恢复记录。
- `handoff-support/`：已把 `/tmp` 中的重要 ABI 分析、RAM 采样补丁草稿、692 loader 重建资料复制到持久目录。新账号不要依赖临时目录还存在。

`nopdc/` 最值得先读：

```
result.json          结构化结果
key-lines.txt        GPU 关键原始行
kernel-lines.txt     当前原始内核范围中的 1360 条时间戳行
window.txt           整个 raw 窗口解码（含用户态输出和潜在旧尾部）
build.json           精确编译参数与哈希
prepare.py           可复现的打包脚本；不要不经判断再次执行刷机
kernel-build.log     单文件增量编译与链接记录
kernel-build.exit    0
logbook-before.bin / logbook-after.bin
logbook-after.txt    普通脚本仅看到的 79 字节 HLOG
flash.log / reboot.log / restore-stock.log / restore-stock-reboot.log
device-final.txt     原厂 boot_completed=1
```

## 3. 已纠正的历史归因

### 3.1 真实 692 产物找到了

原始归档：

```
/home/lizi/IOS/work-xaga/codex/out/round-17-support/trial-692-boot-a.img
size 22978560
md5 643b2a27e0db0346af6bbccfc1ae4e66
sha256 1f63143ac9b4d3e74ad7dfe775cd1d1df7fd9910061b65ddfa4511c50899e399
```

解包原件曾放在 `/tmp/trial692-artifact-51floukp/`；核心结果已复制到证据根目录及 `handoff-support/rebuilt/`。

692 loader 原始大小 48208；去除 ARM64 Image 打包时修改的 size/reserved 字段，原始 loader SHA256：

```
038953c16f8f779c9d81b38da0f70c6f4401695726fbc30902ed8aa2beac8389
```

**当前 loader 源码使用真实 692 flags 重建，hash 完全相同，字节差 0。** 因此“692 后重写 4172 行”已被产物推翻。HEAD 的 3453 行 C 是 MT6768，与 xaga 不兼容。

692 原内核：15286040 字节，SHA256：

```
cdc9e38282d2258fb8fd7743a32a9b1c3b13ae6f8c30a9ed55dd9d6c0c5a49a3
```

该 hash 已在交接时对解包的 kernel.bin 再次运行 sha256sum 核实。

### 3.2 真实 692 flags 不是 board.h.bak

关键 flags 来自历史 prepare 脚本与 loader 产物：

```
-DBOARD_NO_UART -DBOARD_NO_OVL -DBOARD_SKIP_MENU -DBOARD_NO_CONNSYS
-DXNU_CORES=4 -DXNU_VERBOSE=1 -DXNU_DISPLAY=0
-DXNU_WATCH_SECONDS=0 -DXNU_RETURN_AFTER="150"
-DBOARD_MALI_POWER=1 -DBOARD_MALI_PREPOWER=0
```

共同编译选项：aarch64、freestanding、nostdlib、fno-builtin、mgeneral-regs-only、mstrict-align、`-Os`，BOARD_HEADER 指向当前 MT6895 board.h。

692 的 XNU_EXTRA_ARGS 完整内容：

```
cpus=4 buttons=2 touchscreen=3 keylog=1 key_scan=1 fbwidth=1080 fbheight=2460 fb_disp=1 mali=5 mali_cold=1 mali_preas0=0 mali_low=0 mali_trace=1 mali_witness=0 mali_ptvendor=1 mali_scclk=1 mali_scupd=1 mali_sc26=0 mali_cacheflush=0 mali_resetonly=0 mali_cohnone=0 mali_hwissues=0 mali_stackpwron=0 mali_mcuwin=0 mali_mcuseq=0 mali_doorbell=0 mali_pdcsettle=0 mali_statussem=0 mali_assorder=1 mali_trial=692
```

loader 另加 `rd=md0 -v` 与 `loader_return_after=150`。今天三个实验都采用真实 692 基线；NOPDC 只额外加 `mali_nopdc=1`。

### 3.3 之前镜像打进的是旧内核

此前顶层 `hakios-kernel` 仍是 Nov13 的旧文件（15286040 字节，SHA256 `509f61b6219219f4c614091c645366122dbdaac42b7335ea3bf31ce5e886271d`），没有 headset 字符串。用户检查的新 unstripped 有字符串，但它没有进到实际 payload。

`xnu-build.sh` 不会自动把产物复制到 `hakios-kernel`；`tools/make-kernel.sh` 会。之后还要更新 build-payload，最后才打 boot。`xaga-cycle.sh` 重建 loader 不等于重建 payload。

今天已经使用 `tools/make-kernel.sh -j8` 正确更新 `hakios-kernel`；NOPDC prepare.py 从真实 692 payload 替换内核，保留原 ramdisk/kernel.ver。不要简单运行 cycle 并假设新内核自动被使用。

## 4. 今天三次受控启动的结论

| 镜像 | 变化 | 结果 |
|---|---|---|
| 真实 692 原镜像 | 归档原件 | XNU 时间戳行至约 169.536s，正常到返回路径 |
| 今日内核 + 完全相同 692 loader/参数 | 只换内核 | XNU 至约 166.967s，用户态启动；GPU 仍未启动 MCU |
| 今日内核加入 mali_nopdc 开关 + 只增 mali_nopdc=1 参数 | 跳过默认路径 PDC 配置写入 | XNU 至 166.811735s；域状态改变，MCU 仍失败 |

前两轮各保存结果 JSON。两次前后 raw window 都有 116 个 4KB 页变化，因此不是直接把旧窗口当新日志。

第二轮镜像 MD5 `027620f6f89569a98c0f58db1910bf13`；内核 SHA256 `4cf231e51c295778014c3fa5feb959115feb47a7d2fccaaf7ebf92248e4f22b6`。

这三轮没有 `headset=1`，input 调用点未恢复，**headset 行数均为 0，不能做 124 项对拍**。

## 5. PDC 单变量：已完成，不要自动重刷

### 5.1 本代理实际新增的源码改动

`mtk/mt6895/drivers/gpu/hakios.mali610.c`：

- 新增 `gMaliNoPdc`，启动参数 `mali_nopdc`。
- 只给默认路径约 4473 行的 `pdc_enable()` 加 guard。
- 保留原有 `pdc_report()`，不添加额外硬件读。
- 跳过时打印 `pdc setup skipped (mali_nopdc=1); inherited values unchanged`。
- `mali_nopdc` 与另一路 `mali_pdcauto` 同时启用会在启动线程前报冲突返回，避免另一分支仍写 PDC。
- 默认值 0 保留旧行为。

`configs/boot-args.lst`：新增一行 `mali_nopdc k`；生成后 diff 只有该项。

未改 GPUEB、marker、watchdog、headset 调用与 PMIC 写入。`pdc_enable()` 打印 17，但内部有额外 RSV stores，不能把打印数直接当总 MMIO 写次数。

构建：`tools/make-kernel.sh -j8` 成功（exit 0），本次增量编译 GPU 与 version，链接／strip 通过。仓库已有 kext symbolset 导出错误由现有 wrapper 分类忽略；日志保留。

### 5.2 实际刷入的产物

```
镜像：logs/headset-resume-20261114-220244/nopdc/boot.img
size 22978560
md5 f5eb8440782be1cd641bdabf92f9dfaa
sha256 cebbd4174a0f2d22c6748f7dfa153b881f25d94dfab6f93c39a99be248504b29

内核 sha256 22ac36046ee687566dc1a22703c9cd38224fb5d09adc113642fb563a8ac88af3
GPU 源码 sha256 3d1511fb1e4996e2783f4033b11df680def1ed838c6af16b621f69254f18d65f
```

22:50:38 左右重启，约 22:55 捞回。随后用户手动 fastboot，本代理恢复原厂。不是又刷了一次实验。

### 5.3 关键对照（见 key-lines.txt）

| 观测 | 开 PDC 的今日内核基线 | mali_nopdc=1 |
|---|---|---|
| MFG5/6/10/12 | 0x100d | **0xc000000d** |
| GPufreq power status 0xF3C | 0x4 | **0x3ffc** |
| PDC 抽查 sc0/sc9/cg/st0/st4/st6 | 多项非零 | 全 0 |
| MCU 状态（AUTO 后） | 1 | 1 |
| 版本哨兵 | 0xff5aa5f0 | 0xff5aa5f0 |
| JOB IRQ / MMU IRQ / AS0 fault | 0 | 0 |
| GLOBAL_IF | 未升起 | 未升起 |

原始行：

```
[   34.188945]: mali: pdc setup skipped (mali_nopdc=1); inherited values unchanged
[   34.188967]: mali: after pdc: mfg5 0xc000000d mfg6 0xc000000d mfg10 0xc000000d mfg12 0xc000000d
[   34.188979]: mali: pwr status 0x1fc / 0x1fc (scpsys 0xF34/0xF38), gpufreq 0x3ffc (0xF3C); want 0x3ffc
[   44.035724]: mali: version word 0xff5aa5f0 (untouched; no firmware version published) trial 692
[   44.038616]: mali: the global interface never raised; the MCU did not boot
```

**能下的结论：早开 PDC 导致的域状态问题得到了本机单变量支持；单独跳过 PDC 没有解决固件启动。** 不能宣称它推翻／解决了全部 19 轮。

另外两点核查：`0x100d` 的 bit0 是 reset release，PWR_ON/PWR_ON2 为 bit2/3，SRAM ack 为 bit12，PWR_ACK 为 bit30/31；用户对 bit0 的解释不准。现有 300MHz PLL 的 postdiv=8，VCO=2400MHz，不踩其提出的 2000MHz 下限问题，故未改 PLL。

## 6. 日志问题：不要照旧报告开 WDT

HLOG header `reason` 与 `code` 是独立字段。之前 `0x13`、`0xfeedface` 是 **reason**，不是 code，也不能仅据 checkpoint 推断异常 PC。

现已直接观察到：

1. XNU 正常返回，屏幕显示保存约 460KB。
2. `xnu_returned()` 约 2497 行调用 `save_log_to_flash()` 写完整内核日志。
3. 随后的 `mali_power_down()` 约 6854 行执行 `probe_flash(38u)`。
4. HLOG 被覆盖为 reason `0x26`、code 0、len 79、checksum OK；内容仅两句 GPU isolation/down。
5. 完整 raw 窗口的后续块仍保有本轮内核，包含此前从未出现过的 nopdc skip 标记。

NOPDC 原始日志长度 **471129** 来自用户最后照片；结构化结果按该范围统计 1360 条时间戳。首个 body 块已被覆盖，**不能称完整无损日志**。不要把现有 header length 外的一切旧残留都当作当前有效 HLOG body；需用新标记、前后页变化与已知原长度交叉核实。

用户多次提出“MARK_RUNNING 不设置导致 rescue_log 永不可达，设一行就全解决”，需要纠正：

- xaga `MARKER_PA` 实际由 board.h 覆盖为 **0x90000000**，不是 C 默认 0x80000000。
- `XNU_WATCH_HEARTBEAT` 默认 **1**，该分支会快照、设 MARK_DONE 并 WFE，**不是用户假设的总会复位**。
- MARK_DONE 路径也有 `snapshot_take()`。
- `rescue_log()` 的 `save_log()` 写 ramoops（调用 plog_putc），不是 `save_log_to_flash()`；board 定义 **BOARD_NO_PLOG=1**，该写者为空。无条件 marker 并不足以打通 UFS 持久化。
- `host_keep` 支持确实存在：find_syms、save_host_keep，normal/rescue/watcher 多处调用，用户“全树零命中”不成立。
- 已离线解析**实际打包 NOPDC 内核**的 LC_SYMTAB，`_loader_log`、`_loader_log_len`、`_loader_log_ring_at`、`_loader_log_wrapped` 四符号均存在，不需 WDT 刷机来证明。

本轮未修日志逻辑，保持单变量。后续若修，优先独立修复最终关电探针对已保存内核 HLOG 的覆盖；不要混进 GPU 电源实验。

## 7. GPUEB：已有源码与 ABI，但没有做硬件实验

已读完整 fork 实现，不只是 API：

```
MT6895-Mainline/linux
branch 7.2-mt6895-xiaomi-xaga
pinned commit 25fd36e67958d96086b8542340477948ab40c2a2
drivers/gpu/mediatek/gpueb/gpueb_power.c
drivers/gpu/mediatek/gpueb/include/gpufreq_ipi.h
```

原始链接：

- https://raw.githubusercontent.com/MT6895-Mainline/linux/25fd36e67958d96086b8542340477948ab40c2a2/drivers/gpu/mediatek/gpueb/gpueb_power.c
- https://raw.githubusercontent.com/MT6895-Mainline/linux/25fd36e67958d96086b8542340477948ab40c2a2/drivers/gpu/mediatek/gpueb/include/gpufreq_ipi.h
- https://raw.githubusercontent.com/MT6895-Mainline/linux/25fd36e67958d96086b8542340477948ab40c2a2/drivers/clk/mediatek/clk-mt6895-mfgcfg.c

结论边界：

- `available()` 只找 DT node，不证明固件 alive。
- power 路径 CMD_INIT_SHARED_MEM → CMD_POWER_CONTROL，使用 **IPI_SEND_POLLING**；因此没有 GIC548 处理并不能单独证明失败原因。
- **IPI 消息为 32 字节／8 个 u32，不是用户转述的 4 个 u32。** 已用 vendor 头与 AArch64 静态断言核对。
- 不要顺手 CMD_COMMIT OPP；该独立动作未授权进实验。
- 真机 DT 共享区为 0x7c600000 / 2MiB；GPUFREQ 0x1000，status 前 0x400，debug 后 0xc00；log 从 0x7c601000 开始 0x180000。
- vendor shared_status 大小 344 字节；关键字段偏移：power_control +0x60，dvfs_state +0x64，shader_present +0x68，signed power_count +0x6c。
- **没有 heartbeat／magic／timestamp。** 非零共享状态可能是旧数据；静止／全零不能直接判固件死，power_count 是当前引用计数，不是“历史上是否上过电”。
- 本次原厂 Android `/proc/gpufreqv2/gpufreq_status` 显示 ShaderPresent=0x00130013、PowerCount=0，系统正常；直接反例说明 count=0 不能证明“未上过电”。该记录在证据目录的 stock-gpufreq-status.txt。
- 历史全部 gpueb_watch 都是 not mapped，没有成功的 XNU WDT_KICK 活性观测。
- GPUEB 时钟门位置仍未知；不触碰 0x13c40000。

已持久化的**未应用** RAM 采样补丁：

```
logs/headset-resume-20261114-220244/handoff-support/
  gpueb-shared-late-sample-draft-puq8gg06/gpueb-shared-late-sample.patch
  gpueb-abi-2dn1xxy2/REPORT.md
```

该草稿用独立 `mali_gpueb_shared`，仅映射普通保留 DRAM 首 4KB，在既有 gpueb_watch 两个晚期调用点读 32 words 与四个字段，不加新线程/延迟/电源写、不访问 GPUEB MMIO。私有副本 syntax-only 与 git apply --check 已通过。**没有应用、没有上机。** 不要用更早另一个 `/tmp/gpueb-shared-probe-draft-*` 草稿，那版会提前 return，改变 GPU 主流程。

## 8. 耳机：最新权威源码已找到，局部核对结果

用户提供，本代理已实际读取：

```
/home/lizi/IOS/work-xaga/research/raw/xaga-ecosystem/mt6368-accdet.c
/home/lizi/IOS/work-xaga/research/raw/xaga-ecosystem/mt6368.h
/home/lizi/IOS/work-xaga/research/raw/xaga-ecosystem/mt6368-accdet.h
/home/lizi/IOS/work-xaga/research/raw/xaga-ecosystem/mt6368-registers.h
```

原 sparse repo `/home/lizi/IOS/work-xaga/k-xaga`，HEAD：`0a8d13c1a180fed82803f56337a0b065ff704fb4`。后续查源码先 `git cat-file -p HEAD:<path>`，不要因未 checkout 就反汇编。

核对到的具体语义：

- `0x26a9 bit0 = ACCDET_IRQ`，不是插入状态，也不是 EINT pending。当前 headset.c TODO 里仍误写 EINT pending，需更正文字。
- `0x26aa bit0` 清 ACCDET IRQ；`0x26b6[7:6]` 是 AB，`0x26b7[2:0]` 为状态机状态。
- `check_cable_type()` 约 2002 行是**有前态、有 eint_sync_flag 门控**的状态机。PLUG_OUT 时 AB00 → 三段耳机，AB01 → 带麦耳机；但 MIC_BIAS 时 AB00 是**按键按下**，不能写成无条件 `AB==0 => 三段耳机`。
- 5000Ω 阈值函数 `mt6368_accdet_read_audio_res(unsigned res_value)` 约 845 行只消费调用者已经算好的阻抗，**并没有提供 ADC→Ω 计算**。还要找 codec 上游调用者才能补齐。
- mic_vol==8 分支约 3023 行设置 low-power 位，并 OR `3 << 4`，源码注释 2.8V；头文件 HVEN mask 却写 1bit，实施前以实际相邻位定义与当前平台配置核对，不能只按单个宏 mask 截断。
- CONFIG_TARGET_PRODUCT_XAGA 确实定义 USB_3_5_UNSUPPORT；259..333 行 Type-C notifier 单独上报 `g_usb_3_5_jack`，不应替代真实 accdet。

现有驱动：

```
mtk/mt6895/drivers/audio/hakios.mt6895_headset.c
```

mode1 只读 0x2688..0x26d3（76）与 0x2508..0x2537（48），SID5，严格格式 `headset: 0x%04x = 0x%02x`。mode2 TODO。构建配置／声明／stub 已有；**drivers/input/hakios.input.c 中调用仍移除，内核只含函数不执行。**

并发要点：keys.c 使用同一个 PMIF 通道且无共享锁。不要简单再开一个独立 headset worker，导致 keys/headset 命令交错。候选是把一次只读 dump 放在 keys worker 首次成功 PMIF 读后、进入主循环前；若做持续 mode2，必须把双方事务串行化（同 worker 或可靠共用锁）。本代理只分析，没有改调用点。

VLDCLR 是事务 W1C ACK；不要套 codec 配置寄存器的“写完读回值必须相等”。验证效果应 bounded poll STA 回 IDLE。

物理耳机是否在场曾异步询问，**没有收到答案**；不得声称插拔／听音已验证。

既有耳机文档与对拍工具：

```
/home/lizi/IOS/repo/hakios-mt6895/docs/headset-wip/
  ACCDET-DECODE.md
  ACCDET-STATEMACHINE.md
  PLAYBACK-SEQUENCE.md
  accdet-compare.py
  accdet-baseline.json
```

init 19/19 是 stock 对拍；SPMI PMIC 写事务没有在 XNU 验证。SGEN 寄存器序列未对拍。**SGEN 能响只证明 tone path，不等于解码播放 PCM**；实际播放还需核查 AFE/ADDA CH3/CH4、MTKAIF/DMA 与音频输出接口。

用户新提出 expdb/log_store、Ken-u/Mali MCU shared-region 两坑、ADDA UCM：**本代理尚未核实／未实施**。尤其对方 Linux 移植的 log_store/XAGR/cinit 脚本不能直接当作我们现有 XNU 写过日志的证据。fastboot 通常不能任意 fetch expdb；如后续授权只读取证，当前原厂 root ADB 可用，先核对 by-name 节点与大小，别盲读整块或写分区。

## 9. 接手建议与停止点

1. 先读 `nopdc/result.json` 与 `key-lines.txt`，把“域 ACK 已恢复但 MCU 仍失败”的结果给主控，避免重复本轮。
2. 若继续 GPU，RAM 状态只读采样比未确认门控的 GPUEB MMIO 更适合下一步，但只能作状态快照，不能把非零／power_count 解读成 liveness。是否做下一轮由当前主控决定。
3. 若回耳机主线，先基于本地厂商源码设计 PMIF 访问串行化，恢复 mode1 并完成 124 项对拍，再做 mode2 写初始化和状态机；不要把 GPU 旋钮、日志修复和耳机同时放进一次刷机。
4. 若独立修日志，先修“正常返回完整 HLOG 被后续 probe 覆盖”的确定问题，验证保存路径与失败救援路径分别是否有 UFS 写者。不要先开不确定 WDT。
5. 不需重跑三次基线，不需重查 692 loader 来源；产物和 exact-match 证据已在持久目录。

当前没有活动子进程。两个子代理 loader_source_compat / trial692_artifact 已完成，未碰硬件。没有 commit/push，没有改账号，也没有给外部人员发消息。

本文件由主控转交新 Codex 账号；原代理到此停止。
