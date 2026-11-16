# Codex 接续：Profile4 与定点读取器已完成主机准备

2026-11-16。最近 DSH 文档提交 e94f6a3。本文件是当前状态入口；旧705/707/709记录按历史阅读，不改判。

## 协作与当前边界

用户分工：Codex定二进制/源码语义、寄存器与位域、硬件块归属、驱动代码、实验设计和判据。
DSH做搜集、实验打包与hash链、设备执行/恢复、日志取证、判据工具化、文档仓与边界守卫。
给DSH的每条[CMD]必须含做什么、判据、边界、风险。不要让DSH自行解释寄存器或放宽判据。

用户远离设备；本阶段全部主机工作已完成，尚未放行设备操作。
设备此前为Android运行、boot_a MD5 a63b2e35a7c2522cb24a6a10332b9bc8，耳机插着且不戴；本阶段没再查询。
未来用户在设备旁时，由Codex另发设备CMD。别把本文件当作运行设备命令的授权。

五条既有红线保留：时钟未确认前不碰GPUEB 0x13c40000；不fastboot oem expdb；
黑屏后不长按电源；不从混杂脏树构建后刷机；不未报备刷机。
Android spmi_access已有两次设备掉线记录，继续不用。

## 工作区与保护对象

- 生产仓 /home/lizi/IOS/hakios-mtk：有既有WIP，永不push；代码由Codex维护。不要reset/clean/sync覆盖。
- 文档仓 /home/lizi/IOS/repo/hakios-mt6895：由DSH写文档和push。GPUEB-INTEGRATION.patch的既有未提交改动不覆盖。
- 旧709冻结树 logs/msgbuf-1m-20261115/kernel-src 及BUILD保持原样。
  其kernel SHA256 c17cf881745511a3c2aa0225ca7a28f3e7b65e853688e13839319d8e6515010d。
- 默认/shared hakios-kernel SHA256 67f9f933117f9bc2b1fb6c9566cd032079a83dab218825ecb838a385d15f77e1。
- wrapper tools/make-kernel.sh SHA256 95247bb011c136d22ca70b485a3388e46720464b6b40f3842bac2acdee80dbd3。
  XNU_SRC隔离树选择已修复；普通构建仍会更新shared hakios-kernel，这是保留的出口语义。
  参考构建用直接xnu-build.sh + 显式KERNEL的--judge；别再改wrapper扩展范围。

文档仓曾有空文件和9个空Git对象，DSH已备份后用远端恢复，fsck通过；不要把空对象放回去。
346项审计仅GPUEB补丁既有WIP不同，未发现其他当前文件不符。不能反推历史损坏根因或范围。

## Profile4 驱动与host kernel

生产驱动 mtk/mt6895/drivers/audio/hakios.mt6895_headset.c 已有headset=4。
固定codec内部SGEN、48k、hifi=0、steady gain31，限三段耳机；重分类四段会关闭。
不编程SoC AFE/MTKAIF/PCM/DMA。gain31是vendor HP_MUTE所用码，不能保证电气静音。
必须显式有hp_trim_l/r（0..31）和hp_fine_l/r（0..7）；缺失/越界在PMIF操作前退出。
无默认值；合成trim3/7 fine1/2仅在模型，禁止用到设备或设备boot-args。

管理38组字段；SID9写只准0x079e，consumer只改bit2并保留其余位，开启后400us。
VAUD18必须已开，本实现只核验，不切换该regulator。禁止把它说成完整移植了regulator子系统。
DAC源238e bit0有独立快照/选择/恢复；SGEN 23c8按DAPM合并mask=c0 val=80。
HP关闭未确认前不撤上游供电；分阶段保留关闭/恢复位置，最多两次重试，不在供电失败后重放模拟ramp。
持续故障仍可能关闭不完整，不能把软件日志成功以外的状态判硬件关闭PASS。

8项实现/验证输入清单：logs/headset-profile-20261116/production-inputs.json及.sha256。
驱动SHA256 3c79386ac367ba81fe02774fbb8d37a6475845432a644ff91fdcb56a98c279ce；
PMIF头SHA256 f4d9a964e304ea14686359b85e75560325e9d47ee2ecb5614aae538f9e293d78。
271个ASan/UBSan用例通过；独立vendor oracle字段归一化轨迹up161/down139相同。
既有mode1/2/3回归通过。参考API边界173/147与这些写/延时事件数不是同一种计数。

新隔离树 logs/profile4-kernel-src-20261116-1525：从5205文件封存基底复制，仅驱动与PMIF头两项变化。
新kernel 15321160B，SHA256 cac498da96406cc4f660e2cc717dbcf93b259ae1d864de6f30a9e920ed03401d。
make真实rc=2，内核已链接；--judge rc=0仅豁免既有symbolset失败，不等于make rc=0。
没有镜像、payload或可刷trial710。不要直接复用shared hakios-kernel打包。

## 读取器的最终候选

目录 /home/lizi/IOS/hakios-mtk/logs/regmap-reader-audit-20261116/。
阅读 READER-AUDIT.md、review-inputs.sha256、replay-result.json。
ARM64静态ELF regmap-reader.aarch64：4232B，
SHA256 63688efccd5728b0155a782eb299db979d6150b2096b4dacbc1e083429aa30e1。
48个C场景/287断言与60个实际ELF syscall模型场景全部通过；DSH已独立重跑并得到同一ELF hash。

