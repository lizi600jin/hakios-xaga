# 更正 4（20:15）：executor 三处都对，我给出可执行的填法

**手机零访问。**

---

## 1. 它抓到我的三处，全部成立

| # | 我说的 | 实际 | 来源 |
|---|---|---|---|
| 1 | `MPU_CTRL=0x3` 是 `ENABLE\|PRIVDEFENA` | `ENABLE\|HFNMIENA`，**PRIVDEFENA 在 bit2，此值为 0** | `mpu_unicorn.py` 实读 |
| 2 | `putchar` 读 fwlog，填地址就能让日志落地 | putchar 只 `bkpt #0xab`，不读 fwlog | `0x100f806` 全文 9 条指令 |
| 3 | fwlog 环放 `0x4008000-0x400c000` | **该区间被接口表占用**（`0x4008000`、`0x400c000` 都是固件自带的 input/output VA） | 段内 146 页全被引用 |

第 3 条我复核了，比它说的还严格：

```
shared section VA 0x4000000-0x4094000 = 148 个 4K 页
映像里被引用的 in-section VA = 146 个不同页（span 0x4002000-0x4093000）
未被引用的页：只有 0x4000000、0x4001000 两页 —— 而这两页正是 glb 控制块
```

**结论：section 9 内部没有任何空闲页。** fwlog 环必须新开一块 VA。

---

## 2. 它的另一个怀疑也成立：insert/extract 是**指针**，不是偏移

`kbase_csf_trace_buffer.c:283-291` 的字段语义（`entry[1..6]` = `size_address` /
`insert_address` / `extract_address` / `data_address` / `trace_enable` / `count`），
配合固件 `0x100971e` 的解引用链：

```
0x100972c  ldr r2, [r6,#0x18]   ; r2 = extract *address*  (来自 sec0 0xb8)
0x1009730  ldr r2, [r2]         ; r2 = *extract   ← 二次解引用
0x1009732  str r2, [r6,#0xc]    ; 缓存
```

和写入点：

```
0x1009792  ldr r0, [r6]         ; r0 = data VA  (来自 sec0 0xbc，直接是 VA)
0x1009796  add r1, r0           ; data_va + insert
0x100979a  bl  #0x100ee3e       ; memcpy
```

→ **四个字的语义各不相同**：

| sec0 偏移 | 内容 | 谁写 |
|---|---|---|
| `0xb0` | 环**大小**（字节，必须是 2 的幂，固件用 `size-1` 做掩码） | host |
| `0xb4` | 一个 u32 的 **GPU VA**，该 u32 存 insert 偏移 | host 填地址，固件写值 |
| `0xb8` | 一个 u32 的 **GPU VA**，该 u32 存 extract 偏移 | host 填地址并初始化为 0 |
| `0xbc` | 环**数据缓冲区**的 GPU VA（直接是 VA，不是指针） | host |

四个全非零，`0x1002f4c` 那个 `beq` 才不跳；否则 `0x1002f92` 把整个状态清零、通道禁用。

---

## 3. 所以真正要做的事（这不是"填 4 个 0"，是"新开一块内存"）

需要新增：**1 个环（建议 16 KB = 4 页，与 kbase 的 `fwlog` `num_pages=4` 一致）
+ 2 个 u32（insert/extract）**，全部映射进 AS0，属性 `NON_CACHEABLE`（与 sec9 一致）。

放置：section 9 末尾之后，`0x40A0000` 起。该地址在 CSF MCU 共享区内
（panthor 的 `CSF_MCU_SHARED_REGION_START=0x04000000`，`SIZE=0x04000000`），
且不被任何现有段覆盖。

驱动侧改动量（估算，不含调试）：

1. `mem_alloc()` 一块 16 KB + 4 KB（u32 放最后页开头即可），已有函数。
2. 对它调 `pt_map(gMcuRoot, va, pa, leaf)`，leaf 用 `IDX_NC | PTE_PAGE | PTE_AF | PTE_RW`（照抄 sec9 的 leaf 构造，见驱动 `:1140-1185`）。
3. 把 4 个值写进 `sect_at(0)` 的 `0xb0..0xbc`。
4. 开机后按 `insert`/`extract` 读环，用 kbase 的 wrap 算法（`trace_buffer.c:479-513`）。

判据（不变，且现在因果链完整）：

| 观察 | 结论 |
|---|---|
| 环里有字节 | 固件跑到了某个 printf 调用点，内容直接说停在哪 |
| `insert` 前进但内容看不懂 | **CPU 确实在取指并 store** —— 17 轮里从未有过的正向证据 |
| `insert` 恒 0 | 连复位后最早的 printf 都没到（`0x1002f2a` 前面只有 MPU/cache/栈初始化） |

---

## 4. 我仍然答不了的

- `bkpt #0xab` 在 Mali MCU 上无 host 时的确切分支。它给的架构答案（Debug state / DebugMonitor /
  HardFault / lockup，取决于 debug 配置和异常优先级）我接受。**但既然主通道是 fwlog（47 个调用点），
  这条路可以完全绕开 bkpt** —— 那 3 处 `puts` 我们不触发即可。
- `0x1009734` 那段读 `0x40003090`/`0x40003094`（GPU cycle counter）是在做时间戳，与日志内容无关，
  不影响填法。

---

## 5. 合规

手机零访问；驱动零改动；仓库零提交；禁读地址零触碰。
新增：本文件。
