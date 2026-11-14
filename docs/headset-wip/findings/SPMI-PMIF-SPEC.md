# SPMI / PMIF 事务规范（MT6895 · MT6368，HakiOS 裸机）

对象：`0x1c804000` 的 PMIF，SWINF_2 通道；从 XNU 裸机侧读 MT6368（SPMI **SID 5**）。
目标：**不可挂死**——每一次总线访问要么成功，要么在有界时间内失败并说明原因，永不把核心卡死、永不重启板子。

本文件所有结论都带出处。标记：

| 标记 | 含义 |
|---|---|
| **[U]** | 上游 mainline：`MT6895-Mainline/linux` @ `7.2-mt6895-xiaomi-xaga`，`drivers/spmi/spmi-mtk-pmif.c`（898 行） |
| **[U-h]** | 同一仓库，`drivers/soc/mediatek/mtk-pmic-wrap.c`（2617 行，老 pwrap 驱动） |
| **[V]** | 厂商源码：`XagaForge/android_kernel_xiaomi_mt6895` @ `17.0`，`drivers/spmi/spmi-mtk-pmif-core.c`（1221 行） |
| **[V-bin]** | 本机 TWRP 镜像里的 `spmi-mtk-pmif.ko` 反汇编/`.rodata` |
| **[O]** | 本板实测，记录在 `hakios-mtk` 里（行号见文） |
| **[未验证]** | 没有任何一手证据，只是机制推断 |

审计对象（**只读**，我未修改其中任何一个字节）。

⚠️ **这三个文件在我审计期间被并行修改了三次**（不是我改的）：

| 时刻 | 文件 | 变化 |
|---|---|---|
| 23:27 | `keys.c`、`headset.c` | 大改：keys 删掉旧探针并新增 headset 调用；headset 从 145 行扩到 537 行 |
| 23:44 | `headset.c` | 小改：加 `mode != 2 \|\| addr == 0x014b \|\| addr == 0x0240` 到 `update_bits()`；EINT 处理重构 |

**所有行号以这张表为准**（23:44 之后的版本）。PMIF 事务函数（`pmif_wait` / `mt6368_read` / `mt6368_write`）在 23:44 那次改动里**逐字节未变**（`diff` 已确认），所以 §7.2/7.5 的结论与行号全部仍然有效。

| 文件 | md5 | 行数 |
|---|---|---|
| `mtk/mt6895/drivers/input/hakios.mt6895_keys.c` | `fd9a28032aec6724cc2bebfd9a6e813b` | 643 |
| `mtk/mt6895/drivers/audio/hakios.mt6895_headset.c` | `28fe17285d1e485140d61382c83668c3` | 545 |
| `mtk/mt6895/drivers/gpu/hakios.mali610.c` | `212f2088c6972a7053d37b5cafc04bf1` | — |

`headset.c` 行号对照（旧 = 23:30 快照 `997e0fc...`，新 = 当前 `28fe172...`）：

| 内容 | 旧 | 新 |
|---|---|---|
| `#define PMIF_SIZE 0x1000` | 27 | **28** |
| `pmif_wait()` 定义 | 105 | **108** |
| `pmif_wait` 里的自旋循环 | 109 | **110** |
| `mt6368_read()` | 118 | **120** |
| 读：`pmif_wr(ACC, ...)` | 124 | **125** |
| 读：`pmif_wr(VLDCLR, 1u)` | 130 | **131** |
| `mt6368_write()` | 138 | **140** |
| 写：禁写寄存器门 | 141 | **142** |
| 写：`pmif_wr(WDATA, ...)` | 144 | **145** |
| `update_bits()` 的写后读回校验 | 181-188 | **182-189** |
| `restore_configuration()` 的 VLDCLR 补 ACK | 285-287 | **287-289** |
| `ml_io_map(PMIF_BASE, PMIF_SIZE)` | 488 | **496** |
| `hakios_mt6895_headset_quiesce()` | 532 | **540** |

**`headset.c` 行号在 23:44 后整体偏移 +1..+8**，下表给出对照。凡本文引用 `headset.c` 的行号，都是右列（当前）值。

---

## 0. 结论摘要

1. `ACC` 是**一个 32 位命令字**，不是位域拼装的多个寄存器：`opc[31:30] | WR[29] | SID[27:24] | (len-1)[23:16] | ADDR[15:0]`。**没有** USID / PID / BCUSID / A 位。
2. `STA[3:1]` 是通道状态机，`0`=IDLE、`6`=WFVLDCLR（有效待清）。`STA[18]`=ERR、`[17]`=SBUSY、`[15]`=DONE。
3. `VLDCLR` 是 **W1C 事务 ACK**：写 1 让 FSM 从 WFVLDCLR 回到 IDLE。**不清它，下一次事务的 IDLE 等待永远不可能成功**——这就是上游注释里点名的那个 bug。
4. 两个驱动都在 `IDLE` 超时时**只补一次 ACK 然后返回错误**，没有人用 `SW_RST`。PMIF 里**没有** SW_RST 寄存器（`[U]`、`[V]` 全文没有）。
5. `≥0x900` 不是 SWINF_3，是**另一个设备**：`spmi_pmif_mpu@1c804900`，`size 0x500`。`iomem.txt:264` 与 `xaga_live.dts:2150-2154` 都指着它。
6. **STA 的错误位无法在越界前拦住越界**——它描述的是"已经发到通道上的 SPMI 事务"，而越界在更早一步的总线访问上就 fault 了。唯一防线是**地址白名单**。
7. 现有实现有 **9 个缺陷，其中 5 个会挂死或重启**（§7.7）；三份驱动各自独立实现了同一套事务，互相无锁。B1 与 B3 必须一起修。

---

## 1. `ACC`：命令字完整位定义

### 1.1 位表

| 位 | 名称 | 宽度 | 含义 | 出处 |
|---|---|---|---|---|
| `31:30` | `opc` | 2 | PMIF 自己的操作码，**不是 SPMI opcode** | `[U]:25-28`、`[U]:389`、`[U]:467` |
| `29` | `WR` | 1 | `1`=写，`0`=读 | `[U]:467`（写路径唯一的 `BIT(29)`）；`[V]:501` 同 |
| `28` | —— | 1 | 恒 0。SID 只有 4 位，`sid & ~0xf` 被两个驱动直接拒 | `[U]:356`、`[U]:425`、`[V]:387`、`[V]:459` |
| `27:24` | `SID` | 4 | SPMI 从地址。本板：MT6363=4、MT6368=5 | `[U]:389`、`[O]` `xaga_live.dts:1134/1796` |
| `23:16` | `BC` | 8 | **字节数减一**。单字节访问 = `0` | `[U]:389` `(len - 1) << 16` |
| `15:0` | `ADDR` | 16 | PMIC 寄存器地址，**就在 ACC 里**，不是另一个寄存器 | `[U]:389` 尾部 `| addr` |

