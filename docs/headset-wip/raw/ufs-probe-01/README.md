# UFS 只读探针候选 01（未接入生产树）

> 2026-11-16 21:10，DSH。**仅主机。未构建内核、未刷写、未改生产代码、未碰设备。**

**🔴 这是候选，等 Codex 复核。归它的判断我不越界。**

---

## 0. 它要回答的问题

**主线 Linux 的 UFS 节点既没有 `phys` 也没有 `resets`，它的驱动容忍两者缺失 ——
说明 PHY 和控制器状态是 LK 配好的。**

**但 K20 的 shim 是「ABL 刚跳转后第一时间接管」，那时一切还活着。
XNU 是「跑了一会儿之后接管」——中间有没有人关过 UFS 的 clock，我不知道。**

**这个探针就是去问那一个事实：**

| 问题 | 用哪个位回答 |
|---|---|
| **控制器还在不在** | `HCE` bit0 |
| **设备还在不在** | `HCS` bit0（`DP`） |
| **请求链表就绪没有** | `HCS` bit1（`UTRLRDY`） |

**🔴 它不能证明**：链路速率、gear、lane 数、读盘会成功、文件系统能挂载。

---

## 1. 身份

| 文件 | 大小 | SHA256 |
|---|---|---|
| **`hakios.mt6895_ufs.c`** | **6065 B / 179 行** | **`35c9c297a0df1f49736bbe158ea10b8377789676faa11c86602c2c4d8e2f6929`** |
| `test-ufs.c` | 8147 B / 257 行 | `95e34a2942744da81674153b91026a3a2af7d69dd7391df6689288d9bac15ea5` |
| `ufs-arm64.o`（ARM64 对象） | 2832 B | `076b52ca4be3171f9f7aaacdeceaee5ae9170d796bb28d99b5f363d506a1b38e` |

---

## 2. 设计

### 2.1 只读的九个个寄存器（JEDEC UFSHCI / JESD223）

| 偏移 | 名称 | 含义 |
|---|---|---|
| `0x00` | `CAP` | 能力；`[4:0]` 是 `NUTRS-1` |
| `0x08` | `VER` | UFSHCI 版本 |
| `0x20` | `IS` | 中断状态 |
| `0x30` | `HCS` | 控制器状态（`DP`/`UTRLRDY`/`UCRDY`） |
| `0x34` | `HCE` | 控制器使能（bit0） |
| `0x50` | `UTRLBA` | 请求链表基址低 32 位 |
| `0x54` | `UTRLBAU` | 高 32 位 |
| `0x58` | `UTRLDBR` | 门铃 |
| `0x60` | `UTRLRSR` | 链表启停 |

**这九个偏移与 K20 `shim/ufs.c` 用的完全一致，也与 `ufs-mediatek.c` 一致。**

### 2.2 🔴 零写（类型层面保证）

**读回调类型：**

```c
struct ufs_reader { bool (*read)(void *, uint16_t, uint32_t *); void *context; };
```

**类型里没有写操作的位置 —— 不是「碰巧没写」。**

**全文件 `volatile` 访问只有 1 处（第 131 行）：**

```c
*value = *(const volatile uint32_t *)(base + offset);
```

**`write`/`memcpy`/`memset` 命中 3 次，全部是注释（第 2、11、70 行）。**

**不写 `UTRLRSR`、不写 `UTRLBA`、不敲门铃、不发 SCSI。**

### 2.3 白名单二次检查

**`ufs_mmio_read` 除了遍历 `ufs_offset[]`，还检查 `offset + 4 <= UFS_SIZE`。**

**`UFS_SIZE = 0x2300`（DT 的 `reg` 大小），最大合法偏移 `0x60 + 4 = 0x64`，远在界内。**

### 2.4 双快照

**每次 9 个读，做两遍 = **18 次读**。两遍必须完全一致，否则 `UFS_UNSTABLE`。**

### 2.5 六种结果

| 值 | 名称 | 含义 |
|---|---|---|
| 0 | `UFS_OBSERVED` | 两快照一致且控制器看起来活着 |
| 1 | `UFS_READ_FAILED` | 白名单内的读失败 |
| 2 | `UFS_UNSTABLE` | 两快照不同 |
| 3 | `UFS_NOT_ENABLED` | `HCE` bit0 = 0 |
| 4 | `UFS_NO_DEVICE` | `HCS` `DP` = 0 |
| 5 | `UFS_NOT_READY` | `HCS` `UTRLRDY` = 0 |

