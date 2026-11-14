# MT6895 GPUEB IPI 协议还原

> 目标：把 AP ↔ GPUEB 的 mailbox/IPI 协议还原到「可以照抄写代码」的精度。
>
> **证据标签**
> - `[ML]` = 上游 `/home/lizi/IOS/work-xaga/k-xaga`（= MT6895-Mainline/linux `7.2-mt6895-xiaomi-xaga`，已比对 md5 相同）
> - `[VND]` = 厂商 5.10 树 `/home/lizi/IOS/work-xaga/k-xaga` 的 `drivers/gpu/mediatek/gpueb/`
> - `[DTS]` = 真机反编译 DT `/home/lizi/IOS/work-xaga/dump/xaga_live.dts`
> - `[IOMEM]` = 真机 `/proc/iomem` `/home/lizi/IOS/work-xaga/dump/iomem.txt`
> - `[FW]` = `gpueb_a.img` 字符串表（本机实测）
> - `[OUR]` = `/home/lizi/IOS/hakios-mtk/mtk/mt6895/drivers/gpu/hakios.mali610.c`（只读）
>
> 原件全部保存在 `docs/headset-wip/raw/vendor-gpueb/` 与 `docs/headset-wip/raw/upstream-gpueb/`，可复查。

---

## 0. 结论先行（够不够写代码）

**够。** 除了两处需要真机确认（见 §9），其余全部有源码行号。

三条硬结论：

1. **`mbox0_base 0x13c5fd80` 不是寄存器块，是消息 SRAM。** 大小 `0x280` = 640 B = **160 个 4 B slot**，与 DT 的 `mbox_size = <160>` 完全相等（`[IOMEM]:97`，`[DTS]:4888`）。把 `0x13c5fd80` 当寄存器读会得到垃圾。
2. **四个寄存器是 1 bit / 通道的位图，不是队列指针。** `set` 写 1 = 触发 IRQ；`clr` 写 1 = 清 IRQ/应答；`send` 读 = 我方的 TX 忙位；`recv` 读 = 对方的 TX（即我方的 RX 待收位）。**`send` 在 `0x13c62000`，`set` 在 `0x13c62004`——注意 `send` 在低地址，和 DT 的 reg 顺序不同，别按顺序硬编码。**
3. **GPUFREQ 通道在 slot 4（TX，字节 0x10）与 slot 47（RX，字节 0xbc）。** RX 不是 slot 4。这是最容易写错的一处，见 §4。

---

## 1. mbox0 寄存器布局

### 1.1 地址（`[DTS]:4895-4898`，`[IOMEM]:97-101` 双向确认）

```dts
reg = <0 0x13c40000 0 0x22000>,   /* gpueb_base   —— 禁读，见 §8 */
      <0 0x13c5fd80 0 0x280>,     /* mbox0_base   —— 消息 SRAM，不是寄存器 */
      <0 0x13c62004 0 0x4>,       /* mbox0_set    */
      <0 0x13c62074 0 0x4>,       /* mbox0_clr    */
      <0 0x13c62000 0 0x4>,       /* mbox0_send   */
      <0 0x13c62078 0 0x4>;       /* mbox0_recv   */
interrupts = <GIC_SPI 516 IRQ_TYPE_LEVEL_HIGH 0>;   /* = GIC INTID 548 */
```

真机 `/proc/iomem` 逐行对得上（`[IOMEM]:97-101`）：

```
13c5fd80-13c5ffff : 13c40000.gpueb mbox0_base
13c62000-13c62003 : 13c40000.gpueb mbox0_send
13c62004-13c62007 : 13c40000.gpueb mbox0_set
13c62074-13c62077 : 13c40000.gpueb mbox0_clr
13c62078-13c6207b : 13c40000.gpueb mbox0_recv
```

> 注意 `13c5fd80 + 0x280 = 13c60000`，而 `mbox0_send` 在 `13c62000`。**中间 `0x13c60000..0x13c61fff` 是空的**（iomem 里没有映射）。所以不能把 ctl bank 当成 `mbox0_base + 0x227c` 之类去算，必须单独 map `0x13c62000`。

### 1.2 ctl bank 内部偏移（权威：上游 `drivers/mailbox/mtk-gpueb-mailbox.c:22-25`）

| ctl bank 偏移 | 绝对地址 | 名称 | 方向 | 语义 |
|---|---|---|---|---|
| `0x00` | `0x13c62000` | `GPUEB_MBOX_CTL_TX_STS` | **RO** | 我方 TX 是否仍被对方占用（1 = busy，不能发下一条） |
| `0x04` | `0x13c62004` | `GPUEB_MBOX_CTL_IRQ_SET` | **WO** | 写 `BIT(chan)` = 向对方触发中断（发消息） |
| `0x74` | `0x13c62074` | `GPUEB_MBOX_CTL_IRQ_CLR` | **WO** | 写 `BIT(chan)` = 清中断（收完应答 / 收完消息） |
| `0x78` | `0x13c62078` | `GPUEB_MBOX_CTL_RX_STS` | **RO** | 对方给我方发的待收位（1 = 有待收消息） |

源码逐行（`mtk-gpueb-mailbox.c`）：

```
:22  #define GPUEB_MBOX_CTL_TX_STS   0x00
:23  #define GPUEB_MBOX_CTL_IRQ_SET  0x04
:24  #define GPUEB_MBOX_CTL_IRQ_CLR  0x74
:25  #define GPUEB_MBOX_CTL_RX_STS   0x78
:27  #define GPUEB_MBOX_FULL    BIT(0)   /* i.e. we've received data */
:28  #define GPUEB_MBOX_BLOCKED BIT(1)   /* i.e. the channel is shutdown */
:30  #define GPUEB_MBOX_MAX_RX_SIZE  32  /* in bytes */
```

- `TX_STS` 读法：`:196-201` `mtk_gpueb_mbox_last_tx_done()` → `return !(readl(ctl + TX_STS) & BIT(num));`
- `RX_STS` 读法：`:97` ISR 里 `rx_sts = readl(ctl + RX_STS); if (rx_sts & BIT(ch->num))`
- `IRQ_SET` 写：`:145` `writel(BIT(ch->num), ctl + IRQ_SET);`
- `IRQ_CLR` 写：`:118`（收完）与 `:164`（startup 时先清一次）

### 1.3 与 vendor 驱动命名对照

vendor 用 DT 的 `reg-names` 取裸地址，名字不同但**指向同一组寄存器**（`[ML] drivers/soc/mediatek/mtk-mbox.c:745-780`）：

| DT reg-name | vendor 结构字段 | 上游名字 | 属性 |
|---|---|---|---|
| `mbox0_set` | `set_irq_reg` | `IRQ_SET` | WO |
| `mbox0_clr` | `clr_irq_reg` | `IRQ_CLR` | WO |
| `mbox0_send` | `send_status_reg` | `TX_STS` | RO |
| `mbox0_recv` | `recv_status_reg` | `RX_STS` | RO |
| `mbox0_base` | `base` | `mbox_mmio` | RW SRAM |

**这里有个坑**：vendor 的 `clr_irq_reg` 在 `recv_status_reg` 缺失时会被当**读**用（`mtk-mbox.c:337-340`），而 `set_irq_reg` 在 `send_status_reg` 缺失时会被当**读**用（`mtk-mbox.c:302-305`）。这是兼容老平台的退化路径，**MT6895 四个都齐，不要走退化路径**。

### 1.4 reset 值

**未验证。** 无公开 reset 值文档，DT 无 `reset` 属性，vendor 驱动也从不写初值（只在 `startup` 里写一次 `IRQ_CLR`，`mtk-gpueb-mailbox.c:164`）。

实测判据：正常 Android 启动后 `RX_STS` 应为 `0`（无待收），`TX_STS` 应为 `0`（无在途）。若 `RX_STS` 非 0 且一直不清，说明对方在等一个我们没回的应答。

---

## 2. `set` / `clr` / `send` / `recv` 的确切语义

### 2.1 一句话

**四个寄存器全是 32 bit 位图，bit = 通道号，不是队列指针。**

- `set` = 写 1 触发（raise）→ 对端收到中断
- `clr` = 写 1 清除（ack/lower）→ 告诉硬件「这条我处理完了」
- `send` = 读我方 TX 状态（1 = 上一条还没被取走）
- `recv` = 读我方 RX 状态（1 = 对方给我发了一条）

### 2.2 写序（`mtk-gpueb-mailbox.c:127-148`，这就是权威顺序）

```c
/* 1) 先查 TX 是否忙 */
if (atomic_read(&ch->rx_status))
        return -EBUSY;

/* 2) 逐 32-bit 写消息体到 SRAM（注意：writel 逐个写，不要用 memcpy_toio） */
for (i = 0; i < ch->c->tx_len; i += 4)
        writel(values[i / 4], mbox_mmio + ch->c->tx_offset + i);

/* 3) 最后写 1 到 IRQ_SET 触发 */
writel(BIT(ch->num), mbox_ctl + GPUEB_MBOX_CTL_IRQ_SET);
```

