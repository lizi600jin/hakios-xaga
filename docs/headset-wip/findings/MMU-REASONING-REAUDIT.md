# MMU 推理链复核：「AS0 表走查被总线拒绝」全链作废

审计对象：`/home/lizi/IOS/hakios-mtk/mtk/mt6895/drivers/gpu/hakios.mali610.c`
审计快照：**5253 行，sha256 `947c0267c3303065c2b5ebfd7e8b01bc37288083d89633ec8fb6d7527f2bce10`**
（审计期间该文件被另一个会话持续改动：现为 5255 行 / `798642d5…65844a`，改动集中在 PMIF 预算重构 `:3017-3108`，
把 `mali_pmif_wait`/`mali_pmif_recover` 换成 `struct hakios_pmif_budget` 版本，**未触碰任何 MMU 注释**。
本报告的行号以 5253 行快照为准；脚本可按文本自动重定位，见 §6。）

上游依据：`/tmp/mt6895-mainline/drivers/gpu/drm/panthor/panthor_mmu.c:646-650`、
`panthor_mmu_regs.h:47-48`、`:40-45`、`:32-38`。

---

## 0. 一句话

`0x10000` 是 AS0 的 REQ_COMPLETE（bit16），不是 `MMU_BUS_ERROR(0)`。全链 33 个推理点：**作废 8、需重写 10、仍成立 12、未验证 3**，其中 **3 处「结论偶对、理由错」**。
「AS0 walk 是否成功」的答案本身需要改写：**走查是否成功，当前没有独立证据；但 UPDATE 命令退休有独立证据（AS_STATUS.bit0）**——而这两件事在过去被当成同一件事。

---

## 1. 推翻成立，且证据比给定的更硬

给定的事实只用了 panthor 的注释。审计时在自家日志里找到一条算术级旁证，把它从「上游注释」升级为「本机实测」：

`logs/headset-resume-20261114-220244/nopdc/kernel-lines.txt:176-181`

```
mali: AS2 parked, mmu irq 0x40000     bit18 = 16+2
mali: AS3 parked, mmu irq 0x80000     bit19 = 16+3
mali: AS4 parked, mmu irq 0x100000    bit20 = 16+4
mali: AS5 parked, mmu irq 0x200000    bit21 = 16+5
mali: AS6 parked, mmu irq 0x400000    bit22 = 16+6
mali: AS7 parked, mmu irq 0x800000    bit23 = 16+7
```

`0x10000` = bit16 = **16+0**。七个地址空间各置**自己的** `16+as` 位，位号与 AS 号严格线性对应——这正是 panthor
`panthor_mmu_irq_handler()` 里 `ffs(status | (status >> 16)) - 1`（`panthor_mmu.c:1792-1794`）所处理的编码。

三点推论，都直接杀死旧解读：

1. **AS2..AS7 并不「静默」**。它们各自置位，只是位置不同。旧推理的核心对照——「AS0 报错、邻居不报错」——在自家日志里就是假的。
2. 既然是「一个 AS 一位」，就不可能是「总线拒绝」这种全局故障。总线拒绝不会按 AS 号整齐编号。
3. 该位在 `UPDATE` 之后出现、在三个配置写之后不出现（日志 239-245：`0,0,0,0` 然后 `0x10000`），形态是**命令完成**。

顺带钉掉一个位数陷阱：全文件里 `0x10000` 只出现 3 次（`:2909`、`:4801`、`:4843`），而 `0x100000`（AS4）和
`0x10000000`（`MALI_TOPCKGEN_PHYS`）都在附近。把 AS4 的 bit20 读成 AS0 的 bit16，是这条链最可能的起点。

---

## 2. 受影响点全表

判定口径：
**仍成立** = 结论不依赖该位解读，或依赖的前提独立成立；
**需重写** = 结论可能仍对，论证要换；
**彻底作废** = 结论直接建立在错误前提上，必须删或反转；
**未验证** = 与本位无关，但缺证据。

行号为 5253 行快照。`任务行` 列给出任务书点名的行在该快照中的位置。

