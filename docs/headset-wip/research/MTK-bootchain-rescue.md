# 联发科启动链与救援（xaga / MT6895）

> 调研对象：HakiOS 项目，Xiaomi xaga（Redmi Note 11T Pro / POCO X4 GT / Redmi K50i），MT6895 天玑 8100。
> 现场问题：`mtkloader` chainboot 进 `boot_a` 后连续四次黑屏、取不到日志。
>
> **证据分级**（每条结论都带）：
> - 🟢 **本地实测**：真机 dump 的镜像 / scatter / dts / 反汇编，路径写全，可复现
> - 🔵 **官方文档**：AOSP / 厂商 wiki
> - 🟡 **二手**：论坛、博客、社区工具，描述与 MTK 行为吻合但无一手确认
> - ⚪ **未确认**：只有推理，无出处
>
> 本文全部是**只读分析**。没有执行任何 `adb` / `fastboot` / 真机操作，也没写过任何分区。

---

## 0. 一句话：链长什么样、最容易死在哪

**链**：`BootROM → Preloader → TEE(TZ) → GenieZone(GZ) → LK → Linux kernel → Android init`
（出处：ItsVixano 的 xaga 专属 wiki，https://wiki.itsvixano.me/device_specific/preloader_xaga 🟡；与本机 `lk_a.img` 里的 `bl2_ext` / `aee` / `gz` 子分区结构吻合 🟢）

对 HakiOS 来说，链多插了一段：

```
LK  ──读 boot_a ──> Android Image 头
                     └─ kernel 段（gzip）= [mtkloader 程序][0x40000 处原 Android Image][XNUP payload]
                          └─ LK 解压到 0x40000000，跳进去
                               └─ mtkloader 起来 → 读 xnusys、建 boot_args → 跳 XNU
                                    └─ XNU 跑完 / 死掉 / 看门狗 → 回 Android 或复位
```

**最容易死在三处**（按"改 boot_a 之后"这个场景排序）：

1. **XNU 极早期就挂，LK 无从得知**。LK 跳 kernel 之后**完全不看返回值**（`boot_linux()` 是 `noreturn`），内核死在第一行它也只会静静躺着，直到 AP WDT 硬复位 🟢。**这是当前四次黑屏的实际所在地** —— 见 §5.6.6 的实锤：同一个 `boot_a`、同一个槽位、同样的 692 参数，**11-14 22:13 那轮完整跑到了 `Main will unblank` + SpringBoard 起来**，所以格式/AVB/槽位/LK 校验全部排除，问题在 **XNU 卷进来的代码回归**。
2. **LK 把 kernel 段当成 Linux Image 解压**。LK 的 `boot_linux_fdt()` 里是硬编码 `panic("decompress kernel image fail!!!\n")`，解压失败**不会**给你 fastboot，会 panic → 走 AEE → 复位。我们的 chainboot 把原 Image 塞进 gzip 里、外面又包一层 gzip，等于赌 LK 的 gunzip 能吃下这个 59 MB 的 blob 🟢（反汇编 + `svoboda18/lk` 的 `app/mt_boot/mt_boot.c:761-769`）。**当前余量不明，是下一个要盯的悬崖。**
3. **AVB 摘要对不上**。`boot_a` 的 vbmeta Hash descriptor 记的是 stock 镜像的摘要，改一个字节就 mismatch；`lk` 里 `load_vfy_boot.c` 的 `sec_img_auth()` 失败会 `ASSERT(0)` 🟢（本地 md5 与 salt-hash 双重验证）。**但实测 unlocked 状态下 LK 不 enforced**（692 那轮摘要早就 mismatch，照样起）。

---

## 1. 完整启动链（逐阶段）

### 1.1 阶段总表

| # | 阶段 | 跑在哪 | 从哪加载什么 | 日志机制 | 超时 / 看门狗 |
|---|---|---|---|---|---|
| 0 | **BootROM** | SoC 内部 ROM（掩膜，不可改） | UFS LU0 的 preloader 分区；或 USB 下载模式 | 无（只有 USB 枚举） | 握手窗口 ~2–3 s，超时后放弃下载、继续启动 🟡 |
| 1 | **Preloader** | DRAM 里的 SRAM 镜像，EL3 前 | `preloader`（UFS_LU0，1 MiB） | `log_store` → 写 SRAM + `expdb`；串口只在 ENG preloader 上开 | 进 fastboot/下载前有超时；`pllk` 日志经 `oem dump_pllk_log` 取出 🟢 |
| 2 | **TEE / ATF (BL31)** | EL3，Secure | `tee_a`（5 MiB）、`gz_a`（32 MiB）、`spmfw_a` | 崩溃走 `SYS_ATF_CRASH` 进 expdb 🟢 | ATF 有自己的 WDT |
| 3 | **GZ (GenieZone)** | EL1 secure partition | `gz_a` | 同上 | — |
| 4 | **LK**（= `lk_a`，8 MiB） | EL1 non-secure，MMU on，VA 基址 `0xffffffff` 🟢（`lk` 子分区头 `addr=0xffffffff`） | `lk_a`（内含 `lk` + `bl2_ext` + `aee` + `lk_main_dtb` 四个子分区，各带 cert1/cert2）🟢 | `log_store`（`/soc/logstore`，`enabled=1`）；崩溃/panic 走 AEE → expdb | **AP WDT**（`mediatek,mt6895-wdt`，`0x1c007000`）；另有 fastboot idle timer 🟢 |
| 5 | **LK 加载 boot** | 同上 | `boot_a` → `load_vfy_boot(BOOTIMG_TYPE_BOOT, CFG_BOOTIMG_LOAD_ADDR)`；`vendor_boot_a` → 拿 DTB；`dtbo_a` → overlay | 打印走 UART（`disable_uart=1` 时关掉） | 解压失败 → `panic()` 🟢 |
| 6 | **跳 kernel** | `0x40000000`（不是 Image 头里的 `text_offset`）🟢 | — | `boot_linux()` 之后 LK 不再参与 | 交给 AP WDT |
| 7 | **Linux / Android** | — | ramdisk + `system`（super 里的逻辑分区） | `ramoops`（`0x48090000`，`mem_size=0xe0000`）、pstore、`expdb` 🟢 | `panic_on_taint=20`、MTK WDT |

### 1.2 每个阶段的细节

#### BootROM
- xaga 的 BootROM **带 SLA/DAA 校验**，原厂状态下想从 BROM 写分区必须过小米账号授权。
  出处（xaga 专属 wiki，原文强调）：*"There is currently no way to bypass these checks on xaga, as Boot ROM has a bunch of checks to prevent unauthorised attacks."*
  https://wiki.itsvixano.me/device_specific/preloader_xaga 🟡
- **MT6895 是 V6 协议 + BROM 已打补丁**。mtkclient 官方 README 明写：
  *"MT6781, MT6789, MT6855, MT6886, **MT6895**, MT6983, MT8985 — These chipsets use a new protocol called V6 and the bootrom is patched. You need to use the `--loader` option and a proper loader from the Loaders/V6 directory. **Bootrom won't work, you need to use preloader mode** (no hw buttons pressed, just connect)."*
  https://github.com/bkerler/mtkclient 🔵（一手工具文档）
  → **这条很关键**：网上大量"按住音量上下进 BROM 就能救"的教程对 MT6895 **不成立**。
- mtkclient 的 `brom_config.py` 里 MT6895 的条目把 `watchdog=0x1c007000`、`uart=0x11001000` 都写死，**与我们 `BOARD.md` 从设备树拿到的值完全一致** 🟢（交叉验证成功）
  https://github.com/bkerler/mtkclient/blob/main/mtkclient/config/brom_config.py

#### Preloader
- 分区：`preloader`（1 MiB，`region=EMMC_BOOT1` / `UFS_LU0`）、`preloader_backup`（1 MiB，`EMMC_BOOT2` / `UFS_LU1`）🟢（`xaga_scatter.xml`）
- 两个槽都在，但 scatter 里 `is_download` 被默认关掉（这份 scatter 是 ItsVixano 改过的，注释里写 *"Disables the preloader flash option by default"*）🟢
- **工程 preloader 的差别**：刷了 ENG preloader 之后，**每次启动都暴露一个 insecure VCOM 口**，SLA/DAA 检查关掉，SP Flash Tool V6 免授权可写。
  https://wiki.itsvixano.me/device_specific/preloader_xaga 🟡
- **Preloader 模式的 USB ID**：`0e8d:2000`（`0x0E8D` + `0x2000`）。mtkclient 的 `usb_ids.py` 里 `0x0E8D` 段：
  ```
  0x0E8D: {0x0003: -1,  # MTK Brom
           0x6000: 2,   # MTK Preloader
           0x2000: -1,  # MTK Preloader
           0x2001: -1, 0x201D: -1, 0x20FF: -1, 0x3000: -1}
  ```
  https://github.com/bkerler/mtkclient/blob/main/mtkclient/config/usb_ids.py 🟢
- **本机实测过 preloader 模式**：`docs/headset-wip/DANGER-spmi-access.md` 记录，写错 SPMI 参数导致设备掉线，`lsusb` 出现 `0e8d:2000 MediaTek Inc. MT65xx Preloader`，**20–30 秒后自动重启回 Android**，`boot_a` 未受影响。→ 这是本机一手数据：**preloader 模式在 xaga 上会自动超时退出**，不是死等 🟢
  （这篇文档同时警告：`spmi_access` 的 SID 写成 `0x5` 会让设备掉线，写十进制 `5` 才安全）

#### LK
- `lk_a` 内部结构（本地解析，`0x58881688` 头链）🟢：

| 子分区 | 数据大小 | 备注 |
|---|---|---|
| `lk` | 799,768 B | 真正的 LK 代码 |
| `cert1` / `cert2` | 1733 / 1969 B | RSA 证书链 |
| `bl2_ext` | 831,344 B | EL3 阶段的扩展（跑在 ATF 里） |
| `aee` | 868,624 B | **AEE 异常引擎，含 KEDump / MRDUMP** |
| `lk_main_dtb` | 327,308 B | **与 `vendor_boot.dtb`、真机 `/proc/device-tree` md5 完全相同** 🟢 |

- `lk_main_dtb` 与 vendor_boot 里那份 DTB 是**同一个文件**（md5 `c1ab86afa6d57a9ff97d81dfdbf73232`）🟢。这解释了"改了 vendor_boot 的 dtb 会崩 LK"这个传闻的真实机制：
  **LK 用 `lk_main_dtb` 初始化自己（PMIC/按键/显示/SPMI 都在里面），之后才去读 `vendor_boot` 的 dtb 做内核用**。
  改 `vendor_boot` 里那份不会直接崩 LK 初始化，但会让"内核看到的 DTB"和"LK 自己用的 DTB"分叉；而 `dtbo` 是 **overlay**，`ufdt_apply_overlay()` 失败会打 `DT overlay fail (%d)` 然后 `panic` 🟢
- **dtbo 的真实内容**（本地 `dtc` 反编译 `rom/dtbo.img`）：🟢
  ```
  fragment@0 { __overlay__ { bootargs_ext = "console=tty0 root=/dev/ram androidboot.hardware=mt6895"; }; };
  fragment@1 { __overlay__ { DISABLE_MTKBATTERY = <0x01>; }; };
  fragment@2 { __overlay__ { vibrator@0 {...}; }; };
  fragment@3 { __overlay__ { xiaomi_touch {...}; }; };
  ...
  ```
  **注意 `bootargs_ext`**：这是 dtbo 注入内核 cmdline 的地方。改坏 dtbo = 丢 `console=tty0` 或丢 `androidboot.hardware=mt6895`。
- LK 的 fastboot 命令表（本地 `lk_a.img` offset `0x84b5c`–`0x85200` 字符串表）🟢：
  ```
  getvar:  reboot-bootloader  reboot-fastboot  reboot-recovery  continue  set_active:
  oem poweroff   download:   erase:   oem ultraflash:   oem ultraflash_en
  oem p2u  oem lkmsg  oem lpmsg  oem uefilog  oem expdb  oem dump_pllk_log
  oem usb2jtag  oem fbreason  oem get_socid  oem off-mode-charge
  oem mrdump  oem mrdump_out_set  oem mrdump_fallocate  oem mrdump_chkimg
  oem unlock  oem lock  oem get_token  oem lks  oem cdms
  ```
  **怎么读这张表（方法论，重要）**：`strings` 会漏掉"后缀合并"的字符串。我用的判据是
  **「找到一个 `名字:` 后紧跟 `\0` 的位置」**，因为编译器会把 `"flash:"` 这种后缀合并进
  `"oem ultraflash:"` 的尾部，两者共享一个 NUL。按这个判据实测 🟢：

  | 命令 | 出现次数 | NUL 终止的独立字符串 | 判定 |
  |---|---|---|---|
  | `getvar:` | 1 | ✅ `0x8495c` | 已注册 |
  | `erase:` | 1 | ✅ `0x8584e` | 已注册 |
  | `download:` | 1 | ✅ `0x85844` | 已注册 |
  | `set_active:` | 1 | ✅ `0x8499f` | 已注册 |
  | `continue` | 3 | ✅ `0x84996` | 已注册 |
  | **`flash:`** | 1 | ✅ `0x8585e`（**从 `oem ultraflash:` 尾部合并出来的**，`0x85855+9`） | **已注册** |
  | **`boot:`** | 7 | **❌ 一个都没有**（7 处全是 `"fastboot: ..."` 的子串，后面跟的是空格/字母，不是 NUL） | **未注册** |

  → **结论：`flash:` 在，`boot:` 不在。**
  `fastboot flash boot_a xx.img` 能用是真的（本机刷机日志 `Sending 'boot_a' ... OKAY` / `Writing 'boot_a' OKAY` 🟢）；
  `fastboot boot` 不能用也是真的 —— **LK 根本没注册这个命令**，不是"接受后加载失败"。
  ⚠️ 与 XDA 上 POCO X4 GT 实测报 `remote: unknown command` 吻合 🟡
  （https://xdaforums.com/t/failure-to-boot-into-custom-recovery.4757295/）
  ⚠️ 这条对 AOSP 官方那句"these devices don't support the `fastboot boot` command"给出了**机理解释**：
  不是"被策略禁止"，是**命令表里没有这一项**。