上游注释（`:136-141`）明确解释为什么用逐字 `writel` 而不是 `memcpy_toio`：

> memcpy_toio/__iowrite32_copy don't work here, as they may use writes of different sizes or memory ordering characteristics...

vendor 侧等价序列（`[ML] drivers/rpmsg/mtk_rpmsg_mbox.c:29-67`）：

```c
status = mtk_mbox_check_send_irq(mbdev, mbox, send_pin_index);   /* 读 TX_STS */
if (status != 0) return MBOX_PIN_BUSY;
ret = mtk_mbox_write(mbdev, mbox, send_slot, buf, len * 4);      /* 写 SRAM */
mb();                                                            /* 内存屏障 */
ret = mtk_mbox_trigger_irq(mbdev, mbox, 0x1 << send_pin_index);  /* 写 IRQ_SET */
```

**→ 顺序必须是：查 TX_STS → 写 SRAM → 内存屏障 → 写 IRQ_SET。** 中间任何一步乱序都会丢消息。

### 2.3 读序（`mtk-gpueb-mailbox.c:107-125`）

```c
if (readl(ctl + RX_STS) & BIT(n)) {          /* 1) 查有没有 */
        memcpy_fromio(buf, mbox_mmio + rx_offset, rx_len);   /* 2) 读体 */
        writel(BIT(n), ctl + IRQ_CLR);                        /* 3) 清位（应答） */
}
```

vendor 的轮询版（`mtk-mbox.c` 的 `mtk_mbox_polling()`）顺序也是「先清位再读体」：

```c
reg = mtk_mbox_read_recv_irq(mbdev, mbox);      /* 读 RX_STS */
irq_state = reg & (0x1 << recv_pin_index);
if (irq_state > 0)
        ret = mtk_mbox_clr_irq(mbdev, mbox, irq_state);   /* 先清 */
else
        return MBOX_PIN_BUSY;
ret = mtk_mbox_read(mbdev, mbox, pin_recv->offset, data, ...);  /* 后读 */
```

> **两种顺序都能工作**（上游先读后清，vendor 先清后读）。差别只在极端竞态：先清后读更安全（避免重复处理同一 bit），先读后清更安全（避免清掉之后对方立刻又写导致丢）。**照抄上游顺序（先读后清）即可**，因为上游是 mainline 审过的。

### 2.4 为什么 `clr` 同时是「应答」

GPUEB 固件的应答机制就是：**往 AP 的 RX slot 写回一条同样格式的消息，然后写 `BIT(1)` 到 `IRQ_SET`**。AP 侧看到 `RX_STS` bit1 置位，读 slot 47 的 32 字节，得到 `u.return_value`。

这一点由 vendor 的 `mtk_ipi_send_compl()` 结构反推（`mtk_tinysys_ipi.c:429-560`）：

- `:479` 发出去
- `:511-525` **轮询 `mtk_mbox_polling()` 等 `pin_r`（接收 pin）**
- `:536` `ipi_monitor(..., RECV_ACK)`
- 返回后调用方读 `g_recv_msg`（`gpufreq_v2.c:873` `ret = g_recv_msg.u.return_value`）

即 **一个通道的「发送」和「接收应答」用的是两个不同的 slot**（TX slot 4，RX slot 47），但**同一个 pin_index（bit 1）**。

---

## 3. 32 字节消息布局

### 3.1 结构体（`[ML] drivers/gpu/mediatek/gpueb/include/gpufreq_ipi.h:51-78`）

```c
struct gpufreq_ipi_data {
        enum gpufreq_ipi_cmd cmd_id;    /* u32 */
        unsigned int target;            /* u32 */
        union {
                int oppidx;
                int return_value;
                unsigned int freq;
                unsigned int volt;
                unsigned int power;
                unsigned int power_state;
                unsigned int mode;
                struct {
                        unsigned long long status_base;
                        unsigned long long debug_base;
                        unsigned int status_size;
                        unsigned int debug_size;
                } addr;
                struct { unsigned int freq, volt; } custom;
                struct { unsigned int limiter; int ceiling_info, floor_info; } setlimit;
        } u;
};
```

`GPUFREQ_IPI_DATA_LEN = sizeof(...) / sizeof(unsigned int)` = **8**（`gpufreq_ipi.h:17-18`）。

### 3.2 实测偏移（本机 gcc 编译验证，不是估算）

```
sizeof(struct gpufreq_ipi_data) = 32 bytes = 8 slots
cmd_id              off=0x00  u32[0]
target              off=0x04  u32[1]
u.addr.status_base  off=0x08  u32[2..3]   (u64)
u.addr.debug_base   off=0x10  u32[4..5]   (u64)
u.addr.status_size  off=0x18  u32[6]
u.addr.debug_size   off=0x1c  u32[7]
u.power_state       off=0x08  u32[2]
u.return_value      off=0x08  u32[2]
```

**⚠ 最容易写错的一处**：`addr` 结构里 **`status_base` 与 `debug_base` 是两个相邻的 u64，然后才是两个 u32 size**。不是 `status_base, status_size, debug_base, debug_size` 那样交错。用 `CMD_INIT_SHARED_MEM` 时必须按 `u32[2..3]=status_base / u32[4..5]=debug_base / u32[6]=status_size / u32[7]=debug_size`。

### 3.3 8 个 u32 的完整表

| u32 # | 字节偏移 | 字段 | 备注 |
|---|---|---|---|
| 0 | `0x00` | `cmd_id` | 见 §3.4 命令表 |
| 1 | `0x04` | `target` | `0=DEFAULT, 1=GPU, 2=STACK, 3=INVALID`（`gpufreq_v2.h:78-83`）。仅 `CMD_COMMIT` 用 |
| 2 | `0x08` | `u.xxx` 低 32 | `CMD_INIT_SHARED_MEM`：`status_base[31:0]`<br>`CMD_POWER_CONTROL`：`power_state`<br>其余：`oppidx`/`return_value`/`freq`/`volt`/`power` |
| 3 | `0x0c` | `u.xxx` 高 32 | `CMD_INIT_SHARED_MEM`：`status_base[63:32]`；其余通常 0 |
| 4 | `0x10` | `u.xxx` | `CMD_INIT_SHARED_MEM`：`debug_base[31:0]` |
| 5 | `0x14` | `u.xxx` | `CMD_INIT_SHARED_MEM`：`debug_base[63:32]` |
| 6 | `0x18` | `u.xxx` | `CMD_INIT_SHARED_MEM`：`status_size` |
| 7 | `0x1c` | `u.xxx` | `CMD_INIT_SHARED_MEM`：`debug_size` |

**没有 token / slot / core / ret 字段。** 任务描述里的 `slot`/`core`/`token`/`ret` 在这份协议里都不存在：

- 「slot」是 mbox SRAM 里的**物理位置**（由通道表固定），不占消息字段
- 「core」由 mbox 的 bit（pin_index）承载，不占消息字段
- 「token」不存在，靠 `RX_STS` bit + 单通道互斥保证
- 「ret」= 应答消息的 `u32[2]`（`u.return_value`）

### 3.4 命令表（`gpufreq_ipi.h:29-49`）

```c
enum gpufreq_ipi_cmd {
        CMD_INIT_SHARED_MEM    = 0,
        CMD_GET_FREQ_BY_IDX    = 1,
        CMD_GET_POWER_BY_IDX   = 2,
        CMD_GET_OPPIDX_BY_FREQ = 3,
        CMD_GET_LEAKAGE_POWER  = 4,
        CMD_SET_LIMIT          = 5,
        CMD_POWER_CONTROL      = 6,
        CMD_COMMIT             = 7,
        /* Debug */
        CMD_GET_DEBUG_OPP_INFO = 8,
        ... /* 9..19 */
        CMD_NUM                = 20,
};
```

只有 0 和 6 是启动路径必需的。

---

## 4. 通道 / slot 映射（**最容易出事的一节**）

### 4.1 通道表（`[DTS]:4891-4894`）

```dts
/* id, mbox, send_size(slots) */
send_table = <0 0 4>, <1 0 8>, <2 0 3>, <3 0 3>,
             <4 0 9>, <5 0 4>, <6 0 6>, <7 0 6>;
send_name_table = "IPI_ID_FAST_DVFS_EVENT", "IPI_ID_GPUFREQ", "IPI_ID_SLEEP",
                  "IPI_ID_TIMER", "IPI_ID_FHCTL", "IPI_ID_CCF",
                  "IPI_ID_GPUMPU", "IPI_ID_FAST_DVFS";

/* id, mbox, recv_size(slots), recv_opt */
recv_table = <0 0 4 0>, <1 0 8 1>, <2 0 1 0>, <3 0 1 0>,
             <4 0 1 1>, <5 0 4 1>, <6 0 1 1>, <7 0 6 1>;
```

