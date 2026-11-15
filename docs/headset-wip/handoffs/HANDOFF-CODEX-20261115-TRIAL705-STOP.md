# Codex 接手交接：MT6895 xaga，headset=3，trial 705 后主动停手

日期：2026-11-15。作者：本轮 Codex。**这是实际写出的交接文件，不是预告路径。**

本账号 orca:2a3f54a2 剩余约 13%，用户要求立即停止深入调查，只回答 260b 的问题并写交接。备用 system 账号将接手。**交接时没有修复 705 暴露的代码问题，没有新构建，没有操作设备。** 不要把本文件中的修复建议误认为已落实。

## 1. 项目、人员、权限、当前优先级

项目是给 Xiaomi xaga / MT6895 移植 XNU 的 hakios 工程。本轮目标为 MT6368 耳机插入识别后的 DAC/耳放寄存器探针；播放音频尚未实现。GPU 是另一条线，当前不优先。

- 实际生产代码仓库：`/home/lizi/IOS/hakios-mtk`，本轮开始 HEAD 为 `6a51ef2`。**永不 push**。代码归 Codex，不能 stash/reset/clean 掉他人或之前的 WIP。
- DSH 文档仓库：`/home/lizi/IOS/repo/hakios-mt6895`。`docs/**` 只有 DSH 能改，我们只读。
- Orca 当前工作目录：`/home/lizi/orca/hakios-xaga`，不是上述生产仓库。
- 我们的报告、反汇编、构建证据放在生产仓库忽略的 `logs/` 下。
- DSH 是主控，负责信息搜集、日志与下载物核验、实验打包、设备操作/恢复、文档和仓库。
- 新分工：Codex 独占二进制反汇编、寄存器位域/时序、硬件块归属、生产驱动、架构决策。DSH 不再先提取寄存器表让 Codex 复核，避免把错误前提传入实现。规则见 DSH 的 `docs/headset-wip/TEAM-SPLIT.md`，提交 `06a5edd`。
- DSH 仍可交叉验证我们引用的源码是否真存在、日志是否真有那些行、构建是否通过、产物哈希是否一致；不要要求他负责硅片语义复核。
- 用户授权过刷机，但设备操作仍由 DSH 协调。本次最新明确指令是 **不改代码、不构建、不碰设备，交接后停手**。新账号接手时先遵循新的用户指令，不能把旧授权当作现在可立即刷机。

设备在 705 后已恢复原厂 Android 17，DSH 报告完好。705 使用启动前插入的三段无麦耳机，全程没有拔出。

可以直接给 DSH 发终端消息，用户已明确授权。当前可用 CLI：

```
/home/lizi/.config/orca/linux-orca-cli-shim/orca terminal send --terminal term_d7250c3e-8393-4fa6-8d42-ba82c8a73fae --text '内容' --enter --json
```

旧 `term_67e563b9-0361-4d66-ab3b-c0e072ae63b6` 已失效，别重复用。当前我们的终端是 `term_418175b4-9fe1-4dd6-b045-a7ba63cef8ad`。若 handle 过期，按 orca-cli skill 查询新 handle。CLI 返回 accepted 不代表提供方能确认投递；不要盲目重复发送。

本轮已读并使用 `/home/lizi/.agents/skills/orca-cli/SKILL.md`、`reverse-engineer-anything/SKILL.md`。REA doctor 无可用 Codex MCP 注册，本轮实际使用 objdump + Unicorn 离线模拟，不存在可引用的 REA Evidence ID。

## 2. 接手应先读哪些文件

完整 headset=3 交付目录：

`/home/lizi/IOS/hakios-mtk/logs/headset3-review-20261115/`

- `FINDINGS.md`：上一轮实现、语义、安全边界、判据和验证。**这是 705 之前的结论；涉及 260b/c 全字节校验/9f 读回的部分被 705 暴露为错误，须结合本交接阅读。**
- `HP-SEQUENCE.md`：普通 HP 完整事件上/下电顺序、helper 循环、所有延时、逐项展开调用轨迹。不是整条 DAPM 图。
- `mt_hp_event.disassembly.txt`、`hp_helpers.disassembly.txt`。
- `trace-vendor.py`、`vendor-trace.json`：执行实际模块指令的离线模拟，regmap 是模型。
- `vendor-mt6368.c`、`vendor-mtk-spmi-pmic.c`、`provenance.json`。
- `headset3-clean700.patch`：705 使用的功能补丁来源；只含本轮四个生产文件、测试和 fixture。
- `headset3-worktree.patch`：相对本轮开始前工作树的增量，不能再次应用到已经改过的当前树。
- `clean700-files/`、`patch-manifest.json`、`compile-results.json`、`*.compile-command.json`、`*.compile.log`、`symbols.log`、`host-test.log`、`clean700-host-test.log`、`SHA256SUMS`。

