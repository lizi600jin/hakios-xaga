# OBSERVE-02 审核结果与供电实现边界

2026-11-16，Codex。设备操作由 DSH 执行；用户当前远离设备，后续保持主机侧工作。

## 三个问题的结论

1. **DC Trim 与 HP_ANA_TRIM 分开处理。** A/B1/B2/C 四次采样中，DC Trim 均 Off，
   HP_ANA_TRIM 为 Off/On/On/Off。DC Trim 没有普通播放 route，codec init 会先置
   trim-buffer 位，再由初始 DAPM 同步触发关闭回调；回调可调度异步校准线程。
   这条启动触发路径与本次稳定播放时 DC Trim 为 Off 相容。
   当前采样不能证明启动校准是否运行、结果是什么，也不能排除采样间瞬态。
   不把 DC Trim 作为每次播放都要置 On 的 supply，不据此强制重新校准。
   真正应用已知 trim 的 HP_ANA_TRIM 仍属于所选 HP 播放 profile。

2. **AIF_RX inactive 不表示绕开 codec AIF1。** 固定版本 `soc-dapm.c` 的
   `soc_dapm_dai_stream_event()` 通过 `snd_soc_dai_get_widget()` 更新 DAI widget。
   后者在 `include/sound/soc-dai.h:406..412` 内联返回 playback_widget/capture_widget；
   不需要另有 `.c` 函数定义。codec 自动 DAI widget 名为 `AIF1 Playback`。
   普通 aif widget `AIF_RX` 与它是两个对象；调试打印只是读取当前对象的 active 位，
   不会沿边继承该字段。B1 原文已经列出 `AIF1 Playback → AIF_RX` 入边，
   DAC In Mux 的已连通分支为 Normal Path，HPL/HPR 为 Audio Playback。
   CPU ADDA 经 MTKAIF 连接 codec AIF1 是同一链路的不同层级，不是两个互斥候选。
   我上一 CMD 要求普通 AIF_RX 自身 active 的判据不准确，由我撤回。

   实际 machine 模块也已直接核对：`adda_cpus` 的 dai_name=ADDA，
   `adda_codecs` 的 name=mt6368-sound、dai_name=mt6368-snd-codec-aif1；
   init 的 `.text+0x6b4..0x6c4` 以参数 2 调 `mt6368_set_mtkaif_protocol`，
   与固定源码的 MTKAIF_PROTOCOL_2_CLK_P2 一致。
   组件记录结果在 `machine-adda-identity.json`，不是“绕过 AIF1”的证据。

3. **29 项同时激活是运行中软件 profile 的证据，不是逐项电气必要性证明。**
   与模块依赖图共同支持：先复现完整所选播放路径、回收与已定义字段，
   不凭三联寄存器值认定供电完备，也不逐 bit 关钟寻找最小条件。
   将来如需裁剪，须先有可重复的有效输入和物理输出正对照。
   HP_MUTE/HP_PULL_DOWN 的 widget 为 On 也不等于对应模拟信号持续处于静音/下拉；
   这些 supply 承载的是事件回调，最终寄存器动作应以回调轨迹为准。

## 播放证据重算

`observe02-review.json` 已机械验证 30 个目标名称和四次状态：29 个
Off/On/On/Off，DC Trim 为 Off/Off/Off/Off。

同一 AudioOut_15 指针 `0xb400007a20fcf4d0`、I/O 21：

| 项 | B2 | C |
|---|---|---|
| Standby | no | yes |
| 路由 | 0x8 WIRED_HEADPHONE | 0x8 WIRED_HEADPHONE |
| Frames written | 3870720 | 5943296 |
| Total writes | 1890 | 2902 |
| Last write ms | 55 | 23405 |

差值为 **2072576 帧、1012 次写**；`Timestamp stats n` 是有效时间戳统计计数，
不是音频帧数。B1 完整 AudioFlinger dump 未归档，不把 n=1496 当独立帧证据。
这些数据证明框架线程有持续写入及耳机路由，不证明模拟输出、时钟波形或 DAC 收到正确 PCM。
仅检查的 pcm0p/1p/2p closed 也不能代表所有 PCM 端点。

实际 host 采样时间为 A 13:37:41、B1 13:37:55、B2 13:38:07、C 13:39:06（+08:00），
不能继续叙述为“B1/B2 仅隔 2 秒”或“C 一定在结束后 15 秒内”。

ASoC 固定源码：
https://github.com/XagaForge/android_kernel_xiaomi_mt6895/blob/0a8d13c1a180fed82803f56337a0b065ff704fb4/sound/soc/soc-dapm.c

时间戳计数含义的 AOSP 对照：
https://android.googlesource.com/platform/prebuilts/vndk/v30/+/68f677b4f959ebacdc4a4bc0d16d302b6c9de4d9/x86_64/include/system/media/audio_utils/include/audio_utils/TimestampVerifier.h