| # | 行 | 原文摘录 | 基于什么前提 | 判定 | 处理方法 |
|---|---|---|---|---|---|
| 1 | 450-455 | `UPDATE is one, and the phone's own register trace settles it … write one to AS_COMMAND at 0x2418` | 厂商 trace 记录 7 = UPDATE(1) | **仍成立** | 保留。可加一条独立旁证：`panthor_mmu_regs.h:34` `AS_COMMAND_UPDATE 1` 同值 |
| 2 | 469-471 | `mali_as0=1 parks address space 0 the way the other seven are parked … Nothing else changes: same clear, same three writes, same UPDATE` | 对照组机制 | **仍成立** | 保留。这是对照组机制的准确描述；作废的是它后面 `:472-481` 对结果的预期，不是机制本身。⚠️ 该对照组从未跑过，见 §4.3 |
| 3 | 472-476 | `AS0 … its UPDATE raises MMU_BUS_ERROR(0); AS2 through AS7 … their UPDATEs raise nothing` | 0x10000 = BUS_ERROR；邻居静默 | **彻底作废** | 整段删。事实是两者都置位，差别只在位号。由此衍生的「四个候选原因」（granule / 表内容 / 根地址 / 槽位）全部失去意义 |
| 4 | 478-481 | `If a parked AS0 also raises the bus error, the fault is in the slot … If a parked AS0 is silent … the error comes from the walk` | 同上二分法 | **彻底作废** | 删。两个分支都建立在「AS0 报错、邻居不报」上，两边都不成立 |
| 5 | 655-658 | `The original reason … was an interpretation of IRQ bit16 … as a root-fetch bus error. That interpretation was not established` | 作者自述旧解读「未被确立」 | **需重写** | 不是「未被确立」，是「已被证伪」。改成：bit[31:16] 是 REQ_COMPLETE（`panthor_mmu.c:646-650`），bit16 是 AS0 的 UPDATE 完成；AS2..7 各置 16+as 即算术证据。低地址分配器保留，理由换成「uncached/WCOMB，免缓存维护」 |
| 6 | 811-812 | `a leaf without them is 0b00, which the walker reads as an invalid descriptor: it produces no translation and, on this walker, no fault either` | AArch64 描述符规则 + 「无效描述符不报 fault」 | **仍成立**（附带一处未验证） | 与本位无关，保留。「0b00 不报 fault」这个硬件行为本身没有实测支撑，可标未验证；但它是另一条链的支点，不影响本条判定 |
| 7 | 943-944 | `the upper IRQ bit after UPDATE does not prove a fault caused by this flag or a completed table walk` | 该位两义 | **需重写** | 前半句对（不是 fault），后半句错（bit[31:16] 正是完成信号）。改为：`UPDATE 之后出现的 16+as 位是该 AS 的 REQ_COMPLETE；它证明命令退休，不证明取指发生；fault 只看 bit[15:0]` |
| 8 | 1122-1127 | `every leaf the MCU walked read back as an invalid descriptor, which is a translation failure that raises no MMU interrupt` | M1 期 leaf 低两位缺失 | **仍成立** | 保留。解释的是 M1 静默，不是本次误读 |
| 9 | 1372-1375 | `the two page table entries that are already known good from the walk` | 走查已验证这两个 PTE | **需重写** | 限定为 CPU 侧：`已由 load_firmware() 的覆盖自检确认每个 leaf 写成 0b11`。不能说「硬件走查确认」——没有任何 GPU 侧回读 |
| 10 | 1148-1151 | `if the fetch … is refused by the MMU there is no fault on AS0 because the fetch never becomes a transaction: status stays 1, debug_top goes busy and the version word keeps the sentinel. That is this board's exact shape.` | 「XN 拦截的取指不产生 AS fault」 | **未验证** + 前提已被自家日志否掉 | 与本位无关，但同样缺证据：panthor 的 `AS_FAULTSTATUS_ACCESS_TYPE_EX`（`panthor_mmu_regs.h:43`）说明执行访问被拒**是要记 fault 的**。且本段前提已失效——`section 1: va 0x1000000-0x1010000 flags 0xd (r-x cached)` 本身可执行（日志 197 行）。建议删掉该假说 |
| 11 | 2295 | `got 0x4 before the table walk, 0x4 after it, and 0x1b once the core had been told to run … may be exactly that` | debug_top 的位义未知 | **仍成立** | 保留。这是全文件最诚实的怀疑句（明说「两个词不构成证据」） |
| 12 | 2726-2732 | `panthor_mmu.c:556-568 as_send_cmd_and_wait() brackets the write with wait_ready() on both sides: AS_STATUS bit0 is set while the command is outstanding` | panthor 的等待形状 | **仍成立** | 保留。与 `panthor_mmu.c:521-553` 逐字一致。**这是全文件唯一的「命令是否退休」独立判据**，见 §3 |
| 13 | 2881-2885 | `Doing both before the very first UPDATE means … on some parts that is enough to make the UPDATE never complete` | panthor 只做三次写 + 一次 UPDATE | **仍成立** | 保留。论据是调用形状（`panthor_mmu.c:596-606`），不是中断位。「某些部件」仍属一般化，可弱化为「没有依据认为需要」 |
| 14 | 2894-2899 | `with 0x420001c6 the walk geometry is wrong in a way that would keep the MCU from fetching anything` | kbase 写 0x42000006，panthor 写 0x420001c6 | **仍成立**（后半句未验证） | 保留。`panthor_mmu.c:782-788` 确写 `INA_BITS(55-va_bits)`；本机 `va_bits=48`（日志 172）→ `0x420001c6`。「会错到取不到指」是推断，标未验证 |
| 15 | 2906-2917 | `Which write raises the bus error … zero after the clear, 0x10000 -- MMU_BUS_ERROR(0) -- once the address space had taken the update` | 0x10000 = BUS_ERROR | **需重写** | **保留 AS_STEP 插桩**（它恰好是本次推翻的证据来源），删掉 bus-error 框架。改为：`fault 只看 bit[15:0]；bit[31:16] 是各 AS 的 REQ_COMPLETE。逐步读的目的：确认三个配置写不产生任何事件、UPDATE 之后才出现 16+as 位` |
| 16 | 2941-2945 | `the walker would be handed a TRANSTAB of zero … a bus error with no page fault, which is what every run has reported` | 每次运行都报 bus error | **彻底作废** | 回读本身保留且有效（日志 244：三项都非零、与写入值一致 → 三次写没有静默丢失），但「每次运行都报 bus error」不存在，落脚点消失 |
| 17 | 2956-2961 | `the second one raced the first one's completion bit in the interrupt latch` | 该位是 completion bit | **仍成立** | 保留。**全文件用词最准的一处**。⚠️ 与 `:2909` 的 BUS_ERROR 读法在同一函数里并存 |
| 18 | 3453 | `Park the seven address spaces the MCU does not use` | 七个 | **需重写** | 改「六个」：循环是 `as = 2..7`（`:3490`），AS1 留给固件；`:3504` 的打印自己写的就是 `2 to 7`。与本位无关的独立笔误 |
| 19 | 3459-3460 | `a space left at whatever the block powered up with still answers walks, and a walk into one of them is a fault raised into a line nobody is watching` | 未编程 AS 会响应走查 | **未验证** | 与本位无关，保留但标未验证。ADRMODE_UNMAPPED 下的行为是推断 |
| 20 | 3475-3476 | `All six spaces remain UNMAPPED, so a trace difference alone does not establish that their attributes are required` | 拒绝从相关性推因果 | **仍成立** | 保留。全文件最克制的一句 |
| 21 | 3478-3488 | `Every argument about MMU_BUS_ERROR(0) so far rested on the claim that spaces two through seven take the same write without raising it … If those spaces raise the error too then it belongs to the block` | 邻居是否静默从未测量 | **彻底作废**（实验保留） | 实验（`:3490-3503`）已跑过并给出结论：每个空间都抬自己的位（日志 176-181）。claim 被自家数据否证。注释改成记录结论 |
| 22 | 3760-3764 | `It has been carrying MMU_BUS_ERROR(0) since AS0 took its update … the same stale bit` | 0x10000 = BUS_ERROR | **需重写** | 动作（启动前清一次再读）保留且必要；叙述改为：`自 AS0 UPDATE 起锁存器携带 bit16 = AS0 的 REQ_COMPLETE。启动前清零是为了让之后读到的任何 bit[15:0] 只能来自取指` |
| 23 | 3963-3965 | `A fetch … refused by the address space raises a page fault or a bus error on AS0` | 拒绝取指会抬 bus error | **需重写** | 改为：`被拒绝的取指在 bit[15:0] 对应 AS 的位置 fault，并在 AS_FAULTSTATUS/AS_FAULTADDRESS 留下类型与地址；不会在 bit[31:16] 留下任何东西——那里是完成位` |
| 24 | 4054-4060 | `an MCU that walked into a translation fault … and the MMU interrupt latch says the walk failed` | 锁存器说走查失败 | **彻底作废** | 删「the walk failed」。保留 AS_FAULTSTATUS/AS_FAULTADDRESS 两个寄存器的读取价值——它们独立于中断位 |
| 25 | 4087-4092 | `unanimous and uninformative: no fault, no fault address, no walk error` | 三个「无」 | **仍成立** | 保留。与实测一致（fault 0 / addr 0 / bits[15:0]=0），且这里没有把 bit16 当 fault |
| 26 | 4661-4664 | `a walk into one of them raises an interrupt` | 同 #19 | **未验证** | 同 #19 |
| 27 | 4698-4706 | `The parked spaces AS2..AS7 … raise no interrupt at all; AS0 … raises MMU_BUS_ERROR(0) every single run. Two facts, one variable, never separated.` | 邻居静默 + AS0 报错 | **彻底作废** | 整段删。两个「事实」里第二个是错的，所以「两个事实一个变量」的对照不存在。`mali_preas0` 可留，理由换成「与厂商 trace 记录顺序一致」，不能声称对照了中断行为 |
| 28 | 4779-4795 | `AS0 takes the update, the bus protections are released … a hand-written thirty-two byte Cortex-M7 stub at the same address does not run either` | 「AS0 takes the update」= 已消除的解释 | **需重写** | 改为 `AS0 的 UPDATE 退休（AS_STATUS.bit0 归零 + REQ_COMPLETE 置位）`——仍成立，且现在是唯一成立的。总线忙那条（debug_top `0x4`→`0x1b`）保留为独立观察，注明位义未验证 |
| 29 | 4799-4805 | `Clear the MMU latch immediately before the table walk … whatever is latched after the clear was raised by as_enable()` | 清零以区分本次与遗留 | **需重写** | 动作保留（本次推翻的证据来源）；措辞改为 `清零后出现的 bit[31:16] 只可能由 as_enable() 产生；bit[15:0] 才代表 fault` |
| 30 | 4808-4814 | `If this is silent and the real path is not, the difference between the two is the table and only the table` | 对照组能区分表 | **彻底作废** | 删。两条路径都不静默（都抬 16+as 位），无法区分任何东西。且 `mali_as0=1` **从未真正跑过**——全仓库 `docs/` 下 109 份含 `AS0 step update` 的运行日志里没有一行 `AS0 parked:` |
| 31 | 4843-4847 | `mmu irq 0x10000 -- MMU_BUS_ERROR for address space 0, a walk the bus refused` | 0x10000 = BUS_ERROR | **彻底作废** | 删。紧接其下的 `:4849` 打印已经把同一个值解码成 `request completions 0x1`——同一处代码两种读法 |
| 32 | 4849-4850 | `printf(… (page faults %#x, request completions %#x) …, mrd & 0xff, mrd >> 16 & 0xff)` | bit[15:0]=fault, bit[31:16]=完成 | **仍成立** | 保留，并**提升为主判据**。全文件唯一与 panthor 语义一致的解码。⚠️ 与上方 #30 注释互相矛盾，矛盾的一方（注释）要删 |
| 33 | 5175-5179 | `the walker reported MMU_BUS_ERROR(0) on AS_COMMAND=UPDATE … with the root table at 8.5 GiB. A walk that cannot read its own root produces exactly that signature` | 高位地址不可达假说 | **需重写** | 删 bus-error 因果。`mali_low` 保留，理由用 `:660-668` 已有的那条（低窗 uncached/WCOMB）。「高于 4 GiB 不可达」随本条作废——见下面的反证 |

