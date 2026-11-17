# Trial721 主机交付源码与审查证据

本目录归档通过 root 主机复审的 revision2。没有 trial721 设备查询、刷写、启动或硬件结果。

`source/runtime/` 保留实际集成的 7 个文件及原相对 include 布局；`source/kernel-integration/` 保留 3 个集成文件和相对 R20 的完整源码补丁。`source/loader/` 保留本轮相关 loader 源码及唯一返回前缀修正。此目录是源码证据快照，完整构建还需要既有工程、工具链和本地生成资产。

`metadata/ORIGINAL-FROZEN-INPUTS.json` 是原始清单的逐字节副本，包含 5227 项 R21 与 5220 项 R20 输入 hash。`LOCAL-R2-FROZEN-MANIFEST.json` 是本地完整产物库存的逐字节副本，其镜像、失败尝试及原始记录仅保存在本地，不表示这些文件一并发布。仓库实际交付内容由本目录 `SOURCE-MANIFEST.json` 单独列明。

`review/ROOT-REVIEW-721-R2.md` 与 JSON 是本轮独立审查结论。root 独立复现 loader 三阶段，全部 rc=0 且与 DSH 产物逐字节相同；包结构检查通过，新验包脚本实际 50 项 PASS。kernel 复用原 721-D 产物，未在 H1/H2 重建。make 原生 rc=2、无效旧 judge0、原脚本真实日志 rc=1、新私有 judge rc=0 分开记录。

`tests/SYNTHETIC-HLOG-FIXTURE.txt` 明确带合成标记，解析结果只能是 FORMAT_PASS_ONLY，原生 CLI rc=1 是该状态的预期返回。它不是硬件证据。此前漏标合成结果与弱解析器已被拒绝，仅其缺陷说明和独立反例摘要在审查报告中保留。

`metadata/DEVICE-PLAN-UNEXECUTED.md` 仅是未来设备工作的草案，不能据此执行；仍需确认实际状态、恢复条件并逐项获得 root 技术授权和操作者释放。成功试验也必须另做原厂 boot_a 恢复，loader 返回 Android 不会恢复分区。

原始镜像、HLOG 二进制、GPT/GUID、参考内容、pre-logbook、设备序列号及既有 GPUEB WIP 均未纳入本目录。测试代码为已审 721-A/B/C v2 的源码副本；本轮未重跑其旧 sanitizer 全套，身份核对与既有阶段结果不能扩大为设备结论。