原始模块：

`/home/lizi/IOS/repo/hakios-mt6895/docs/headset-wip/ko/snd-soc-mt6368.ko`

SHA256：`4ce5cab06d3d29aa3e2ccfbd8044d68d691b27f3b4e336332bfb41ee6e479475`。

位域头：`docs/headset-wip/raw/mt6368.h`；MFD 寄存器头另有 `mt6368-registers.h`，不要混淆不同头文件的内容。

交叉核对用固定源码提交：

https://github.com/XagaForge/android_kernel_xiaomi_mt6895/blob/0a8d13c1a180fed82803f56337a0b065ff704fb4/sound/soc/codecs/mt6368.c

本地副本 SHA256：`84ebc78fead84adbea5ff7208bde3d130123637e5bc6a46069712043fa846ddc`。

MFD：https://github.com/XagaForge/android_kernel_xiaomi_mt6895/blob/0a8d13c1a180fed82803f56337a0b065ff704fb4/drivers/mfd/mtk-spmi-pmic.c

本地副本 SHA256：`2c4efc39c095746220084aa41850b8a7a6751d6c353fe85be380359327b9e535`；`spmi_regmap_config` 明确 reg_bits=16、val_bits=8。

## 3. 为什么不能照抄 DSH 的 45 项表

`mt_hp_event` 在模块 `.text+0x8be0`，size=1912，结束 0x9358。函数内静态调用点是 regmap_write 46、regmap_update_bits_base 20、usleep_range 4，但它们横跨 PRE_PMU/PRE_PMD 和三个互斥 mux 分支，不是一次执行的 45 步。

ARM64 参数：regmap_update_bits_base 的 x1=reg、w2=mask、w3=value。DSH 第一版把 mask/value 反了。不能靠相邻 mov w1/mov w2/bl 正则提取流程，必须追踪参数和 CFG。

普通耳机是 HP_MUX_HP=2；HPSPK=1；阻抗检测=4。普通 HP 的 PRE_PMU 在计数从 0 加到 1 时才执行启用，第二个声道的引用增加不会重复上电；PRE_PMD 在计数归零时才关闭。我们单个 worker 用自己的 ownership/on 状态对应一次插入。

原 45 项表的分支归属：

- 1–9 是 HPSPK 的 LO 设置，普通 HP 跳过，还漏了 2597=0。
- 10 是共有 259c=87，前面遗漏依 hifi_mode 决定的三个 bias update。
- 11 是 >=96 kHz 分支；本轮选择 <96 kHz，所以应直接 W(2591,00)。
- 12–17 是阻抗检测启用；18–20 是阻抗检测关闭。
- 21–40 是普通 HP 主体的一部分，遗漏零写、helper、循环、gain ramp 和延时。
- 41–43 是 HPSPK 结尾；44–45 才是普通 HP 的左右 mux。

普通 HP 的 2589 是 **30→32→3a**。绝不是先执行 value=01 再执行 value=02；二者分支互斥。低两位 HPL mux=2，bits3:2 HPR mux=2。bit0 不是独立使能，期望为 0。

本轮选 `hifi=0`（vendor 默认）、48k 用于选择 <96k 的模拟参数、target gain=31（-40 dB），没有创建实际 PCM stream。hifi=1 有不同偏置和输入对电流 ramp，不能把本实现称为所有模式通用。

helper 地址：hp_pull_down 0xc26c；hp_in_pair_current 0xc44c；headset_volume_ramp 0xc554；mtk_hp_disable 0xc660，size852；mt_hp_mute_event 0x93a8。

四个内联 delay：0x913c，main stage 8 轮，每轮左右更新后 600..650 us；0x917c，feedback 16 轮，每轮 600..650 us；0x91e4，在 258a=03 后/25a2 bit0 开前，100..120 us；0x9258，在259b=f2后/mux选择前，100..120 us。pull_down 每步100..150 us；gain 30↔31 各一轮600..650 us。输入对电流 helper 在 hifi=0 下无操作。

## 4. 普通 HP 事件序列摘要

