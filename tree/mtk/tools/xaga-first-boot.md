# xaga 首刷清单（真机到手后照着做）

> 手机：Redmi Note 11T Pro / `xaga` / MT6895 / 已解 BL + root（boot_a 有 KernelSU）
> 产物：`xnu-flashed-slot_a.img`（22.7 MB，boot_a 分区 64 MiB）
> 运行脚本：`mtk/tools/xaga-cycle.sh`

## 为什么不能用「试跑」

这台机 `lk_a` 的 fastboot **没有 `boot:` 命令**（手机的 `lk_a.img` 与官方 OS2.0.12 的 `lk.img` 都核过，`boot:` 作命令串出现 0 次），也**没有 recovery 分区**。所以：

- 不能 `fastboot boot` 只进内存试；
- 每次试验都是写 `boot_a`；
- 写坏了只能靠 fastboot 救回来。

因此顺序是「先只读验证，再逐层往上」。

## 第 0 步：备份（只做一次，最重要）

```sh
export HAKIOS_SERIAL=$(adb devices | awk 'NR==2{print $1}')
mkdir -p backup
adb -s $HAKIOS_SERIAL shell 'su -c "dd if=/dev/block/by-name/boot_a"' > backup/boot_a_magisk.img
md5sum backup/boot_a_magisk.img      # 应等于 a63b2e35a7c2522cb24a6a10332b9bc8
```

这台的 `boot_a` 里是 KernelSU。**不要用官方 stock 当备份**——恢复 stock 会丢 root，之后读 pstore 的 `su -c` 就失效了，而那是唯一能拿到 XNU 日志的路。

仓库里已经放了一份同 md5 的副本，可以先用它核对。

## 第 1 步：先验 UFS 读盘（不碰 Android，不动 boot_a 之外的任何东西）

UFS 驱动是全新写的、从未在真机上跑过。**先单独验证它**，因为后面每一步都依赖它读分区表。

仓库里已经打好了一份 `ufs-test-slot_a.img`（28876 字节的 loader + 同一个 payload），
不用现编，直接刷：

```sh
cd /home/lizi/IOS/hakios-mtk
fastboot -s $HAKIOS_SERIAL flash boot_a ufs-test-slot_a.img
fastboot -s $HAKIOS_SERIAL reboot
```

要自己重打则：

```sh
cd /home/lizi/IOS/hakios-mtk
DEVICE=xaga ./mtk/loader/build.sh -DUFS_TEST=1
python3 mtk/loader/build_flashed.py
mv xnu-flashed-slot_a.img ufs-test-slot_a.img   # 主镜像别被这个版本覆盖
DEVICE=xaga ./mtk/loader/build.sh               # 恢复常规模块
python3 mtk/loader/build_flashed.py
```

⚠️ `-DUFS_TEST=1` 版**不含** XNU 跳转代码（编译器把后续当死代码删了），它是纯自检镜像，跑完自己回 Android。

**看屏幕**（LK 的 framebuffer 上会打出来）：
- `== mtkloader: UFS read test ==`
- 控制器状态、TEST UNIT READY 的结果
- `ufs: unit 2: N blocks of 4096 bytes` ← READ CAPACITY(10) 问出的真实逻辑块大小
- **`GPT header: block 4096: EFI PART, entries at 2 count 128`** ← 这一行是关键。它先探测块大小
  （512 不行就 4096），再用那个块大小去读 header。真机上答的应该是 **4096**
- `boot_a LBA ...: ANDROID! kernel_size ...`
- 256 KB 读的耗时

**判断**：
- 能看到 GPT 与 `ANDROID!` → UFS 通了，进第 2 步
- `GPT header: block 0` → 512 和 4096 都没找到 header，说明 LUN 或控制器接管有问题
- `GPT header: block 4096: read, but not a GPT header: ...` → 读到了但内容不对，把后面 8 个字节拍照
- 打印 `logical block size is neither 512 nor 4096` → 设备给的块长是别的值，把屏幕拍照
- 卡住不动 → 等 120 秒看门狗自己重启回 Android；若没回来，用第 5 步救砖
- 打印了错误码 → 把屏幕拍照，错误码在 `ufs.h` 里有对应含义

### 为什么会有这次修复

`gpt_find()` 原来假设扇区是 512 字节，写的是 `board_read(1, 1, ...)` 再比对 `EFI PART`。
xaga 的 UFS 逻辑块是 **4096**（真机 `/sys/block/sdc/queue/logical_block_size` 实测），
GPT header 在 **byte 4096**，而 byte 512..1023 是保留区的全零。所以那行比对**永远失败**，
`load_system_image` 会打印 `no xnusys partition` 然后返回 0 —— XNU 就没有 md0，卡在
`Still waiting for root device`。

