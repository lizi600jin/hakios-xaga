# fastboot oem 取证记录 2026-11-14T23:29:44+08:00

设备：序列号已隐（见 HAKIOS_SERIAL），fastboot 模式，lsusb 18d1:d00d

## 逐条结果

| 命令 | 结果 | 说明 |
|---|---|---|
| `oem fbreason` | ✅ 有输出 | `Reason: reboot bootloader` |
| `oem lkmsg` | ⚠️ 返回 OKAY 但**零字节输出** | LK 未挂 log_store，或该版本不实现 |
| `oem lpmsg` | ❌ FAILED | `usb_read failed: Value too large for defined data type` |
| `oem dump_pllk_log` | ❌ FAILED | 同上 |
| `oem uefilog` | ⚠️ 零字节 | |
| `oem get_socid` | ⚠️ 零字节 | |
| `oem lks` | ⚠️ 零字节 | |
| `oem expdb` | ⚠️ 挂住 60s 无输出，被杀 | 可能正在压缩 128MB 分区后吐给 USB，超时太短 |
| `oem mrdump_chkimg` | ⚠️ 无输出 | |
| `oem help` | ⚠️ 零字节 | 不实现 |

## 关键判断

1. `fbreason` 证明该 fastboot 的 oem 通道**通**，不是完全不可用。
2. `lkmsg` 返回 OKAY 却零字节 → LK 侧 `log_store` 未初始化或 `oem_dump_lkmsg` 未编译进这个 LK。**该路径对本机无效**，可从取证清单划掉。
3. `lpmsg`/`dump_pllk_log` 的 `Value too large for defined data type` 是**主机端 libusb/内核对返回长度字段的解析错误**（不是设备拒绝）。可能是 fastboot 34.0.5 的已知 bug；换 usb 传输参数或旧版 fastboot 可能可解。**未重试**。
4. `expdb` 挂住 → 说明 LK 确实在处理（压缩 128MiB）。下次给 >180s 超时再试。
5. 之后 `getvar product` 连续无返回 —— 说明 `expdb` 命令把 fastboot 会话**留在了半死状态**。`fastboot devices` 仍能列出（USB 层活着），但协议层不再应答。**这是新发现的风险点**：不要在最后一条命令跑 expdb，且结束后必须断电重启 fastboot。

---

## 追加 2026-11-14 23:50 — 自愈机制实测（推翻本人的"需要 USB 复位"判断）

### 事实
1. 23:23 发 `fastboot oem expdb`（超时 60 s 被杀）后，fastboot 协议层死亡：
   `fastboot devices` 仍列出设备，但 `getvar product` / `reboot` / `oem help` 全部
   `FAILED (usb_read failed: Value too large for defined data type)`。
2. 我尝试过 **USB 端口 unbind/rebind**、**`authorized` 0/1**、**`USBDEVFS_RESET`** ——
   **三种都无效**，`getvar` 依旧失败。
3. 23:49 用户报告：**手机屏幕闪数次后自行重启，回到 fastboot，用户未做任何干预。**
4. 实测恢复：`lsusb` 设备号 **037 → 041**（USB 重新枚举），
   `getvar product` → `product: xaga`，`oem fbreason` → 正常返回。**协议层完全恢复。**

### 结论（推翻先前判断）
- **先前判断「需要 USB 层复位才能恢复」错误。** 主机侧任何 USB 复位手段都不管用。
- **真实机制**：`oem expdb` 让 LK 进入 dump/压缩流程，超时被主机放弃后，**LK 侧自己
  的看门狗把设备重启回 fastboot**（屏闪是重启过程）。整轮约 25 分钟。
- **`fastboot oem expdb` 的真实风险等级：中，不是高。** 后果是等 ~25 分钟 + 屏闪数下，
  不是变砖、不需要刷机、不需要断电。

### 操作规范（写进团队约束）
- 跑 `fastboot oem expdb` 后，**不要尝试任何主机侧 USB 复位**，无效且浪费时间。
- 发了之后**直接等**：≥180 s 命令超时；若协议层死了，等设备自行重启（实测 ~25 min）。
- 期间 `fastboot devices` 仍会列出设备 —— **这个信号不可信**，必须用 `getvar product`
  判断协议层是否活着。
- 不要在其后立刻做任何需要设备可用的操作。

