# USB 数据端点打通：EP2 配置 + PIO 回环（Trial801–802）

2026-11-24。方案来自 Sol 的 `STAGE2-PLAN.md` 第 1、2 节。

## 一、结果

**801 — EP2 生命周期**

```
fifo cap tx 0x2000 (8192) rx 0x2000 (8192); epinfo 0x808 eps tx 8 rx 8
ep2 enabled: tx2 0x1000200/0/0x90020  rx2 0x20200/0/0x90000  epier 0x40004  ep_rst 0
ep2 fields: maxpkt tx 512 rx 512, type tx 0 rx 0, segsize tx 9 rx 9,
            dmareq tx 0 rx 0, reset released 1
```

主机侧探针（PyUSB）切 `alt 1 → 0 → 1 → 0`，四次全部成功。

**802 — PIO 回环，八个长度全部通过**

```
echo 1 字节 OK     echo 512 字节 OK
echo 2 字节 OK     echo 513 字节 OK
echo 3 字节 OK     echo 1024 字节 OK
echo 511 字节 OK   echo 1514 字节 OK
```

内核侧收到的包长分布，与方案第 2 节的表逐项吻合：

```
len 0:   2 次   ZLP（512 和 1024 各一个）
len 1:   2 次   1 字节帧，加 513 的尾巴
len 2:   1 次
len 3:   1 次
len 511: 1 次
len 512: 6 次
len 490: 1 次   1514 = 512 + 512 + 490
```

## 二、寄存器字段核对

| 项 | 期望 | 实际 | |
|---|---|---|---|
| maxpacket | 512 | 512 / 512 | ✅ |
| type | bulk (0) | 0 / 0 | ✅ |
| slot | 0 | 0 | ✅ |
| segsize | 9 (512 B) | 9 / 9 | ✅ |
| `DMAREQEN` | 0（PIO） | 0 / 0 | ✅ |
| `EP_RST` | 释放 | `ep_rst 0` | ✅ |
| `EPIER` | `0x00040004` | `0x40004` | ✅ |
| TX2CSR2 | `0x00090020` | `0x90020` | ✅ |
| RX2CSR2 | `0x00090000` | `0x90000` | ✅ |

`tx2csr0 0x1000200` 的 `0x1000000` 是 `FIFOEMPTY`（bit24），正常。

## 三、两处实现细节

### EP_RST 也是电平保持

和 EP0 一样，`EP_RST` 的 EP2 两位必须显式清。写法沿用 799 的脉冲：

```c
mac_set(U3D_EP_RST, EP2_RST_BOTH);
delay(1);
mac_clear(U3D_EP_RST, EP2_RST_BOTH);
```

方向分布和 `EPISR` 相反：IN 在 `bit 16+n`，OUT 在 `bit n`。这一点容易记反。

### ZLP 的时序（802 第一轮失败的原因）

第一版在最后一包发出去时就清了「欠一个 ZLP」的标志：

```c
} else if (ep2_tx_off >= ep2_tx_len) {
    if (!ep2_tx_zlp) { ep2_tx_active = false; }
    ep2_tx_zlp = false;        // ← 这里清早了
}
```

于是下次进来 `chunk == 0` 且标志已是 false，直接结束传输，**ZLP 从来没发出去**。
主机在等一个标记帧结束的零长度包，512 字节那一轮就卡住了。

改成：标志只在 ZLP 真正提交时才清。

```c
if (chunk != 0u) {
    ep2_fifo_write(ep2_tx_frame + ep2_tx_off, chunk);
} else if (ep2_tx_zlp) {
    ep2_tx_zlp = false;        /* 这次提交的就是 ZLP */
    ep2_zlp_count++;
} else {
    ep2_tx_active = false;
    return;
}
```

## 四、FIFO 地址布局

按方案第 1 节，TX RAM 给 EP1 留 `0x000`，TX2 用 `0x200`，RX 在独立 RAM 从 `0x000` 起：

```c
#define EP2_TX_FIFO_ADDR    0x200u
#define EP2_RX_FIFO_ADDR    0x000u
#define EP_FIFO_SEGSIZE     9u      /* 2^9 = 512 */
```

`CSR2` 的 `FIFOADDR` 字段是字节地址右移 4 位，所以 `0x200 >> 4 = 0x20`，正是回读的 `0x90020`。

## 五、主机侧探针

`/home/lizi/usb-test/ep2_probe.py`，PyUSB 1.3.1。按 `18d1:d00d` 加序列号 `HAKIOS0001` 选中设备。

关键点：**设备必须在 VMware 虚拟机里可见**（不是接到 Windows 上）。操作者确认过是接在 VM 上的。

`--echo` 会跑完整回环。整倍数帧由脚本显式补 `device.write(0x02, b"")` 作为 ZLP。

## 六、文件

| 文件 | 内容 |
|---|---|
| `usb-801-802.diff` | 相对 800 的改动，620 行 |
| `hakios.mt6895_usb.c` | 当前完整文件，99 KB |
| `ep2-802.log` | 157 条 usb 日志 |

镜像：`trial736/boot-802b.img`

## 七、下一步

803：换成真 ECM 描述符（interface 0 改 `02/06/00`，加 13 字节 Ethernet Networking 描述符，去掉 ACM 和 Call Management），加 `SET_ETHERNET_PACKET_FILTER` 处理，加 EP1 通知。

让 Linux 的 `cdc_ether` 绑定，`lsusb -t` 看到 480M 和驱动。