`opc` 取值（`[U]:25-28`）：

| 值 | 宏 | 对应 SPMI 形状 |
|---|---|---|
| 0 | `PMIF_CMD_REG_0` | SPMI `>= 0x80`（Zero Write） |
| 1 | `PMIF_CMD_REG` | SPMI `0x40..0x5f`（Write）/ `0x60..0x7f`（Read），8 位寄存器空间 |
| 2 | `PMIF_CMD_EXT_REG` | 未被两个驱动使用 |
| 3 | `PMIF_CMD_EXT_REG_LONG` | SPMI `0x20..0x2f` / `0x38..0x3f`，**16 位扩展寄存器空间，MT63xx 走这条** |

### 1.2 构造公式

```c
/* 读一个字节： [U]:389 */
cmd = (PMIF_CMD_EXT_REG_LONG << 30) | (sid << 24) | ((len - 1) << 16) | addr;

/* 写一个字节： [U]:467 —— 只多 BIT(29) */
cmd = (PMIF_CMD_EXT_REG_LONG << 30) | BIT(29) | (sid << 24) | ((len - 1) << 16) | addr;
```

本板四个具体值（`[O]` 实测残值与 §7 推导交叉一致）：

| 操作 | dword |
|---|---|
| 读 MT6368 `0x26b6`（accdet AB） | `0xC50026B6` |
| 写 MT6368 `0x0241`（VBUCK2 set） | `0xE5000241` |
| 读 MT6363 `0x001e`（keys） | `0xC400001E` |
| 读 MT6363 `0x002a`（TOPSTATUS 候选） | `0xC400002A` |

`0x880` 在 Android 侧读回 `0xC50011C4`（`hakios.mt6895_keys.c:122`），低 16 位 `0x11C4` 正是某个被丢弃命令的地址字段——**ACC 不是只读的影子寄存器，它保留最后一次写入**，所以它不能用来判断"命令有没有被接受"。

### 1.3 关于 USID / PID / BCUSID（问题里问到的）

这三者在**这个命令字里不存在**：

- `USID`（User Slave ID）/ `PID`（Peripheral ID）属于 SPMI **帧**层，由 PMIF 根据 `opc` 自行组装。两个驱动都没有任何 USID/PID 字段（`[U]` 全文 grep 无命中，`[V]` 同）。
- `BCUSID`（broadcast）无法表达：SID 只有 4 位，且 `[U]:356`、`[V]:387` 明确拒绝 `sid & ~0xf`。**广播的一次性多 PMIC 写在本驱动栈里不可达**——这正好，我们不该广播。

---

## 2. `STA`：通道状态

### 2.1 位表

| 位 | 名称 | 出处 | 用途 |
|---|---|---|---|
| `3:1` | `FSM` | `[U]:20-23` `GET_SWINF(x) = (x >> 1) & 0x7` | **唯一的同步点** |
| `0` | —— | `[O]` 恒 1 | 未验证，别用 |
| `4` | `REQ` | `[V-bin]` | 队列有请求 |
| `5` | `QFULL` | `[V-bin]` | 队列满 |
| `6` | `QEMPTY` | `[V-bin]` | 队列空 |
| `10:7` | `QFREECNT` | `[V-bin]` | 空闲槽数 |
| `14:11` | `QFILLCNT` | `[V-bin]` | 已用槽数 |
| `15` | `DONE` | `[V-bin]` | 事务完成 |
| `17` | `SBUSY` | `[V-bin]` | 软件忙 |
| `18` | `ERR` | `[V-bin]` | **错误 / NAK** |

`[V-bin]` 的来历要说清楚：厂商 `.ko` 里有个格式串
`"[err:%d, sbusy:%d, done:%d, qfillcnt:%d qfreecnt:%d, qempty:%d, qfull:%d, req:%d"`，
编译器为它生成的一串 `ubfx` 全部从**同一个寄存器**取位，位序为 `#18 #17 #15 #11(w4) #7(w4) #6 #5 #4`（`get_pmif_swinf_reg_dump`，`aarch64-linux-gnu-objdump` 于 `.text+0x3c48` 起的指令序列）。这套解码与本板实测的空闲值 `0x00008441` **自洽**：`done=1, qempty=1, qfreecnt=8, qfillcnt=0, err=0, sbusy=0, req=0`——一个空的、空闲的队列。这是 `[V-bin]` 而非 `[V]` 的原因：厂商 `spmi-mtk-pmif-core.c` 里没有这段（它是 `spmi-mtk-pmif-dbg` 之类的另一个编译单元）。

### 2.2 FSM 状态值

| 值 | 名称 | 出处 | 含义 |
|---|---|---|---|
| 0 | `IDLE` | `[U]:20` | 可以下命令 |
| 2 | `REQ` | `[U-h]:42` | 命令已受理 |
| 4 | `WFDLE` | `[U-h]:43` | **从机没应答，停在这里** |
| 6 | `WFVLDCLR` | `[U]:21` | 读数据有效，等 ACK |

`[O]` 佐证"停在 4"确实会发生：`hakios.mt6895_keys.c:158-159` 记录过"用 `PMIF_CMD_REG` 让状态机停在 4 而不是 WFVLDCLR"。

### 2.3 读超时应该判什么

**判 FSM，不判时间。** 上游两个驱动都是 `readl_poll_timeout_atomic(..., GET_SWINF(data) == SWINF_WFVLDCLR, PMIF_DELAY_US, PMIF_TIMEOUT_US)`（`[U]:396-398`）。轮询间隔 2 µs、总预算 10 ms（`[U]:30-31`）；厂商把预算放宽到 100 ms（`[V]:35-36`）。

等待分三段，**每段都必须有界**：

| 阶段 | 等什么 | 超时判定 | 出处 |
|---|---|---|---|
| 下命令前 | `FSM == IDLE` | 不是 IDLE → 若 `FSM==WFVLDCLR` 补一次 ACK 再报错 | `[U]:376-386` |
| 读：下命令后 | `FSM == WFVLDCLR` | 不是 → `ERR`/`WFDLE` → NAK | `[U]:396-403` |
| 读：ACK 后 | `FSM == IDLE` | 不是 → ACK 没生效 | `[U]:406`（上游不查，见 §7.7 B9） |
| 写：下命令后 | `FSM == IDLE` | 不是 → 写没被接受 | `[U]:464-471`（上游**完全不查**） |

**不要**用 `STA[15] DONE` 做主判据：它在空闲时也是 1（`0x8441`），没有区分力。它只能当辅助断言。

---

## 3. `VLDCLR` 的确切语义

