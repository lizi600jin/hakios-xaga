> 本审阅记录冻结于构建完成时；其中“未commit/22项未暂存”描述当时状态。之后已本地保存为c9b4632并推送源码导出5606fa8，详见最新交接；真实手机验证仍未进行。公开源码按source-export.json重建，本机review-tree未上传。

B1+B3 共享集成已完成离线验证，供 DSH 独立复核。未执行任何设备命令、未刷机、未 commit。

kernel.bin SHA256：9dc40973afff3cf1551275d2da0fe80b8b4e9aa3089d32e1bcf1b26941cf191a
headset1/boot.img MD5：5e7c67f7cd8aeb90a86ff4098466812d
headset1/boot.img SHA256：8859138ecad4a56bdbe71a5c3ba5f472044556023b04ef9e27eed1aea77978c2
镜像 22986752 字节；完整参数及 loader/kernel/payload 哈希见 headset1/build.json。

实现与交付草案的差异：

- 共享实现放 mtk/mt6895/drivers/core/hakios.mt6895_pmif.c，公有头 xnu/files/bsd/sys/hakios.pmif.h；三个消费者只调用它。复用 hakios_spmi_lock()，没有另建锁；每笔事务单独加解锁，accdet_init 的 2ms/1ms 间隔在锁外。
- 使用单份 100ms deadline、全程累计最多 10000 次状态轮询；失败样本间隔至少10us，用时基忙等，暂停本身也有硬计数，冻结时钟仍能失败返回。旧 2ms/20000 实现已从活动源码和构建树消除。100ms 是轮询预算，之后可有有限的 STA/ACK 清理；不声称能界定一个自身不返回的 MMIO，也未实测硬件持锁耗时。
- mmio accessor 只承认 STA/RDATA 读、ACC/WDATA/VLDCLR 写；映射请求大小0x8ff，实际页粒度映射不授予0x900等偏移访问许可。移除了 chan 参数及越界算术。
- pmif_init 采用锁外 map、锁内发布。并发首调产生的多余映射在锁外按 VM 页粒度 unmap；map/unmap 都不在 spin 内。
- 所有失败点汇入同一 FSM=6 ACK 恢复，包括初始 IDLE 等待超时后才出现的 valid word；不重置全局控制器、不另起恢复预算。读仅在 ACK 后验 IDLE 成功后发布结果，写路径不等待 WFVLDCLR，保留 bit29 缺失会变读事务的注释。
- 严格拒绝 SID/地址/数值超宽及0x014b/0x0240写，先校验再编码，避免函数形参窄化后失去校验。
- 停核后的 headset_quiesce 只发布 atomic quiesced 标志，不取锁、不读/写PMIF、不ACK、不打印、不回放寄存器。saved_count 保留，明确没有恢复配置。回滚只在 keys worker 的失败路径执行。此项按 DSH 最新同意的“连 besteffort 也不做”实现。
- core/conf 新 driver MT6895_PMIF，三个消费者 requires；临时去掉配置的PMIF开关，三个依赖都能报错。drivers-check 的 usb 合法子系统遗漏已补，当前通过。

验证：

- preapply-check.log：冻结基线 md5 cb0a64f5/7bd9751d/255a08d4 匹配，3文件20hunk，4处ACC全部覆盖。应用使用检查器的精确上下文，不使用 fuzz。
- final-consumer-check.log：从该冻结基线到最终消费者代码的真实 diff 重新检查，仍3文件20hunk/4ACC零遗留；checker自身7项自测通过。
- state-machine-tests.log：实际生产共享C的10项基础状态机断言、6项集成检查全过；含1600笔三线程SID4/5事务、首次映射竞争、迟到FSM6、丢ACK、写完成、整笔100ms/10us、冻结时钟与硬顶；ASan/UBSan，严格警告作错误。宿主 pthread mutex 只代替既有锁入口，不替换事务代码；这不是手机SMP实测。
- 同一测试入口同时运行实际 accdet C + 共享PMIF C；验证129项零PMIC写、vendor配置/状态机、worker回滚和停核后零PMIF访问。它不证明真实MICBIAS供电或插入功能。
- kernel-build-second.log / build-console-second.log：完整内核构建包装器exit0；只有项目既有 kext symbolset/export 检查豁免。新共享模块实际编译并链接，最终四驱动/头文件与构建树逐字节一致。初次编译的新模块缺sys/types.h已修正。

首轮镜像边界：

- make xaga_defconfig 已执行，.config及 tools/hakios-config.py args 明确有headset=1；MT6895_PMIF和三消费者均启用，mtk/mt6895/configs/mt6895.config 已按用户要求单路径git add，未提交。
- 离线镜像刻意继承已验证trial692/NOPDC的完整loader flags，再增加headset=1。GPU参数使用该对照的 mali=5/mali_cold=1/mali_preas0=0；.config默认的mali=6/cold=0/preas0=1未混入这张对照镜像。actual_loader_args含headset=1且在loader二进制中按完整NUL字符串验证，463字节。gzip解包后payload与打包输入一致。
- headset=1 原124项 + 0008/0009/1cb2/230c/231d =129项，expected-registers.json给出完整顺序；只观测VAUD18，不启用。VAUD18未开时，AB读数不能作为检测成功的证据。无耳机，所以本轮仅SID5可读性与未插入基线，不能验插入、按钮或声音；headset=2的VAUD18/时钟前置项仍待后续实现。
- 验收不能只看一致率：需形状、镜像关系、231d锚点和供电前置一起解读。不引用baseline与round20相同MD5为两次独立实测证据。
- 镜像包含本轮之前已完成的三个loader日志修复：MARK_RUNNING不依赖WDT、rescue实际写HLOG、后续probe不覆盖已保存内核日志；未开watcher或未验证硬件WDT。日志修复仍需这次之后的真机验证。

审阅从 review-tree/ 与 review-source-sha256.json 开始；integration.diff是相对于本轮冻结快照的改动，不是对git HEAD的全树WIP归因。原有三个本地耳机提交 ef54fa0/7df8a9d/4ebad21 全保留，没有改历史或push。22个intent-to-add项仍保留原状，没有进行全树add；以后如要commit必须逐路径暂存并核对内容。
