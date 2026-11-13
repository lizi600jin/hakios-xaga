2026-11-13 · Round 8：授权后的真机基线、AS0/L2 顺序与完整映射 witness

用户已明确授权操作手机和刷机。本阶段完成 trial 670、671、672；截至 13:10:54，手机已恢复原厂 boot_a，`sys.boot_completed=1`，boot_a 读回 MD5 为 `a63b2e35a7c2522cb24a6a10332b9bc8`。三个禁读地址未由实验探针访问；没有运行 mali=6。

**结论**

- 修复后的 shared GLB/CSG 模板已经真机读回：GLB input/output=`0x04002000/0x04003000`，CSG0=`0x04004000/0x0400c000`；完整跨度检查通过。三轮 coverage 均为 fully mapped；version 保持 `0xff5aa5f0`，启动 JOB IRQ 未到。
- **AS0/L2 顺序单变量无效**：670 先 UPDATE 后 L2，671 先 L2 后 UPDATE；两轮都是 82 次 GPU 写，写入 `(offset,value)` 多重集合完全相同，仅顺序不同。MCU 启动结果不变。
- **完整映射下 witness 仍未完成首 store**：672 的 reset 已读回 `0x01000001`，三次复读均为 `0xff5aa5f0,0`。这支持继续查取指/翻译/存储路径；不支持把问题仅归到厂商 trace 或全局接口初始化。未观察到首 store，仍不能严格证明首条指令没有执行。
- **mali=5 也能在回 Android 时卡住**，与 global_interface 调用无关。三轮均明确走到失败尾部、跳过 global_interface；用户照片显示停在 starting Android，随后捕获的 Linux 日志确认 Android kbase 自身也启动固件失败、GPU reset 超时。
- 补查完整日志发现，**AS2～AS7 的未映射空间 UPDATE 后也有对应上半位**，663 旧日志已有同样现象。因此 round-7 中把 `0x10000` 称为 AS0 配置异常仍应降级：它不是根表读取失败的充分证据；暂保留原始值，不能仅按旧 JM 宏名断言硬件 bus error。

**可复核的实验控制**

源文件：`tree/mtk/mt6895/drivers/gpu/hakios.mali610.c`；工作副本同名文件位于 `/home/lizi/IOS/hakios-mtk/mtk/mt6895/drivers/gpu/`。

三轮的驱动 SHA256 都为 `e77e7cfa4f9817e3d6dbdbe0a0481f96972b2b71c47a7390cc3b1c88312ddf51`；内核均为 `c262afdb4f0a719a74cd1e6ef1e262b6c49126529ce29690b271b2a627a097fe`；payload 均为 `788893a3b2673fcfa6794e898f325b3a52d96003ce147af9ae58ec936d46deeb`。

本阶段驱动只增加内联打印，不改启动控制字或页表：

- 在已输出的 AS_STEP(update) 后打印 MMU RAW、AS_STATUS、FAULTSTATUS/ADDR、GPU_FAULTSTATUS（当前源码 2894–2899）。
- 在已输出的 version word 路径内、DISABLE 前读取已分配 shared 的固定 GLB/CSG 偏移（当前源码 3977–3995）。没有跟随未知地址，没有新增独立探针函数。

同步驱动后构建，`make-kernel.sh` 对构建日志判定通过；只有本仓库既有 kext symbol-set 检查失败，未忽略其他编译/链接错误。随后在产出内核中检出探针字符串，并由三轮真机日志证明实际执行。

loader 参数经 `strings -a mtk/loader/mtkloader.bin` 逐项检查，**每项出现恰好 3 次**，不只检查参数存在。镜像、loader、payload、内核和源码哈希分别记录在 `trial-N-build.json`；刷前再次按这些哈希校验文件。

共同 Mali 参数为 `mali=5 mali_cold=1 mali_low=1 mali_trace=1`，共同 `loader_return_after=150`；trial 标识用于关联证据。

| trial | 唯一实验变量 | 镜像 MD5 | GPU 结果 |
| --- | --- | --- | --- |
| 670 | 基线：`mali_preas0=1` | `72d68191ace13550004b207f4c8eb332` | 版本哨兵不变，无启动 IRQ，global_interface skipped |
| 671 | 相对 670：`mali_preas0=0` | `f8e8559acddc287056f797968f303d96` | 同基线 |
| 672 | 相对 671：增加 `mali_witness=1` | `806efb4c9253ab1aa39f0fb3db304c18` | 入口改到 `0x01000001`，三次首两字读回仍为哨兵/0 |

**AS0/L2 对照的原始时间点**

| 事件，单位秒 | 670 | 671 |
| --- | --- | --- |
| L2 up | 37.643482 | 37.455767 |
| AS0 UPDATE | 37.643381 | 37.593804 |
| UPDATE 后 raw/status/fault/addr/gpu_fault | `0x10000/0/0/0/0` | `0x10000/0/0/0/0` |
| 版本 word | 46.986278：`0xff5aa5f0` | 46.567753：`0xff5aa5f0` |
| 写次数 | 82 | 82 |

