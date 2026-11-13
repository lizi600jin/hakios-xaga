# 2026-11-13 · Round 11：分离 cache 与 early reset，校验 witness 字节

本轮在用户持续授权下操作真机，仍为 `mali=5`，未访问三个禁读地址。主问题仍未解决，不能因缓存操作完成就宣称 MCU 已启动。

## 679：GPU cache 操作确实完成，仍无可见 store

新增默认关闭的 `mali_cacheflush=1`，在已经输出过 GLB_REQ/AUTO 日志的 start 路径内，所有 CPU 固件/向量/witness 修改之后、AUTO 之前，内联执行 GPU `0x3304` 操作并轮询 RAW completion bit17。没有新增一个无法确认调用的 kernel 探针函数。若不完成，明确记录并在 AUTO 前停止；这种结果不能当作 MCU 执行失败。

679 保持 678 的完整映射、post-L2、high allocation、vendor SH/NG、MFGSC parent0/gate/update、witness，只启用上述 cache 动作：

```
38.326469 mali: cache trial 679: cmd 0x3304 spins 1 raw 0x20000 completed 1
47.567800 mali: witness: read1 0xff5aa5f0 0 read2 0xff5aa5f0 0 read3 0xff5aa5f0 0 trial 679
```

启动前 GPU 写记录为 85，较旧基线增加 clear/command/clear 三次，AUTO 仍在 trace freeze 之后。仅将不同物理 root 的 TRANSTAB LO/HI 值归零作对齐后，678→679 的完整写序列差异正好是末尾 `0x24=0x20000,0x30=0x3304,0x24=0x20000`，见 write-diff JSON。cache completion 证明该命令已完成，不直接证明 MCU 取指、PTW 内容或 DRAM 访问已正确。

679 与 678 使用不同二进制内核；不标成同内核 A/B。679 的源码 SHA256=`33ba53d8d6c2774e4d5b36790e4a07c4e3849e72296678609e5ee2a10bae13bf`，内核=`1570b909195beabfd346c0adc5e41049d9f52817b2630a4eeb3d9f6c61d78557`，payload=`605f00f400601cf5200363b6a982b8fd3b9bb004e59e49380f368a988582a229`。编译通过既有错误分类门；新增开关默认关闭，并修正部分历史 AP、IRQ、映射和 witness 注释/输出。679 镜像 MD5=`15af95df2fc07577d7da571c9a9008ea`，参数实际 loader strings 计数均为 3。

## 初次固件装载之前的 PM 生命周期

不能把从 AS0 配置起的 Android history，当成覆盖了整个驱动初始化过程。所给 kbase 源码的顺序是：

1. `device/backend/mali_kbase_device_csf.c:69–83`，backend late init 调用 `kbase_hwaccess_pm_powerup(...PM_HW_ISSUES_DETECT)`。
2. `backend/gpu/mali_kbase_pm_backend.c:571–591`，powerup 调 `kbase_pm_init_hw`；该路径注释明确是 startup/insmod。
3. `backend/gpu/mali_kbase_pm_driver.c:2892–2940`，disable IRQ/snoop、protected-mode disable/reset、硬件 issues apply、设置 system coherency。默认 protected ops 在 `mali_kbase_core_linux.c:4374–4405` 回到 `kbase_pm_protected_mode_disable`。
4. `mali_kbase_pm_driver.c:2858–2867` 调 `kbase_pm_do_reset`；2672–2728 的实现有平台 soft-reset callback 分支，否则写 GPU soft reset，等 completion，后续还可能 fallback hard reset。不能把 generic 分支无条件说成每个平台的实际选择。
5. `backend/gpu/mali_kbase_cache_policy_backend.c:25–32` 在 COHERENCY_REG feature 存在时写 coherency mode。
6. `device/backend/mali_kbase_device_csf.c:412–437` 明确将 firmware init 推迟到第一次设备 open；其中先 context_active，再调用 firmware init。

