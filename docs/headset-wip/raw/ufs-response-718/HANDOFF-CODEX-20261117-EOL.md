# Codex EOL handoff — 2026-11-17

用户最新指示：额度告急，现在准备交接；随后用户会让 DSH 切换账号继续。停止新增技术工作。
本文件是当前最新交接入口。旧 HANDOFF-718-CURRENT.md 有过程快照，本文件覆盖其进行中状态。
不再构建、测试、打包或刷写；当前没有待完成的设备恢复。

## A. 当前结论与设备

**718 已完成一次实机试验，首次得到内核 READ_OK(5)。已自动恢复原厂。**
固定 READ10 / LUN2 / 逻辑 LBA1 / 单块4096字节，完整响应接受，CRC32与 Android 独立参考
0x28a2836c 一致，租约干净归还，运行时 HLOG reason0，恢复全程无需手动 fastboot。
这只是一次有界单块读取；没有持久块设备、文件系统挂载或持续存储服务。

最后实际核验的原厂状态：

- boot_a67108864字节，全分区 MD5 a63b2e35a7c2522cb24a6a10332b9bc8。
- slot_a，boot_completed1，SELinux Enforcing，SurfaceFlinger 正常响应。
- boot_id **f745b1c6-ee43-4abc-8ed2-06ee9629bf27**。
- 恢复原厂 flash native rc0，2.25秒，Sending/Writing OKAY；reboot rc0。
- **没有新的设备试验处于授权待执行状态，不得重复718。**

## B. 已交付的仓库状态

P=/home/lizi/IOS/hakios-mtk（生产/构建工作区，禁止 push）。
D=/home/lizi/IOS/repo/hakios-mt6895（文档与证据仓）。
R=P/logs/ufs-response-20261117（718全部本地输入与产物）。

重要提交：

- 02b13a1：718主机审查与隔离内核构建。
- 8a3aa81：718独立打包审查。
- fdc574d：718原始设备证据。
- **c32917a7765b2f3424c952de75b80c019770c1fd**：最终源码/补充证据/交付入口。
  Codex 已独立看到 remote main 为此完整提交，并核对提交树23源码清单项、26快照文件及5项
  原始日志/结果/复核 blob 与本地逐字节一致。
- **79c24fb**：最后文档澄清，只改 docs/headset-wip/UFS-DELIVERY-20261117.md。
  Codex 已独立读过 diff，确认只有这一个文档变化；不改源码或设备证据。
  写本交接时最新79c24fb远端查询尚未收取结果，**不冒称已由 Codex 独立核实远端**。
  若 DSH 后续补交本交接，HEAD 还会更新；源码/设备身份仍以 c32917a 为已核对基准。

入口：

- D/docs/headset-wip/UFS-DELIVERY-20261117.md：带源码、清单、试验结果的实际相对链接。
- D/docs/headset-wip/raw/final-ufs-source-718/source/：26文件，19源码+4冻结元数据，23清单项。
- D/docs/headset-wip/raw/device-ufs-read-718/：22文件，含原始 HLOG/body、结果和独立复核。
- D/docs/headset-wip/raw/ufs-response-718/、raw/pack-ufs-response-718/：主机/验包证据。

**源码快照不是完整可独立构建的5210文件内核树。** 原始完整隔离树为 R/kernel-src，
清单 R/kernel-inputs.sha256；快照 metadata/ 中有同一输入清单。保留本机完整树和 BUILD。
快照 candidate/ 的4个 host test可独立按 README 编译；没有为复制再重复测试。

D最终 status 仅有开工前的 docs/headset-wip/findings/GPUEB-INTEGRATION.patch；SHA256
9cdcb0da826e9bd561fce98aae81304789fee2742c63b10235942f028e53eb16，未改未 stage。
P原有 WIP 路径集合与开工时相同，不能清掉或误当成本轮新增生产修改。

## C. 718关键身份

内核 R/kernel-src/BUILD/obj/DEVELOPMENT_ARM64_BCM2837/mach.development.bcm2837：
15338864 B，SHA256 **60bd6b195c2ab65ab0c5c1cb519fe3cf5af9ad64c351485ccbf59aa899fd7cfe**。
make native rc2；显式 KERNEL judge rc0，仅既有 kext symbol-set 豁免。不能称 make 原生成功。