`recv_opt = 1` 表示「这个 pin 用于接收对方对我的应答」（`MBOX_RECV_ACK`）；`0` 表示普通消息。

**GPUFREQ（id=1）的 `recv_opt = 1`**，即它是请求-应答式通道。

### 4.2 slot 分配算法（`[VND] gpueb_ipi.c:250-274`）

```c
void gpueb_mbox_setup_pin_table(unsigned int mbox)
{
        unsigned int i;
        int last_ofs = 0;

        for (i = 0; i < send_count; i++)          /* 先把所有 TX 排完 */
                if (mbox == pin_send[i].mbox) {
                        pin_send[i].offset = last_ofs;
                        last_ofs += pin_send[i].msg_size;
                }

        for (i = 0; i < recv_count; i++)          /* TX 之后接着排 RX */
                if (mbox == pin_recv[i].mbox) {
                        pin_recv[i].offset = last_ofs;
                        last_ofs += pin_recv[i].msg_size;
                }

        if (last_ofs > g_mbox_size)
                pr_debug("mbox%d exceed the maximum size\n", mbox);
}
```

**关键：TX 与 RX 共用一个 `last_ofs` 累加器，TX 全排完才排 RX。**

### 4.3 模型自校验（用上游 MT8196 的固定表反证）

上游 `mtk-gpueb-mailbox.c:284-300` 给了 MT8196 的**绝对** tx/rx 偏移表。用上面的算法复算：

```
TX accum end = 0x0e0, RX starts here
model matches MT8196 published table: True
```

MT8196 的 RX 起点正好是 `0x0e0` = 它 12 条 TX 的总和。**算法成立。**

### 4.4 MT6895 实际结果

```
MT6895 TX (AP -> GPUEB):
  FAST_DVFS_EVENT  slot=  0 byte=0x000  4 slots / 16 bytes
  GPUFREQ          slot=  4 byte=0x010  8 slots / 32 bytes   ← 我们发这里
  SLEEP            slot= 12 byte=0x030  3 slots / 12 bytes
  TIMER            slot= 15 byte=0x03c  3 slots / 12 bytes
  FHCTL            slot= 18 byte=0x048  9 slots / 36 bytes
  CCF              slot= 27 byte=0x06c  4 slots / 16 bytes
  GPUMPU           slot= 31 byte=0x07c  6 slots / 24 bytes
  FAST_DVFS        slot= 37 byte=0x094  6 slots / 24 bytes
  TX total = 43 slots (0x0ac)

MT6895 RX (GPUEB -> AP):
  FAST_DVFS_EVENT  slot= 43 byte=0x0ac  4 slots / 16 bytes
  GPUFREQ          slot= 47 byte=0x0bc  8 slots / 32 bytes   ← 应答读这里
  SLEEP            slot= 55 byte=0x0dc  1 slots / 4 bytes
  TIMER            slot= 56 byte=0x0e0  1 slots / 4 bytes
  FHCTL            slot= 57 byte=0x0e4  1 slots / 4 bytes
  CCF              slot= 58 byte=0x0e8  4 slots / 16 bytes
  GPUMPU           slot= 62 byte=0x0f8  1 slots / 4 bytes
  FAST_DVFS        slot= 63 byte=0x0fc  6 slots / 24 bytes
  total = 69 slots (0x114)  limit=160 slots (0x280)
```

**绝对地址**

| 用途 | 地址 | 长度 |
|---|---|---|
| GPUFREQ TX（AP 写） | `mbox0_base + 0x10` = `0x13c5fd90` | 32 B |
| GPUFREQ RX（AP 读） | `mbox0_base + 0xbc` = `0x13c5fe3c` | 32 B |

> **交叉印证**：MT6895 的 TX 偏移 `0x000/0x010/0x030/0x03c` 与 MT8196 的前四条**完全一致**。两代 SoC 用同一布局，说明这套表是固化在设计里的。

### 4.5 未验证项

`recv_table` 的 `recv_opt` 第 4 列在 vendor 的 `gpueb_mbox_setup_pin_table()` 里**不参与 offset 计算**，只影响 ISR 分派（`mtk_tinysys_ipi.c:641-647`）。我的推算假设「offset 只由 msg_size 决定」，与代码一致。

但 **GPUEB 固件侧是否用同一张表**，只能从固件字符串 `[MBOX]recv pin mbox=%u off=%u ...`（`[FW]`）确认它有等价的 pin 表结构，**具体数值未从固件反汇编出**。这与 §9 第 2 项「真机 dump」相关。

---

## 5. `CMD_INIT_SHARED_MEM (0)` 完整交互

### 5.1 谁分配内存

**AP 分配，GPUEB 不分配。** 内存在 LK 阶段就作为 reserved-memory 划好（`mblock-38-me_GPUEB_SHARED`，`0x7c600000`，2 MiB），AP 只需把**物理地址**告诉 GPUEB。

`[VND] gpufreq_v2.c:1946-1958`：

```c
g_status_shared_mem_va = shared_mem_va;              /* ioremap_wc 得到的 VA */
status_shared_mem_pa = shared_mem_pa;                /* LK 保留的 PA */
status_shared_mem_size = GPUFREQ_STATUS_MEM_SZ;      /* 0x400 */

g_debug_shared_mem_va = shared_mem_va + GPUFREQ_STATUS_MEM_SZ;
debug_shared_mem_pa   = shared_mem_pa + GPUFREQ_STATUS_MEM_SZ;
debug_shared_mem_size = shared_mem_size - GPUFREQ_STATUS_MEM_SZ;

send_msg.cmd_id            = CMD_INIT_SHARED_MEM;
send_msg.u.addr.status_base = status_shared_mem_pa;
send_msg.u.addr.status_size = (uint32_t)status_shared_mem_size;
send_msg.u.addr.debug_base  = debug_shared_mem_pa;
send_msg.u.addr.debug_size  = (uint32_t)debug_shared_mem_size;
```

`GPUFREQ_STATUS_MEM_SZ = 0x400`（`gpufreq_v2.h:19`）。

### 5.2 xaga 实际参数

`gpueb_mem_table = <0 0x1000>, <1 0x180000>`（`[DTS]:4899`）→ MEM_ID_GPUFREQ 只有 **`0x1000`（4 KiB）**，MEM_ID_LOG 是 `0x180000`（1.5 MiB）。

保留区起点 `0x7c600000`（2 MiB 总），MEM_ID_GPUFREQ 排在第一个。

所以：

| 参数 | 值 |
|---|---|
| `cmd_id` (`u32[0]`) | `0` |
| `target` (`u32[1]`) | `0`（不用） |
| `status_base` (`u32[3:2]`) | `0x000000007c600000` |
| `debug_base` (`u32[5:4]`) | `0x000000007c600400` |
| `status_size` (`u32[6]`) | `0x400` |
| `debug_size` (`u32[7]`) | `0xc00`（= `0x1000 - 0x400`） |

> **注意** `debug_size` 用 `mem_size - 0x400 = 0xc00`，**不是** 2 MiB 减 0x400。因为 `MEM_ID_GPUFREQ` 只有 4 KiB，`MEM_ID_LOG` 是另一块（在 `0x7c601000` 起）。用 2 MiB 去减会让 GPUEB 认为 debug 区跨越了两个 mblock。

### 5.3 GPUEB 回什么

**回一条同格式消息到 GPUFREQ 的 RX slot（slot 47）。** AP 读 `u32[2]` 即 `u.return_value`。

证据链：`gpufreq_ipi_to_gpueb()`（`gpufreq_v2.c:1706-1733`）只检查传输返回值，不读 `g_recv_msg`：

```c
ret = mtk_ipi_send_compl(get_gpueb_ipidev(), g_ipi_channel, IPI_SEND_POLLING,
        (void *)&data, GPUFREQ_IPI_DATA_LEN, IPI_TIMEOUT_MS);
if (unlikely(ret != IPI_ACTION_DONE)) {
        GPUFREQ_LOGE("[ABORT] fail to send IPI command: %s (%d)", ...);
        gpufreq_abort();
        goto done;
}
GPUFREQ_LOGD("channel: %d receive IPI command: %s (%d)",
        g_ipi_channel, gpufreq_ipi_cmd_name[g_recv_msg.cmd_id], g_recv_msg.cmd_id);
```

**它只打日志，不校验 `return_value`。** 即 `CMD_INIT_SHARED_MEM` 的「成功」判据就是**传输层收到应答**（`IPI_ACTION_DONE`）。