**计数**：作废 8（#3 #4 #16 #21 #24 #27 #30 #31）、需重写 10（#5 #7 #9 #15 #18 #22 #23 #28 #29 #33）、
仍成立 12（#1 #2 #6 #8 #11 #12 #13 #14 #17 #20 #25 #32）、未验证 3（#10 #19 #26）。
（#10 即 XN 那条：判「未验证」而非「作废」，因为它的前提本身也被日志否掉了，见该行说明。）

### 2.1 「结论偶对、理由错」清单（最危险的一类）

链内 3 处（JSON 里标 `accidentally_right: true`）。这三处的**结论**单独拿出来看是对的，
但支撑它的理由已经被推翻。只读结论会以为推理链还活着。

| 行 | 结论（对的） | 理由（错的） | 危险在哪 |
|---|---|---|---|
| 2956-2961 | 第二次 UPDATE 会和第一次的完成位竞争 | 同一函数 `:2909` 把这位叫 BUS_ERROR | 同一段代码用两个互斥的名字指同一个位。后来者按注释理解，会把「完成位竞争」读成「两次 bus error」 |
| 3478-3488 | 邻居空间的行为值得单独测 | 「若邻居也抬，就属于块而不是地址空间」——把抬位当成 error | 实验确实做了，数据也确实回答了问题，但答案是「每个 AS 各抬一位」，与这段预设的两种结果都不同。数据在，读法错 |
| 4849-4850 | `bit[15:0]`=fault、`bit[31:16]`=完成 | 紧邻的 `:4843-4847` 说同一个值是 BUS_ERROR | 判据和结论写在同一处、互相矛盾。谁先被读到决定后面所有推理的方向 |

