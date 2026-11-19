# 主线可用提速补丁

`mainline-speedups.patch` —— 三处改动，`patch -p1` 直接可用，
已在 `hakios-mtk` 的工作树副本上验证：**可干净应用、编译通过、生成 58776 字节 loader**。

```bash
cd /home/lizi/IOS/hakios-mtk
patch -p1 < /home/lizi/IOS/xnu-quick/mainline-speedups.patch
```

## 三处改动

### 1. `XNU_SYSTEM_REPORT_HOLD_MS`（原本硬编码 60000 ms）

`run_xnu()` 在进入内核前画一屏 `SYSTEM IMAGE` 报告（2.32 GiB 读成功的证据），
然后 `dbg_delay_ms(60000)` 等拍照。**每一次启动白等一分钟。**

改成宏，默认 `0`。同样的数字仍然进 logbook（`save_log_to_flash` 未动），
要拍照时 `-DXNU_SYSTEM_REPORT_HOLD_MS=60000u` 即可。

**实测省 60 秒**，是这批改动里最大的一笔。

### 2. 内核段只清 BSS 尾部

```c
// 之前：清 19.3 MB，然后用 14.7 MB 覆盖其中大部分
memset(dst_pa, 0, sg->vmsize);
memcpy(dst_pa, pi.kernel + sg->fileoff, sg->filesize);

// 之后：只清文件没覆盖的 4.6 MB
if (sg->vmsize > sg->filesize)
        memset(dst_pa + sg->filesize, 0, sg->vmsize - sg->filesize);
memcpy(dst_pa, pi.kernel + sg->fileoff, sg->filesize);
```

MMU 关闭时每个 store 都是 device memory 事务，省掉的 14.7 MB 直接是时间。

### 3. `find_syms()` 加长度预筛

51,418 个符号 × 31 个名字的全量 `strcmp`。长度先比一次，绝大多数直接跳过。
名字长度是字面量，首次调用算一遍存 static 数组。

**实测 650 ms → 约 300 ms**。

## 不在这个补丁里、但同批发现的东西

### 屏幕绘制是启动路径的头号开销

`mtkdbg.h` 的 `fb_px()` 每像素一次 device memory 写。一次全屏清屏 = **2,662,800 次写**。
启动路径上一串 `fb_rect(0,0,PANEL_W,PANEL_H,...)` 加行文字，实测吃掉近 20 秒。

我做的开关（`FB_SILENT`，运行时，菜单之后置位）没有放进补丁，因为它同时改了
`mtkdbg.h` 和 `boot_choice()`，且**踩过一个坑**：直接早退会让编译器把整段文字循环
优化删除，连菜单标题一起消失。要清掉必须保留光标状态更新并用 `volatile` 触一下。

**如果主线要做这件事**，完整实现见
`/home/lizi/IOS/xnu-quick/mtkdbg-FINAL.h` 与 `mtkloader-FINAL.c`。

### 多核与抬频：都更慢，别再试

- `cpus=4` → `cpus=7`：12 s → 17 s 到 logo。低主频下加核只是在加 rendezvous 等待。
- 抬 BL (A78) 1399 → 2000 MHz：12 s → 21 s。A78 上去了而 DSU 还在 1339 MHz，
  频率差拉大后跨核同步更慢。

两条都与 `board.h` 里已有的记录一致。

### 两个会让排查走弯路的坑

**a. 改 PLL 不能用覆盖式写**

```c
wr32(at, (1u<<31) | (post<<24) | pcw);   // 错：清掉 bit27/bit25，还置了没设过的 bit31
wr32(at, (before & ~((7u<<24)|0x3fffffu)) | (post<<24) | pcw);   // 对
```

这台机 LL 是 `0x0b133b13`、BL 是 `0x011aec4e`、B 是 `0x02133b13`、DSU 是 `0x0119c4ec`，
位分布各不相同。

**b. `pcw` 有 22 位上限，超了会被静默跳过**

`pcw = (mhz << post) * 16384 / 26` 必须 ≤ `0x3fffff`。所以：

| PLL | POSDIV | 最大可表示 |
|---|---|---|
| LL | 3 | 831 MHz |
| BL | 1 | 3327 MHz |
| B | 2 | 1663 MHz |
| DSU | 1 | 3327 MHz |

**LL 想上 2000 MHz 必须先改 POSDIV**，否则拒掉且不报错——一度让我以为"改过 LL 了"。

**c. `.S` 里的 `7:` / `8:` 是局部标签，重复定义会让新代码被静默跳过**

判断编译结果里是否真有某条指令，别靠眼看书，直接搜机器码：

```bash
# msr icc_ctlr_el1, x11
python3 -c "
import struct;d=open('loader.bin','rb').read()
print([hex(i) for i in range(0,len(d)-3,4) if struct.unpack_from('<I',d,i)[0]==0xd518cc8b])"
```

## 顺带记录：这台机的实测基线

| 项 | 值 |
|---|---|
| loader 自身（菜单确认→跳 XNU） | **3.2 s** |
| 其中 UFS 读 2.32 GiB | 1.6 s（约 1.4 GB/s，HS-G4 已协商上） |
| 内核段复制 20 MB | 716 ms |
| 符号查找 + 打补丁 | 650 ms → 300 ms |
| 写 logbook | 214 ms |
| XNU 到苹果 logo | 8.8 s |
| XNU 用户态到桌面 | 48 s |

启动核实测约 **1000 MHz**（`cpu_mhz()` 用 PMCCNTR 量的），而
PLL 读数算出来是 249 MHz —— 两者不一致，说明 A55 还有别的分频路径，
**这个 PLL 读数不能直接信任**，后续查频率的人需要先解决这个矛盾。