- **LK 日志**：`log_store`（设备树 `/soc/logstore { enabled=1 }`）🟢；`oem lkmsg` 把它吐出来，格式字符串 `oem_dump_lkmsg: log_header=0x%p data_size=0x%x` + `DATA%08llx` + `lkmsg log::::` 🟢
  （sibling 报告 `BLACKSCREEN-forensics.md` 给出基址 `0x7ffbf000`、256 KiB；本文未独立复核该地址，标 ⚪）

#### 跳 kernel
- LK 忽略 boot image header 里的 `text_offset`，**固定把 kernel 解压到 `CFG_BOOTIMG_LOAD_ADDR` = `0x40000000`**。
  源码注释（`app/mt_boot/mt_boot.c:761-763`，https://github.com/svoboda18/lk/blob/master/app/mt_boot/mt_boot.c）🟢：
  ```
  /* for 64bit decompreesed size.
   * LK start: 0x41E00000, Kernel Start: 0x40080000
   * Max is 0x41E00000 - 0x40080000 = 0x1D80000.
   * using 0x1C00000=28MB for decompressed kernel image size */
  ```
  这段注释是**旧平台的**（那时 LK 在 `0x41E00000`）。本机 `/proc/iomem` 显示 `40000000-4807ffff : System RAM`，`40000000-4201ffff : Kernel code`，`42320000-4263ffff : Kernel data` 🟢 —— 说明 xaga 上 kernel 窗口比 28 MB 大得多，`KERNEL_DECOMPRESS_SIZE` 被平台重定义过（⚪ 具体值未从 `lk_a.img` 里定死，但**我们能确认 40 MB 解压结果在其工作范围内**——真机 stock 内核 46.85 MB 都能起）。
  ⚠️ 但注意：**我们自己的 release 镜像 gzip 后 21 MB、解压后 61.6 MB**，而 stock 只有 46.85 MB。增长 31%，全在 `boot_a` 的 kernel 段里。
- LK 的 `boot_linux_fdt()` 有三道硬检查 🟢：
  ```c
  if ((uint32_t)kernel_target_addr & 0x7FFFF) panic("64 bit kernel can't boot at 0x%08x\n");
  if (decompress_kernel(...))                    panic("decompress kernel image fail!!!\n");
  if (dtb_dconfig_overlay(fdt) == FALSE)         panic("dconfig DT overlay failed, system not bootable\n");
  ```
  然后还有 `mboot_allocate_bootimg_from_mblock()` 里一串 `assert(0)`：
  ```c
  if (kernel_addr_mb != p_boot_hdr->kernel_addr) { pal_log_err("Warning! kernel_addr ... not taken from mb"); assert(0); }
  if (ramdisk_addr_mb != p_boot_hdr->ramdisk_addr) { ... assert(0); }
  if ((ramdisk_addr_mb != LK_RAMDISK_BASE) || (ramdisk_sz_mb > LK_RAMDISK_MAX_SIZE)) { ... assert(0); }
  ```
  **`LK_RAMDISK_MAX_SIZE` 默认 16 MiB**（`mt_boot.c:97-99`）🟢。xaga 的 ramdisk 1.87 MB，安全。

#### Android
- `boot_a` 的 cmdline 段在 stock 与我们的镜像里**都是空的**（`d[44:1580]` 全 0）🟢。内核 cmdline 全部来自 DTB 的 `chosen/bootargs` + dtbo 的 `bootargs_ext` + LK 追加的 `androidboot.*`。
- 真机抓到的 cmdline（`dump/cmdline.txt`）🟢：
  ```
  ... console=tty0 root=/dev/ram 8250.nr_uarts=4 ... mtk_printk_ctrl.disable_uart=1
  log_buf_len=2M ramoops.mem_address=0x48090000 ramoops.mem_size=0xe0000
  ramoops.pmsg_size=0x10000 ramoops.console_size=0x40000
  ... hwid.sku=xaga hwid.country=CN hwid.level=MP hwid.version=3.9.0
  aee_aed.pureason=reboot aee_aed.poffreason=Cold_reset panic_on_taint=20 ... bootconfig
  ```
  **注意：里面没有 `androidboot.verifiedbootstate=`，也没有 `androidboot.slot_suffix=`。**
  这两个是 LK 在 `set_fdt_avb_cmdline()` / `boot_mode_cmdline()` 里追加的；它们的缺失说明这份 cmdline 抓的是**内核解析后的 `/proc/cmdline`**，而 `androidboot.*` 被 init 消费掉了（正常行为）。⚪ 不能据此判断 AVB 状态。

---

## 2. 分区可写性分级（表格）

> 分级标准：**A = 随便刷（刷坏只丢功能，能刷回来）**；**B = 能刷但要小心（刷错要动 fastboot/BROM）**；**C = 刷错会进不了 fastboot（软砖）**；**D = 刷错/擦错不可恢复（硬砖 / 丢 IMEI）**。
> 所有大小来自 `xaga_scatter.xml`（182 条，UFS_LU2）🟢，块设备号来自真机 `dump/byname.txt` 🟢。

### 2.1 A 级：安全可写

| 分区 | 大小 | 块设备 | 说明 |
|---|---|---|---|
| `boot_a` / `boot_b` | 64 MiB | sdc43 / sdc72 | **我们的战场**。刷坏顶多起不来，fastboot 还在 |
| `vendor_boot_a` / `_b` | 64 MiB | — | 装 recovery ramdisk。⚠️ 刷错会让 recovery 也进不去 |
| `dtbo_a` / `_b` | 32 MiB | sdc45 / sdc74 | ⚠️ overlay，刷错丢 `bootargs_ext` |
| `vbmeta_a` / `_b` | 8 MiB | — | 刷成 disable-verity 版本是常规操作 |
| `vbmeta_system_a/_b`、`vbmeta_vendor_a/_b` | 8 MiB | — | 链式 vbmeta |
| `logo_a` / `_b` | 64 MiB | sdc40 / sdc69 | 开机 logo，刷坏只是难看 |
| `rescue` | 128 MiB | sdc84 | 挂 `/cache` |
| `cust` | 2 GiB | sdc83 | 小米定制区 |
| `metadata` | 32 MiB | sdc20 | FBE key 目录；擦掉会丢加密密钥（数据打不开，但机器能起） |
| `misc` | 4 MiB | sdc1 | **boot_control 槽位状态在这**（见 §5.3）。刷/擦会清掉 slot 记录 → LK 回落到 slot a。⚠️ 见 §6.3 D |
| `frp` | 1 MiB | sdc12 | 恢复出厂保护 |
| `userdata` | `NEEDRESIZE` | sdc86 | 我们的 `xnusys` 就是从它尾部刻的 |

### 2.2 B 级：能刷，但刷错要动 fastboot / BROM

| 分区 | 大小 | 风险 |
|---|---|---|
| `lk_a` / `_b` | 8 MiB | **刷错 = 没有 fastboot = 软砖**。要 BROM/SP Flash Tool 才能救。⚠️⚠️ |
| `md1img_a/_b` | 200 MiB | 基带固件。刷错丢信号，但机器能起 |
| `scp_a/_b`、`sspm_a/_b`、`mcupm_a/_b`、`gpueb_a/_b`、`apusys_a/_b`、`mvpu_algo_a/_b`、`ccu_a/_b`、`dpm_a/_b`、`pi_img_a/_b`、`vcp_a/_b`、`audio_dsp_a/_b`、`spmfw_a/_b`、`mcf_ota_a/_b` | 1–64 MiB | 各种 MCU 固件。**刷错会导致"能开机但某功能全废"**，重刷官方 ROM 可恢复 |
| `gz_a/_b` | 32 MiB | GenieZone。刷错 LK 起不来 → 软砖 ⚠️ |
| `tee_a/_b` | 5 MiB | TrustZone。同上 ⚠️ |
| `super` | 8.5 GiB | 动态分区容器，装 system/vendor/product/odm 等。**只能通过 fastbootd 刷逻辑分区**，直接 `fastboot flash super` 会毁掉整套系统 |

### 2.3 C 级：刷错直接软砖（进不了 fastboot）

| 分区 | 大小 | 后果 |
|---|---|---|
| `preloader` | 1 MiB（UFS_LU0） | 刷错 = **连 preloader 都没有 = BROM 都未必认**。⚠️⚠️⚠️ |
| `preloader_backup` | 1 MiB（UFS_LU1） | 同样的道理，这是唯一的备份 |
| `seccfg` | 8 MiB（sdc22） | **BL 解锁状态存这里**。擦掉/写坏 = 锁状态不确定，可能变回锁 + 变砖。⚠️⚠️⚠️ |
| `lk_a` / `lk_b` | 8 MiB | 见上 |
| `boot_para` | 1 MiB（sdc50） | 启动参数 |
| `dram_para` | 25 MiB（sdc51） | DDR 训练参数。**刷错 = 内存起不来 = 硬砖** ⚠️⚠️⚠️ |
| `proinfo` | 3 MiB（sdc6） | 产品信息，`is_download=false`，被 protected |
| `flashinfo` | 16 MiB | `RESERVED` |

### 2.4 D 级：绝对不碰（不可恢复）

| 分区 | 大小 | 为什么 |
|---|---|---|
| `nvram` | 64 MiB（sdc5） | **IMEI / MAC / 射频校准**。擦掉后手机永久无信号，且**官方售后才能重写**。scatter 里 `operation_type=BINREGION` |
| `nvdata` | 64 MiB（sdc13） | NV 数据（WiFi/BT 配置也在里面）。postmarketOS 的 initramfs 专门从这里拷 WiFi/BT 配置 → 说明它含唯一数据 |
| `nvcfg` | 32 MiB | NV 配置 |
| `protect1` / `protect2` | 8 MiB ×2（sdc3 / sdc4） | 受保护分区，`operation_type=PROTECTED` |
| `persist` | 64 MiB（sdc8） | 传感器校准、指纹模板 |
| `sec1` | 2 MiB（sdc48） | 安全区 |
| `efuse` | 512 KiB（sdc49） | eFuse 镜像。**写它 = 物理熔断** ⚠️⚠️⚠️ |
| `otp` | 48 MiB（sdc23） | 一次性可编程区。**出厂后不可再写** |
| `countrycode` | 2 MiB | 国家码 |
| `expdb` | 128 MiB（sdc11） | **别擦**。这是 MTK 的异常日志库，是黑屏取证的唯一 flash 落点（见 §1） |
| `devinfo`（若存在） | — | 部分机型解锁标志在这；xaga 的 by-name 里没有（`byname.txt` 无此项），但 LK 字符串里有 `devinfo[%s]` 🟢 → 说明代码支持，只是这台没建 |

> **注**：`nvram` / `otp` 在 §2.3 里也出现过，那是按"操作危险性"归类；在 §2.4 里再列一次是因为它们**碰了就不可恢复**。以更严格的 D 级为准。

### 2.5 ⚠️ 三条来自 xaga 专属 wiki 的硬禁忌

出处：https://wiki.itsvixano.me/device_specific/preloader_xaga 🟡（原文措辞很重）

| 操作 | 后果 |
|---|---|
| SP Flash Tool 选 **`Firmware Upgrade`** | **会把 bootloader 重新锁上** |
| SP Flash Tool 选 **`Format all + Download`** | **擦掉整片 UFS，含 IMEI、MAC**，不可恢复 |
| 用错 scatter | 分区错位，可能直接废板 |

→ **永远只用 `Download Only`，并且先备份。**

---

## 3. boot image 格式与参数

### 3.1 header v3 / v4 结构（AOSP 权威）

出处：https://source.android.com/docs/core/architecture/bootloader/boot-image-header 🔵
结构体原文：https://android.googlesource.com/platform/system/tools/mkbootimg/+/refs/heads/main/include/bootimg/bootimg.h 🔵

