# Regmap 定点读取的审定结论

Codex，2026-11-16。全部为主机证据，未执行设备操作。

**候选入口确定为 regmap/0-05/registers 的固定一次 pread；运行实例身份和播放上下文仍待以后核验。**
普通 cat、缓冲 stdio 或全量 vendor dump 不属于该方案。

## 输入身份与源码归属

固定提交 0a8d13c1a180fed82803f56337a0b065ff704fb4 的8份源码见 source-manifest.json。
DSH原下载的5份与本机该提交 Git blob 逐字节相同。
MT6368由通用 drivers/mfd/mtk-spmi-pmic.c 管理；最初要求的 mt6368-core.c 文件名是 Codex 的错误。
该文件的404及原5/6请求结果保留，不追认成已找到原文件。

vendor_boot提取的 mtk-spmi-pmic.ko SHA256 1f2d95c5...、regmap-spmi.ko SHA256 0ac2b7bc...，
来源链由DSH封存；与当前运行实例的一致性尚未取得。
MFD模块 spmi_regmap_config 是280字节常量，完整对象内无重定位，不能把未链接的零指针误读为已初始化回调。
匹配头文件LP64布局及实际内核初始化访问共同核验：reg_bits16、val_bits8、max_registerffff、
reg_stride配置0（core转为默认1）、cache_type0（REGCACHE_NONE）、fast_io1，
readable/precious回调及访问表为空，ranges为空、num_ranges0、disable_locking0。
详见 mfd-regmap-config-binary.json 与 __regmap_init-stock.disasm.txt。

## 从备份直接得到实际内核实现

源备份 boot MD5 a63b2e35a7c2522cb24a6a10332b9bc8、
SHA256 c5f8e7bde8e9f1d824d0c7915f7d701adce8142656a82c20e10ccb75e15e01fc。
它的v4头后kernel为gzip；解出的42201364字节Image SHA256：
12d255cdd09fb5b9fb5cf9608e6b060a5b2dabc109fd4344b071a3dbdc0cf391。

Image完整banner为5.10.237-Fuutao-Qn_miao-gc5e20589c，编译器clang/LLD22。
这与两个模块的5.10.198 vermagic不同；vermagic不等于当前内核身份，更不等于源码提交身份。
因此本轮直接反汇编备份内核，没有把所下载regmap源码追认为实际运行core。

内置IKCONFIG解出181983字节；KALLSYMS/ALL/BASE_RELATIVE启用，BTF未启用。
离线解码保留140592个符号记录及重复名字，全部550个压缩marker一致，地址偏移单调。
通过唯一_text/__start_notes/__stop_notes符号限定kernel notes区间，得到GNU build-id：
76cc81c2a436243ca099b361b84defb940552ea2。
不能把Image里任意GNU字串都当kernel build-id；另有其他GNU note候选不属于该区间。
可重跑入口是 extract-stock-kernel.py，原始输出是 stock-kernel-identity.json、stock-config-1.txt、stock-kallsyms.tsv。
这些是备份内容的身份与静态实现，不是当前设备的实时观测。

## 为什么该窗口可被限制

实际内核 regmap_read_debugfs 在0xffffffc008c46fd8，map读取wrapper在0xffffffc008c47cbc。
错误路径调用_regmap_read后检查返回值，失败用X填充两个值字符；不会像vendor dump那样忽略返回码。
缓冲边界检查发生在每次寄存器读取之前。
get_dump_start的debugfs_off_cache只缓存“文本偏移→寄存器”的对应关系，不缓存寄存器值。
实际regcache_read中cache_type0会直接返回-ENOSYS；随后_regmap_read才进入硬件读取路径。
源码与实际模块的regmap_spmi_ext_read对258e/258f调用spmi_ext_register_readl并传播错误。
空ranges不设置虚拟分页，所以这条可见调用链不需要分页选择写；控制器及电气行为未被执行。

对于stride1、8位值、完整可打印0-ffff集合，文本每行是 `xxxx: NN\n`，恰9字节。
0x258e十进制9614，86526=9614×9。一次pread(count18,offset86526)对应258e和258f两行。
范围文件只是软件可打印集合，不代表所有65536个硬件地址都可安全读取，不能据此做全量dump。

emulate-read-window.py运行的是上述备份内核的实际指令；15个模型用例通过。
包括cold/warm定点两行、9/8/17字节窗口、负/零请求、单/双寄存器错误XX、用户拷贝失败、
分配失败、00/ff合法、错位seek以及cold range元数据无寄存器读取。
错误偏移86527会请求258f和2590，说明“偏移+返回地址标签”都必须校验。
模型边界为分配器、锁、元数据谓词、格式化、用户拷贝及_regmap_read；没有执行PMIF/SPMI控制器。
这不是硬件总线事务测量，也不是原子双寄存器快照。

## 读取工具与以后设备动作的门槛

固定C工具合同是 READER-CONTRACT.md。DSH原实现经审计后由Codex修订，最终工具在：
/home/lizi/IOS/hakios-mtk/logs/regmap-reader-audit-20261116/。
审计、修正和验证详见该目录 READER-AUDIT.md；原候选不得替代修正版。

未来设备CMD之前必须确认：当前kernel notes与这份备份匹配，两个模块与提取件匹配；
map确属SID5，name/range/cache元数据符合合同；用户在手机旁。
数据取证须与实际耳机播放及HP_ANA_TRIM On关联，并保留独立时间、原始两行和失败状态。
idle零值不能当默认trim，因为HP_ANA_TRIM PMD会清除258e/258f。
读到vendor实际使用的byte也不证明DC offset校准质量；当前仍没有真实trim参数。
不生成mode4设备boot-args或镜像；gain31也不保证电气静音。

本轮没有改变mode4八项输入、cac498da host kernel、709冻结树、wrapper或共享hakios-kernel。
全部工具使用普通ELF/objdump/Python/Unicorn流程；REA连接此前不可用，没有REA原生Evidence ID。
