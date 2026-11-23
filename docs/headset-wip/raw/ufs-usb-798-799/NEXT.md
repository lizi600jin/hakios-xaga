# 明天从这里接

## 一句话现状

EP0 控制传输通了（`SET_ADDRESS` 成功，地址 6），
但配置描述符拿不到，主机每 5 秒重来一次。

## 最硬的一条证据

```
[19.798105] usb: send chunk 18/18 pre 0x100040 post 0x900040
```

- `pre  0x100040` = `SETUPPKTRDY`(17) + `MAXPKTSZ` 64
- `post 0x900040` = `FIFOFULL`(23) + `MAXPKTSZ` 64

**`DPHTX`(bit20) 在写之前就不在。** 而 `ep0_poll()` 读完 SETUP 之后明明写了
`(now & 0xffacffffu) | EP0_DPHTX | EP0_SETUPPKTRDY`（1043 行）。

所以：要么那次写没生效，要么之后被清掉了。

## 要查的

### 1. 所有 EP0CSR 的写点

```
 727   ep0_send 第一片
 743   ep0_send 的 ZLP
 750   ep0_send 的 DATAEND
 763   ep0_status
 771   ep0_stall
1001   ep0_poll 清 SENTSTALL
1043   ep0_poll 读 SETUP + IN 方向     ← DPHTX 应该在这里置
1045   ep0_poll 读 SETUP + OUT 方向
1382   ep0_configure 设 maxpacket
1576   usb_take_over 清 SENTSTALL
```

`1001` 那行 `(csr & 0xfdbcffffu) | EP0_SENTSTALL` —— 掩码里 bit20 保不保留？
如果它把 DPHTX 清了，而 `ep0_poll` 每次进来都执行这段，那就是它。

### 2. `FIFOFULL` + `TXPKTRDY=0` 这个组合

正常应该是 `TXPKTRDY=1` 表示"有数据待发"，硬件发完后清 0。
现在 `FIFOFULL=1` 而 `TXPKTRDY=0`，像是控制器认为没有待发的包，但 FIFO 里还有 18 字节。

### 3. `ep0_send` 的等待循环

```c
for (unsigned i = 0; i < 200000u; i++) {
    if ((mac_rd(U3D_EP0CSR) & EP0_TXPKTRDY) == 0) { taken = true; break; }
}
```

如果 `TXPKTRDY` 从来没被置起来（因为写入时 bit18 的位置不对），
这个循环**第一次就读到 0**，立刻 `taken = true` 通过，然后写 DATAEND。
等于根本没等，也没发。

**这个可能性最大。** 值得加一条：写完立刻读回，看 `TXPKTRDY` 到底置起来没有。

## 环境

- 一轮刷机 200 秒（`loader_return_after=150`）
- 内核增量编译 2 分钟
- 设备当前 `boot_a` = `boot-799b.img`
- 可回退：`boot-796.img`（卡在 USB 之前的稳定版，`/private/var` 64 GiB 正常）
- 备份齐全，BROM 深刷兜底

## 操作者会告诉你

- Windows 设备管理器有没有新设备（这是主机侧的唯一硬证据）
- 有没有闪烁/卡顿（说明枚举在循环）
- 进桌面的时间