| 问题 | 答案 | 出处 |
|---|---|---|
| 是什么 | 通道的**事务 ACK**，W1C | `[U]:406` `pmif_writel(arb, pbus, 1, inf_reg->ch_rdy)`；`[V]:442` 同 |
| 清什么 | 把 FSM 从 `WFVLDCLR` 退回 `IDLE`，并吞掉 RDATA 的 valid | 由 §2.2 状态迁移反推；`[U]` 把它命名为 `ch_rdy`（"channel ready"） |
| 必须清吗 | **必须**。读事务不清，下一次事务必挂 | `[U]:379-386`；`[U-h]:1424-1431` 原文见下 |
| 不清会怎样 | FSM 停在 6，之后**每一次**事务的 IDLE 等待都超时。不是变慢，是彻底不动 | `[U-h]:1424-1431` |
| 写事务要清吗 | **不要**。写路径根本不产生 `WFVLDCLR` | `[U]:464-471` 写路径无 WFVLDCLR 等待、无 ACK |
| 能读回校验吗 | **不能**。它是自清的，写完立刻读回是 0 | `[M]` 由 W1C 定义推出；本板未单独验证 |

`[U-h]:1424-1431` 原文（老 pwrap 驱动，同一套硬件语义）：

> Timeout issue sometimes caused by the last read command failed because pmic wrap could not got the FSM_VLDCLR in time after finishing WACS2_CMD. It made state machine still on FSM_VLDCLR and timeout next time. Check the status of FSM and clear the vldclr to recovery the error.

对应代码 `[U-h]:1433-1437`：`pwrap_leave_fsm_vldclr()`。

**"写完读回必须相等"是错的。** 本仓库里这条规则只在一处被写对（`hakios.mt6895_headset.c:130` 的注释），却在同一文件的 `:182-189` 里对普通寄存器通用化了——对普通寄存器是对的，对 ACK 值是错的，两者混用会埋新的坑（§7.7 B9 附近）。

---

## 4. 完整事务时序

### 4.1 读一个字节

```
 调用方
   │ ① 前置检查：sid ∈ {4,5}；addr 16 位；offset 在 0x000..0x8fe
   ▼
 ┌─────────────────────────────────────────────────────────────┐
 │ ② poll STA[3:1] == 0 (IDLE)          预算 10 ms / 2 µs 间隔 │
 │    否 → 若 FSM==6 写 VLDCLR=1 再 poll 一次；仍否 → E_BUSY    │
 └─────────────────────────────────────────────────────────────┘
   │ ③ W(ACC, 0xC0000000 | sid<<24 | addr)     ← 无时延要求
   ▼
 ┌─────────────────────────────────────────────────────────────┐
 │ ④ poll STA[3:1] == 6 (WFVLDCLR)                             │
 │    否 → FSM==4 ⇒ NAK；其它 ⇒ E_TIMEOUT；STA[18] ⇒ E_NAK      │
 └─────────────────────────────────────────────────────────────┘
   │ ⑤ v = R(RDATA) & 0xff        ← 必须在 ⑥ 之前，ACK 会吞掉它
   │ ⑥ W(VLDCLR, 1)               ← W1C ACK，不读回、不比较
   ▼
 ┌─────────────────────────────────────────────────────────────┐
 │ ⑦ poll STA[3:1] == 0 (IDLE)                                 │
 │    否 → E_ACK（通道留给了别人一个坏状态，见 §4.4）           │
 └─────────────────────────────────────────────────────────────┘
   │ ⑧ 返回 v
```

### 4.2 写一个字节

```
   │ ① 前置检查：sid；addr；addr ∉ {0x014b}；addr ≠ 0x0240
   ▼
 ② poll IDLE（同 §4.1 ②，含 WFVLDCLR 补 ACK）
   │ ③ W(WDATA, value)            ← 必须在 ④ 之前
   │ ④ W(ACC, 0xE0000000 | sid<<24 | addr)
   ▼
 ┌─────────────────────────────────────────────────────────────┐
 │ ⑤ poll STA[3:1] == 0 (IDLE)      ← 上游这里什么都不等        │
 │    否 → FSM==6 ⇒ 补 ACK 后 E_STUCK_VLDCLR；其它 ⇒ E_TIMEOUT  │
 └─────────────────────────────────────────────────────────────┘
   │ ⑥ 无 VLDCLR
```

### 4.3 步间时延

**没有手册级最小间隔。** 两个驱动都不 sleep，同步点只有 FSM 字段；`PMIF_DELAY_US = 2`（`[U]:30`）是**轮询间隔**，不是步间延时。所以规范写成：

| 步间 | 要求 |
|---|---|
| ② → ③ | 无（但每次 store 后要有 `dsb sy`，见 §4.5） |
| ③ → ④ | 无 |
| ④ → ⑤ | 无（FSM 到 6 就说明 RDATA 已稳） |
| ⑤ → ⑥ | 无 |
| ⑥ → ⑦ | 无（FSM 回 0 就说明 ACK 已生效） |

任何"为了保险 sleep 一下"都是**没有依据的**，而且会把一次 10 µs 的读拖成毫秒。要加就标注 `[未验证]`。

### 4.4 一次事务失败后，通道留给了谁

这是本文件最要紧的一节。失败不是"返回 false 就完了"：

| 失败点 | FSM 留在 | 后果 | 必须做 |
|---|---|---|---|
| ② IDLE 超时，且 FSM==6 | 6 | 通道死。**直到有人 ACK** | 写 `VLDCLR=1` |
| ② IDLE 超时，FSM==其它 | 不变 | 别人在用（keys 驱动 / LK） | **不要碰**，直接返回 |
| ④ WFVLDCLR 超时，FSM==4 | 4 | 从机 NAK。通道卡住，需外部复位 | 报错；**不要**盲目重试 |
| ④ WFVLDCLR 超时，FSM==6 | 6 | 罕见竞态 | 写 `VLDCLR=1` |
| ⑦ ACK 后不到 IDLE | ? | 通道状态未知 | 报错并**停用整个 PMIF 通路** |

### 4.5 内存序

现有三份实现的 `pmif_rd`/`pmif_wr` 都是 `dsb sy` 前后各一条（`hakios.mt6895_keys.c:191-206`、`hakios.mt6895_headset.c:90-105`、`hakios.mali610.c:608-621`）。这是**对的**，比上游的 `readl`/`writel` 还严。`ACC` 的写之后必须保证 store 已经出到设备，否则紧接着的 `STA` 轮询可能读到旧值——`dsb sy` 够。

---

## 5. 错误恢复

### 5.1 上游做什么

两个驱动**只做一件事**：IDLE 超时的时候，如果 FSM 是 `WFVLDCLR`，就写一次 `ch_rdy`（= `VLDCLR`），然后返回错误。

```c
/* [U]:379-386 */
if (ret < 0) {
        if (pmif_is_fsm_vldclr(arb, pbus))
                pmif_writel(arb, pbus, 1, inf_reg->ch_rdy);
        dev_err(&ctrl->dev, "failed to wait for SWINF_IDLE\n");
        return ret;
}
```

写路径同样（`[U]:455-462`）。厂商源码一模一样（`[V]:409-418`、`[V]:484-493`）。

