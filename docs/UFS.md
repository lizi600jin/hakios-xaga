# UFS：让 loader 从闪存里读出 2.32 GiB

`mtkloader` 是个裸机程序，跑在 LK 之后、XNU 之前，全程 MMU 关闭。它要做的事里最难的一件是从 UFS 里把 `system.apfs` 整个读进内存，因为整个项目没有任何可抄的现成实现。

这篇记下从「读不出一个字节」到「HS-G4 双通道、2.32 GiB 进内存」的过程。

## 为什么没有现成实现

上游 `hakios-xnu-kernel` 的 `mtk` 分支只支持 eMMC（`mtk/loader/msdc.h`），没有 UFS。

ios-sweet（Redmi Note 10 Pro / SM7150）有 UFS，但它的 loader 自己不读盘 —— 它把 UEFI 留下的 UTRLBA 交给内核，读盘是内核干的。所以那段代码在 MTK 上用不了。

`HKaim/…` 之类别的 MTK 移植要么是 eMMC，要么 UFS 读函数直接 `return 0`。

结论：UFS 主机驱动得自己写。内核侧 `drivers/storage/hakios.ufshc.c` 有一份 1351 行的参考，可以照它的 UTRD/UPIU 构造。

## 致命的三条

### 绝对不能清 `HCE`

`HCE`（Host Controller Enable，`0x34`）看起来像复位时该关一下再开的寄存器。在这台机上，写 0 之后整个寄存器块永久读 0。

症状很像总线问题：所有 MMIO 读回来都是 0。但 `IE` 还能正常写 —— 所以不是总线，是 SPM 把没时钟的域整个断电了，而且不会自己回来。

不是"清一下再开"，是"碰都不能碰"。

### `IS` 只能写单 bit

`IS`（Interrupt Status）是写 1 清零。写 `0xffffffff` 或者 `0x7ff` 这种宽掩码会把整个中断状态机打死，门铃从此再也不响。

只在响铃前清 `bit0`（`UTP_TRANSFER_REQ_COMPL`），一个比特。

### `UTRLCLR` 只能在列表停止时写

JESD223 的规定。顺序写反了，控制器直接判 `PWR_FATAL_ERROR`（`HCS 0x501`），三个 READY 位全灭，`UTRLRSR` 再也置不上。

正确的顺序是 `UTRLRSR = 0` → `UTRLCLR` → 写 `UTRLBA`/`UTRLBAU` → `UTRLRSR = 1`。

**但这一整套在这台机上做不成。** 详见下面「列表」一节。

## 一步一步来的顺序

### 1. 电源域与时钟走 SMC

`HCS` 恒 `0x0`，写 `HCE` 读不回 1，是所有问题的第一个。

根因：UFS 的电源域和参考时钟由 TFA 通过 SMC 授予，纯写寄存器无效。

```c
/* MTK_SIP_UFS_CONTROL */
#define UFS_SIP_FUNC  0xC2000276

/* 子命令位 */
#define UFS_SIP_REF_CLK_NOTIFICATION  (1u << 3)
#define UFS_SIP_MTCMOS_CTRL           (1u << 9)
```

`0xC2000276` 的推导：`ARM_SMCCC_CALL_VAL(FAST_CALL=1, SMC_64=1, OWNER_SIP=2, 0x276)` = `(1<<31)|(1<<30)|(2<<24)|0x276`。

顺序（照 `ufs-mediatek.c` 的 `ufs_mtk_vops_init`）：

1. `mtcmos_ctrl(false)` —— 先 restore，避免备份到意外的值
2. `mtcmos_ctrl(true)` —— 上电

然后才是 `ref_clk_notify(on, PRE_CHANGE)` → 写 `REFCLK_REQ` → 等 `REFCLK_ACK` → `ref_clk_notify(on, POST_CHANGE)`。

写寄存器之前必须先发 SMC 通知 TFA，否则 ACK 永远不来。

`smc #0` 在 freestanding 环境下直接内联汇编：

```c
static void ufs_smc(uint64_t fn, uint64_t a1, uint64_t a2, uint64_t a3,
    uint64_t *r0, uint64_t *r1)
{
	register uint64_t x0 __asm__("x0") = fn;
	register uint64_t x1 __asm__("x1") = a1;
	register uint64_t x2 __asm__("x2") = a2;
	register uint64_t x3 __asm__("x3") = a3;
	__asm__ volatile("smc #0" : "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3) :: "memory");
	*r0 = x0; *r1 = x1;
}
```