以下是 byte regmap 的寄存器序列；U(reg,mask,val)，W(reg,val)。完整展开以 HP-SEQUENCE.md 为准。

准备：258d mask70 的 pull-down 0..7，每轮100 us；HP_MUTE PRE_PMU 给 260b/c 写低字节1e。

开启：

1. U(259f,07,01)，U(25a1,c0,00)，U(25a1,03,00)。
2. W259c=87，2591=00，258c=33，258d=f1，258b=00，258a=0c→3c。
3. W259a=00，259b=0c，2589=30，2588=c0→f0，258a=fc。
4. hifi=0 的 input-current helper 无操作；W259b=0e→02，258a=ff。
5. 258b 左右 main-stage 0..7，每轮600 us；259b maskf0 feedback 0..15，每轮600 us。
6. W258a=cf，258b=77；HP gain 1e→1f，600 us。
7. W258a=c3→03，100 us；U25a2 bit0=1。
8. W2589=30，2588=ff，259a=00，259b=f2，100 us。
9. U2589 mask03=02、mask0c=08；258d pull-down 7..0，每轮100 us。

终值：2588=ff、258a=03、2589=3a。258a 是主/辅输出级与反馈/短接控制，不是“DAC 数字就绪”。

关闭：pull-down；断开2589低四位；清259a bit0、2588低四位、25a2 bit0；W258b77、258ac3→cf；gain31→30；258aff；feedback15..0、main7..0；清258a low2；259a01、259b0e→0c；hifi0输入电流无操作；清258a c0、2588 30、2588 c0；259a01、259b02；清258a30和0c。

此后当前实现还复制了 HP_MUTE POST_PMD 的 260b/c 低字节9f写，**这正是705触发全字节校验失败的位置**。正常物理关闭检查应独立核验2588=0、258a=0、2589低四位0、25a2 bit0=0，再恢复初值。原 DSH“下电不改2588/2589”的判断错误。

## 5. 三条共享寄存器的语义与 mask 推导

### 259f，AUDDEC_ANA_CON23，mask07

vendor 的 DRBIAS_HP 低三位是 HP driver bias 电流选择。hifi=0 选码1（源码注释5uA），hifi=1选码2（6uA）。更高位有 HS bias，必须保留。故普通低功耗 profile 只拥有07，目标 `(baseline & f8) | 01`。

705 baseline=22，因此期望21。注意 **705没有打印最终全字节21**；不能写成实测已读到21。

### 25a1，AUDDEC_ANA_CON25，maskc3

源码通过 IBIAS_ZCD maskc0 和 IBIAS_HP mask03 分别更新，hifi=0 两者都写0；bits2:5 属于 HS/LO bias，不能顺便清除。联合 ownership mask 为 c3，目标 `baseline & 3c`。

705 baseline=55，数学期望14。当前代码的每次 update 回读只核验自己本次 mask，不记录全字节，也不单独证明未拥有位在硬件上一直保持。**705日志没有最终25a1=14的全字节观测，应判该项证据不足；不能虚构通过。**

### 25a2，AUDDEC_ANA_CON26，mask01

bit0 为 RG_RSTB_DECODER_VA32，vendor 的 decoder clock helper 控制它。bit4 为 AUDGLB_PWRDN，bits5/6 为 VOW 相关共享策略，bit1有96k选择等。只拥有01，不能把整个byte写01。

705 baseline=10，上电回读11，**这是全字节实测**，证明该时点只bit0改变。也正说明 AUDGLB_PWRDN bit4仍为1：不能把 ff/03/3a 三联值称作 DAC 已经真正上电。

25a0 low3是LO bias，bit4是audio IBIAS power-down；普通HP事件不写25a0。25a3是LDO/remote/NV enable，当前探针只读。芯片内部寄存器也能控制模拟电源，不能泛称“不碰电源门控”。2589 bit4/5是HP短路保护disable，复制vendor值30并不意味着全程保护开启。

## 6. 705 实测、核实范围和 260b 的当前结论

日志：`/home/lizi/IOS/repo/hakios-mt6895/docs/headset-wip/raw/trial705-logbook/trial705-full.log`。

**我已在本轮实际计算文件 SHA256**：`7bf7ddadaf1febb784c986ae8e838919af5053d11129acb561e8e3796417b3de`，与DSH相符。已逐行读取有关headset日志。header len521981、sum02895ee1、reason0是DSH报告，本轮未再次解析原始header。