### 5.2 有没有 `SW_RST`

**PMIF 里没有。** `[U]` 全文 898 行、`[V]` 全文 1221 行，grep `rst|reset|swrst` 只命中两处，都是 **SPMI 协议层的** `SPMI_CMD_RESET`（`pmif_arb_cmd`，`[U]:330-343`），那是给**从机**发复位命令，不是复位控制器。

老的 pwrap 驱动（`[U-h]`）有一个 `PWRAP_SW_RST`（`[U-h]:419`，偏移 `0x1A0` / `0x168`），但那是 pwrap 这一代硬件的，**不在 PMIF 的寄存器表里**——`[U]:129-223` 和 `[V-bin]` 的 `mt6xxx_regs` 都没有 `0x1a0` 或 `0x168`。

**结论：不要写 `0x1a0`。** 它不在窗口里的已知寄存器表内，属于 §6 的越界风险区（`0x1a0` 本身在 `0x8fe` 之内，但语义未知）。

### 5.3 正确的清错流程

```
STA[18] (ERR) 或 FSM==4 (WFDLE)
   │
   ├─ ① 先读 STA 存档（sta_before / sta_after 进日志）
   │
   ├─ ② 若 FSM==6：W(VLDCLR, 1)，再 poll IDLE
   │
   ├─ ③ 不要重试同一事务。NAK 是"从机说不"，重试只会再来一次 NAK
   │
   ├─ ④ 标记该通道为"需要 re-init"，把整个 PMIF 通路降级为只读/停用
   │
   └─ ⑤ 绝不写 CMDISSUE_EN / ARB_EN / INF_EN / TIMER_CTRL
```

**⑤ 为什么是硬红线**：`PMIF_CMDISSUE_EN`（`0x3b8`）有一位控制一个软件通道（`[U]:181` 表项 + 语义见 `hakios.mt6895_keys.c:430` 注释）。裸机侧把它写错，影响的是**所有**共用 SWINF_2 的消费者，包括 Android 起来之后的 keys/regulator/charger 路径。而 `[O]` 实测 `0x3b8 = 1`（bit0），而我们用通道 2——**这个值本身就和"SWINF_2 已使能"对不上**（见 §5.4）。

### 5.4 复位会不会影响 PMIC 其它模块

分三层回答：

| 手段 | 影响面 | 结论 |
|---|---|---|
| 只写 `VLDCLR=1` | 仅本通道的 ACK 位 | **安全**，这是唯一推荐动作 |
| 写 `SPMI_OP_ST_CTRL`（`0x1c801000+0`） | 给**从机**发 RESET/SLEEP/SHUTDOWN（`[U]:322-343`）。`SPMI_CMD_RESET` 会让该 SID 的 PMIC 复位，所有 regulator 掉 | **禁止**。`SPMI_CMD_SHUTDOWN` 更是直接关机 |
| 写 `PMIF_ARB_EN` / `CMDISSUE_EN` / `TIMER_CTRL` | 全局：所有通道、所有消费者 | **禁止** |

`[O]` 有一个直接证据说明为什么不能碰这些：`hakios.mt6895_keys.c` 的旧版本做过 `pmif_wr(PMIF_ARB_EN, arb ^ 0x80000000)` 和 `pmif_wr(PMIF_TIMER_CTRL, tim ^ 0x00010001)` 的写-回读探针（现在这段已被删除，`:430` 只留了一句"without the old timer/gate write probes"）。这类"写个值看看能不能写进去"的探针在共享总线上是不可接受的。

---

## 6. 为什么 `≥0x900` 会 bus abort

### 6.1 它是什么

**不是 SWINF_3。** SWINF_3 的 ACC 在 `0x8c0`、STA 在 `0x8e8`（`[U]:218-222`），都还在窗口内。`0x900` 起是**另一个设备**：

| 证据 | 内容 |
|---|---|
| `[O]` `work-xaga/dump/iomem.txt:263` | `1c804000-1c8048fe : 1c804000.spmi pmif` |
| `[O]` `work-xaga/dump/iomem.txt:264` | `1c804900-1c804dff : 1c804900.spmi_pmif_mpu pmif_mpu` |
| `[O]` `xaga_live.dts:2150-2154` | `spmi_pmif_mpu@1c804900 { compatible = "mediatek,mt6895-spmi_pmif_mpu"; reg = <0 0x1c804900 0 0x500>; }` |
| `[O]` `xaga_live.dts:1119` | pmif 节点只声明 `0x8ff` 字节 |

`pmif_mpu` 是 **PMIF 的访问权限单元**（access violation unit）。厂商驱动专门为它挂了中断：`IRQ_PMIF_ACC_VIO` / `IRQ_ALL_PMIC_MPU_VIO_V4` → `pmif_pmif_acc_vio_irq_handler()` → `aee_kernel_warning("PMIF", "PMIF:pmif_acc_vio")`（`[V]:562-569`、`[V]:620-621`、`[V]:692-694`）。也就是说：**访问 `0x900` 在 Android 上是一次被捕获的 violation + 内核告警；在我们这里，因为没有异常向量，就是核心停住 → 看门狗 → 重启。**

### 6.2 我们的窗口边界到底在哪

`size 0x8ff` 意味着合法偏移是 `0x000..0x8fe`（`iomem.txt:263` 的 `1c8048fe` 印证）。`0x8ff` 本身就已经越界了。最深的真实寄存器是 `SWINF_3_VLD_CLR = 0x8e4` 和 `SWINF_3_STA = 0x8e8`（`[U]:221-222`），`[V-bin]` 的 `mt6xxx_regs` 表最后一项也是 `0x8e4`。

**所以白名单必须写 `off <= 0x8fe`，不是 `0x8ff`，更不是 `0x8ff` 之后还允许 `0x900`。**

### 6.3 能不能用 STA 在越界前拒绝

**不能，而且这个想法本身是错的。** 两层原因：

1. **时间顺序反了。** `STA[18] ERR` 是 PMIF 把事务发到 SPMI 线上、从机没应答之后才置起（§2.1）。而越界发生在**更早一步**——CPU 的 load/store 在总线上就被 `pmif_mpu` 拦了。你根本走不到读 STA 那一步。
2. **越界之后读不了任何东西。** 一旦 abort，核心停住；就算有向量，`STA` 也在同一个已经出问题的块里。

**唯一的防线是纯软件的地址白名单**，在形成地址之前判：

```c
if (off > 0x8feu) return PMIF_E_OFFSET;   /* 在算地址之前，不是之后 */
```

配套的硬约束：**`PMIF_SIZE` 映射 0x1000（整页）是映射粒度，不是访问许可。** 三份现有实现都 `ml_io_map(PMIF_BASE, 0x1000)`（`hakios.mt6895_keys.c:93/424`、`hakios.mt6895_headset.c:28/488`、`hakios.mali610.c:3007/3088`），映射本身无害（不产生总线事务），但**它让 `0x900` 变成"能读"**——这正是事故 2 的形状。

