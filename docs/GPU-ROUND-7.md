2026-11-13 · Round 7：4K 映射修复后，MCU 为什么仍未完成启动

本轮仅做本地源码、固件头数组及既有日志审计；没有调用 adb/fastboot，没有构建、打包或刷机，没有访问手机。按用户提供的当前状态，GAJZ4HIN99AQ69Y9 保持原厂 Android；boot_a 为 stock 镜像，MD5 `a63b2e35a7c2522cb24a6a10332b9bc8`。

**先读结论**

- 4K 步进修复在当前源码和当前固件上成立：10 个段共 **357 个叶**；shared `[0x04000000, 0x04094000)` 为 **148/148**，没有第二处 GPU 映射循环按 16K 跳页。16K 表页分配只是多分配空间，不会改变 4K 索引。
- **确定否证“version=0 导致偏移越界”**：H:4048–4050 直接返回；即使 version 非零而两个 VA 为零，H:1341–1342 也会拒绝，不做减法、不形成越界指针。CSG 结构本来就没有 version 字段。
- **shared 第二页不是全零**：镜像预填了完整的 GLB/CSG 控制模板；GLB input/output 为 `0x04002000/0x04003000`，CSG0 input/output 为 `0x04004000/0x0400c000`。真正从零初始化的是 shared 偏移 `0x2000` 以后的 146 页。
- 修复后的 trial 663 在 **AS0 UPDATE 当场仍出现 `MMU_IRQ_RAWSTAT=0x10000`**，AUTO 前又被清除。“AUTO 后 fault=0”不能抹去这个事实。它是待解释的 AS0 配置异常；当前证据尚不能证明错误来自哪一次 GPU 内存读取。
- 两份参考源码的**首次加载**都先开 L2、再激活 MCU 地址空间；trial 663 却是 `preas0=1`、UPDATE 在 L2 前。这比继续追全零 CSG 更值得先验证。
- kbase 确有我们没做的 boot 前 trace 缓冲区分配与指针填写；但写入位置在 **VA 0 的 config 段**，缓冲区在动态 shared VA 中，不是给静态 shared 的 GLB/CSG 凭空填版本号。panthor 跳过 trace 条目，因此这只能作为厂商固件兼容候选，不能直接认定根因。
- 目前可确定的是“未发布版本/未发出启动完成握手”；修复后尚无新的 witness 结果，不能仅凭版本哨兵不变断言“零条指令执行”。

**来源、版本及复现材料**

下文行号均对应本轮读取时的源文件；H 的仓库副本与工作源码逐字节相同。源码中的历史注释只作为线索，结论以实际语句、固件字节和既有日志为准。

| 简称 | 文件 |
| --- | --- |
| H | `/home/lizi/IOS/hakios-mtk/mtk/mt6895/drivers/gpu/hakios.mali610.c`；仓库副本 `tree/mtk/mt6895/drivers/gpu/hakios.mali610.c` |
| P | `/tmp/src-ref/panthor_panthor_fw.c` |
| K | `/home/lizi/IOS/work-xaga/k-xaga-16.2/drivers/gpu/mediatek/mali-valhall/gpu/arm/midgard/csf/mali_kbase_csf_firmware.c` |
| T | 同一 midgard 根目录下 `csf/mali_kbase_csf_trace_buffer.c` |
| A | 同一根目录下 `mmu/mali_kbase_mmu_mode_aarch64.c` |
| R | 同一根目录下 `include/uapi/gpu/arm/midgard/gpu/mali_kbase_gpu_regmap.h` |
| D | 同一根目录下 `mmu/mali_kbase_mmu_hw_direct.c` |

H SHA256：`d37bb795b0e996bfe787e85ab5118d092f640ac0779ce00c30ee19557c0e24d6`。
P SHA256：`344282baeb26af9d59fbbbd997476f0dbdc501abdb0d76b84c701052be194e73`。
K SHA256：`48cda0c20e0f4d86e843a89290fa159c48e1bd90a3f641837ae1e5f0c249764c`。

实际嵌入头文件为 `/home/lizi/IOS/hakios-mtk/drivers/gpu/hakios.mali610_fw.h`。从数组还原得到 258048 字节；SHA256 与头文件声明一致：`de22d2fc06bc8a859968499a6e609c27fe838be062bf5c97d92ea9f904f7229a`。没有生成或修改固件。

