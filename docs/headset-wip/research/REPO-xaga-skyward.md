# xaga-skyward 仓库调研

调研对象：`https://github.com/BkaNeko/xaga-skyward`（clone 于 `/tmp/xaga-skyward`，`main` @ `14781cce46cd5594887efa5725df4cdc061c9cb3`，2026-11-14，version 0.3.3，GPL-3.0-or-later）
调研时间：2026-11-14
证据分级：**【代码】**＝仓库源码里写的；**【构建产物】**＝从 release 镜像里解出来的；**【README】**＝文档声称的。

---

## 0. 一句话：这是什么

**它不是一个内核，也不是设备树仓库**——它是**用 Rust 从零写的 Android 恢复系统（recovery）**，是 `kirklin/skyward` 的 fork，
而 fork 的唯一目的是**给 xaga（Redmi Note 11T Pro / POCO X4 GT / Redmi K50i）做一份能用的第三方 recovery**，
所以 `devices/xaga.toml` 是这个 fork 相对上游唯一的实质改动（10.7 KB，全仓最大的一份设备配置）。

对我们的价值不在代码本身，而在两件副产品：

1. **`devices/xaga.toml`** —— 一份被人肉验证过、能在 xaga 真机上跑起来的**硬件/服务/分区行为清单**（WLAN、haptics、TEE、keymint、boot control、UFS RPMB 权限…）。
2. **它 release 里的 xaga 镜像**（`skyward-v0.3.3-xaga-OS1.0.3.0.ULOCNXM.img`，64 MiB，vendor_boot 格式）——
   这个镜像里带着 **xaga 的完整 vendor_boot ramdisk（含 198 个 MTK 内核模块、`fstab.mt6895`、`prop.default`）**
   和 **327 KB 的 MT6895 平台 DTB**。这两样是我们这次最大的收获，第 2 节全部来自它。

---

## 1. 基本信息（上游版本、设备、构建方式）

| 项 | 值 | 证据 |
|---|---|---|
| 语言/构建 | Rust workspace，23 个 crate，`edition 2024`，`rust-toolchain 1.100.0` | 【代码】`Cargo.toml:1-23`、`rust-toolchain.toml:1-4` |
| 目标三元组 | `aarch64-linux-android`，NDK r29，`cargo-ndk 4.1.2` | 【代码】`.github/actions/setup/action.yml:12-26` |
| 产物 | `skyward`（recovery 主程序）+ `skyward-system-init`（静态链接的首阶段）+ `skyward-pack`（PC 侧打包器） | 【代码】`README.md:200-217` |
| 目标设备 | `xaga`：Redmi Note 11T Pro / 11T Pro+ / POCO X4 GT / Redmi K50i，SoC 天玑 8100 | 【代码】`devices/xaga.toml:2-8` |
| 恢复分区位置 | **`vendor_boot`**（不是 `recovery`）——MTK 机型把 recovery 放 vendor_boot | 【代码】`devices/xaga.toml:12`、`:10-11` 注释 |
| 基线固件 | HyperOS `OS1.0.3.0.ULOCNXM`（= 机内 `V816.0.3.0`），Android **14**（包名 `..._14.0.zip`） | 【代码】`devices/xaga.toml:91-93` |
| 基线包 URL | `https://bkt-sgp-miui-ota-update-alisgp.oss-ap-southeast-1.aliyuncs.com/OS1.0.3.0.ULOCNXM/miui_XAGA_OS1.0.3.0.ULOCNXM_ddae354606_14.0.zip`（5,978,073,633 B，支持 Range，实测 HTTP 200） | 【代码】`devices/xaga.toml:92` + 本次实测 |
| 基线 vendor_boot 的 SHA-256 | `fed80580d619b916d73b51b704f2af6c9f08afafc84810ced0b2397a0b52797f` | 【代码】`devices/xaga.toml:93` |
| **实际内核版本** | **`5.10.198-android12-9-g5e8653a7cee7`**（GKI android12-9，SMP preempt，带 modversions） | 【构建产物】`lib/modules/*.ko` 的 `vermagic=`（如 `ufs-mediatek-mod.ko`、`mtk_panel_ext.ko`） |
| 本项目 release | `v0.3.3`，资产 `skyward-v0.3.3-xaga-OS1.0.3.0.ULOCNXM.zip`，56,917,051 B，sha256 `a2b991459653d0d935169bea4c4cdc547dc62c97877d62b018c8e7bf29761109`（已实测比对一致） | 【构建产物】 |

### 1.1 「目录里没有内核」这件事要记牢

全仓 **没有** `.dts` / `.dtsi` / `.dtb` / `Kconfig` / `defconfig` / 任何 `.c` / `.h`（`find` 结果为空）。
所以「仓库里的设备树」这个要求**在源码层面是不存在的**；第 2 节的 DTS 全部来自它 **release 出来的成品镜像**，并已标注。

---

## 2. 设备树：xaga/MT6895 的硬件事实

### 2.0 这些数据怎么来的（可复现）

```sh
# 1) 下载 release（54 MB，GitHub 直连即可，无需代理）
curl -L -o rel.zip https://github.com/BkaNeko/xaga-skyward/releases/download/v0.3.3/skyward-v0.3.3-xaga-OS1.0.3.0.ULOCNXM.zip
unzip rel.zip    # => skyward-v0.3.3-xaga-OS1.0.3.0.ULOCNXM.img (67108864 B)

# 2) 这是 vendor_boot v4：header_size=2128, page=4096, ramdisk=64,195,136 B @4096, dtb=327,372 B
python3 - <<'EOF'
import struct
b=open('skyward-v0.3.3-xaga-OS1.0.3.0.ULOCNXM.img','rb').read(4096)
u=lambda o: struct.unpack_from('<I',b,o)[0]
print(b[:8], u(8), u(12), u(24), u(2096), u(2100), u(2116))  # VNDRBOOT 4 4096 64195136 2128 327372 1
EOF

# 3) dtb 段是 Android DTBO 容器（magic 0xd7b7ab1e），里面 1 个 entry，entry 里是标准 FDT
dd if=...  # 偏移 64200704，长度 327372 => dtb.bin
python3 -c "
import struct;b=open('dtb.bin','rb').read()
m,t,h,e,c,o,p,v=struct.unpack_from('>8I',b,0)
sz,off,idx,rev=struct.unpack_from('>4I',b,o)
open('xaga.dtb','wb').write(b[off:off+sz])"
dtc -I dtb -O dts -o xaga.dts xaga.dtb     # 11,583 行

# 4) ramdisk 是 LZ4，解出来是 117 MB cpio
dd ... | lz4 -d -c > ramdisk.raw && mkdir rd && cd rd && cpio -idm --quiet < ../ramdisk.raw
```

**重要限定**：这份 DTB 的根节点是 `model = "MT6895"; compatible = "mediatek,MT6895";`，
且里面同时存在 `charger` 与 **`xagapro_charger`（`mediatek,xagapro_charger`）** 两套充电节点——
说明它是 **MT6895 平台公共 DTB（xaga / xaga-pro 共用）**，不是 xaga 定制板级 DTB。
板级差异（触摸、摄像头模组、屏、传感器）在 **`dtbo` 分区**，不在这个包里。
【构建产物】`xaga.dts:3-5`、`xaga.dts:8939`

### 2.1 核心平台事实

| 项 | 值 | 位置 |
|---|---|---|
| 内存 | `reg = <0x0 0x40000000 0x0 0x3e605000>` → **0x40000000 起，约 999.4 MiB 可见**（8 GB 机缩减后的 DTB 视图） | `xaga.dts:888-891` |
| CPU | 4×`arm,cortex-a55` @1.7 GHz（`0x65633340`）+ 4×`arm,cortex-a78` @2.17 GHz（`0x8166d4c0`）；cpu0-3 一组 PPI，cpu4-7 另一组 | `xaga.dts:523-620`、`948-971` |
| GIC | `arm,gic-v3`，dist `0xc000000`(0x40000) + redist `0xc040000`(0x200000)，`#interrupt-cells=<4>` | `xaga.dts:948-963` |
| Timer | `arm,armv8-timer`，`clock-frequency = 0xc65d40` = **13 MHz**，4 个 PPI（secure/ns phys/virt） | `xaga.dts:974-981` |
| PSCI | `arm,psci-1.0`，`method = "smc"` | `xaga.dts:853-856` |
| GPIO/EINT | `mediatek,mt6895-pinctrl`，14 个 iocfg 段，**227 个 GPIO**（`gpio-ranges ... 0xe3`），`mediatek,eint = <0x67>` | `xaga.dts:2707-2736` |
| bootargs（DTB 里写死的） | `console=tty0 root=/dev/ram 8250.nr_uarts=4 rcu_expedited=1 transparent_hugepage=never vmalloc=400M swiotlb=noforce firmware_class.path=/vendor/firmware` | `xaga.dts:518` |
| `kaslr-seed` | `<0x0 0x0>`（**KASLR 被关掉**） | `xaga.dts:519` |
| reserved-memory | **只有 3 条**：`vcp_share` 0x201000、`ssmr` 0x10000000(可复用 CMA)、`ssheap` 0x10000000 —— 明显是**被裁剪过的公共 DTB**，真实 xaga 的 TEE/ATF/SSPM 保留区在 dtbo 或 bootloader 里 | `xaga.dts:893-924` |