DSH报告构建：700干净树（695驱动+msgbuf1MiB）加本轮四个生产文件；GPUEB IPI零命中。kernel短哈希4b1a55da，image MD5 97dee2876770fe826704bad337335502。本轮没有自行复核镜像字节，不要把短哈希当完整SHA。

实际705日志行号与内容：

- 3109：20.455640，EINT0 plug in；3112：20.584352，三段无麦分类。DSH最初摘要中的20.466/20.595近似701时刻，引用705请使用原始日志的这两个时刻。
- 3113：20.585276，chip68、VAUD18=01、CKPDN=00、CLKSQ=00、IBIAS=12、LDO/NV=00、NCP=00、GAIN_SEL=00、LO=00、HS=00。
- 3114–3128：15条baseline；2588/89/8a/8b/8c/8d/2591/9a/9b/9c=00；259f22、25a155、25a210、260b/c1f。
- 3130：20.618500，2588ff、258a03、25893a、25a211、gain1f/1f，verified。
- 3405、3407：26.868104关micbias；26.931531 AB3、EINT0=0、sync1，正常分类保持。
- 4708、4709：169.742662与169.768233，260b wanted9f got1f，重复同一失败。
- 4710、4711：HP shutdown incomplete/analog off NOT confirmed；ACCDET restored36 bytes incomplete。

**给DSH的两句结论已经发送在对话中：**

> 下电应写0x260b的低五位增益；本次首先暴露的是我错误要求bit7也读回为1，没有证据支持归因于CLKSQ/NCP未开。
> 头文件只定义RG_AUDHPLGAIN_MASK=0x1f，vendor常量来自(31<<7)|31，而实测0x9f→0x1f的低五位完全正确。

依据已读取、无需先重新猜：`raw/mt6368.h:3787`定义ZCD_CON3的RG_AUDHPLGAIN只有低五位；CON4右声道同样1f；:5932/5933定义 `DL_GAIN_N_22DB_REG = (gain<<7)|gain`、`DL_GAIN_N_40DB_REG = (gain<<7)|gain`。vendor `mt_hp_mute_event` 确实在POST_PMD写这个常量；MFD是8-bit值，所以总线发9f，但vendor并没有要求整个byte持久等于9f。

**边界：** 现有证据能判我们不应把bit7纳入“定义的gain字段必须读回”判据；不能凭头文件缺少bit7就断言硅片具体是RES0、RAZ/WI、自清零还是另有内部行为。CLKSQ/NCP关闭确实存在，但没有证据证明它们导致这个差异。禁止据此开放时钟或扩大供电改动来“修复bit7”。也不能说整个260b寄存器不可写：本轮启用/关闭途中1e与1f低五位的逐步核验能通过。

705不是完整PASS，不能事后改判。当前代码将260b全字节核验放在hp_verify(false)前且以&&短路，因此失败时不会执行最终off三联读回，也不会继续右声道最后那次写/快照恢复。前面的物理关闭寄存器操作已经走过，并不等于“已证实模拟输出没关”；只能说没有完成最终关闭确认/恢复。这是需要修正的诊断和回收结构问题。

CLKSQ=0、NCP=0、LDO/NV=0、IBIAS12的bit4=1、AUDGLB10的bit4=1说明当前只是寄存器探针，供电配置仍未建立。CKPDN=0表示对应gate没有标为power-down，不能证明上游clock活着。这与bit7校验错误是两件事。

## 7. 已实现代码、测试与已知模型漏洞

生产改动四个文件：

1. `mtk/mt6895/drivers/audio/hakios.mt6895_headset.c`
2. `drivers/core/hakios.stubs.c`
3. `xnu/files/bsd/sys/hakios.audio.h`
4. `xnu/files/osfmk/arm64/loader_return.c`

测试：`mtk/tools/test-headset.py`；新增固定基准 `mtk/tools/fixtures/mt6368-hp-normal.json`。

mode3复用ACCDET，C后等待B，不因六秒后AB3失去分类。HP前置会读ID/VAUD/clocks/LO/HS/gain selector，拒绝已有输出或NLE gain。15个HP字节快照与ACCDET 48槽分开；所有写通过白名单mask，失败/拔出关闭后恢复。所有PMIF依旧由keys worker执行，不新增总线worker。

正常timer-return/host-stop在停CPU前调用stop，原子请求让keys worker关闭，最多等约2秒。若请求先于init，禁止worker开始PMIF。CPU停后quiesce仅设置软件标志，绝不在那个阶段补做PMIF ACK/锁/回滚。panic/watchdog不能保证关闭，不能给完整PASS。