复现：`python3 /home/lizi/IOS/work-xaga/codex/out/round-7-support/audit.py`。该脚本只读取本地文件，将模型结果写入脚本所在目录；它绑定上述 H/固件哈希，源码改变后须先更新模型。仓库收录此报告为 `docs/GPU-ROUND-7.md`，材料位于 `docs/round-7-support/`：

- [audit.py](round-7-support/audit.py)、[audit.json](round-7-support/audit.json)：审计脚本、输入哈希、全部段、trace 条目初值、模型覆盖与既有 walk 对比。
- [shared-leaves.tsv](round-7-support/shared-leaves.tsv)：148 个叶的 VA、四级索引、模型 PA/PTE。
- `trial-{608,613,660,662,663}-key.txt`：读取完整既有 logbook 后筛出的关键行；未使用 header len 截断。

这是源码算术模型及既有日志交叉检查，**没有执行 XNU 的 C 函数，也没有证明 GPU 能读取全部模型 PA**。本轮未修改驱动。

**1. load_firmware() 与每一页的覆盖**

H:1048–1053 读取段 flags、VA 起止和文件区间；H:1089 分配 `va_end-va_start`；低内存分配器 H:666–689 按宿主 16K 对齐并清零；H:1094–1099 拷贝文件数据并同步。H:1186–1192 以 `PT_LEAF_GRANULE=0x1000`（H:857）逐页调用 pt_map，PA 每次重新取 `mem_pa(section, va-va_start)`。

固件条目表恰好解析到头部声明的 `0x300`。10 个非空段均 4K 对齐、数据长度未超过段长度、VA 不重叠。模型逐个检查叶类型 `0b11`、AF、精确 PA 和属性，得到：

| sec | VA 区间，右端不含 | 4K 叶数 | 文件字节数 | 低内存模型 PA 起点 |
| --- | --- | ---: | ---: | --- |
| 0 | `0x00000000–0x00001000` | 1 | 2836 | `0x50004000` |
| 1 | `0x01000000–0x01010000` | 16 | 63560 | `0x50014000` |
| 2 | `0x02000000–0x0203e000` | 62 | 180752 | `0x50028000` |
| 3 | `0x0203e000–0x0203f000` | 1 | 0 | `0x5006c000` |
| 4 | `0x0203f000–0x02040000` | 1 | 0 | `0x50070000` |
| 5 | `0x02800000–0x02840000` | 64 | 180752 | `0x50074000` |
| 6 | `0x03000000–0x0303e000` | 62 | 0 | `0x500b8000` |
| 7 | `0x0303e000–0x0303f000` | 1 | 0 | `0x500fc000` |
| 8 | `0x0303f000–0x03040000` | 1 | 0 | `0x50100000` |
| 9 | `0x04000000–0x04094000` | **148** | **8192** | `0x50104000` |
| 合计 | 10 段 | **357** | | 9 个页表 |

shared 的计算不依赖自检日志：`(0x04094000-0x04000000)/0x1000 = 0x94 = 148`。循环实际访问 `base+n*0x1000`，`n=0..147`；末页 `0x04093000–0x04093fff`，不会漏掉最后 4K，也不会写 end 地址。整个 shared 仍在同一张 L3 表中。

模型首叶：PA `0x50104000`，PTE `0x0040000050104e57`。
模型末叶：PA `0x50197000`，PTE `0x0040000050197e57`。
厂商入口 `0x010018ed` 对应叶起点 `0x01001000`、模型 PA `0x50015000`，现在也有有效、可执行的只读叶。

trial 663 的 `walk 0`、`walk 0x1000000`、`walk 0x2000000`、`walk 0x4000000` 共四组 L0–L3 描述符与模型**全部相等**；trial 660、662、663 都有 `coverage: every section fully mapped`。四组采样不等于真机读回了每个叶，但结合循环与无重叠固件条目，能够确定当前源码不存在原来的跳页错误。

现有 coverage 自检的边界：H:1287–1301 对中间表和叶只查 bit0，没有核对完整类型、PA、权限、AF；H:1311 打印 HOLES 后也不会让 load_firmware 失败。它是有效的“旧空洞报警”，不是完整的 GPU 翻译正确性证明；上述离线模型补查了这些算术属性。

