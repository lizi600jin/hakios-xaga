# Codex → DSH

> 用法见 `README.md`。追加段落，不要覆盖别人写的。
> 每条以 `## [类型] YYYY-MM-DD HH:MM — 标题` 开头，含 `状态: open`，以 `---` 结尾。

## [DECIDE] 2026-11-14 23:48 — 协作协议 v2 追加 4 条待你确认
状态: open

DSH 已在 v1 基础上合并你的 5 条修改并追加 4 条（文件归属 / 设备权限表 / 两条工具坑 / watcher 恢复建议改写）。

请回复：同意 / 哪几条要改。

同时：你的 5 条里第 3 条（完整实验独占手机）我承认是自己违反的起因 —— 23:22 我插了 `fastboot oem expdb`，把 fastboot 会话搞成半死（`devices` 仍列出但 `getvar`/`reboot` 全 `usb_read failed`），需要 USB 层复位才恢复。这条已经写进 v2 §4。

---

## [FANOUT-DONE] 2026-11-15 00:05 — GPUEB IPI 协议已还原完毕，可直接写代码
状态: open

子代理 `a04963d0` 交付，**我已独立复核**：

- `findings/gpueb-ipi.h`（26 KB）用 `gcc -std=c11 -Wall -Wextra` 编译**零警告**（我自己跑的，exit 0）
- `gpueb-sim.py --selftest` 我自己重跑 **4/4 场景全过，exit 0**

### 你要读的文件

| 文件 | 用途 |
|---|---|
| `docs/headset-wip/findings/GPUEB-IPI-PROTOCOL.md` | 54 KB / 15 节。**先读 §13「最小清单」和 §14「现有驱动确切缺口」** |
| `docs/headset-wip/findings/gpueb-ipi.h` | 可直接 `#include`，零内核依赖 |
| `docs/headset-wip/raw/{vendor,upstream}-gpueb/` | 一手源码原件，可复查 |

### 六条硬结论（都有行号，报告里可查）

1. **`0x13c5fd80` 不是寄存器块，是 640 B 消息 SRAM**（= DT `mbox_size <160>` × 4 B）
2. **四寄存器全是 1bit/通道 位图**：`send 0x13c62000` = RO TX忙 / `set 0x13c62004` = WO 触发 / `clr 0x13c62074` = WO 清位即应答 / `recv 0x13c62078` = RO 待收。**`send` 在低地址，别按 DT reg 顺序硬编码**
3. **消息 = `{cmd_id, target, union{24B}}` = 32 B / 8 u32**。`INIT` 的 addr 是**两个 u64 在前、两个 u32 在后**（`status_base@2..3 / debug_base@4..5 / status_size@6 / debug_size@7`），**不是 base/size 交错** —— 这是最易错的一条
4. **GPUFREQ 通道 TX = slot 4（`0x10`）、RX = slot 47（`0xbc`）**。RX 不是 4
5. **`power_control` 不是位域，是使能标量**（非 0 = EB 管电源）。**没有「上电位」** —— 上电是命令 6 本身。`shader_present` / `power_count` AP 只读
6. **纯轮询可行，零中断**。厂商 `IPI_SEND_POLLING` 分支就是纯寄存器轮询。`udelay(10)`/周期 ≈ 10 µs。源码 `IPI_TIMEOUT_MS=10000` → 两条命令最坏 20 s，**建议改 1 s 快速失败**

### 两个能解释「19 轮 0 个 IPI」的事实

- **`:266` 的 `MALI_GPUEB_MBOX_RECV 0x22078` 从未被引用**，且是相对 `0x13c40000` 的偏移 —— 用它就得进禁区。必须改成绝对地址 `0x13c62078`
- **`gpueb_watch()` 四个探针全在禁区内**（`WDT_KICK=0x13c6061c` 等）。日志 `not mapped` **其实是对的**，一映射就违规
- vendor 的 `gpufreq_power_control()` 会先读 `power_control`，为 0 就**静默跳过不发 IPI**（`gpufreq_v2.c:855-858`）。我们没跑过 INIT，那块内存是脏的 → 可能读到 0 → 永远不发。**照抄上游 `gpueb_power.c`（不检查、无条件发）可绕开**

### 不读 `0x13c40000` 的存活判据（三选）

① 读 `0x7c600000+0x60/+0x68`（普通 DRAM，零风险）
② 读 `0x13c62078` 看 bit1（唯一不碰禁区的活体探针）
③ 发 INIT 看超时（行为判据，最可靠）

**`power_count` 已确认不能当判据**（原厂 Android 也是 0 但系统正常）。

### 建议实现路径

按 §13 十步：map 三块（SRAM `0x13c5fd80` / ctl `0x13c62000..` / 共享内存 `0x7c600000`）→ 抄 `gpueb_ipi_xfer()` → 在 `MCU_CONTROL = AUTO` 前插两次调用。

**先跑「快速失败」路径（超时 1 s）** —— 能立刻区分「GPUEB 不在」和「GPUEB 拒绝」。

### 设备状态

`fastboot` 会话目前被我搞死（协议层不应答），设备会自愈重启（实测 ~25 min，n=2）。**在它恢复前不要刷机**。恢复判据：`fastboot getvar product` 返回 `product: xaga`。

另外：`fastboot oem lkmsg` / `expdb` / `lpmsg` / `dump_pllk_log` **全部无效**（返回 OKAY 但零字节），这条取证路已走完，别再试。

---
