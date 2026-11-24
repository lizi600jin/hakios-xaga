# USB 枚举完全打通（Trial800）

2026-11-24。Windows 设备管理器显示 **`Google HakiOS phone`**。

## 一、结果

```
19.598  GET_DESCRIPTOR(Device) 64    → tx done 18/18
19.604  SET_ADDRESS 5                → usb: address 5
19.618  GET_DESCRIPTOR(Device) 18    → tx done 18/18
19.625  GET_DESCRIPTOR(Config) 255   → 64/76 + 12/76
19.634  GET_DESCRIPTOR(String 3)     → 22/22   "HAKIOS0001"
19.640  GET_DESCRIPTOR(String 0)     → 4/4
19.646  GET_DESCRIPTOR(String 2)     → 26/26   "HakiOS phone"
19.723  GET_DESCRIPTOR(String 1)     → 14/14   "HakiOS"
19.736  GET_DESCRIPTOR(String 2)     → 26/26
20.499  SET_CONFIGURATION 1          → usb: configuration 1        ✅
20.512  SET_INTERFACE(1, alt 0)      → interface 1 alternate 0
23.325  device conf 0x5000003 [speed 3 addr 5]; opstate 0x12       ✅ 稳定
```

**控制传输全部通过。** 主机读完了整套描述符，这就是 Windows 显示产品名的原因。

`addr 5` + `speed 3` + `epi 0 lv1 0`：设备稳定停在配置好的状态，不再反复复位。

## 二、根因：判断"发送完成"的方式错了

旧代码等 `TXPKTRDY` 清零：

```c
for (unsigned i = 0; i < 200000u; i++) {
    if ((mac_rd(U3D_EP0CSR) & EP0_TXPKTRDY) == 0) { taken = true; break; }
}
```

日志说明这个位**从来没被置起来过**（`tx_seen 0`）：

```
pre 0x100040   write 0x140040   post 0x900040   csr 0x100040   epi 0x1
      ↑ 没有bit18    ↑ 写了bit18     ↑ 读回没了       ↑ 最终态      ↑ 完成事件在这
```

写进去的 `0x140040` 含 `EP0_TXPKTRDY`(bit18)，硬件瞬间处理掉，读回来变成 `0x900040`。
**轮询这个位永远抓不到窗口。**

正确信号是 `EP0ISR`（`epi 0x1`），和 `mtu3_ep0_isr` 的 TX_END 分支一致：

```c
if ((epi & EP0ISR) != 0u && (csr & busy) == 0u) {
    mac_wr(U3D_EPISR, EP0ISR);
    if (last) {
        mac_wr(U3D_EP0CSR, (csr & EP0_W1C_BITS) | EP0_DATAEND);
    }
    complete = true;
}
```

`busy = last ? EP0_TXPKTRDY : (EP0_FIFOFULL | EP0_TXPKTRDY)`
—— 还有下一包时要等 FIFO 腾出空间，末包不用。

## 三、Sol 的改动（494 行）

| # | 改动 | 为什么 |
|---|---|---|
| 1 | 新增 `ep0_send_packet()` | 按包提交，等 `EP0ISR` 完成事件而非 `TXPKTRDY` |
| 2 | 新增 `ep0_tx_aborted()` | 新 SETUP / 总线复位 / 断开时中止旧回复 |
| 3 | 每包 100 ms 超时 | 替代 200000 次空转，且不占死 CPU |
| 4 | `DATAEND` 只在末包写一次 | 之前合并写 + 重复写，等于一个传输两个结束 |
| 5 | `ep0_write_fifo()` 按 32/16/8 位写 | 18 字节走 4+4+4+4+2，76 字节的 12 字节尾部和 9 字节头部都靠这个 |
| 6 | `usb_service_once(intr, *seenp)` 签名变更 | 复位与速度变化处理在启动和常驻两处共用 |

## 四、操作者提供的证据

Windows 设备管理器弹出 **`Google HakiOS phone`** 的提示气泡。

那是 `usb_strings[2]`，说明主机把字符串描述符读完了。**这是主机侧第一次认到我们的设备名。**

## 五、还差什么

### ① 数据端点从未配置

```c
case REQ_SET_INTERFACE:
    usb_alternate = (uint8_t)(setup->value & 0xff);
    printf("usb: interface %u alternate %u\n", setup->index & 0xff, usb_alternate);
    ep0_status();
    return;                    // ← 只记变量，没有配置 EP2
```

描述符里 interface 1 alt 0 **没有端点**，alt 1 才有 bulk 对：

```c
/* interface 1, alternate 1: where the bulk pair lives, 512 bytes each */
0x09, 0x04, 0x01, 0x01, 0x02, 0x0a, 0x00, 0x00, 0x00,
0x07, 0x05, 0x02, 0x02, 0x00, 0x02, 0x00,     /* EP2 OUT */
0x07, 0x05, 0x82, 0x02, 0x00, 0x02, 0x00,     /* EP2 IN  */
```

主机切到 alt 1 时才建立数据通道。`grep EP2CSR` 零结果 —— **整个数据端点层没写**。

### ② CDC class 请求不完整

```c
if (type == REQ_TYPE_CLASS && setup->length == 0) {
    ep0_status();
    return;
}
```

- `SET_CONTROL_LINE_STATE`（wLength=0）✅
- `SET_LINE_CODING`（wLength=7，主机带数据）❌ 掉进 `ep0_stall`

注释里写着「one with data still owes the device a read of the OUT packet」，代码没实现。

### ③ 没有网络数据通路

`live 0 sofs 0` 全程不变，EP2 的收发、`hakios_net_input()` / `hakios_net_attach()` 对接都是空的。

## 六、文件

| 文件 | 内容 |
|---|---|
| `usb-800-sol.diff` | Sol 相对 799b 的全部改动，693 行 |
| `hakios.mt6895_usb.c` | 当前完整文件，81 KB |
| `enumeration-800.log` | 109 条 usb 日志原文 |

镜像：`logs/ufs-apfs-fstree-root-736/trial736/boot-800.img`