### 2.2 我们要用的外设节点（地址 / 中断 / 兼容串）

| 子系统 | 节点 | compatible | 地址 | 中断（SPI 号，GIC type 4） | 备注 |
|---|---|---|---|---|---|
| **SPMI 控制器** | `spmi@1c804000` | `mediatek,mt6895-spmi` | PMIF `0x1c804000`+0x8ff；SPMIMST `0x1c801000`+0x100 | `rcs_irq`=SPI 0xf3(243)、`pmif_irq`=SPI 0x19e(414) | `swinf_ch_start=0x0a`、`ap_swinf_no=0x02`；时钟 3 路 |
| **PMIC 主** | `pmic@4` | `mediatek,mt6363` | SPMI **slave 4**（`reg=<0x4 0x0>`） | `interrupts=<0x4>` | 见 2.3 |
| **PMIC 副** | `second_pmic@5` | `mediatek,mt6368` | SPMI **slave 5** | `interrupts=<0x5>` | **accdet 挂这里**，见 §3 |
| 子 PMIC | `mt6319@6` / `@7` | `mediatek,mt6319` | SPMI slave 6 / 7 | — | 给 CPU/GPU 供电的 buck |
| **UFS** | `ufshci@112b0000` | `mediatek,mt8183-ufshci` | `0x112b0000`+0x2300 | **SPI 0x89 (137)** | 10 路时钟、`power-domains=<0x4 0x2>`、`mediatek,ufs-qos` |
| UFS AO/PDN | `syscon@112b8000` / `@112bc000` | `mt6895-ufs_ao_config` / `ufs_pdn_cfg` | — | — | 复位控制器 |
| **eMMC/SD** | `mmc@11240000` / `@11242000` | `mediatek,common-mmc-v2` | — | — | msdc0/1 |
| **Mali GPU** | `mali@13000000` | `mediatek,mali`, `arm,mali-valhall` | **`0x13000000` + 0x480000** | **GPU=0x200(512), MMU=0x201(513), JOB=0x202(514), EVENT=0x203(515), PWR=0x204(516)** | 见 §4 |
| GPUEB | `gpueb@13c00000` | `mediatek,gpueb` | base `0x13c40000`+0x22000；mbox0 `0x13c5fd80`+0x280；set `0x13c62004`；clr `0x13c62074`；send `0x13c62000`；recv `0x13c62078` | **mbox0 = SPI 0x204 (516)** | 见 §4 |
| GPU FDVFS | `gpu_fdvfs@00112000` | `mediatek,gpu_fdvfs` | `0x112000`+0x400 | — | `fdvfs-policy-support=0` |
| MFG PLL | `syscon@13fa0000` | `mt6895-mfgpll_pll_ctrl` | `0x13fa0000`+0x400 | — | `#clock-cells=1` |
| GPUEB PLL | `syscon@13fa0800` | `mt6895-gpuebpll_pll_ctrl` | `0x13fa0800`+0x400 | — | |
| GPU RPC bus | `syscon@13f91000` | `mt6895-gpu_eb_rpc_bus` | — | — | |
| PMA（GPU 保护内存） | `protected-memory-allocator` | `arm,protected-memory-allocator` | **`0x13c40000`+0x22000**，`gpr_offset=0x1fd1c`, `gpr_id=6` | — | `gmpu_table_size=0x400000`, `protected_reserve_size=0x1000000` |
| **DSI** | `dsi@14017000` | `mediatek,dsi0`,`mt6895-dsi` | `0x14017000`+0x1000 | **SPI 0x143 (323)** | clocks: engine/digital/hs；phy=`0xdd` |
| MIPI TX | `mipi_tx_config@11f70000` | `mt6895-mipi-tx` | `0x11f70000` | — | D-PHY |
| Disp 主控 | `dispsys_config@14000000` | `mediatek,mt6895-disp` | `0x14000000` | — | 见 2.4 全表 |
| 背光 PWM | `disp_pwm0@1100e000` | `mediatek,mt6895-disp-pwm` | `0x1100e000` | — | |
| 通用 PWM | `pwm@11008000` | `mediatek,pwm` | `0x11008000` | — | |
| **I2C5** | `i2c@11280000` | `mediatek,mt6983-i2c` | `0x11280000`+0x1000, DMA `0x11300400`+0x80 | **SPI 0x95 (149)** | **1 MHz**（`clock-frequency=0xf4240`）；挂 rt5133/rt6160/mt6375 |
| I2C6 | `i2c@11281000` | `mediatek,mt6983-i2c` | `0x11281000` | — | 挂 `gate_ic@11` |
| 其余 I2C | `0x11f40000`(i2c0), `0x11d00000`-`0x11d06000`(i2c2..i2c9) | `mediatek,mt6983-i2c` | — | — | 板级设备（触摸/ sensor）在 dtbo |
| SPI | `spi0..7` `0x11010000`-`0x11017000` | `mediatek,mt6983-spi` | — | — | |
| **UART0（调试口）** | `serial@11001000` | `mediatek,mt6577-uart` | `0x11001000`+0x1000 | **SPI 0x8d (141)** | `8250.nr_uarts=4`；**黑屏排查第一入口** |
| **USB0（OTG/ADB）** | `usb0@11201000` | `mediatek,mtu3`,`mt6895-mtu3` | MAC `0x11201000`+0x2e00；IPPC `0x11203e00`+0x100 | **SPI 0x80 (128)** | `dr_mode="otg"`, `maximum-speed="high-speed"`（**USB2 only**） |
| USB0 xHCI | `xhci0@11200000` | `mediatek,mtk-xhci` | `0x11200000` | — | |
| USB1 | `usb1@11211000` / `xhci1@11210000` | `mediatek,mtu3` / `mtk-xhci-p1` | — | — | |
| efuse/devinfo | `efuse@11f10000` | `mediatek,devinfo` | `0x11f10000` | — | |
| LVTS（温度） | `lvts@10315000` | `mediatek,mt6895-lvts` | `0x10315000` | — | 12 个 thermal zone |
| 音频 AFE | `mt6895-afe-pcm@1e100000` | `mediatek,mt6895-sound` | `0x1e100000`+0x2000 | **SPI 0x1a0 (416)** | 见 §5.3 |
| **声卡** | `sound` | `mediatek,mt6895-mt6368-sound` | — | — | `mediatek,headset-codec=<accdet 的 phandle 0x126>`、`mediatek,platform=<AFE 0x127>` |
| 音频 SRAM | `audio_sram@1e102000` | `mediatek,audio_sram` | `0x1e102000` | — | |
| BT CVSD | `mtk-btcvsd-snd@18830000` | `mediatek,mtk-btcvsd-snd` | `0x18830000` | — | |
| WiFi/Conn | `consys@18000000` | `mediatek,mt6895-consys` | `0x18000000` | — | 见 §5.5 |
| BTIF | `btif@1100c000` | `mediatek,btif` | `0x1100c000` | — | |
| WiFi | `wifi@18000000` | `mediatek,wifi` | `0x18000000` | — | |
| BT | `bt@18000000` | `mediatek,bt` | `0x18000000` | — | |
| FM | `fm@18000000` | `mediatek,fm` | `0x18000000` | — | |
| CAM ISP | `camisp@1a000000` | `mediatek,camisp` | `0x1a000000`+0x1000 | — | `pd=<0x4 0xc>` |
| CAM RAW A/B/C | `cam_raw_a@1a030000` / `_b@1a070000` / `_c@1a0b0000` | `mediatek,cam-raw` | — | **0x1ac (428)** for A | |
| CAM YUV A/B/C | `0x1a050000` / `0x1a090000` / `0x1a0d0000` | `mediatek,cam-yuv` | — | — | |
| CAMSV1-16 | `0x1a110000`..`0x1a187000` | `mediatek,camsv` | — | — | 16 路 |
| SENINF | `seninf_top@1a010000` | `mediatek,seninf-core` | `0x1a010000` | — | |
| MRAW1-4 | `0x1a130000`..`0x1a160000` | `mediatek,mraw` | — | — | |
| CCU（相机协处理器） | `ccu_rproc@1b080000` | `mediatek,ccu_rproc` | `0x1b080000` | — | |
| SCP | `scp@1c700000` | `mediatek,scp` | `0x1c400000`+0x180000 等 8 段 | — | |
| SSPM | `sspm@1C340000` | `mediatek,sspm` | `0x1c340000` | — | |
| MCUPM | `mcupm@c540000` | `mediatek,mcupm` | `0xc540000` | — | |
| Sleep/SPM | `sleep@1C001000` | `mediatek,sleep` | `0x1c001000` | — | |
| SPM TWAM | `spmtwam@1c001000` | `mediatek,spmtwam` | `0x1c001000` | — | |
| Topckgen | `syscon@10000000` | `mt6895-topckgen` | `0x10000000`+0x1000 | — | `#clock-cells=1` |

### 2.3 MT6363（主 PMIC，SPMI slave 4）子节点