返回 `0` 是 `SIP_SVC_E_SUCCESS`，`-1` 是 `NOT_SUPPORTED`，`-4` 是 `PERMISSION_DENIED`。真机上拿到的是 `0`。

### 2. 链路要 `DME_RESET → DME_ENABLE → LINK_STARTUP`

`HCE` 使上之后，UIC 命令能发了，但传输全报 `OCS_PEER_COMM_FAILURE`（`0x5`）。

根因：设备的 RST_n 被脉冲过，对端 UniPro 栈已经归零。只发 `DME_LINK_STARTUP`（`0x16`）不够。

```
DME_RESET (0x14)  →  DME_ENABLE (0x15)  →  DME_LINK_STARTUP (0x16)
```

关键点：这一步要放在 `HCE` 使能**之前**。放在之后试过，没用。

### 3. UIC 的成败不看 `IS`

`IS` 的 `UIC_COMMAND_COMPL` 无论成功失败都会置位。真成败在 `REG_UIC_COMMAND_ARG_2` 的低字节：

```c
ok = completed && ((arg2 & 0xFF) == 0);
```

这一条坑了很久，因为早期代码只看 `IS`，于是每次 UIC 命令都"成功"，然后后面全乱。

### 4. 必须做 `fDeviceInit` 握手

设备被复位之后，所有 SCSI 命令返回 `response 0x1`。

缺的是 QUERY 的 `fDeviceInit`：`SET_FLAG(FDeviceInit)` 再轮询 `READ_FLAG` 直到清零。

QUERY UPIU 布局：

```
[0]  = 0x16          (transaction code)
[2]  = lun
[3]  = task tag
[5]  = 0x01 读 / 0x81 写
[12] = opcode
[13] = idn
[14] = index
[15] = selector
[20..23] = value (big endian)
```

`QUERY_FLAG_IDN_FDEVICEINIT = 0x01`。

另外 LUN 不能只扫 0。原来的 `ufs_pick_lun()` 只试 `l < 1`，改成 `l < 8` 之后才找到 LUN 2。

### 5. UPIU 的 status 在 byte 7，不是 byte 6

UPIU 头：

```
[0] transaction_code
[1] flags
[2] lun
[3] task_tag
[4] iid | command_set_type
[5] query_function
[6] response
[7] status
[8] ehs_length
[9] device_information
[10-11] data_segment_length
```

早期代码把 `r[6]` 当 SCSI status 读，于是所有命令都被判成失败。判成功的条件应该是：

```c
ok = (response == 0) && ((r[7] & 0x3f) == 0);
```

`CHECK CONDITION` 是刚复位设备的正常反应，不该判死。

## 列表：只能继承 LK 的

上游 README 和 ios-sweet 都要求「接管列表」：

```
UTRLRSR = 0  →  写 UTRLBA/UTRLBAU  →  UTRLRSR = 1  →  读回校验
```

在这台机上这套做不成。写 `UTRLRSR = 1` 之后读回还是 0，自己建的列表抓不住。

最后的选择是继承 LK 留下的列表：基址 `0x51266400`，`UTRLRSR` 已经是 1，列表健康。`UFS_USE_LK_LIST` 这个分支就是干这个的。

**注意 `board.h` 里必须把 `UFS_USE_LK_LIST` 写死为 1。** 有一轮改了 `XNU_SYSTEM_IMAGE` 却漏传这个宏，走了自建列表分支，列表起不来，`ufs_read()` 全返回 0，最后表现为 `/sbin/launchd.ios: No such file or directory` —— 看着像用户态问题，其实是读盘根本没读。

`UTMRL` 的偏移也容易记错：

```
UTMRLBA  0x70
UTMRLBAU 0x74
UTMRLDBR 0x78
UTMRLCLR 0x7c
UTMRLRSR 0x80
```

## 性能：从 43 分钟到 0 秒

第一版成功读出 2.32 GiB 花了 **2615 秒**（43.6 分钟），约 0.9 MB/s。那是 PWM-G1 的速度。

### 速率协商