## 已交付的供电回调参考

`trace-supplies.py` 将实际 MT6368 模块 callback 指令放入 Unicorn 执行，
外部 regmap/delay/logging/regulator/DCXO 均有明确模型边界。
调度按固定 soc-dapm 源码的类型顺序、subseq、reg 及同寄存器合并写建模。
结果保存在 `supply-callback-trace.json` 与 `SUPPLY-CALLBACK-TRACE.md`。

| profile | widget 闭包 | 上电参考操作 | 下电参考操作 |
|---|---|---|---|
| normal_aif1_hp | 39 | 165 | 142 |
| internal_sgen_hp | 44 | 173 | 147 |

操作数包含 callback 的读/写/update/delay 和外部调用边界；不是硬件总线事务数。
共同假设为普通 HP mux=2、hifi=0、48 kHz、gain=31（静音参考）。
trim 使用 **人工哨兵 3/7、fine 1/2**，只验证动态参数流向，绝不能用于设备。
对 gain 寄存器建模为已观测到的低五位读回，未把全部 byte 当可持久保存的 RAM。

**同寄存器合并是必要的实现细节。** SGEN DL Enable 与 SGEN MUTE 共享 23c8，
其 PRE_PMU 回调会先将该 byte 清零；DAPM 然后一次性以 mask=c0、value=80 写回，
保留 DAC_EN=1、MUTE=0。若先写 enable，再单独调用会清零整 byte 的回调，
最后只清 mute 位，就会漏掉 enable。单独照抄宏列表会造成可复现的软件遗漏。

**源选择不是 callback 的替代品。** `DAC In Mux` 的控制是 238e bit0，
Normal Path=0、Sgen=1。轨迹将选择状态列作前置条件，没有执行 control put；
以后驱动必须对它单独快照、选择、恢复，不能漏掉后宣称 SGEN 已生效。

参考尚未覆盖真实驱动错误注入、真实稳压器动作、MT6685 总线写、
GPIO SET/CLR alias 副作用或硅片所有可写/保留位行为；不构成新 hardware PASS。
同排序键完全相同的 widget 使用声明顺序作为模型 tie-break，实际 dirty-list 顺序未取证。

## 下一实现阶段的约束

- 使用完整限定 profile；先复用已观测有效的 vendor 动作，再做最小化。
- 真实 trim 未取到时不得使用哨兵或宣称校准完成；静音参考与可听测试分开。
- 供电、HP ramp、源选择、增益、关闭、快照恢复均有独立阶段与失败状态。
- 所有 PMIF 仍由现有 worker 串行执行；正常返回在 CPU 停止前完成关闭。
- 新增 MT6685 写路径必须限到指定地址并用 mask RMW 保持其他位，不泛开整个 SID。
- 字段读回使用定义 mask，不能再次把 reserved bit 与合法 gain 位混验。
- 正确 off 与原值 restore 是两个判据；恢复初值不替代先确认安全关闭。
- 主机模型对写失败、左右 gain 有效字段错误、半途回收及 stop 都需要有针对性的验证。
- 当前没有交付或刷写供电/SGEN 生产补丁；设备保持原厂，用户回到设备旁再安排独立硬件 CMD。

## 参考构建与工具修复

当前冻结输入经新 BUILD 构建得到 39709224…；旧 709 BUILD 已恢复，仍为 c17cf881…。
`compare-reference-kernels.py` 解析 Mach-O load command/section 并归因全部 542838 个差异字节：
LC_UUID 16、版本构建时间字符串 18、__LINKINFO.__symbolsets XML 数组顺序 542804；
未归因=0。9 个 symbolset 的 5638 条完整记录按保留重复项的 multiset 比较相同。
其余字节相同。不推断 UUID 生成算法，不追认缺失的 709 历史源记录。

生产 `tools/make-kernel.sh` 已修复：KERNEL 随 XNU_SRC 选择；隔离树不自动同步脏驱动；
--judge 无构建/复制；named 复制失败返回失败。原中文 symbolset 过滤保留。
`tools/test-make-kernel.py` 的 7 项假树回归测试与 bash -n 已由双方验证。
未再次编译真实内核，未改变旧 kernel、全局 hakios-kernel 或 709 镜像。

**共享出口语义保留。** 普通 wrapper 构建成功仍更新 ./hakios-kernel；
这是当前构建选定产物的共享出口，不是“默认树专用副本”。需要保留这个全局文件的
参考构建继续用已核验的 xnu-build.sh + 显式 KERNEL --judge 流程，不以事后恢复掩盖来源。
此工具支线已验证完成，本阶段不再扩大改动。
