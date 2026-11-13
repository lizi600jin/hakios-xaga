# 危险记录：Android 的 `spmi_access` sysfs 接口会导致设备掉线

> 时间：2026-11-14 02:00–02:03
> 设备：xaga（GAJZ4HIN99AQ69Y9），stock Android，boot_a 未动
> 结果：**两次掉线，均自行恢复**，无数据损失

---

## 1. 发生了什么

想从 Android 侧读 MT6368 的 `0x26a9` 等寄存器（为发声路径补基线），
发现 `spmi-mtk-pmif` 驱动导出两个 sysfs 接口：

```
/sys/bus/platform/drivers/spmi-mtk/pmif_access    rw-r--r--
/sys/bus/platform/drivers/spmi-mtk/spmi_access    rw-r--r--
```

### 1.1 `pmif_access`：安全

写一个偏移，`cat` 得到该 PMIF 寄存器的值。

```sh
echo '0x880' > .../pmif_access
cat .../pmif_access
→ [pmif_access_show] [0x880]=0xe40011c4
```

试过的偏移：`0x3b8`、`0x880`、`0x884`、`0x894`、`0x8a4`、`0x8a8`
—— **全部安全，反复读无异常**。

失败格式会打印 `Illegal offset[0x...]`，不崩溃：
```sh
echo '0x1c804000 0x880' > .../pmif_access
→ pmif_access_store() Illegal offset[0x1c804000]!!
```

### 1.2 `spmi_access`：**会掉线**

这个接口本应按 SPMI 语义访问从设备。但**没有源码、没有文档**，
只能猜输入格式。实测：

| 输入 | 结果 |
|---|---|
| `5 0x26a9` | 安全，但 `show` 返回 `[0x0]=0x0`（未解析） |
| `5 0x26a9 1` | 安全，同样 `[0x0]=0x0` |
| **`0x5 0x26a9`** | **设备掉线**（第一次，preloader 模式） |
| **`0x5 0x26a9`**（重复） | **再次掉线**（复现） |
| `5,26a9` / `[5][0x26a9]` | 掉线后未能测 |

**结论：`spmi_access` 的 SID 必须写成十进制 `5`，写成 `0x5` 会导致设备崩溃。**

## 2. 掉线时的表现

- `adb devices` 空
- `lsusb` 出现 `0e8d:2000 MediaTek Inc. MT65xx Preloader`
- 约 20–30 秒后自动重启回 Android
- 重启后 `sys.boot_completed=1`，`ro.build.display.id=CP2A.260605.016`（原系统）
- **boot_a 未动**（本次全程没有刷机）

## 3. 为什么写错 SID 会崩

**未确定。** 两种可能：

1. 解析失败时变量未初始化，用了一个随机的 SID（可能命中不存在的从设备 → 总线 abort）
2. `0x` 前缀被解析成别的东西，导致越界访问

`spmi_access_store` 只打印 `size is N, buf is ...` 就返回，**不打印解析结果**，
所以无法从日志判断它把 `0x5` 理解成了什么。

## 4. 结论与禁令

**不要再用 `spmi_access` 猜格式。** 理由：

- 没有源码、没有文档、没有错误提示
- 格式错会**崩溃设备**，不是返回错误
- 即使侥幸找对格式，也不值得为一次读冒这个风险

**`pmif_access` 可以安全使用**（只读 PMIF 自身寄存器，不能访问 SPMI 从设备）。

## 5. 对耳机任务的影响

想从 Android 侧读 MT6368 寄存器这条路**基本关闭**。剩下的办法：

1. 用户在 Android 上手动跑 `tinymix`（只能改 kcontrol，不能读寄存器）
2. **裸机 XNU 用 `headset=1` 读** —— 这正是已写好的驱动，走 PMIF 通道，
   不经过 `spmi_access` 这个危险接口，且代码有 spin limit 保护
3. 等耳机到手，从 Android 的 `dump_reg` 拿插入态基线

**第 2 条才是正路**，而且它本来就不需要耳机。