Panthor 的 `panthor_panthor_gpu.c:319–323` 也在 L2 上电前设置 coherency 和 L2 config。上述是源代码生命周期依据，未声称已经动态捕获 Android 本次开机的全部初始化调用。

这使“只凭 warm/cold history 没看到 reset/coherency，就认为裸机可以省略”的结论不再成立。与此同时，也不能把 `mali_cold=0` 的 reset、power key、coherency、ASN 和 cache 多变量一起改完，再将成功或失败归给其中一项。

## 680：early soft reset 完成，仍无可见 store

680 与 679 使用同一内核，但测试的是另一项动作：`mali_cacheflush=0 mali_resetonly=1 mali_cold=1`。该分支在 AS/L2 配置之前执行已有 soft-reset 实现，保留 cold=1 对 power key、coherency、ASN、旧 cache 路径的条件；没有把整个 cold=0 分支一起打开。不能将 679→680 描述为只变一个开关，比较基线仍是 678 的语义路径，二进制差异已说明。

```
38.373939 mali: reset trial 680: early soft reset, resetonly 1 cold 1
38.375375 mali: soft reset done: status 0
47.965704 mali: witness: read1 0xff5aa5f0 0 read2 0xff5aa5f0 0 read3 0xff5aa5f0 0 trial 680
```

680 coverage 完整，AS0 的 status/fault/address/gpu_fault 均零，启动前 GPU 写 85。该 soft reset 单独不足以让 witness 写入；它不等于验证完整 kbase 初始化序列。680 镜像 MD5=`8cbbbba129822e81fed8ad88503bfd5e`；关键参数 strings 计数均为 3。

按同样的 root-PA 归一化，678→680 仅在序列开头增加 `0x24=0x100,0x30=0x101,0x24=0x100`；没有额外 power-key/coherency/ASN/cache 写。两轮都已自动写回原厂 boot_a 并完成重启，`boot_completed=1`，分区 MD5=`a63b2e35a7c2522cb24a6a10332b9bc8`。logbook 专用窗口备份/清零/回读校验沿用 round10 流程，当前日志无外来 trial 尾部。

下一项优先读取并对照已定义的 COHERENCY_FEATURES/PROTOCOL，在 L2 上电前独立验证 noncoherent 配置。cache 与 reset 的各自负结果不能证明二者组合及 coherency 已正常，不能写成“所有缓存/复位问题都排除了”。

## witness 指令离线验证

`emulate-witness.py` 从本轮驱动的实际 `kMaliWitness[32]` 提取字节，不重新拼装一份替代程序。Capstone 5.0.7 完整解码，Unicorn 2.1.4 使用 Cortex-M7/Thumb M-class 模型，在 40 条指令预算内得到：

| 顺序 | 目标 VA | 宽度 | 写值 |
| --- | --- | --- | --- |
| 1 | 0x04000000 | 4 | 0x11 |
| 2 | 0x04000004 | 4 | 0x22 |
| 3 | 0x04000000 | 4 | 0xdeadbeef |

最终两个字为 `0xdeadbeef,0x22`，随后停留在尾部自循环，未使用 stack。字节 SHA256=`12f91ecda337f305eb22f00af7774ce331e5ddeffc0917dee60f966f96b5d814`。这排查了立即数、字节序、Thumb 指令和目标地址拼装；它**不模拟 Mali MMU、cache、NVIC、时钟或实际 reset/startup**。不能把离线结果当成真机已执行，也不能从真机哨兵不变反推零指令执行。

源码的实际失败 stdout 现在为 `no completed first store observed (not an instruction trace)`；历史日志中的 `never executed` 只保留为原始证据，不再按字面扩展结论。部分早期 bring-up 解释性注释仍属历史推测，权威性低于本轮 source evidence 和实际 trial 结果。

本轮发布 `docs/GPU-ROUND-11.md`、精确匹配 679/680 构建的驱动、两轮参数/元数据/正文/写轨迹、PM 源码行与指纹、离线仿真脚本和结果。完整原始 logbook 及镜像留在本地。
