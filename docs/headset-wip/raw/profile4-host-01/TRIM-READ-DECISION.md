# 主机复核结论与下一步输入

2026-11-16，Codex。仅分析主机上的源码、模块和新内核，不授权设备操作。

## 本轮 host 结果接受

DSH VERIFY-PROFILE4-HOST-01 的 271-case 模型、既有模式回归、两项源码差异及冻结输入接受。
新内核 SHA256 为 cac498da96406cc4f660e2cc717dbcf93b259ae1d864de6f30a9e920ed03401d。
它已经链接，构建真实 rc=2；judge rc=0 仅豁免既有 symbolset 失败。
不能把 judge rc=0 改写成完整 make rc=0。

Codex 独立核验新隔离树全部 5205 个基底文件：恰好只有驱动和 PMIF 头两个内容不同。
最终内核包含 p4_enable、p4_apply、p4_step、p4_verify、p4_down_until 等代码符号，
以及 p4_up、p4_down、p4_registers 数据和四个 trim 参数名。
与 709 比较，__TEXT_EXEC.__text 增加 5152 字节、__TEXT.__const 增加 2720、
__TEXT.__cstring 增加 2219、__DATA.__data 增加 384、__DATA.__bss 增加 32。
总文件大小增加 288 不表示只增加 288 字节代码；段、节与尾部元数据布局影响文件总长。
这些核验只证明链接产物包含实现，不能证明运行或硬件正确。

`linked-profile4-presence.json`、`linked-section-audit.json`、`dsh-new-tree-audit.json`
保留上述原始解析结果。

Codex 检索 DSH 完整 profile.log 2580890 B 与 legacy.log 177844 B：
AddressSanitizer / UndefinedBehaviorSanitizer / LeakSanitizer / runtime error 等诊断模式均零命中。
日志实测 hash 与检索模式在 `dsh-sanitizer-audit.json`。
这补充 DSH 当时只确认退出码、未检索完整 sanitizer 文字的说明。

## 为什么下一步先核定读取入口

mode4 需要真实 vendor 使用的四个 trim 参数，当前只有模型合成值。
不能从 idle 的 0x258e/0x258f 零值推断 trim=0：HP_ANA_TRIM 的 PMD 会清除这两个 byte。
未来取证应关联实际耳机播放、HP_ANA_TRIM On 及取证时刻。
读到应用中的参数仍只证明 vendor 使用的取值，不证明模拟 DC offset 校准质量。

匹配源码 mt6368.c 的 mt6368_codec_read() 从约 5789 行开始。
它不检查 regmap_read 的返回码，反复复用同一个 unsigned int value。
匹配实际 ko（4ce5cab0...）反汇编的该函数包含 314 个 regmap_read 调用点、315 个 scnprintf。
其中对 0x258e 的调用位于 .text+0x73e4，0x258f 位于 .text+0x741c；
调用后直接取栈中 value 去格式化，返回的 w0 未作成功/失败分支判断。
因此读取失败时可能打印前一次保留值，而 dump 本身不提供逐寄存器失败标记。

mt6368_debugfs_read() 和 mt6368_codec_sysfs_read() 都调用这个完整 dump 函数；
sysfs 的 offset/size 在 dump 完成之后才用于拷贝输出，不是选择寄存器地址。
所以用小 read、dd skip 或只 grep 两行不能把它变成两次定点寄存器读取，
也不能补上 errno 丢失的问题。目前不选它作为唯一 trim 依据。
本轮只分析该函数及 read wrapper 的调用关系，不把“源码调用名是 read”当成总线无副作用的全链证明。

反汇编完整输出与调用统计在 `mt6368_codec_read.disasm.txt`、
`mt6368_codec_sysfs_read.disasm.txt`、`mt6368_debugfs_read.disasm.txt`、
`trim-read-interface-binary.json`。由普通 ELF/objdump 工具生成，非 REA 原生 Evidence。

下一步收集固定 vendor commit 的 regmap debugfs/core 与 MFD regmap 配置，
由 Codex 核定错误处理、缓存、地址筛选及读副作用；DSH 仅下载验真与归档。
这些源码到位之前，不生成设备读取命令，不播放，不为当前无实测 trim 的 host kernel 打镜像。