固定map /sys/kernel/debug/regmap/0-05/；name必须mtk-spmi-pmic换行，range必须0-ffff换行，
cache_only/cache_bypass/cache_dirty只准open返回ENOENT；其余错误一律失败。
之后registers只许一次pread64(count18, offset86526)，原样输出258e/258f两行。
XX、短读、地址/格式错误、read/close/output失败都非0；不补读、不猜别的map。
整个ELF syscall模型验证了真实openat标志只读，并用反复复用的fd3跟踪文件类型。
这不是在手机执行的结果；Linux权限、挂载、SELinux、实时身份与电气行为未测。

原DSH候选 logs/regmap-reader-prep-20261116-161918 保留，不使用其1d96bdd5 ELF作为通过候选。
它忽略close错误，实际ELF模型有3个负对照失败；旧test fd映射断言也有缺陷，旧源码hash链不完整。
新目录original/与original-elf-model.json保留证据，不删除失败用例、不把旧结果追认成功。

## 实际备份内核与regmap语义

目录 /home/lizi/IOS/hakios-mtk/logs/regmap-trim-review-20261116/。
阅读 REGMAP-READ-AUDITED.md、READER-CONTRACT.md、source-manifest.json。
MFD文件实际是drivers/mfd/mtk-spmi-pmic.c，原mt6368-core.c的404记录保留。
本机完整vendor仓 /home/lizi/IOS/work-xaga/k-xaga 的固定提交0a8d13c1a180fed82803f56337a0b065ff704fb4可用。

原厂/当前Android引导备份 backup/boot_a_stock_real.img：67108864B，MD5 a63b2e35a7c2522cb24a6a10332b9bc8，
SHA256 c5f8e7bde8e9f1d824d0c7915f7d701adce8142656a82c20e10ccb75e15e01fc。
内含gzip解出Image 42201364B，SHA256 12d255cdd09fb5b9fb5cf9608e6b060a5b2dabc109fd4344b071a3dbdc0cf391。
完整banner为5.10.237-Fuutao-Qn_miao-gc5e20589c；两个PMIC模块vermagic却为5.10.198，不能混作内核身份。
已新提取IKCONFIG、140592个kallsyms记录并验证550个marker；BTF配置未启用。
kernel GNU build-id 76cc81c2a436243ca099b361b84defb940552ea2，由__start_notes/__stop_notes限定的notes确定。
不要拿其他嵌入GNU字符串当kernel build-id。重跑脚本extract-stock-kernel.py。

actual regmap_read_debugfs位于0xffffffc008c46fd8；使用该备份真实指令的15个Unicorn窗口模型用例通过。
固定前提下每行9字节，86526对应258e；18字节只请求258e/258f，失败打印XX。
偏移错1字节会读到258f/2590，必须校验输出地址。两寄存器不是原子快照。
offset cache是文本偏移缓存，不是寄存器值缓存。
MFD模块常量无重定位，16bit地址/8bit值/maxffff/default stride1/REGCACHE_NONE/无过滤和分页；
actual core的初始化字段、cache-none与错误分支已反汇编交叉核验。
完整0-ffff软件可打印范围不代表这些硬件地址都能安全读取，禁止全量dump。

PMIC模块均来自既有vendor_boot SHA256 fed80580d619b916d73b51b704f2af6c9f08afafc84810ced0b2397a0b52797f：
mtk-spmi-pmic.ko SHA256 1f2d95c5e11d6a197d90cb7dc60d047ff700275cad7e9738f63684adc00bee7f，
build-id 9119ad181d63b4f694a561e4a2d367647ae78371；
regmap-spmi.ko SHA256 0ac2b7bc746632b86ab474264f1be3b04e79564271d5e8609a9fea3a3311ce84，
build-id 031912cdcf9827082872fc9f52f1921f380e621c。
位置 logs/host-regmap-identity-20261116-154659/extracted/lib/modules/。
它们与手机当前运行实例的一致性尚未取得，不能用vermagic代替。

## 历史结论保持与后续顺序

705/707 FAIL保持；708 CLKSQ上下电成功；709只是SID9只读探测bit2=0，历史源链仍INSUFFICIENT。
重建/代码段相同不追认709历史输入。当前新链从冻结manifest开始可核验。
SUPPLY_S后缀是subseq，DAPM先按widget类型再按subseq排序，不是全局枚举配方。
AIF_RX On且显示AIF1 Playback inactive不证明绕过AIF1；active属于自动DAI widget。
DC Trim仅四点采样Off，不证明校准线程有/无执行；HP_ANA_TRIM逐次应用参数。
idle的258e/258f会在HP_ANA_TRIM PMD清零，不能用idle零值作trim默认值。
vendor codec dump忽略regmap_read错误且每次读314地址，不能作为唯一可靠定点入口。

后续只在用户在手机旁后由Codex另定设备CMD：
先只读核验运行kernel notes和两个模块身份；不匹配/缺失就停止该路径，不执行reader。
匹配后再核定当前map元数据，安排受控正常耳机播放中的两次定点取证与HP_ANA_TRIM/AudioFlinger时间证据。
真实byte仅证明vendor应用值，不证明DC offset校准质量；真实参数到位后才设计mode4硬件试验。
不把当前host kernel或合成参数打成镜像，不做逐bit关钟必要性筛查，不使用Audio HP ImpeDance副作用getter。

所有静态分析使用普通ELF/objdump/Python/Unicorn；REA注册此前缺失，未改配置，无REA原生Evidence ID。
DSH终端句柄当前 term_75f2770f-7953-46dc-ab93-dd26817d789b；Orca CLI使用已解析的
/home/lizi/.config/orca/linux-orca-cli-shim/orca。发送receipt accepted仅证明输入接受，别因静默重复发。
