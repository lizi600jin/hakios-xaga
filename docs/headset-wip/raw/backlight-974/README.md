# 背光调光通了，但 `RETURN` 之后设备卡死

2026-11-26，`boot-974`。前半是成功，后半是新的问题。

## 成功：六步全部读回正确

`/dev/hakios-backlight` 注册成功：

```
i2c: bl: /dev/hakios-backlight ready (orig=1649, safe range [200..1967]; cmds: status|raw|level|restore)
```

| 命令 | 读回 raw11 | 寄存器 |
|---|---|---|
| `status` | `1649`（orig=1649） | `0x04=0x01 0x05=0xce` |
| `raw 800` | **`800`** | `0x04=0x00 0x05=0x64` |
| `raw 400` | **`400`** | `0x04=0x00 0x05=0x32` |
| `raw 1900` | **`1900`** | `0x04=0x04 0x05=0xed` |
| `raw 0` | **`200`** ← 被钳位 | `0x04=0x00 0x05=0x19` |
| `restore` | **`1649`** | `0x04=0x01 0x05=0xce` |

**`raw 0` 被硬钳到 200，屏幕没黑。** 安全限幅是有效的。

## 硬件确认

`ktz8863a` 挂在 `i2c-6`（`0x11281000`）地址 `0x11`，8 位寄存器地址：

```
DEVICE_ID  (0x01) = 0x12      芯片 ID
BL_CFG1    (0x02) = 0x30
BL_CFG2    (0x03) = 0xcd      与 LK 写的一致
BL_BRT_LSB (0x04) = 0x01      亮度低 3 位
BL_BRT_MSB (0x05) = 0xce      亮度高 8 位
BL_EN      (0x08) = 0x17      与 LK 写的一致
```

亮度是 11 位：`(0x05 << 3) | (0x04 & 7)`，范围 `0..2047`，
安全区间 `[200..1967]`（下限防黑屏，上限是 LK gamma 表的天花板）。

写顺序：先 `0x04`（低 3 位），隔 200 µs 再 `0x05`（高 8 位），写 MSB 时芯片锁存。
单向写用 `CONTROL=0x28`（`ACKERR | CLK_EXT`，**不含** `DIR_CHANGE`/`RS`），
`TRANSFER_LEN=2`、`AUX=0`、`TRANSAC_LEN=1`，两个字节都用 8 位 `STRB` 写进 `DATA_PORT`。

## 失败：`RETURN` 之后设备死了

```
OK returning            ← hakid 应答了
35 / 60 / 90 秒都等不到 adb

ping 10.0.0.2         →  100% packet loss
hakid connect × 5     →  OSError
USB 仍挂 18d1:d00d，iProduct = "HakiOS phone"（卡在 XNU，没回 loader）
```

操作者手动长按电源恢复。

**`RETURN` 那一刻 hakid 是应答的**，所以不是命令没收到，是之后的清理路径卡住了。

`ping` 都不通，说明整个网络栈停了——这不像某一步慢，更像 `halt_all_cpus` 之后
没有人处理中断了。

## 关键对比

| 镜像 | 背光写路径 | `RETURN` |
|---|---|---|
| `boot-973` | 只读 6 个寄存器 | **32 秒正常** |
| `boot-974` | 只读 + 写 `0x04`/`0x05` | **卡死** |

差别只有写路径。

## 我这一轮的疏漏

刷 `974` 时 boot-args 里还留着 `bl=1`（做只读探针时加的，一直没删）。
`bl=1` 会让驱动跳过 `/dev/hakios-backlight` 注册，所以第一遍刷完节点没出现。

去掉 `bl=1` 重新打包才成功。

**规矩：每次刷机前把 boot-args 打出来核对。** 现在 `loader-build.json` 里只剩 `i2c=5`。

## 镜像

- `boot-974.img` —— 背光六步全过，`RETURN` 后卡死
- `boot-973.img` —— 背光只读，`RETURN` 32 秒，**当前基线**
- `boot-969.img` —— 电池周期读，无背光