**boot_img_hdr_v4**（1584 字节）：

| offset | 字段 | 说明 |
|---|---|---|
| 0 | `magic[8]` | `"ANDROID!"` |
| 8 | `kernel_size` | 字节 |
| 12 | `ramdisk_size` | 字节 |
| 16 | `os_version` | v4/GKI 设备**必须为 0** |
| 20 | `header_size` | v3=1580，v4=1584 |
| 24 | `reserved[4]` | 16 B |
| 40 | `header_version` | 3 或 4 |
| 44 | `cmdline[512+1024]` | 1536 B |
| 1580 | `signature_size` | v4 才有；GKI boot_signature 长度 |

**关键：v3/v4 的 header 里没有 `page_size` 字段，页大小固定 4096。**（`bootimg.h` 注释原文：*"Note that in version 4 of the boot image header, page size is fixed at 4096 bytes."*）🔵
→ 这也是 `chainboot.py` 里 `V34_PAGE = 4096  # v3/v4: no page_size field in the header` 的由来 🟢

布局：
```
+---------------------+
| header (1 page)     |  4096
+---------------------+
| kernel   (m pages)  |  m = ceil(kernel_size/4096)
+---------------------+
| ramdisk  (n pages)  |
+---------------------+
| signature(g pages)  |  g = ceil(signature_size/4096)   ← v4 only
+---------------------+
```

**vendor_boot_img_hdr_v4**（2128 字节）：在 v3 的 2112 字节后面追加 4 个 `uint32` —
`vendor_ramdisk_table_size` / `vendor_ramdisk_table_entry_num` / `vendor_ramdisk_table_entry_size` / `bootconfig_size`。

vendor_boot v4 布局：
```
+------------------------+
| vendor boot header     | o = ceil(2128 / page_size)
+------------------------+
| vendor ramdisk section | p = ceil(vendor_ramdisk_size / page_size)
+------------------------+
| dtb                    | q = ceil(dtb_size / page_size)
+------------------------+
| vendor ramdisk table   | r = ceil(vendor_ramdisk_table_size / page_size)
+------------------------+
| bootconfig             | s = ceil(bootconfig_size / page_size)
+------------------------+
```
出处：https://source.android.com/docs/core/architecture/partitions/vendor-boot-partitions 🔵

### 3.2 xaga 的实测值（本地解析）🟢

**`boot_a`（stock，`dump/boot_a.img`，md5 `a63b2e35a7c2522cb24a6a10332b9bc8`）**

| 项 | 值 |
|---|---|
| magic | `ANDROID!` |
| header_version | **4** |
| header_size | **1584** |
| kernel_size | 15,311,555（gzip，`1f8b08`） |
| ramdisk_size | 1,867,873（lz4 legacy，`02 21 4c 18`） |
| os_version | `0x1800018c` |
| cmdline | **空**（1536 字节全 0） |
| signature_size | **4096** |
| kernel 偏移 | `0x1000` |
| ramdisk 偏移 | `0xE9C000` |
| signature 偏移 | `0x1065000` |
| 镜像有效长度 | `0x1066000` = 17,195,008 B |
| AVB footer | `AVBf` v1.0 @ `0x3ffffc0`，`orig_image_size=17195008`，`vbmeta_offset=17195008`，`vbmeta_size=1600` |
| vbmeta flags | **0**（既没禁 verity 也没禁 verification） |
| vbmeta algorithm | `SHA256_RSA2048`，pubkey sha1 `b2a02f1e56e366d727a1a8e089762fe0b91bbc84` |
| Hash descriptor | `image_size=21020672`（⚠️ **比 `orig_image_size` 大 3.8 MB**），salt `72e17ae8…`，digest `e4ab2ceb…` |
| Prop | `com.android.build.boot.fingerprint = Xiaomi/mihal/mihal:12/SP1A.210812.016/OS2.0.12.0.ULOCNXM:user/release-keys` |

> 解析工具：AOSP 官方 `avbtool.py`（`python3 avbtool.py info_image --image boot_a.img`）。可复现。

**`vendor_boot_a`（stock，`dump/vendor_boot_a.img`）** 🟢

| 项 | 值 |
|---|---|
| magic | `VNDRBOOT` |
| header_version | **4** |
| page_size | **4096** |
| kernel_addr | `0x40000000` |
| ramdisk_addr | `0x66f00000` |
| vendor_ramdisk_size | **42,741,855** |
| cmdline | `bootopt=64S3,32N2,64N2` |
| tags_addr | `0x47c80000` |
| name | 空 |
| header_size | **2128** |
| dtb_size | **327,372** |
| dtb_addr | `0x47c80000` |
| vendor_ramdisk_table_size | 108 |
| entry_num | **1** |
| entry_size | 108 |
| **bootconfig_size** | **0** |
| ramdisk 段偏移 | `0x1000`（10436 页） |
| dtb 段偏移 | `0x28c5000` |
| table 段偏移 | `0x2915000` |
| 镜像有效长度 | `0x2916000` = 43,081,728 B |
| dtb 段内容 | **DTBO 容器**：magic `0xd7b7ab1e`，`total=327372`，`hdr=32`，`entry=32`，`count=1`，`page=2048`，单条 entry `size=327308 off=64` |
| ramdisk 表项 | `size=42741855`，`offset=0`，`type=1`（`VENDOR_RAMDISK_TYPE_PLATFORM`），`name=""`，`board_id` 全 0 |

> ⚠️ **注意 `type=1`（PLATFORM）**。AOSP 规定 PLATFORM 类型的 ramdisk "bootloader should **always** load these into memory"。也就是说 vendor_boot 里那 42.7 MB 的 ramdisk **每次启动都会被 LK 读并搬进内存**，即使我们不需要它。
>
> 这也验证了任务背景里的数字：`vendor_boot` header v4 / page 4096 / header 2128 / ramdisk 64,195,136 B / dtb 327,372 B。
> ⚠️ **`64,195,136` 这个数在本机三个镜像里都搜不到**（`dump/vendor_boot_a.img` 和 `rom/vendor_boot.img` 里按 LE32 / LE64 搜索均 0 命中）🟢。
> 它是 `64 MiB − (header + dtb + table + 1 page)` 那类"分区余量"口径，**不是任何 header 字段**。
> **请勿把 `64195136 / 4096 = 15672.64` 当成"ramdisk 占 15672 页"** —— 真实是 `42,741,855 / 4096 = 10435.0 → 10436 页`。
> 如果某次分析按 64,195,136 去算偏移，`dtb_offset` 会算成 `0x3d39000` 而不是真实的 `0x28c5000`，**差 21 MB**，据此算出来的任何结论都作废。

**dtbo（`rom/dtbo.img`，85,072 B）** 🟢
- DTBO 容器 `total=81305 hdr=32 entry=32 count=1 page=2048`，entry `size=81241 off=64`
- 单个 overlay FDT，`__symbols__` 在末尾（offset 49072）
- `fragment@0` 注入 `bootargs_ext = "console=tty0 root=/dev/ram androidboot.hardware=mt6895"`

### 3.3 MTK 加了什么？

**结论：`boot_a` / `vendor_boot_a` 里没有 MTK 分区头。** 🟢

- 扫描整个 `dump/boot_a.img`（64 MiB）和 `dump/vendor_boot_a.img`，`0x58881688` 出现 **0 次**。
- MTK 头（`0x58881688`，512 B，含 `name` / `size` / `load_addr` / `img_type`）**只出现在 `lk` / `preloader` / `tee` 这类"MTK 自有分区"里**，kernel 侧用的是纯 AOSP 格式。
  出处（MTK 分区头结构）：https://github.com/jsoltan226/mtkpartdump 🟡 + 本地 `lk_a.img` 实测 🟢
- 但**有间接的 MTK 痕迹**：
  1. `vendor_boot` 的 cmdline 字段 = `bootopt=64S3,32N2,64N2`（MTK 专有 bootopt 串，控制 DRAM 位宽/通道配置）
  2. `dtb` 段是 DTBO 容器而不是裸 FDT（AOSP 也允许，但 MTK 常用）
  3. `boot_a` 末尾的 AVB footer 是 MTK 改过的 `avbtool 1.2.0` 生成的（`release_string` 字段），**Hash descriptor 的 `image_size` 比 `orig_image_size` 大**——这是 MTK 的 padding 习惯，AOSP 标准 avbtool 不会这么干

### 3.4 对 xaga 正确的 mkbootimg 参数

**权威来源（一手）：TWRP device tree 的 `BoardConfig.mk`** 🟢
`dt-device_xiaomi_xaga-TWRP/BoardConfig.mk:64-83`：
```make
BOARD_VENDOR_CMDLINE      := bootopt=64S3,32N2,64N2
BOARD_KERNEL_BASE         := 0x3fff8000
BOARD_PAGE_SIZE           := 4096
BOARD_KERNEL_OFFSET       := 0x00008000      # → kernel_addr  = 0x40000000
BOARD_RAMDISK_OFFSET      := 0x26f08000      # → ramdisk_addr = 0x66f00000
BOARD_TAGS_OFFSET         := 0x07c88000      # → tags_addr    = 0x47c80000
BOARD_DTB_OFFSET          := 0x07c88000      # → dtb_addr     = 0x47c80000
BOARD_BOOT_HEADER_VERSION := 4
BOARD_HEADER_SIZE         := 2128
BOARD_DTB_SIZE            := 322337
BOARD_FLASH_BLOCK_SIZE    := 262144
BOARD_KERNEL_SEPARATED_DTBO := true
BOARD_AVB_ENABLE := true
```

**验算**（与本地 dump 逐字段核对）🟢：
- `0x3fff8000 + 0x8000     = 0x40000000` ✅ 等于 vendor_boot header 的 `kernel_addr`
- `0x3fff8000 + 0x26f08000 = 0x66f00000` ✅ 等于 `ramdisk_addr`
- `0x3fff8000 + 0x07c88000 = 0x47c80000` ✅ 等于 `tags_addr` / `dtb_addr`

→ **三个地址全部对上，这组参数是可信的。**

**打包 boot（v4）**：
```bash
mkbootimg \
  --header_version 4 \
  --pagesize 4096 \
  --base 0x3fff8000 \
  --kernel_offset 0x00008000 \
  --ramdisk_offset 0x26f08000 \
  --tags_offset 0x07c88000 \
  --kernel Image.gz \
  --ramdisk ramdisk.lz4 \
  --cmdline "" \
  --board "" \
  -o boot.img
# 注意：v4 的 boot 里没有 --dtb，dtb 在 vendor_boot 里
```
（`--cmdline` 留空是对的，stock 就是空的；内核参数靠 DTB + dtbo 的 `bootargs_ext`）

**打包 vendor_boot（v4）**：
```bash
mkbootimg \
  --header_version 4 \
  --pagesize 4096 \
  --base 0x3fff8000 \
  --kernel_offset 0x00008000 \
  --ramdisk_offset 0x26f08000 \
  --tags_offset 0x07c88000 \
  --dtb_offset 0x07c88000 \
  --vendor_boot vendor_boot.img \
  --vendor_cmdline "bootopt=64S3,32N2,64N2" \
  --vendor_ramdisk vendor_ramdisk.lz4 \
  --dtb dtb.img \
  --board "" \
  -o vendor_boot.img
```
⚠️ **不要轻易重打 vendor_boot**：它的 ramdisk 里是 recovery 资源 + DLKM，dtb 段是 DTBO 容器，两者都由原厂工具链生成。重打极易丢掉 `bootargs_ext` 或 vendor ramdisk 表项类型。

### 3.5 AVB 签名（务必看懂这一段）

`mkbootimg` 出来的镜像**没有** AVB footer，必须再过一次 `avbtool add_hash_footer`：

```bash
avbtool add_hash_footer \
  --image boot.img \
  --partition_name boot \
  --partition_size 67108864 \
  --hash_algorithm sha256 \
  --salt <16+ bytes hex> \
  --key <your_rsa.pem> \
  --algorithm SHA256_RSA2048
```

⚠️ **但 xaga 上"签名了也没用"**，原因有两层：

1. **公钥不匹配**。stock 用的是小米的 key（pubkey sha1 `b2a02f1e…`）。你用自己 key 签，LK 会走 `validate_public_key_for_partition()` 失败 → `ERROR_PUBLIC_KEY_REJECTED` 🟢（`lk_a.img` 里有完整的 libavb 错误串表）
2. **`vbmeta` 分区里锁了公钥哈希**。LK 字符串 `[AVB] pubk hash in %s partition =` / `[AVB] vbmeta_avb_key =` / `[AVB] lk_avb_key =` 说明它把公钥哈希**另存了一份**在 `vbmeta` 分区里做二次校验 🟢

→ 所以改 `boot_a` 之后，**要么 vbmeta 关掉校验，要么整个绕过 AVB**。见 §5.2。

---

## 4. 刷机与救援

### 4.1 fastboot 在 xaga 上到底有哪些命令

