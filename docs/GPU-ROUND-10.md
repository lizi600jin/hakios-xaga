# 2026-11-13 · Round 10：MFGSC 提交实验与原厂实际 PTE

本轮关注“映射修复后，MCU 为什么仍未完成可见 store”，不以 level 6 为唯一问题。用户已明确授权手机操作与刷机；所有实验保持 `mali=5`。未访问三个禁读地址。

## MFGSC 的源代码缺项已验证，尚未解决 MCU 问题

675–677 使用同一内核 `a5c04ceb6fbda38ffbfb797eee0883d6a7d40d91f0e69df451a3824068a57056`，驱动 SHA256=`59147207c289989b084407e2f10dbf37f1f265a0cd5439dee23b2119b93dea36`。保持完整 4K 映射、post-L2 AS0、vendor SH/NG、witness。每个 boot-arg 经实际 loader strings 验证均出现 3 次。

| trial | 相对变化 | 实际路径日志 | witness 三次读回 |
| --- | --- | --- | --- |
| 675 | 同一新内核的 gate-only 对照 | `update20 0 parent 2 gate 0 cfg5 0x1010102` | 46.581328 秒均 `0xff5aa5f0,0` |
| 676 | 相对 675 仅加 `mali_scupd=1` | `update20 1 parent 2 gate 0 cfg5 0x1010102` | 47.106087 秒均 `0xff5aa5f0,0` |
| 677 | 相对 676 仅加 `mali_sc26=1` | `update20 1 parent 0 gate 0 cfg5 0x1010100` | 48.029926 秒均 `0xff5aa5f0,0` |

gate 清零、UPDATE bit20 的写路径和 parent 选择均已送达。日志不能测出 mux 输出的真实频率；能够确定的是，补齐这组 vendor 操作后，仍没有 witness 标记。不能再把漏 update 单独称作已证实的启动根因。

新增开关全部默认关闭，保留原基线：`mali_scupd` 清 gate 并写 update；`mali_sc26` 仅在 scupd 启用时选择 parent0。改动内联于已经输出日志的 PLL 路径，关键 AS、version、witness 行追加 trial ID。

权威时钟依据见上一轮 [sc-clock-source.json](round-9-support/sc-clock-source.json)：vendor `clk-mt6895.c:142,1243–1247,2279–2283` 和 `clk-mux.c:44–63`。没有据寄存器字段读回就声称频率已经实测。

## 原厂 Android 实际 PTE：不支持盲加 PBHA=6