R/trial718/boot.img：22999040 B，SHA256
**3a3ceb1d77c9a61f289695879c077baf7ec076a8554c936aec8110afd1e61970**，
MD5 **42beb68e83eabea8a281965888380417**。
loader.bin：47264 B，SHA256
dc1d273481eeab99086726e90762c4dca4a30e7d631fddab31990349ce6e4b25。
payload.bin：19533320 B，SHA256
a43093c1479e34debc82ebca787a3c59735aa79279a3f5ec78b686183b673aba。

新引擎 R/candidate/ufs_read_engine.h：11027 B，SHA256
81aeee096c339fbbf61aa23bf248b1fa24aa3b38e4c5ce26d642d35cd3afc7df。
冻结51项：R/HOST-INTEGRATION-MANIFEST.json；隔离树5210项。设备试验后51项及三产物重新核对未变。

原厂 P/backup/boot_a_stock_real.img：67108864 B，SHA256
c5f8e7bde8e9f1d824d0c7915f7d701adce8142656a82c20e10ccb75e15e01fc；MD5见A。

## D. 直接运行证据与独立复核范围

718新鲜 guard trial718/state0，准确228字节运行时参数，继承 READ-only UTRD/UPIU。
READ_OK5，claimed1/issued1/restored1/data_valid1，polls116，OCS0。
响应 valid=attempted0x1fff，failures0，13个小端词：

```
00020021 00000000 14000200 00000000 00000000 00000000 00000000 00000000
10801200 00010001 00000000 00000000 00000000
```

transaction21/LUN2/tag0/response0/status0，EHS0，data segment20，residual0，sense长度18。
sense的18字节只是原样记录，未解释其语义或据此宣称设备健康。
block CRC32=expected0x28a2836c，4096字节，accepted1。
最终 shared state2/loader_io_allowed1。

raw4198400 B SHA256825da97d036603a4796ffafb1b4f2554fbe9d38e3a4bad37fe743d7b8d5e8719。
body497459 B SHA2568f0d1e8a2a545201bdcff283aabbb21c048ea00becc93acfbee29c14c705918d。
HLOGv1/reason0，完整整数 sum0x26f45ab 与 header 相等。原始文件在 R/device718/，提交树同字节。
实际 reboot14:50:43.319700；capture14:54:28.352026，约225秒，在300秒预算内。

Codex 已独立核过：

- 5210内核输入、51候选/loader文件、原生 rc2/显式 judge0、MachO中 quiesce真实引用与符号。
- loader源码仅 trial标签/cookie/参数改变；native三rc0；返回 gate 语义未变。
- 严格镜像解包、实际参数NUL本体唯一、168/228/229长度、XNUP24字节终止项/exact EOF。
- 主机 ASan+UBSan/MSan各 engine32513、contract983、atomic40、guard23；真实XNU头ARM64编译0。
- 718日志解析器58合成案例；实际原始HLOG/body/完整sum/13词/新判据/CRC/租约/正常返回。
- DSH恢复原厂记录、完整分区MD5、新boot_id及SurfaceFlinger。
- c32917a提交树源码23项/快照26文件和5项核心设备blob；远端c32917a。

尚未核实或实现：持久块设备、长期队列所有权、任意/多块读取、文件系统挂载、内核写服务、
吞吐、物理链路速率/gear/lane、sense内部语义、CPU713恢复问题的原因/修复。
**CRC32一致不是逐字节相等或全块SHA256。**

## E. 为什么修改响应判据，以及不能回溯的历史

716：OCS0/COMPLETED_ERROR3，没记录响应词；具体拒绝字段未知，保持 NOT_PROVEN。
717：直接采到 failures20(bit5)，header的data-segment-length20被旧“必须0”判据拒绝；
诊断CRC匹配但accepted0，保持 NOT_PROVEN。717没有采到header之后的20字节。
718：依EDK2 UTP_RESPONSE_UPIU布局，采52字节，明确把段长接受范围从仅0改为0或20，
对20要求sense长度<=18，新增EHS0限制。实际sense18/EHS0，接受并CRC匹配。
**这是新修订改变响应接受范围，不是“判据没变”；白名单和READ几何/lease/restore/timeout没变。**
不能说716已证明bit5，不能跨轮断言所有故障只有这一原因；不重写716/717结论。