**来源：本地 `lk_a.img` 命令表**（offset `0x84b5c`–`0x85200`）🟢。这是**一手**，比任何教程都准。

| 命令 | 在表里 | 说明 |
|---|---|---|
| `getvar:` | ✅ | `version-bootloader` / `max-download-size` / `product` / `serialno` / `is-userspace` / `current-slot` / `slot-count` / `has-slot:` / `partition-type:` / `partition-size:` / `slot-unbootable:a|b` / `slot-successful:a|b` / `slot-retry-count:a|b` |
| `download:` | ✅ | fastboot 传数据的底层命令 |
| `erase:` | ✅ | **可用**。`fastboot erase userdata` 走这条 |
| `flash`（两步 download+commit） | ✅ | `fastboot flash boot_a xx.img` 实测可用（本机日志 `Sending 'boot_a' (22436 KB) OKAY` / `Writing 'boot_a' OKAY`）🟢。字符串 `flash:` 存在于 `0x8585e`（从 `oem ultraflash:` 尾部合并出来的，`\0` 终止，可判为已注册） |
| `set_active:` | ✅ | `fastboot --set-active=a` |
| `reboot` / `reboot-bootloader` / `reboot-fastboot` / `reboot-recovery` | ✅ | |
| `continue` | ✅ | 从 fastboot 继续启动 |
| **`boot:`（下载并直接启动）** | ❌ **不在表里** | 7 处 `boot:` 全是 `"fastboot: ..."` 的子串，**没有一处 `\0` 终止** → **这是 `fastboot boot` 不可用的根因** |
| `oem poweroff` | ✅ | |
| `oem lkmsg` | ✅ | **吐 LK 的 log_store** |
| `oem lpmsg` | ✅ | 另一路日志 |
| `oem uefilog` | ✅ | UEFI 日志（xaga 无 UEFI，可能空） |
| `oem dump_pllk_log` | ✅ | **preloader → LK 的日志** |
| `oem expdb` | ✅ | **压缩后吐出 expdb 分区** |
| `oem fbreason` | ✅ | 上次重启原因（`vol down` / `reboot bootloader` / `boot menu` / `unknown`） |
| `oem mrdump` / `mrdump_out_set` / `mrdump_fallocate` / `mrdump_chkimg` | ✅ | **手动触发 / 取 MRDUMP**（AEE 的 minirdump） |
| `oem get_socid` | ✅ | |
| `oem usb2jtag` | ✅ | ⚠️ 把 USB 切成 JTAG，切完要重新上电 |
| `oem p2u` | ✅ | "phone to UART"？MTK 的 USB 转 UART 调试 |
| `oem unlock` / `oem lock` / `oem get_token` / `oem lks` | ✅ | 小米的解锁流程 |
| `oem cdms` | ✅ | 未知 |
| `oem ultraflash:` / `oem ultraflash_en` | ✅ | MTK 的高速刷写通道 |
| `oem off-mode-charge` | ✅ | |

**⚠️ 重要推论**：`fastboot boot` 不可用**不是"被拒绝"，而是"命令不存在"**。任何"用 `fastboot boot` 测试镜像再刷"的流程在 xaga 上根本不成立——**每一次测试都要真刷 `boot_a`**，这是黑屏代价高的结构性原因。

**替代方案**（xaga 专属，AOSP 官方 recovery 文档口径）🔵：
```
These devices don't support the "fastboot boot" command, so the custom recovery images
are built to be flashed to "vendor_boot_a" and "vendor_boot_b" partitions directly.
Platforms: Dimensity 8100/8100-Ultra/9000/9000+  Devices: daumier, matisse, rubens, xaga(pro/in)
Image size: 64 MiB (67,108,864 bytes)
```
https://source.android.com/docs/core/architecture/partitions/vendor-boot-partitions 🔵（同段落也在 xiaomi.eu / droidwin 等转述里出现 🟡）
→ 想要"不改 boot_a 也能测"的路子，**只能刷 `vendor_boot_ab`**，而且要接受它会覆盖 recovery。

### 4.2 刷机命令清单

**常规刷 boot（保留 fastboot 退路）**：
```bash
adb -s $S reboot bootloader
fastboot -s $S flash boot_a backup/boot_a_stock_real.img   # 先确认能刷回去
fastboot -s $S flash boot_a xnu-flashed-slot_a.img
fastboot -s $S reboot
```

**A/B 槽位（xaga 是虚拟 A/B）** 🟢：
- `boot_a`/`boot_b` 是**物理两份**（`sdc43` / `sdc72`），`super` 里的 system 是**逻辑一份** + 快照
- `fastboot flash boot` **会报错**（要槽位），用 `boot_a` 或 `boot_ab`
- `fastboot --set-active=a` / `getvar current-slot`
- 出处（本项目已核）：`xaga-facts.md` §2.3 / `BOARD.md`「AB_OTA_PARTITIONS」

**vbmeta 关校验（常规改机第一步）**：
```bash
fastboot --disable-verity --disable-verification flash vbmeta vbmeta.img
```
⚠️ **必须用原机的 vbmeta.img 作底**，不要用别的机型/别的 ROM 的。
⚠️ 在 MTK 上这条**经常不生效**（见 §5.2），别把它当成万能钥匙。

**刷 vendor_boot（recovery）**：
```bash
fastboot flash vendor_boot_ab recovery.img
fastboot reboot recovery
```
⚠️ **TWRP 必须匹配 ROM 的原厂内核版本**（5.10.66 / .81 / .101）。错配**会硬砖**。
原文（XDA，措辞极重）：*"Flashing twrp.wbs.66 in A13 roms leads to guaranteed hard brick (hard brick = can't boot into system, and recovery and fastboot partitions are inaccessible too. Need to visit service centres for fix.)"*
https://forum.xda-developers.com/posts/88854091/ 🟡

### 4.3 BROM 模式（mtkclient）

**先说结论：MT6895 上 BROM 走不通，只能走 preloader 模式。** 🔵

mtkclient README 原文：
> **MT6781, MT6789, MT6855, MT6886, MT6895, MT6983, MT8985**
> These chipsets use a new protocol called V6 and the bootrom is patched. You need to use the `--loader` option and a proper loader from the `Loaders/V6` directory. **Bootrom won't work, you need to use preloader mode** (no hw buttons pressed, just connect).
> On some devices, preloader is deactivated, but you can reactivate it by running `adb reboot edl`.

https://github.com/bkerler/mtkclient 🔵

**进 preloader 模式**：关机 → **不按任何键** → 插 USB。
（mtkclient issue #164 原文：*"For brom mode, press and hold vol up, vol dwn, or all hw buttons and connect usb. For preloader mode, don't press any hw button and connect usb."* https://github.com/bkerler/mtkclient/issues/164 🟡）

**mtkclient 能做什么**（README-USAGE.md）🔵：
```bash
python mtk.py --loader Loaders/V6/<your_loader>.bin printgpt    # 读分区表
python mtk.py --loader ... r boot_a boot_a.bin                  # 读分区（只读，安全）
python mtk.py --loader ... w boot_a new.img                     # 写分区（危险）
python mtk.py --loader ... e metadata,userdata,md_udc           # 擦
python mtk.py --loader ... da seccfg unlock                     # 改解锁状态（极危险）
python mtk.py --loader ... reset                                # 退出
```

**风险（逐条）**：
| 操作 | 风险等级 | 说明 |
|---|---|---|
| `r`（read / dump） | 🟢 低 | **只读，不改任何东西**。这是取证该用的 |
| `printgpt` | 🟢 低 | 读 GPT |
| `w boot_a` | 🟡 中 | 和 fastboot 刷一样，但绕过 LK 的 AVB |
| `w lk_a` / `w preloader` | 🔴 高 | 写坏 = 没有 fastboot |
| `e`（erase） | 🔴 高 | 擦错分区不可恢复 |
| `da seccfg unlock` | 🔴🔴 极高 | 改解锁状态。**写坏 seccfg = 变砖** |
| `--stock` | 🟡 | 用官方 DA 而非 exploit，需要 auth 文件 |

⚠️ **xaga 是 DAA enabled**，用原厂 preloader 时 mtkclient 大概率报 `DAA_SIG_VERIFY_FAILED (0x7024)` 🟡（本项目 `xaga-facts.md` §5.5 记录，XDA 上多位 POCO X4 GT 用户印证）。
→ **这就是为什么要先刷工程 preloader**，而不是硬刚 mtkclient。

### 4.4 Preloader 模式（`0e8d:2000`）

**什么情况会停在这**（本机实测 + 推断）：
1. **写错 SPMI 参数导致 SoC 复位** —— 本机实测，`docs/headset-wip/DANGER-spmi-access.md` 记录 `0e8d:2000` 出现，**20–30 秒后自动重启回 Android** 🟢
2. **preloader 主动暴露下载口**（每次启动都会开一个很短的窗口，等主机握手）
3. **boot 分区校验失败后 LK 复位**，preloader 重跑
4. **USB 线插着 + 完全关机**（不按键）

**怎么退出**：
- **等**：preloader 有自己的超时（本机实测 20–30 s 自动重启）🟢
- **长按电源 8–10 秒**：强制断电（ItsVixano wiki 的救砖步骤里就是这个手法）🟡
- **`python mtk.py reset`**（如果 mtkclient 已经接上）
- **拔线 + 长按电源**

⚠️ **不要靠短接 test point**。xaga 有完整 preloader 分区，软件可达；短接是老机型（preloader 损坏）的手段，xaga 的 test point 图纸没有公开资料 ⚪

### 4.5 变砖的几种程度及对应救援

| 程度 | 症状 | 救援 | 成功率 |
|---|---|---|---|
| **0. 软故障** | 黑屏但 `fastboot devices` 有 | `fastboot flash boot_a <stock>`；或先 `oem lkmsg` / `oem expdb` 取证 | ~100% |
| **1. 软砖（boot 坏）** | 卡第一屏 / 反复重启，fastboot 还在 | 重刷 `boot_a` + `vendor_boot_a` + `dtbo_a`；必要时 `--disable-verity --disable-verification flash vbmeta` | ~100% |
| **2. 软砖（LK 坏）** | **fastboot 进不去**，但有 preloader `0e8d:2000` | **必须靠工程 preloader + SP Flash Tool V6 `Download Only`** | 高（前提：已刷 ENG preloader） |
| **3. 软砖（preloader 坏）** | 连 `0e8d:2000` 都不出，只有 BROM `0e8d:0003` | MT6895 的 BROM **已打补丁**，mtkclient 官方说不支持 → **只能售后** | 低 |
| **4. 硬砖** | 完全无 USB 枚举，插上没反应 | 售后 / 换主板 | ≈0 |

**关键前置保险（务必照做）**：
```bash
fastboot flash preloader1 preloader_xaga.bin
fastboot flash preloader2 preloader_xaga.bin
```
（ItsVixano wiki，MIUI 用 `preloader_xaga.bin`，HyperOS 用 `preloader_aristotle.bin`）🟡
→ **没刷工程 preloader 就变砖 → 只能去授权的小米售后**（wiki 原文语义）。这是整个救援体系的地基。

**有 ENG preloader 之后的救砖流程**（ItsVixano wiki，逐步）🟡：
1. 下载对应版本的 **Fastboot ROM**（`xmfirmwareupdater.com/miui/xaga/` 或 `/hyperos/xaga/`）
2. 下载定制 `xaga_MT6895_Android_scatter.xml`（本项目已有 `work-xaga/xaga_scatter.xml`）🟢
3. 解压 ROM，用定制 scatter **替换** `images/MT6895_Android_scatter.xml`
4. 关机
5. 开 **SP Flash Tool V6**（https://spflashtools.com/v6）
6. `Download XML` → 选 ROM 里的 `images/download_agent/flash.xml`
7. **确认模式 = `Download Only`** ⚠️⚠️⚠️
8. 点 `Download`，然后插 USB（不识别就长按电源 8–10 秒）

### 4.6 scatter 文件格式

**来源：本地 `xaga_scatter.xml`（182 条分区）** 🟢，由 ItsVixano 基于 `xaga_global_images_OS1.0.2.0.ULOMIXM` 官方 scatter 改出。