链外 2 处，同一类病，但不在那 33 条之内（因此不计入 §2 的判定数）：

| 行 | 结论/行为（对的） | 理由/描述（错的） | 危险在哪 |
|---|---|---|---|
| 3161-3164 | 启动前把 `JOB_INT_MASK` 写全 1，与厂商 trace 一致 | 注释说「job mask 写 0 是解除屏蔽」，与实际极性相反 | `INT_MASK` 是**使能集**（`panthor_device.h:605-617`：`pirq->mask |= mask` 后写回；`:567-575` suspend 写 0）。写 0 = 全关，写全 1 = 全开。照注释改代码会真的把中断关掉。详见 §4.1 |
| 4021-4031 | 写 `0x1008 = 0` / `0x2008 = 0`，值与厂商 trace 一致 | 注释说「zero = 解除屏蔽」，`:4031` 打印 `interrupts unmasked after the boot` | 使能集语义下写 0 是**全关**：这一行之后 job 与 MMU 都不再产生可见中断，而 panthor 在运行时保留非零使能集（`panthor_mmu.c:3287` 使能全部 fault 位后 `panthor_mmu_irq_resume()` 把它们写回，`panthor_device.h:577-584`）。因为全程轮询，功能上无影响；但任何人以后要接 GIC 中断，会照这句注释以为中断已开 |

另有 1 条同源但不同性质：`:2941-2945` 的**回读动作**是对的且有效（日志 244 证明三次写都落地），
但挂在它下面的因果解释（「每次运行都报 bus error」）作废。这一条的差别是**动作对、解释错**，
上面五条是**结论或行为对、理由错**。

---

## 3. 作废后，什么结论还站得住

### 3.1 先厘清一件事：走查完成 ≠ 命令退休

过去把两者当同一件事，这是整条链的根。分开看：

| 问题 | 能回答它的东西 | 现状 |
|---|---|---|
| UPDATE 命令**退休**了吗？ | `AS_STATUS.bit0`（AS_ACTIVE）归零 | **有证据**，见 3.2 |
| 走查**读了表**、读对了吗？ | 无直接寄存器 | **没有独立证据** |
| 走查**失败**了吗？ | `MMU_INT_RAWSTAT[15:0]` 对应位 + `AS_FAULTSTATUS` + `AS_FAULTADDRESS` | **有证据说「没有失败」**，见 3.3 |
| MCU **取指**发生了吗？ | `JOB_INT_STATUS.bit31` / version 词 / witness 标记 | **有证据说「没有」**（但 `RAWSTAT` 从未被读，见 §4.2） |

### 3.2 命令退休：有独立证据