**2. pt_map() 索引、表页和全部 PAGE_SIZE 用法**

H:930–964 的“三次中间表索引 + 一次叶索引”实际是四级结构：

| 层 | 索引 | 本层一项覆盖范围 |
| --- | --- | --- |
| L0 | `(va >> 39) & 0x1ff` | 512 GiB |
| L1 | `(va >> 30) & 0x1ff` | 1 GiB |
| L2 | `(va >> 21) & 0x1ff` | 2 MiB |
| L3 | `(va >> 12) & 0x1ff` | 4 KiB |

每张表 512 个 8 字节项，共 4096 字节；4K 叶步进与上述位切片完全一致。shared 四个索引为 `(0,0,32,n)`，`n=0..147`。没有把 16K 子页地址截断：低内存 `mem_pa()` 是 `m->pa+offset`（H:778–779）；高内存路径是对具体子地址调用 `kvtophys(m->kva+offset)`（H:781）。

全文件 `rg -n 'PAGE_SIZE|PAGE_SHIFT|PAGE_MASK'` 的实际代码使用情况：

| 位置 | 用途 | 判断 |
| --- | --- | --- |
| H:913 | `pt_new()` 分配 `PAGE_SIZE`，即 16K | 正确但浪费 12K；物理地址至少 4K 对齐，索引只使用前 4K，不会隔页 |
| H:666、669、713 | `PAGE_MASK` 对齐宿主分配的起点/长度 | 正确；不会改变 GPU VA 或 pt_map 步长 |
| H:687、724 | `size >> PAGE_SHIFT` 记录宿主页数 | 正确；不是 GPU 叶数 |
| H:743–744 | 遍历宿主页检查 kvtophys 是否存在 | 正确；实际 GPU 映射仍由 H:1186 的 4K 循环处理 |
| H:853、855、1164、1166、1170 等 | PAGE_SIZE 出现在注释中 | 不执行 |

因此 **PAGE_SIZE 只有 H:913 这一处可执行使用**；没有第二处 16K 建叶循环。H:1281 的自检也按 4K。

另外发现两项通用健壮性缺口：H:1055 没有验证 VA 的 4K 对齐，也没有验证 `data_end-data_start <= va_end-va_start`，panthor 分别在 P:574–577、599–603 拒绝这些情况。当前实际镜像逐项满足条件，所以它们不是本次不启动的解释。

**3. global_interface()：确定不越界，以及真正仍缺的检查**

H:2659–2662 的 `glb_control` 有 version；H:2675–2678 的 `csg_control` 首字段为 features，**没有 version**。当前 global_interface 没有按 version 选择布局或计算偏移的分支。

| 输入状态 | 实际控制流 | 确定结论 |
| --- | --- | --- |
| `version=0, input_va=output_va=0` | H:4043 读 version，H:4044–4047 打印 GLB 的固定 32 字节；H:4048–4050 返回 false | **不越界**。根本不调用 shared_at，也不读 CSG，不写 input、不敲 doorbell |
| `version!=0, input_va=output_va=0` | H:4052–4053 调 shared_at；H:1341 发现 `0 < 0x04000000`，H:1342 返回 NULL；H:4054–4056 返回 false | **不越界**。减法位于 H:1344，拒绝路径不会执行它 |
| 强行计算零 VA 的数学差值 | `0-0x04000000 = -0x04000000`；64 位无符号为 `0xfffffffffc000000` | 仅为假设算术；当前代码先检查，**不会把它加到 kva** |
| 镜像原始 GLB | version=0，input=`0x04002000`、output=`0x04003000` | 当前版本检查仍直接返回；两个 VA 自身是合法地址 |
| MCU 发布合法非零 version，GLB VA 保持模板值 | input/output 偏移 `0x2000/0x3000`；固定 CSG 偏移 `0x1000` | 当前 shared 长度 `0x94000` 足够，结构均不越界 |

固件原始 GLB 八字为 `0, 0, 0x04002000, 0x04003000, 8, 0xa0, 0, 0x71`。
CSG0 七字为 `0, 0x04004000, 0x0400c000, 0, 0, 8, 0x0c`。
文件 `[0x3d000,0x3f000)` 的 8192 字节填入 shared 的前两页；之后 `0x92000` 字节清零。故“第二页以前无 PTE，所以 CPU 读取必是零”也混淆了 CPU 映射与 GPU 页表：CPU 的连续 kva 映射从分配时就存在。