### 6.4 "STA 报错之后才知道越界"这个误解的代价

`hakios.mt6895_keys.c:140-146` 的注释把这条推理写下来了：

> the next channel up would be 0x8c0 and the two after it 0x900 and 0x940 — the last two are past the end of the window. … the board rebooted into the same image three times running.

这段推理是**对的**（`0x8c0/0x900/0x940` 步长 0x40），而且已经把 `PMIF_CHAN_COUNT` 压到 1。但它把根因说成"循环走远了"，实际根因是**没有地址白名单**——一旦有人再写一个 `for (chan...)` 或者加一个"扫一遍找通道"的探针，同样的事故会重演。白名单是结构性的，`CHAN_COUNT = 1` 只是这一处的补丁。

---

## 7. 我们实现的审计（逐条）

审计覆盖三份文件：keys（SID 4）、headset（SID 5）、mali610（SID 5）。三份**各自独立**实现了同一套 PMIF 事务，互不知道对方存在。

### 7.1 命令字构造

| # | 结论 | 证据 |
|---|---|---|
| 1.1 | **对**。keys 用 `(3<<30) | (4<<24) | (0<<16) | addr`，SID、长度、opcode 都对 | `hakios.mt6895_keys.c:237` |
| 1.2 | **对**。headset 同样，SID 5 | `hakios.mt6895_headset.c:125`（读）、`:146-147`（写，含 `1u<<29`） |
| 1.3 | **对**。mali 写成 `MALI_PMIF_WRITE_BIT` 具名常量，可读性最好 | `hakios.mali610.c:3041-3044` |
| 1.4 | **对**。三处都硬编码 opcode=3（EXT_REG_LONG），与 MT63xx 的 16 位寄存器空间一致 | 同上 |

命令字这一项**三份实现都对**。这是唯一一项全对的。

### 7.2 超时保护

| # | 结论 | 证据 |
|---|---|---|
| 2.1 | **形式上对，实质上错**。三份都是 `for (spins = 0; spins < 200000; spins++)`，**有界**，不会死循环 | `hakios.mt6895_keys.c:229/239/249`、`hakios.mt6895_headset.c:110`、`hakios.mali610.c:3046/3058` |
| 2.2 | **错**。200000 次**没有时基**：每次循环是一次 MMIO 读 + 一次 `dsb sy`。在裸机、无 cache、跨 SoC 总线的条件下单次可能几百 ns 到几 µs，总预算 **0.1–1 秒**，不可预测 | 三处同上 |
| 2.3 | **错**。没有 `delay()`/`cntvct` 上限，轮询是满速自旋，会独占核心（keys 线程 20 ms 一次，headset 50 ms 一次） | `hakios.mt6895_keys.c:624`、`hakios.mt6895_headset.c:524-527` |

对照 §2.3：上游是 **2 µs 间隔 + 10 ms 预算**（`[U]:30-31`），厂商是 **10 µs + 100 ms**（`[V]:35-36`）。我们的 200000 次自旋与这两个都不是一个量级。

### 7.3 错误检查（有没有看 STA 的错误位）

| # | 结论 | 证据 |
|---|---|---|
| 3.1 | **缺失**。三份实现**只按 `STA[3:1]` 的 FSM 值做判断**，从不看 `STA[18] ERR`、`STA[17] SBUSY`、`STA[15] DONE` | `STA` 读取共 10 处：keys `:230/:240/:250`（判 IDLE / WFVLDCLR / IDLE）、`:473-474`（**完整 32 位读，但只 `printf`，不参与任何判断**）、headset `:110/:285`、mali `:3047/:3059/:3065`。除 `:473` 外全部 `(x >> 1) & 0x7`。**没有一处把 ERR/SBUSY/DONE 当条件** |
| 3.2 | **缺失**。NAK 与超时无法区分：`FSM==4` 和 `FSM==2` 都被归成"超时返回 false" | `hakios.mt6895_keys.c:244-246`、`hakios.mt6895_headset.c:126-128` |
| 3.3 | **错**。keys 在 `:247` 就把 `*value` 写出去了，而函数到 `:254` 还可能 `return false`——于是"失败"返回时调用方的变量**已经被改**。keys 的调用点（`:435`、`:524`）都在 `if (pmic_read(...))` 里用返回值兜住，所以当前不触发；但这个契约是错的 | `hakios.mt6895_keys.c:247` vs `:254`；headset 用局部变量 `result` 到 `:135` 才赋值，是正确写法 `hakios.mt6895_headset.c:129-136` |
| 3.4 | **对**。headset 的 `mt6368_read()` 先算到局部 `result`，全部成功后才 `*value = result` | `hakios.mt6895_headset.c:129-136` |

### 7.4 地址白名单

| # | 结论 | 证据 |
|---|---|---|
| 4.1 | **缺失**。没有任何一处检查"offset ≤ 0x8fe"。三份实现都用常量偏移，靠"人不去写错"保证安全 | 三份全文无 `0x8fe` |
| 4.2 | **部分补偿，但不够**。keys 把 `PMIF_CHAN_COUNT` 设成 1 并写了长篇注释解释为什么 | `hakios.mt6895_keys.c:149`、`:138-147` |
| 4.3 | **隐患**。`pmic_read_on(chan, ...)` 里 `PMIF_SWINF_ACC + chan * PMIF_CHAN_STRIDE` 是**运行时算术**，`chan` 是参数。现在只有 1 个通道，但函数签名允许 `chan=3` → `0x940` → 重启 | `hakios.mt6895_keys.c:222-225` |
| 4.4 | **隐患**。mali 的 `mali_spmi(addr, ...)` 只在**PMIC 地址**上做检查，`MALI_PMIF_*` 偏移是常量 | `hakios.mali610.c:3039-3071` |
| 4.5 | **对**。三份都不碰 `0x03b8`/`0x0150`/`0x0024`/`0x03e4` 的写（keys 曾经的写-回读探针已被删除） | `hakios.mt6895_keys.c:430` |

### 7.5 `VLDCLR` ACK 处理

| # | 结论 | 证据 |
|---|---|---|
| 5.1 | **对**。三份都写 `1` 到 `0x8a4` | `hakios.mt6895_keys.c:248`、`hakios.mt6895_headset.c:131`、`hakios.mali610.c:3071` |
| 5.2 | **对**。headset 明确注释"VLDCLR is a W1C transaction ACK. Validate IDLE, not a read-back of the ACK value." | `hakios.mt6895_headset.c:130` |
| 5.3 | **对**。keys 在 ACK 后**额外**等 IDLE（这是上游都没做的） | `hakios.mt6895_keys.c:249-253` |
| 5.4 | **缺失**。mali 写完 ACK **不验证**是否回到 IDLE | `hakios.mali610.c:3071` |
| 5.5 | **错（顺序）**。mali 在写事务里也走 `WFVLDCLR` 等待分支，但写事务不产生 WFVLDCLR —— 见 B4 | `hakios.mali610.c:3058-3066` |