| 子节点 | compatible | 说明 |
|---|---|---|
| `mt6363-auxadc` | `mediatek,mt6363-auxadc` | 通道：`batadc`=0（`resistance-ratio 4:1`, avg 64）、`bat_temp`=3、`chip_temp`=5、`vcore_temp`=6、`vproc_temp`=7 |
| `mt6363-efuse` | `mediatek,mt6363-efuse` | |
| `mt6363-consys` | `mediatek,mt6363-consys` | 连通性供电 |
| **`mt6363keys`** | `mediatek,mt6363-keys` | **电源键** `linux,keycodes=<0x74>`(KEY_POWER=116)、wakeup；`home`=`<0x73>`(KEY_VOLUMEUP=115)。`long-press-mode=1`, `power-off-time-sec=0` |
| `mtk_dynamic_loading_throttling` | `mediatek,mt6363-dynamic_loading_throttling` | `uvlo-level=0xa28` |
| `regulators` | — | 含 `VUFS18`（`mt6363_vufs18`）、`VUFS12`、`VCORE`、`VS2` 等 |

> xaga.dts **没有**独立的 `rtc` 节点：RTC 走 `mt6685_mfd/mt6685_rtc`（`compatible="mediatek,mt6685-rtc"`，`base=<0x580>`，`interrupts=<0x9 0x0>`），
> 内核模块是 `rtc-mt6685.ko`。**【构建产物】** `xaga.dts:1743-1770`、`rd/lib/modules/rtc-mt6685.ko`

### 2.4 显示（MT6895 mmsys）——完整地址表

两套 disp 管线（主 `0x1400xxxx` / 副 `0x1440xxxx`），全部 `+0x1000`：

```
mutex 0x14001000 | ovl0 0x14002000 | ovl0_2l 0x14003000 | ovl1_2l 0x14004000
rsz0 0x14005000 | rdma0 0x14006000 | tdshp0 0x14007000 | c3d0 0x14008000
color0 0x14009000 | ccorr0 0x1400a000 | ccorr1 0x1400b000 | mdp_aal0 0x1400c000
aal0 0x1400d000 | gamma0 0x1400e000 | postmask0 0x1400f000 | dither0 0x14010000
chist0 0x14011000 | chist1 0x14012000 | cm0 0x14013000 | spr0 0x14014000
dsc_wrap 0x14015000 | merge0 0x14016000 | dsi0 0x14017000 | wdma0 0x14018000
rdma1 0x1401c000 | wdma1 0x1401f000
--- 副管线 ---
ovl1 0x14402000 | ovl2_2l 0x14403000 | ovl3_2l 0x14404000 | rsz1 0x14405000
rdma2 0x14406000 | tdshp1 0x14407000 | c3d1 0x14408000 | color1 0x14409000
ccorr2 0x1440a000 | ccorr3 0x1440b000 | mdp_aal1 0x1440c000 | aal1 0x1440d000
gamma1 0x1440e000 | postmask1 0x1440f000 | dither1 0x14410000 | chist2 0x14411000
chist3 0x14412000 | cm1 0x14413000 | spr1 0x14414000 | dsc1_wrap 0x14415000
merge1 0x14416000 | wdma2 0x14418000 | ufbc_wdma0 0x14419000 | rdma3 0x1441c000
dp_intf0 0x1441d000 | wdma3 0x1441f000
```
**【构建产物】** `xaga.dts:6753-7325`

### 2.5 分区表（来自 vendor_boot ramdisk 里的 `fstab.mt6895`，**这是 xaga 真机的表**）

`:9-105` 的关键行（原文）：

```
system      /system      erofs ro wait,slotselect,logical,first_stage_mount,readahead_size_kb=128
vendor      /vendor      erofs ro wait,slotselect,logical,first_stage_mount,readahead_size_kb=128
product     /product     erofs ro wait,slotselect,logical,first_stage_mount,readahead_size_kb=128
system_ext  /system_ext  erofs ro wait,slotselect,logical,first_stage_mount,readahead_size_kb=128
odm         /odm         erofs ro wait,slotselect,logical,first_stage_mount,readahead_size_kb=128
vendor_dlkm /vendor_dlkm erofs ro wait,slotselect,logical,first_stage_mount,readahead_size_kb=128
odm_dlkm    /odm_dlkm    erofs ro wait,slotselect,logical,first_stage_mount,readahead_size_kb=128
/dev/block/by-name/metadata /metadata ext4 noatime,nosuid,nodev,discard wait,check,formattable,first_stage_mount
/dev/block/by-name/userdata /data f2fs noatime,nosuid,nodev,discard,noflush_merge,fsync_mode=nobarrier,
    reserve_root=134217,resgid=1065,checkpoint_merge,gc_merge,compress_mode=user,compress_cache,atgc,
    age_extent_cache,inlinecrypt wait,check,formattable,quota,latemount,resize,reservedsize=128m,
    checkpoint=fs,fileencryption=aes-256-xts:aes-256-cts:v2+inlinecrypt_optimized,
    keydirectory=/metadata/vold/metadata_encryption,fsverity
/dev/block/by-name/rescue  /cache  ext4 ...        # 注意：cache 就是 rescue 分区
/dev/block/by-name/protect1 /mnt/vendor/protect_f ext4 ...
/dev/block/by-name/protect2 /mnt/vendor/protect_s ext4 ...
/dev/block/by-name/nvdata   /mnt/vendor/nvdata ext4 ...
/dev/block/by-name/nvcfg    /mnt/vendor/nvcfg  ext4 ...
/dev/block/by-name/persist  /mnt/vendor/persist ext4 ...
```
`:86-159` 的裸分区（`emmc`）：`frp`, `nvram`, `proinfo`, `lk`, `lk2`, `para`, `misc`, `boot`, `vbmeta_vendor`,
`vbmeta_system`, `logo`, `expdb`, `seccfg`, `tee1`, `tee2`, `scp1`, `scp2`, `sspm_1`, `sspm_2`, `dpm_1`, `dpm_2`,
`mcupm_1`, `mcupm_2`, `md1img`, `md1dsp`, `md1arm7`, `md3img`, `gz1`, `gz2`, `spmfw`, `audio_dsp`, `pi_img`,
`boot_para`, `odmdtbo`, `dtbo`, `otp`, `vbmeta`。
`:81-84`：可移动存储 `11240000.mmc*` → `sdcard1`；`usb_xhci*` / `11201000.usb0/11200000.xhci*` → `usbotg`。

**对 XNU 的直接含义**：`/data` 是 **f2fs + inlinecrypt + fileencryption=aes-256-xts:v2（hardware wrapped key）**，
metadata 加密目录 `/metadata/vold/metadata_encryption`。这与 Skyward 在 `xaga.toml` 里挂 `persist`、跑 keymint beanpod 的做法一致。

---

## 3. 耳机相关（accdet / MT6368 / jack）

### 3.1 结论先行

**仓库源码里 0 处「accdet」「mt6368」「3.5mm」**（`grep -ril` 全仓为空；`headset` 45 处、`jack` 7 处**全部**在别机型的测试数据
`crates/skyward-programs/tests/described/{fuxi,annibale,...}` 里，都是高通机型的 `build.prop` / `init.qcom.factory.rc`，**与 xaga 无关**）；
**xaga 的 `xaga.toml` 完全没提耳机**。

但 **DTB 里有完整的 MT6368 accdet 节点**，而且这是我们第一次拿到 xaga 耳机检测的**全部硬件参数**。这是本次最有价值的三条之一。

### 3.2 DTB 原文（`xaga.dts:1796-1830`）——【构建产物】

```dts
second_pmic@5 {
    compatible = "mediatek,mt6368";
    reg = <0x05 0x00>;          // ← SPMI slave 5，与你们的判断一致
    interrupts = <0x05>;        // ← MT6368 在 SPMI 上的中断号 5
    #address-cells = <0x01>; #size-cells = <0x00>;
    interrupt-controller; #interrupt-cells = <0x02>;

    accdet {
        compatible = "mediatek,mt6368-accdet";
        accdet-name = "mt63xx-accdet";
        accdet-mic-vol = <0x08>;              // 2.8V?（MTK 编码：0x08）
        accdet-plugout-debounce = <0x01>;
        accdet-mic-mode = <0x02>;             // 双 MIC / ACC mode 2
        eint_use_ext_res = <0x00>;
        headset-mode-setting = <0x500 0x500 0x01 0x1f0 0x800 0x800 0x20 0x44 0x04 0x01 0x05 0x03 0x03 0x05 0x0e>;
        headset-use-ap-eint = <0x00>;         // ★ 不用 AP EINT，走 PMIC 内部中断
        headset-eint-num = <0x00>;
        headset-eint-trig-mode = <0x01>;
        headset-key-mode = <0x00>;            // 按键检测模式 0
        headset-three-key-threshold = <0x00 0x50 0xdc 0x190>;       // 三段键 ADC 门限
        headset-three-key-threshold-CDD = <0x00 0x79 0xc0 0x258>;   // CDD（欧标）门限
        headset-four-key-threshold = <0x00 0x3a 0x79 0xc0 0x190>;   // 四段键门限
        io-channels = <0x52 0x09>;            // ← MT6368 auxadc channel 9
        io-channel-names = "pmic_accdet";
        nvmem = <0x53>;
        nvmem-names = "mt63xx-accdet-efuse";  // ← 校准数据在 MT6368 efuse
        status = "okay";
        phandle = <0x126>;
    };
```