仍存在的**其他条件下**的边界缺口：shared_at 只验证起始 VA，不验证结构跨度或对齐（H:1341–1344）。例如 input VA=`0x04093ffc` 能通过起点检查，但 H:4070 对 `core_en_mask` 的 8 字节写将超出 shared；32 字节 input 需起点不晚于 `0x04093fe0`，28 字节 output 的完整结构需不晚于 `0x04093fe4`。P:875–888 接受 size 并检查整个对象，强于当前 H。固定 CSG 读取 H:4059–4063 也没有显式长度检查，但本镜像长度足够。**这些缺口不能解释“零指针/零版本”场景，更未被本轮证据证明发生过。**

H:3598 还会把原始 version=0 改成 `0xff5aa5f0`。该值不等于 0，会通过 global_interface 的零版本检查；然而输入 VA 若为零，shared_at 仍会拒绝。H:3846–3855 甚至将“哨兵变成任何值，包括 0”当作 booted=true；这最多说明该内存字发生变化，不能等同启动完成。可靠完成判据应结合有效版本、JOB global bit31 和握手结果。

**4. kbase/panthor：boot 前 shared 到底如何准备**

逐项区分“我们已做”“真正缺失”“仅后续/重启阶段”，避免把 boot 之后的配置错搬到 AUTO 前：

| 动作与权威行号 | H 的对应实现 | 对主问题的含义 |
| --- | --- | --- |
| **装载 shared 模板并清零尾部**：K:587–588 调用 K:411–445；ZERO 时 K:436–439 清尾；P:462–465 同样拷贝+清零 | H:683/KMA_ZERO 先清分配，H:1094–1099 拷贝+同步 | **已做**；没有遗漏“把 GLB/CSG 控制模板写进去” |
| **同步装载数据给设备**：K:442–443 逐页 DMA_TO_DEVICE；P:686 DMA 同步 | H:764–769 对缓存内存做 PoC clean/invalidate，低内存用 dsb；H:1099 调用 | 低内存 WCOMB 路径已有等价交接。不能仅因没有 Linux DMA API 就断言漏同步 |
| **CPU/GPU 缓存策略配对**：K:374–401 按 coherency 降级；K:629–651 非一致平台 CPU writecombine；P:659–675 shared 非缓存并持久映射 | H:678 WCOMB；H:1124–1141 shared 选 index5 | 主体已做；GPU 叶额外 SH/NG 位见后文候选，CPU 访问第二页不依赖 GPU 是否映射 |
| **给 shared 的每个页插入 MCU MMU**：K:670–672；P:662–678 建 GPU BO | H:1186–1192 | **已做，148 个叶** |
| **保留静态 shared VA，防动态分配重叠**：K:1840 建 tracker，K:1914 调 K:192–212 预留静态段；P:1525–1528 建含 shared 保留区的 VM | H 只有固定 gSect/gShared，没有动态 shared VA 分配器 | **未做该管理结构**；当前不动态分配就不会因此重叠，只有补 trace/队列时才必须实现同等避让 |
| **识别 trace 元数据、分配动态 shared 缓冲区**：K:829–837 解析；K:1921 在 boot 前初始化；T:146–174 分配 Extract/Insert 页和各 data buffer | H:1047 只处理 type0，其余条目全部跳过 | **真正未做的 boot 前准备**；动态共享 VA 需要新页表映射，不能只打印静态 shared 148 页就称 trace 已备好 |
| **初始化 Extract、填写 trace 指针/size/enable**：T:195–217 将 Extract 置零，写 data/insert/extract GPU VA、字节 size、enable mask | H 无对应写入 | **真正未做**；具体写点在 config 段，见下表。P:784–789 同样跳过 trace，因此是否阻止此厂商固件启动仍待验证 |
| **L2 已上电，再装 AS/MMU，最后 AUTO**：K:1927–1933；P:1521、1535、1539–1543；K:324–335 等待 AS 更新完成 | trial 663 为 `preas0=1`；H:4514–4519 在 L2 前调用 early_as0；H:4108–4117 先装载再 UPDATE | **该轮与首次加载参考顺序不同**；H:2902 只查 AS_ACTIVE，不因 raw=0x10000 拒绝启动 |
| **boot 后才解析 capability、初始化全局 input**：K:1933 boot，1935 parse，1955 global_init；K:1447–1466 写 protm coherency、core mask、timers、IRQ mask、doorbell；P:1543–1551 同序 | H:4791 boot 成功分支才调用 global_interface；H:4070–4086 写配置 | 有 idle/protm 等后续配置差异，但**不能解释 AUTO 后第一份版本/启动 IRQ 从未发布** |
| **reload 时重载可写且非执行段并重建 trace 数据**：K:475–486、1566–1580；无 reload_needed 时 K:1589–1593 直接 AUTO；P:1269–1286 慢 reset 重载、快 reset 清 HALT | H 每次从新分配重新 load；还在 H:3701–3717 清 HALT | **不能据此认定首次加载漏 reload**。trial 663 已打印 `glb req 0 -> 0 (halt was already clear)` |