> 固件侧有 `[E]%s: status mem size: 0x%x < status data size: 0x%x`（`[FW]`，偏移 `0x1162c`）和
> `[E]%s: invalid mem status: (0x%llx, 0x%x) debug: (0x%llx, 0x%x)`（`[FW]`，偏移 `0x13885`）。
> **前者说明 GPUEB 会校验 `status_size` 是否 ≥ `sizeof(struct gpufreq_shared_status)`**，
> 所以 `status_size` 不能乱填，必须 ≥ `0x400`。后者是整体校验失败。

### 5.4 `IPI_SEND_POLLING` 轮询的是哪个寄存器

**两个阶段，两个寄存器：**

**阶段 1 — 等发送成功**（`mtk_tinysys_ipi.c:483-487`）

```c
ret = rpmsg_trysend(ept, data, len);          /* 内部读 TX_STS，忙则返回 MBOX_PIN_BUSY */
while (ret && ts_before(timeover)) {
        ipi_delay();                          /* udelay(10) */
        ret = rpmsg_trysend(ept, data, len);
        trysend_count++;
}
```

`rpmsg_trysend` → `mtk_mbox_send` → `mtk_mbox_check_send_irq()` → **读 `mbox0_send`（`0x13c62000`）的 bit1**。

**阶段 2 — 等应答**（`mtk_tinysys_ipi.c:510-525`）

```c
do {
        polling_count++;
        if (mtk_mbox_polling(mbdev, pin_r->mbox, pin_r->pin_buf, pin_r) == MBOX_DONE) {
                ret = 1; break;
        }
        if (try_wait_for_completion(&pin_r->notify)) { ret = 1; break; }
        ipi_delay();
} while (ts_before(timeover));
```

`mtk_mbox_polling()` → `mtk_mbox_read_recv_irq()` → **读 `mbox0_recv`（`0x13c62078`）的 bit1**，读到即 `mtk_mbox_clr_irq()` 写 `mbox0_clr`（`0x13c62074`）。

**→ 纯轮询只需要这两个寄存器，零中断。**

### 5.5 时序图

```
 AP (驱动)                                    GPUEB (RV33 固件)
 ─────────                                    ─────────────────
 1. 读 0x13c62000 bit1     ──── TX_STS ────►  (硬件)
    期望 0；为 1 说明上次没被取走 → 等或报错

 2. 写 32B 到 0x13c5fd90   ──── SRAM ─────►   (硬件，此时 GPUEB 还看不到)
    [0]=0x00 (INIT_SHARED_MEM)
    [2..3]=0x7c600000
    [4..5]=0x7c600400
    [6]=0x400
    [7]=0xc00

 3. mb() 内存屏障

 4. 写 0x13c62004 = BIT(1) ──── IRQ_SET ───►  GPUEB MBOX ISR 触发
                                               ├─ 读 RX_STS
                                               ├─ 从 pin 表查 slot 4 / 32B
                                               ├─ 拷出消息，cmd_id=0
                                               ├─ 校验 status_size >= 0x400
                                               ├─ 记录 status_base/debug_base
                                               │  （之后往 0x7c600000 写运行状态）
                                               └─ 组应答消息写 slot 47

 5. 轮询 0x13c62078 bit1                     ◄── IRQ_SET (GPUEB→AP)
    每 10 µs 一次，最多 10 s                    写 bit1

 6. 读 0x13c5fe3c 32B       ◄──── SRAM ─────   (GPUEB 已写好)
    [2] = return_value

 7. 写 0x13c62074 = BIT(1) ──── IRQ_CLR ───►  清位，握手完成
```

---

## 6. `CMD_POWER_CONTROL (6)` 完整交互

### 6.1 消息内容

```c
msg.cmd_id = CMD_POWER_CONTROL;        /* u32[0] = 6 */
msg.u.power_state = power_on ? 1 : 0;  /* u32[2] = 1(POWER_ON) / 0(POWER_OFF) */
/* u32[1] target 不用；u32[3..7] = 0 */
```

`enum gpufreq_power_state { POWER_OFF = 0, POWER_ON };`（`gpufreq_v2.h:85-88`）

### 6.2 `power_control` 字段的位含义

**`power_control` 不是位域，是一个「使能开关」标量。**

`gpufreq_v2.c:117-139`：

```c
unsigned int gpufreq_power_ctrl_enable(void)
{
        unsigned int power_control = false;
        if (g_gpueb_support) {
                shared_status = (struct gpufreq_shared_status *)(uintptr_t)g_status_shared_mem_va;
                if (shared_status)
                        power_control = shared_status->power_control;   /* 整字读 */
        } else {
                power_control = gpufreq_fp->power_ctrl_enable();        /* = 1 */
        }
        return power_control;
}
```

AP 侧 plarform 实现（`gpufreq_mt6895.c:301-305`）：

```c
unsigned int __gpufreq_power_ctrl_enable(void) { return GPUFREQ_POWER_CTRL_ENABLE; }  /* = 1 */
```

**判定：`power_control != 0` 表示「GPU 电源归 GPUEB 管」；`== 0` 表示「AP 自己管」。**

所以：

- **没有「上电」位，也没有「shader power」位。** 上电动作是 `CMD_POWER_CONTROL` 这条**命令**，不是某个 bit。
- 若 `power_control == 0`，`gpufreq_power_control()` 会直接 `ret = GPUFREQ_SUCCESS; goto done;`（`gpufreq_v2.c:855-858`）**根本不发 IPI**。

> ⚠ **这是排查「为什么我们没发过 IPI」的关键**：如果 `power_control` 读出来是 0，vendor 路径自己也不会发 IPI。但上游 `gpueb_power.c:158-195` 的 `mt6895_gpueb_power_control()` **不检查 `power_control`**，无条件发。两种行为不同，见 §6.5。

### 6.3 `shader_present` 怎么写

**AP 不写，只读。** 它是 GPUEB 固件填的运行状态。

`gpufreq_v2.c:213-234` 只有读。`gpufreq_mt6895.c:325` `__gpufreq_get_shader_present()` 返回的是 AP 侧的 `g_shader_present`（EB 模式下不用）。

值定义（`gpufreq_mt6895.h:81-97`，`gpufreq_mt6895.c:4425`）：

```c
#define MFG3_SHADER_STACK0  (T0C0 | T0C1)   /* MFG7, MFG9  */
#define MFG4_SHADER_STACK1  (T1C0)          /* MFG8        */
#define MFG5_SHADER_STACK2  (T4C0 | T4C1)   /* MFG10, MFG12 */
#define MFG6_SHADER_STACK4  (T5C0)          /* MFG11       */
#define GPU_SHADER_PRESENT_6 \
        (MFG3_SHADER_STACK0 | MFG4_SHADER_STACK1 | MFG5_SHADER_STACK2 | MFG6_SHADER_STACK4)
#define SHADER_CORE_NUM  (6)
```

MT6895 是 6 核，正常值应为 `GPU_SHADER_PRESENT_6`。

**位索引部分可解（`gpufreq/v1/include/mt6885/mtk_gpufreq_plat.h:30-38`）：**

```c
#define T0C0  (1 <<  0)
#define T1C0  (1 <<  1)
#define T1C1  (1 <<  5)
#define T2C0  (1 <<  2)
#define T2C1  (1 <<  6)
#define T5C0  (1 << 17)
#define T5C1  (1 << 21)
#define T6C0  (1 << 18)
#define T6C1  (1 << 22)
```

**MT6895 需要的 `T0C1` / `T4C0` / `T4C1` 在这套 v1 头里没有定义（MT6885 无 T4）→ 未验证。**

可确定部分：`T1C0 = 0x2`，`T5C0 = 0x20000`。完整 6 核值**需要真机 dump 确认**（见 §9）。

**读它的用途**：`gpufreq_get_shader_present()` 是给 Mali DDK 用来决定 shader 电源域的（`gpufreq_v2.c:206-212` 注释）。如果在 `POWER_ON` 之后读到 `0`，说明 GPUEB 没真正把 shader 域点起来——**这正是我们当前 `SHADER_READY=0` 的对照组**。

### 6.4 `power_count` 谁递增

**GPUEB 固件递增，AP 只读。**

`gpufreq_v2.c:150-173`：

```c
int power_count = 0;
power_count = shared_status->power_count;
power_state = power_count > 0 ? POWER_ON : POWER_OFF;
```

AP 侧自己有独立的 `g_stack.power_count`（`gpufreq_mt6895.c:754-758`），但那只在 AP 管电源时用。

**⚠ 但这里有个已被证实的陷阱**：`power_count == 0` **不代表没上过电**。原厂 Android 下也是 0 而系统正常（任务给定的已确认事实）。原因：GPUEB 的 `power_count` 是**引用计数**，`POWER_OFF` 时会减回 0；而「上过电」这件事不留下痕迹（无 heartbeat / magic / timestamp）。

`gpufreq_v2.c:162` 的 `power_count > 0 ? POWER_ON : POWER_OFF` 用于**运行时**判断，不是历史判断。