配套（同文件）：
- `mt6368-auxadc`（`xaga.dts:1832-1871`）：`accdet { channel = <0x09>; }`、`hpofs_cal { channel = <0x0c>; avg-num = <0x100>; }`
- `mt6368-efuse`（`:1873-1883`）：`mt6368_e_data { reg = <0x52 0x0a>; }` → accdet 校准 efuse 偏移 **0x52，长度 0x0a**
- `mt6368codec`（`:1890-1897`）：`compatible = "mediatek,mt6368-sound"`，`io-channels=<0x52 0x0c>`（`pmic_hpofs_cal`），`nvmem-names="pmic-hp-efuse"`
- 声卡（`:10106-10111`）：`mediatek,mt6895-mt6368-sound`，`mediatek,headset-codec = <0x126>` ← **指向 accdet**，`mediatek,platform = <0x127>` ← AFE
- aliases（`:11007`）：`accdet = "/spmi@1c804000/second_pmic@5/accdet"`

### 3.3 驱动在哪：`mtk-spmi-pmic.ko`（**在 vendor_boot ramdisk 里，不在 vendor_dlkm**）

`strings rd/lib/modules/mtk-spmi-pmic.ko` 里出现：`mt6368-accdet`、`mediatek,mt6368-accdet`、`mt6369-accdet`、
`mt6368_accdet_resources`、`mt6369_accdet_resources`、`ACCDET_IRQ`、`ACCDET_EINT0`、`ACCDET_EINT1`。

**这条对你们很关键**：
1. accdet **不是**独立模块，它编译在 MTK 的 SPMI PMIC mfd 驱动里 → 想「先点亮 SPMI 再读耳机寄存器」，只加载 `mtk-spmi-pmic.ko` 就够了，
   不需要 `mt6368-regulator.ko`（虽然 `modules.load.recovery:104-105` 也加载了它）。
2. xaga 的 `modules.sources` 是 `["vendor_boot","vendor_dlkm","vendor"]`，而 **`mtk-spmi-pmic.ko` 在 vendor_boot 的 `/lib/modules`**，
   所以哪怕 vendor_dlkm 不挂，accdet 的代码也是**已经在内存里可加载**的。
3. 但 `accdet` 节点在 DTB 的 `second_pmic@5` 下，`status="okay"` → **只要 SPMI 控制器 probe 成功、能读 slave 5 的 ID，accdet 就该 probe**。

### 3.4 我们「从未读到过任何耳机寄存器」的可能解释（基于上述事实的**推断**，非仓库结论）

> 以下是分析，不是仓库里写的，标注清楚以免与事实混淆。

- SPMI 访问要过 `mtk-pmic-wrap.ko` + `spmi-mtk-pmif.ko` + `mtk-spmi-pmic.ko` 三层，**且要先让 `spmi@1c804000` 的 `pmif_irq`(SPI 414) 和
  `rcs_irq`(SPI 243) 可用**；如果 XNU 侧只做了寄存器读写而没处理 `RCS`（Read Command State）握手，读会**静默失败**（PMIF 有 `pmif_access_store`/`spmi_access_show` 调试接口）。
- MT6368 的 ID 校验：`mediatek,mt6368` 的 `reg=<0x5 0x0>`，SPMI 从设备地址是 **7-bit 0x5**（写 0x0A / 读 0x0B）。
  若按 8-bit 写 0x05 就会打到 slave 2。
- `accdet-mic-mode=<0x02>` + `headset-mode-setting` 的第 3 个值 `0x01` 表示 **ACCDET 上电即做一次 plug 检测**；
  不写这些寄存器、只读状态，会一直是「未插入」。
- `nvmem-names="mt63xx-accdet-efuse"` → 校准值必须从 **efuse 0x52/0x0a** 读出来再写进 accdet 的校准寄存器，否则门限比较全错。

### 3.5 明确没有的

- 没有 accdet 的**寄存器偏移定义**（驱动是闭源 `.ko`，无 `.c`、无头文件）。
- 没有 `.dtsi` 里 accdet 的 `interrupts`/`interrupt-names`（该节点**没有** `interrupts` 属性，靠 `second_pmic@5` 的中断域 2-cell 编号）。
- 仓库里没有 `headset`/`jack` 的 sysfs 路径、没有 `/proc/accdet` 之类调试节点名。

---

## 4. GPU 相关（Mali-G610）

### 4.1 结论

仓库源码里 **0 处** Mali-G610/GPU 驱动代码（`mali` 的 28 处命中全是字体路径 `colour-normalised` 和别的机型 rc 里的 `mali_systrace`）。
**但 DTB 里有完整的 Mali + GPUEB 地址/中断/mailbox/OPP 表**，这直接对应我们「MCU 收到 AUTO 后不执行」的卡点。

### 4.2 `mali@13000000`（`xaga.dts:9125-9136`）

```dts
mali@13000000 {
    compatible = "mediatek,mali", "arm,mali-valhall";
    reg = <0x00 0x13000000 0x00 0x480000>;      // 4.5 MiB 寄存器窗口
    interrupts = <0x00 0x200 0x04 0x00
                  0x00 0x201 0x04 0x00
                  0x00 0x202 0x04 0x00
                  0x00 0x203 0x04 0x00
                  0x00 0x204 0x04 0x00>;
    interrupt-names = "GPU", "MMU", "JOB", "EVENT", "PWR";   // 512/513/514/515/516
    protected-memory-allocator = <0x110>;
    operating-points-v2 = <0x111>;               // opp-table0
    #cooling-cells = <0x02>;
    ged-supply = <0x112>;
    phandle = <0x276>;
};
```

### 4.3 GPUEB（MCU）mailbox（`xaga.dts:4874-4892`）—— 我们卡住的地方

```dts
gpueb@13c00000 {
    compatible = "mediatek,gpueb";
    gpueb-support = <0x01>;
    gpueb-logger-support = <0x00>;
    mbox_count = <0x01>;      mbox_size = <0xa0>;      slot_size = <0x04>;      ts_mbox = <0x00>;
    reg = <0x00 0x13c40000 0x00 0x22000        // gpueb_base（SRAM/寄存器）
           0x00 0x13c5fd80 0x00 0x280          // mbox0_base
           0x00 0x13c62004 0x00 0x04           // mbox0_set
           0x00 0x13c62074 0x00 0x04           // mbox0_clr
           0x00 0x13c62000 0x00 0x04           // mbox0_send
           0x00 0x13c62078 0x00 0x04>;         // mbox0_recv
    reg-names = "gpueb_base","mbox0_base","mbox0_set","mbox0_clr","mbox0_send","mbox0_recv";
    interrupts = <0x00 0x204 0x04 0x00>;        // SPI 516，注意与 mali 的 "PWR" 同号
    interrupt-names = "mbox0";
    gpueb_mem_table = <0x00 0x1000 0x01 0x180000>;   // MEM_ID_GPUFREQ:0x1000, MEM_ID_LOG:0x180000
    gpueb_mem_name_table = "MEM_ID_GPUFREQ", "MEM_ID_LOG";
    phandle = <0x1f0>;
};
```

**IPI 通道表**（`send_table` / `recv_table`，每 4 个 u32 一组：`id, mbox, slot, ...`）：

| IPI | id | send slot | recv slot |
|---|---|---|---|
| `IPI_ID_FAST_DVFS_EVENT` | 0 | 4 | 4 |
| `IPI_ID_GPUFREQ` | 1 | 8 | 8 |
| `IPI_ID_SLEEP` | 2 | 3 | 1 |
| `IPI_ID_TIMER` | 3 | 3 | 1 |
| `IPI_ID_FHCTL` | 4 | 9 | 1 |
| `IPI_ID_CCF` | 5 | 4 | 4 |
| `IPI_ID_GPUMPU` | 6 | 6 | 1 |
| `IPI_ID_FAST_DVFS` | 7 | 6 | 6 |

> 原文：`send_name_table = "IPI_ID_FAST_DVFS_EVENT", "IPI_ID_GPUFREQ", "IPI_ID_SLEEP", "IPI_ID_TIMER", "IPI_ID_FHCTL", "IPI_ID_CCF", "IPI_ID_GPUMPU", "IPI_ID_FAST_DVFS"`（`xaga.dts:4883`）
> `send_table = <0 0 4 1 0 8 2 0 3 3 0 3 4 0 9 5 0 4 6 0 6 7 0 6>`（`:4882`）

### 4.4 GPU DVFS / 电源域（`xaga.dts:9475-9498`）