实际固件 trace 条目全部来自已校验哈希的数组。以下为 MCU VA，均在 config `[0,0x1000)`，**不是 MMIO 地址，本轮也没有读取设备这些地址**：

| 名称 | size / insert / extract / data 的配置 VA | enable VA / 位数 | 本地 K 是否识别 |
| --- | --- | --- | --- |
| fwutf | `0x8c / 0x90 / 0x94 / 0x98` | `0x9c / 0` | 仅非 MALI_KBASE_BUILD，T:126–128 |
| firmware trace | `0x9c / 0xa0 / 0xa4 / 0xa8` | `0xac / 4` | T:124–132 无此名字；T:300–305 会忽略 |
| fwlog | `0xb0 / 0xb4 / 0xb8 / 0xbc` | `0xacc / 11` | 是；FW_TRACE_BUF_NAME 定义于 `csf/mali_kbase_csf_trace_buffer.h:28`；T:129 分配 4 页 |
| benchmark | `0xc0 / 0xc4 / 0xc8 / 0xcc` | `0xd0 / 45` | 是；T:130 分配 2 页 |
| timeline | `0xd8 / 0xdc / 0xe0 / 0xe4` | `0xad0 / 1` | 是；T:131；`csf/mali_kbase_csf_tl_reader.h:30` 为 128 页 |

这些指针、size、enable 的镜像初值均为 0。T:284–289 规定条目字段含义；T:201–217 明确实际写入。写指针字和启用跟踪不是同一件事：T:129–131 的初始 enable mask 也为 0，但仍准备缓冲区。当前 H 则缓冲区、指针、size 全无。

type1 配置条目不是另一个已发现的 boot 前写入：K:815 调 `csf/mali_kbase_csf_firmware_cfg.c:275–308`，这里只记录地址、范围、名字；sysfs cfg_init 更在 K:1959、boot 后。不能把“跳过配置元数据”直接说成缺少必需初值。

**5. 对“为什么仍不启动”的候选、判据、成本与顺序**

寄存器以下均为既有驱动已访问过的 GPU 相对偏移：MCU_CONTROL `0x700`、MCU_STATUS `0x704`；JOB RAW/STATUS `0x1000/0x100c`；MMU RAW `0x2000`；AS0 TRANSTAB/MEMATTR/FAULTSTATUS/FAULTADDR/STATUS/TRANSCFG 分别 `0x2400/0x2408/0x241c/0x2420(+4)/0x2428/0x2430`（H:357–358、374、442–445）。GPU fault status/address 也已有 H:4687–4689 读取路径。不要新增未知 MMIO 读取来满足表格。

P0 是先取得因果分叉所需证据；P1 在分叉之后处理；P2/P3 仅在前项结果支持时投入。