### 6.5 上游与 vendor 的行为差异（**影响我们照抄哪一份**）

| | vendor `gpufreq_power_control()` | 上游 `mt6895_gpueb_power_control()` |
|---|---|---|
| 位置 | `gpufreq_v2.c:853-909` | `gpueb_power.c:158-195` |
| 是否检查 `power_control` | **是**（`:855-858`，为 0 直接返回成功不发 IPI） | **否**，无条件发 |
| 是否校验 `return_value` | 是（`:872-875`） | 是（`:180-183`） |
| 是否发 `CMD_COMMIT` | 否（注释明确不发） | 否（`:185-189` 注释解释原因） |
| 失败处理 | `gpufreq_config_dfd(power)` + log | 直接返回负数 |

**照抄建议：用上游 `gpueb_power.c` 的版本。** 因为它不依赖共享内存里 `power_control` 的初值（我们没跑过 `CMD_INIT_SHARED_MEM` 时那块内存是脏的，读 `power_control` 可能得 0 而**静默跳过整个上电**）。这一条能直接解释「一次 IPI 都没发过」。

---

## 7. 中断处理

### 7.1 硬件侧

- `MBOX_ISR` = `GIC_SPI 516` = **GIC INTID 548**（`[DTS]:4897` `interrupts = <0 0x204 4>`；`0x204 = 516`；SPI→INTID 偏移 32）
- vendor 注册名就是 `"MBOX_ISR"`（`mtk-mbox.c:702-703`、`:786-787`）

```c
ret = request_irq(minfo->irq_num, mtk_mbox_isr, IRQF_TRIGGER_NONE,
                "MBOX_ISR", (void *) minfo);
```

- 触发类型：DT 里是 `IRQ_TYPE_LEVEL_HIGH`，vendor 用 `IRQF_TRIGGER_NONE`（沿用 DT）

### 7.2 它在 `0x13c5fd80` 的哪个位置

**不在那里。** `0x13c5fd80` 是消息 SRAM，中断状态在 **`0x13c62078`（`recv`）** 上，bit1 = GPUFREQ 通道。

任务描述里「`MBOX_ISR` 在 `0x13c5fd80` 的哪个位置」这个提法本身是错的——`0x13c5fd80` 没有任何中断寄存器，它是 640 B 的消息缓冲区。

### 7.3 纯轮询可行性（**我们的关键约束**）

**可行，且上游/厂商都自带纯轮询路径。**

`mtk_ipi_send_compl(..., IPI_SEND_POLLING, ...)` 在 `IPI_SEND_POLLING` 模式下**不依赖中断**：

```c
} else {
        /* WAIT Mode */
        ret = wait_for_completion_timeout(&pin_r->notify, ...);   /* 这条需要中断 */
}
```

`IPI_SEND_POLLING` 分支（`mtk_tinysys_ipi.c:510-525`）是 `mtk_mbox_polling()` **直接读寄存器**，`try_wait_for_completion()` 只是顺手检查一下 ISR 有没有已经完成——**没有中断也能正常返回**。

`mtk_mbox_polling()` 的实现完全自足（`mtk-mbox.c`）：

```c
reg = mtk_mbox_read_recv_irq(mbdev, mbox);      /* 读 0x13c62078 */
irq_state = reg & (0x1 << recv_pin_index);
if (irq_state > 0)
        ret = mtk_mbox_clr_irq(mbdev, mbox, irq_state);   /* 写 0x13c62074 */
else
        return MBOX_PIN_BUSY;
ret = mtk_mbox_read(mbdev, mbox, pin_recv->offset, data, ...);   /* 读 SRAM */
```

**→ 纯轮询只需要：读 `0x13c62000`（TX 忙）、写 SRAM、写 `0x13c62004`（触发）、读 `0x13c62078`（RX 待收）、写 `0x13c62074`（清位）、读 SRAM。零中断。**

### 7.4 最坏延迟

轮询间隔 `ipi_delay()` = `udelay(10)`（`mtk_tinysys_ipi.c:21`：`#define ipi_delay() udelay(10)`）。

| 场景 | 延迟 |
|---|---|
| GPUEB 正常应答 | 一个轮询周期 ≈ **10 µs** + GPUEB 处理时间（实测未知，`[FW]` 里有 `gpufreq_task` 说明可能有 task 调度，估计 10–100 µs 量级） |
| 超时 | `IPI_TIMEOUT_MS = 10000`（`gpueb_ipi.h:10`）→ **10 s**。这是任务给定的 10 秒，源码确认 |
| 两次 IPI 之间 | 串行，第二条要等第一条完成（单 pin 加锁 `pin_lock`） |

**10 秒 × 2 条命令 = 最坏 20 秒。** 在内核启动路径上不可接受，建议：

- `CMD_INIT_SHARED_MEM` 用 10 s（首次，值得等）
- `CMD_POWER_CONTROL` 用 **100 ms 量级**，超时即判定「GPUEB 不理我」并走降级路径
- 或两条都用 1 s，配合 §8 的存活预检，能快速失败

### 7.5 若要启用中断（可选，非必须）

```c
/* 1) 使能 GIC INTID 548 (SPI 516)，level-high */
/* 2) ISR 里：读 0x13c62078 → 处理 bit1 → 写 0x13c62074 = BIT(1) */
/* 3) startup 时先写一次 0x13c62074 = BIT(1) 清残留（上游 :164 就这么做） */
```

**注意**：启用中断后必须**同时**保留轮询路径，因为 GPUEB 在极早期可能来不及响应中断。上游就是 `polling + completion` 双保险（`:519-522`）。

---

## 8. 时序约束：LK 加载了吗？我们还要不要加载？

### 8.1 LK 已经加载并启动

`strings lk_a.img` 含：

```
app_load_gpueb(fdt, b_mode) == NO_ERROR
load gpueb
tinysys-gpueb-RV33_A
me_GPUEB_FW / me_GPUEB_SHARED / vm-GPUEB-reserved
%s: GPUEB load from partition failed, ret: %d
%s: GPUEB boot-up timed out!!!
%s: GPUEB_PLL = 0x%x   %s: GPUEB_MUX = 0x%x
```

GPUEB 在 MFG0 域，MFG0 归 ATF/LK 管（`[VND] gpufreq_mt6895.c:3578` 注释 `MFG0 is off in ATF`），Linux 的 scpsys 域表从 MFG1 开始（`mtk-scpsys-mt6895.c`）。

### 8.2 我们不需要重新加载

**不要重新加载。** AP 侧只发 IPI。重载会破坏 GPUEB 已有的状态机（它可能正持有 PLL/buck）。

### 8.3 **不读 `0x13c40000`** 判断 GPUEB 是否在跑

三种手段，按可靠性排序：

#### 手段 A（推荐）：读共享内存 status 区（`0x7c600000`，普通 DRAM，零风险）

```c
/* 注意：这不是「GPUEB 活着」的证明，是「GPUEB 曾经写过这块内存」的证明 */
u32 *st = (u32 *)0x7c600000;
u32 power_control  = st[0x60/4];
u32 dvfs_state     = st[0x64/4];
u32 shader_present = st[0x68/4];
int power_count    = (int)st[0x6c/4];
```

判据：

- `power_control == 1` → GPUEB 跑过完整的 gpufreq 初始化（这是它自己写的）
- `shader_present != 0` → GPUEB 至少枚举过 shader 核
- 全 0 / 全 0xFF → **未初始化**（我们没发过 `CMD_INIT_SHARED_MEM`，或发了但没成功）

**局限**：任务给定事实已确认「原厂 Android 下 `power_count` 也是 0 而系统正常」。所以 `power_count` 不能当判据。`power_control` 与 `shader_present` 才有信息量。

#### 手段 B：读 `0x13c62078`（`recv`）——**推荐作为第一判据**

`RX_STS` 是硬件寄存器，只要 mbox 的时钟在，就能读。它反映「对方有没有给我发东西」。

- 读 `0x13c62078`，若 bit1 已置位 → 说明 **GPUEB 主动给我们发过消息**（它活着并且在说话）
- 若为 0 → **不能证明它死了**（可能只是没话说）

**这是唯一一个「读寄存器但不碰 `0x13c40000`」的活体探针。** 风险：如果 mbox bank 时钟被关，读会挂。MFG0 常开，`[IOMEM]` 显示 bank 已映射，风险低但**未在 XNU 下实测**。

#### 手段 C（最安全）：直接发 `CMD_INIT_SHARED_MEM` 并看是否超时

这是**行为判据**，不是状态判据：

- 发出去，`0x13c62000` bit1 一直不 busy（说明 SRAM 可用）+ `0x13c62078` bit1 在 10 s 内没置位 → GPUEB 不在 / 不理
- 收到应答 → GPUEB 活着

