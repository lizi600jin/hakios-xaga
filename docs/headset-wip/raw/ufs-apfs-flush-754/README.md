# 里程碑 6e · 真实 SYNCHRONIZE CACHE(10)（Trial754）

2026-11-21。设备已恢复原厂，`boot_a` 逐字节相同。

## 结果

```
mt6895-ufs-twoblock: flush done, ocs=0 polls=231 restored=1
```

三种命令的轮询次数对比，直接反映设备在做什么：

| 命令 | polls | 含义 |
|---|---|---|
| 普通写 | 39 | 应答即可，数据可能还在设备缓存 |
| FUA 写 | 252 | 应答前先刷这一条 |
| flush | 231 | 应答前刷全部 |

三个块（用 Android 自己的驱动读，与写入时留档逐字节比对）：

```
608456 普通写        575a3457ed020000  ✅
608457 FUA 写        a9ad0ec32e7ca23c  ✅
608458 普通写+flush  f082391786299763  ✅
```

## 两个坑，都是命令格式

**一、`_IO('x', 5)` 到不了内核**

XNU 的 `_IO` 展开成 `IOC_VOID | group | num`，而 `IOC_VOID` 是 `0x20000000`。
用户态按 `0x00007805` 发，驱动什么也没收到——日志里一条 flush 都没有。

改成 `_IOW('x', 5, struct { uint32_t done, pad; })` 才通。

**二、PRDT 条目数写成 1**

`utrd[7]` 是 `prd_table_length[15:0] | prd_table_offset[31:16]`，
**length 是条目数，不是字节数**。

我第一版写 `0x00000001` —— 一个条目、offset 0。设备回 `ocs=0x1`
（`OCS_INVALID_COMMAND`）：让一个搬运零字节的命令去描述一个 scatter-gather 表。

正确是 `0x01000000` —— 零条目，offset 保持 `0x100` dwords。

这条 codex 事先提醒过（"不要沿用写命令的数据描述"），我还是按读写命令的习惯填了。
主机测试里我原本也把错值写进了断言，修正时它先炸——这次测试起了作用。

## 主机回归

```
-O2 -Wall -Wextra -Werror   38 PASS
ASan + UBSan                38 PASS   0 违规
```

新增断言覆盖：opcode `0x35`、`IMMED` 清零、期望传输长度 0、
LBA 0 / 块数 0、UTRD 方向位 `0x10000000`、PRDT 零条目，
以及篡改任一字节（方向位、PRDT、FUA 位）都必须被拒。

## 仍未做

- 卸载时强制 flush：现在卸载仍是 `MNT_FORCE`，不要求 flush 先成功
- 突然断电的持久性验证（codex 的判断是：不为这一项冒险，先补 flush 与卸载）
