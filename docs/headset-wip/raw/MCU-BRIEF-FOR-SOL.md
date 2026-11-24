# Mali-G610 MCU 不启动 —— 需要你的判断

## 现状

GPU 硬件已经认出来了，电源、时钟、固件、页表、reset vector 全部正确，
但 **MCU 启动请求发出后不执行**。

## 已确认的数据（真机实测）

```
GPU_ID        0xa8670004            ← Mali-G610
CSF           0x40a0412
L2 features   0x7130306
Shader        0x130013
MMU           0x2830 (va 48 bits, pa 40 bits, address spaces 0xff)
tiler 0x1  l2 0x1  stack 0x33  thread_max 0x800
固件           258048 bytes, header 0.1, 10 个 section
域             7 of 12 answered
```

## 启动序列走到哪

```
mali: the whole block settled, so power and clocks are not the problem
mali: firmware 258048 bytes, header 0.1, entry table ends at 768
mali: 10 sections, 9 page tables
mali: coverage: every section fully mapped
mali: reset vector: sp 0x2040000  pc 0x10018ed
mali: entry words at VA 0x1000000: 0x10019b1 0x10019b1 0xd00cf8df 0xf832f002
mali: firmware loaded; enabling the address spaces
mali: AS0 enabled: root 0x50000000 transcfg 0x42000006
mali: address space 0 is live; starting the MCU
mali: sentinel 0xff5aa5f0 written to the version word
mali: the start loop ended after 1000000 spins (halt seen 0, fatal seen 0)
mali: MCU status 1 (raw state 1; boot unproven), job irq 0, gpu irq 0
mali: version word 0xff5aa5f0 (untouched; no firmware version published)
mali: the global interface never raised; the MCU did not boot
```

**关键点：**

1. 固件在内存里是对的（入口字 `0x10019b1` 是真实 ARM64 指令）
2. 地址空间 AS0 启用了
3. `MCU_CONTROL` 写了 `AUTO`（值 2）
4. `MCU_STATUS` 一直是 1，`halt seen 0, fatal seen 0`
5. **版本字哨兵 `0xff5aa5f0` 没被固件改掉 —— MCU 完全没执行**
6. `debug_top 0x1b`（bit 3/4 置位）—— 总线忙
7. gpu/job/mmu irq 全是 0 —— 一条中断都没来

## 驱动里已有的实验开关（35 个）

相关的几个：

```
mali_mcuseq=1      在 AUTO 之前先写 ENABLE
mali_doorbell=1    AUTO 之后敲一次 doorbell 0
mali_gpueb=1       映射 GPUEB 块（默认不映射，因为时钟可能没开）
mali_rpc=1         读 mfgrpc
mali_mcuwin=1      看 MCU 窗口
mali_statussem=1   状态语义
mali_asmorder      地址空间顺序
mali_as0first      先起 AS0
```

**这些开关说明前面已经试过一些组合，但我不清楚试过哪些、结果如何。**

## 我需要你判断的

1. **Mali-G610（CSF 架构）的 MCU 启动，`MCU_CONTROL` 的正确写法是什么？**
   现在是写 2（AUTO）。要不要先写 1（ENABLE）再写 2？还是别的？

2. **`debug_top 0x1b` 意味着什么？**
   bit 3/4 置位，总线忙着，但驱动在等 MCU 状态位。是不是 MCU 在取指但卡住了？

3. **一条中断都没有，是正常的吗？**
   CSF 的 MCU 启动后应该通过 JOB IRQ 报告。如果中断路径没通，驱动会误判。

4. **`mali_gpueb=1` 值不值得试？**
   驱动注释说"GPUEB 在自己的时钟门后面，读一个时钟关着的 MTK 外设会挂死"。
   GPU 上电后这个门是不是已经开了？

## 环境

- 设备：红米 Note 11T Pro（xaga），MT6895Z / 天玑 8100，Mali-G610 MP6
- 内核源码：`/home/lizi/IOS/hakios-mtk/logs/ufs-apfs-fstree-root-736/kernel-src/`
- 驱动：`bsd/dev/hakios.mali610.c`（5221 行）
- 改 `boot-args` 必须重编 loader（`coordinator/loader-build.json` 改完不自动进 loader）
- 一轮刷机 200 秒，600 秒窗口可主动 RETURN 提前退出（30 秒回 Android）
- 现在有 `/dev/hlog` 实时日志，`hakid` 远程执行，不用等窗口结束

## 已经知道的教训

1. `mali=0` 是默认（驱动直接 return），`mali=6` 是最高档
2. **第一轮冷启动 `GPU_ID` 读到 0**，八个域全失败
3. **第二轮 `GPU_ID` 直接是 `0xa8670004`** —— loader 已经上电，XNU 接的是活的 GPU
4. 所以冷启动路径和热路径结果差很多，值得分开看
