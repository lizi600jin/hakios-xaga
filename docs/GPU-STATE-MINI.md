# STATE-MINI · HakiOS xaga GPU

更新：2026-11-13T14:28:15+08:00。旧版保存于 STATE-MINI-pre-round10-2026-11-13.md；旧版的“所有内存/时钟因素已排除”“零指令执行”不可继续当事实。

## 当前任务与授权

主问题是修复 4K 映射后 CSF MCU 为什么仍未完成可见启动。
用户已明确授权操作手机、刷机、全权接管；无需重复询问相同权限。
最初要求的 round-7 离线审计已完成、提交并推送。后续真机结果见 GPU-ROUND-8/9/10。
不直接试 mali=6，继续 mali=5 上独立变量。

## 手机与恢复

设备 GAJZ4HIN99AQ69Y9，678 完成后已恢复原厂 Android，boot_completed=1。
boot_a MD5=a63b2e35a7c2522cb24a6a10332b9bc8。
备份 /home/lizi/IOS/hakios-mtk/backup/boot_a_stock_real.img，64MiB。
用户照片卡点是 XNU 返回 Android；672 Android dmesg 有 Mali FW boot/reset timeout。
root adbd 返回后立即拉 logbook；不用等 boot_completed。
普通 reboot 服务会卡：已验证 root dd 写 stock boot_a、fsync、MD5、sync、sysrq restart 的恢复路径。

## 不可违反

- 禁读 0x13bfee00、0x13c40000、0x13F91034。
- kernel 探针内联于已经输出的路径，不能仅新增独立函数。
- 每项实际 boot-arg 用 strings -a mtk/loader/mtkloader.bin 检查计数=3。
- 每轮结束把产物同步 /home/lizi/IOS/repo/hakios-mt6895，export https_proxy=http://127.0.0.1:7897 后 git push origin main。
- logbook 不按 header len 截断；674 起发现旧尾部混入，不能混用 trial。
- 675 起只备份/清理专用 4198400 字节 logbook 窗口，起点2676981760；校验分区总长2685370368。不要清整个 xnusys。

## 已证实与边界

- 原 bug：CPU PAGE_SIZE=16K，GPU leaf=4K；load loop 只填每四页的一页。已用 PT_LEAF_GRANULE=0x1000 修正。
- 实际固件总357叶，shared 0x04000000–0x04094000 共148叶；四级表39/30/21/12。pt_new 分配16K是浪费，不产生覆盖洞。
- 原固件入口0x010018ed在旧leaf1洞。旧 witness 改入口到0x01000001，代码/目标均leaf0，本来有映射，不能用leaf1洞解释它。
- global_interface 的 version=0 直接 return；VA0 被 shared_at 提前拒绝，确定无所称零指针偏移越界。实际GLB/CSG模板有有效in/out VA。
- AS UPDATE 后高位 IRQ 也在 unmapped AS2–7 出现。不要再仅凭0x10000称root fetch bus error；fault/status/address/gpu_fault均0。
- MCU_STATUS=1 不能替代 boot IRQ、有效version或witness完成标记。
- witness无标记仅为首个store未完成可见，不能严格证明零指令执行；旧stdout verdict过强。

## 修复后的实验

- 670/671：AS0在L2前/后，无有效固件version；671 post-L2保留为基线。
- 672：完整映射 witness；673：仅加强制FW NC；674：精确vendor SH=0/NG=0。均无可见store。
- 675/676/677：同内核，gate-only → gate+UPDATE20 → parent0 26MHz配置。日志各字段确认，均无可见store。寄存器字段不等于实测输出频率。
- 678：仅mali_low=0，root=0x101dec000，walk/读回正确、全覆盖，三次仍0xff5aa5f0,0。高分配路径未解决。该开关同时改变位置和CPU缓存策略。
- stock CPU kretprobe + 自有Mali缓冲分配捕获64个native PTE（16不同值），PBHA=0，低属性0x443。external回调0事件。只证明这些用户映射，不是firmware AS0全表；无依据盲加PBHA6。probe与instance已全部清理。

## 当前源码/构建与下一步

源码 /home/lizi/IOS/hakios-mtk/mtk/mt6895/drivers/gpu/hakios.mali610.c
675–678 driver SHA256=59147207c289989b084407e2f10dbf37f1f265a0cd5439dee23b2119b93dea36
kernel=a5c04ceb6fbda38ffbfb797eee0883d6a7d40d91f0e69df451a3824068a57056
payload=2b16691d48b4da14dfc1f144b0340ca9d2d43b8956f0e4d6fbc783138626738a
本地已打包镜像仍678，已运行并恢复；不要当作新trial再次刷。
下一步：单独验证GPU cache失效、复位生命周期；不直接用mali_cold=0把多变量混在一起。
先保持witness判据；未通可见store前，trace buffer不是唯一解释。
脚本/完整raw在out/round-8-support；run-once.py为已测恢复流程，prepare-loader.sh只构建loader/打包，analyze.py纯离线。
构建须走make-kernel错误分类门，不许仅看旧文件存在。