当前具体缺陷位置：`hp_saved`把260b/c的ownership设ff；`hp_gain`使用legacy mask9f；`hp_write`按整byte比较；`hp_disable_sequence`末尾 `hp_write(260b,9f) && hp_write(260c,9f) && hp_verify(false)`。705后这些代码**尚未修改**。

上一轮验证是真做过的：工作树与clean700版本分别ASan/UBSan通过；实际模块指令轨迹对照通过；五个Mach-O ARM64对象（headset、clean700 headset、loader-return、两种stubs）编译rc0；stop符号定义/引用检查通过。没有由Codex完成整内核链接，705整内核/镜像由DSH构建。

**但模型有关键盲区：** Unicorn的regmap与host PMIF模型把这些byte都当可原样存储的RAM，允许260b bit7写1后读1。于是“忠实复现vendor发出的写值”被我错误扩展成“硅片应该原样读回全部位”。指令轨迹对照不能证明寄存器实现/保留位的读回语义。下一版测试必须建模定义字段的可写性，并能重现705的9f→1f，否则只是再次掩盖bug。

clean700补丁SHA256：`57d0cfbdfe1a1445a4cb1db1dc08682fb32eca690b592e459275dcbbc3f44879`。对应四个生产基线文件与700 kernel-src逐字节相同。去除了开始本轮前已有的headset入口诊断差异；当前工作树保留那些差异。

## 8. 尚未完成，恢复工作后的建议顺序

当前停手令有效时不要执行以下事项；这是给用户恢复工作后的接手计划。

1. 修复260b/c字段所有权和核验：以已定义gain mask1f为基础决定最小写/读回语义，不能全局忽略PMIF失败，也不能把任意不匹配算“预期”。保留vendor bus-write常量还是改成只写定义字段，应明确说明选择与测试基准的区别；不要为了维持旧fixture而继续错误的全byte保持断言。
2. 让真正的DAC/HP off形状核验不会被末尾gain保留位差异遮住；仍要求左右gain有效字段正确且无事务错误。关闭失败与恢复失败应分清楚，避免笼统日志丢失已经完成的阶段证据。
3. 模型增加260b/c高位不存储的行为，先用现有代码重现705失败，再验证修复；覆盖左右声道、真实低五位写失败仍必须报错、关闭回收、失败重试。不能只改expected让测试绿。
4. 在on和恢复阶段打印并核验259f/25a1/25a2全字节，直接比较未拥有共享位与baseline。705缺少25a1/259f最终实测，本轮不能认定共享位全部通过。期望分别21、14、11。
5. 独立最小补丁基于705/700隔离树，不加入任何GPU/电源变化。下一轮若仅验证回收修复，保持启动args、设备、插入方式及供电策略不变。trial号由DSH分配，不自行宣称已安排706。
6. 最小修复构建与主机验证后交DSH，重新实验应要求on、off、恢复都完整且0错误。705保留原FAIL/INCOMPLETE事实。
7. 要真正让DAC和耳放工作，另外恢复DAPM供电/时钟/电荷泵/trim依赖，再处理MTKAIF/AFE实际数字路由。不能把“只缺I2S”当既定事实；当前hakios.sound.c走PRI_TDM_RX_0/AW88261外放，不代表MT6368接口一定是普通I2S。没有做PCM、没有声音成功证据。

## 9. 给下一位 Codex 的重要警告：DSH 与本 Codex 各犯过什么错

DSH主动要求把错误模式写进交接，以后不要重复。他负责广域资料和取证很有帮助，但以下涉及二进制/硬件的精确判断必须由Codex独立确认。

