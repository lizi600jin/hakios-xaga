# Trial 692 方案定案（2026-11-13 18:35）

## 执行者（6.1sol）的评审，我逐条核实

它写了 `/home/lizi/IOS/work-xaga/codex/out/trial-692-review.md`，三条修正：

### 1. 「从未跑过 witness=0 基线」—— 我错了一半

**我说的**：670–691 全开了 witness，所以 `witness=0` 是全新基线。

**它查到的**：**670/671 跑的就是正常固件入口**，672 才第一次加 `mali_witness=1`。

我核实：`round-8.md:34` 的 trial 对照表明确写着
`672 | 相对 671：增加 mali_witness=1`。

→ **正常入口不是没跑过**，只是当时的内存/页表/时钟配置与现在不同（那时 `mali_low=1`、
`preas0` 不同、4K 步进修复还没到位）。

**所以这轮补的是"现行条件下的匹配对照"**，不是"首次基线"。**这个区别重要**：
- 我的说法暗示"可能发现 witness 一直在掩盖故障"，那是夸大
- 准确说法是"补一个条件匹配的对照"

### 2. 不能只改 `witness=0` 一个参数

**它说的**：建议完整复制 691 的参数，只改 `mali_witness=0` 和 trial 编号，
保留 `assorder=1`、`low=0`、`ptvendor/scclk/scupd=1`，复用同一内核和 payload。

**为什么**：省略这些参数会**同时改变多个条件**，那就不是单变量了。

我核实 691 的 args 确实有这些：`mali_low=0 mali_ptvendor=1 mali_scclk=1 mali_scupd=1 mali_assorder=1`。

→ **采纳**。692 = 691 原样 + `mali_witness=0`。

### 3. 两种结果都要限制解释

**它说的**：
- 出现新 fault → 可以说明"正常入口访问了与桩不同的路径"（**这是真证据**）
- 结果相同 → 只能说明"没有观察到分叉"，**不能**证明"witness 没掩盖任何东西"

**还有一个我没想到的**：关 witness 同时取消了**代码覆盖、向量修改、全段 flush** 三件事
（`hakios.mali610.c:1421` 附近）。所以出现差异也**不能直接归罪于桩机器码**。

→ **采纳**。这条把我的"简单对照"变成了"三项同时变化"，必须写进报告边界。

### 它给的后续排序

`skipprot` → 经兼容性核对的换固件 → 段加载顺序，理由是：
- skipprot 有明确参考实现差异（Panthor `panthor_fw.c` 会跳过 PROT 节）
- 段序目前**缺少会改变最终映射或内容的依据**

这个理由比我原来的"按风险排序"更硬 —— **它按"有没有参考实现"排，而不是按"风险低不低"排。**

## 最终方案（692）

### 参数（完整复制 691，只改两处）

```
cpus=4 buttons=2 touchscreen=3 keylog=1 key_scan=1 fbwidth=1080 fbheight=2460 fb_disp=1
mali=5 mali_cold=1 mali_preas0=0 mali_low=0 mali_trace=1
mali_witness=0          ← 唯一改动
mali_ptvendor=1 mali_scclk=1 mali_scupd=1 mali_sc26=0
mali_cacheflush=0 mali_resetonly=0 mali_cohnune=0 mali_hwissues=0
mali_stackpwron=0 mali_mcuwin=0 mali_mcuseq=0 mali_doorbell=0
mali_pdcsettle=0 mali_statussem=0 mali_assorder=1
mali_trial=692
```

### 判据（不含 witness）

| 判据 | 说明 |
|---|---|
| 固件版本字 ≠ `0xff5aa5f0` | 只有真执行固件才会改 |
| `JOB_INT_STATUS` bit31 | 同上 |
| **`AS_FAULTSTATUS` / `AS_FAULTADDRESS`** | **本轮核心**：若正常入口访问 `.bss` 而没映射，会出现 fault，地址落在 `0x3000000-0x3040000` |
| `debug_top` 是否回落 | 回落 = 核心真跑起来了 |

### 取证要求（它提的，采纳）

**必须等版本行和尾部完整**，不能再用 `witness: read1` 当完成标志
（这轮根本不产生那行）。`run-once.py` 的 `_has_completion_marker` 需要改成"检测
`the start loop ended` **或** `version word` **或** `witness: read1`"。

### 报告边界（必须写）

1. 出现新 fault ≠ 证明了"witness 掩盖故障"，只能说"正常入口访问了桩没访问的路径"
2. 结果相同 ≠ 证明了"witness 清白"，只能说"没观察到分叉"
3. 关 witness 同时取消了代码覆盖/向量修改/全段 flush，**出现差异不能归罪于桩机器码**

## 我这次的错误（第 5 条，记入 audit-fixes）

**我说"从没跑过 witness=0 基线"是错的** —— 670/671 就是。
我没查 trial 对照表就下了"从未"的断言，还把它的意义夸大成"可能发现 witness 一直在掩盖故障"。

执行者查了 `round-8.md:34` 直接驳回。

## 引用

- 执行者的评审：`out/trial-692-review.md`
- 670/671 正常入口：`out/round-8.md:32-34`
- witness 安装点（三项同时变化）：`hakios.mali610.c:1421-1437`
- 我的原建议：本会话 18:30 的发送内容