**代价**：最坏等 10 s。**收益**：这是唯一 100% 可靠的判据。

#### 建议顺序

```
1. 读 0x7c600000 的 power_control / shader_present    (零风险，先看历史)
2. 读 0x13c62078 看 bit1                              (低风险，看对方有没有话)
3. 发 CMD_INIT_SHARED_MEM，超时 1 s                    (行为判据，最可靠)
4. 成功 → 发 CMD_POWER_CONTROL
```

### 8.4 时序约束清单

| 约束 | 依据 |
|---|---|
| 必须先 `CMD_INIT_SHARED_MEM`，再 `CMD_POWER_CONTROL` | `gpueb_power.c:70-114` 的 `gpueb_power_init_locked()` 在 `power_control` 前调用，且用 `gpueb_power_registered` 保证只做一次 |
| GPUFREQ 通道必须注册接收缓冲 | `mtk_ipi_register()` 要求 `msg != NULL`（`mtk_tinysys_ipi.c:264-265`），否则 `IPI_NO_MSGBUF` |
| 两条命令必须串行 | 单 pin 的 `pin_lock` + `mutex_send`（`mtk_tinysys_ipi.c:457-473`） |
| `mb()` 必须在写 SRAM 与写 IRQ_SET 之间 | `mtk_rpmsg_mbox.c:59-63` 明确注释 |
| `CMD_POWER_CONTROL` 必须在 `MCU_CONTROL = AUTO` **之前** | `panthor_fw.c:1131-1143`；`panthor_device.c:256-267` 还有一次更早的 |

---

## 9. 需真机 dump 清单

按「影响正确性」排序。每项都给了**具体命令**与**期望值**。

### D1. GPUFREQ 的 RX slot 是否真是 47（**最高优先级**）

我的推算依赖「TX 全排完才排 RX」这一条 vendor 代码行为。若 GPUEB 固件用了不同的表，应答会读错位置。

**验证方法**（原厂 Android，root）：

```sh
# 触发一次真实 IPI（跑一次 3D 负载），同时抓 mbox SRAM 快照
adb shell 'su -c "dd if=/dev/mem bs=1 skip=$((0x13c5fd80)) count=640 2>/dev/null | xxd" '
```

或直接看 vendor 的 debugfs：

```sh
adb shell 'su -c "cat /sys/kernel/debug/gpueb*/mbox*"'
adb shell 'su -c "cat /proc/gpufreqv2/status"'  # 看 dump 里的 pin table
```

**期望**：slot 4 有 `cmd_id` 在 `0x00..0x03`，slot 47 有应答。

**若不符**：改用 `mtk_ipi_send` 的 vendor 原生接口，别自己算 slot。

### D2. `shader_present` 的完整位掩码

需要 `T0C1 / T4C0 / T4C1` 三个位索引（MT6895 专用，v1 头里没有）。

**验证方法**：

```sh
adb shell 'su -c "cat /proc/gpufreqv2/status" | grep -i shader'
# 或
adb shell 'su -c "xxd -s $((0x7c600000+0x68)) -l 4 /dev/mem"'
```

**期望**：`GPU_SHADER_PRESENT_6` 的某个值，且**不是 0、不是 0x3f**（0x3f 是「连号位」的想当然值，MTK 的 bit 布局是跳的）。

### D3. `CMD_INIT_SHARED_MEM` 的应答 `return_value`

我在 §5.3 说「vendor 不校验」，但**固件可能回非 0**。

**验证方法**：kprobe 抓（见 §10）。

**期望**：`0`。

### D4. `power_control` 在上电前后的值

判断它是「常量 1」还是「会变」。

```sh
adb shell 'su -c "xxd -s $((0x7c600000+0x60)) -l 4 /dev/mem"'   # 息屏
# 点亮屏幕、跑游戏
adb shell 'su -c "xxd -s $((0x7c600000+0x60)) -l 4 /dev/mem"'   # 运行中
```

**期望**：两次都是 `1`（它是使能常量，不是状态）。

### D5. mbox 四个寄存器的 reset 值

```sh
adb shell 'su -c "xxd -s $((0x13c62000)) -l 4 /dev/mem; xxd -s $((0x13c62078)) -l 4 /dev/mem"'
```

**期望**：`0x00000000` / `0x00000000`（空闲态）。

### D6. `RX_STS` bit1 是否会因为「上一条没清」而卡住

决定失败回滚策略。

```sh
# 连续两次触发 IPI，看第二次是否 MBOX_PIN_BUSY
adb shell 'su -c "cat /sys/kernel/debug/gpueb*/mbox_recv"' 
```

---

## 10. 纯轮询版 C 伪代码

完整可编译版本见同目录 `gpueb-ipi.h`。这里给带超时与失败回滚的主流程。

```c
/* ─────────────────────────────────────────────────────────────
 * MT6895 GPUEB power-on, 纯轮询, 零中断
 * 依赖: gpueb-ipi.h
 * ───────────────────────────────────────────────────────────── */

#define GPUEB_MBOX_SRAM   0x13c5fd80u   /* 消息 SRAM, 640 B */
#define GPUEB_MBOX_SEND   0x13c62000u   /* RO: TX busy */
#define GPUEB_MBOX_SET    0x13c62004u   /* WO: raise IRQ */
#define GPUEB_MBOX_CLR    0x13c62074u   /* WO: clear IRQ */
#define GPUEB_MBOX_RECV   0x13c62078u   /* RO: RX pending */

#define GPUFREQ_CHAN      1             /* IPI_ID_GPUFREQ */
#define GPUFREQ_TX_SLOT   4             /* 见 §4.4 */
#define GPUFREQ_RX_SLOT   47            /* 见 §4.4 —— 不是 4！ */

#define GPUEB_SHARED_PA   0x7c600000u
#define GPUFREQ_STATUS_SZ 0x400u
#define GPUFREQ_DEBUG_SZ  0xc00u        /* = MEM_ID_GPUFREQ(0x1000) - 0x400 */

#define TMO_INIT_MS       1000u
#define TMO_POWER_MS      1000u

/* 返回 0 = 成功; 负数 = 失败原因 */
static int gpueb_ipi_xfer(u32 cmd, u32 target, u32 w2, u32 w3,
                          u32 w4, u32 w5, u32 w6, u32 w7,
                          u32 *ack_ret, u32 timeout_ms)
{
    u32 tx[8], rx[8];
    u32 deadline;
    int i;

    /* ---- 步骤 1: 等 TX 空闲（bit1 必须为 0） ---- */
    deadline = now_ms() + timeout_ms;
    while (mmio_read32(GPUEB_MBOX_SEND) & BIT(GPUFREQ_CHAN)) {
        if (now_ms() > deadline)
            return -GPUEB_ETXSTUCK;      /* 上一条没被取走 */
        udelay(10);
    }

    /* ---- 步骤 2: 组消息并逐字写 SRAM ---- */
    tx[0] = cmd;  tx[1] = target;
    tx[2] = w2;   tx[3] = w3;
    tx[4] = w4;   tx[5] = w5;
    tx[6] = w6;   tx[7] = w7;

    for (i = 0; i < 8; i++)
        mmio_write32(GPUEB_MBOX_SRAM + GPUFREQ_TX_SLOT*4 + i*4, tx[i]);

    mb();   /* 必须：SRAM 写完才能触发 IRQ */

    /* ---- 步骤 3: 触发 ---- */
    mmio_write32(GPUEB_MBOX_SET, BIT(GPUFREQ_CHAN));

    /* ---- 步骤 4: 轮询 RX pending ---- */
    deadline = now_ms() + timeout_ms;
    for (;;) {
        if (mmio_read32(GPUEB_MBOX_RECV) & BIT(GPUFREQ_CHAN))
            break;
        if (now_ms() > deadline) {
            /* 超时回滚：清掉可能残留的位，避免下次误判 */
            mmio_write32(GPUEB_MBOX_CLR, BIT(GPUFREQ_CHAN));
            return -GPUEB_ETIMEOUT;
        }
        udelay(10);
    }

    /* ---- 步骤 5: 读应答体 ---- */
    for (i = 0; i < 8; i++)
        rx[i] = mmio_read32(GPUEB_MBOX_SRAM + GPUFREQ_RX_SLOT*4 + i*4);

    /* ---- 步骤 6: 清位（应答） ---- */
    mmio_write32(GPUEB_MBOX_CLR, BIT(GPUFREQ_CHAN));

    if (ack_ret)
        *ack_ret = rx[2];        /* u.return_value，见 §3.2 */

    return 0;
}

/* ── 主流程 ───────────────────────────────────────────────── */
int mt6895_gpueb_power_on(void)
{
    u32 ack = 0;
    int ret;

    /* 0) 预检：读共享内存（零风险），仅用于日志，不作为判据 */
    gpueb_log_status();

    /* 1) INIT_SHARED_MEM */
    ret = gpueb_ipi_xfer(
            CMD_INIT_SHARED_MEM,          /* 0 */
            0,                            /* target: 不用 */
            GPUEB_SHARED_PA,              /* w2 = status_base[31:0]  */
            0,                            /* w3 = status_base[63:32] */
            GPUEB_SHARED_PA + GPUFREQ_STATUS_SZ,  /* w4 = debug_base[31:0]  */
            0,                            /* w5 = debug_base[63:32] */
            GPUFREQ_STATUS_SZ,            /* w6 = status_size */
            GPUFREQ_DEBUG_SZ,             /* w7 = debug_size  */
            &ack, TMO_INIT_MS);
    if (ret) {
        printf("gpueb: INIT_SHARED_MEM failed %d\n", ret);
        return ret;                       /* 不回滚：没改过任何硬件状态 */
    }

    /* 2) POWER_CONTROL + POWER_ON */
    ack = 0;
    ret = gpueb_ipi_xfer(
            CMD_POWER_CONTROL,            /* 6 */
            0,
            1,                            /* w2 = POWER_ON */
            0, 0, 0, 0, 0,
            &ack, TMO_POWER_MS);
    if (ret) {
        printf("gpueb: POWER_CONTROL failed %d\n", ret);
        return ret;                       /* 超时已清过位 */
    }
    if ((s32)ack < 0) {
        printf("gpueb: POWER_ON rejected, ret=%d\n", (s32)ack);
        return -GPUEB_EREFUSED;           /* 固件拒绝，见场景 S3 */
    }

    /* 3) 事后校验（软判据，不阻断） */
    {
        u32 shader = mmio_read32(GPUEB_SHARED_PA + 0x68);
        if (shader == 0)
            printf("gpueb: warn shader_present==0 after POWER_ON\n");
    }
    return 0;
}
```

