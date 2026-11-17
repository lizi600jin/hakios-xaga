# 勘误：721-A v2 的 CDB byte1 位命名

**冻结文件不予覆写。** `candidate-v2/ufs_variable_read_codec.h`
(`6c7e531ec4b52f90a7a75a454fbb614263ed9b88b6ac74d4f783cdccb83d8098`) 保持字节不变。
本文件单独记录该错误，供后续引用。

## 错误

v2 的头注释与测试把 READ(10) CDB byte1 的 bit0 命名为 **PMI**：

    CDB[1]  upiu[17]  flags: RDPROTECT 0x80, DPO 0x10, FUA 0x08, FUA_NV 0x02, PMI 0x01

**PMI（Partial Medium Indicator）是 READ CAPACITY(10) CDB 的 byte8 bit0，不是 READ(10) 的，
也不是 RC10 的 byte1。** 把 RC10 的字段名搬到 READ(10) 上，是错的。

> 本勘误的初版把 PMI 写成「RC10 的 byte1 bit0」，**那也是错的**，此处更正为 **byte8**。
> 首要来源：sg3_utils `lib/sg_cmds_basic2.c` 的 `sg_ll_readcap_10`，其中 `rc_cdb[8] |= 1`
> 设置 PMI —— 见
> <https://raw.githubusercontent.com/hreinecke/sg3_utils/master/lib/sg_cmds_basic2.c>
>
> READ(10) 的 CDB 是 byte0..9：byte1 是 flags，byte8 是传输长度低字节，
> **两者都不是 PMI**。行为上 READ(10) byte1 全零的约束不受影响。

## 正确的表述

READ(10) CDB byte1 在本阶段**全部为零**，每一位都属于「保留 / 禁用」，
没有一个是本 codec 主动使用的：

| 位 | 掩码 | 本阶段状态 |
|---|---|---|
| 7..5 | `0xe0` | 保留，禁用 |
| 4 | `0x10` | DPO，禁用 |
| 3 | `0x08` | FUA，禁用 |
| 2..1 | `0x06` | 保留，禁用 |
| 0 | `0x01` | **保留，禁用** —— 不是 PMI |

**约束本身（byte1 必须为 0）是正确的，错的只是命名。**

## 影响范围

- **行为无影响**：`vr_make_read` 写 `upiu[17]=0x00`，`vr_command_lba` 要求
  `upiu[17]==0x00`。约束正确，只是注释与测试名用词错误。
- **测试仍然有效**：v2 场景 12 确实分别测了 `0x80` / `0x08` / `0x10` / `0x01` 四个值
  并全部拒绝，只是把 `0x01` 标成了 PMI。
- **后续若需更正**：在新的候选版本里改注释与测试名，**不改这份冻结文件**。

## 本阶段的有效约束（照此陈述）

> READ(10) CDB byte1 必须为 `0x00`：RDPROTECT、DPO、FUA、FUA_NV 及全部保留位一律禁用。
> 本 codec 不设置该字节的任何位。