`as_ready()`（`:2705-2716`）轮询 `AS_STATUS & 1`，最多 10 万次；`as_command()`（`:2718-2739`）在写前后各等一次。
判据与 `panthor_mmu.c:521-539`（`wait_ready()`，`!(val & AS_STATUS_AS_ACTIVE)`，10 µs × 100000）同构。

实测：

- 全仓库 `AS0 enabled: root …` 共 **40 次**，`transcfg` 全部是 `0x42000006`。
- `AS0 did not take the update` / `AS0 never finished command` —— **在 `docs/` 下所有 `trial-*-mali.txt`（38 份）里 0 次**（只在源码里作为字符串出现）。
- 日志 246 行 `status 0`（`AS_STATUS` 读 0），574 行 `as0 status 0`。

**结论：AS0 的 UPDATE 命令确实退休了。** 这条不依赖 `0x10000`，且现在成了「AS0 配置被接受」的主要依据。

### 3.3 走查失败：有证据说「没有」

- `AS_FAULTSTATUS` 在 UPDATE 后读 **0**（日志 246 `fault 0`），启动后仍 **0**（日志 574）。
- `AS_FAULTADDRESS` 读 **0**（246 / 574）。
- `MMU_INT_RAWSTAT & 0xff` 在 UPDATE 后 **0**（日志 250 明确打印 `page faults 0`）；启动循环四次采样全 **0**（264 / 265 / 321 / 408）。

按 `panthor_mmu.c:1802-1808` 的解码，`fault_status & 0xFF` 是异常类型、`>>8 & 3` 是访问类型、`>>16` 是 source id——
全零意味着**没有记录任何 fault**。所以「走查被拒绝」不仅没有证据，还有反证。

### 3.4 页表内容回读核对：只有 CPU 侧，没有 GPU 侧

`grep -n -i "verify|readback|memcmp|checksum"` 结果里与页表相关的只有三处，全部是 CPU 侧：

| 位置 | 做什么 | 是不是 GPU 侧回读 |
|---|---|---|
| `:1213-1260` | `walk %#llx: L0 … L3 … page`：从 `gMcuRoot` 经 `pt_kva()` 走一遍，打印描述符字 | **不是**。`pt_kva()`（`:893-904`）在 `gPt[]` 里按物理地址查 CPU 虚拟地址，读的是 CPU 自己的那份 |
| `:1264-1305` | `coverage: every section fully mapped`：逐 leaf granule 比对 need/have | **不是**，同上 |
| `:2938-2948` | `AS0 readback: transtab/memattr/transcfg` | **不是页表**，但**是** GPU 侧寄存器回读，且有效 |

日志 226-233 的实测：

```
walk 0: L0 0x1a0658403 L1 0x1a06dc403 L2 0x1a06a0403 L3 0x400001a07144c3 page
coverage: every section fully mapped
reset vector: sp 0x2040000  pc 0x10018ed
```

`AS0 readback`（日志 244）：`transtab 0x1a0550000 memattr 0x84c8d888d8f88 transcfg 0x42000006`——
三项都与写入值相同，`0x84c8d888d8f88` 与 `MALI_MEMATTR`（`:877`）逐字节一致。**这证明三次配置写落地了**，
但它证明的是寄存器，不是走查。

**没有任何一次 GPU 侧页表回读。** 硬件没有提供「把走查结果读回来」的接口，驱动也没有尝试过间接路径
（例如让 MCU 通过自己的页表写一个标记——这正是 witness 想做的事，但 witness 从未成功跑起来）。

### 3.5 「没有 fault」是零值证据，不是正证据

必须区分：

- 「`MMU_INT_RAWSTAT[15:0]` = 0」证明**没有记录 fault**。
- 它**不证明**走查成功。一个从未被触发的走查、一个被 ADRMODE 挡住的走查、一个走查了但结果没被任何寄存器保存的走查，
  都会给出同样的零。

所以 §3.3 的正确表述是：**没有证据表明走查失败**；而不是「有证据表明走查成功」。

### 3.6 重新回答：「AS0 walk 是否成功」

> **走查是否成功，当前没有独立证据。**
>
> 能确定的只有三件互不相同的事：
> 1. AS0 的三次配置写**落地了**（`AS0 readback` 三项与写入值一致，日志 244）。
> 2. AS0 的 UPDATE 命令**退休了**（`AS_STATUS.bit0` 归零，40 次 `AS0 enabled`，0 次超时）。
> 3. 走查**没有报 fault**（`AS_FAULTSTATUS`/`AS_FAULTADDRESS`/`RAWSTAT[15:0]` 全 0）。
>
> 「走查读了表并且读对了」这件事，**没有任何一次 GPU 侧观测**。它既没有被证实，也没有被证伪。
> 过去那句「AS0 被总线拒绝」是把 (2) 的完成位读成了错误；现在把 (2) 读对，也只是拿回了 (2)，
> 不能顺势把「走查成功」补上。

这个答案对后续排查是有用的，因为它把「表有问题」这条从嫌疑名单里**降级**（没有反证支持它），
同时不假装已经清白了它。

---

## 4. 顺带发现的其它问题（不在 0x10000 链上，但同源）

### 4.1 `JOB_INT_MASK` 注释与语义相反（已确认，行为对）

`:3172-3174`：