结构：
```xml
<root>
  <general name="MTK_PLATFORM_CFG">
    <config_version name="V2.2.0">
      <platform>MT6895</platform>       <!-- SoC -->
      <project>xaga</project>           <!-- 机型 -->
    </config_version>
  </general>
  <storage_type name="EMMC">            <!-- ⚠️ 名字是 EMMC，实际是 UFS -->
    <general name="MTK_STORAGE_CFG">...</general>
    <partition_index name="SYS0">
      <partition_name>preloader</partition_name>
      <file_name>preloader_xaga.bin</file_name>
      <is_download>false</is_download>       <!-- 是否默认勾选 -->
      <type>SV5_BL_BIN</type>                <!-- SV5_BL_BIN / NORMAL_ROM / EXT4_IMG -->
      <linear_start_addr>0x0</linear_start_addr>
      <physical_start_addr>0x0</physical_start_addr>
      <partition_size>0x100000</partition_size>
      <region>EMMC_BOOT1</region>            <!-- EMMC_BOOT1/2, EMMC_USER, UFS_LU0/1/2 -->
      <storage>HW_STORAGE_EMMC</storage>
      <boundary_check>true</boundary_check>
      <operation_type>BOOTLOADERS</operation_type>
      <!-- BOOTLOADERS / UPDATE / INVISIBLE / PROTECTED / BINREGION / NEEDRESIZE / RESERVED -->
      <is_upgradable>true</is_upgradable>
      <empty_boot_needed>false</empty_boot_needed>
      <combo_partsize_check>false</combo_partsize_check>
      <reserve>0x00</reserve>
    </partition_index>
    ...
  </storage_type>
</root>
```

**这份 scatter 的两个关键改动**（文件头注释原文）🟢：
> - Disables the preloader flash option by default
> - Flash on both {_a/_b} slots

→ **`preloader` 的 `is_download` 被改成 `false`**。也就是说，用这份 scatter 救砖**不会**顺手把你的 preloader 也刷了（防止刷错版本），但也意味着**它救不了 preloader 坏掉的情况**。

**`operation_type` 语义**：

| 值 | 含义 |
|---|---|
| `BOOTLOADERS` | bootloader 类，SP Flash Tool 会特殊处理（先下再校验再跳） |
| `UPDATE` | 普通可更新分区 |
| `INVISIBLE` | 不显示在 UI 列表里（多数是 `_b` 槽和内部区） |
| `PROTECTED` | 受保护，擦除/写入前要额外确认 |
| `BINREGION` | 裸二进制区（如 `nvram`） |
| `NEEDRESIZE` | 首次开机按实际容量 resize（`userdata`） |
| `RESERVED` | 保留 |

⚠️ **`storage_type name="EMMC"` 有误导性**。xaga 是 **UFS 3.1**，scatter 里第二段（`UFS_LU0/1/2`）才是真实布局。`sdc` 这个块设备名也是 UFS 的 LUN 2。🟢

---

## 5. ★ 刷 boot_a 后黑屏的全部可能原因（逐条机制）

> 这一节是重点。每条都给出**机制**（为什么会黑）、**在 xaga 上成不成立**、**怎么证伪**。

### 5.0 先分清"黑屏"的三种形态

| 形态 | 现象 | 说明什么 |
|---|---|---|
| **A. LK 就死了** | 刷完连 logo 都没有，几秒后自己重启 / 或完全静默 | 问题在 boot 头 / 解压 / AVB，**没进 kernel** |
| **B. LK 活了，kernel 极早期死** | logo 闪一下 → 黑屏 → 停住或循环重启 | 进了 kernel，但第一屏之前就挂 |
| **C. kernel 跑了一段才死** | 有过任何画面 / 背光变化 / 振动 | 更晚的问题（本项目历史上能跑进 iOS，所以 C 是常态） |

**用户观察原文**（`docs/headset-wip/ROUND-20-VERDICT.md` §2）🟢：
> 「输入到 jumping into xnu 就闪一下黑屏」
> 「之前修 GPU 的时候都能进 iOS」

**这个描述指向形态 C/B 之间**：loader 自己打完了 "jumping into xnu" 才黑，说明 **LK 那一段是通的**（boot 头对、解压成功、跳转成功）。**所以 §5.1–5.4 的格式/AVB 问题大概率不是当前这四次黑屏的原因**，但仍是必须排除的地基。

---

### 5.1 boot image 格式不对 / 超限 / dtb 不匹配

#### 5.1.1 格式不对

**机制**：LK 的 `load_bootinfo_from_hdr()` 读 header。magic 不是 `ANDROID!` → 打 `%s:%d: invalid boot image header`，然后 `load_vfy_boot()` 返回负值 → `boot_linux_from_storage()` 里的 `PAL_ASSERT(ret >= 0)` 触发 → panic 🟢

**在 xaga 上成不成立**：**我们的镜像这一层是对的** 🟢
```
release/hakios-xaga-boot_a.img: magic=ANDROID! hv=4 hs=1584 ks=21027384 rs=1867873 sig=4096
```
与 stock 逐字段一致。✅ 排除。

⚠️ **但有一个真实风险**：`chainboot.py` 的 `parse_v34()` 断言
```python
want = 1584 if hver == 4 else 1580
assert hsize == want
```
如果将来换一个 header_size 不是 1584 的 stock 镜像（有些 MTK 机型的 v4 header 是 1584+padding），会直接 `AssertionError` 退出，**打不出镜像**。这是个脆点，不是当前黑屏原因。

#### 5.1.2 超限（★ 这条要认真看）

**机制 A：boot 分区超限**
`chainboot.py` 有检查：`if len(img) > boot_partition: sys.exit(1)`，`BOOT_PARTITION = 32 << 20`（32 MiB）默认。
⚠️ **但 xaga 的 boot_a 是 64 MiB，32 MiB 的默认值会误报**。本项目 README 已经写「`chainboot.py` 里的 `BOOT_PARTITION` 要放宽」。当前 release 22.9 MB < 32 MB，没触发。
→ ✅ 不是当前原因，但是个定时炸弹。

**机制 B：LK 的解压输出缓冲**
LK 把 kernel 解压到 `0x40000000`。源码注释（旧平台）说预算 `0x1C00000`（28 MB）🟢。
我们的镜像解压后 **61,645,720 B（58.8 MiB）**，stock 是 46,851,620 B（44.7 MiB）。

| 镜像 | gzip | **解压后** | vs stock |
|---|---|---|---|
| stock `boot_a` | 15,311,555 | 42,201,364 (40.2 MiB) | — |
| stock `boot.img`（ROM 里） | 19,630,219 | 46,851,620 (44.7 MiB) | 基准 |
| **我们 release** | 21,027,384 | **61,645,720 (58.8 MiB)** | **+31%** |
| 我们 11-14 最后一次 | 21,101,060 | 61,944,024 (59.1 MiB) | +32% |

**为什么多了 14 MB**：`chainboot.py` 的做法是 `blob = prog + pad(0x40000) + image + payload`，然后整个 gzip。
- `prog`（mtkloader 程序本身）很小
- `image` = 原 stock Image 解压后 42,201,364 B
- `payload`（XNUP，含 XNU 内核 + ramdisk）= 19,182,200 B

→ **`42.2 MB（原 Image 明文）+ 19.2 MB（payload，已压缩过）≈ 58.8 MB`**，全都塞进 boot_a 的 kernel 段。

**风险判定**：⚪ **未定论，但这是最值得优先验证的一条。**
- 反面证据：11-01 到 11-14 的所有历史镜像解压后都在 61–62 MB 区间，**而 11-13 的 692 那轮是成功进 iOS 的** 🟢（`xnu-*.img` 全部 `dec≈61.9 MB`）→ 说明 62 MB 在 LK 的工作范围内。
- 正面证据：`KERNEL_DECOMPRESS_SIZE` 的具体值没从 `lk_a.img` 里定死（⚪）。如果它是 64 MB，我们**只剩 2 MB 余量**；哪天 payload 涨 3 MB 就会踩线，而且**踩线的表现就是 `panic("decompress kernel image fail!!!")` = 黑屏 + 复位**，与用户观察吻合。

**怎么证伪**：
```bash
# 1. 把 release 的 kernel 段解出来，看 gzip 尾部 ISIZE
python3 -c "
import struct,zlib
d=open('hakios-xaga-boot_a.img','rb').read()
ks=struct.unpack('<I',d[8:12])[0]; gz=d[0x1000:0x1000+ks]
print('ISIZE =',struct.unpack('<I',gz[-4:])[0])
print('real  =',len(zlib.decompress(gz,16+zlib.MAX_WBITS)))
"
# 2. 从 lk_a.img 反汇编里找 decompress_kernel 的 outlen 参数
#    （本次尝试未定死，见 §8 未完成项）
```

#### 5.1.3 dtb 不匹配（★ 这条与"改 vendor_boot 会崩 LK"直接相关）

**机制**：LK 的 DTB 处理是**三段式**：
```
1. platform_load_device_tree()  → 从 lk_a 里的 lk_main_dtb 加载，初始化 LK 自己（PMIC/按键/显示/SPMI）
2. 从 vendor_boot 读 dtb 段       → 这是给 kernel 的
3. ufdt_apply_overlay(dtbo)      → 把 dtbo 的 fragment 叠上去
```
🟢 依据：`lk_main_dtb` 与 `vendor_boot.dtb` **md5 完全相同**（`c1ab86afa6d57a9ff97d81dfdbf73232`），说明是同一份源文件的两个拷贝；LK 字符串里有 `lk_main_dtb` / `dtbo` / `dtbo_size=%u` / `load_dtbo fail, fdt size(%d)` / `DT overlay fail (%d)` / `ufdt_apply_overlay() failed!`

**"改 dtbo 会崩 LK"这个传闻的真实性**：
- ✅ **部分为真**：`dtbo` 里的 `fragment@0` 注入 `bootargs_ext = "console=tty0 root=/dev/ram androidboot.hardware=mt6895"`。改坏它 → 内核丢 `console=tty0`（**黑屏且无输出**）或丢 `androidboot.hardware=mt6895`（init 找不到硬件配置 → 起不来）
- ✅ **部分为真**：`ufdt_apply_overlay()` 失败会 `panic`。如果 dtbo 的 `target` phandle 对不上（比如你换了 vendor_boot 的 dtb 但没换 dtbo），overlay 找不到目标节点 → panic
- ❌ **"改 vendor_boot 的 dtb 会崩 LK 自己"是不准确的**：LK 用的是自己那份 `lk_main_dtb`，与 vendor_boot 无关。**但**——如果 vendor_boot 的 dtb 段格式错（不是合法 FDT / DTBO），LK 在 `fdt_check_header` 那一步就会挂，表现和"崩 LK"一样

**本项目现状**：`vendor_boot_a` 和 `dtbo_a` **从没被改过**（stock md5 `vendor_boot_a_stock.img` 在 `backup/` 里，11-04 之后没重打过）🟢
→ ✅ **排除当前黑屏原因**。⚠️ 但这是绝对不能碰的红线（见 §6）。

---

### 5.2 AVB / dm-verity 校验失败（★ 这条要分清两层）

#### 5.2.1 硬事实：我们的 boot_a 确实通不过 AVB

本地实测 🟢：
```
$ python3 avbtool.py info_image --image dump/boot_a.img
Hash descriptor:
  Image Size:    21020672
  Partition Name: boot
  Salt:          72e17ae8ee16d5896dd020d6d5f52e252b22dede3a883e4483131b5c326ccc2a
  Digest:        e4ab2ceb824e0ed524a9350a700793b1f64578336e90ca4df83e4fcb802fd499
```
```python
# 用同样的 salt 重算
sha256(salt || rom/boot.img[:21020672]) = e4ab2ceb...  ✅ match
sha256(salt || dump/boot_a.img[:21020672]) = 9c39ad3e...  ❌ mismatch
```
→ **`dump/boot_a.img` 的内核别人改过**（是 Magisk 打过补丁的版本），**AVB 摘要已经对不上**。
而我们四次刷机的镜像，摘要只会更不对（整段 kernel 都换了）。

#### 5.2.2 机制：LK 怎么处理 mismatch

LK 字符串里是完整的 **libavb** 错误码表 🟢：
```
: Error verifying vbmeta image: 
: Hash of data does not match digest in descriptor.        ← 我们的情况
: Public key used to sign data does not match key in chain partition descriptor.
: Public key used to sign data rejected.
[AVB] avb_ret = %d
[AVB] img_auth_required = %d
```
以及 MTK 自己的 `app/mt_boot/avb/load_vfy_boot.c`（字符串确认存在）。

**关键未知**：LK 拿到 `AVB_HASH_DIGEST_MISMATCH` 之后是**继续启动**还是**拒绝启动**？
- `load_vfy_boot.c` 的 AOSP 参考实现里，`avb_slot_verify()` 的返回值有 `AVB_SLOT_VERIFY_RESULT_ERROR_VERIFICATION`，上层通常按 `allow_verification_error` 标志决定
- MTK 版本里有 `[AVB] Fail To Get Verification Disabled Bit` / `avb.managed_verity_mode`，说明它有"verification disabled"的旁路
- 本机现象（**决定性证据**）：**11-13 的 692 那轮，同一个改了 kernel 的 boot_a 成功进了 iOS** 🟢
  → **AVB 校验要么被绕过，要么失败也不 block**

**结论**：✅ **当前状态下 AVB 不是黑屏原因**。但如果哪天 `vbmeta` 被刷成 strict 版本（比如 `fastboot --disable-verification` 用错底图），就会变成原因。

