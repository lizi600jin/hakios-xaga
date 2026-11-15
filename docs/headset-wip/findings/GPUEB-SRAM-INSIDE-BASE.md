# 更正：GPUEB 的 SRAM 落在 gpueb_base 之内，补丁的「三窗口都在外」是错的

> 2026-11-15 20:30，DSH。**Codex 指出，我复算确认。**

---

## 0. 一句话

**`gpueb_base` 是 `0x13c40000 + 0x22000` = `0x13c40000..0x13c62000`。
消息 SRAM `0x13c5fd80..0x13c60000` 完全落在里面。**

**所以补丁注释里「None of the three is inside gpueb_base」是假的。**

---

## 1. 精确计算

来自 `mt6895.dtsi` 的 GPUEB 节点：

```dts
reg = <0x13c40000 0x22000>,      /* gpueb_base  */
      <0x13c5fd80 0x280>,        /* mbox0_base  */
      <0x13c62004 0x4>,          /* mbox0_set   */
      <0x13c62074 0x4>,          /* mbox0_clr   */
      <0x13c62000 0x4>,          /* mbox0_send  */
      <0x13c62078 0x4>;          /* mbox0_recv  */
```

**`gpueb_base` 的结束地址 = `0x13c40000 + 0x22000` = `0x13c62000`。**

| 窗口 | 地址范围 | 在 `gpueb_base` 内？ |
|---|---|---|
| **`mbox0_base`（SRAM，640 B）** | `0x13c5fd80 .. 0x13c60000` | **✅ 在里面** |
| `mbox0_send`（TX busy, RO） | `0x13c62000 .. 0x13c62004` | ❌ 边界外（恰好从 end 开始） |
| `mbox0_set`（IRQ set, WO） | `0x13c62004 .. 0x13c62008` | ❌ |
| `mbox0_clr`（IRQ clr, WO） | `0x13c62074 .. 0x13c62078` | ❌ |
| `mbox0_recv`（RX pending, RO） | `0x13c62078 .. 0x13c6207c` | ❌ |

---

## 2. 补丁原文与事实冲突

`hakios.gpueb-ipi.h` 与 patch 的 H1 注释里写着：

> What is safe is the mailbox, and that is what the header carries: 0x13c5fd80 for the
> 640-byte message SRAM, 0x13c62000 for the four control registers, 0x7c600000 for the
> status block GPUEB writes. **None of the three is inside gpueb_base.**

**最后一句是错的。**

**SRAM 就在 `gpueb_base` 里。** 它和 GPUEB 的核心寄存器区是同一段地址空间。

---

## 3. 这条错误的实际后果

### 3.1 「另列资源即可安全访问」这个推论不成立

DTS 把 SRAM 单独列成一个 `reg` 条目，只说明**它在设备树里被单独登记**，
不说明**它在硬件上不属于同一个门控域**。

**而 `mali610.c:4296-4302` 的注释讲的正是这个风险：**

> GPUEB is behind its own clock gate. ... **Reading a gated MTK peripheral does not
> return an error -- the access never completes, the core waits, and the hardware
> watchdog resets the board.**

**SRAM 在 base 内，所以它受同一个门控影响。**

### 3.2 控制寄存器在边界外，但这不足以证明安全

`0x13c62000` 起的那四个寄存器确实在 `gpueb_base` 之外。
**但「不在某个 reg 范围内」不等于「不需要那个块的时钟」** ——
很多 MTK 外设的控制寄存器在单独的地址页上，仍然依赖同一个 clock gate。

**Codex 的第二条判断也成立**：`gpueb_probe()` 首次读的就是 TX `0x13c62000`（`:2414`，
调用点 `:4872`），**如果单次 MMIO 读不返回，协议层的一秒超时救不了** ——
超时是软件计时，而卡死发生在读指令本身。

---

## 4. 所以「先试一次」的代价被低估了

| 我之前的说法 | 实际 |
|---|---|
| 「只读试探，风险可控」 | **一次读就可能挂死，且无法恢复** |
| 「协议有 1 秒超时兜底」 | **超时在 MMIO 读之后，读不返回就到不了超时** |
| 「SRAM 在块外，安全」 | **SRAM 在块内** |

---

## 5. 正确的做法应该是什么

**必须先确认 GPUEB 的时钟已开，且这个确认不能靠读 GPUEB 自己。**

可能的方向：

| # | 方向 | 说明 |
|---|---|---|
| 1 | **读时钟控制器的门控状态** | `infracfg` / `topckgen` 里查 GPUEB 的 gate 位，**这些不在 GPUEB 块内** |
| 2 | **读 SPM 的电源状态** | GPUEB 若在某个 MTCMOS 域下，SPM 的 `PWR_STATUS` 会显示 |
| 3 | **查 LK 是否留下记录** | loader 的 boot note 或 LK log 里可能有 GPUEB 启动信息 |

**方向 1 最有希望**，因为我们已经在读 `infracfg` 和 `topckgen`（`mfg_master_gates()` 用的就是它们）。

---

## 6. 待办

- [ ] **修掉补丁里那句错误的注释**（"None of the three is inside gpueb_base"）
- [ ] 找出 GPUEB 的 clock gate 在 `infracfg`/`topckgen` 的哪一位
- [ ] 确认那一位的状态，再决定是否碰 GPUEB

**在完成前两条之前，不能刷带 `mali_gpueb` 的内核去做「试一次」。**