```dts
gpufreq {
    compatible = "mediatek,gpufreq";
    reg = <... 0x13fa0000 0x400   // mfg_pll
            ... 0x13fa0c00 0x400   // mfgsc_pll
            ... 0x13fbf000 0x1000  // mfg_top_config
            ... 0x13f90000 0x10000 // mfg_rpc
            ... 0x1c001000 0x1000  // sleep
            ... 0x10000000 0x1000  // topckgen
            ... 0x1021c000 0x1000  // nth_emicfg_reg
            ... 0x1021e000 0x1000  // sth_emicfg_reg
            ... 0x10270000 0x1000  // nth_emicfg_ao_mem_reg
            ... 0x1030e000 0x1000  // sth_emicfg_ao_mem_reg
            ... 0x10001000 0x1000  // infracfg_ao
            ... 0x10023000 0x1000  // infra_ao_debug_ctrl
            ... 0x1002b000 0x1000  // infra_ao1_debug_ctrl
            ... 0x10042000 0x1000  // nth_emi_ao_debug_ctrl
            ... 0x11f10000 0x1000  // efuse
            ... 0x13fb9c00 0x100  // mfg_cpe_control
            ... 0x13fb6000 0x1000>; // mfg_cpe_sensor
    power-domains = <... pd_mfg1 .. pd_mfg12>;   // 12 个 MFG 电源域（scpsys id 0x1b..0x26）
    _vcore-supply / _dvfsrc-supply / _vstack-supply / _vsram-supply;
    nvmem-cell-names = "efuse_segment_cell";
};
gpufreq_wrapper { compatible="mediatek,gpufreq_wrapper"; gpufreq-version=<2>; dual-buck=<1>; gpueb-support=<1>; };
ged { compatible="mediatek,ged"; gpufreq-supply=<gpufreq>; };
```

### 4.5 GPU OPP 表（`opp-table0`，`xaga.dts:9260-9473`，共 **42** 档 opp00..opp41）

| opp | 频率 | 电压 | opp | 频率 | 电压 |
|---|---|---|---|---|---|
| 00 | **950 MHz** | 788 mV | 20 | 538 MHz | 631 mV |
| 01 | 860 MHz | 750 mV | 21 | 521 MHz | 625 mV |
| 02 | 852 MHz | 744 mV | 22 | 504 MHz | 619 mV |
| 03 | 845 MHz | 738 mV | 23 | 487 MHz | 612 mV |
| 04 | 837 MHz | 731 mV | 24 | 470 MHz | 606 mV |
| 05 | 830 MHz | 725 mV | 25 | 453 MHz | 600 mV |
| 06 | 822 MHz | 719 mV | 26 | 436 MHz | 594 mV |
| 07 | 815 MHz | 712 mV | 27 | 419 MHz | 588 mV |
| 08 | 807 MHz | 706 mV | 28 | 402 MHz | 581 mV |
| 09 | 800 MHz | 700 mV | 29 | 385 MHz | 575 mV |
| 10 | 773 MHz | 694 mV | 30 | 368 MHz | 569 mV |
| 11 | 747 MHz | 688 mV | 31 | 351 MHz | 562 mV |
| 12 | 721 MHz | 681 mV | 32 | 334 MHz | 556 mV |
| 13 | 695 MHz | 675 mV | 33 | 317 MHz | 550 mV |
| 14 | 668 MHz | 669 mV | 34 | 304 MHz | 544 mV |
| 15 | 642 MHz | 662 mV | 35 | 292 MHz | 538 mV |
| 16 | 616 MHz | 656 mV | 36 | 280 MHz | 531 mV |
| 17 | 590 MHz | 650 mV | 37 | 268 MHz | 525 mV |
| 18 | 572 MHz | 644 mV | 38 | 255 MHz | 519 mV |
| 19 | 555 MHz | 638 mV | 39 | 243 MHz | 512 mV |
| — | — | — | 40 | 231 MHz | 506 mV |
| — | — | — | 41 | **219 MHz** | **500 mV** |

> 注意：**950 MHz 是 DTB 里的顶档，不是营销值 950/…**。天玑8100 官方 Mali-G610 MC6 @ 950 MHz 与此一致。

### 4.6 与「MCU 收到 AUTO 后不执行」直接相关的三条硬事实

1. **GPUEB 的 mailbox 是 5 个独立 32-bit 寄存器**（`set`/`clr`/`send`/`recv` 各 4 字节，base 0x13c62000 一族），
   不是常见的「写一个 64-bit 队列」。**ACK/AUTO 的握手很可能走 `mbox0_set` 写 1 清 0**——
   如果 XNU 只写 `send` 不读 `recv`/不清 `clr`，MCU 侧会停在「上一次事务未完成」。
2. **GPUEB 的中断是 SPI 516，与 Mali 的 `PWR` 中断同号**（`mali` 的 interrupt-names 第 5 个也是 0x204）。
   共享中断必须分别 ack，否则会出现「中断来了但状态机不前进」。
3. **GPUEB 固件分两段内存**：`MEM_ID_GPUFREQ` 0x1000 B、`MEM_ID_LOG` 0x180000 B，
   都在 `gpueb_base 0x13c40000` 起 0x22000 之外的 **GPUEB SRAM**（`gpueb_mem_table` 给的是 ID + 大小，不是物理地址）。
   → MCU 起来后不执行，很可能是**这两段共享内存的地址没按 bootloader/`mtk-spmi`/`scpsys` 约定填对**。
4. `PROTECTED_MEMORY_ALLOCATOR`：`reg=0x13c40000+0x22000`、`gpr_offset=0x1fd1c`、`gpr_id=6`、
   `gmpu_table_size=0x400000`、`protected_reserve_size=0x1000000` —— **GPU 要被允许访问 DDR，必须先经 GPR 6 在 GPA 表里登记**。

---

## 5. 其它子系统

### 5.1 存储（UFS）

| 事实 | 证据 |
|---|---|
| 控制器 `ufshci@112b0000`，`mediatek,mt8183-ufshci`，窗口 0x2300，**IRQ SPI 137** | 【构建产物】`xaga.dts:6584-6600` |
| 10 路时钟：`phy_tx_sym, phy_rx_sym0, phy_rx_sym1, unipro_sys, unipro_sap, phy_sap, ufs_ck, ufs_fde_ck, ufs_fde, ufs` | 同上 |
| 3 路复位：`unipro_rst`, `crypto_rst`, `hci_rst`；`power-domains=<scpsys 2>`；`vcc-supply`/`vccq-supply` | 同上 |
| 供电：`mt6363_vufs18`（1.8V，范围 0x927c0–0x200b20 µV，ramp 0x2a8）、`mt6363_vufs12` | `xaga.dts:1515-1522`、`10977`、`10984` |
| 驱动链（recovery 里实际加载顺序）：`phy-mtk-ufs.ko` → `rpmb.ko` → `rpmb-mtk.ko` → `ufs-mediatek-mod.ko` → `ufs-mediatek-dbg.ko` → `cqhci.ko` → `mtk-mmc*.ko` | 【构建产物】`rd/lib/modules/modules.load.recovery:94-98`（UFS 链）；`cqhci.ko`/`mtk-mmc*.ko` 在 `:87-90` |
| QOS：`mediatek,ufs-qos` 属性 | `xaga.dts:6597` |
| **UFS RPMB 设备节点：`/dev/0:0:0:49476` 和 `/dev/rpmb0`，必须 chown system:system 0660** | 【代码】`devices/xaga.toml:236-240` |
| 该权限**必须在 `teei_daemon` 启动前**设好，否则 gatekeeper TA 读不到 RPMB，会拒绝所有已录入的锁屏密码 | 【代码】`devices/xaga.toml:228-235` 注释（原文：*"the TA reads RPMB only when it loads, so handing it over later changes nothing"*） |

> `0:0:0:49476` 是 **UFS well-known LUN 的 RPMB unit**（49476 = 0xC144）。这条对 XNU 侧做 secure storage 同样成立。

### 5.2 电源 / 充电