```
 * The masks do not stay on. The trace has twelve writes across a boot, alternating all-ones
 * before a start with zero immediately after the firmware comes up -- zero meaning unmasked,
 * 0xffffffff meaning masked.
```

panthor 的 `INT_MASK` 是**使能集**：`pirq->mask |= mask` 后写回（`panthor_device.h:605-617`），
`suspend` 时写 0（`:567-575`），`resume` 时写 `pirq->mask`（`:577-584`）。写 0 = 全关，写全 1 = 全开。

所以 `gpu_irq_mask_prestart()`（`:3176-3185`）里 `kJobIntMask = 0xffffffff` 实际是**全开**，
`:4026` 的 `kJobIntMask = 0` 实际是**全关**——与注释说的正好相反，但**行为与 panthor 一致**
（panthor 也是在 boot 后把 mask 恢复成使能集）。

**这是第二例「结论偶对、理由错」**：注释描述的极性和代码行为相反，而代码行为是对的。
`0x100c` 始终读 0 因此**不是自己屏蔽出来的**，是硬件真的没置 bit31。

同一函数 `:3184` 写 `kMmuIntMask = 0x0000ffff`——只使能 fault 位，**不使能 REQ_COMPLETE**。
这解释了为什么启动循环里读 `mmu irq` 一直是 0 而不是 `0x10000`：不是位消失了，是它不进 masked 视图。

### 4.2 `kJobIntRaw` 只定义、零使用（已确认）

`:358` 定义 `kJobIntRaw = 0x1000`，全文件（5253 行快照）**只此一处**。`grep -n kJobIntRaw` 单行命中。
所以「RAWSTAT 是否置过 bit31」从未被观测——这条任务书给的结论复核成立。

后果：§3.5 里「MCU 没取指」的证据链少了一环。现有的是 `JOB_INT_STATUS`（masked 视图）、version 词、witness 标记，
缺的是 raw 视图。加一行 `printf` 即可补上，成本为零。

### 4.3 `mali_as0=1` 对照组从未执行

`:4808-4827` 的对照分支存在，但全仓库 `docs/` 下 109 份含 `AS0 step update` 的运行日志里没有一行 `AS0 parked:`。
`configs/` 与 `devices/` 下也没有任何 `mali_as0=` 的 boot-arg。

所以 `:472-481` 和 `:4808-4814` 描述的对照实验**一次都没跑过**，却有两段注释在讨论它的预期结果。
这是「基于未执行实验的推理」——与本次作废同类的病。

### 4.4 高位地址假说有两侧反证

`:5175-5179` 认为「根表在 8.5 GiB 导致走查读不到根」。实测按根表地址分类统计 `AS0 step update` 的值：

| 根表位置 | 运行数 | UPDATE 后的 `mmu irq` |
|---|---|---|
| < 4 GiB（`0x50000000`） | 23 | 全部 `0x10000` |
| ≥ 4 GiB（`0x101dec000` … `0x1f2444000`） | 21 | 全部 `0x10000` |

两边一模一样。地址高低与该位无关，假说被两侧反证。

（注意：这只否掉「该位由高位地址引起」，**没有**否掉「高位地址对 MCU 取指有影响」——后者仍未被测过，
因为 `mali_low=0` 的运行里 MCU 同样一条指令都没取。）

---

## 5. 驱动注释应该怎么改（具体文字，不改文件）

只给替换文字。行号为 5253 行快照。原则：**删掉 bus-error 框架，保留全部插桩**——那些插桩正是这次推翻的证据来源。

### 5.1 `:469-481`（`mali_as0` 的说明）—— 整段替换（`:468` 的 `/*` 与 `:482` 的 `*/` 保留）

```c
/*
 * mali_as0=1 parks address space 0 the way AS2..AS7 are parked, instead of giving it the
 * firmware's table. Nothing else changes: same clear, same three writes, same UPDATE.
 *
 * The question this answers is narrow: whether the REQ_COMPLETE bit that appears after AS0's
 * UPDATE depends on what TRANSTAB points at. It does not -- AS2..AS7 each raise their own
 * bit (16+as) with a zero table and ADRMODE_UNMAPPED, so a parked AS0 raising bit16 would say
 * only that the command retired, not that the table was read.
 *
 * What this comparison cannot do is separate "the table is wrong" from "the table is right".
 * No register reports the result of a walk, and that is the gap this driver has never closed.
 *
 * Note: this branch has not been run. No log on record contains "AS0 parked:".
 */
```

### 5.2 `:655-658`（低地址分配器的由来）—— 替换前三句

```c
 * The original reason for this window was an interpretation of IRQ bit16 after AS UPDATE as a
 * root-fetch bus error. That reading is wrong: bits 16 to 31 are REQ_COMPLETE
 * (panthor_mmu.c:646-650), and bit16 is AS0's UPDATE completing. AS2..AS7 latch bits 16+as for
 * the same reason -- their own UPDATE completing -- which is what settled it. One window with a
 * root below 4 GiB and one above produced the same bit16, so the address plays no part in it.
 *
 * Keep this allocator because the low window is uncached (WCOMB), which removes the cache
 * maintenance question, and because a predictable root address makes logs comparable.
```

### 5.3 `:943-944`（PTE 表描述符）

