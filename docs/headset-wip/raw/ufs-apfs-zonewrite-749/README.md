# 里程碑 6b · 真机写入未成（Trial749）— 三轮失败，根因已定位两处

2026-11-21。设备已恢复原厂，`boot_a` 逐字节相同。

## 结论先说

**真机写入没有做成。** 测试区一个字节都没写进去（已核对 512 块与备份逐字节相同，容器末尾也相同）。

三轮各暴露一个错误，前两个已修，第三个未验证：

## 错误一：`physio` 把用户指针当内核地址

第一版让 `write(2)` 走 `physio` → `xnusys_strategy`。内核对 `B_PHYS` 的 buffer 只做：

```c
if (!(bp->b_flags & B_CLUSTER)) { *io_addr = (caddr_t)bp->b_datap; return 0; }
```

`physio` 的 `b_datap` 是**用户地址**（`buf_setdataptr(bp, uio_curriovbase(uio))`）。
内核直接读它 → `Kernel data abort`。

读路径从来不走 `physio`，所以这个问题只在写的时候冒出来。

**修法**：写改走 ioctl，`copyin()` 把载荷拷进内核缓冲，再逐块交给驱动。

## 错误二：我自己的 poison 把整条存储链打死

第一版驱动在 `VRQ_NO_LEASE` 时看到 `restored == false` 就 `us_poison()`。
但 `NO_LEASE` 走的是 `return` 而不是 `goto clean`，`restored` 本来就是 false——
**命令根本没发出，我却把它当成"发出后没能恢复"**。

后果：租约 poisoned → guard 拒绝归还所有权 → 无 HLOG、无 Android IO、必须手动进 fastboot。

照片上那行 panic 就是我自己写的那条。

**修法**：拒绝路径只归还租约，不 poison。

## 错误三：ioctl 号的结构体长度算错

`struct xnusys_write_arg` 有 `uint64_t, uint64_t, const void *, uint32_t, uint32_t`，
对齐后是 **32 字节**，我按 24 算，`_IOW` 的长度字段不匹配，内核收到未知命令号。

已改 32，未验证。

## 顺带修掉的

`platform.data_bytes` 是设备级状态（`twoblock_word` 用它做 DATA 区边界），
我在写路径里改了它没恢复，导致后续读取越界。已改为保存/恢复。

## 现在这个方向的风险

三轮里两轮让设备卡死到必须手动进 fastboot。第三次虽然自动回落到安卓，
但 HLOG 被 guard 挡住只剩 622 字节，取证反而更难。

**在把 lease/poison 语义彻底走通之前，不该再往真机上刷写入版本。**

## 阶段 6 的账

| 项 | 状态 |
|---|---|
| 主机写入演练（写入/半径/撕裂/恢复） | ✅ 四项全过 |
| 真机专用测试空间 | ✅ 608448..653559，176 MiB，全零，已备份 |
| 真机原始备份 | ✅ 512 块 + 容器末尾，sha256 已存 |
| 独立读回路径 | ✅ 驱动读 + ELF 读两条 |
| 真机写入 | ❌ 未成，三轮后停手 |
| 跨重启持久化 | ❌ 未开始 |