| 事实 | 证据 |
|---|---|
| 充电器 IC：**MT6375**（i2c5 @0x34），`mediatek,mt6375`，中断父=pinctrl，`interrupts=<0x74 0x08>` | 【构建产物】`xaga.dts:3996-4010` |
| MT6375 子节点 `chg`：`chg_name="primary_chg"`，`aicr=0x1f4`(500 mA)，`mivr=0x1130`(4400 mV)，`cv=0x1068`(4200 mV)，`ichg=0x7d0`(2000 mA)，`ieoc=0x96`(150 mA)，`wdt=0x9c40`，`vbus_ov=0x38a4` | `xaga.dts:4016-4040` |
| MT6375 子节点 `adc`（`mediatek,mt6375-adc`）；`mt6375-battery`（电量计走 `bq28z610`） | `xaga.dts:4007-4014`；`modules.load.recovery` |
| 充电算法节点 `charger`：`compatible="mediatek,charger"`，`algorithm_name="Basic"`，`battery_cv=0x43e6d0`(**4450 mV**)，`max_charger_voltage=0x632ea0`(**6500 mV**)，`min_charger_voltage=0x4630c0`(**4600 mV**) | `xaga.dts:8894-8937` |
| JEITA：T4=50 °C, T3=45 °C, T2=10 °C, T1=0 °C；CV 在 4.45/4.34/4.30/4.04 V 间切换 | 同上 |
| **xaga 专属** `xagapro_charger`：`compatible="mediatek,xagapro_charger"`（同一套参数，去掉 JEITA） | `xaga.dts:8939-8963` |
| 快充协议节点：`pe`(PumpExpress)、`pe2`、`pe4`、`pe5`、`pdc`、`pd_adapter`（`phys=<0x76 3>`, `product_index=1`） | `xaga.dts:8759-8796`、`10031-10040` |
| 电荷泵：`sc8551.ko`、`sc8561.ko`、`ln8000_charger.ko`、`pd_cp_manager.ko`、`fan53870.ko`、`wl2868c.ko` | 【构建产物】`modules.load.recovery` |
| 电量计：`bq28z610.ko`（TI），另 `mt6375-battery.ko` | 同上 |
| 电池曲线：`battery0_profile_t0..t5`，`_num=0x64`(100 点)，4 列（容量/电压/内阻/温度） | `xaga.dts:4196-4213` |
| 温控/降流：`mtk_dynamic_loading_throttling`、`mtk_battery_oc_throttling`、`mtk_low_battery_throttling`、`pmic_lbat_service`、`mtk_pbm`、`mtk_mdpm` | `xaga.dts:1144-1152`、`modules.load.recovery` |
| 小米私有：`xiaomi_charge`、`xagapro_PCM`、`smart_pa`、`pd_cp_manager`（**只有 phandle，无 compatible、无属性** → 由小米闭源驱动通过 phandle 引用填） | `xaga.dts:10015-10029` |
| **`lk_charger`**（bootloader 关机充电参数）：`max_charger_voltage=0x632ea0`，`usb_charger_current=0x7a120`(500 mA)，`ac_charger_current=0x1f47d0`(2050 mA)，`pd_charger_current=0x7a120` | `xaga.dts:8738-8757` |

> **对 HakiOS 的直接用法**：Skyward 只读 `/sys/class/power_supply`（`crates/skyward-info/src/lib.rs:18,358-405`），
> 字段名 `capacity/status/temp/health/cycle_count/charge_full_design/charge_full/voltage`，
> 并对高通机型额外读 `qcom-battery/quick_charge_type`（**xaga 走 MTK，没有这个节点**，所以 `quick` 恒 false）。

### 5.3 音频

| 事实 | 证据 |
|---|---|
| AP 侧 AFE：`mt6895-afe-pcm@1e100000`，`mediatek,mt6895-sound`，0x2000，**IRQ SPI 416** | `xaga.dts:10068-10079` |
| 声卡：`mediatek,mt6895-mt6368-sound`，`headset-codec=<accdet>`，`platform=<AFE>` | `xaga.dts:10106-10110` |
| Codec：MT6368 内置（`mt6368codec`，`mediatek,mt6368-sound`），走 `pmic_hpofs_cal`（auxadc ch 12）+ `pmic-hp-efuse` | `xaga.dts:1890-1897` |
| 外部时钟：`mt6685-audclk.ko`、`clkbuf.ko` | `modules.load.recovery` |
| DSP offload：`snd_audio_dsp`（`mtk_dsp_voip/primary/offload/deep/playback/music/capture1/a2dp/bledl/dataprovider/call_final/fast` 各带 memory 参数） | `xaga.dts:10127-10150` |
| SCP ultra（低功耗语音）：`snd_scp_ultra` | `xaga.dts:10099` |
| **DTS 里没有** 独立 speaker amp 节点（无 `aw87xx`/`tfa98xx`/`cs35lxx`）→ 推断由 MT6368 内部 Class-D 直驱（**推断，非原文**）| `grep -i 'aw87\|tfa98\|cs35'` 全 DTS 无匹配 |
| 音频分区：`/dev/block/by-name/audio_dsp`（DSP 固件） | 【构建产物】`rd/first_stage_ramdisk/fstab.mt6895:149` |
| 音频 HAL 服务名（其它 MTK/高通机型参考，xaga 未在仓库出现）：`vendor.audio-hal-aidl` | 【代码】`described/annibale/vendor/etc/init/audiohalservice_qti.rc:1` |

### 5.4 显示

- 面板：**6.6"，1080×2460，density 480**；MTK 显示驱动在 144 Hz 会撕裂，**只能稳 60 Hz**。**【代码】** `devices/xaga.toml:18-27`
- 面板驱动模块两个：**`panel-l16-42-02-0a-dsc-vdo.ko`** 和 **`panel-l16-36-02-0b-dsc-vdo.ko`**（两块屏，bootloader 按机型选）。**【构建产物】** `rd/lib/modules/`
  → 命名含义：`l16`=6.6" 代次、`42/36`、`02`、`0a/0b` 版本、`dsc`=DSC 压缩、`vdo`=Video mode。
- 显示驱动链：`mtk_disp_notify.ko` → `mtk_panel_ext.ko` → `mediatek-drm-gateic.ko` → `mediatek-drm-panel-drv.ko` → `mediatek-drm.ko`
  + `mtk-mml-mt6895.ko`、`mtk-mml.ko`、`mdp_drv_mt6895.ko`、`cmdq-platform-mt6895.ko`、`mtk-cmdq-drv-ext.ko`、`cmdq_helper_inf.ko`。**【构建产物】** `modules.load.recovery`
- 背光：`leds-ktz8863a.ko`（TI/ Kinetic **KTZ8863A** 背光 IC，I2C）+ `leds-mtk-disp.ko`/`leds-mtk-pwm.ko`/`leds-mtk.ko`/`pwm-mtk-disp.ko`。**【构建产物】**
- 背光 sysfs：Skyward 先找 `/sys/class/backlight/panel0-backlight`（高通名），找不到退到 `/sys/class/leds/*backlight*`（MTK 名 `lcd-backlight`）。**【代码】** `crates/skyward/src/ui.rs:97-139`
- 亮度换算读 `/sys/module/msm_drm/parameters/dsi_display0` + `/proc/device-tree/soc/.../mi,normal-max-brightness-clone` 等
  —— **这是高通路径，xaga 上会 fallback 到 `max_brightness`**。**【代码】** `crates/skyward/src/ui.rs:163-201`
- DSI/DP：`dsi@14017000`（IRQ 323）、`dp_intf0@1441d000`、`mipi_tx_config@11f70000`。**【构建产物】**
- 亮屏/灭屏：无 `double_click.ko`（双击唤醒）在 recovery 里，但 `modules.load` 有它 → **stock 系统有双击亮屏**。

### 5.5 无线（Wi-Fi / BT）

| 事实 | 证据 |
|---|---|
| 连通性子系统：`consys@18000000`（`mediatek,mt6895-consys`）、`wifi@18000000`、`bt@18000000`、`btif@1100c000`、`connfem@18000000`、`fm@18000000` 全在 **0x18000000** | 【构建产物】`xaga.dts:8984-9080` |
| Wi-Fi 驱动模块：**`wlan_drv_gen4m_6895`**（gen4m = MT6895 专用） | 【代码】`devices/xaga.toml:54` |
| 节点权限：`/dev/wmtWifi` → `wifi:wifi 0660`；`/dev/conninfra_dev` → `system:system 0660` | 【代码】`devices/xaga.toml:58-61` |
| 起 AP 的完整流程（**这是真机验证过的**）：① 挂 `nvdata` 到 `/mnt/vendor/nvdata` ② 起 `conninfra_loader`(system) ③ 起 `wlan_assistant`(wifi) ④ 等 `vendor.mtk.nvram.ready=1` ⑤ 向 `/dev/wmtWifi` 写 `1`，**最多重试 5 次**（首次必失败，因为 calibration 要几十秒） | 【代码】`devices/xaga.toml:50-83`（含注释原文） |
| supplicant：`/vendor/bin/hw/wpa_supplicant`，HIDL 服务名 `android.hardware.wifi.supplicant@1.4::ISupplicant/default` | 【代码】`devices/xaga.toml:76,82` |
| 联网探针：`http://connect.rom.miui.com/generate_204` | 【代码】`devices/xaga.toml:83` |
| MAC 地址在 **`persist`** 分区（`/mnt/vendor/persist`） | 【代码】`devices/xaga.toml:107-109` |
| BT：`mtk-btcvsd-snd`（SCO over PCM）、`btif`；无独立 BT 固件节点在 DTS | 【构建产物】 |
| 日志目录：`/data/vendor/connsyslog` | 【代码】`devices/xaga.toml:248` |

### 5.6 摄像头

- DTS 有完整的 **CAM ISP + 3 组 RAW/YUV + 16×CAMSV + 4×MRAW + SENINF + CCU** 地址（见 2.2 表）。**【构建产物】**
- **没有任何 sensor 型号**（sensor 在 dtbo，`seninf` 的 endpoint 在 dtbo 里绑）。**【构建产物】**，`grep -i 'imx\|ov[0-9]\|s5k\|hi84'` 无匹配。
- 相机相关内核模块：`clk-mt6895-cam.ko`、`clk-mt6895-ccu.ko`。**【构建产物】**
- 小米相机私有：`mtk_composite_v4l2_1/2`、`camera_fsync_ccu`、`camera_camsys_ccu`、`camera_imgsys_ccu`、`camisp_l13/l14/l25/l26`（不同 sensor 组合的 ISP 变体）。**【构建产物】** `xaga.dts:10047-10516`

### 5.7 传感器