1. **update_bits mask/value反了**：w2是mask，w3是value；不能按自然语言箭头猜。
2. **把多个CFG分支并成45步**：HPSPK/普通HP/阻抗检测/PRE_PMD互斥；固定立即数正则漏零写、动态字段、helper与循环。修正终值后也可能保留错误推演（把互斥mux先1后2）。
3. **735-byte日志误判XNU早期死亡**：loader的logbook_boot_note在跳内核之前预写735字节；这不是XNU启动进度。残留只证明没有成功落盘新的内核日志，不证明没进返回路径、没打印版本或没初始化内存。
4. **reason与sum字段混淆**：save_log_to_flash第三参数0xfeedface是reason，不是sum，也不是“checksum通过所以没复位”的证据。校验通过只证明那个日志载荷内部一致。
5. **AUDENC当AUDDEC**：2508–2537是AUDENC；2588起才是AUDDEC。699/700/701的129项dump包含48项AUDENC，不能宣称已读过AUDDEC或证实其写路径。
6. **interconnect votes当缺口**：主线GPU节点没有interconnects属性；我们mfg_master_gates的global/async/dcm/qchannel配置不能随口称带宽vote。先核实际binding/实现，再谈不足。
7. **GLOBAL_IF未出现推出未取指**：只能定位未见首次固件握手，不能证明MCU没执行指令。主线runtime ping SError与我们的首次握手失败阶段不同，但不是直接因果解释。
8. **七对寄存器当镜像**：已撤回，它们是独立CON配置项。
9. **0x231d当ACCDET锚点**：已撤回，这是MFD按IRQ申请配置的使能位；我们的轮询实现与Linux IRQ申请不同，不能硬要求相同值。
10. **三条诊断已在699/700生效**：实际当时是按695基线隔离构建，产物strings与日志均无这些诊断；工作树里有代码不等于试验内核含它。
11. **703把脏树当单变量**：含诊断/L2/PDC等无关改动；704才隔离成695+msgbuf+GPUEB，但仍只有loader boot note，不能据此定位失败阶段。
12. “MCU-IS-CORTEX-M”等文档曾有多处推断/修正，不要把标题当处理器架构证据。具体架构问题重新从原始资料确认，交接不把未核实结论带下去。

**本 Codex 自己的错误同样必须避免：**

- 把8-bit regmap意味着“低字节都会保持”混为一谈，要求260b写9f后全byte读9f，遗漏了头文件只定义gain1f；这直接导致705关闭/恢复流程中断，是我的生产代码问题，不是DSH提取或打包问题。
- 离线模型太理想化，没有reserved/未实现位行为，导致全部测试通过仍漏真实硅片差异。独立来源的oracle也有建模边界，不能当硬件真值。
- 末尾gain写失败短路了真正off读回与恢复，日志只能NOT confirmed，失去区分“关闭步骤已执行”与“未成功收尾”的能力。
- 前一轮判据已经明确过25a1/259f缺全字节日志，不能因为代码使用RMW就把未拥有位的实测保持自动算PASS。
- 我没有声称三联值证明实际供电，但早先“这些步骤让DAC上电”的项目措辞容易回流；始终使用“寄存器探针”，直到依赖条件和硬件证据齐全。

## 10. 需要保留的工作树与其他线索

本轮开始前已有且应保留：drivers/map里的GPUEB头映射；未跟踪drivers/gpu/hakios.gpueb-ipi.h；mali610里的GPUEB/L2/PDC修改；keys入口日志；headset入口诊断；make-kernel.sh中文regex；xnu/hakios.patch里的msgbuf1MiB等。不要只按旧交接“五项脏改动”清理：后来GPUEB也加入了，当前git status应自己看。

`hakios-mtk`本轮未push、未commit。所有本轮四生产文件/测试/fixture仍在脏树。DSH自己的docs提交由他管理。

历史已知：699首次完整ACCDET dump；700空座mode2稳定；701三段耳机C→B成功，6秒micbias关闭后AB3仍插入正常。kernel700/701完整SHA `e91a9a4ae0e8f8cca7bf6ee0dbf67cce6de4d6ddae73a80e11beedcc2e3bfa74`。不要把它与705的4b1a55da混同。

GPU若以后恢复，先看 `/home/lizi/IOS/hakios-mtk/logs/gpueb-704-review-20261115/FINDINGS.md`。曾建议704原kernel/payload仅gpueb=0、return150配录像定位；耳机优先后尚未据此完成新GPU结论。_Static_assert是编译期，宏只在展开处起作用，不能直接归因XNU“早期挂死”。GPUEB TX/RX新增MMIO可能卡住，软件timeout救不了不返回的总线读取；不要把没落盘日志误读成已经知道执行阶段。

## 11. 本交接完成时的准确状态

705日志已核SHA并读取；260b两句判断已答；本交接已写。**没有在705后修改任何生产代码/测试，没有构建，没有刷机，没有进一步搜索。** 当前生产代码仍会对260b=9f做错误的全字节核验。

下一位应先向DSH确认收到本交接及新的恢复工作指令，然后从第8节最小修复继续。不要重走整套反汇编，不要把705事后改成PASS，不要先扩展CLKSQ/NCP供电来掩盖字段校验错误。