| 候选与优先级 | 现有证据/限制 | 证实或否证判据 | 成本（是否刷机） |
| --- | --- | --- | --- |
| **C1 · P0：AS0 激活/表读取路径未正常建立；首次加载在 L2 前 UPDATE 是首个可控变量** | trial 663 39.259140 秒 `AS0 step update mmu irq 0x10000`；39.271727 秒注明 before L2；H:3670 随后清除；K/P 初次加载都 L2→AS。当前 CPU walk 正确不代表 GPU 读表成功 | 固定已修复映射、固定其余 boot args，以 `mali=5, preas0=0` 对照现有 `preas0=1`；在已输出的 AS_STEP 路径保留 UPDATE 后 RAW、AS_STATUS、FAULTSTATUS/ADDR 及 GPU fault，再观察版本和 JOB bit31。若顺序调整独立消除异常并完成 boot，支持因果；若 RAW 不变或仍无 boot，只否证“顺序足以修复”，继续查可达性/缓存，不能把清 IRQ 当修复 | 本轮源码/旧日志已能发现；因果 A/B **需经授权刷入**，预计先 1 轮，必要时配对重复 |
| **C2 · P0：硬件能取指，但厂商固件在发布版本前停住；“零条指令”判断过强** | 修复后只有厂商入口的失败日志，无新 witness。哨兵未变只证明没有观察到该字写回。旧 witness 入口是 `0x01000001`，本来就不在 leaf1 空洞 | 先在同一修复后基线上 `mali=5 + mali_witness=1`；日志必须有 vector now `0x1000001`，并读 shared 首两字。`0xdeadbeef/0x22` 证明取指和 DRAM store 贯通，转向厂商初始化；哨兵不变只说明首 store 未完成，仍可能是取指、权限或 store 路径，不能严格证明一条都没执行。witness 不发布正常 boot IRQ，应读标记，不以 start_mcu 返回 false 判失败 | 源码已证实旧解释矛盾；修复后分叉实验 **需经授权刷入 1 轮**；与 C1 分开控制变量 |
| **C3 · P1：GPU 叶属性仍与 vendor 不一致，缓存/共享性使满覆盖不能真正使用** | H:1139–1141 为所有叶加 NG，并按 cached 加 SH_IS/SH_OS；A:114–140 只有 coherency flag 才加 SH，K:387–401 普通 cached/降级 NC 不加该 flag。RW 的 bit6、RO 的 bits7:6=11 与 A:39–40 一致，不能被 H 的旧注释误导成权限编码仍错 | 当前 shared 首叶低属性 `0xe57`；按所给 K/A 在 COHERENCY_NONE 下为 `0x457`，差 `0xa00`（SH/NG）。先离线算各段完整 raw PTE；获授权后分开测 `mali_fwnc=1`、精确 vendor SH/NG，逐轮对照 C1 所列 RAW/fault、版本、witness。只有属性单变量带来标记/boot 转变才能认定相关；改后无转变则否证对应变量 | raw 差异 **离线可证**；影响 **需经授权刷入**，每个变量 1 轮；旧 trial 604/605 在坏映射下不能作为修复后排除证据 |
| **C4 · P1（C2 witness 成功后）：缺 trace 指针/size 的厂商初始化卡住** | K:1921/T:146–217 是确凿缺项；所有配置初值为 0。P:784–789 跳过 trace，说明并非所有固件必需；尚无该二进制早期流程读取空指针的直接证据 | 在 config 的 CPU 段映射上打印上述已知字初值，不跟随零指针；若已有安全 firmware trace/早期检查点显示访问这些变量，支持。补齐 allocator、额外叶和指针后，必须同时核对 config 字的精确值、额外 VA→PA，以及 JOB bit31/有效版本；只有其他变量固定而 boot 恢复，才证实。单纯“缓冲区分配了”不够 | 元数据和缺项已 **离线可证**；真实依赖还需固件控制流证据或 **经授权刷入验证**。不能用一次静态 shared dump 验证全部 |
| **C5 · P2：非零 version 哨兵改变了固件原始初始化条件** | K/P 首次装载不写 GLB_VERSION；H:3598 主动改原始 0 为 `0xff5aa5f0`。尚无固件读取该初值再决定是否初始化的证据 | 同一已修复基线只去掉 version 写入，保持模板与其余动作；看 `JOB RAW/STATUS bit31` 和从 0 变成合理版本。若 boot 随该单变量恢复支持；若无变化，只否证该版本哨兵变量，不能借此判定 CPU/GPU 可见性 | 初值差异 **离线可证**；因果 **需经授权刷入 1 轮**；无新控制流证据前低于 AS0/witness |
| **C6 · P2：旧 GPU L2/PTW 内容未失效，CPU 新表正确但 GPU 用旧表/数据** | H 低内存固定 PA，每轮相同；H:4522–4536 在 cold 路径不做该 cache flush。两参考的初始化环境不等于照抄一段 Android warm trace。当前没有证据证明缓存实际陈旧 | 在保证 AS/L2 合法顺序后，只改变明确的 GPU cache/PTW 失效步骤，记录既有 cache-complete 日志 `mali: caches flushed`（H:3550）、UPDATE RAW、witness/版本。结果随单变量翻转支持；不变只能排除该失效方案，不能声称所有 reset/cache 路径排除。不要直接把 mali_cold=0 的多处写入变化当单变量实验 | 生命周期仍可继续 **离线追源码**；因果 **需经授权刷入**，先 1 轮独立变量 |
| **C7 · P3：protected 段用普通 RAM 的语义差异** | H:1071–1074 用普通页映射；K:555–561 无 PMA 时直接跳过，K:571–577 有 PMA 才分配；P:586–589 直接忽略。因此不是已证实的首次 boot 必需条件 | 只有已跑通取指/store、且安全日志/trace 指向 protected-mode 进入，或 fault VA 落入 `0x03000000–0x03040000`，才提升优先级；改变 protected 处理后若成功/失败随其翻转再判因果。普通 boot 没触及 protected 不能支持此项 | 策略差异已 **离线可证**；因果 **需经授权刷入**；当前不安排实验 |

