# DSH 侧交接备用稿（Codex 额度将尽时）

> 2026-11-16 21:00，DSH。**这份是我这边能独立完成的交接内容。
> Codex 若来不及写，本文可作基础。Codex 自己的交接文件优先级更高。**

---

## 1. 当前状态（截至 `c34d20b`）

| 项 | 值 |
|---|---|
| **文档仓 HEAD** | **`c34d20b57b2154c3062518d491cfea41926c1a86`**，远端同步 ✅ |
| **最新内核** | **`a80231ed9dc78a82bfddc71f34685808e66ce490d70ed059e37cd108638885c3`**，15321336 B |
| **内核路径** | `logs/mt6895-cpu-baseline-20261116/kernel-src/BUILD/obj/DEVELOPMENT_ARM64_BCM2837/mach.development.bcm2837` |
| **make rc** | 2（既有 symbolset） |
| **judge rc** | 0 |
| **设备状态** | **原厂运行**，`boot_a` md5 `a63b2e35a7c2522cb24a6a10332b9bc8` |
| **未做的** | 打包、刷写、设备验证——**均未开始** |

---

## 2. 已独立复核过的（我这边完成）

| # | 内容 | 文档 |
|---|---|---|
| 1 | 60/23/6 复算、编译状态（15 个 `.o`）、`mt6895.config` 49/57 行关闭 | `host-cpu-baseline-01/README.md` |
| 2 | vendor 源码 387 行逐行审查、域偏移、LUT 解码、DT 依赖链 | `host-cpu-baseline-01/PROTOCOL-VENDOR-CPUFREQ.md` |
| 3 | 候选驱动七项要点、130 例复跑、Android CPU 基线 | `candidate-cpufreq-review/README.md` |
| 4 | 713 构建五步、`-Werror` 复跑、strings 验证 | `build-cpu-probe-713/README.md` |

**全部含路径、hash、行号、native rc。**

---

## 3. 尚未复核的（Codex 侧未完成或未知）

| # | 项 |
|---|---|
| 1 | **713 内核是否被 Codex 独立复核**（我不知它是否做完） |
| 2 | **运行的 `mediatek_cpufreq_hw.ko` 是否与 `xaga-s-oss` 源码配对** |
| 3 | 候选驱动在**真实硬件**上的行为（从未运行过） |
| 4 | `CPU_NOT_READY` 在实机上会不会出现 |

---

## 4. 五条未证实项

| # | 未证实 |
|---|---|
| 1 | `cpufreq-hw` 与 `cpuhvfs` **谁赢**（两个驱动都可能写频率） |
| 2 | `cpuhvfs` 的 `tbl-off = <0x04 0x4c 0x94 0xdc>` **四组分别对应哪个域** |
| 3 | **MCUPM 是否需在 cpufreq-hw 之前初始化** |
| 4 | **`status` bit0/bit1 由谁置**（TF-A？MCUPM？cpuhvfs？） |
| 5 | `cpuhvfs` 的 USRAM/ESRAM 与 `cpufreq-hw` 的 CSRAM **是否重叠** |

**另有事实**：`cpufreq-hw` 三域 `0x11bc10`–`0x11bf6f` **完全落在** `cpuhvfs` CSRAM `0x11bc00`–`0x11bfff` 内。

---

## 5. 关键文件与 hash

| 文件 | hash |
|---|---|
| 新内核 | `a80231ed9dc78a82bfddc71f34685808e66ce490d70ed059e37cd108638885c3` |
| 候选驱动 | `ef013ea9178e18a2018cf865e609f26f5ef15fdafcc680f95d38e2c165d7b6e9` |
| vendor 协议源码 | `3cc72e1f5b36e0154ab34df932a1541a59e2f1932b160a5538866df7f2d31d8f` |
| `mt6895.config` | `b8edfb815291c9d45a06a964a919c7ebaaafbf1869b0be5d98acc4367134a2de` |
| `xaga-live.dts` | `f47d073b92b76209…` |
| 712 headset | `8c0cf1c232d0ac4f108a9ab4693559e99cb5efeecc00753c1e2bf7e96d300a02` |
| pmif.h | `f4d9a964e304ea14686359b85e75560325e9d47ee2ecb5614aae538f9e293d78` |
| 原厂 boot | `c5f8e7bde8e9f1d824d0c7915f7d701adce8142656a82c20e10ccb75e15e01fc` |

---

## 6. 下一阶段候选（我的判断，Codex 未表态）

**候选 A：先做只读探针的设备验证**（`mtk_cpu_probe=1`）
- 成本低，风险低（纯读，无写）
- 能首次看到真实 LUT 与 status 位
- **能验证候选驱动在真机上不崩**
- **但不能证明调频有效**

**候选 B：直接实现可写的最小调频**
- 需要先解决五条未证实项里的至少 1、4
- **风险高**：写错 `0x88` 可能让 CPU 跑到不可用频率
- **不建议在未做 A 的情况下直接做**

**我倾向 A 先做。** 但按 `mt6895.config` 的教训，**这需要新的隔离树 + 一次刷写**。

---

## 7. 红线（不能碰）

| # | 禁止 |
|---|---|
| 1 | 未证明时钟前读 `0x13c40000`（GPUEB） |
| 2 | `fastboot oem expdb` |
| 3 | 黑屏长按电源 |
| 4 | 混合脏树构建 |
| 5 | 未公告的刷写 |
| 6 | `spmi_access`、vendor 全量 codec dump、tinymix getter |
| 7 | PMIC `014b` 写、`0240` RMW、PMIF 控制器 reset/gating |
| 8 | `stash/reset/checkout/clean/gc/prune` |
| 9 | 生产仓 push、混合树重建 |
| 10 | **写 `0x84`/`0x88` 做调频**（本轮新加：未做设备验证前不得写控制器寄存器） |

---

## 8. 额度恢复后第一件事

**读 Codex 的交接文件**（若有），与本文交叉核对。

**然后确认**：713 内核有没有被独立复核过。**若没有，先复核再往下走。**

---

## 9. 设备当前状态（可随时验证）

```
slot      _a
boot_a    md5 a63b2e35a7c2522cb24a6a10332b9bc8（原厂）
boot_id   686cb45c-3f54-481f-ba0c-5e9945f95449
SELinux   Enforcing
CPU       online 0-7，三 policy 均 mtk-cpufreq-hw，governor sugov_ext
```

**设备是干净的，没有残留试验镜像。**