连带的两处同源错误也一起修了：条目表地址必须乘块大小（`ent_lba * 4096`，不是 `* 512`），
分区大小的 MB 显示原来按 512 算会显示成真实值的 1/8。

## 第 2 步：验证显示链路

UFS 通了之后，跑真 XNU 首刷。这一步要验证的是：`mtkloader` 能进、`chain_stub` 能把 Android 的 `Image` 搬回原位、XNU 能起、能打印。

```sh
mtk/tools/xaga-cycle.sh
```

它会：备份检查 → 编译 → 打镜像 → 刷 boot_a → 重启 → 等 Android 起来 → 通过 pmsg 请求一次 XNU → 等 XNU 跑完（45 秒）→ 自动回 Android → 把日志抓下来。

**看屏幕**：
- `== mtkloader: Darwin/XNU on XAGA (MT6895) ==` —— 到了这行说明 loader 起来了
- `LK dtb 0x...` —— 设备树地址
- `boot: Darwin / XNU (requested)` —— pmsg 请求被识别

**XNU 起来后应该能看到**（串口/屏幕二者之一，取决于 LK 留了什么）：
- 苹果 logo 之前是没有的（没有 bootlogo 阶段）
- 内核的早期打印

**回来之后**：
```sh
cat logs/xnu-xaga.log
```

**判断**：
- 有 XNU 的内核打印（`Darwin Kernel Version ...`）→ 内核跳转成功，最大的一关过了
- 只有 `mtkloader` 的打印、没有内核的 → loader 起来了但跳转失败，多半是 `KERNEL_PA` / `chain_stub` 的问题
- 什么都没有 → 显示链路问题，回去核 `OVL0_BASE` 与 `LK_FB_BASE`

## 第 3 步：确认自动回滚

上面每一步都靠 `loader_return_after=45`（XNU 跑 45 秒后自己交还）和 `XNU_WATCH_SECONDS=120`（watcher 核 120 秒没动静就复位）。这两个值在 `board.h` 里。

**故意验证一次**：跑 `XNU_SECS=10 mtk/tools/xaga-cycle.sh`，看是不是 10 秒就回 Android。如果不回，说明自动回滚没生效，后面的试验会很难受。

## 第 4 步：往「能看见界面」推

`hakios-kernel` 里有完整的 framebuffer 驱动（`MiniIOMobileFramebuffer`，含 `fbfit`/`fbscale` 的三种缩放分支），但**没有任何 MTK 驱动**（`strings hakios-kernel | grep -ic mediatek` = **0**）。所以第 2 步成功后，画面会是内核自己画的东西，不是 iOS 界面。

要往界面推，缺的是（按重要性）：
1. **触控** —— xaga 是 Novatek NT36672E（`novatek,NVT-ts-spi`，SPI2，GPIO 109/110/111/112，IRQ GPIO135），而 `tcm.h` 是 OmniVision TD4160 的协议，完全不同。要新写一个 Novatek 的 SPI 驱动。
2. **GPU** —— xaga 是 Mali-G610（`arm,mali-valhall` @ `0x13000000`），`mtkmali.h` 是 G52 MC2（`0x13040000`）的，寄存器不同。
3. **UFS 块设备** —— 现在 loader 能读盘，但内核里没有块设备。`hakios.ufshc.c` 假设 UEFI 的 UFSDxe 常驻（高通场景），MTK 这边没有对应的东西。
4. **Wi-Fi** —— CONNSYS 新一代，`mtkconnsys.h` 是旧的。

## 第 5 步：救砖

任何时候卡住/黑屏/不进 Android：

```sh
# 长按电源键 15 秒强制关机，然后 音量下 + 电源 进 fastboot
fastboot devices
fastboot -s $HAKIOS_SERIAL flash boot_a backup/boot_a_magisk.img
fastboot -s $HAKIOS_SERIAL reboot
```

如果连 fastboot 都进不去：MTK 的 BROM 模式（关机状态下按住两个音量键插 USB），用 SP Flash Tool 刷官方 ROM 的 `boot.img`。官方包在 `/home/lizi/下载/xaga_images_OS2.0.12.0.ULOCNXM_14.0.zip`。

## 每步之间记什么

每次跑完都存一份日志（`xaga-cycle.sh` 自动存到 `logs/xnu-xaga.log`），并且**拍下屏幕**。因为屏幕上的信息（`mtkloader` 的横幅、OVL 的寄存器值）在 pstore 里不一定有。

特别要记的两个数：
- `boot: Darwin / XNU (requested)` 有没有出现 —— 判断 pmsg 通道
- `display_alpha_off` 打印的 OVL L0_CON 新旧值 —— 这是核实 `OVL_L0_CON 0x030` 偏移对不对的唯一途径（xaga 的 DT 里 `0x1400b000` 是 ccorr1 不是 OVL0，三棵源码树都没有 OVL 寄存器头文件，无法离线核实）