### 7.6 串行化

| # | 结论 | 证据 |
|---|---|---|
| 6.1 | **完全缺失**。三份文件里没有任何 `lck_mtx` / `lck_spin` / `IOLock` / 关中断 | 全文 grep 零命中 |
| 6.2 | **缺失**。headset 与 keys 的串行化靠"在同一个线程里顺序调用"这一**约定**，不是机制 | `hakios.mt6895_keys.c:441`、`:623`；注释 `hakios.mt6895_headset.c:13-14` |
| 6.3 | **缺失（真并发）**。mali 是**独立内核线程**，启动后 `delay(2000 ms)` 再动 PMIF | `hakios.mali610.c:5049-5055`、`:5214` |
| 6.4 | **缺失**。headset 的 `hakios_mt6895_headset_quiesce()` 在 `loader_return()` 里调用，注释说"the shared PMIF worker is stopped"，但**没有任何代码真正停掉 keys 线程** | `xnu/files/osfmk/arm64/loader_return.c:374`；`hakios.mt6895_keys.c:517` 是 `for (;;)` 无条件循环 |

### 7.7 缺陷清单：**9 个**，其中 **5 个会挂死或重启**

数字先给清楚，再给清单。分三类，"会挂死"与"结果错"分开数：

| 类别 | 数量 | 编号 |
|---|---|---|
| **A. 会挂死 / 会重启** | **5** | B1 B2 B3 B6 B8 |
| **B. 不挂死但结果静默错误** | 3 | B4 B5 B9 |
| **C. 健壮性（拖到看门狗的风险）** | 1 | B7 |

A 类里再分可达性，因为"签名允许"和"现在就会踩"不是一回事：

| 可达性 | 编号 | 触发条件 |
|---|---|---|
| **现在就可达** | B1、B3、B6 | 默认启动路径上就会发生 |
| **潜伏（需一次代码改动/误用）** | B2、B8 | 函数签名允许、映射允许，当前调用点还没踩 |

| # | 缺陷 | 后果 | 证据 | 类 |
|---|---|---|---|---|
| **B1** | **mali 与 keys/headset 无锁并发共用 SWINF_2** | 两边同时读 `STA`、同时写 `ACC`；A 的 `VLDCLR` 会清掉 B 的事务，两边的 `ACC` 互相覆盖。FSM 停在谁也解释不了的状态，两边都只剩超时 | 独立线程 `hakios.mali610.c:5049-5055` + `:5214`（`kernel_thread_start`）对 keys `hakios.mt6895_keys.c:517`（`for (;;)`）。三份文件 grep `lck_mtx\|lck_spin\|IOLock` 零命中 | **A** |
| **B2** | **`pmic_read_on()` 的 `chan` 参数可做地址算术越界** | `chan=3` → `0x880 + 3*0x40 = 0x940`，落在 `pmif_mpu` 里 → 重启。函数签名允许，只有调用点 `PMIF_CHAN_COUNT=1` 挡着 | `hakios.mt6895_keys.c:222-225`（`PMIF_SWINF_ACC + chan * PMIF_CHAN_STRIDE`）、`:149` | **A** |
| **B3** | **IDLE 超时后不补 `VLDCLR` ACK** | 上一次读把 FSM 留在 6，此后**每一次**事务都在第一步超时。通道永久失效，日志只会说"did not answer"。**三份实现全缺**，上游/厂商都有（`[U]:381-382`、`[V]:414-415`） | keys `:234-236`、headset `:121-123`、mali `:3052-3053`，三处都是直接 `return false` | **A** |
| **B4** | **mali 的写事务等 `WFVLDCLR`** | 写路径不产生 WFVLDCLR（`[U]:464-471`）。`mali_spmi(write=true)` **必然**超时，`mali_vstack_rail()` 写 `0x241` 永远报失败——而写可能已生效。诊断与事实相反 | `hakios.mali610.c:3058-3066`：不区分读写地等 `MALI_PMIF_WFVLDCLR` | B |
| **B5** | **headset 初始化被嵌在"SID 4 读成功"分支里** | `hakios_mt6895_headset_init()` 在 `if (pmic_read(MT6363_KEYS_STATE, ...))` 的花括号内。SID 4 只要**瞬时**读失败一次（B3 一发作就必然失败），headset 模式 2 再也不初始化，日志只有"only the keypad's own line will work"，没有 headset 字样 | `hakios.mt6895_keys.c:435`（`if`）→ `:441` → `:462-475`（else 分支，不提 headset） | B |
| **B6** | **`restore_configuration()` 与 keys 线程并发** | `loader_return()` 注释写"the shared PMIF worker is stopped"，实际只调 `psci_smp_stop_others()`——停的是**其它 CPU**，不是 keys 内核线程。keys 的 `for (;;)` 无退出条件 | `xnu/files/osfmk/arm64/loader_return.c:373-374`；`hakios.mt6895_headset.c:538-544`；`hakios.mt6895_keys.c:517`、`:636` | **A** |
| **B7** | **200000 次无时基自旋** | 每次循环一次 MMIO 读 + `dsb sy`。裸机、非 cache、跨 SoC 总线，单次可能数百 ns 到几 µs → 总预算 **0.1–1 秒**，不可预测。失败时满速自旋独占核心 | keys `:229/:239/:249`、headset `:109`、mali `:3046/:3058`；`PMIF_SPIN_LIMIT 200000` 见 `:165` | C |
| **B8** | **`PMIF_SIZE = 0x1000` 映射整页** | 映射本身不产生事务，但它把"读 `0x900`"从"没人写得出这个地址"变成"合法 load"。DTS 只声明 `0x8ff` | `hakios.mt6895_keys.c:93`、`hakios.mt6895_headset.c:28`、`hakios.mali610.c:3007` | **A** |
| **B9** | **mali 写完 ACK 不验证回到 IDLE** | keys 在 ACK 后额外等 IDLE（`:249-253`），mali 写完就走（`:3071`）。ACK 没生效时调用方以为成功 | `hakios.mali610.c:3071` | B |

**按严重度**：B1 > B3 > B2 > B6 > B8 > B4 > B5 > B7 > B9。

**B1 与 B6 同根**：三份驱动各自实现了同一套事务，没有共享的串行化点。只修 B3 不修 B1，通道仍会被并发打坏；只修 B1 不修 B3，锁保护的是一个已经死掉的通道。**这两个必须一起修。**

### 7.8 每条的证据强度

代码能证实的写"代码可证"，需要真机的一律写"未验证"，不含糊：