### 失败回滚策略

| 失败点 | 已改动的状态 | 回滚动作 |
|---|---|---|
| TX 一直 busy（`-ETXSTUCK`） | 无 | 无。报告并放弃 |
| 写 SRAM 后 `SET` 超时 | 写了 SRAM + 置了 SET 位 | **写 `CLR` = BIT(1)** 清位。SRAM 内容是脏的但下次会覆盖 |
| 收到应答但 `ack < 0` | GPUEB 内部状态未变（它拒绝了） | 无。记录 `ack` |
| `INIT_SHARED_MEM` 成功、`POWER_CONTROL` 失败 | 共享内存地址已交给 GPUEB | **不回滚**。重发 `POWER_CONTROL` 是安全的（幂等） |
| 全部成功但 `shader_present == 0` | 电源可能只上了一半 | 记录；**不要**盲目重发（可能叠加引用计数） |

> **关键设计取舍**：`INIT_SHARED_MEM` 不做回滚。因为它只告诉 GPUEB「内存在哪」，不改变电源状态；而且上游用 `gpueb_power_registered` 标志保证它**只做一次**（`gpueb_power.c:77-78, 109`）。重复发是安全的但没必要。

---

## 11. 找不到的部分（诚实边界）

以下内容在公开资料里**确实找不到**，已用尽 vendor 源码 + 上游 mainline + 固件字符串：

| 缺什么 | 为什么找不到 | 影响 | 替代方案 |
|---|---|---|---|
| **GPUEB 固件侧的 cmd 分派表** | 固件是 RV32 裸二进制，`gpufreq_ipi_handler` 是字符串不是符号；我扫了 32-bit 字面量与 `lui/addi` 对，**0 命中**（固件用 `auipc` 相对寻址 + 运行时注册，静态扫不到）。`[FW]` 只确认存在 `[E]%s: invalid IPI cmd: %d` 串 | 不影响 AP 侧照抄（AP 只需知道 cmd 号） | 见 §12 kprobe 方案 |
| **`T0C1/T4C0/T4C1` 位索引** | MT6895 的 v2 头引用了它们，但定义在未被本仓库包含的 v1 MT6895 头里（只有 MT6853/MT6873/MT6885 有 v1 头，都没 T4） | 只能给部分 `shader_present` 值 | D2 |
| **mbox 四寄存器 reset 值** | 无文档，驱动不写初值 | 低（可用「读当前值当基线」绕过） | D5 |
| **GPUEB 应答延迟实测值** | 需真机 | 影响超时参数选择 | D3 顺带测 |
| **`power_control` 是否会被固件改写** | AP 侧代码只读；固件侧不可见 | 中（决定能否用它当判据） | D4 |

**结论：协议的「AP 侧要做什么」100% 清楚；「GPUEB 内部怎么想」有 4 处空白，都不阻塞写代码。**

---

## 12. 用真机 Android 抓 IPI 收发的具体方案

如果 §9 的 dump 不够，用 ftrace/kprobe 抓真实收发。**这是次优但可靠的路。**

### 12.1 可直接 kprobe 的函数（vendor 内核里都有符号）

| 函数 | 文件:行 | 抓什么 |
|---|---|---|
| `mtk_ipi_send_compl` | `drivers/soc/mediatek/mtk_tinysys_ipi.c:429` | 入口参数：`ipi_id`、`opt`、`len`、`timeout` |
| `mtk_ipi_send` | 同上 `:319` | 同上（非 compl 版） |
| `mtk_mbox_write` | `drivers/soc/mediatek/mtk-mbox.c:162` | `mbox`、`slot`、`len` → **直接给出真实 slot** |
| `mtk_mbox_read` | 同上 `:199` | 同上 → 应答从哪个 slot 读 |
| `mtk_mbox_trigger_irq` | 同上 `:262` | `irq` 位 → 确认 bit1 |
| `mtk_mbox_polling` | 同上 `:~460` | 轮询是否命中 |
| `mtk_mbox_clr_irq` | 同上 `:239` | 清位时机 |
| `gpufreq_power_control` | `drivers/gpu/mediatek/gpufreq/v2/gpufreq_v2.c:853` | `power` 参数 |
| `gpufreq_ipi_to_gpueb` | 同上 `:1706` | **static，但 kprobe 可用**（`p:... gpufreq_ipi_to_gpueb`） |
| `__gpufreq_power_control` | `gpufreq_mt6895.c:740` | EB 模式下不应被调用（可作反证） |
| `mtk_gpueb_mbox_send_data` | 仅 MT8196/上游 | MT6895 vendor 内核里**没有**这个函数（上游驱动不参与 vendor 构建） |

### 12.2 tracefs 路径与命令

```sh
adb root && adb shell

# ---- 1. 确认 tracefs 挂载 ----
mount | grep tracefs                       # 期望: /sys/kernel/tracing 或 /sys/kernel/debug/tracing
TR=/sys/kernel/tracing
[ -d $TR ] || TR=/sys/kernel/debug/tracing

# ---- 2. 加 kprobe（函数入口 + 关键参数） ----
# mtk_ipi_send_compl(ipidev, ipi_id, opt, data, len, timeout)
#   arg1=%di .. 用 $argN 语法（arm64: x0..x7）
echo 'p:ipi_send_compl mtk_ipi_send_compl ipi_id=%x1 opt=%x2 len=%x4 timeout=%x5' \
    > $TR/kprobe_events

# mtk_mbox_write(mbdev, mbox, slot, data, len) —— 这个直接给出 slot！
echo 'p:mbox_write mtk_mbox_write mbox=%x1 slot=%x2 len=%x4' \
    >> $TR/kprobe_events

# mtk_mbox_read(mbdev, mbox, slot, data, len)
echo 'p:mbox_read mtk_mbox_read mbox=%x1 slot=%x2 len=%x4' \
    >> $TR/kprobe_events

# mtk_mbox_trigger_irq(mbdev, mbox, irq)
echo 'p:mbox_trig mtk_mbox_trigger_irq mbox=%x1 irq_bit=%x2' \
    >> $TR/kprobe_events

# gpufreq_power_control(power)
echo 'p:gpufreq_pwr gpufreq_power_control power=%x0' \
    >> $TR/kprobe_events

# 返回值也要抓（retprobe），看 ack
echo 'r:ipi_send_compl_ret mtk_ipi_send_compl ret=$retval' \
    >> $TR/kprobe_events

# ---- 3. 打开 ----
echo 1 > $TR/events/kprobes/enable
echo 1 > $TR/tracing_on

# ---- 4. 触发：启动一个 3D 负载 ----
#   开游戏 / 跑 gfxbench / 甚至只是点亮屏幕
#   或者手动触发一次 power cycle：
#   adb shell 'echo 1 > /sys/class/kgsl/kgsl-3d0/force_bus_on'   # 视驱动而定
#   最稳：息屏 → 亮屏（会走 POWER_OFF/ON）

# ---- 5. 收 ----
echo 0 > $TR/tracing_on
cat $TR/trace | grep -E 'ipi_send|mbox_write|mbox_read|mbox_trig|gpufreq_pwr'
```