```c
			/* kbase entry_set_pte() includes ENTRY_ACCESS_BIT on table descriptors.
			 * Match that encoding. The IRQ bit that appears after UPDATE is that address
			 * space's REQ_COMPLETE (bits 16..31, panthor_mmu.c:646-650): it says the
			 * command retired, not that this flag caused a fault and not that a walk
			 * produced the translations we expect. */
```

### 5.4 `:2906-2917`（AS_STEP 的说明）

```c
	/*
	 * Ask which write produces an interrupt, one write at a time.
	 *
	 * The MMU latch carries two different things. Bits 15..0 are faults, one bit per address
	 * space; bits 16..31 are REQ_COMPLETE, one bit per address space, set when that space's
	 * command retires (panthor_mmu.c:646-650, and AS2..AS7 each latch 16+as on this board).
	 *
	 * So each step clears the latch before its own write and reads it after. What that
	 * separates: a configuration write that raises anything at all, versus an UPDATE that
	 * raises only its own completion bit. The observed shape is 0, 0, 0, 0, then bit16 --
	 * the three writes are silent, the UPDATE retires.
	 *
	 * It does not say whether the walk read the tables. Nothing does.
	 */
```

### 5.5 `:2938-2945`（readback 的说明）—— 只改最后两句

```c
	 * Read back what the three writes actually left in the registers.
	 *
	 * Every step so far has been judged by the interrupt latch, which reports command
	 * completion, not whether a value landed. If a write had silently gone nowhere, the
	 * walker would be handed a TRANSTAB of zero and would read its root from physical
	 * address zero. Two lines settle it: on this board all three read back what was
	 * written, so the writes land and the root address is the one this code chose.
```

### 5.6 `:3758-3765`（启动前清锁存器）

```c
	 * Clear the MMU latch before the core is told to run.
	 *
	 * It has been carrying bit16 since AS0's UPDATE retired -- AS0's REQ_COMPLETE, which is
	 * what that bit means. Leaving it there makes every later reading a mixture of "this
	 * happened after the start" and "this happened during setup". Clearing costs one write
	 * and makes the next fault read attributable: any bit in 15..0 after this point can only
	 * have come from a fetch.
```

### 5.7 `:3963-3965`（启动循环采样）

```c
			 * A fetch that leaves the core and is refused by the address space raises a
			 * fault in bits 15..0 for that space and records the type and address in
			 * AS_FAULTSTATUS / AS_FAULTADDRESS. A fetch that never leaves the core raises
			 * nothing. Bits 16..31 are not part of this question: they are REQ_COMPLETE.
```

### 5.8 `:4054-4060`（启动后读寄存器）

```c
	 * MCU_STATUS only reports the state machine we poked with MCU_CONTROL; it is one if the
	 * request was taken, not proof that a single instruction ran. The registers that can tell
	 * the difference are these: an MCU that walked into a translation fault leaves the address
	 * in AS_FAULTADDRESS and the access type in AS_FAULTSTATUS. All three of fault status,
	 * fault address and the fault bits of the latch read zero here, which rules a fault out
	 * and leaves "never fetched" standing. Without them "did not start" and "started and
	 * faulted on its first fetch" would look identical.
```

### 5.9 `:4698-4706`（`mali_preas0` 的理由）—— 整段替换

```c
	/*
	 * mali_preas0=1 puts AS0's update where the phone's trace has it: before the L2 request.
	 *
	 * Record seven of that trace is AS0's AS_COMMAND=UPDATE and record ninety-eight is
	 * L2_PWRON. This driver has always done it the other way round. The order is worth
	 * matching because it is the only one known to boot on this part, not because it changes
	 * anything observable in the interrupt latch: AS2..AS7 are written before the L2 and AS0
	 * after it, and all eight latch their own REQ_COMPLETE bit either way.
	 */
```

### 5.10 `:4808-4814`（对照分支）

```c
		/*
		 * The control run: AS0 gets exactly what AS2..AS7 got, and nothing else changes.
		 *
		 * Written as its own branch rather than as a flag inside as_enable() so the three
		 * register writes and the one UPDATE are textually the same shape as the parking
		 * loop. Both shapes latch a REQ_COMPLETE bit, so the bit itself separates nothing;
		 * what differs is whether a table was walked, and no register reports that.
		 */
```

### 5.11 `:4841-4848`（启动前读锁存器）—— 整段替换

```c
	/*
	 * Read the MMU latch before the start request, not only after it.
	 *
	 * The value to expect here is bit16 alone: AS0's REQ_COMPLETE, set when its UPDATE
	 * retired. Bits 15..0 zero means no fault was recorded during setup -- which is the
	 * useful half, because a fault there would have to be explained before anything the MCU
	 * does could be attributed to the MCU.
	 *
	 * The line below decodes the two halves separately for exactly this reason.
	 */
```

### 5.12 `:5173-5180`（`mali_low` 的理由）—— 整段替换