- **DTS 里没有 accelerometer/gyro/als/ps 节点**（在 dtbo）。**【构建产物】**
- 只有热敏：`lvts@10315000`（12 个 zone）+ `thermal-ntc1..10` + `thermal-tsx`。**【构建产物】** `xaga.dts:9538-9645`
- 传感器校准数据在 **`persist`** 分区。**【代码】** `devices/xaga.toml:107-109`、`:243`
- 触摸：**Novatek NT36672C**（`nvt36672c.ko` + 小米 `xiaomi_touch.ko`），
  **不要在恢复里改它的固件参数**——`xaga.toml:40-42` 原文：*"xaga's Novatek NT36672C resets its firmware when told to report positions through Xiaomi's touch layer, so nothing is written here."*
- 马达：**AW8697**（awinic），i2c 地址 **0x5a**，sysfs `/sys/bus/i2c/drivers/awinic_haptic/1-005a/f0`（谐振频率，单位 0.1 Hz）。**【代码】** `devices/xaga.toml:44-48`

### 5.8 安全 / TEE（对「日用」很关键）

| 事实 | 证据 |
|---|---|
| MTK TrustZone daemon：`/vendor/bin/teei_daemon`，需 `SYS_RAWIO`，21 个 `-r`/`-t` UUID 参数（**完整列表见原文件**） | 【代码】`devices/xaga.toml:122-150` |
| KeyMint：`/vendor/bin/hw/android.hardware.security.keymint@1.0-service.beanpod`（MTK+小米 "beanpod"），注册 3 个服务 | 【代码】`devices/xaga.toml:154-164` |
| Gatekeeper：`/vendor/bin/hw/android.hardware.gatekeeper@1.0-service` | 【代码】`devices/xaga.toml:176-181` |
| Boot control：`/vendor/bin/hw/android.hardware.boot@1.2-service`（HIDL，非 AIDL） | 【代码】`devices/xaga.toml:184-189` |
| TEE 节点权限：`/dev/teei_client`, `/dev/teei_config`, `/dev/tz_vfs` → system:system 0660；`/dev/ut_keymaster` → system:drmrpc 0660；`/dev/isee_tee0` → system:drmrpc 0666 | 【代码】`devices/xaga.toml:210-226` |
| **UFS RPMB 权限**（见 5.1） | 【代码】`devices/xaga.toml:236-240` |
| VB meta 状态：`androidboot.vbmeta.device_state`、`androidboot.verifiedbootstate` | 【代码】`crates/skyward-info/src/lib.rs:149-176` |

> ⚠️ 这 21 个 UUID 是**明文写在仓库里的**，等于把 xaga 的 TEE 服务白名单公开了。对我们搭 XNU 侧的 TEE 客户端是现成的入口清单。

### 5.9 按键

- `mt6363keys`：**电源键 KEY_POWER(116)** + **home KEY_VOLUMEUP(115)**；`long-press-mode=1`（长按强制重启/关机），`power-off-time-sec=0`。**【构建产物】** `xaga.dts:1261-1276`
- `mtk-pmic-keys.ko`、`mtk-kpd.ko` 在 recovery 里也加载。**【构建产物】** `modules.load.recovery:104-105`

---

## 6. 启动流程与刷机相关

### 6.1 镜像格式事实（这些是我们能直接用的）

| 事实 | 证据 |
|---|---|
| xaga 的 recovery 在 **`vendor_boot`**，格式是 **vendor_boot header v4**（`VNDRBOOT`，page 4096，header_size 2128，含 ramdisk table + bootconfig） | 【构建产物】实测 + `crates/skyward-bootimg/src/vendor.rs:1-5` |
| vendor_boot 的 4 段布局：`header → pad → ramdisk → pad → dtb → pad → [table → pad → bootconfig]` | 【代码】`crates/skyward-bootimg/src/vendor.rs:127-157` |
| 本机实测：`kernel_addr=0x40000000`，`ramdisk_addr=0x66f00000`，`tags_addr=0x47c80000`，`dtb_addr=0x47c80000`，cmdline 只有 `bootopt=64S3,32N2,64N2` | 【构建产物】 |
| bootconfig 段在本镜像里**长度为 0** | 【构建产物】 |
| 打包器只替换 ramdisk，保留 dtb / table 的 `kind`（1=platform, 3=recovery） | 【代码】`vendor.rs:104-125` |
| 镜像末尾必须有 **AVB footer `AVBf`**，否则打包器拒绝 | 【代码】`crates/skyward-pack/src/main.rs:212-226` |
| Skyward 用**未签名 vbmeta**（自己算 digest 铺在 footer 前） | 【代码】`crates/skyward-pack/src/avb.rs:1-6` |

### 6.2 启动链（README 声称 + 代码印证）

```
BootROM → preloader(lk) → LK → [vendor_boot: header+ramdisk+dtb] → kernel(5.10 GKI)
   → 首阶段 mount /system /vendor(erofs, logical, slotselect)
   → init → recovery 服务(system/bin/recovery) → Skyward
```

- **首阶段模块加载**：读 `/proc/cmdline` + `/proc/bootconfig` → 从 `/lib/modules/<release>/modules.load` 或 `modules.load.charger` 加载，
  支持 `modules.dep` / `modules.alias` / `modules.softdep`，命令行参数按 `<module>.<key>=<value>` 分发。
  **【代码】** `crates/skyward-modules/src/modprobe.rs:377-449`
- **`androidboot.mode=charger` 时走 `modules.load.charger`**（关机充电）。**【代码】** 同上 `:402-415`
- **双系统机制**（README）：第二系统根目录是 `/data` 里的一个文件，用 dm-linear 铺成块设备；
  切换只改「下次启动槽位」的 **`boot` 和 `init_boot`** 两个分区，**不动 vendor_boot / dtbo / recovery / vbmeta / super / firmware**；
  每一步先写 `/metadata`，掉电可恢复。**【README】** `README.md:148-198`

### 6.3 刷机 / 重启目标

- Skyward 支持 `reboot` 到：`system`、`recovery`、`fastboot`、`bootloader`、**`EDL`**、关机、切槽。
  **【代码】** `crates/skyward-sys/src/lib.rs:127-155`（写 `/proc/sys/kernel/boot_reason` 风格字符串 `reboot,edl` 等）
- 安装脚本可用的命令：`7za`、`zstd`、`unzip`（`-p` 流式）、`avbctl`、`resetprop`。**【README】** `README.md:32-37`
- `avbctl` 实现在 `crates/skyward-install/src/commands/avbctl.rs`（关 verity/verification），`resetprop` 在 `resetprop.rs`。
- **misc / BCB 布局**（对我们做「从 Android 侧触发切系统」有用）：
  ```
  0..32      command[32]          "boot-recovery" / "boot-fastboot" / "boot-rescue"
  32..64     status[32]
  64..832    recovery[768]        "recovery\n--xxx\n..."
  832..864   stage[32]
  864..2048  reserved[1184]
  16 KiB..32 KiB   WIPE_PACKAGE（A/B 机型放擦除包）
  32 KiB+64 .. 32 KiB+128   MEMTAG_MESSAGE（虚拟 A/B 的 MTE 请求）
  ```
  **【代码】** `crates/skyward-sys/src/bcb.rs:8-25`
- **关机充电判定**：`bootconfig` 含 `androidboot.mode = "charger"` 或 cmdline 含 `androidboot.mode=charger`。**【代码】** `modprobe.rs:402-405`

### 6.4 ⚠️ 对「四次刷机全黑屏」有用的一条

Skyward 的**打包流程会重写 ramdisk 里的 `prop.default`**，原文（`crates/skyward-pack/src/main.rs:346-358`）：

```rust
// On an unlocked device adbd keeps root when ro.secure is 0 and lets any
// computer in without authorization when ro.adb.secure is 0; init lets a
// later line of a property file override an earlier one. update_engine
// refuses a package built before the recovery itself unless both of the
// last two are set; the recovery checks packages against the installed
// system instead.
props.extend_from_slice(b"\nro.secure=0\nro.adb.secure=0\nro.debuggable=1\nro.ota.allow_downgrade=true\n");
```

并且**会删掉 3 个 property**：`ro.build.version.release`、`ro.build.version.security_patch`、`ro.vendor.build.security_patch`
（`BUILD_VERSION`，`main.rs:235, 356`），理由是「恢复的 ramdisk 里的版本号比已装系统旧，安全服务会因此拒绝已录入的密钥」。

> **如果我们的刷机包是自己拼的 vendor_boot，而 `prop.default` 里这三个 property 还是旧值，`teei_daemon`/`keymint` 会拒绝服务**
> ——这解释不了黑屏，但能解释「起来了却卡在某处」。黑屏优先级更高的是 vendor_boot 的 header v4 / ramdisk table / dtb 三段是否对齐。

---

## 7. 对 HakiOS 的具体可用点（按优先级排序）

### P0 —— 立刻能用，直接解卡