| bug | 强度 | 依据 |
|---|---|---|
| B1 | **代码可证** | 三份文件无任何互斥原语（grep 零命中），mali 用 `kernel_thread_start` 起独立线程后 `delay(2000 ms)` 才动 PMIF（`hakios.mali610.c:5054`），keys 是 `for (;;)` 每 20 ms 轮询（`hakios.mt6895_keys.c:517/624`）。两者必然在 2 秒后重叠 |
| B2 | **代码可证** | `chan * PMIF_CHAN_STRIDE` 无上界检查；`chan=3` 的算术结果是 `0x940`，而 `0x940` 在 `pmif_mpu`（`iomem.txt:264`） |
| B3 | **代码可证** | 三处 `spins == LIMIT` 分支都是直接返回；`[U]:379-386` 与 `[V]:409-418` 都证明正确做法是补 ACK 再返回 |
| B4 | **代码可证** | `[U]:464-471` 的写路径**没有** WFVLDCLR 等待；mali `:3058` 无条件等它。逻辑上必然超时 |
| B5 | **代码可证** | `hakios_mt6895_keys.c:435` 的 `if` 花括号在 `:476` 才闭合，`:441` 在其中。缩进层级可直接看出 |
| B6 | **代码可证** | `psci_smp_stop_others()` 停的是 CPU，不是线程（`psci_smp.c:283` 注释："The system is going back to the boot loader"）；keys 线程 `for (;;)` 无退出路径（`:636` 只有启动失败才 return） |
| B8 | **代码可证**（映射行为）／**危害未验证** | `ml_io_map(PMIF_BASE, 0x1000)` 成功是代码事实；"有人真读 `0x900` 会怎样"从未被记录——但事故 2 报告过 ≥0x900 触发重启，所以危害是**外部证据**而非本地实验 |
| B7 | **量级未验证** | 没测过单次 MMIO 读的裸机耗时。只能说"200000 次不可预测"，不能说"一定 1 秒" |
| B9 | **代码可证**（缺验证步骤）／**后果未验证** | 不验证 IDLE 是事实；"ACK 失效的概率"没有任何数据 |

**没有一条是靠推理"可能有问题"凑数的**：9 条里 8 条的代码事实可以直接在 `文件:行号` 上读到，1 条（B7）的**量级**未定但**机制**确定。

## 8. 安全访问清单（强制前置检查）

在裸机里做**任何**一次 SPMI 读写，必须先过这 6 关。写成伪代码：

```
function spmi_read(sid, addr) -> (status, value):
    # ---- 关卡 1：SID 白名单 ----------------------------------------
    if sid not in {4, 5}:
        return (E_SID, -)            # 4=MT6363, 5=MT6368

    # ---- 关卡 2：PMIF 偏移白名单（在算地址之前！）--------------------
    # 这一步不是给 PMIF 的，是给编译器和 mmu 的：越界没有异常向量。
    for every pmif_offset this call will touch:
        if pmif_offset > 0x8fe:
            return (E_OFFSET, -)      # 0x8fe 是 DTS 声明的最后一字节

    # ---- 关卡 3：命令字形状 ------------------------------------------
    if addr > 0xffff:  return (E_CMD, -)      # 地址必须装得进 15:0
    cmd = (3 << 30) | (sid << 24) | (0 << 16) | addr
    if (cmd >> 30) != 3:               return (E_CMD, -)   # opc = EXT_REG_LONG
    if ((cmd >> 16) & 0xff) != 0:      return (E_CMD, -)   # BC = 0，只允许单字节
    if ((cmd >> 24) & 0x0f) != sid:    return (E_CMD, -)   # SID 没被地址挤掉

    # ---- 关卡 4：PMIC 寄存器禁写 -------------------------------------
    # 读也要留意：0x0240 是只读状态位，对它的任何 RMW 都禁止
    if addr == 0x014b and is_write:  return (E_REG, -)
    if addr == 0x0240 and is_write:  return (E_REG, -)

    # ---- 关卡 5：串行化 ----------------------------------------------
    # 必须与 keys 驱动、headset 驱动、mali 共享同一把锁。
    # 没有锁就不要发事务。
    lock(pmif_lock)                  # 见 §9 的未决项

    # ---- 关卡 6：有界执行 --------------------------------------------
    deadline = now() + 10 ms         # 上游预算 [U]:31
    if not wait_fsm(IDLE, deadline):
        if fsm() == WFVLDCLR:  write(VLDCLR, 1); wait_fsm(IDLE, now()+1ms)
        else:                  unlock(); return (E_BUSY, -)
        if fsm() != IDLE:      unlock(); return (E_BUSY, -)

    write(ACC, cmd)                  # 唯一一次 store
    if not wait_fsm(WFVLDCLR, deadline):
        st = fsm()
        unlock()
        return (st == 4 ? E_NAK : E_TIMEOUT, -)

    v = read(RDATA) & 0xff
    write(VLDCLR, 1)                 # W1C ACK，不读回
    if not wait_fsm(IDLE, now() + 1 ms):
        unlock(); return (E_ACK, v)  # 通道状态未知 → 调用方必须停用 PMIF
    unlock()
    return (OK, v)
```

### 8.1 写成 C 的断言形式（可以直接抄进驱动）

```c
/* 每一关都是一个真能编译、真能断言的函数，不是注释。 */
#define SPMI_ASSERT_PRECONDITIONS(x)                                    \
	do {                                                            \
		if (!pmif_sid_ok((x)->sid))          return PMIF_E_SID; \
		if (!pmif_offset_ok(PMIF_SWINF_2_ACC))  return PMIF_E_OFFSET; \
		if (!pmif_offset_ok(PMIF_SWINF_2_STA))  return PMIF_E_OFFSET; \
		if (!pmif_offset_ok(PMIF_SWINF_2_RDATA))return PMIF_E_OFFSET; \
		if (!pmif_offset_ok(PMIF_SWINF_2_VLD_CLR)) return PMIF_E_OFFSET; \
		if ((x)->write && !pmif_pmic_reg_writable((x)->addr)) \
			return PMIF_E_REG;                              \
		if (!pmif_cmd_is_byte_ext(pmif_cmd_for(x))) return PMIF_E_CMD; \
	} while (0)
```

`spmi-pmif.h` 里 `pmif_offset_ok()`、`pmif_sid_ok()`、`pmif_pmic_reg_writable()`、`pmif_cmd_is_byte_ext()`、`pmif_xfer_admit()` 就是这套清单的可编译版本，已经在 `gcc -std=c11 -Wall -Wextra -Werror` 下通过断言测试（`findings/spmi-pmif-hdrtest.c`）。

### 8.2 三条"永不做"

1. **永不**用 `PMIF_SIZE = 0x1000` 当作可访问范围。
2. **永不**为了"探测"而写 `ARB_EN` / `CMDISSUE_EN` / `INF_EN` / `TIMER_CTRL` / `SPI_MODE_CTRL`。
3. **永不**在拿不到锁的时候发事务——宁可返回 `E_BUSY`。

---

## 9. 未决项（明确标注，不要当成结论用）