byte1是flags；byte8是独立EhsLen。响应DataSeg长度不是4096字节DMA传输长度。
primary references见 R/DESIGN-RESPONSE718.md 与717的 PROTOCOL-REFERENCE-PRE717.md。
failures0必须同时full valid1fff AND OCS0才算响应接受；不完整或OCS非0时可能是未计算的0。

## F. 下阶段最小动作（主机先行，不需要询问用户额度）

1. 新账号先读本文件、交付入口、R/RESULT-TRIAL718.md与当前源码；不要重做已通过的718。
2. 确认最新远端文档提交及本交接归档即可，无须重建或再跑全套测试来“接手”。
3. **我倾向先做持久只读块服务的主机设计与模型**：明确LK继承队列的长期排他所有权、
   工作线程/STOP/loader返回生命周期、范围白名单与有界READ、多请求恢复策略、DMA缓冲区。
   当前 one-shot init试验不是可直接循环调用的块驱动，不能未经审查简单包成后台worker。
4. 先形成具体API/状态机/测试再写新候选；任何新增实际硬件命令都要新隔离源、审查、
   打包和新编号单次DEVICE。用户已有推进授权，不要再问是否可以继续，但不能跳过验证。
5. CPU可写调频先不要顺带推进：cpufreq-hw与cpuhvfs谁生效、tbl-off域对应、MCUPM初始化、
   status置位者、USRAM/ESRAM与CSRAM重叠这五项仍未在本轮解决。GPU/基带也没有本轮修复。

## G. 红线与操作约定

- DSH独占 adb/fastboot；Codex不直接碰设备。单次DEVICE、先抓HLOG、立即恢复stock、后分析。
- 禁止 stash/reset/checkout/clean/gc/prune，禁止旧BUILD/镜像/源码覆盖，禁止生产仓push。
- GPUEB物理0x13c40000/OEM expdb/watchdog/任意MMIO或getter/clock-power-reset-UIC不扩展。
- 不手工清写HLOG，不上传raw GPT/GUID/pre-log/image，不改ignore规则，不stage既有GPUEB WIP。
- loader已有boot-note/HLOG写是基线；新内核只发固定READ。不是整个启动过程“零存储写”。
- shared lease/STOP不确定时必须fail-closed，禁止恢复可能仍活的DMA或绕过return gate。
- 安全理由是STOP阻止clean CAS+loader拒绝CLAIMED/POISONED，不是假定其余CPU都停了。
- 300秒从实际reboot起算；故障时有界等待并通知人工fastboot，不猜reset/看门狗路线，不加试。
- 原参数cpus4/mali0/headset0/mtk_ufs_read1/return150，5个GPU宏0、watcher0、lease1。不要误用旧CPU镜像。

## H. DSH与账号切换

DSH当前终端 handle：term_1af4e603-ad92-41dc-b838-e243ab05af8f。
CLI固定 /home/lizi/.config/orca/linux-orca-cli-shim/orca；不要裸调用orca（可能是屏幕阅读器）。
terminal read --terminal HANDLE --json 读最新尾部；若视口滚动，不用 --screen 防止看到旧画面。
terminal send --terminal HANDLE --text ... --enter --json 仅表示接受输入，要查看终端确认执行。
若账号/Orca重启导致handle失效，按orca-cli技能重新发现，别误发到旧会话。
Hydrogen已由用户关闭，不再等待它。

用户将安排 DSH 切换账号。交接完成后等待，不启动新技术任务。额度自己用 codex-quota 查询，
不要问用户；最后成功查询本账号五小时剩11%，不是新账号额度。

## I. 写入后收取的既有远端查询结果

交接先落盘后，收取了用户要求交接前已启动的只读查询结果：remote main确认为
**79c24fb21282c5e8c63c92bbd44d55a84903ae4d**。因此B节所述“79远端查询尚未收取”是写入
当时的历史状态，现在已完成独立核实。本交接的归档提交将晚于该提交；源码/设备证据仍为
c32917a那一组不可变身份。没有为此次补记构建、测试或操作设备。