| # | 用途 | 素材 | 具体怎么用 |
|---|---|---|---|
| 1 | **给 GPU MCU 补全寄存器模型** | `mali@13000000` + `gpueb@13c00000` 的 `reg`/`reg-names`/`interrupts`/IPI 表（§4.2-4.3） | GPUEB mailbox 是 **5 个 32-bit 寄存器**（`base 0x13c5fd80` len 0x280；`set 0x13c62004`、`clr 0x13c62074`、`send 0x13c62000`、`recv 0x13c62078`），不是队列。先按「写 send → 轮询 recv → 写 clr 清位」实现握手，再发 AUTO。 |
| 2 | **确认 GPUEB 中断是 SPI 516 且与 Mali PWR 共享** | §4.2、§4.3 | 共享中断必须两个 handler 都 ack。XNU 里现在很可能只有一边 ack → 状态机卡死。 |
| 3 | **耳机 accdet 全套参数** | §3.2 的 `headset-mode-setting` 15 个值、3/4 段键门限、`mic-mode=2`、`mic-vol=8`、`adc ch9`、`efuse 0x52/0x0a` | 直接照抄写进 SPMI 寄存器序列；`headset-use-ap-eint=0` 说明**不用 GPIO 中断，走 PMIC 内部中断**，别再等 EINT。 |
| 4 | **accdet 驱动在 `mtk-spmi-pmic.ko`**，不是独立模块 | §3.3 | 只要 SPMI 通了 + `mtk-spmi-pmic.ko` 能 load，accdet 就会 probe。可用它当「SPMI 是否真的通了」的判据。 |
| 5 | **UFS 控制器全参数** | §5.1：`0x112b0000`，IRQ 137，10 时钟 3 复位，`vcc/vccq` 来自 MT6363 `VUFSUFS18/VUFS12` | XNU 侧 UFS 驱动照这个填。 |
| 6 | **UFS RPMB 节点与权限时序** | §5.1 末两行 | secure storage 前必须先 `chown system:system 0660 /dev/rpmb0`（和 `0:0:0:49476`），否则 gatekeeper 全拒。 |

### P1 —— 日用必需，少走弯路

| # | 用途 | 素材 |
|---|---|---|
| 7 | **分区表 / 挂载参数**（尤其 `/data` 的 f2fs + inlinecrypt + wrapped key） | §2.5，`rd/first_stage_ramdisk/fstab.mt6895` |
| 8 | **Wi-Fi 起 AP 的 5 步流程 + 重试策略**（真机验证过） | §5.5，`devices/xaga.toml:50-83` |
| 9 | **充电/电池参数**（CV 4.45V、JEITA 4 段、MT6375 寄存器默认值、bq28z610 电量计） | §5.2 |
| 10 | **背光实际是 KTZ8863A**（不是 MTK 内部 PWM），且 MTK 面板名是 `lcd-backlight` | §5.4 |
| 11 | **调试串口 UART0 = `0x11001000`，IRQ 141，`8250.nr_uarts=4`** | §2.2 —— 黑屏排查的第一入口，比 USB 可靠 |
| 12 | **按键**：电源键 + 音量上（`mt6363keys`，不是 GPIO key） | §5.9 |
| 13 | **TEE 21 个 UUID 白名单 + 服务路径** | §5.8 |

### P2 —— 有就用，没有不阻塞

| # | 用途 | 素材 |
|---|---|---|
| 14 | **GPU OPP 42 档频率/电压表**（950→219 MHz，500–788 mV） | §4.5 |
| 15 | **显示管线全地址表**（两套 mmsys） | §2.4 |
| 16 | **相机 ISP/SENINF/CAMSV 地址**（sensor 型号仍缺） | §2.2 |
| 17 | **`lk_charger` 关机充电参数**（做 charger mode 时需要） | §5.2 末 |
| 18 | **BCB/misc 偏移布局**（从 Android 侧触发切换） | §6.3 |
| 19 | **Skyward 的 `system-init` 首阶段模块加载算法**（可直接照抄到 XNU 的 kext 预加载） | `crates/skyward-modules/src/modprobe.rs:377-449` |
| 20 | **`skyward-pack described` 工具**：能从 OTA zip 里按 Range 只抽 `etc/init`/`etc/vintf`/`build.prop`，不下载整包 | `crates/skyward-pack/src/described.rs:1-18` |

### P3 —— 方法论（不是数据，但值钱）

| # | 教训 |
|---|---|
| 21 | **官方 OTA zip 是 5.98 GB 但支持 Range + 里面的 payload 可随机读**——`unpayload` + `rust-fs-erofs` 能只下载需要的几十 MB。我们可以用同一招把 `dtbo`/`super` 里的 vendor 分区掏出来。 |
| 22 | **vendor_boot 的 ramdisk 就是一台完整 MTK 机器的驱动快照**（198 个 .ko + fstab + prop + sepolicy）——以后任何 xaga 问题，先 `unlz4` 它。 |
| 23 | **`xaga.toml` 的注释是「真机实测结论」而不是文档猜测**（"the first write of a boot is answered with an error while the connectivity part runs its calibration, which takes tens of seconds"）——这类注释比代码更可信。 |

---

## 8. 明确没有的东西

| 缺什么 | 确认方式 |
|---|---|
| **任何 `.dts`/`.dtsi`/`.dtb` 源文件** | `find` 全仓为空；DTB 只能从 release 镜像解 |
| **内核源码 / Kconfig / defconfig** | 同上；无 `.c`/`.h` |
| **accdet / MT6368 的寄存器偏移定义** | 驱动是闭源 `.ko`；`strings` 只有名字没有偏移 |
| **Mali-G610 / GPU 驱动代码、CSF 固件** | 全仓无 GPU 代码；`mali` 命中全是误匹配 |
| **耳机（headset/jack/3.5mm）相关源码** | `accdet`/`mt6368`/`3.5mm` 全仓 0 命中；`headset`/`jack` 全在别机型测试数据 |
| **触摸屏 / 传感器 / 摄像头 sensor 型号与寄存器** | 在 `dtbo` 分区，这个包里没有 |
| **板级 DTS**（xaga 真正的板级差异） | 本 DTB 是 MT6895 公共平台 DTB（含 `xagapro_charger` 共存） |
| **完整 reserved-memory**（TEE/ATF/SSPM/GPU 保留区） | 只有 3 条，明显被裁剪 |
| **性能调优 / 省电配置** | 无 governor/task_profiles 调优（`task_profiles.json` 只是让 logd 能启动的最小集）；无 thermal profile 调参 |
| **bootloader / preloader / LK 源码或文档** | 无 |
| **刷机文档**（除 README 的 3 行 fastboot 命令） | 只有 `README.md:126-146` |
| **accdet 的 sysfs/proc 调试节点名** | 仓库与 .ko strings 都没有 |
| **xaga 的 DTBO 镜像** | 不在 release 包里；要另从 OTA 的 `dtbo` 分区或整机 dump 取 |

---

## 附录 A：本次解出来的产物位置（都在 /tmp，非 /home/lizi/IOS）

```
/tmp/xaga-skyward/                                    # git clone（源码）
/tmp/xagarelease/rel.zip                              # release zip 原件（54 MB）
/tmp/xagarelease/out/skyward-v0.3.3-xaga-OS1.0.3.0.ULOCNXM.img   # 64 MiB vendor_boot
/tmp/xagarelease/out/ramdisk.bin                      # LZ4 (64 MB)
/tmp/xagarelease/out/ramdisk.raw                      # cpio (117 MB)
/tmp/xagarelease/out/rd/                              # 解开的 ramdisk 根
/tmp/xagarelease/out/dtb.bin                          # DTBO 容器 (327 KB)
/tmp/xagarelease/out/xaga.dtb                         # 单条 FDT
/tmp/xagarelease/out/xaga.dts                         # 反编译, 11,583 行
```

## 附录 B：一条命令重建全部结论

```sh
source ~/.dsh/env.sh
curl -L -o /tmp/x.zip https://github.com/BkaNeko/xaga-skyward/releases/download/v0.3.3/skyward-v0.3.3-xaga-OS1.0.3.0.ULOCNXM.zip
unzip -o /tmp/x.zip -d /tmp/xo
python3 - <<'EOF'
import struct
p='/tmp/xo/skyward-v0.3.3-xaga-OS1.0.3.0.ULOCNXM.img'
f=open(p,'rb'); b=f.read(4096); u=lambda o: struct.unpack_from('<I',b,o)[0]
page,hdr,rsz,dsz=u(12),u(2096),u(24),u(2100)
ram=((hdr+page-1)//page)*page; dtb=((ram+rsz+page-1)//page)*page
f.seek(ram); open('/tmp/rd.lz4','wb').write(f.read(rsz))
f.seek(dtb); d=f.read(dsz)
m,t,h,e,c,o,pg,v=struct.unpack_from('>8I',d,0)
sz,off,_,_=struct.unpack_from('>4I',d,o)
open('/tmp/xaga.dtb','wb').write(d[off:off+sz])
EOF
lz4 -d -c /tmp/rd.lz4 > /tmp/rd.cpio && mkdir -p /tmp/rd && (cd /tmp/rd && cpio -idm --quiet < /tmp/rd.cpio)
dtc -I dtb -O dts -o /tmp/xaga.dts /tmp/xaga.dtb
grep -n -A22 'accdet {' /tmp/xaga.dts | head -30
```