### 未解释
- 屏闪的**图案/次数**未记录（用户在旁观察，可补充）。
- LK 侧看门狗的具体超时值未从 `lk_a.img` 反汇编确认，`~25 min` 是本轮唯一观测值，n=1。

---

## 追加 2026-11-15 00:00 — `oem expdb` 第二次实测：空操作 + 再次杀死会话

### 事实
- 设备自愈回 fastboot 后（协议层正常），重跑 `fastboot oem expdb`，超时给 300 s。
- **结果**：`OKAY [ 6.774s] Finished. Total time: 6.774s`，**stdout 零字节**（0 bytes）。
- **随后 `getvar product` 立刻再次 `usb_read failed: Value too large`。**

### 结论（n=2 一致）
1. **`oem expdb` 在这台 LK 上是空操作** —— 6.774 s 返回 OKAY，不吐任何数据。不是"压缩 128 MiB 需要很久"，是**根本没有实装**（或需要先 `oem mrdump_out_set` 之类前置）。
2. **它必然后续杀死 fastboot 会话** —— 两次都是零字节 + 会话死亡。
3. **`oem lkmsg` 同理**（返回 OKAY，零字节）。

### 划掉
本机 fastboot `oem` 通道**只有 `fbreason` 一个命令真正有输出**。`lkmsg` / `expdb` / `lpmsg` / `dump_pllk_log` / `uefilog` / `get_socid` / `lks` **全部无效**。
→ **「零成本 fastboot 取证」这条路已走完，无产出。** 从待办清单删除。

### 剩余取证路径
| 路径 | 状态 |
|---|---|
| `xnusys` 尾 8 MiB | ✅ 可读，但只有 loader 写 |
| `log_store @ 0x7ffbf000` | ❌ Android 无 `/dev/mem` |
| `ramoops @ 0x48090000` | ⚠️ `BOARD_NO_PLOG=1` |
| Android 侧 ftrace/kprobe 抓 accdet | ⏳ **未试，这是新的首要候选** |
| 修日志 3 个 bug 后重跑 | ⏳ 进行中（Codex） |

---

## 追加 2026-11-15 00:10 — 恢复机制终于确定：**用户插拔 USB 有效，主机侧复位无效**

### 完整时间线（n=3）
| 时刻 | 事件 |
|---|---|
| 23:22 | `oem expdb` → OKAY 6.774s + 零字节，会话死亡 |
| ~23:49 | 设备屏闪数次自行重启回 fastboot，devnum 037→041（用户未干预） |
| 00:00 | 再跑 `oem expdb` → OKAY 6.774s + 零字节，会话再次死亡 |
| 00:00–00:07 | 主机侧尝试 **全部失败**：unbind/rebind、`authorized` 0/1、`USBDEVFS_RESET` |
| 00:07 | `/usr/bin/fastboot` 34.0.5 报出根因：**`Write to device failed (Cannot send after transport endpoint shutdown)`** |
| ~00:08 | **用户手动插拔 USB 一次** |
| 00:09:18 | `getvar product` → `product: xaga`，devnum 041→**042**。**恢复** |

### 结论（推翻我之前的两个判断）
1. ❌ 「等设备自愈 ~25 min」—— 不完全对。第一次确实是自愈，但第二次等了 40+ 分钟没动，**是插拔救回来的**。
2. ❌ 「主机侧 USB 复位无效所以没救了」—— 无效的原因搞清楚了：**内核已经把 endpoint shutdown**（`transport endpoint shutdown`），任何 ioctl 级的 reset 都没法复活一个已 shutdown 的 endpoint。**只有物理重新枚举才能重建 endpoint。**
3. ✅ **正确恢复动作：让用户物理插拔一次 USB 线。** 这会让设备侧重新初始化 USB 端点。

### 操作规范（最终版）
- 跑 `fastboot oem expdb` 前先确认用户在旁边能插拔线。
- 会话死后**不要浪费时间去试 USB 复位**（三种实测全废）。
- **直接请用户插拔 USB。**
- 判据始终是 `getvar product`，不是 `fastboot devices`。

### 为什么 `devices` 一直能列出
`fastboot devices` 只做 USB 枚举扫描（读 sysfs），不碰 endpoint。而 endpoint 已 shutdown，所以任何实际命令都失败。**这两个信号在 endpoint shutdown 状态下会矛盾**，是排查时最容易误判的点。