**⚠️ 一条容易踩的坑**：`dk` 的 `vbmeta` flags 实测是 **0**（没禁 verity 也没禁 verification）🟢。而 `vbmeta` 是 `BOARD_AVB_ENABLE := true` + `--flags 3` 生成的（TWRP tree 的参数）。**stock 的 vbmeta flags=0 说明原厂没禁校验**，我们的镜像能起说明 LK 实际上没在 enforcing。这个结论要记牢：**xaga 的 LK 在 unlocked 状态下大概率不 enforced AVB**。

#### 5.2.3 dm-verity

**机制**：`dm-verity` 保护的是 **system/vendor 等只读分区**，不是 boot。`boot` 的完整性归 AVB 管。
LK 字符串里有 `dm-verity corruption` / `Your device is corrupt. It can't be trusted and may not work properly. Press power button to continue.` / `set_dm_verity_state` 🟢
→ **如果 dm-verity 失败，屏幕上会有明确的 "Your device is corrupt" 提示**，不是纯黑屏。

**结论**：✅ 排除。我们不碰 system。

---

### 5.3 A/B slot 相关

#### 5.3.1 机制

xaga 是**虚拟 A/B**（`ENABLE_VIRTUAL_AB := true`）🟢。`boot_a` / `boot_b` 是**物理两份**，`system` 等是 `super` 里的逻辑一份 + 快照。

LK 怎么选槽：
```
读 misc 分区的 boot_control 结构（magic + slot 信息）
  → BOOTCTRL_MAGIC 不匹配 → "[LK_AB] boot_ctrl magic number is not match %x , BOOTCTRL_MAGIC = %x"
  → 读失败 → "[LK_AB] read boot_control fail"
  → 都失败 → 回落到 slot a
```
🟢 依据：`lk_a.img` 字符串 `[LK_AB] boot_ctrl magic number is not match` / `[LK_AB] open misc fail` / `[LK_AB] read boot_control fail` / `set slot a failed!` / `set slot b failed!` / `check_suffix_with_slot` / `unknown slot suffix`

**slot 状态也会经 cmdline 传出**：`androidboot.slot_suffix=%s` 🟢

#### 5.3.2 在 xaga 上成不成立

**风险点：只刷了 `boot_a`，但机器启动的是 `boot_b`。**
→ 表现：**刷完毫无变化**（还是原来那个系统），不是黑屏。⚠️ 但如果 `boot_b` 是坏的（没有效镜像），就会黑屏 + 回落。

**怎么确认**：
```bash
fastboot getvar current-slot      # → a 或 b
fastboot getvar slot-successful:a # → yes/no
fastboot getvar slot-unbootable:a
fastboot getvar slot-retry-count:a
```
🟢 这些 `getvar` 项在 LK 命令表里**全部存在**（`publish_attributes` 函数里）。

**`misc` 分区是 slot 状态的载体**（4 MiB，`sdc1`）。⚠️ **擦 misc = 清掉 slot 记录 = LK 回落到 slot a**，这既可能是救援手段也可能是变砖原因。

**结论**：⚠️ **不能排除，但可能性低**。如果 `current-slot=b` 而我们只刷了 `boot_a`，那四次刷机**根本没生效**——这能解释"四次全黑屏且行为一致"。**这条必须验。**（一条命令的事）

---

### 5.4 LK 自己的校验

**LK 有两套校验，别混为一谈**：

| 校验 | 对象 | 失败表现 | 我们会不会中 |
|---|---|---|---|
| **MTK sec_img_auth**（`sec_img_auth()` / `cert chain vfy`） | `lk` / `preloader` / `tee` / `gz` 等 **MTK 自有分区** | `[%s] cert chain vfy fail (0x%x)` + `ASSERT` | ❌ 不涉及（我们只改 boot_a） |
| **AVB（libavb）** | `boot` / `vendor_boot` / `dtbo` / `system` 等 | `[AVB] avb_ret = %d`，按 flag 决定是否 block | ✅ 涉及，但实测不 block（见 §5.2） |
| **fastboot 侧保护** | 任何 `erase`/`flash` | `Flashing is not allowed in Lock state` / `Flashing is not allowed for Protected Partitions` / `partition is not writable` | ❌ 已解锁，且 boot_a 不是 protected |

🟢 依据：`lk_a.img` 字符串 `sec_img_auth` / `sec_cert_parser_and_vfy` / `sec_get_vfy_policy` / `sec_cfg_get_lock_state` / `seccfg lock_state = %d` / `mi lock_state = %d` / `[%s] Major SECCFG_MAGIC_CORRECT` / `Flashing is not allowed in Lock state`

**还有一个隐蔽的校验：kernel 加载地址对齐**
```c
if ((uint32_t)kernel_target_addr & 0x7FFFF) panic("64 bit kernel can't boot at 0x%08x\n");
```
→ 要求 kernel 目标地址 **512 KiB 对齐**。`0x40000000` 满足 ✅。但如果谁改了 `BOARD_KERNEL_BASE`，这条会 panic。

**结论**：✅ 排除。我们的 boot_a 不触发 MTK 的 cert 校验。

---

### 5.5 电量不足

**机制**：LK 起来后第一件事之一是 `mtk_charger_init()`。如果检测不到充电器 / 电量低于阈值，会走 `KERNEL_POWER_OFF_CHARGING_BOOT` 分支，**不加载 kernel，只显示充电图标**。
LK 字符串 🟢：
```
mtk_charger_init        mtk_charger_kpoc_check
[XMCHG_LK] no charger, power off
charger_init            charger_enable_charging
```
→ **低电量 + 插 USB，表现是充电图标，不是黑屏。** 但**电量极低 + 不插线**，可能起来就掉电复位，看起来像黑屏循环。

⚠️ **这条与"刷机前要 50% 以上电量"的通用建议对应**。

**结论**：⚠️ **不能完全排除**，但表现不符（用户说是"jumping into xnu 就闪一下黑屏"，说明 LK 跑完了）。⚪ 建议下次刷机前确认电量 > 50%，作为排除项。

---

### 5.6 ★ 内核本身在极早期就挂（LK 会怎样）

**这是最可能的真实原因，而且机制最难受。**

#### 5.6.1 LK 跳 kernel 之后完全放手

```c
boot_linux((void *)kernel_target_addr, (unsigned *)tags_target_addr,
           board_machtype(), (void *)ramdisk_target_addr, ramdisk_real_sz);
return 0;   // 实际上 boot_linux 是 noreturn
```
—— `app/mt_boot/mt_boot.c:1706-1712` 🟢

LK **没有任何**"内核起来了吗"的检测：
- 不读返回值（也没有返回值）
- 不设"内核已启动"标志
- 不看 kernel 是否写回了 `boot_args`

#### 5.6.2 内核死了之后，谁来收拾

**只有一个东西：AP WDT（`mediatek,mt6895-wdt` @ `0x1c007000`）** 🟢

链路：
```
kernel 死（不管死在哪）
  → 没喂狗
  → AP WDT 超时
  → SoC 硬复位
  → BootROM → Preloader → ... → LK 重来
  → LK 读 misc 看 slot 状态 / 读 expdb 看有没有崩溃
```

**关键**：**WDT 复位之前，内核有没有机会留下东西？**
- MTK 的 AEE（跑在 LK 的 `aee` 子分区里）支持 `AEE_REBOOT_MODE_WDT` / `KERNEL_PANIC` / `KERNEL_OOPS` / `HANG_DETECT` 四种崩溃类型，会做 **full dump 到 expdb** 🟡
  出处：https://deepwiki.com/svoboda18/lk/4-exception-handling-system-(aee)
  本地 `lk_a.img` 也印证了：`kedump: found content in expdb` / `kedump: write err %ld` / `panic_header.exp_type = 0x%x` / `panic_header.wdt_status = 0x%x` / `kedump: last is full pmic reset!` 🟢
- **但**：这套机制依赖内核在死之前**已经初始化了 MTK 的 pstore/ram_console 通道**。XNU 完全没有这些 —— 我们是裸机 XNU，没有 `ram_console`，没有 `aee_debug_kinfo`，没有 `pstore` 🟢（依据：`BLACKSCREEN-forensics.md` 与 `docs/DEBUGGING.md` 都记录了 "pstore 一直是空的"）
- **所以**：XNU 死了 → expdb 里**什么都不会有** → 下一轮上去看，expdb 是空的 → 看起来"什么都没发生"

#### 5.6.3 我们的 loader 能看到什么

现有机制（`docs/DEBUGGING.md`）🟢：
| 机制 | 触发条件 | 覆盖到哪 |
|---|---|---|
| `logbook`（`xnusys` 尾部 8 MiB） | loader 跳 XNU 前写 boot note；XNU 交回控制权后写正文 | **只覆盖"跳转前"和"正常交回"两个时刻** |
| `XNU_WATCH_SECONDS` 看门狗快照 | XNU 死了，第二核 120 s 后写 DRAM `0x90000100` | **要下一轮 loader 起来才有机会落盘** |
| 屏幕分页大字 | 同上 | 只有人为盯着才拍到 |

**已知的三个 bug**（`BLACKSCREEN-forensics.md` §0）🔴：
1. `XNU_WATCH_HEARTBEAT` 默认 1 → watcher 120 s 后 `for(;;) wfe`，**不复位、不快照**
2. `XNU_WDT_SECONDS=0` → `marker_set(0)` → `if (mark == MARK_RUNNING) rescue_log()` **永远进不去**
3. `rescue_log()` 只调 `save_log()` → `plog_putc()`，而 xaga 是 `BOARD_NO_PLOG`，`plog_putc()` 是空函数

**⚠️ 时序冲突（本项目已知）**：`loader_return_after=150 > XNU_WATCH_SECONDS=120`，watcher 永远先触发 🟢（`ROUND-20-VERDICT.md` §5）

#### 5.6.4 XNU 极早期可能的死法（按可能性）

| 死法 | 机制 | 本项目的迹象 |
|---|---|---|
| **`parse_payload()` 读到垃圾** | LK 忽略 `text_offset`，把程序放 `0x40000000`；如果链接脚本按 `0x40080000` 写，所有 `adrp` 整体偏 512 KiB | 本项目**已经踩过并修了**（README「引导链」一节）🟢 |
| **`chain_android()` 自我覆盖** | 源 `0x40040000`，目标 `0x40080000`，正向逐 8 字节拷贝会自我覆盖 | 已修（改双向拷贝）🟢 |
| **GPU 上电卡死** | `BOARD_MALI_POWER=1` 时 loader 进 XNU 前给 12 个域上电，`mali_aoc_up()` 里挂 | 第 1 次刷机 logbook `code=0x13`（`probe_flash(19u)`），**确认死在这里** 🟢 |
| **XNU 自己的 EARLY_BOOT 挂** | 无任何输出 | 第 2 次 `code=0xfeedface` = loader 成功跳转，XNU 死 🟢 |
| **payload 里的 XNU 二进制本身坏了** | 编译期问题 | 第 4 轮移除了耳机驱动、参数逐字复刻 692，**仍黑屏** 🟢 |

#### 5.6.5 这条为什么最可能

因为：
1. **LK 段是通的**（用户看到 "jumping into xnu" 才黑）
2. **AVB 不 block**（692 那轮同样改过 kernel，成功进 iOS）
3. **vendor_boot / dtbo 没动**
4. **四次刷机行为一致**（不是随机故障）
5. **第 4 轮已经排除了耳机驱动和启动参数**（`ROUND-20-VERDICT.md` §4-5）
6. 剩下的差异只有：**内核二进制本身**（今天多次重建）+ **49 个未提交改动**跨 GPU/loader/UFS/input/audio

→ **最可能是 XNU 在 `EARLY_BOOT` 之后、第一次能输出之前就挂了**，而我们的日志机制在这个窗口里**全线失效**（三个 bug + 时序冲突）。

#### 5.6.6 ★★ 一条刚拿到的实锤：692 镜像**今天真的起来了**

调研过程中在本机发现一份 **2026-11-14 22:13 的实机运行记录**，用的是 11-13 那版 `trial-692-boot-a.img`（22,978,560 B）。原文在
`/home/lizi/IOS/hakios-mtk/logs/headset-resume-20261114-220244/` 🟢：

| 文件 | 内容 |
|---|---|
| `baseline692-slot.txt` | `current-slot: a` ← **槽位是 a，我们刷的就是 a。§5.3 的疑虑排除** |
| `baseline692-flash.log` | `Sending 'boot_a' (22440 KB) OKAY` / `Writing 'boot_a' OKAY` |
| `baseline692-result.json` | `reason: 0x26`、`code: 0x0`、`header_checksum_ok: true`、**`changed_pages: 116`**、**`timestamp_rows: 1364`**、`trial692_marker: true` |
| `baseline692-window.txt` | 4 MiB logbook 正文，**3,955 行真实内核/用户态日志** |