### 12.3 期望看到什么

```
gpufreq_pwr: power=0x1                      ← POWER_ON
ipi_send_compl: ipi_id=0x1 opt=0x1 len=0x8 timeout=0x2710
mbox_write: mbox=0x0 slot=0x4 len=0x8       ← ★ 确认 TX slot = 4
mbox_trig: mbox=0x0 irq_bit=0x2             ← ★ BIT(1) = 通道 1
ipi_send_compl_ret: ret=0x0                 ← IPI_ACTION_DONE
mbox_read: mbox=0x0 slot=0x2f len=0x8       ← ★★ 0x2f = 47，确认 RX slot
mbox_trig: mbox=0x0 irq_bit=0x2
```

**`mbox_read` 的 `slot` 参数就是 D1 的直接答案。** 如果它是 `0x2f`（47），我的推算正确；如果是 `0x4`，说明 GPUEB 用了不同表，必须改。

### 12.4 备用：不开 kprobe，用 ftrace function_graph

如果内核没开 `CONFIG_KPROBE_EVENTS`：

```sh
echo function_graph > $TR/current_tracer
echo 'mtk_ipi_*' > $TR/set_ftrace_filter
echo 'mtk_mbox_*' >> $TR/set_ftrace_filter
echo 'gpufreq_*' >> $TR/set_ftrace_filter
echo 1 > $TR/tracing_on
# ... 触发负载 ...
echo 0 > $TR/tracing_on
cat $TR/trace
```

只给调用序列，不给参数——但**足以确认「有没有发 IPI」和「收发次数」**。

### 12.5 备用：直接 dd /dev/mem 抓 SRAM

如果连 ftrace 都没有（`CONFIG_FUNCTION_TRACER` 关闭）：

```sh
# 抓 TX 区（slot 4 起 32 B）
adb shell 'su -c "dd if=/dev/mem bs=1 skip=$((0x13c5fd90)) count=32 2>/dev/null | xxd"'
# 抓 RX 区（slot 47 起 32 B）
adb shell 'su -c "dd if=/dev/mem bs=1 skip=$((0x13c5fe3c)) count=32 2>/dev/null | xxd"'
```

在**负载运行时**高频重复，能抓到「消息就位但还没被取走」的瞬间。

> 注：`/dev/mem` 在 `CONFIG_STRICT_DEVMEM=y` 下无法访问这些地址。原厂内核通常开着 strict。**这条是最后手段**，优先用 kprobe。

---

## 13. 给写代码的人：最小清单

按这个顺序，一步都不能跳：

1. map `0x13c5fd80`（640 B，SRAM，**normal memory 语义**，可 cache 但要 `mb()`）
2. map `0x13c62000`（4 B）、`0x13c62004`、`0x13c62074`、`0x13c62078`（各 4 B，**device 语义**）
3. map `0x7c600000`（4 KiB 就够，只要 status 区）
4. **不要** map `0x13c40000`
5. `readl(0x13c62078) & BIT(1)` → 有值说明 GPUEB 有话要说，先清掉再开始
6. 发 `CMD_INIT_SHARED_MEM`：`{0, 0, 0x7c600000, 0, 0x7c600400, 0, 0x400, 0xc00}`
7. 发 `CMD_POWER_CONTROL`：`{6, 0, 1, 0, 0, 0, 0, 0}`
8. 检查 `ack = rx[2]`，`(s32)ack < 0` 即拒绝
9. 读 `0x7c600068` 的 `shader_present`，为 0 则警告（不阻断）
10. 再去写 `MCU_CONTROL = AUTO`

**最容易犯的三个错**：

- 把 `0x13c5fd80` 当寄存器块（它是 SRAM）
- 应答从 slot 4 读（应该是 **slot 47**，字节 `0xbc`）
- `INIT_SHARED_MEM` 的 8 个字按「base/size/base/size」交错填（应该 base 是 u64，占 2 个字）

---

## 14. 现有 `hakios.mali610.c` 的确切缺口（逐行核对）

只读核对，未改动该文件。行号取自 `wc -l` = 5219 的版本。

### 14.1 一个 IPI 都没有

```
grep -c -i "ipi"  hakios.mali610.c     → 0
grep -c -i "mbox" hakios.mali610.c     → 1   （只有一行 #define，从未使用）
```

那唯一一行是 `:266`：

```c
#define MALI_GPUEB_MBOX_RECV    0x22078u
```

**它从未被任何代码引用**，而且它是**相对 `0x13c40000` 的偏移**。要用它就必须映射 `0x13c40000` —— 即禁区。所以这行不能直接用，必须改成我们头文件里的绝对地址 `0x13c62078`。

### 14.2 `gpueb_watch()` 全部落在禁区内

`[OUR]:2326-2360` 的 `gpueb_watch()` 读四个偏移：

| 宏 | 偏移 | 绝对地址 | 是否在 `0x13c40000..0x13c62000` 内 |
|---|---|---|---|
| `MALI_GPUEB_WDT_KICK` | `0x2061c` | `0x13c6061c` | **是（禁区）** |
| `MALI_GPUEB_WDT_CON` | `0x20618` | `0x13c60618` | **是（禁区）** |
| `MALI_GPUEB_IRQ_RAW` | `0x2033c` | `0x13c6033c` | **是（禁区）** |
| `MALI_GPUEB_GPR` | `0x1fd1c` | `0x13c5fd1c` | **是（禁区）** |
| `MALI_GPUEB_MBOX_RECV` | `0x22078` | `0x13c62078` | **否**（`0x22078 > 0x22000`，超出 gpueb_base 范围） |

**结论**：现有 `gpueb_watch()` 的存活探测（看 WDT kick 是否在变）**本身就违反「不读 `0x13c40000`」的约束**。
日志里 `gpueb: not mapped` 说明它连映射都没做——**这反而是对的**，因为它一旦映射就会碰禁区。

**建议**：`gpueb_watch()` 应改为只读 `0x13c62000` / `0x13c62078` / `0x7c600000`，见 §8.3。

### 14.3 与 panthor 的差异（`panthor_fw.c:1131-1143` 已核实）

panthor 在 `MCU_CONTROL = AUTO` 紧前面做：

```c
#if IS_ENABLED(CONFIG_MTK_GPUEB)
        if (of_machine_is_compatible("mediatek,mt6895") && mt6895_gpueb_available()) {
                ret = mt6895_gpueb_power_on();
                if (ret) { ... return ret; }
        }
#endif
        gpu_write(fw->iomem, MCU_CONTROL, MCU_CONTROL_AUTO);
```

我们 `[OUR]` 在 `kMcuControlAuto`（`[OUR]:392` 定义）之前**没有任何 GPUEB 调用**。这就是 `MCU_STATUS` 卡在 1 的直接对照。

### 14.4 缺口清单（按优先级）

| # | 缺什么 | 现状 | 补什么 |
|---|---|---|---|
| 1 | 两条 IPI 命令 | 0 调用 | 用 `gpueb-ipi.h` 的 `gpueb_ipi_xfer()` |
| 2 | mbox SRAM 映射 | 未映射 | map `0x13c5fd80` 640 B |
| 3 | ctl bank 映射 | 未映射 | map `0x13c62000` 128 B（覆盖 4 个寄存器） |
| 4 | 共享内存映射 | 未映射 | map `0x7c600000` 4 KiB |
| 5 | 存活判据 | 依赖禁区 WDT | 改读 `0x13c62078` + 共享内存 |
| 6 | 中断 | 0 注册 | **不需要**，纯轮询即可 |

**只有 1–4 是必须的，5 是改进，6 可以不做。**

---

## 附：证据文件位置

```
docs/headset-wip/raw/vendor-gpueb/          ← 厂商 5.10 树原件
  mtk-mbox.c  mtk-mbox.h                    (1022/317 行)
  mtk_tinysys_ipi.c  mtk_tinysys_ipi.h      (652/141 行)
  gpueb_ipi.c  gpueb_init.c  gpueb_plat_service.c
  gpueb_reserved_mem.c
  gpufreq_v2.c  gpufreq_v2.h  gpufreq_ipi.h
  gpufreq_mt6895.c

docs/headset-wip/raw/upstream-gpueb/        ← 上游 mainline 原件
  drivers_gpu_mediatek_gpueb_gpueb_power.c  (217 行，panthor 实际用的)
  mtk-gpueb-mailbox.c                       (319 行，★寄存器布局权威)
  Documentation_devicetree_bindings_mailbox_mediatek,mt8196-gpueb-mbox.yaml
```

交叉核对：`mtk-mbox.c` 在 vendor 与上游 **md5 完全相同**（`e43a0c8ee7df0c6a4df90d5413886815`），所以本文引用的 `mtk-mbox.c` 行号两边通用。
