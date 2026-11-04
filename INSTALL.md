# 上手：把 iOS 装到 Redmi Note 11T Pro

本文只做一件事：让一台 `xaga` 启动进 iOS 桌面。面向第一次上手的用户。

整个过程分五步：**备份 → 备份分区表 → 刻 xnusys 分区 → 写入系统镜像 → 刷 boot_a**。

每一步都为"手机变砖"留了退路。真的卡住了，跳到最后的[救砖](#救砖)。

---

## 开始之前

### 确认手机

只适用于 **Redmi Note 11T Pro**，代号 `xaga`，SoC 是 MT6895（天玑 8100）。

营销名相近的机型不代表硬件相同。在手机上确认：

```bash
adb shell getprop ro.boot.hardware     # 应输出 mt6895
adb shell getprop ro.product.device    # 应输出 xaga
```

### 必须的条件

- **Bootloader 已解锁。** 解锁会清除用户数据，在解锁前备份。
- **已 root。** 后两步要在手机上 `su -c` 直接写块设备。KernelSU 或 Magisk 都可以。
- **电脑能识别 fastboot。** 装 `platform-tools`。
- **至少 3 GiB 空闲的 `userdata`。** 会从 `userdata` 尾部刻走 2.5 GiB。

### 需要的文件

| 文件 | 从哪来 | 大小 |
|---|---|---|
| `hakios-xaga-boot_a.img` | 本仓库 Releases | 22.9 MiB |
| `system.apfs` | 自行准备（见下） | 2.32 GiB |
| `boot_a` 的原备份 | 第一步自己做 | 64 MiB |

`system.apfs` 是 iOS 16.5 的用户态根文件系统（`/System`、`/usr`，含 SpringBoard、UIKit、WindowServer）。它不随本仓库分发，需要自己准备。

### 会丢的东西

- `userdata` 会被重新划分大小，**Android 的应用数据全部清空**。照片、微信记录、认证器恢复码，先备份出去。
- 手机原系统的 `boot_a` 会被覆盖，恢复靠第一步的备份。

---

## 第一步：备份 boot_a

这是唯一一处不做就会后悔的操作。

```bash
export S=$(adb devices | awk 'NR==2{print $1}')
echo $S

mkdir -p backup
adb -s $S shell 'su -c "dd if=/dev/block/by-name/boot_a of=/data/local/tmp/boot_a.img"'
adb -s $S pull /data/local/tmp/boot_a.img backup/boot_a.img
adb -s $S shell 'rm -f /data/local/tmp/boot_a.img'
md5sum backup/boot_a.img
```

记下这个 md5。恢复时先核对它，避免拿错文件。

`dd if=... | adb pull` 这种写法会损坏二进制（`adb shell` 是 pty 不是管道），必须先在手机上写到文件，再 `adb pull`。

顺手把 `vendor_boot_a` 也备份了，将来做 TWRP 之类的东西会用上：

```bash
adb -s $S shell 'su -c "dd if=/dev/block/by-name/vendor_boot_a of=/data/local/tmp/vb.img"'
adb -s $S pull /data/local/tmp/vb.img backup/vendor_boot_a.img
adb -s $S shell 'rm -f /data/local/tmp/vb.img'
```

---

## 第二步：备份分区表

改 GPT 是唯一"出错就回不来"的操作，所以先留底。

```bash
adb -s $S shell 'su -c "dd if=/dev/block/sdc bs=4096 count=64 of=/data/local/tmp/gpt-head.bin"'
adb -s $S pull /data/local/tmp/gpt-head.bin backup/gpt-head.bin
```

`xaga` 的 GPT 用 **4096 字节逻辑块**，header 在 byte 4096（不是 512），这也是备份从 `sdc` 开头 64 个 4096 块的原因。

---

## 第三步：刻 xnusys 分区

`system.apfs` 有 2.32 GiB，`boot_a` 只有 64 MiB，装不下。要从 `userdata` 尾部划一块独立的 `xnusys` 出来。

先**只算不写**，看清楚会动哪些数字：

```bash
HAKIOS_SERIAL=$S mtk/tools/xaga-carve.sh --gib 2.5
```

输出会列出：当前 `userdata` 的起止块、划走之后的新尺寸、新 `xnusys` 的起止块、entry 编号。确认没有异常，再加 `--write`：

```bash
HAKIOS_SERIAL=$S mtk/tools/xaga-carve.sh --gib 2.5 --write
```

脚本会自己备份原 GPT 到 `backup/gpt/`。

### 刻完之后必须重建 userdata

这一步不做，Android 会崩：f2fs 的超级块里还记着旧的 106.48 GiB，实际分区已经小了 2.5 GiB，surfaceflinger 会 SIGBUS。

```bash
adb -s $S reboot bootloader
fastboot -s $S erase userdata
fastboot -s $S reboot
```

Android 起来后会按新尺寸重建 `/data`，`df /data` 应该显示约 104 GB。

> **root 会一起没掉。** `erase userdata` 清掉的不只是数据，KernelSU 的应用配置也在里面。重新装一遍 KernelSU 管理器并激活 root，否则第四步写不进去。

确认分区在：

```bash
adb -s $S shell 'su -c "ls -l /dev/block/by-name/xnusys"'
```

应该指向 `/dev/block/sdc88`。

---

## 第四步：写入系统镜像

```bash
adb -s $S push system.apfs /data/local/tmp/system.apfs
timeout 1800 adb -s $S shell "su -c 'dd if=/data/local/tmp/system.apfs of=/dev/block/by-name/xnusys bs=4M'"
timeout 60 adb -s $S shell "su -c 'sync'"
adb -s $S shell 'rm -f /data/local/tmp/system.apfs'
```

2.32 GiB 走 USB 2.0 大概要 3 到 6 分钟，`dd` 那一步再几十秒。`timeout` 给宽一点。

校验一下写对了：

```bash
adb -s $S shell 'su -c "dd if=/dev/block/by-name/xnusys bs=512 count=1 2>/dev/null" | xxd -s 32 -l 4'
```

应该输出 `NXSB`（APFS 容器的魔数在 offset 32）。

仓库里的 `mtk/tools/system-flash.sh` 把这几步包好了，等价于上面全部：

```bash
HAKIOS_SERIAL=$S sh mtk/tools/system-flash.sh /path/to/system.apfs
```

---

## 第五步：刷 boot_a

```bash
adb -s $S reboot bootloader
fastboot -s $S flash boot_a hakios-xaga-boot_a.img
fastboot -s $S reboot
```

### 会看到什么

1. LK 的 logo 闪一下，然后黑屏。
2. 大约 20 秒后**苹果 logo** 出现，下面有进度条。
3. 大约 60 到 90 秒后进入**桌面**：状态栏、小组件、Dock。

第一次进系统会跑一次数据迁移，屏幕中间会转很久。这段时间不要断电。

首次启动后，机器上会一直停在这个桌面。

### 现在能做什么

- 看桌面、状态栏、控制中心、通知中心
- 打开设置、照片、日历、时钟
- 系统在软件渲染下跑 60 fps

### 现在不能做什么

- **没有触控**，没有按键，USB 也不枚举。iOS 起来了但操作不了，这一版只能"看"。
- 没有 GPU 加速，复杂界面的流畅度会掉。
- 没有 Wi-Fi、没有蜂窝、没有音频、没有相机。
- 重启不保存任何设置。

触控、按键、USB 都在修，进度见仓库 README 的「正在卡的地方」。

---

## 回到 Android

```bash
adb -s $S reboot bootloader
fastboot -s $S flash boot_a backup/boot_a.img
fastboot -s $S reboot
```

35 秒左右 Android 回来。

`xnusys` 分区留着不影响 Android，想彻底还原就把 GPT 恢复回去（`backup/gpt/` 里有原件），再 `fastboot erase userdata`。这一步会再清一次数据。

---

## 救砖

### 还能进 fastboot

问题不大。刷回备份就行：

```bash
fastboot devices                       # 确认能看到手机
fastboot -s $S flash boot_a backup/boot_a.img
fastboot -s $S reboot
```

### 刷 boot_a 报错

先确认当前在哪个 slot。`xaga` 是虚拟 A/B：

```bash
fastboot -s $S getvar current-slot     # 必须是 a
```

如果刷的是 `boot_a` 而机器在 `boot_b` 上启动，什么都不会变。

### 连 fastboot 都进不去

长按电源键 15 秒强制断电，然后 **电源 + 音量下** 进 fastboot。

MTK 的机型断电后会先枚举成 `0e8d:2000 MT65xx Preloader`，几秒后才是 fastboot。`lsusb` 看到 `0e8d:2000` 说明机器还活着，等一下再试。

### 都不行

用 MTK 官方刷机工具（SP Flash Tool）配合官方固件包救。这一步需要机器的 scatter 文件，官方固件里带。

> 官方 ROM 里的 `lk.img` 与本机 dump 的 `lk_a.img` 的 fastboot 命令表完全一致，`boot:` 出现 0 次 —— 所以没有"用 fastboot boot 临时试"这种办法，每次实验都是写 `boot_a`。这也是为什么本文把备份放在最前面。

---

## 常见问题

**为什么不能 `fastboot boot`？**

这台机的 LK 没有注册 `boot:` 命令。从 `lk_a.img` 里读出的完整命令表是 `getvar:`、`erase:`、`set_active:`、`continue`、`reboot-*`、`oem poweroff`、`oem p2u`、`download:`、`oem ultraflash`，没有 `boot:`。MTK 的刷机走 `download:` 加分区名的私有路径。

**为什么装完 iOS，Android 的数据还在但空间少了？**

`xnusys` 是从 `userdata` 尾部划的，Android 分区本身没被格式化，只是变小了。但 `erase userdata` 那一步会清空它 —— 那一步是为了让 f2fs 按新尺寸重建。

**屏幕右下角/整体偏右怎么办？**

已知问题，与 framebuffer 无关，是 DSI / panel 时序。暂时没有绕过办法。

**能刷回官方系统吗？**

能。恢复 `boot_a` 备份即可回到原来的 Android。要彻底还原分区表，用 `backup/gpt/` 里的原件恢复 GPT，再 `fastboot erase userdata`。

**这版会自己重启进 Android 吗？**

不会。Release 里是给用户用的版本，启动后停在 iOS。开发过程中有一个"跑完自动回 Android 方便取日志"的调试版，那个不在 Release 里。