`DME_SET(PA_PWRMODE)` 之后，判据是 `HCS` 的 bits 10:8 = `UPMCRS` 等于 `PWR_LOCAL`。

通了之后：`hs 7 ok 1 upm 1 rx 4/2 tx 4/2` —— **HS-G4 双通道**。2376 MB 从 2615 秒掉到 `secs 0`，约 2600 倍。

### 请求放大

`UFS_SLOT_BUFFER` 从 256 KiB → 1 MiB → **8 MiB**。

8 MiB = 32 条 PRD（每条 256 KiB），PRD 表 1024 字节 / 16 = 64 条，够用；也是 2048 个 4096 字节逻辑块，在 READ(10) 的 16 位块计数范围内。

配对要改的：

- `board_read_bytes()` 内部原来有个 2 MB 上限，会把 8 MiB 请求又切成 4 次，请求放大的收益全部吃掉。改成 `cap = UFS_SLOT_BUFFER`。
- 缓存 sync 的目标必须是真实的 DMA 目标：`ufs_prd_dst ? ufs_prd_dst : UFS_AREA_BUF`。零拷贝路径下 PRD 直指 `SYSTEM_PA`，数据不走 `UFS_AREA_BUF`；原来 invalidate 了没人写的那 8 MB，真正填好的 8 MB 没被动过。
- PRD 覆盖长度要按实际条数算：`UCD_PRDT + 16 * (UFS_SLOT_BUFFER / UFS_PRD_MAX + 1)`，即 33 条。硬编码 8 会让后 24 条 PRD 留在 CPU cache 里。

### 轮询预算

`UFS_MAX_POLLS` 原来固定 `1 << 22`（419 万次）。8 MiB 一次请求约需 **880 万次**轮询，预算只有一半，请求连一半都走不完就判超时。

改成 `1 << 24` 并按数据量缩放：

```c
uint64_t need = (uint64_t)s->bytes * 2u;
if (need > (uint64_t)polls) {
	polls = need > 0xffffffffull ? 0xffffffffu : (uint32_t)need;
}
```

## 零拷贝

MMU 关闭时，`ufs_put()` 每 4 字节一次 `memcpy`，每次都是一个 Device 总线事务。8 MiB 就是两百多万次。

`ufs_prd_dst` 让 PRD 直接指向 `SYSTEM_PA`，数据由 DMA 直接写进去，绕开 `ufs_put()`。要求 `off == 0 && (d & 63) == 0`。

## 写入：给 logbook 用

`ufs_write()` 走同一条 `ufs_cmd_once()`。**CDB 操作码必须按方向选**：

```c
uint8_t op = (dw0 & UTRD_DD_OUT) ? 0x2a : 0x28;   /* WRITE(10) : READ(10) */
```

写死 `0x28` 会让 `WRITE(10)` 报 status 失败。

## 单位陷阱

`board_read()` 的第一个参数是 **512 字节扇区号**，而 `base + done` 是字节偏移。混用会得到 `lba out of READ(10) range 0xa53e0000` 这种报错。

按字节读的路径统一走 `board_read_bytes(uint64_t off, uint64_t bytes, void *dst)`。

## 内存访问约束

`mtkloader` 全程 MMU 关闭，所有内存都当 device memory：

- 只能 32 位对齐访问
- 没有 SIMD
- 非对齐的宽访问会 fault，不是读出错值

所以 `dcache_clean_range` 之类的操作要自己接。

## 权威资料在哪

| 内容 | 位置 |
|---|---|
| 寄存器偏移与位域 | `/usr/src/linux-headers-7.0.0-38/include/ufs/ufshci.h` |
| HCE / refclk / mtcmos / SMC | `drivers/ufs/host/ufs-mediatek.c`、`ufs-mediatek-sip.h` |
| `ufshcd_hba_execute_hce`、`fDeviceInit` | `drivers/ufs/core/ufshcd.c` |
| UTRD / UCD / UPIU 布局 | 同 `ufshci.h`，以及内核侧 `drivers/storage/hakios.ufshc.c` |

反汇编容器里的厂商 `mtu3.ko` / `mtk_wdt.ko` 时，只能看 `.ko` 本身；loader 侧要看 `mtk/loader/mtkloader.o`（ELF），`mtkloader.bin` 是二进制裸镜像，反不出符号。
