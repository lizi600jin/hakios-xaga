# STATE-MINI · HakiOS xaga GPU

更新：2026-11-13T15:28:34+08:00。最新报告 out/round-12.md；上一版 STATE-MINI 已备份至 STATE-MINI-pre-round12-2026-11-13.md。

## 当前任务与授权

主问题仍是修复 4K 映射后 CSF MCU 为什么未完成可见启动。
用户持续授权刷机、操作手机及每轮恢复；当前会话为这条线唯一手机操作者。
设备 GAJZ4HIN99AQ69Y9。继续 mali=5 的独立变量，不直接试 mali=6。

## 手机与恢复

684 已运行并恢复原厂 Android，runner 正常结束；15:25:47 +08:00 独立 adb 复核 boot_completed=1。
boot_a MD5=a63b2e35a7c2522cb24a6a10332b9bc8。
备份 /home/lizi/IOS/hakios-mtk/backup/boot_a_stock_real.img，64MiB。
root adbd 返回后立即拉 logbook，不等 boot_completed；随后 root dd/fsync/MD5/sync/sysrq restart 恢复原厂。
当前本地打包镜像是已运行的 684，不要直接当新 trial 重刷。

## 不可违反

- 禁读 0x13bfee00、0x13c40000、0x13F91034。mali_rpc 会碰最后一个地址，禁止启用。
- 手机独占；一次一个实验变量；探针内联在实际已到达的路径。
- 每项实际 boot-arg 用 strings -a mtk/loader/mtkloader.bin 检查完整 token，计数必须为 3。
- 每轮恢复原厂 boot_a 并验证 MD5；不能把日志取回当成已恢复。
- logbook 不按 header len 截断。只备份/清理专用窗口 offset=2676981760、bytes=4198400；先校验分区总长2685370368，禁止清整个 xnusys。
- 内核构建走 tools/make-kernel.sh 错误分类门，不得只凭旧文件存在。
- 每轮同步 /home/lizi/IOS/repo/hakios-mt6895，export https_proxy=http://127.0.0.1:7897 后 git push origin main。

## 已证实与边界

- 原 bug 为 CPU PAGE_SIZE=16K 而 GPU leaf=4K，加载循环每四页只填一页；现以 PT_LEAF_GRANULE=0x1000 修复。
- 实际固件357叶、shared148叶，四级表39/30/21/12。旧固件入口0x010018ed落在洞里；旧 witness 入口在 leaf0，本来有映射。
- version=0 会直接 return；shared_at 提前拒绝 VA0，无所称零指针偏移越界。
- AS UPDATE 后 unmapped AS2–7 也有高位 IRQ；AS0 status/fault/address/gpu_fault 均0。不能仅凭0x10000断言root fetch bus error。
- MCU_STATUS=1 不代替 boot IRQ/有效version/witness；哨兵不变仅为未观察到首个store完成，不是零指令执行证明。
- Android AS配置起的 warm/cold history 不是整个 PM init，不能据此省略更早的 reset/coherency。
- 离线实际32字节witness在M7/Thumb模型产生11、22、deadbeef；只验证编码，不代替真机。

## 修复后的实验

- 670/671：AS0在L2前/后均无有效version；后者为基线。
- 672完整映射witness、673强制FW NC、674 vendor SH=0/NG=0，均无可见store。
- 675/676/677：MFGSC gate、UPDATE bit20、parent0，字段确认仍无store；寄存器字段不是实测频率。
- 678：mali_low=0高分配、walk/读回/coverage正常，仍无store；该开关同时改变位置和CPU缓存策略。
- Android native PTE样本64个（16不同值），PBHA=0；仅证明这些用户映射，不证明firmware AS0全表。
- 679：独立cache 0x3304 completion bit17到达，仍无store。
- 680：独立early soft reset完成，仍无store。
- 681：mali_sc26=0恢复parent2，启动前82写；与678的GPU序列仅root PA不同（不覆盖外部mux）。
- 682：mali_cohnone=1写0x304=0x1f，读回1f；启动前83写=681+这一写，仍无store。
- 683：cacheflush/resetonly/cohnone三合一全部生效，89写，仍无store；组合只作收尾，不用于单项归因。
- 684：mali_hwissues=1，四字0xf04/0xf08/0xf0c/0xf00写前和写后全0；启动前86写=681+恰好这四写。
  AUTO前MCU control/status=0/0，AUTO后status=1，JOB/GPU/MMU IRQ=0；debug_top由4变1b，DISABLE后仍1b。
  46.748776s witness三次均ff5aa5f0,0。只说明四写未恢复可见store；本轮resetonly=0，reset后补四写的组合仍未验证。

## 当前源码与产物

源码 /home/lizi/IOS/hakios-mtk/mtk/mt6895/drivers/gpu/hakios.mali610.c
681–683 driver=a918401ea06da499b428ba38ac65c6e22dd3bf6f38df1f8acd55ce1fcf91b484
681–683 kernel=abd289518ee8f4e96ae87ab4c1b5b31c84e3ba5b4bf16fb3f699b72650526a49
684 driver=e9300e29bcc0c41b8d28f4644e3c8f1ef74ff5982c6495ea7e3a8a21e4822aab
684 kernel=2c31b71aea82aef777ca4b44a442cc8ab77f958d7476e3b39cd0bdf0a9d5b532
684 payload=6b95a459cc8c5cc2d08901df38d4a8bd36c8fe6fcabe00e6844c1b30e62d46c0
684 image MD5=cdd641286e77be61f973fa55da843d6c
mali_hwissues 默认0，内联于可选early reset成功分支之后、as_park_unused之前，打印前后四值。
本地raw/runner/loader证据在out/round-8-support；新源码快照、镜像、构建日志在out/round-12-support。
报告out/round-12.md；发布docs/GPU-ROUND-12.md及docs/round-12-support，包含接手前681–683的补齐证据。

## 下一步

依主控未试变量表：先独立验证STACK_PWRON_LO(0xe20)及STACK_READY_LO(0xe10)，再mali_mcuwin=1全窗口只读。
这些动作尚未执行；不要先直接写语义未知的0x3020，不要把多个新开关一起打开。
每条结论保留witness判据；缓存、总线、时钟的全部可能性没有被穷尽排除。
