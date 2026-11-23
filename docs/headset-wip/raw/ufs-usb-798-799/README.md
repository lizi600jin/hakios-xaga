# USB 从「完全没有日志」到「收到 SETUP 并完成 SET_ADDRESS」（Trial797→799b）

2026-11-23 夜。设备已回安卓，明天继续。

## 一、进度

```
轮次    usb 日志   intr    SETUP   链路状态              主机侧
797     88 条     0 条     0        speed 0 opstate 0    无反应
798     59 条     1 条     0        speed 3 opstate 0x12 Windows 认出 Xiaomi 设备
799     59 条     2 条     4        speed 3              仍反复枚举
799b    72 条     2 条     8        speed 3              SET_ADDRESS 成功
```

`799b` 走到的位置：

```
setup 1: GET_DESCRIPTOR(Device) 64B   → send 18/18
setup 2: SET_ADDRESS 6                → usb: address 6      ✅
setup 3: GET_DESCRIPTOR(Device) 18B   → send 18/18
setup 4: GET_DESCRIPTOR(Device) 64B   → 又从头开始
```

**SET_ADDRESS 成功了**，说明 EP0 的控制传输通了。卡在拿配置描述符。

## 二、五层，逐层打通

### 第一层：驱动根本没跑

```c
// hakios.mt6895_usb.c:1951
if (!PE_parse_boot_argn("usb", &which, sizeof(which)) || which != 2) {
    return;
}
```

启动参数里从来没有 `usb=`。加上 `usb=2` 之后驱动起来了，88 行日志。

### 第二层：PHY 被吊在挂起态

原来 `USB_PHY_DTM0_RESET = 1` 走 wide clear，把 `dtm0` 从 `0x2be00d4` 清成 `0x2000000`。
**注释自己写着这条路会弄死 PHY，代码却开着。**

Neon 反汇编 `phy-mtk-xsphy.ko` 找到真正的 9 步，其中有两个人手抄错的地方：

```c
phy_clear(XSP_U2PHYDTM1, (1u << 16) | (1u << 18));   // 错：应该在 DTM0
phy_wr(XSP_U2PHYDTM1, phy_rd(XSP_U2PHYDTM1) & 0xff41c30b);  // 错：掩码是 DTM0 的
```

**`bit18` 就是 `P2D0_FORCE_SUSPENDM`**——清错了寄存器，PHY 一直挂在挂起，D+ 永远不拉。

改成正确的 9 步后：

```
phy before: dtm0 0x2be00d4 dtm1 0
phy after:  dtm0 0x2000000  dtm1 0x226  acr4 0x1a  acr6 0x40100442
```

与 Neon 从模块里推出的预期值一字不差。**链路立刻起来：`speed 3`、`opstate 0x12`。**

### 第三层：`U3D_EP_RST` 是电平保持，不是自清零脉冲

```c
mac_wr(U3D_EP_RST, EP0_RST);
for (i = 0; i < 10000u; i++) {
    if ((mac_rd(U3D_EP_RST) & EP0_RST) == 0) break;   // 永远不成立
}
// 超时退出，EP0_RST 一直保持 1
```

`0x804` 这个寄存器必须软件写 0 才释放。**从第一版驱动开始，EP0 的 FIFO 和 SETUP 接收引擎就被按在硬件复位态里**：
`EP0CSR` 还能读出 `0x40`（maxpacket 64），但**永远不会锁存 `SETUPPKTRDY`**。

日志里 `qfcr` 到 `u2 ctrl` 之间那 **4.75 毫秒**的停顿，就是那 10000 次读超时。

改成 mtu3 的脉冲写法（`mtu3_core.c:mtu3_ep_reset()`，Neon 从反汇编 `0x6940..0x69a0` 找到）：

```c
mac_set(U3D_EP_RST, EP0_RST);
delay(1);
mac_clear(U3D_EP_RST, EP0_RST);
```

**改完立刻收到 SETUP。**

### 第四层：`EP0ISR` 一直挂着

`epi` 从 0.2 秒起就是 `0x1` 再不变化。`usb_take_over()` 清过一次，之后 `ep0_poll()` 只读 `EP0CSR`，从不碰 `EPISR`。

在 `ep0_poll()` 开头补上 W1C，并清 `SENTSTALL`。

### 第五层（未完成）：配置描述符

`send chunk 18/18 pre 0x100040 post 0x900040`：

```
pre  0x100040 = SETUPPKTRDY(17) + MAXPKTSZ 64
post 0x900040 = FIFOFULL(23)    + MAXPKTSZ 64
```

**`DPHTX`(bit20) 从头到尾没出现过。** 读 SETUP 时那次 `(now & 0xffacffff) | EP0_DPHTX | EP0_SETUPPKTRDY` 应该有它，
但下一次读就是 `0x100040` 了。

数据进了 FIFO（`FIFOFULL` 置位）却不发出去，因为控制器不知道这是 IN 方向。

## 三、明天从这里接

### 已知

- `usb_device_descriptor` 是 **18 字节**
- 主机第一次要 64、第二次要 18，我们两次都给 18
- `SET_ADDRESS` 成功，`DEV_ADDR` 设成了 6
- 主机每 5 秒重来一次，说明超时

### 三个方向

1. **`DPHTX` 为什么丢**：读 SETUP 那次写入之后，谁把它清了？
   可能是 `handle_setup()` 之前或之后的某次 `EP0CSR` 写。所有写入点都在 727/743/750/763/771/1001/1043/1045/1382/1576 行。

2. **`EP0_FIFO_RST`（bit16）**：已经加了定义和脉冲，但没验证。
   如果 `FIFOFULL` 是真满，需要确认它被清掉。

3. **`ep0_tx_state` 的完整语义**：Neon 说 mtu3 在 TX_END 中断里才写 `DATAEND`。
   我们是在 `ep0_send` 里同步等 `TXPKTRDY` 清零再写——如果 `TXPKTRDY` 根本没被清零（因为没发出去），
   就会一直等到 200000 次超时。

### 需要问 Neon 的

- `EP0_DPHTX` 在 mtu3 里是什么时候置的？是读 SETUP 时还是发数据时？
- `FIFOFULL` 置位但 `TXPKTRDY` 清零，这个组合正常吗？

## 四、操作者的两个关键观察

1. **Windows 设备管理器弹出「USB 设备"Xiaomi 22041216C"的连接失败。设备已连接到其他虚拟机。」**
   → **主机侧确认 D+ 拉起来了、枚举到了。** 这是第一次有主机侧证据。

2. **回安卓后那段一直闪烁、虚拟机变卡，插拔才恢复。**
   → 设备在反复重新枚举，VMware 的 USB Arbitrator 反复抢，形成死循环。

## 五、顺带完成

`loader_return_after` 从 420 改到 150 秒。操作者实测 47 秒进桌面，
所以 150 秒够用。**每轮刷机从 470 秒降到 200 秒，省了 4 分半。**

## 六、文件

| 文件 | 内容 |
|---|---|
| `usb-797-799.diff` | 相对 Trial797 之前的全部改动，556 行 |
| `hakios.mt6895_usb.c` | 当前完整文件，85 KB |

镜像：`logs/ufs-apfs-fstree-root-736/trial736/boot-799b.img`