stock Android `boot_completed=1`、boot_a MD5=`a63b2e35a7c2522cb24a6a10332b9bc8` 时，使用专用 tracefs instance，对三个已知 CPU 函数安装 return probe。只取 `$retval:x64`，没有解引用 CPU 指针、物理内存或 MMIO。方法遵循 [Linux 5.10 kprobe 文档](https://www.kernel.org/doc/html/v5.10/trace/kprobetrace.html)。

第一次用 screencap readback 触发，没有事件，记录为无结论。第二次用所给 vendor UAPI 编译的 AArch64 小程序，创建自己的 Mali context，分配/映射 4 次各 16 页的 CPU/GPU RW 缓冲区，只向自己映射的首字节写 `0x5a`，随后 unmap/close；没有提交 GPU job。四次分配均成功，CSF API 协商结果为 1.18。

| CPU 函数 | 捕获事件 | 结果 |
| --- | ---: | --- |
| `kbase_mmu_create_ate` | 0 | 不能用零事件断言函数未参与；可能被内联 |
| `kbase_native_mgm_update_gpu_pte` | 64 | 全部 PBHA bits62:59=0，低 12 位=0x443 |
| `example_mgm_update_gpu_pte`（external mali_mgm） | 0 | 本次分配的 PTE 走 native 回调 |

样本物理地址范围 `0x1018bb000–0x219dae000`，在 4GB 以上；64 条记录有 16 个不同 PTE，重复分配复用了页，不能当 64 个独立内存来源。`0x443` 与普通 cached RW、AF、page 的 vendor 编码一致。该样本是用户缓冲区，**不是 firmware AS0 全表，也没有直接证明 firmware 所有映射的 PBHA**。

所给源码 `mali_kbase_native_mgm.c:149–156` 原样返回 PTE；`mali_kbase_mem.c:1262–1272` 无 DT external manager 时使用 native。external `memory_group_manager.c:358–379` 确实可能加入 group6，但“mali_mgm 模块已加载”不能替代真实调用证据。本次运行证据支持继续保留 PBHA=0，降低“应该强行加 group6”的优先级。

专用三组 probe 和 instance 均已删除，每个 cleanup 返回码为 0，并复核事件表/instance 没有本轮名字；Android 仍 `boot_completed=1`。原有 tracing 用户未被关闭。代码、返回值 trace、JSON 和 helper 的 SHA256 一并保存。

## logbook 可信度与 Android 返回恢复

675 起，在每轮开始时先备份专用 logbook 窗口，再只清零该窗口并回读校验：xnusys 总长 `2685370368`；起点为分区末尾减 8MiB，即 `2676981760`；清理长度 `4198400`。脚本断言精确位置和上限，未清理 xnusys 文件系统区域。原始备份与实验完整日志留在本地，仓库收录 SHA256 与精简文本。

这解决了 674 暴露的旧尾部混入问题；675–678 的当前轮分析未发现 foreign trial suffix。仍不按最终 header 的短长度截断正文。分析器继续保留“遇到外来 trial 即保守舍弃所在 4K 页及之后”的防线，MCU 窗口续行也服从同一截取边界。

用户照片对应 XNU 返回 Android 的阶段。672 的 Android dmesg 已出现原厂 Mali firmware boot timeout/GPU reset timeout，因此不能只归为画面没刷新。新 runner 在 root adbd 回来后立即拉日志，不再等待 `boot_completed` 才取证；超时则写回已验 MD5 的 stock boot_a，fsync、分区 MD5 复核、sync 后用内核 sysrq restart。675–678 都成功恢复到原厂 Android，`boot_completed=1`，分区 MD5 均为 `a63b2e35a7c2522cb24a6a10332b9bc8`。

## 尚未完成的因果判定

旧注释把 AS UPDATE 后 `0x10000` 解释成 root fetch bus error，证据不足。修复前后，park 为 unmapped 的 AS2–7 也在 UPDATE 后分别出现 `0x40000…0x800000`，而对应 fault/status/address 为零。早期 `round-1.md:192` 已将这组高位记为 REQ_COMPLETE；仅凭 JM 头文件同位宏名不能把 CSF 的读数反推成 root 访问失败。

因此，不再沿用“高内存已证实不可达，所以只能固定用 0x50000000”的前提。**678 已实测修复后的 `mali_low=0`**，相对 677 只改该参数，内核/驱动相同。AS0 root=`0x101dec000`，GPU readback 与软件 walk 相符；vector/shared 的 L3 分别为 `0x40000101db04c3`、`0x400001013e8457`，完整覆盖。47.685492 秒 witness 三次读回仍为 `0xff5aa5f0,0`；未观测到首个 store。其余 fault/status/address 仍为零。故切换到本次高内存分配路径未解决问题；该开关同时改变物理分配位置及 CPU 缓存方式，不能把它视为纯地址变量。

后续若高内存仍无标记，优先隔离 GPU cache/PTW 失效、复位生命周期与输出可见性，再决定是否进入完整厂商固件初始化。witness 未完成可见 store 时，不把 trace buffer 缺项提升为唯一解释。未改变哨兵不能严格证明零指令执行；源码里的旧强断言与 stdout verdict 应按这一证据边界理解，后续修订时一并纠正。

本轮内核构建通过仓库 make-kernel 的错误分类门：仅接受既有 kext symbol-set 错误，未出现新的未分类编译失败；payload/loader/pack 完成。不是完整 XNU 配置零错误，也没有只凭旧文件存在认定构建成功。

仓库产出为本报告、精确匹配已构建 SHA256 的驱动源码、`round-10-support/` 的 675–678 元数据/参数计数/当前轮日志/写轨迹与原厂 PTE 样本，并更新既有 runner/analyzer。678 镜像 MD5=`c44bb0499dd1237739bb8e419ec4af82`。完整 4MiB logbook、前次备份及镜像留在本地；没有把旧尾部、预备镜像或零事件采样误报为成功启动。