| # | 未决问题 | 怎么定性 |
|---|---|---|
| U1 | `STA[0]` 恒 1 的含义 | 读一个已知空闲/已知忙的通道对比；`[V-bin]` 的 dump 串里没有 bit0 |
| U2 | `STA[18] ERR` 在 NAK 时是否真的置位 | 故意对不存在的 SID 发一次（**在不重要的通道上**，或先用模拟器），读 STA 存档 |
| U3 | 单次 MMIO 读在裸机下的真实耗时 | `cntvct_el0` 前后计时，跑 1000 次取分布——决定 200000 次到底是 0.1 秒还是 1 秒 |
| U4 | `PMIF_CMDISSUE_EN = 1` 与"SWINF_2 已使能"的矛盾 | `[O]` 实测 `0x3b8 = 0x00000001`。若 bit2 才是 SWINF_2 的闸门，那这个值是"SWINF_0 使能"——但我们明明在用 SWINF_2 且事务成功。**要么 bit 编号不是通道号，要么闸门不是必需的**。这是 `[O]` 与 `[U]:181 + keys.c:430` 注释的直接冲突，未解决 |
| U5 | 写事务成功但 `FSM` 停在 6 是否可能 | 只能靠真机 + 故意的非法写观测；模拟器里我按 `[U]:464-471` 建模为"直接回 IDLE" |
| U6 | 串行化用哪把锁 | XNU 侧没有现成的跨线程互斥；`lck_mtx` 可用但 key 驱动在 `kernel_thread` 上下文。**这是设计决策，不是查资料能解决的** |

---

## 10. 与上游 / 厂商的差异一览

| 项 | 上游 `[U]` | 厂商 `[V]` | 我们（三份） |
|---|---|---|---|
| 轮询间隔 | 2 µs | 10 µs | 无（满速自旋） |
| 总预算 | 10 ms | 100 ms | 200000 次（无时基） |
| IDLE 超时补 ACK | **有** `[U]:381-382` | **有** `[V]:414-415` | **无**（B3） |
| 读后等 IDLE | 无 | 无 | keys 有、headset 有、mali 无 |
| 写后等 IDLE | **无** | **无** | mali 等**错的状态**（B4） |
| 看 STA ERR | 无 | 无 | 无 |
| 锁 | `raw_spin_lock_irqsave` `[U]:373` | 同 `[V]:404` | **无**（B1） |
| 地址白名单 | 无（靠 DT） | 无（靠 DT） | 无（B2/B8） |
| SID 白名单 | `sid & ~0xf` `[U]:356` | 同 `[V]:387` | 无（硬编码常量） |
| 命令字 | 对 | 对 | 对 |

注意最后一列的对称性：**上游和厂商也都不看 `STA[18]`、也都不做地址白名单**。我们的问题不是"抄错了",而是"抄了一个为 Linux 设计的驱动，却没有 Linux 提供的三层保护"——DT 的 `reg` 范围（内核 `devm_ioremap_resource` 会检查）、`raw_spinlock`、以及异常向量。裸机这三样都没有，所以 §8 的清单必须自己补。

---

## 11. 复现与验证

模拟器：`docs/headset-wip/spmi-sim.py`，9 个断言，覆盖

1. 空闲 STA = `0x8441` 的字段解码；
2. 读事务全流程 + 命令字 `0xC50026B6` / `0xE5000241`；
3. 写事务（WDATA 先、无 VLDCLR）；
4. **不清 VLDCLR → 通道永久挂死**，补 ACK 后恢复；
5. **NAK → FSM 停在 4**，有界等待返回；
6. **`0x900` → bus abort**，且 abort 后整机不可用；
7. **SID 写成未移位的 `5` → SID 字段变 0**，落到 slave 0；
8. 短操作码（opc=1）→ 停在 4；
9. 禁写寄存器 + SID 白名单。

```
$ cd docs/headset-wip && python3 spmi-sim.py --selftest
ok   test_idle_sta_matches_this_board
ok   test_read_transaction_succeeds
ok   test_write_transaction_succeeds
ok   test_missing_vldclr_blocks_the_channel
ok   test_nak_parks_the_fsm_at_4
ok   test_out_of_window_is_a_bus_abort
ok   test_sid_not_shifted_into_bits_27_24
ok   test_short_opcode_parks_at_4
ok   test_forbidden_registers_and_sid_whitelist

9/9 passed
```

头文件：`findings/spmi-pmif.h`，测试 `findings/spmi-pmif-hdrtest.c`

```
$ gcc -std=c11 -Wall -Wextra -Werror -I findings findings/spmi-pmif-hdrtest.c -o /tmp/hdrtest && /tmp/hdrtest
header ok: read8(5,0x26b6)=0xc50026b6 write8(5,0x241)=0xe5000241 poll_max=5000
```

**模拟器不是证据。** 它证明的是"如果 §1–§3 的寄存器语义成立，那么 §4 的时序和 §7 的 bug 会这样表现"。§1–§3 的每条都有 `[U]`/`[V]`/`[O]` 出处；§9 列了哪里还没闭合。

---

## 12. 引用索引

| 简称 | 完整位置 |
|---|---|
| `[U]` | `https://raw.githubusercontent.com/MT6895-Mainline/linux/7.2-mt6895-xiaomi-xaga/drivers/spmi/spmi-mtk-pmif.c` |
| `[U-h]` | `https://raw.githubusercontent.com/MT6895-Mainline/linux/7.2-mt6895-xiaomi-xaga/drivers/soc/mediatek/mtk-pmic-wrap.c` |
| `[V]` | `https://raw.githubusercontent.com/XagaForge/android_kernel_xiaomi_mt6895/17.0/drivers/spmi/spmi-mtk-pmif-core.c` |
| `[V-bin]` | `work-xaga/dt-device_xiaomi_xaga-TWRP/recovery/root/lib/modules/spmi-mtk-pmif.ko`（`.rodata` + `get_pmif_swinf_reg_dump` 反汇编） |
| `[O]` dts | `work-xaga/dump/xaga_live.dts`（pmif 节点 `:1117-1132`；`pmic@4` `:1134`；`second_pmic@5` `:1796`；`spmi_pmif_mpu@1c804900` `:2150-2154`） |
| `[O]` iomem | `work-xaga/dump/iomem.txt:262-264` |
| `[O]` 实测残值 | `hakios-mtk/mtk/mt6895/drivers/input/hakios.mt6895_keys.c:115-123` |

**未使用的来源**：`drivers/soc/mediatek/mtk-pmic-wrap.h` 与 `mtk-spmi-pmic.c` 在 `7.2-mt6895-xiaomi-xaga` 与 `master` 两个 ref 上都是 404（`include/linux/soc/mediatek/` 目录列表里没有该头文件），所以本报告的 `[U-h]` 全部来自 `mtk-pmic-wrap.c` 本身。这一点明确记下来，避免后来者以为漏查。
