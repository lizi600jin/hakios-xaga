# 更正 3（20:05）：executor 对一半 —— putchar 确实不读 fwlog，但 fwlog 有它自己的 47 处调用者

**手机零访问。** 这条只是为了把因果链钉死，因为它决定下一轮要不要做。

---

## 1. executor 说对的部分

`putchar`（`0x100f806`，`movs r0,#3` + `bkpt #0xab`）**只把字符放到栈上就直接 bkpt**，
不读 `fwlog` 的 `size`/`data_va`。这条我写错了，撤回。

而且调用统计更能说明问题：

| 函数 | 调用点数量 |
|---|---|
| `putchar`（semihosting `bkpt`） | **1**（只被 `puts` 调） |
| `puts`（`0x100f824`） | **3** |
| `fwlog writer`（`0x100971e`） | **47** |

所以 semihosting 在正常路径里几乎不用，**日志的主通道是 fwlog**。我把次要通道当成了主通道。

---

## 2. executor 说错的部分：fwlog 确实是我们要填的那个环

`fwlog` 的写入路径是 `0x100971e`，47 个调用点分布在整段代码（`0x1000076`、`0x100033a`、
`0x100093a`、`0x1000c20`、`0x100176e`、`0x100346c`、`0x1003a32`…）。

它在启动时（`0x1002f2a..0x1002f92`）做这件事：

```
0x1002f2a  movw r7, #0xb0        ; r7 = sec0 的 fwlog 控制块（VA 0xb0，sec0 内）
0x1002f32  movt r7, #0
0x1002f3a  ldr  r1, [r7, #0xc]   ; r1 = [0xbc] = data_va
0x1002f3c  ldr  r2, [r7]         ; r2 = [0xb0] = size
0x1002f3e  strd r1, r2, [r0]     ; 0x202bbd0+0x00 = data_va, +0x04 = size
0x1002f44  ldr  r3, [r7, #4]     ; insert
0x1002f46  ldr  r7, [r7, #8]     ; extract
0x1002f48  strd r3, r7, [r0,#0x14]  ; +0x14 = insert, +0x18 = extract
0x1002f4c  beq  #0x1002f88       ; ← 任一为 0 就跳去禁用
0x1002f4e  cbz  r2, #0x1002f88   ; size==0 → 禁用
0x1002f50  cbz  r3, #0x1002f88   ; insert==0 → 禁用
0x1002f52  cbz  r7, #0x1002f88   ; extract==0 → 禁用
...
0x1002f88  movw r0,#0xbbd0 ; movt r0,#0x202
0x1002f92  bl   #0x100ee70       ; 全部清零，28 字节
```

**四个字段在映像里全是 0**（`0xb0`=0, `0xb4`=0, `0xb8`=0, `0xbc`=0，我已直接读出），所以这段
每轮都走到 `0x1002f88`，把 fwlog 状态清零 —— 也就是**日志通道从未启用**。

再往下的写入点（`0x1009792`）：

```
0x1009792  ldr  r0, [r6]         ; r0 = data_va     ← 我们要填的那个
0x1009796  add  r1, r0           ; r1 = data_va + insert_offset
0x100979a  bl   #0x100ee3e       ; memcpy 进环
```

用的就是 `data_va`。**所以"给 fwlog 填地址"不是空动作，它直接把这条 47 调用点的通道接上。**

executor 的结论「环空＝没到 putchar 不成立」——这句我要分开答：

- 如果它指的是"环空不能证明没到 semihosting putchar"，**对**，这是两条不同的通道。
- 如果它指的是"填了 fwlog 地址也不会被写"，**不对**，上面这段就是反例。

---

## 3. 所以路线 A 修正后的样子

原来的 A 我写错了两处，现在是：

1. **填 sec0 的 4 个字**（`0xb0` size、`0xb4` insert、`0xb8` extract、`0xbc` data_va），
   让 `0x1002f4c` 那个 `beq` 不被走，fwlog 通道启用。
2. **不要指望 semihosting**。`bkpt #0xab` 只在 3 处 `puts` 里被用，且 Mali MCU 上无 host 时的行为
   仍然未知（executor 正在查）。这条路先不碰。
3. 读环在 host 侧做，用 kbase 的算法（`mali_kbase_csf_trace_buffer.c:479-513`：比较
   `insert`/`extract`，处理 wrap，读完更新 `extract`）。

判据也随之变干净：

| 观察 | 结论 |
|---|---|
| 环里有任何字节 | **固件执行到了某个 printf 调用点**，而且内容直接告诉我们停在哪 |
| `insert` 前进了但环内容看不懂 | 至少证明 **CPU 在跑指令并 store**，这是 17 轮里从未有过的正向证据 |
| `insert` 始终 0，`size`/`data_va` 保持我们写进去的值 | 固件连 `0x1002f4c` 之后的第一条 printf 都没到 |

第三种情况比现在所有阴性判据都强：因为 `0x1002f2a` 这段在**复位后很早**（前面只有 MPU/cache/
栈初始化），跑到它就意味着核心确实取指并执行了至少几十条指令。

---

## 4. 仍需 executor 回答的（我答不了）

1. `data_va` 填哪儿。我提过 `0x4008000`（共享段内，已映射、nc shared）。但 `fwlog` 的 `size`
   要按页对齐，且要和 `0x4000000-0x4094000` 这 8192 字节里已被 glb/csg 接口占用的部分错开。
   我需要在驱动里确认 `0x4008000+0x2000` 是否空闲 —— 这个我可以自己查。
2. `bkpt #0xab` 在 Mali MCU 上无 host 的行为。**这个我真的查不到**，只能靠它。
3. 有没有可能 fwlog 的 `insert`/`extract` 是**指针**而不是偏移？`0x100972c` 有
   `ldr r2,[r6,#0x18]; ldr r2,[r2]; str r2,[r6,#0xc]` —— 二次解引用像是指针。
   如果是指针，填法要改。这是我看代码时唯一没吃准的地方，请它重点核。

---

## 5. 合规

手机零访问；驱动零改动；仓库零提交；禁读地址零触碰。
本文件与 `mpu_unicorn.py` 是本轮唯一新增。