关于 `0x10000` 的语义，R:248–249 确实将其命名为 `MMU_BUS_ERROR(0)`，不是 H:4633 文案里的“request completions”。但所给 CSF 中断处理 K 的配套 `mmu/backend/mali_kbase_mmu_csf.c:421–429` 主要处理低 16 位，GPU bus fault 又走该文件 601–613 的 GPU fault 路径。因此本轮将其作为**已复现、需联合 fault 寄存器解释的配置异常**，不把“root PA 已被硬件读失败”写成已证实事实；UPDATE 的文档语义也只是广播配置（R:327），不保证必然发生表遍历。

**6. 修正交接结论，避免下一轮再烧周期**

1. 原来的 16K/4K bug 是真实的充分阻断因素：厂商入口 leaf1 未映射。不过“它解释了一切”过强。H:1434–1435 会把 witness reset 改成 `0x01000001`；trial 608、613 都有对应日志。witness 的代码、向量、两个写入目标原本就在有效 leaf0，**不能再用仍取 `0x010018ed` 来解释 witness 没写入**。
2. `start_mcu()` 返回 false 时，H:4649–4774 完成失败诊断后直接 return；global_interface 调用在 H:4791。level 5/6 如果都得到与 trial 663 相同的失败结果，就都不会到 global_interface。因此“level6 曾卡死”是历史观察，**不能凭 level 数字单独确定卡点**。trial 661 没有同等完整日志/镜像到源码的对应材料；本轮既不复刷也不臆定停在哪一行。
3. 当前源码已包含失败尾部的内联 shared probe（H:4732–4773），而本轮读取的 trial 663 完整 logbook 没有这些输出。源码存在探针，不等于已有真机送达/执行证据；本轮不称它已验证。
4. `MCU_STATUS=1` 只作为原始读数保留；不能代替版本发布、JOB global IRQ 或 witness store。无论 vendor/panthor 给状态的名字如何，都没有证明它表示“固件已完成启动”。
5. 不重做 AOC/mfgsc/MD0/MCU_CONTROL 已有排查；先保留映射修复，沿 **AS0 异常 → 修复后 witness 分叉 → 对应固件或属性候选** 收敛。

**下一轮建议（仅计划，尚未执行）**

先准备可审查的 `mali=5` 最小探针方案：沿 AS_STEP 已输出路径保留 UPDATE 后、任何 clear 前的寄存器快照；沿失败尾部已输出路径记录 GLB/CSG 原值和完整跨度判定。优先把 C1 的首次加载顺序与 C2 的修复后 witness 分成独立轮次，保持其余参数固定，不能一次叠加所有历史开关。

真机动作继续等用户明确授权；不直接用 mali=6。探针必须内联已输出的路径。未来实际构建后，按既有规则用 `strings -a mtk/loader/mtkloader.bin | grep -c "mali_xxx=1"` 核对新增启用参数送达，预期 3；本轮未构建，故没有声称做过该检查。

禁读 `0x13bfee00`、`0x13c40000`、`0x13F91034`；后续探针不得触发这些地址。原厂 Android 保持可用，当前不安排手机操作。