`order-comparison.json` 对保存的实际写序列进行多重集合及顺序比较：`same_write_multiset=true`，`order_identical=false`。这证明该控制变量真实送达，不是只换了 loader 字符串。

**完整映射 witness 的原始证据（672）**

```text
37.792402 coverage: every section fully mapped
37.864579 witness: code at 0x1000000, reset vector now 0x1000001
46.627133 version word 0xff5aa5f0 (untouched; no firmware version published)
46.637376 witness: read1 0xff5aa5f0 0 read2 0xff5aa5f0 0 read3 0xff5aa5f0 0
48.779912 shared probe trial 672: completed; returning before global_interface
```

witness 不发布正常固件启动 IRQ，不能用 start_mcu 超时本身判 witness 失败；这里的判据是实际 store 标记没有出现。GPU 侧 code/vector 缓存以及最初 store 的总线可达性仍需区分。

**`0x10000` 的证据边界**

663 和 670 完整日志都记录：

```text
AS2 parked, mmu irq 0x40000
AS3 parked, mmu irq 0x80000
AS4 parked, mmu irq 0x100000
AS5 parked, mmu irq 0x200000
AS6 parked, mmu irq 0x400000
AS7 parked, mmu irq 0x800000
```

这些空间的 TRANSTAB 为 0、TRANSCFG 为 unmapped；说明对应上半位不限于有真实固件根表的 AS0。它们可能与配置命令完成有关，具体含义尚未由 CSF 对应寄存器定义证实；也不能仅据此排除更广泛的配置/互联问题。后续以原始寄存器与可观察执行结果为主，不再把这个位独立当作 root PA 读失败证明。

**Android 回切与恢复**

670：adbd 曾出现，但用户观察到屏幕停在 starting Android，干预前未观察到 boot_completed=1；用户手动进入 fastboot 后恢复 stock。原来的自动脚本只在 boot_completed=1 后取日志，因而没有及时利用可工作的 adbd。本次已改为 adbd 恢复就先保存完整 xnusys 日志。

671：正常 `adb reboot bootloader` 在半启动状态下超时。日志已保存后，经 root shell 将原厂备份写回 **boot_a**，`dd conv=fsync`、sync，读回 MD5 一致，再用 `/proc/sysrq-trigger` 的内核重启恢复；13:05:16 确认 stock boot_completed=1。没有格式化分区或清除用户数据。

672：同样未在 adbd 出现后的 45 秒观察窗内达到 boot_completed=1。已自动执行相同 stock 写回、校验、同步和内核重启；13:10:54 确认 stock boot_completed=1、MD5 一致。这个观察窗不证明 Android 永久不能启动；以下内核错误则提供了具体失败证据：

```text
[20.680365] mali 13000000.mali: Timed out waiting for fw boot completion
[25.860761] JOB_IRQ_RAWSTAT=0 MMU_IRQ_RAWSTAT=0 GPU_FAULTSTATUS=0
[26.372779] Version check failed. Firmware may have failed to boot.
[31.494172] Timed out waiting for the GPU reset to complete
[31.500021] Firmware initialization failed
```

这是 **返回后的 Linux 驱动日志**，不能与 XNU 中的启动尝试混成一份成功/失败状态。它证明回切时 GPU 初始化有实际错误，尚不能单独证明错误由哪一个 XNU 步骤造成。打包底图 `boot_a_magisk.img` 与 stock_real.img 的 MD5 相同，已排除这两份底图实际不同的解释。

**下一步优先级**

1. 同内核、同 post-L2 witness 基线上，只增加 `mali_fwnc=1`，检验 vector/code 的 GPU 缓存策略。trial 673 已准备，尚不把结果写作已验证。该实验不同于坏映射下的旧厂商固件试验。
2. 若仍无 marker，再区分 GPU cache/PTW 内容交接与精确 vendor 页属性；需要明确单变量，不能直接用会改变多处行为的 mali_cold=0 代替。
3. 暂缓 trace 缓冲区补全：最小 witness 都未完成 store，先为执行/存储路径取得正向证据。
4. 已离线核查 MGM/PBHA 作为线索：外部 manager 的确可能在 bits62:59 添加 group（memory_group_manager.c:358–379），但真机 GPU DT 节点没有 `physical-memory-group-manager` 属性；所给 kbase 的 `mali_kbase_mem.c:1262–1272` 会回退 native，native 的 149–156 行原样返回 PTE。因此不凭 mali_mgm 模块加载记录就盲目加入 PBHA=6。

完整原始 logbook（每份 4198400 字节）和完整 Linux dmesg 保留在本地 `/home/lizi/IOS/work-xaga/codex/out/round-8-support/`，哈希见元数据。仓库只收录源码、脚本、写轨迹、关联元数据与相关文本；Linux 日志筛选为 GPU 相关行。先前会话未刷入的 trial-670 预构建说明已另存 `trial-670-preauthorization-build.json`，不与本轮真实刷入结果混用。