**🔴 `UCRDY` 不是门槛** —— vendor 驱动也不要求它，只在输出里报告。

### 2.6 参数零映射

**`mtk_ufs_probe` 不存在 / =0 / =2 / =UINT32_MAX，四条路径都在 `ml_io_map` 之前返回。**

### 2.7 结果非 OBSERVED 时打印全部原始值

**即使拒绝判定，九个原始字也全部打印 —— 不妨碍看原始数据。**

---

## 3. 复跑结果

```
clang -std=c11 -Wall -Wextra -Werror -O1 -g -fno-omit-frame-pointer \
      -fsanitize=address,undefined test-ufs.c -o test-ufs-asan
```

| 项 | 结果 |
|---|---|
| build rc | **0**（`-Werror` 零警告） |
| run rc | **0** |
| stderr | **0 字节** |
| **输出** | **`PASS 2313 cases; 9 registers, 2 snapshots, read-only, no controller writes`** |

### 3.1 覆盖了哪些

| # | 覆盖 |
|---|---|
| 1 | 健康控制器 → `UFS_OBSERVED`，`nutrs=32` |
| 2 | **每次快照恰好 9 个读，且偏移顺序与白名单一致** |
| 3 | **18 个读位置各自注入失败** → `UFS_READ_FAILED`（18 例） |
| 4 | **9 个字各自在两次快照间不同** → `UFS_UNSTABLE`（9 例） |
| 5 | `HCE` bit0 清零 → `UFS_NOT_ENABLED` |
| 6 | `HCS` `DP` 清零 → `UFS_NO_DEVICE` |
| 7 | `HCS` `UTRLRDY` 清零 → `UFS_NOT_READY` |
| 8 | **`UCRDY` 单独清零不改变结果** |
| 9 | **`NUTRS` 全 32 种取值**（0..31 → 1..32） |
| 10 | **`ufs_mmio_read` 对 `0x2300` 内所有非白名单偏移全部拒绝**（约 2224 例） |
| 11 | 参数 absent/0/2/UINT32_MAX 四条路径零映射 |
| 12 | map 失败时不读任何寄存器 |
| 13 | mode=1 且 map 成功时打印 3 行（无 raw dump） |
| 14 | 结果非 OBSERVED 时打印 3 + 9 行 |

---

## 4. 我明确没有做的事

| # | 没做 |
|---|---|
| 1 | 不写 `UTRLRSR`/`UTRLBA`/`UTRLDBR` |
| 2 | 不敲门铃 |
| 3 | 不发 SCSI 命令（连 TEST UNIT READY 都没有） |
| 4 | 不碰 clock / power-domain / PHY / reset |
| 5 | 不映射 `0x112b0000` 以外的任何地址 |
| 6 | 未接入生产树、未接入 `bsd/conf/files`、未构建、未刷写 |

**这个探针比 K20 的 shim 更保守** —— K20 至少接管了链表并发 SCSI READ，
**这个只读九个字就退出。**

---

## 5. 需要 Codex 判定的

| # | 问题 |
|---|---|
| 1 | **九个寄存器够不够** —— 要不要加 `UIC` 相关（如 `UICCMD`/`UICARG1`） |
| 2 | **若 `HCE` 已关，下一步查哪里** —— clock？power-domain？还是 LK 行为 |
| 3 | 路线 1（接管链表发 SCSI）vs 路线 2（先补 clock） |
| 4 | `ml_io_map(0x112b0000)` 在这个 XNU 上是否需要额外的映射属性（如 `ml_io_map_wcomb`） |
| 5 | 是否需要同时读 `ufscfg_ao`(`0x112b8000`) / `ufscfg_pdn`(`0x112bc000`) 两个 syscon |

**我倾向先只做这一版探针，拿到 `HCE`/`HCS` 三个位再决定。**

---

## 6. 边界

**本轮只做**：写驱动与测试、编译、跑测试、记录。

**没有做**：构建内核、打包、刷写、碰设备、改生产代码。

**「LK 保留了 UFS 状态」仍是推断**，这个探针是去验证它的第一步。