里面能读到的里程碑（`/tmp/b692.txt` 的 grep 结果）🟢：
```
line 167  boot-args = rd=md0 -v cpus=4 ... mali_trial=692 loader_return_after=150 hakios_kver=2.1.13-hakios-darwin22.5-mt6895-gdb08f63c-dirty
line 202  com.apple.xpc.launchd (boot) <Notice>: Early boot complete. Continuing system boot.
line 233  hakios: window server hooks installed
line 244  hakios: SpringBoard: no GPU draws, vibrant views are plain ...
line 1488 backboardd[10:343] [Display] Main will unblank
line 1491 backboardd[10:343] [Display] Set Blanked: NO
line 2584 --- loader_return_after expired ---
line 2586 psci: cpu 1 ends the system: the return to the boot loader is the boot processor's to make
line 2588 --- kernel message buffer ---
line 2590 [   23.818072]: memorystatus_update(enter): pid 24, ...
```
**`SpringBoard` 出现 677 次、`launchd` 1835 次、`panic` 0 次。**

→ **结论：`boot_a` 刷 692 镜像后，XNU 完整跑到 `Main will unblank`（屏幕点亮）→ SpringBoard 起来 → 150 s 后 `loader_return_after` 正常到期交回 Android。**
→ **这直接证明：§5.1 格式、§5.2 AVB、§5.3 槽位、§5.4 LK 校验、§5.6.1/2 全都不是阻塞项。**
→ **四次黑屏是「代码回归」，不是「boot 链/格式/AVB 的问题」。**

**⚠️ 但注意一个反直觉的点**：`code: 0x0` + `reason: 0x26` + `changed_pages: 116`，说明这次 logbook 是**正常写入**的（`0xfeedface` 的 boot note 之外还有 1364 行正文）。而第 2/3/4 轮的 logbook 只有 396 字节。**差异在"XNU 有没有活着走到交回控制权"** —— 这反过来证明 §5.6.4 的三个日志 bug 只在"XNU 挂了"时才致命，"XNU 正常跑完"时日志路径是通的。

**→ 所以真正要修的不是日志机制，而是找到那 49 个未提交改动里导致 XNU 极早期挂掉的那个。**
（同一目录还有一份 23:11 的新构建记录 `resume-sol-20261114-231152/`：`flashed: false`，`image_md5 9d8f6ba2…`，`loader_flags` 里多了 `mali_nopdc=1`，`XNU_WATCH_SECONDS=0`。**还没刷**，是下一轮的候选。）

---

### 5.7 逐条汇总表

| # | 原因 | 机制 | xaga 上成不成立 | 判定 |
|---|---|---|---|---|
| 1 | boot image 格式不对 | LK `invalid boot image header` → `PAL_ASSERT` → panic | 我们的镜像 hv=4/hs=1584/magic 正确 | ❌ 排除 |
| 2 | boot 分区超限 | `chainboot.py` 检查 32 MiB（默认值，实际 64 MiB） | 22.9 MB < 32 MB，没触发 | ❌ 排除（但是定时炸弹） |
| 3 | **kernel 解压超 LK 缓冲** | `panic("decompress kernel image fail!!!")` | 58.8 MiB，比 stock 多 31%；上限未定死 | ⚠️ **待验证，优先级高** |
| 4 | dtb 不匹配 | `DT overlay fail` → panic；丢 `bootargs_ext` | vendor_boot/dtbo 从没动过 | ❌ 排除 |
| 5 | **AVB 校验失败** | libavb → `ERROR_VERIFICATION` | 摘要实测 mismatch，但 **11-14 22:13 的 692 实测**（`reason 0x26` / `SpringBoard` 起来 / `code 0x0`）证明它不 block | ❌ **已实测排除** |
| 6 | dm-verity | 屏幕会显示 "Your device is corrupt" | 不碰 system | ❌ 排除 |
| 7 | **A/B slot 刷错槽** | 刷了 `boot_a` 但启动 `boot_b` | `baseline692-slot.txt` 实测 `current-slot: a` 🟢 | ❌ **已实测排除** |
| 8 | LK 自己的 cert 校验 | `sec_img_auth` 对 MTK 自有分区 | 只改 boot_a，不涉及 | ❌ 排除 |
| 9 | 电量不足 | `[XMCHG_LK] no charger, power off` → 充电图标 | 表现不符（有 "jumping" 输出） | ⚠️ 低概率 |
| 10 | **内核极早期挂** | LK 放手不看；AP WDT 复位；无 expdb；日志机制三个 bug | **692 同参数能起 → 是代码回归** | ✅ **最可能** |
| 11 | **看门狗先于日志落盘** | `RETURN_AFTER=150 > WATCH=120` | 已确认；但 692 那轮 `RETURN_AFTER=150` 仍然正常交回了（日志 1364 行）→ **只在"XNU 挂了"时才致命** | ✅ 确认为日志丢失原因，**但不是黑屏原因** |

---

## 6. 对 HakiOS 的建议（哪些能试、哪些别碰）

> **⚠️ 先读这条**：§5.6.6 那份 11-14 22:13 的实测记录已经把 **§5.1 / §5.2 / §5.3 / §5.4 四条全排除了**。
> 所以下面 6.1 的「取证」动作**优先级下降** —— 不是为了找"为什么黑屏"，而是为了在**下一次黑屏时**有备无患。
> **当前该做的是 §6.5「查那 49 个未提交改动」**，不是继续折腾 boot 链。

### 6.1 🟢 立刻能做（零风险 / 只读）

| # | 动作 | 为什么 | 命令 |
|---|---|---|---|
| **1** | ~~确认槽位~~ | **已实测 `current-slot: a`**（`baseline692-slot.txt`）🟢 | ~~`fastboot getvar current-slot`~~ |
| **2** | **`fastboot oem lkmsg`** | 拿 LK 自己的 log_store。能回答"LK 走到哪一步" | 见下 |
| **3** | **`fastboot oem expdb`** | 拿 expdb（128 MiB）。AEE 可能在里面留了 WDT/panic 的 minirdump | 见下 |
| **4** | **`fastboot oem dump_pllk_log`** | preloader → LK 的日志。能回答"preloader 有没有报错" | |
| **5** | **`fastboot oem fbreason`** | 上次重启原因。`wdt_hw` = 看门狗复位，`kernel_panic` = 内核 panic | |
| **6** | **确认已刷工程 preloader** | 这是救援体系的地基。**没刷就动手 = 赌命** | `fastboot getvar version-bootloader` 对版本 |

**取证命令模板**（⚠️ **只在需要时执行，不要现在跑**）：
```bash
source ~/.dsh/env.sh
export S=<serial>
fastboot -s $S getvar current-slot
fastboot -s $S getvar slot-successful:a
fastboot -s $S oem lkmsg      > /tmp/lkmsg.txt      2>&1
fastboot -s $S oem dump_pllk_log > /tmp/pllk.txt    2>&1
fastboot -s $S oem fbreason   > /tmp/fbreason.txt   2>&1
fastboot -s $S oem expdb      > /tmp/expdb.bin      2>&1   # 可能很大（128 MiB 压缩后）
```
> ⚠️ `oem expdb` 是 LK 端 deflate 压缩后吐出来的（字符串 `-- Compress expdb offset %llx`），不是原始分区镜像，需要解压。⚪ 具体封装格式未确认。

### 6.2 🟡 可以试（低风险，需一次刷机）

| # | 动作 | 收益 | 风险 |
|---|---|---|---|
| **7** | **把 `XNU_RETURN_AFTER` 改到 30，`XNU_WATCH_SECONDS` 改到 60** | 让"正常交回"路径**先于**看门狗触发，日志能落盘 | 无（只改 loader 常量） |
| **8** | **修 `XNU_WATCH_HEARTBEAT` / `MARK_RUNNING` 三个 bug** | 让 watcher 真的快照 + 复位 | 无（`BLACKSCREEN-forensics.md` §方案 2/3 给了 patch） |
| **9** | **加"像素色带"探针** | 不依赖任何日志通道，纯 framebuffer。**已经有一版**（`docs/DEBUGGING.md` 末尾） | 无 |
| **10** | **逐段二分 payload** | 把 XNU payload 换成"只跑到 `EARLY_BOOT` 就 `wfe`"的桩，确认能不能过那一段 | 一次刷机 |
| **11** | **刷回 11-13 的 692 镜像** | 单变量确认"是代码改动还是环境" | 一次刷机，**已知能起** |

### 6.3 🔴 别碰（会变砖 / 毁证据）

| # | 动作 | 后果 |
|---|---|---|
| **A** | **改 `vendor_boot_a`** | 它的 ramdisk 是 42.7 MB PLATFORM 类型（每次启动必读），dtb 段是 DTBO 容器，`bootargs_ext` 在里面。重打极易丢东西 → 内核丢 `console=tty0` 或 `androidboot.hardware=mt6895` → 黑屏 |
| **B** | **改 `dtbo_a`** | overlay 的 `target` phandle 对不上就 `panic` |
| **C** | **改 `lk_a` / `preloader`** | 刷错 = 没有 fastboot = 软砖 |
| **D** | **擦 `misc`** | 清掉 slot 记录（可能有用，但会掩盖真实状态） |
| **E** | **擦 `seccfg` / `efuse` / `otp` / `dram_para`** | **硬砖 / 不可恢复** |
| **F** | **擦 `nvram` / `nvdata` / `nvcfg` / `persist` / `protect1/2`** | **丢 IMEI / MAC / 传感器校准，售后才能修** |
| **G** | **擦 `expdb`** | **毁掉唯一的 flash 侧崩溃日志**。这是取证命脉 |
| **H** | **SP Flash Tool 选 `Firmware Upgrade`** | **会把 BL 重新锁上** |
| **I** | **SP Flash Tool 选 `Format all + Download`** | **擦掉整片 UFS，含 IMEI** |
| **J** | **`fastboot boot`** | LK 根本没这个命令，别浪费时间 |
| **K** | **刷别的机型/别的内核版本的 TWRP 到 vendor_boot** | **保证硬砖**（XDA 原话） |

### 6.4 建议的下一步顺序

**⚠️ 已按 §5.6.6 的实锤重排**：槽位、AVB、格式、LK 校验都实测排除了，所以顺序变了。

```
① 看 11-14 22:13 那份成功的记录，把它当基线
   /home/lizi/IOS/hakios-mtk/logs/headset-resume-20261114-220244/
   关键：image md5、loader_sha256、payload_sha256、boot-args 全记下来

② 查「49 个未提交改动」里 11-13 之后动过的东西（§6.5）
   重点：mtk/loader/mtkloader.c、drivers/input/hakios.input.c、
        mtk/mt6895/drivers/gpu/hakios.mali610.c、userland/src/inject.c

③ 单变量回退：把 ② 里查出来的可疑改动逐个回退，一次只回一个，刷一次

④ 如果 ③ 太慢 → 直接刷 692 镜像确认机器还是好的（一次刷机）
   fastboot -s $S flash boot_a xnu-flashed-slot_a.img

⑤ 只有在 ④ 也失败时，才回头看 boot 链
   → fastboot oem fbreason / oem lkmsg / oem expdb
```

**核心判断（已更新）**：不是"先修好看这件事"，而是 **"先定位是哪次提交把 XNU 弄死的"**。
日志机制该修（§6.2 #7/#8），但它只影响"下次黑屏能不能看到"，不影响"为什么黑屏"。

### 6.5 ★ 真正该做的事：审那 49 个未提交改动

`resume-sol-20261114-231152/initial-status.txt` 里列了 `hakios-mtk` 工作区的全部未提交改动 🟢。
按"最可能弄死 XNU 极早期"排序：

| 优先级 | 文件 | 为什么可疑 |
|---|---|---|
| **1** | `mtk/loader/mtkloader.c`（M，261 KB） | loader 本身。跳 XNU 前的一切都在这里。**第 1 轮 `code=0x13` 就是它死在 `mali_aoc_up()`** |
| **2** | `mtk/mt6895/loader/board.h`（A，新增） | 所有板级常量。一个地址错就黑屏 |
| **3** | `mtk/loader/link.ld`（M）+ `mtk/loader/start.S`（M） | **链接地址错 = 所有 `adrp` 整体偏移**（本项目踩过，README 有记录） |
| **4** | `xnu/files/iokit/Kernel/MiniIOMobileFramebuffer.cpp`（M） | 显示。改坏 → 有输出但屏幕黑 |
| **5** | `drivers/gpu/hakios.mali610_fw.h`（A，1.3 MB）+ `mtk/mt6895/drivers/gpu/hakios.mali610.c`（A） | GPU。`BOARD_MALI_POWER=1` 时 loader 会动它 |
| **6** | `userland/src/inject.c`（M）/ `userland/src/init.c`（M） | 用户态注入。692 那轮能到 SpringBoard，说明这条链是通的 |
| **7** | `mtk/tools/chainboot.py`（M） | 打包逻辑。**如果它改了 `CHAIN_OFFSET` 或 padding**，整个 blob 布局就变了 |
| **8** | `mtk/mt6895/drivers/audio/hakios.mt6895_headset.c`（M） | 已知不是原因（第 4 轮移除了调用仍黑屏） |