```c
	/*
	 * mali_low=0 puts the MCU's memory back above four gigabytes.
	 *
	 * The reason to keep the low window is the attribute, not the address: those pages are
	 * mapped uncached (WCOMB), so a CPU store is in memory when it retires and there is no
	 * cache maintenance to get wrong. A root above 4 GiB and a root below it produce the same
	 * bit16 after AS0's UPDATE, so the address is not what the interrupt latch is reporting.
	 *
	 * Whether the GPU can reach above 4 GiB at all is still untested: every run so far has
	 * used the low window, so no run has put a root up there and then asked the MCU to fetch.
	 */
```

### 5.13 `:3453`（一行）

`Park the seven address spaces` → `Park the six address spaces AS2..AS7`。

### 5.14 建议新增（不是改注释，是补观测）

`:4849` 旁边加一行 raw 视图，把 §4.2 的缺口补上：

```c
	printf("mali: job int raw %#x (global if %u), mmu int raw %#x\n",
	    mrd(kJobIntRaw), (mrd(kJobIntRaw) & kJobGlobalIf) ? 1u : 0u, mrd(kMmuIntRaw));
```

理由：`kJobIntRaw`（`:358`）已定义、零使用。`MMU_INT_MASK`（`0x2008`）在 `:3184` 被写成 `0x0000ffff`，
只放 fault 位过去，REQ_COMPLETE 不进 masked 视图——文件里也没有定义 `kMmuIntStatus`（`0x200c`），
所以要么读 `kMmuIntRaw`（`:359`），要么把 `0x200c` 补进枚举。

---

## 6. 工具与复现

```sh
cd /home/lizi/IOS/repo/hakios-mt6895/docs/headset-wip
./mmu-audit.py --selftest                    # 16 条断言
./mmu-audit.py                               # 审计当前驱动
./mmu-audit.py --src <任意快照>               # 审计指定版本
./mmu-audit.py --list-hits                   # 逐条列出并分类
./mmu-audit.py --json-out                    # 机器可读
```

脚本做五件事：

1. 用 8 组模式扫源码，统计每个模式命中数（当前 121 行 = 注释 87 + 代码 34，四类判定之和与之相等）。
2. 把**注释**行当 claim 逐条核对 `findings/mmu-refuted.json`（33 条覆盖 68 行、19 行按文本豁免、0 行漏审）；代码行只计数，其中 10 处显式列入 `CODE_CLAIMS`。
3. 片段校验：JSON 里的 `snippet` 必须仍在该行的注释段落里。仅行号漂移的自动重定位并单独报告，
   文本消失的报 `SNIPPETS GONE` 并退出 1。
4. 豁免表按**文本片段**匹配而非行号，所以并发编辑不会把豁免挪到另一条 claim 上；失效的豁免会被报为 `UNUSED EXEMPTIONS`。
5. 代码行里 10 处承载争议语义的（`AS_STATUS` 轮询、单次 UPDATE、逐 AS 锁存读、对照分支、总线忙采样、两半区解码等）在 `CODE_CLAIMS` 里逐条命名理由。

审计期间该文件被另一会话改动两次（5253 → 5255 行），脚本按设计重定位了 31 条并给出零未审项。

---

## 7. 证据边界（明确列出没做的事）

| 事项 | 状态 |
|---|---|
| 读 `0x13c40000` | **没有读**，按要求跳过 |
| 修改 `/home/lizi/IOS/hakios-mtk` | **没有修改**，全程只读；`git status` 仅用于确认文件被他人改动 |
| `kbase` 源码 | **未取得**。`/home/lizi/IOS` 下找不到 `mali_kbase_mmu_hw_direct.c` / `mali_kbase_mmu_mode_aarch64.c`，所以驱动里引用的 kbase 行号（`:2895`、`:829`）**未复核** |
| `mali_as0=1` 对照实验 | **未执行**（不在本次任务范围，且需要真机刷机） |
| 「无效描述符不报 fault」 | **未验证**，无实测也无上游依据 |
| 「XN 拦截不产生 AS fault」 | **未验证**，且有反证倾向（`AS_FAULTSTATUS_ACCESS_TYPE_EX` 存在） |
| 高位地址对 MCU 取指的影响 | **未验证**。本次只否掉了它对 bit16 的影响 |
| `pandora`/`panfrost` 对照 | 未做，不在范围 |
| 运行期固件 sha256 校验 | 驱动里没有；本次未新增 |

---

## 8. 对后续排查的影响

1. **「表走查被拒」这条从嫌疑名单删除。** 它没有证据，还有反证（无 fault、命令退休、三次配置写回读一致）。
2. **「走查成功」也不要写进结论。** 它同样没有证据。正确的表述是「没有证据表明走查失败」。
3. **`AS_STATUS.bit0` 升级为主要判据。** 它是唯一能证明「命令退休」的寄存器，且现在有了 40 次成功样本。
4. **`mali_low` 的理由要换。** 保留分配器，但理由从「避免高位地址」改成「uncached/WCOMB」。
5. **GPUEB 仍是头号嫌疑，且不受本次推翻影响。** 它建立在「一次 IPI 都没发」上，与 MMU 中断位无关。
   本次审计没有给 GPUEB 增加或减少任何证据。
6. **补一行 raw 读**（§5.14）。`JOB_INT_RAWSTAT` 零使用是一个纯粹的观测缺口，成本一行。
7. **不要再引用 `:472-481` 和 `:4808-4814` 的对照逻辑。** 那个实验从未执行，且即使执行也区分不了表的好坏。
