2026-11-13 · Round 9：完整映射 witness 的缓存与 vendor 叶属性对照

本轮完成 673、674。两轮都未观察到 witness 首个 store；均已自动恢复原厂 boot_a，Android boot_completed=1，boot_a MD5=`a63b2e35a7c2522cb24a6a10332b9bc8`。没有运行 mali=6，没有访问三个禁读地址。

**实验结果**

| trial | 变量与对照 | 实际生效证据 | 结果 |
| --- | --- | --- | --- |
| 673 | 相对 672，只加 `mali_fwnc=1`；内核仍为 `c262afdb…`，完整映射、post-L2、witness | 参数每项 strings 计数=3；向量和代码段改为 memattr index5；coverage fully mapped；reset=`0x01000001` | 45.417768 秒三次读回均 `0xff5aa5f0,0`；单独改变 vector/code 缓存策略不足以使 store 完成 |
| 674 | 新增受控 `mali_ptvendor=1`，按所给非一致 kbase 去掉 SH/NG；未开 fwnc；保持 post-L2、witness | 10 个段打印 vendor leaf；code 低属性 `0x4c3`、shared `0x457`；实际 walk 也相符；coverage fully mapped | 46.809074 秒三次读回仍 `0xff5aa5f0,0`；该叶策略单独不足以解决问题 |

673 镜像 MD5=`a8901ce2956ab4f2a4336b47da18cc73`，驱动仍为 `e77e7cfa…`。
674 镜像 MD5=`9d65f20dd947070b75c147db020973df`；驱动 SHA256=`1107f51fc077cee692ebaefb01bac513bb8e324ca8b68998559164ea78965bcb`；内核 SHA256=`3391fc9a61107f81b5df17818cd7015923cb04f8c83c66b87b744f38d7a7249b`。

674 与 672 不是同一二进制内核：新内核加入了页策略开关和启用日志，保留默认策略；不把它描述成同内核 A/B。新增策略不改变 PA、4K 步进、RW/RO、XN 或 cache index，只控制 SH/NG。

674 的参考是 kbase `convert_mem_flags()` 与 AArch64 `get_mmu_flags()` 的非一致性分支，未擅自加 MGM/PBHA。此前真机 DT 未配置 external manager；当前仍没有证据支持强行加 group6。

**日志旧尾部：本轮修正的取证问题**

674 的完整 4198400 字节原始 logbook 中，前段是 674，但从文件偏移 **0x60000** 起的连续 4K 块与 673 保存文件相同，尾部还包含 `trial 673 after AUTO` 和 `shared probe trial 673`。这说明读取完整预留区也可能带入历史残留，不能把全部匹配行归给当前轮。

本轮分析器已经：

- 保留原始完整文件及 SHA256，不按最终 header 的 79 字节截断。
- 遇到当前 trial 标识后，再遇到其他 trial 标识时，保守截取到该外来标识所在 4K 页之前；674 的界限为 0x60000。
- 把 `conservative_prefix_end`、`foreign_suffix_trial` 写入 parse JSON；不使用旧 673 的失败尾部作 674 的调用路径证据。

674 的新 trial 标识位于 0x5e517；版本 word 位于 0x5f6c9，witness 三次读回位于 0x5f90d，均在保守界限之前。10 个 vendor leaf、82 条写轨迹也位于该前段。因此本轮的页属性生效及 witness 没有标记的结论仍有当前轮证据支撑。

残留具体由“本次正文较短”还是“后续块未成功覆盖”造成，当前不能只凭这个文件确定。后续将先备份并清理专用 logbook 预留区，并给关键输出附 trial 标识；不再依赖未标识尾部判定缺失调用。

**新找到的具体源码缺项：MFGSC mux 没有 update strobe**

这是对旧“scclk 已排除”前提的收窄，不是声称已找到 MCU 根因。权威文件均为 xaga 的本地 vendor 内核：

- `drivers/clk/mediatek/clk-mt6895.c:32`：CLK_CFG_UPDATE=0x4；142 行：`TOP_MUX_MFGSC_REF_SHIFT=20`。
- 同文件 1243–1247：MFGSC 三个 parent 分别为 26MHz、univpll_d6、mainpll_d5_d2。
- 同文件 2279–2283：mfgsc_ref_sel 需要 CFG5 bits1:0、gate bit7、UPDATE bit20。
- `drivers/clk/mediatek/clk-mux.c:44–63`：vendor enable 先 clear gate，再写 update；57–60 行明确指出恢复后的 mux 可能尚未生效，需要 update 确保设置生效。

相对地，HakiOS 的 `pll_300mhz()` 改 CFG5 parent、可选清 gate 后，没有写 UPDATE bit20；其中“CFG5 没有自己的 update bit”的旧注释与上述源码矛盾。旧 trial 630 虽读回 CFG5=`0x01010102`，只证明配置寄存器字段发生变化，不能证明完成了 vendor 的 enable/update 序列；且当时厂商固件入口仍有页表空洞。

源文件指纹和相关行已保存在 [sc-clock-source.json](round-9-support/sc-clock-source.json)。该缺项现在值得独立验证：保持本轮 vendor 页策略不变，在已执行过的 PLL 路径内补 gate+UPDATE bit20，并打印 trial、parent、CFG5 和实际提交动作；随后仍以 witness store 标记为判据。若 parent2 仍无改善，再单独改 parent0=26MHz 检验 parent 依赖。尚未把这些计划写成已完成实验。

**产出与边界**

仓库收录 `docs/GPU-ROUND-9.md`、新增受控页策略源码、673/674 的 build/参数计数/元数据/当前轮文本/写轨迹和时钟源码依据。完整原始日志继续保留本地 `out/round-8-support/`；分析器同时保留 670–674 的对比，674 已排除已识别旧尾部。

结论仍是“MCU 最小程序尚未完成可见 store”，不是“所有内存/时钟因素都排除了”。页表覆盖修复保持有效；版本为零越界及全零 CSG 的旧假设已由源码和真机固定偏移读回否证。下一步优先验证已找到的 mux 提交缺项，不补厂商 trace，不直接刷 mali=6。