**最快的单变量做法**：
```bash
cd /home/lizi/IOS/hakios-mtk
git stash list                      # 看有没有更早的 stash
git diff --stat                     # 确认 49 个改动的规模
# 重点看这几个的 diff：
git diff mtk/loader/link.ld mtk/loader/start.S
git diff mtk/tools/chainboot.py
```
⚠️ **不要 `git checkout .`** —— 49 个改动里有新增文件（`A`），checkout 会把它们留下但让 `M` 的文件回退，产生半新半旧的状态。**逐个 `git checkout -- <file>`**。

---

## 7. 引用 URL

### 官方文档（🔵）
- AOSP · Boot image header v3/v4 结构、v4 `signature_size`：
  https://source.android.com/docs/core/architecture/bootloader/boot-image-header
- AOSP · Vendor boot partitions（v3/v4 布局、2128 header、ramdisk table、bootconfig）：
  https://source.android.com/docs/core/architecture/partitions/vendor-boot-partitions
- AOSP · `bootimg.h` 源码（逐字段定义）：
  https://android.googlesource.com/platform/system/tools/mkbootimg/+/refs/heads/main/include/bootimg/bootimg.h
- AOSP · `mkbootimg.py` 源码：
  https://android.googlesource.com/platform/system/tools/mkbootimg/+/refs/heads/main/mkbootimg.py
- AOSP · Android Verified Boot (AVB) 概览：
  https://source.android.com/docs/security/features/verifiedboot/avb
- AOSP · `avbtool.py` 源码（本文用它解析 footer）：
  https://android.googlesource.com/platform/external/avb/+/refs/heads/main/avbtool.py
- AOSP · A/B 更新（slot bootable/successful 语义）：
  https://source.android.com/docs/core/ota/ab

### MTK LK 源码 / 逆向（🟢 一手）
- `svoboda18/lk` · `app/mt_boot/mt_boot.c`（`boot_linux_from_storage` / `boot_linux_fdt` / `boot_linux` / `decompress_kernel` / `mboot_allocate_bootimg_from_mblock` 全部在这）：
  https://github.com/svoboda18/lk/blob/master/app/mt_boot/mt_boot.c
- `svoboda18/lk` · AEE 异常处理系统（KEDump / MRDUMP / expdb 偏移 / 崩溃类型）：
  https://deepwiki.com/svoboda18/lk/4-exception-handling-system-(aee)
- `svoboda18/lk` · AEE Core and Detection：
  https://deepwiki.com/svoboda18/lk/4.1-aee-core-and-detection
- `mtk-lk-tools` · MTK LK 镜像结构、`0x58881688` 分区头、双证书链、签名细节：
  https://github.com/TheGammaSqueeze/mtk-lk-tools
- `mtkpartdump` · MTK 分区头 512 B 结构（magic / size / name / load_addr / ext）：
  https://github.com/jsoltan226/mtkpartdump
- R0rt1z2 · How to Reverse Engineer MediaTek Bootloaders（LK v1/v2、子分区列表、`lkpatcher`）：
  https://blog.r0rt1z2.com/posts/reverse-engineering-mediatek-lk/

### 工具 / 救援（🔵 / 🟡）
- mtkclient · README（**MT6895 = V6 + BROM patched，必须走 preloader 模式**）：
  https://github.com/bkerler/mtkclient
- mtkclient · README-USAGE（`--loader` / `printgpt` / `r` / `w` / `da seccfg unlock`）：
  https://github.com/bkerler/mtkclient/blob/main/README-USAGE.md
- mtkclient · `brom_config.py`（MT6895 的 `watchdog=0x1c007000` / `uart=0x11001000`）：
  https://github.com/bkerler/mtkclient/blob/main/mtkclient/config/brom_config.py
- mtkclient · `usb_ids.py`（`0e8d:0003` = BROM，`0e8d:2000` = Preloader）：
  https://github.com/bkerler/mtkclient/blob/main/mtkclient/config/usb_ids.py
- mtkclient · issue #164（BROM/preloader 按键组合原文）：
  https://github.com/bkerler/mtkclient/issues/164
- ItsVixano · **xaga 专属 preloader / 救砖 wiki**（工程 preloader、SP Flash Tool 步骤、三条禁忌）：
  https://wiki.itsvixano.me/device_specific/preloader_xaga
- postmarketOS wiki · xaga（`BOOT_PARTITION=/dev/sdc86`、`NVDATA_PARTITION=/dev/sdc13`、进 preloader 模式）：
  https://wiki.postmarketos.org/wiki/Xiaomi_Redmi_Note_11T_Pro_(%2B)_/_POCO_X4_GT_/_Redmi_K50i_(xiaomi-xaga)
- XDA · TWRP 版本错配导致硬砖的警告：
  https://forum.xda-developers.com/posts/88854091/
- XDA · `fastboot boot` 在 POCO X4 GT 上报 `unknown command`：
  https://xdaforums.com/t/failure-to-boot-into-custom-recovery.4757295/
- SP Flash Tool V6 下载：
  https://spflashtools.com/v6

### 二手参考（🟡）
- smdbunker · MTK SLA/DAA bypass 指南（按键组合、`mtk payload`）：
  https://smdbunker.com/?p=217
- androidmobiles.org · BROM + SLA/DA bypass 流程：
  https://androidmobiles.org?p=60069/
- techeia · MTK VCOM 驱动说明（preloader 握手窗口 2–3 s）：
  https://www.techeia.com/blog/download-mtk-usb-vcom-driver-windows-11
- HackTricks · MTK Secure Boot / `bl2_ext` / `sec_img_auth` 源码片段：
  https://hacktricks.wiki/jp/hardware-physical-access/firmware-analysis/android-mediatek-secure-boot-bl2_ext-bypass-el3.html
- CSDN · MTK 安全启动密钥体系（`sec_img_auth` / `img_auth_required` 源码原文）：
  https://blog.csdn.net/g_linuxer_/article/details/162864809
- CSDN · MTK LK 分析（`decompress_kernel` → `gunzip`，`0x1C00000` 注释原文）：
  https://blog.csdn.net/itom1900/article/details/125023048
- droidwin · Vendor Boot as Recovery 刷 TWRP：
  https://droidwin.com/how-to-flash-twrp-recovery-in-the-vendor-boot-partition

### 本项目内部资料（🟢 本地一手）
| 文件 | 提供了什么 |
|---|---|
| `work-xaga/dump/boot_a.img` | stock boot_a，md5 `a63b2e35a7c2522cb24a6a10332b9bc8`；header 逐字段、AVB footer 逐字段 |
| `work-xaga/dump/vendor_boot_a.img` | vendor_boot v4 逐字段、DTBO 容器、ramdisk table |
| `work-xaga/dump/dtbo_a.img` | DTBO 容器 + overlay 内容 |
| `work-xaga/dump/lk_a.img` | **命令表、校验逻辑、AEE/expdb、bootctrl、子分区结构、MD5 对比** |
| `work-xaga/dump/xaga_live.dts` | `/soc/logstore`、`chosen/bootargs`、reserved-memory |
| `work-xaga/dump/cmdline.txt` | 真机 `/proc/cmdline` |
| `work-xaga/dump/byname.txt` | 分区 → 块设备映射（`sdc43`=boot_a 等） |
| `work-xaga/xaga_scatter.xml` | 182 条分区、`operation_type`、`region` |
| `work-xaga/dt-device_xiaomi_xaga-TWRP/BoardConfig.mk` | **mkbootimg 全套参数**（`0x3fff8000` / `0x26f08000` / `0x07c88000`） |
| `hakios-mtk/mtk/tools/chainboot.py` | 我们自己的打包逻辑（`CHAIN_OFFSET=0x40000`、`BOOT_PARTITION=32<<20`） |
| `repo/hakios-mt6895/release/hakios-xaga-boot_a.img` | 当前发布镜像，解压后 61,645,720 B |
| `repo/hakios-mt6895/docs/headset-wip/ROUND-20-VERDICT.md` | 四次黑屏的逐轮记录、logbook `code` 语义、时序冲突 |
| `repo/hakios-mt6895/docs/DEBUGGING.md` | logbook 结构、看门狗快照、屏幕分页 |
| `repo/hakios-mt6895/docs/BOARD.md` | 板级常量与出处（`0x1c007000` WDT、`0x11001000` UART、ramoops 地址） |
| `research/BLACKSCREEN-forensics.md` | 姊妹报告：expdb / log_store / AEE 细节、loader 三个 bug |

---

## 8. 未完成 / 需要下一轮验证的项

| # | 项 | 为什么没做完 | 怎么补 |
|---|---|---|---|
| 1 | **LK 的 `KERNEL_DECOMPRESS_SIZE` 真实值** | 反汇编 `lk_a.img` 的 `lk` 子分区（799,768 B，VA 基址 `0xffffffff`）没定死。`decompress_kernel()` 的 4 个参数里第 4 个 `outlen` 在 `.text` 里传递，需要更完整的 Ghidra/objdump 追踪 | 用 Ghidra 载入 `lk` 子分区，base `0xffffffff`，找 `boot_linux_fdt` → `decompress_kernel(x0,x1,x2,x3)`，读 `x3` |
| 2 | **`LK_RAMDISK_MAX_SIZE` / `LK_RAMDISK_BASE` / `LK_DT_BASE`** | 在 `platform/mt6895/memory_layout.h` 里，`svoboda18/lk` 仓库没有这个文件（404） | 需要 MTK BSP 或从 `lk_a.img` 反汇编导出 |
| 3 | **`LK_RAMDISK_MAX_SIZE` 默认 16 MiB 是否被平台覆盖** | 同上。xaga 的 vendor_boot ramdisk 是 42.7 MB，如果还是 16 MiB 早就 assert 了，所以**必然被覆盖** | 同上 |
| 4 | **`oem expdb` 输出的封装格式** | 只知道是 deflate 压缩（字符串 `-- Compress expdb offset %llx, size %llu`），不知道有没有包头 | 真机跑一次 `oem expdb` 看前 64 字节 |
| 5 | **LK 的 `log_store` 基址 `0x7ffbf000`** | 姊妹报告给了这个值，本次未独立复核。设备树里 `/soc/logstore` 只有 `enabled` / `pmic_register`，没有 `reg` | 反汇编 `log_store_new()` 或看 `mboot_logstore_params_tag` |
| 6 | **`androidboot.verifiedbootstate` 的实际值** | 真机 `/proc/cmdline` 里没有（被 init 消费掉了） | `adb shell getprop ro.boot.verifiedbootstate` |
| 7 | **`oem cdms` 是什么** | 纯未知 | — |
| 8 | **xaga 的 test point 位置** | 没有公开图纸 | 不需要（有 preloader 就别短接） |
| 9 | **§5.6.6 那轮 692 的 `boot-args` 里 `loader_return_after=150` 与 `XNU_WATCH_SECONDS` 的关系** | `baseline692-result.json` 说正常交回（`code 0x0`，1364 行），但 11-14 后来那 4 轮黑屏用的是 `RETURN_AFTER=70`（< 120）。**说明"改小 RETURN_AFTER"不是黑屏的原因，也不是解药** | 下次刷机保持 `RETURN_AFTER=30`，同时 `WATCH=60`，确保任何情况下日志先落盘 |
| 10 | **`oem lkmsg` 的实际输出长度上限** | 字符串里只有 `data_size=0x%x`，不知道 LK 是否截断 | 真机跑一次看总字节数 |

---

## 9. 附：本次调研中被推翻/修正的三个中途判断

留档，避免下一轮重复踩。

| # | 中途判断 | 为什么错 | 修正后 |
|---|---|---|---|
| 1 | 「`flash:` 不在 LK 命令表里」 | 用 `strings` 找到的 `flash:` 只有一处，被误判成 `oem ultraflash:` 的子串。**实际它是从 `oem ultraflash:` 尾部合并出来的独立字符串**（`0x85855+9`，`\0` 终止） | `flash:` **在**（已注册），`boot:` **不在**（7 处全是 `fastboot: ...` 的子串，无一处 `\0` 终止）。见 §1.2 / §4.1 |
| 2 | 「`vendor_boot` 的 ramdisk 是 64,195,136 B」 | 这个数在三个镜像里按 LE32/LE64 都搜不到，是"分区余量"口径。真实 `vendor_ramdisk_size = 42,741,855`（10436 页），`dtb_offset = 0x28c5000` 而不是 `0x3d39000` | 见 §3.2 |
| 3 | 「四次黑屏要按 §5.1–§5.4 逐条查」 | 查到 `logs/headset-resume-20261114-220244/` 才发现 **11-14 22:13 有一次 692 镜像的完整成功记录**（`SpringBoard` 起来、`Main will unblank`、`current-slot: a`） | **§5.1/§5.2/§5.3/§5.4 全部实测排除。** 问题在 XNU 侧代码回归，见 §5.6.6 / §6.5 |

---

> **最后一句**：这份文档里"能确认的"和"不能确认的"混在一起的时候，看 §5.7 的汇总表。那里每条都有判定（✅ / ⚠️ / ❌）和理由。**别把 ⚠️ 当 ✅ 用。**
