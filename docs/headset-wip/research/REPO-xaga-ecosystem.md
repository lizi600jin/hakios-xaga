# xaga 开源生态调研（不含 MT6895-Mainline）

调研时间：2026-11-14 · 执行目录 `/tmp/repo-search`（克隆）+ `/home/lizi/IOS/work-xaga`
范围：**除 `MT6895-Mainline/*` 以外的全部 xaga 相关公开仓库**。

**证据标注约定**（全文统一）

- **[源码]** = 我实际读到文件内容，给出 `仓库/路径:行号`
- **[镜像]** = 从本机已有的真机 dump / 原厂固件包直接解析，给出文件名与偏移
- **[自述]** = 仓库 README/docs 的自述，未独立复核
- **[未找到]** = 明确搜过但没有

> 本机已有素材（非本次克隆，但被引用）：
> `/home/lizi/IOS/work-xaga/k-xaga`（XagaForge kernel 17.0 的 sparse 克隆，5.10.269）、
> `/home/lizi/IOS/work-xaga/dump/`（真机 dump：`xaga_live.dts`、`lk_live.dts`、`iomem.txt`、`byname.txt`、`cmdline.txt`、`boot_a.img`、`vendor_boot_a.img`、`dtbo_a.img`）、
> `/home/lizi/IOS/work-xaga/rom/MT6895_Android_scatter.txt`。

---

## 0. 仓库清单

### 0.1 有硬件价值（按价值排序）

| 全名 | 用途 | 最近更新 | 硬件价值 |
|---|---|---|---|
| **`XagaForge/android_kernel_xiaomi_mt6895`**（branch `17.0`）| LineageOS 24 (A17) 的 xaga/MT6895 内核源码，5.10.269 | 2026-11-11 | ★★★★★ **本次唯一源码级金矿**：完整 `mt6368-accdet.c`(3609 行) + `mt6368.h`(5974 行，含 257 个 ACCDET 寄存器位域) + `xaga.dts`/`xaga-mt6895.dtsi`/`mt6368.dtsi`/摄像头。本地已有 sparse 克隆，我用 `git cat-file` 取出了未 checkout 的文件 |
| **`XagaForge/android_device_xiaomi_xaga`**（`17.0`）| xaga 设备树（A17） | 2026-10-17 | ★★★★ 分区/内核模块清单/init |
| **`XagaForge/android_device_xiaomi_mt6895-common`**（`17.0`）| MT6895 公共设备树 | 2026-10-31 | ★★★★★ **boot image v4 全部偏移**、super 分区、AVB、Wi-Fi/BT、模块加载列表；本地已有克隆 |
| **`xiaomi-mt6895-devs/android_device_xiaomi_xaga`**（`lineage-24.0`）| 官方 LineageOS 24 设备树 | 2026-10-11 | ★★★★ 与上者同源（commit 相同 `6a08306`），modules.load 172 条 |
| **`LineageOS/android_device_xiaomi_xaga`** | LineageOS 官方镜像仓库 | 2026-10-11 | ★★★ 同上（内容一致） |
| **`dimensity-twrp-dev/device_xiaomi_xaga-TWRP`**（`twrp-12.1_5.10.66`）| TWRP 设备树 | 2026-10-17 | ★★★ recovery.fstab、`cmdline`、`dtb` 预编译 |
| **`citie114514/xaga-windows-arm64-kvm`** | 在 xaga 上用 KVM 跑 Win11 ARM64 的完整记录（含 ATF 替换）| 2026-10-24 | ★★★★ **preloader 逆向结论**（SBC 来自 eFuse word 0x1F bit1）、刷机/回退流程、vendor_boot 软编补丁、`lk_a.img` 存档 |
| **`Void1315/xaga-beanpod-keybox-rkp-research`** | xaga TEE / Beanpod / RPMB 研究 | 2026-11-07 | ★★★★ **RPMB 容量 16 MiB、TEE 参数物理地址 `0x7c600000`、keybox 分区 LBA 0x36af**、TA 目录 |
| **`zhaoqing412/xaga-mt6895-k6.12`** | xaga 6.12 内核移植 | 2026-11-08 | ★★★★ `docs/05-dts-driver-matrix.md`（35 KB）逐节点列出 **49 个 compatible 的驱动齐备性** + `docs/09-flashing.md`（镜像大小实测）|
| **`BkaNeko/xaga-mali-r54-port`** | Mali-G610 r32p1→r54p1 + GED 6.12 backport | 2026-11-10 | ★★★ GPU 相关地址与 DVFS 细节（`0x13000000` mali、`0x13c40000` gpueb）|
| **`ESK-Project/android_kernel_xiaomi_mt6895`** | ESK 内核（xaga/in/pro），5.10.272 | 2026-11-12 | ★★★ **6 个 xaga 摄像头驱动完整源码 + I2C 地址 + sensor ID**（本次唯一拿到这些的地方）|
| `Jefino9488/XAGA-builder` | recovery-rom 转换/修补自动化 | 2026-11-12 | ★★ 刷机流程工具 |
| `xiaomi-mt6895-devs/fenrir` | MTK 启动链 PoC（已归档）| 2026-10-01 | ★★ **`bl2_ext` 免校验漏洞**（EL3 代码执行）+ boot chain 图 |
| `TheMuppets/proprietary_vendor_xiaomi_xaga` | 闭源 blob | 2026-10-05 | ★★ 需要 blob 时用 |
| `Rohail33/Realking_xiaomi_xaga` | 第三方内核（2.1 GB）| 2026-11-11 | ★ 未逐文件读 |
| `dereferencex/android_kernel_gki_common_5.10`（`ZenX`）| 第三方 GKI 内核 | 2026-10-19 | ★ 未逐文件读 |
| `Mandi-Sa/micode-xaga-s-oss` | 小米官方 xaga 内核存档（小米已下架）| 2026-06-14 | ★★★ **原厂 5.10 内核的另一份拷贝**，可交叉验证 |
| `mt6895-plato/android_device_xiaomi_mt6895-common` | 同族（plato）设备树 | 2026-10-29 | ★★ 可对比 |
| `sheikhmehraann/X6871-iOS-Port` | Infinix GT 20 Pro（同 MT6895）的 XNU 移植 | 2026-10-31 | ★★★ 同 SoC 的 XNU 移植（另一个父代理可能在查）|

### 0.2 无硬件价值 / 无关

`reedable/xaga`（RSS 阅读器同名）、`bunqii/OANDA_XAGAUD_SilverAUD`、`arch-creator/miraiXagami_UpdATED`、
`Xanz-crypto/XAGameSpace-`、`itsrohan17/Lunaris-Aosp-Xaga-website`、`franklinsamuel0/xaga`（content）、
`paulsteve8/xagad`（content）、`306266563/xaga-lagfix`（Magisk 模块）、`Angxddeep/xaga-5g-sa-enabler`。

### 0.3 组织全貌（本次枚举结果）

| 组织 | 仓库 |
|---|---|
| `XagaForge` | `android_kernel_xiaomi_mt6895`(17.0)、`android_device_xiaomi_xaga`(17.0)、`android_device_xiaomi_mt6895-common`(17.0)、`android_vendor_xiaomi_mt6895-common`(17.0)、`android_device_mediatek_sepolicy_vndr`、`android_hardware_mediatek`、`android_hardware_xiaomi`、`android_system_core`、`android_build_soong`(16.2)、`proprietary_vendor_xiaomi_miuicamera-xaga` |
| `xiaomi-mt6895-devs` | `android_kernel_xiaomi_mt6895`(lineage-24.0)、`android_device_xiaomi_xaga`、`android_device_xiaomi_mt6895-common`、`proprietary_vendor_xiaomi_{xaga,mt6895-common}`、`android_kernel_xiaomi_mt6895-devicetrees`(23.2)、`releases`、**`fenrir`** |
| `ESK-Project` | `android_kernel_xiaomi_mt6895`(main)、`esk_builder`、`AnyKernel3`(xaga)、`esk-releases`、`ReSukiSU`、`android12-5.10-gki`、`gki_kernel_builder` |
| `MT6895-Mainline` | **（任务边界：不查）** |
| `dimensity-twrp-dev` | `device_xiaomi_xaga-TWRP`、`rubens-TWRP`、`matisse-TWRP`、`daumier-TWRP` |

---

## 1. 设备树事实汇总

### 1.1 DTS 文件拓扑（xaga，来自 XagaForge kernel `17.0`）

```
arch/arm64/boot/dts/vendor/mediatek/xaga.dts                    (11 行，入口)
├── mediatek/xaga-mt6895.dtsi                                   (624 行，板级)
│   ├── mediatek/xiaomi-mt6895-common.dtsi                      (788 行，小米公共)
│   ├── mediatek/xaga_mt6895_camera_v4l2.dtsi                   (736 行)
│   ├── mediatek/xiaomi-mt6895-xaga-display.dtsi                (162 行)
│   └── mediatek/xaga-mt6895-touch.dtsi                         (112 行)
└── cust/xaga.dtsi                                              (399 行，DrvGen 生成：GPIO/EINT/keypad)
基座：mediatek/mt6895.dts                                       (15316 行，SoC 级)
PMIC：mediatek/mt6368.dtsi                                      (298 行)
```

真机 live DTS（`/home/lizi/IOS/work-xaga/dump/xaga_live.dts`，11583 行）与 `lk_live.dts` **完全相同**（md5 `564b0f45e6225acd05ae086316718259`）——即 LK 传给内核的就是这一份。

### 1.2 SoC 级关键节点（`mt6895.dts`，XagaForge kernel `17.0`）

| 节点 | compatible | reg | interrupts | 备注 |
|---|---|---|---|---|
| `gic` | `arm,gic-v3` | dist `0x0c000000` size `0x40000`；redist `0x0c040000` size `0x200000` | `GIC_PPI 9 LEVEL_HIGH` | `#redistributor-regions = <1>`；`#interrupt-cells = <4>`；ppi_cluster0=A55×4，ppi_cluster1=A78×4 —— `mt6895.dts:560-580` [源码] |
| `timer` | `arm,armv8-timer` | — | PPI 13/14/11/10 | **`clock-frequency = <13000000>`（13 MHz）** —— `mt6895.dts:582-590` [源码] |
| `psci` | `arm,psci-1.0` | — | — | `method = "smc"` —— `mt6895.dts:474-477` [源码] |
| `hwrng` | `arm,sec-rng` | — | — | `methods="smc"`, `method-fid=0x26a`, `quality=900` —— `mt6895.dts:592` [源码] |
| `ufshci` | `mediatek,mt8183-ufshci` | `0x112b0000` size **`0x2300`** | `GIC_SPI 137 LEVEL_HIGH` | 见 §4 —— `mt6895.dts:5247-5290` [源码] |
| `ufs_ao_config_clk` | `mediatek,mt6895-ufs_ao_config`,`syscon`,`simple-mfd` | `0x112b8000` size `0x1000` | — | 内含 `ufsaocfg_rst`（`ti,syscon-reset`，unipro reset = `0x44 bit1 / 0x48 bit1`）—— `mt6895.dts:5093-5108` [源码] |
| `ufs_pdn_cfg_clk` | `mediatek,mt6895-ufs_pdn_cfg`,`syscon`,`simple-mfd` | `0x112bc000` size `0x1000` | — | 内含 `ufspdncfg_rst`：`0x44 bit0/0x48 bit0` = ufs-crypto，`0x44 bit1/0x48 bit1` = hci —— `mt6895.dts:5110-5128` [源码] |
| `spmi` | `mediatek,mt6895-spmi` | pmif `0x1c804000` size `0x8ff`；spmimst `0x1c801000` size `0x100` | `&pio 243`（rcs_irq）+ `GIC_SPI 414`（pmif_irq） | `swinf_ch_start=10`, `ap_swinf_no=2`；`#interrupt-cells=1` —— `mt6895.dts:838-860` [源码] |
| `spmi_pmif_mpu` | `mediatek,mt6895-spmi_pmif_mpu` | `0x1c804900` size `0x500` | — | `mediatek,pmic_all_rgn_en = <0x020c2800>` —— `mt6895.dts:861-866` [源码] |
| `mt6363`（`pmic@4`）| `mediatek,mt6363` | SPMI USID 4 | — | 主 PMIC；提供 `mt6363_vemc`(vcc of UFS)、`mt6363_vufs12`(vccq)、`mt6363_vufs18` —— live DTS `:1117-1160`、`:1517`、`:1591` [镜像] |
| `mt6368`（`second_pmic@5`）| `mediatek,mt6368` | SPMI USID 5 | — | 副 PMIC：accdet / auxadc / efuse / codec / consys / 22 路 regulator —— `mt6368.dtsi:9-16` [源码] |

CPU 拓扑（`mt6895.dts:170-300` [源码]）：`cpu@000..003` = cortex-a55，`reg = 0x0000/0x0100/0x0200/0x0300`，`clock-frequency = 1701000000`，capacity 380；
`cpu@100..200` = cortex-a78，`reg = 0x0400/0x0500/0x0600/0x0700`，`clock-frequency = 2171000000`，capacity 1024。
`enable-method = "psci"`，`performance-domains` 分别是 `&performance 0/1/2`。

### 1.3 板级节点（`xaga-mt6895.dtsi`，XagaForge kernel `17.0`）

| 行号 | 节点/标签 | compatible | reg / 参数 | 总线 |
|---|---|---|---|---|
| 14 | `&u2port0` | — | `mediatek,eye-term=3`、`eye-vrt=3`、`rev6=2`、`discth=0xb`（**USB2-only PHY 调参**）| USB |
| 24-29 | `&odm/simtray` | `xiaomi,simtray-status` | `status-gpio = <&pio 42 0>` | GPIO42 |
| 32-63 | `&pio` ln8000 中断脚 | — | master = GPIO13，slave = GPIO14，`bias-pull-up = <11>` | — |
| 64-126 | `&i2c9` | — | 400 kHz | I2C9 |
| ↳ 70 | `sc8551_i2c9@66` | `sc8551_i2c9` | `0x66`，`irq_gpio=GPIO13`，`ac_ovp=13000` `bus_ovp=12300` `bus_ocp=4500` `bat_ovp=5000` `bat_ocp=8000` | I2C9 |
| ↳ 81 | `sc8561_master@64` | `sc8561_master` | `0x64`，`irq_gpio=GPIO13` | I2C9 |
| ↳ 86 | `ln8000@51` | `lionsemi,ln8000` | `0x51`，`charger_name="tertiary_chg"`，`bat-ovp=4660` `bus-ovp=13000` `bus-ocp=3750` `ntc-alarm-cfg=226` | I2C9 |
| 128-213 | `&i2c7` | — | 400 kHz | I2C7 |
| ↳ 131 | `bq28z610@55` | `bq28z610` | `0x55`，`normal_shutdown_vbat_1s=3400` `critical=3310` `cool_critical=3290` `report_full_rsoc_1s=9700` | I2C7 |
| ↳ 156 | `sc8551_i2c7@66` | `sc8551_i2c7` | `0x66`，`irq_gpio=GPIO14` | I2C7 |
| ↳ 167 | `sc8561_slave@65` | `sc8561_slave` | `0x65`，`irq_gpio=GPIO14` | I2C7 |
| ↳ 171 | `ln8000-secondary@51` | `lionsemi,ln8000-secondary` | `0x51`，`charger_name="quaternary_chg"` | I2C7 |
| 215-267 | `&charger` | （基座 `mediatek,charger`）| `max_charger_voltage=12 V` `fv=4450` `fv_ffc=4480` `iterm=200` `pd2_input_current=1.7 A`；**JEITA 6 段**、`step_chg_cfg_low/high_cycle`、`thermal_limit_pd = <12400 10000 9000 ...>` | — |
| 269-322 | `&xagapro_charger` | `mediatek,xagapro_charger` | `max_fcc=21500`、`max_vbus=21 V`、`iterm_ffc=2160`、`thermal_limit_pd` 首档 **21500 mA** | — |
| 324-341 | `&lk_charger` | `mediatek,lk_charger` | `ta_ac_charger_current=3000000`、`pre_charger_current=250000`、`temp_t4=60 / t3=45 / t1=-10`、`boot_battery_voltage=3600000` | LK |
| 343 | `&mtk_gauge` | — | `DISABLE_MTKBATTERY = <1>`（**不用 MTK 内置电量计，改用 bq28z610**）| — |
| 347-370 | `&pd_cp_manager` | `pd_cp_manager` | `max_fcc=12400` `max_vbus=12 V` `max_ibus=6200` `cv_vbat=4440` `cv_vbat_ffc=4470` `min_pdo_vbus=8000` `max_pdo_vbus=11000` | — |
| 372-394 | `&xagapro_PCM` | `xagapro_PCM` | `max_fcc=21500` `max_vbus=21 V` `cv_vbat_ffc=4490` `mt6375_control_gpio = <&pio 11 0>` | — |
| 396-404 | `&mtee_svp/MTEE` | `mediatek,mtk_svp_on_mtee_support` | — | — |
| 396-404 | `&mtee_svp/SecureVideoPath` | `mediatek,mtk_sec_video_path_support` | — | — |
| 406-411 | `&pmic_adc/vin3_open` | — | `channel = <AUXADC_VIN3>` = **0x15**，`pures = ADC_PURES_100K` | — |
| 413-585 | `&tboard_thermistor8` | `generic-adc-thermal` | `io-channels = <&pmic_adc AUXADC_VIN3>` + **175 点查表**（-40 ℃…125 ℃，-40→1760，0→1406，25→900，125→44） | — |
| 587-594 | `&thermal_zones/backlight_therm` | — | `thermal-sensors = <&tboard_thermistor8>` | — |
| 596-604 | `&spk1` / `&spk2` | `goodix,tfa9874`（在 common.dtsi）| `spk1`: irq=GPIO19, spk-sw=GPIO160；`spk2`: irq=GPIO15, **`/delete-property/ spk-sw-gpio`** | I2C6 |
| **607-615** | **`&accdet`** | `mediatek,mt6368-accdet` | **`accdet-mic-vol = <8>`（2.8 V）**、**`accdet-mic-mode = <2>`（DCC，无内部 bias）** | SPMI |
| 617-621 | `&aw8697_haptic` | `awinic,awinic_haptic` | `aw86927_vib_bst_vol_{default,ram,rtp} = 0x53` | I2C1 |

### 1.4 `xiaomi-mt6895-common.dtsi` 里的器件（788 行）

| 节点 | compatible | 地址 | 备注 | 行号 |
|---|---|---|---|---|
| `/odm/vibrator@0` | `mediatek,vibrator` | — | `vib_timer=25 vib_limit=9 vib_vol=9`（**遗留节点，真震动走 aw8697**）| 24-30 |
| `/odm/xiaomi_touch` | `xiaomi-touch` | — | 触摸 feature/HAL 通道 | 33-39 |
| `&i2c6/spk1` | `goodix,tfa9874` | `0x34` | `reset-gpio=GPIO213` `irq-gpio=GPIO20`，`sound-name-prefix="Left"` | 307-315 |
| `&i2c6/spk2` | `goodix,tfa9874` | `0x35` | `reset-gpio=GPIO213` `spk-sw-gpio=GPIO160` `irq-gpio=GPIO25`，`"Right"` | 316-324 |
| `&i2c1/aw8697_haptic@5a` | `awinic,awinic_haptic` | `0x5A` | `reset-gpio=GPIO117` `irq-gpio=GPIO127`，`vib_f0_pre=1700` `vib_cont_drv_lev=60` | 528-560 |
| `&i2c5/rt5133@18` | `richtek,rt5133` | `0x18` | 8 路 LDO，`enable-gpio` | live DTS |
| `&i2c5/rt6160@75` | `richtek,rt6160` | `0x75` | buck-boost | live DTS |
| `&i2c5/mt6375@34` | `mediatek,mt6375` | `0x34` | **充电器 + TypeC + gauge 三合一**（`mtk_gauge` 子节点 compatible `mediatek,mt6375-gauge`）| live DTS `:4150`、`:11174` |
| `&i2c6/gate_ic@11` | `mediatek,MI_I2C_LCD_BIAS` | `0x11` | 背光 bias（驱动 `leds-ktz8863a.c` / 6.12 叫 `leds-ktz8866.c`），`pwm_config=<0 1 0 0 0>` | display.dtsi:16-25 |

### 1.5 触摸（`xaga-mt6895-touch.dtsi`，112 行）[源码]

| 项 | 值 |
|---|---|
| 控制器 | `&spi2`，`mediatek,pad-select = <0>`，`mediatek,kthread-rt = <1>` |
| 器件 | `novatek@0`，`compatible = "novatek,NVT-ts-spi"`，`reg = <0>`（CS0）|
| SPI 频率 | `spi-max-frequency = <13000000>`（13 MHz）|
| 引脚 | `SPI2_A_MI=GPIO109`、`SPI2_A_CSB=GPIO110`、`SPI2_A_MO=GPIO111`、`SPI2_A_CLK=GPIO112`；IRQ = **GPIO135**（`novatek,irq-gpio = <&pio 135 0x2001>`），EINT 类型 `IRQ_TYPE_EDGE_FALLING`（`cust/xaga.dtsi:321`）|
| 特殊地址 | `novatek,swrst-n8-addr = <0x03F0FE>`，`novatek,spi-rd-fast-addr = <0x03F310>` |
| **两个面板批次（按 TP vendor 区分）** | `cfg_0`: `tp-vendor=0x46` `display-maker=0x36` → `novatek_nt36672e_l16_fw01.bin` / `mp01.bin`，`clicknum="nvt+tianma"`<br>`cfg_1`: `tp-vendor=0x53` `display-maker=0x42` → `..._fw02.bin` / `mp02.bin`，`"nvt+csot"` |

### 1.6 显示（`xiaomi-mt6895-xaga-display.dtsi`，162 行）[源码]

| 项 | 值 |
|---|---|
| 背光 | `&mtk_leds`（`mediatek,disp-leds`），`led_mode=4`，`max-brightness=4095`，`label="lcd-backlight"` |
| 背光 IC | `gate_ic@11` on I2C6，`mediatek,MI_I2C_LCD_BIAS` |
| 面板 1 | `panel1@0`，`compatible = "l16_42_02_0a_dsc_vdo,lcm"`，`reset-gpios=GPIO41`、`dvdd-gpios=GPIO118`、`cam-gpios=GPIO158`、`leden-gpios=GPIO215`、`vrf18-supply = <&mt6368_vrf18_aif>` |
| 面板 2 | `panel2@0`，`compatible = "l16_36_02_0b_dsc_vdo,lcm"`，`reg = <1>`，引脚同上 |
| DSI TE | `GPIO40`，`PINMUX_GPIO40__FUNC_DSI_TE`，EINT `IRQ_TYPE_EDGE_RISING` |
| AAL | `disp_aal0/aal1` 均 `mtk_dre30_support = <1>` |
| 分辨率 | 1080×2460，6.6 英寸，30/48/50/60/90/120/144 Hz —— **来自 `dt-device_xiaomi_xaga-TWRP/README.md:42` [自述]，DTS 里没有分辨率字段**（panel 驱动 `panel-l16-*-dsc-vdo.c` 里写死，本次未读该文件）|

### 1.7 摄像头（`xaga_mt6895_camera_v4l2.dtsi`，736 行）[源码]

| sensor | 节点 | compatible | I2C 总线 @ 速率 | DTS reg | **驱动内实际 i2c 写地址** | sensor ID | 供电（`fan53870_lN`）|
|---|---|---|---|---|---|---|---|
| sensor0 | `sensor0@10` | `mediatek,imgsensor0` | i2c8 @ **1 MHz** | `0x10` | **`0x20`** | `0xF971` | `:691` dovdd=`fan53870_l7`、`afvdd=fan53870_l6`；EEPROM `0x51`；VCM `dw9800v@0x0c` |
| sensor1 | `sensor1@10` | `mediatek,imgsensor1` | i2c2 @ 400 kHz | `0x10` | **`0x20`** | `0x1641` | `:554` dovdd=`l7`、`:555` avdd=`fan53870_l3`；EEPROM `0x51` |
| sensor2 | `sensor2@37` | `mediatek,imgsensor2` | i2c4 @ 400 kHz | `0x37` | **`0x6e`**（GC02M1）/ **`0x78`**（OV02B10）| `0x2E0` / `0x002B` | `:616` avdd=`fan53870_l5`；EEPROM `0x52` |
| sensor3 | `sensor3@2d` | `mediatek,imgsensor3` | i2c2 @ 400 kHz | `0x2d` | **`0x5a`** | `0x487B` | `:496` dovdd=`l7`、`:497` avdd=`fan53870_l4`；EEPROM `0x50` |

> ⚠️ **DTS `reg` 与驱动 `i2c_addr_table` 不一致**（`0x10` vs `0x20`、`0x37` vs `0x6e`…）。原因**读过源码确认**：
> MTK imgsensor 框架发 I2C 时**不用 `i2c_client->addr`**，而是把 `ctx->i2c_write_id`（来自驱动 `i2c_addr_table`）直接填进 `i2c_msg.addr`：
> `drivers/misc/mediatek/imgsensor/src-v4l2/adaptor-i2c.c:35,40,66,71,100,105,146,170,200`（`msg[0].addr = addr;` …），
> `ctx->i2c_write_id` 在 `*_mipi_raw/*Sensor.c` 的 open 里由 `while (imgsensor_info.i2c_addr_table[i] != 0xff)` 逐个尝试（如 `xagas5kgw1mipiraw_Sensor.c:835-836`）。
> → **DTS 的 `reg` 只是节点定位用，真正通信地址看驱动表**。做 HakiOS 驱动时应以 `i2c_addr_table` 为准。

摄像头外围：
- `fan53870`（`onsemi,fan53870`）on **i2c9 `0x35`**，7 路 LDO：l1 0x0000 / l2 0x0001（528 mV–1.504 V，650 mA），l3 0x0002 / l4 0x0003 / l5 0x0004 / l6 0x0005 / l7 0x0006（1.504–3.4 V，650 mA）—— `xaga_mt6895_camera_v4l2.dtsi:303-386` [源码]
- 闪光灯 `ktd2687`（`mediatek,ktd2687`）on **i2c6 `0x63`**，两路 `flash@0` / `flash@1` —— 同上 `:387-462`
- `seninf_csi_port_0..3`（`mediatek,seninf`），mapping：sensor0→CSI port 2、sensor1→port 3、sensor2→port 3、sensor3→port 1（dtbo `fragment@31`）[镜像]

### 1.8 EINT / GPIO 映射（`cust/xaga.dtsi`，DrvGen 生成）[源码]

| 功能 | GPIO | 触发 | 行号 |
|---|---|---|---|
| `mrdump_ext_rst` | 12 | `LEVEL_LOW` | 297-302 |
| `smart_pa`（TFA9874 中断）| 19 | `LEVEL_LOW` | 303-307 |
| `dsi_te` | 40 | `EDGE_RISING` | 309-313 |
| `subpmic_pmu_eint`（MT6375）| **116** | `LEVEL_LOW` | 315-319 |
| `touch` | **135** | `EDGE_FALLING` | 321-325 |
| `fingerprint` | **136** | `EDGE_RISING` | 327-331 |
| `rt5133_eint` | **141** | `EDGE_FALLING` | 333-343 |
| `md1_sim1_hot_plug_eint` | — | `LEVEL_LOW`, `debounce=10000`, `src_pin=1` | 345-353 |
| `md1_sim2_hot_plug_eint` | — | `LEVEL_LOW`, `debounce=10000`, `src_pin=2` | 355-363 |

按键（`&keypad`，`cust/xaga.dtsi:381-395`）：`sw-pwrkey=116`、`hw-pwrkey=8`、`sw-rstkey=115`、`hw-rstkey=17`、`hw-recovery-key=17`、`hw-dl-key0=17`、`hw-dl-key2=8`、`debounce=1024`、`hw-map-num=72`，`hw-init-map` 首项 = 114（KEY_VOLUMEDOWN）。

### 1.9 其他板级器件（来自 dtbo 反编译，`dtbo_a.img` → `xaga_ov0.dts`）[镜像]

| 节点 | compatible | 地址 | 要点 |
|---|---|---|---|
| `nxp@28`（NFC）| `qcom,nq-nci` | i2c3 `0x28`，bus 400 kHz（`0x33e140`）| `qcom,nq-irq=GPIO143`、`nq-ven=GPIO18`、`nq-firm=GPIO128`、`nq-clkreq=GPIO194` |
| `p61@31`（eSE）| `nxp,p61` | spi1 `reg=<0>`（CS0），`spi-max-frequency = 0x7a1200` = **8 MHz** | `pinctrl-0 = nxp_ese_spi_mode`（GPIO 25/26/27/28 → `0x1901/0x1a01/0x1b01/0x1c01`）|
| `spi3/fingerprint@0` | `goodix,goodix-fp` | `reg=<0>`，8 MHz | `netlink-event=30`，`power-type=1`，`vfp/vibr/vufs18-supply` |
| `/fingerprint` | `mediatek,fpc1022` / `mediatek,goodix-fp` | — | `fpc,gpio_irq`（GPIO136）|
| `accdet`（dtbo `fragment@61`）| — | — | 只覆写 `accdet-mic-vol=<8>` `accdet-mic-mode=<2>`（= 源码 dtsi 的同一值，冗余）|

---

## 2. 分区表与 boot image 格式

### 2.1 分区表（`/home/lizi/IOS/work-xaga/rom/MT6895_Android_scatter.txt`，3312 行）[镜像]

scatter 里**同时含 EMMC 与 UFS 两套布局**（各 91 个分区，共 182 块）。xaga 是 **UFS** 机器，下面只列 UFS 那套。
（`/home/lizi/IOS/work-xaga/xaga_scatter.xml` 是 ItsVixano 改过的版本：默认不刷 preloader + 双槽都刷。）

| # | 分区 | 大小 | 起始字节 (LBA×512) | scatter type |
|---|------|------|------------------|--------------|
| 0 | `preloader` | 0x100000 = 1024 KiB | 0x0 | SV5_BL_BIN |
| 1 | `preloader_backup` | 0x100000 = 1024 KiB | 0x0 | NORMAL_ROM |
| 2 | `pgpt` | 0x8000 = 32 KiB | 0x0 | NORMAL_ROM |
| 3 | `misc` | 0x400000 = 4096 KiB | 0x8000 | NORMAL_ROM |
| 4 | `backup1` | 0x3f8000 = 4064 KiB | 0x408000 | NORMAL_ROM |
| 5 | `protect1` | 0x800000 = 8192 KiB | 0x800000 | EXT4_IMG |
| 6 | `protect2` | 0x800000 = 8192 KiB | 0x1000000 | EXT4_IMG |
| 7 | `nvram` | 0x4000000 = 65536 KiB | 0x1800000 | NORMAL_ROM |
| 8 | `proinfo` | 0x300000 = 3072 KiB | 0x5800000 | NORMAL_ROM |
| 9 | `nvcfg` | 0x2000000 = 32768 KiB | 0x5b00000 | EXT4_IMG |
| 10 | `persist` | 0x4000000 = 65536 KiB | 0x7b00000 | EXT4_IMG |
| 11 | `countrycode` | 0x200000 = 2048 KiB | 0xbb00000 | NORMAL_ROM |
| 12 | `para` | 0x80000 = 512 KiB | 0xbd00000 | NORMAL_ROM |
| 13 | `expdb` | 0x8000000 = 131072 KiB | 0xbd80000 | NORMAL_ROM |
| 14 | `frp` | 0x100000 = 1024 KiB | 0x13d80000 | NORMAL_ROM |
| 15 | `nvdata` | 0x4000000 = 65536 KiB | 0x13e80000 | EXT4_IMG |
| 16 | `vbmeta_a` | 0x800000 = 8192 KiB | 0x17e80000 | NORMAL_ROM |
| 17 | `vbmeta_system_a` | 0x800000 | 0x18680000 | NORMAL_ROM |
| 18 | `vbmeta_vendor_a` | 0x800000 | 0x18e80000 | NORMAL_ROM |
| 19 | `vbmeta_b` | 0x800000 | 0x19680000 | NORMAL_ROM |
| 20 | `vbmeta_system_b` | 0x800000 | 0x19e80000 | NORMAL_ROM |
| 21 | `vbmeta_vendor_b` | 0x800000 | 0x1a680000 | NORMAL_ROM |
| 22 | `metadata` | 0x2000000 = 32768 KiB | 0x1ae80000 | NORMAL_ROM |
| 23 | `backup2` | 0x180000 = 1536 KiB | 0x1ce80000 | NORMAL_ROM |
| 24 | `seccfg` | 0x800000 = 8192 KiB | 0x1d000000 | NORMAL_ROM |
| 25 | `otp` | 0x3000000 = 49152 KiB | 0x1d800000 | NORMAL_ROM |
| 26 | `md1img_a` | 0xc800000 = 204800 KiB | 0x20800000 | NORMAL_ROM |
| 27 | `spmfw_a` | 0x100000 | 0x2d000000 | NORMAL_ROM |
| 28 | `mcf_ota_a` | 0x2000000 | 0x2d100000 | EXT4_IMG |
| 29 | `audio_dsp_a` | 0xc00000 = 12288 KiB | 0x2f100000 | NORMAL_ROM |
| 30 | `pi_img_a` | 0x100000 | 0x2fd00000 | NORMAL_ROM |
| 31 | `dpm_a` | 0x400000 | 0x2fe00000 | NORMAL_ROM |
| 32 | `scp_a` | 0x600000 | 0x30200000 | NORMAL_ROM |
| 33 | `ccu_a` | 0x400000 | 0x30800000 | NORMAL_ROM |
| 34 | `vcp_a` | 0x600000 | 0x30c00000 | NORMAL_ROM |
| 35 | `sspm_a` | 0x100000 | 0x31200000 | NORMAL_ROM |
| 36 | `mcupm_a` | 0x100000 | 0x31300000 | NORMAL_ROM |
| 37 | `gpueb_a` | 0x100000 | 0x31400000 | NORMAL_ROM |
| 38 | `apusys_a` | 0x400000 | 0x31500000 | NORMAL_ROM |
| 39 | `mvpu_algo_a` | 0x4000000 = 65536 KiB | 0x31900000 | NORMAL_ROM |
| 40 | `gz_a`（GenieZone = GZ）| 0x2000000 = 32768 KiB | 0x35900000 | NORMAL_ROM |
| 41 | `lk_a` | 0x800000 = 8192 KiB | 0x37900000 | NORMAL_ROM |
| 42 | `logo_a` | 0x4000000 | 0x38100000 | NORMAL_ROM |
| 43 | `connsys_bt_a` | 0x800000 | 0x3c100000 | NORMAL_ROM |
| 44 | `connsys_wifi_a` | 0x800000 | 0x3c900000 | NORMAL_ROM |
| 45 | `boot_a` | 0x4000000 = 65536 KiB | 0x3d100000 | NORMAL_ROM |
| 46 | `vendor_boot_a` | 0x4000000 = 65536 KiB | 0x41100000 | NORMAL_ROM |
| 47 | `dtbo_a` | 0x2000000 = 32768 KiB | 0x45100000 | NORMAL_ROM |
| 48 | `tee_a` | 0x500000 = 5120 KiB | 0x47100000 | NORMAL_ROM |
| 49 | `backup3` | 0x1000000 | 0x47600000 | NORMAL_ROM |
| 50 | `sec1` | 0x200000 | 0x48600000 | NORMAL_ROM |
| 51 | `efuse` | 0x80000 | 0x48800000 | NORMAL_ROM |
| 52 | `boot_para` | 0x100000 | 0x48880000 | NORMAL_ROM |
| 53 | `dram_para` | 0x1900000 = 25600 KiB | 0x48980000 | NORMAL_ROM |
| 54 | `backup4` | 0x2580000 = 38400 KiB | 0x4a280000 | NORMAL_ROM |
| 55 | `md1img_b` | 0xc800000 | 0x4c800000 | NORMAL_ROM |
| 56 | `spmfw_b` | 0x100000 | 0x59000000 | NORMAL_ROM |
| 57 | `mcf_ota_b` | 0x2000000 | 0x59100000 | EXT4_IMG |
| 58 | `audio_dsp_b` | 0xc00000 | 0x5b100000 | NORMAL_ROM |
| 59 | `pi_img_b` | 0x100000 | 0x5bd00000 | NORMAL_ROM |
| 60 | `dpm_b` | 0x400000 | 0x5be00000 | NORMAL_ROM |
| 61 | `scp_b` | 0x600000 | 0x5c200000 | NORMAL_ROM |
| 62 | `ccu_b` | 0x400000 | 0x5c800000 | NORMAL_ROM |
| 63 | `vcp_b` | 0x600000 | 0x5cc00000 | NORMAL_ROM |
| 64 | `sspm_b` | 0x100000 | 0x5d200000 | NORMAL_ROM |
| 65 | `mcupm_b` | 0x100000 | 0x5d300000 | NORMAL_ROM |
| 66 | `gpueb_b` | 0x100000 | 0x5d400000 | NORMAL_ROM |
| 67 | `apusys_b` | 0x400000 | 0x5d500000 | NORMAL_ROM |
| 68 | `mvpu_algo_b` | 0x4000000 | 0x5d900000 | NORMAL_ROM |
| 69 | `gz_b` | 0x2000000 | 0x61900000 | NORMAL_ROM |
| 70 | `lk_b` | 0x800000 | 0x63900000 | NORMAL_ROM |
| 71 | `logo_b` | 0x4000000 | 0x64100000 | NORMAL_ROM |
| 72 | `connsys_bt_b` | 0x800000 | 0x68100000 | NORMAL_ROM |
| 73 | `connsys_wifi_b` | 0x800000 | 0x68900000 | NORMAL_ROM |
| 74 | `boot_b` | 0x4000000 | 0x69100000 | NORMAL_ROM |
| 75 | `vendor_boot_b` | 0x4000000 | 0x6d100000 | NORMAL_ROM |
| 76 | `dtbo_b` | 0x2000000 | 0x71100000 | NORMAL_ROM |
| 77 | `tee_b` | 0x500000 | 0x73100000 | NORMAL_ROM |
| 78 | `backup5` | 0x4200000 = 67584 KiB | 0x73600000 | NORMAL_ROM |
| 79 | **`super`**（动态分区容器）| **0x220000000 = 8 704 MiB** | 0x77800000 | NORMAL_ROM |
| 80 | `gsort` | 0x1000000 | 0x297800000 | NORMAL_ROM |
| 81 | `ffu` | 0x800000 | 0x298800000 | NORMAL_ROM |
| 82 | `mem` | 0x400000 | 0x299000000 | NORMAL_ROM |
| 83 | `oops` | 0x1000000 | 0x299400000 | NORMAL_ROM |
| 84 | `oem_misc1` | 0x1000000 | 0x29a400000 | NORMAL_ROM |
| 85 | `cust` | 0x80000000 = 2 097 152 KiB | 0x29b400000 | EXT4_IMG |
| 86 | `rescue` | 0x8000000 = 131072 KiB | 0x31b400000 | EXT4_IMG |
| 87 | `backup6` | 0x8400000 = 135168 KiB | 0x323400000 | NORMAL_ROM |
| 88 | `userdata` | 0xc0000000 = 3 145 728 KiB | 0x32b800000 | EXT4_IMG |
| 89 | `flashinfo` | 0x1000000 | 0xffff0020 | NORMAL_ROM |
| 90 | `sgpt` | 0x8000 | 0xffff0000 | NORMAL_ROM |

**无独立 `recovery` 分区**（91 项里没有），所以 TWRP 必须刷 `vendor_boot`。
真机 `/dev/block/by-name/` 的实号（`dump/byname.txt` [镜像]）：UFS LUN 主区是 `sdc`，`preloader_raw_a → /dev/block/mapper/pl_a`（**preloader 是 dynamic partition**）；`sda`/`sdb` 是另外两个 LUN（LU0/LU1：preloader / preloader_backup）。

`/home/lizi/IOS/work-xaga/xaga_scatter.xml:11-13` 记录该文件基于 `xaga_global_images_OS1.0.2.0.ULOMIXM_20240618.0000.00_14.0_global_9e9e252786.tgz`。

### 2.2 boot image（真机 `dump/boot_a.img` 直接解析）[镜像]

| 字段 | 值（按 AOSP v3/v4 布局偏移读取：magic@0、kernel_size@8、ramdisk_size@12、os_version@16、header_size@20、reserved[4]@24、header_version@40、cmdline@44[1536]）|
|---|---|
| magic | `ANDROID!` |
| `kernel_size` | `0xe9a2c3` = **15 311 555 B**（约 14.6 MiB）|
| `ramdisk_size` | `0x1c8061` = **1 867 873 B**（1.78 MiB）|
| `os_version` | `0x1800018c` |
| `header_size` | **1584**（= 0x630）。⚠️ AOSP v4 规范值是 1580，实测多 4 字节；**多的是什么字段本次没查清** |
| **`header_version`** | **4** |
| `cmdline` | **空**（长度 0）|

### 2.3 vendor_boot image（真机 `dump/vendor_boot_a.img`）[镜像]

| 字段 | 值 |
|---|---|
| magic | `VNDRBOOT` |
| `header_version` | **4** |
| `page_size` | 4096 |
| `kernel_addr` | `0x40000000` |
| `ramdisk_addr` | `0x66f00000` |
| `vendor_ramdisk_size` | `0x28c305f` = **42 741 855 B**（40.76 MiB）|
| `dtb_addr` | `0x0`（v4 不用）|
| `dtb_size` | `0x0` |
| `cmdline` | **`bootopt=64S3,32N2,64N2`**（41 B）|
| ramdisk table | `entry_num = 0`（v4 无 table） |
| bootconfig size | `0x4fecc` = 327 372 B |

### 2.4 BoardConfig 里的完整 mkbootimg 参数 [源码]

**`com-android_device_xiaomi_mt6895-common/BoardConfigCommon.mk`（XagaForge `17.0`，与 LineageOS `lineage-24.0` 内容一致）**，逐行引用：

```
:34  BOARD_KERNEL_BASE         := 0x3fff8000
:35  BOARD_KERNEL_OFFSET       := 0x00008000
:36  BOARD_KERNEL_PAGESIZE     := 4096
:37  BOARD_RAMDISK_OFFSET      := 0x26f08000
:38  BOARD_KERNEL_TAGS_OFFSET  := 0x07c88000
:39  BOARD_DTB_OFFSET          := 0x07c88000
:40  BOARD_BOOT_HEADER_VERSION := 4
:42  BOARD_INCLUDE_DTB_IN_BOOTIMG := true
:44  BOARD_RAMDISK_USE_LZ4     := true
:45  BOARD_USES_GENERIC_KERNEL_IMAGE := true
:46  BOARD_MOVE_GSI_AVB_KEYS_TO_VENDOR_BOOT := true
:58  BOARD_KERNEL_CMDLINE := bootopt=64S3,32N2,64N2 bootconfig
:59  BOARD_KERNEL_CMDLINE += cgroup_disable=memory
:60  BOARD_KERNEL_CMDLINE += log_buf_len=1024K
:61  BOARD_BOOTCONFIG += androidboot.serialconsole=0
:97  BOARD_FLASH_BLOCK_SIZE := 262144   # 4096 * 64 (pagesize)
:98  BOARD_BOOTIMAGE_PARTITION_SIZE        := 67108864
:99  BOARD_VENDOR_BOOTIMAGE_PARTITION_SIZE := 67108864
:100 BOARD_DTBOIMG_PARTITION_SIZE          := 33554432
:101 BOARD_USES_METADATA_PARTITION         := true
:119 BOARD_SUPER_PARTITION_SIZE            := 9126805504
:120 BOARD_SUPER_PARTITION_GROUPS          := main
:121 BOARD_MAIN_SIZE                       := 9122611200   # super - 4MB
:174 BOARD_AVB_ENABLE := true
:175 BOARD_AVB_MAKE_VBMETA_IMAGE_ARGS += --set_hashtree_disabled_flag
:181 BOARD_AVB_VBMETA_SYSTEM := product system_ext system   (SHA256_RSA2048)
:187 BOARD_AVB_VBMETA_VENDOR := vendor odm                  (SHA256_RSA2048)
```

注释原文（很关键，说明 LK 很挑）：
> `# base and pagesize are usually ignored when BOARD_USES_GENERIC_KERNEL_IMAGE is true, but our bootloader is fussy and cares about these offsets being set correctly.`

TWRP 树（`dt-device_xiaomi_xaga-TWRP/BoardConfig.mk:64-87`）同值，另有 `:64 BOARD_VENDOR_CMDLINE := bootopt=64S3,32N2,64N2`、`:71 BOARD_HEADER_SIZE := 2128`、`:72 BOARD_DTB_SIZE := 322337`、`:86 BOARD_RAMDISK_USE_LZ4 := true`、`:87 TARGET_NO_KERNEL := true`。

### 2.5 分区尺寸常量（`BoardConfigCommon.mk`）

```
BOARD_BOOTIMAGE_PARTITION_SIZE        := 67108864   (64 MiB)
BOARD_VENDOR_BOOTIMAGE_PARTITION_SIZE := 67108864   (64 MiB)
BOARD_DTBOIMG_PARTITION_SIZE          := 33554432   (32 MiB)
BOARD_SUPER_PARTITION_SIZE            := 9126805504 (8.5 GiB)
BOARD_MAIN_SIZE                       := 9122611200 (super - 4 MiB)
BOARD_MAIN_PARTITION_LIST := system system_ext product vendor odm vendor_dlkm odm_dlkm
BOARD_FLASH_BLOCK_SIZE                := 262144     (4096*64)
BOARD_AVB_ENABLE := true
BOARD_AVB_VBMETA_SYSTEM := product system_ext system   (RSA2048)
BOARD_AVB_VBMETA_VENDOR := vendor odm                  (RSA2048)
```

### 2.6 dtbo（真机 `dump/dtbo_a.img`）[镜像]

| 字段 | 值 |
|---|---|
| magic | `0xd7b7ab1e`（DTBO，**大端字段**）|
| `total_size` | 81 193 B |
| `header_size` | 32 |
| `entry_size` | 32 |
| `entry_count` | **1** |
| `page_size` | 2048 |
| `dtb0` | size `81129` @ offset `0x40`，id 0，rev 0 |

反编译后共 **86 个顶层 fragment**（编号 `fragment@0`…`fragment@85`；`__fixups__` 里 **76 个 target label**，`__symbols__` 里 **242 个符号**），覆盖 accdet / charger / 双屏 / 触摸 / NFC / 指纹 / 摄像头 / 音频 / 热区。
写入工具给出的实测大小：`dtbo.img` 86 519 B（`zhaoqing412/xaga-mt6895-k6.12/docs/09-flashing.md` §2 [自述]）。

### 2.7 LK / preloader 镜像头 [镜像]

| 文件 | 大小 | 头 |
|---|---|---|
| `lk_a.img` | 8 388 608 B | `88 16 88 58 18 34 0c 00` 后跟 ASCII `"lk"`（MTK LK 头）|
| `preloader_raw_a.img` | 4 190 208 B（`0x3FF000`）| `MMM\x01` 容器；`load=0x02000F10`、`size=0x0007A0B0`、`header=0xF0`、运行时 base `0x02001000` —— `citie114514/xaga-windows-arm64-kvm/docs/appendix-atf-reverse.md` [自述] |

### 2.8 真机内核 cmdline（`dump/cmdline.txt`）[镜像] —— 极有信息量

```
stack_depot_disable=on kasan=off kvm-arm.mode=protected cgroup_disable=pressure
cgroup.memory=nokmem console=tty0 root=/dev/ram 8250.nr_uarts=4
rcupdate.rcu_expedited=1 transparent_hugepage=never vmalloc=400M swiotlb=noforce
firmware_class.path=/vendor/firmware loop.max_part=7
panel-l16-42-02-0a-dsc-vdo.WpAndMaxlum=0x7a,0x76,0x30
mediatek_drm.panel_opt=0x53,0x42,0x32,0x01,0x4c,0x16,0x32,0x65
arm64.nopauth mtk_printk_ctrl.disable_uart=1 log_buf_len=2M
ramoops.mem_address=0x48090000 ramoops.mem_size=0xe0000
ramoops.pmsg_size=0x10000 ramoops.console_size=0x40000
usb2jtag_mode=0 root=/dev/ram debug_ext.initrd_size=43566
hwid.sku=xaga hwid.country=CN hwid.level=MP hwid.version=3.9.0
hwid.project_adc=475 hwid.build_adc=71 aee_aed.pureason=reboot
aee_aed.poffreason=Cold_reset panic_on_taint=20 arm64.nomte bootconfig
```

要点：
- **`mtk_printk_ctrl.disable_uart=1`** —— 原厂把 UART 打印关了。要看串口必须自己改 cmdline。
- `ramoops.mem_address=0x48090000`，size `0xe0000`（896 KiB），pmsg 64 KiB，console 256 KiB。
- `arm64.nomte` / `arm64.nopauth` —— MTK 出厂就关 MTE 与 pointer auth。
- `kvm-arm.mode=protected`（原厂默认）—— 想做 KVM 的话这个要改（见 `citie114514/xaga-windows-arm64-kvm`）。
- `hwid.project_adc=475`、`hwid.build_adc=71`、`hwid.version=3.9.0` —— **硬件 ID 的 ADC 值，可用于机型/SKU 判别**。
- `debug_ext.initrd_size=43566`。

### 2.9 fstab（`XagaForge/…/mt6895-common/init/fstab.mt6895` + TWRP `recovery.fstab`）[源码]

| 挂载点 | 源 | 类型 | 关键 flag |
|---|---|---|---|
| `/system` | logical | erofs / ext4 | `avb=vbmeta_system,logical,first_stage_mount,readahead_size_kb=128` |
| `/vendor` | logical | erofs / ext4 | `avb,logical` |
| `/product` | logical | erofs / ext4 | — |
| `/system_ext` | logical | erofs / ext4 | `avb=vbmeta_system` |
| `/odm` `/vendor_dlkm` `/odm_dlkm` | logical | erofs / ext4 | `avb` |
| `/data` | `/dev/block/by-name/userdata` | **f2fs** | `inlinecrypt`、`fileencryption=aes-256-xts:aes-256-cts:v2+inlinecrypt_optimized`、`keydirectory=/metadata/vold/metadata_encryption`、`fsverity`、`reservedsize=128m` |
| `/metadata` | by-name | ext4 | `formattable` |
| `/cache` | **`/dev/block/by-name/rescue`** | ext4 | 注意：**没有独立 cache 分区，复用 rescue** |
| `/mnt/vendor/{protect_f,protect_s,nvdata,nvcfg,persist}` | by-name | ext4 | 厂商分区 |

---

## 3. 3.5mm 耳机 / accdet / MT6368

**这是本次调研最重要的产出**：MT6368 accdet 的寄存器级细节现在有完整源码可读。

### 3.1 源码位置

| 文件 | 行数 | 来源 |
|---|---|---|
| `sound/soc/codecs/mt6368-accdet.c` | **3609** | `XagaForge/android_kernel_xiaomi_mt6895` @ `17.0`（本地 `/home/lizi/IOS/work-xaga/k-xaga`，用 `git cat-file -p HEAD:<path>` 取出，未 checkout）|
| `sound/soc/codecs/mt6368-accdet.h` | 141 | 同上 |
| `sound/soc/codecs/mt6368.h` | **5974** | 同上；**257 个 ACCDET 位域定义** |
| `include/linux/mfd/mt6368/core.h` | 94 | 同上；IRQ 编号 |
| `include/linux/mfd/mt6368/registers.h` | 54 | 同上；PMIC top-level 中断寄存器 |
| `drivers/mfd/mtk-spmi-pmic.c` | — | **只在完整树里有**（ESK 克隆 `drivers/mfd/mtk-spmi-pmic.c:190-194`）：`mt6368_devs[]` 里 `{.name="mt6368-accdet", .of_compatible="mediatek,mt6368-accdet", .num_resources=3, .resources=mt6368_accdet_resources}` |
| `arch/arm64/boot/dts/vendor/mediatek/mt6368.dtsi` | 298 | accdet 节点在 `:22-45` |
| `arch/arm64/boot/dts/vendor/mediatek/xaga-mt6895.dtsi` | 624 | `&accdet` 覆写在 `:607-615` |

### 3.2 硬件拓扑（实机确认）

```
MT6363 (SPMI USID 4, 主 PMIC)  ──┐
                                 ├─ spmi@1c804000 (pmif 0x1c804000/0x8ff, spmimst 0x1c801000/0x100)
MT6368 (SPMI USID 5, 副 PMIC)  ──┘        │
                                          ├─ accdet 子节点（3 个 IRQ：ACCDET / ACCDET_EINT0 / ACCDET_EINT1）
                                          ├─ AUXADC 通道 0x09 = AUXADC_ACCDET
                                          └─ eFuse（校正常数来源）
```

- accdet 是 **MT6368 的 MFD cell**，不是独立 i2c 器件；`accdet->regmap = dev_get_regmap(pdev->dev.parent, NULL)`（`mt6368-accdet.c:3307`）—— 即 **走 MT6368 的 SPMI regmap**，一次访问 = 一个 SPMI 事务。
- 平台驱动名 `pmic-codec-accdet`（`:3585`），`of_match_table` 里写的是 **`"mediatek,MT6368-accdet"`（大写 MT）**（`:196`），而 dtsi 里是 **`"mediatek,mt6368-accdet"`（小写）** —— 不冲突，因为 `of_compat_cmp` 是 `strcasecmp`（`include/linux/of.h:876`）。
- 3 个 IRQ 由 MFD 提供（`drivers/mfd/mtk-spmi-pmic.c:112-116`）：
  ```c
  DEFINE_RES_IRQ_NAMED(MT6368_IRQ_ACCDET,       "ACCDET_IRQ");
  DEFINE_RES_IRQ_NAMED(MT6368_IRQ_ACCDET_EINT0, "ACCDET_EINT0");
  DEFINE_RES_IRQ_NAMED(MT6368_IRQ_ACCDET_EINT1, "ACCDET_EINT1");
  ```
  IRQ 编号（`include/linux/mfd/mt6368/core.h:62-66`）：`MT6368_IRQ_AUDIO=96`、`MT6368_IRQ_ACCDET=97`、`MT6368_IRQ_ACCDET_EINT0=98`、`MT6368_IRQ_ACCDET_EINT1=99`、`MT6368_IRQ_NR=100`（`:81` `MT6368_IRQ_AUD_BITS = ACCDET_EINT1 - AUD_BASE + 1 = 4`，正好 4 个 AUD 子中断）。
  AUD 组的中断使能/状态（`include/linux/mfd/mt6368/registers.h:51-52`）：`MT6368_AUD_TOP_INT_CON0=0x231d`、`MT6368_AUD_TOP_INT_STATUS0=0x2323`；在 `mt6368.h` 里对应的位是 `RG_INT_EN_AUDIO` bit0 / `RG_INT_EN_ACCDET` bit1 / `RG_INT_EN_ACCDET_EINT0` bit2 / `RG_INT_EN_ACCDET_EINT1` bit3；原始状态 `MT6368_AUD_TOP_INT_RAW_STATUS0=0x2324`。

### 3.3 寄存器基址

| 块 | 地址范围 | 说明 |
|---|---|---|
| `AUD_TOP_*` | `0x2300`–`0x2327` | 时钟门控 `0x230c`、复位 `0x2319`、软复位 bank `0x231c`、中断 `0x231d`/`0x2320`/`0x2323`/`0x2324`、monitor `0x2326`/`0x2327` |
| `ACCDET_*` | **`0x2680`–`0x26d1`**（`ACCDET_ANA_ID` 起，`ACCDET_MON_FLAG_SEL` 止，**82 个连续 8-bit 寄存器**，即 0x2680..0x26d1 = 0x52 字节）| 本次核心 |
| `AUDENC_ANA_*` | `0x2508`–… | micbias / preamp / ADC 模拟侧 |
| AUXADC accdet 结果 | `0x11bf`（`MT6368_AUXADC_ACCDET0`）/ `0x11c0`（`…ACCDET1`）| 高/低字节 |

### 3.4 完整 ACCDET 寄存器表（257 个字段）

完整表已生成在 **`research/raw/mt6368-accdet-regmap.txt`**（261 行），格式 `addr bit mask field reg`。
下面只列**驱动实际会写/读**的那些（都是"我们会碰到的"）：

| 地址 | 位 | 字段 | 驱动怎么写 | 出处（`mt6368-accdet.c`）|
|---|---|---|---|---|
| `0x2319` | 1 | `RG_ACCDET_RST` | **先置 1 再清 0**（复位 accdet 单元）| `:2990-2993` |
| `0x2529` | 2 | `RG_AUDMICBIAS1LOWPEN` | mic_vol ≥ 8 时置 1 | `:3025-3026` |
| `0x2529` | 4..6 | `RG_AUDMICBIAS1VREF` | mic_vol ≤ 7 时写 `mic_vol<<4` | `:3019-3024` |
| `0x252a` | 4..5 | `RG_AUDMICBIAS1HVEN` | **mic_vol==8 → 写 `3<<4`（=2.8 V）**；`==9 → 写 `1<<4`（=2.85 V）** | `:3028-3041` |
| `0x252d` | 0..7 | `RG_AUDACCDETMICBIAS0PULLLOW` | **mic_mode 2 → `RG_ACCDET_MODE_ANA11_MODE2`**；mode 6 → 加内部 bias；AP-EINT 路径写 `0x8F` | `:3044-3076`、`:3087-3089` |
| `0x252a` | — | `RG_AUDMICBIAS1DCSW1PEN` | mode 6 时置 1 | `:3070-3071` |
| `0x2530` | 4..5 | `RG_ANALOGFDEN` | mode 1/2/6 都开（模拟快速放电）| `:3053`、`:3067`、`:3073` |
| `0x268a` | **1** | `ACCDET_SEQ_INIT` | **置 1 → sleep 2–3 ms → 清 0 → sleep 1–1.5 ms** | `:3107-3117` |
| `0x268a` | 0 | `ACCDET_SW_EN` | AP-EINT 模式写 0 | `:3098` |
| `0x268a` | 2 | `ACCDET_EINT0_SW_EN` | PMIC EINT0 且 mode 1/2 时置 1 | `:2778`等 |
| `0x268c` | 0/1/2 | `ACCDET_CMP_PWM_EN` / `VTH_PWM_EN` / `MBIAS_PWM_EN` | AP-EINT 路径 `0x7`（三位全开）| `:3091` |
| `0x268a`–`0x2691` | — | `ACCDET_PWM_WIDTH_{L,H}`(0x268e/0x268f)、`ACCDET_PWM_THRESH_{L,H}`(0x2690/0x2691) | 由 dts `headset-mode-setting[0..1]` 经 `REGISTER_VAL(x)=(x-1)` 写入 | `:3000-3007` |
| `0x2692` / `0x2693` | — | `ACCDET_RISE_DELAY_L`(0x2692)、`RISE_DELAY_H`(0x2693 bit0..6)、`FALL_DELAY`(0x2693 bit7) | `rise_delay` / `fall_delay` | `:3008-3011` |
| `0x2694` | 0..2 / 4..6 | `ACCDET_EINT_CMPMEN_PWM_THRESH` / `…_WIDTH` | `eint_pwm_width<<4 \| eint_pwm_thresh` | `:2730-2731` |
| `0x2697`–`0x269e` | — | `ACCDET_DEBOUNCE0..3_{L,H}` | `accdet_set_debounce()` 写；单位 **debounce/32768 秒** | `:3119-3123` |
| `0x26a3` / `0x26a4` / `0x26a5` | 4 bit 分组 | `ACCDET_EINT_DEBOUNCE0..3`（`0x26a3` bit0/bit4，`0x26a4` bit0/bit4）、`ACCDET_EINT_INVERTER_DEBOUNCE`(`0x26a5`) | `eint_debounce0..3` / `eint_inverter_debounce` | `:3100-3105`、`:3131-3133` |
| `0x26a9` | 0 / 2 / 3 / 4 | `ACCDET_IRQ` / `ACCDET_EINT0_IRQ` / `ACCDET_EINT1_IRQ` / `ACCDET_EINT_IN_INVERSE` | **状态读取位** | — |
| `0x26aa` | 0 / 2 / 3 | `ACCDET_IRQ_CLR` / `EINT0_IRQ_CLR` / `EINT1_IRQ_CLR` | **写 1 清中断**（irq handler 里）| `:1249-1263`(`clear_accdet_eint`) |
| `0x26aa` | 4..6 | `ACCDET_EINT_M_PLUG_IN_NUM` | 收到 n+1 次才发中断，默认 2 次 | `:2751-2752` |
| `0x26ab` | 0..7 / `0x26ac` 0..2 | `ACCDET_DA_STABLE` + `EINT0_{EN,CMPEN,CMPMEN,CTURBO,CEN}_STABLE` + `EINT1_*` | 写 `ACCDET_EINT0_STABLE_VAL` / `EINT1_STABLE_VAL` | `:2733-2748` |
| `0x26ad` | 0 / 1..2 / 3 / 4 / 5 | `ACCDET_HWMODE_EN` / `HWMODE_SEL` / `PLUG_OUT_DETECT` / `EINT0_REVERSE` / `EINT1_REVERSE` | mode 1 时 `0x26ad = 0x0` | `:2765` |
| `0x26ae` | 0 / 1 / 2 / 3 | `EINT_HWMODE_EN` / `EINT_PLUG_OUT_BYPASS_DEB` / `EINT_M_PLUG_IN_EN` / `EINT_M_HWMODE_EN` | 湿检测相关 | — |
| `0x26b6` | 0/1/2..3/4..5/6..7 | `AD_AUDACCDETCMPOB` / `AD_AUDACCDETCMPOA` / `ACCDET_{CUR,SAM,MEM}_IN` | **状态机当前状态 AB** —— `ACCDET_MEM_IN` 在 bit6，`ACCDET_STATE_MEM_IN_OFFSET=6`，`ACCDET_STATE_AB_MASK=0x03` | `:2405-2408` |
| `0x26b7` | 0..2 | `ACCDET_STATE` | 7 状态 | — |
| `0x26b8` / `0x26ba` | — | `EINT0_*` / `EINT1_*` 的 CMPOUT/CMPMOUT/CUR/SAM/MEM | EINT 侧同构 | — |
| `0x26b9` / `0x26bb` | 0..2 / 5/6/7 | `ACCDET_EINT{0,1}_STATE` / `DA_EINT{0,1}{CMPEN,CMPMEN,CTURBO}` | — | — |
| `0x26c0` | 0 / 1 / 2 / 3 / 4 / 5 / 6 / 7 | `ACCDET_EN` / `EINT0_EN` / `EINT1_EN` / `EINT0_M_EN` / `EINT0_DETECT_MOISTURE` / `EINT0_PLUG_IN` / `EINT0_M_PLUG_IN` / `EINT1_M_EN` | **最终使能 + 插入状态** | — |
| `0x26c1` | 0/1/2 | `EINT1_DETECT_MOISTURE` / `EINT1_PLUG_IN` / `EINT1_M_PLUG_IN` | — | — |
| `0x26c2`–`0x26cb` | — | `ACCDET_CUR_DEB_{L,H}`、`EINT{0,1}_CUR_DEB_{L,H}`、`EINT{0,1}_INVERTER_CUR_DEB_{L,H}` | — | — |
| `0x26cc` | 0..7 | `AD_*_MON`（8 个比较器输出镜像）| debug | — |
| `0x26d0` | 0..2 / 4..6 | `ACCDET_EINT{0,1}_M_PLUG_IN_COUNT` | 湿检测计数 | — |
| `0x26d1` | 0 / 4..7 | `ACCDET_MON_FLAG_EN` / `MON_FLAG_SEL` | debug | — |

### 3.5 状态机与判定逻辑（读代码得到的行为，不是猜）

**枚举**（`mt6368-accdet.h:38-52`）：
```c
enum accdet_report_state { NO_DEVICE=0, HEADSET_MIC=1, HEADSET_NO_MIC=2,
                           HEADSET_FIVE_POLE=3, LINE_OUT_DEVICE=4 };
enum accdet_status       { PLUG_OUT=0, MIC_BIAS=1, HOOK_SWITCH=2,
                           BI_MIC_BIAS=3, LINE_OUT=4, STAND_BY=5 };
```

**耳机阻抗判定**（`mt6368-accdet.c:845-864`，函数 `mt6368_accdet_read_audio_res`）：
```c
if (res_value < 5000) return RET_LT_5K;   // 正常设备
/* else */ cable_type = LINE_OUT_DEVICE;  // ≥5 kΩ 判为 LineOut
```
即 **5 kΩ 是 3.5mm 插头"耳机 vs LineOut"的硬阈值**。

**按键电压阈值**（`key_check`，`:1029-1055`）：三分键按 `v < down && v >= up → DW_KEY`、`v < up && v >= mid → UP_KEY`、`v < mid → MD_KEY`。
xaga dts 给的是 `headset-three-key-threshold = <0 80 220 400>`（mV），`headset-key-mode = <0>`（=三分键，走 `ACCDET_THREE_KEY`）。
四分键（`headset-four-key-threshold = <0 58 121 192 400>`）虽写了，但 `headset-key-mode=0` → 走 `ACCDET_THREE_KEY` 分支，**四分键阈值不会被读**（`mt6368-accdet.c:2660-2671` 决定 caps，`:2672-2700` 按 caps 二选一读阈值）。
按键映射到 Linux keycode：`SND_JACK_BTN_0 → KEY_MEDIA`，`BTN_1 → 257(上一曲)`，`BTN_2 → 258(下一曲)`，`BTN_3`（`:3255-3260`）。

**eFuse 校准**（`mt6368-accdet.c:918-996`，`accdet_get_efuse` / `accdet_get_efuse_4key`）：
- `auxadc_offset` = eFuse **word 91**（offset `91*2=182`）低 8 bit，>128 则减 256。
- 四分键阈值 = eFuse **word 92**（低 8 bit → mid，高 8 bit → voice）、**word 93**（低 8 bit → up），**每项 `<<2`**；`down` 恒为 600。
- 湿检测 offset 用 eFuse word 94。

**mic bias 选择**（`accdet_init_once`，`mt6368-accdet.c:3013-3045`）：`mic_vol <= 7` → VREF 直写；`== 8` → `HVEN=3` → **2.8 V**；`== 9` → `HVEN=1` → **2.85 V**。xaga 用 **8 = 2.8 V**。
**mic 模式**（`mt6368-accdet.c:3043-3076`）：`mic_mode` 1=ACC（`RG_ACCDET_MODE_ANA11_MODE1`）、2=DCC 无内部 bias（`MODE2`）、6=DCC + 内部 bias（`MODE6`）。xaga 用 **2**。

**dts 里 `headset-mode-setting` 15 个值的语义**（`mt6368-accdet.c:2521` `int pwm_deb[15] = {0}` + `:2614-2619` `of_property_read_u32_array` + `:3001-3011` 逐项写寄存器）：

```
[0] pwm_width → ACCDET_PWM_WIDTH_{L,H} = REGISTER_VAL(x) = (x-1)
[1] pwm_thresh → ACCDET_PWM_THRESH_{L,H} = (x-1)
[2] fall_delay
[3] rise_delay
[4] debounce0
[5] debounce1
[6] debounce3
[7] debounce4 (注释说 debounce8=auxadc debounce 是默认，不从 dts 读)
[8] eint_pwm_width
[9] eint_pwm_thresh
[10] eint_debounce0
[11] eint_debounce1
[12] eint_debounce2
[13] eint_debounce3
[14] eint_inverter_debounce
```

xaga 实际值：`<0x500 0x500 1 0x1f0  0x800 0x800 0x20 0x44  0x4 0x1  0x5 0x3 0x3 0x5 0xe>`
→ 即 `pwm_width=1280`、`pwm_thresh=1280`、`rise=496`、`fall=1`、`deb0=2048`、`deb1=2048`、`deb3=32`、`deb4=68`、`eint_pwm_width=4`、`eint_pwm_thresh=1`、`eint_deb0..3 = 5/3/3/5`、`inverter_deb=14`。
**这就是"3.5mm 耳机寄存器级细节"里最直接的一组常量。**

### 3.6 ⚠️ 我们之前可能踩的坑：`USB_3_5_UNSUPPORT`

`mt6368-accdet.c:110-112`：
```c
#if defined(CONFIG_TARGET_PRODUCT_PEARL) || defined(CONFIG_TARGET_PRODUCT_XAGA)
#define USB_3_5_UNSUPPORT 1
#endif
```
而 `arch/arm64/configs/vendor/xaga.config:31` 有 **`CONFIG_TARGET_PRODUCT_XAGA=y`** → **xaga 编译时 `USB_3_5_UNSUPPORT` 是开的**。

含义（`mt6368-accdet.c:114-333` 事件处理、`:3234-3241` jack 创建、`:3507-3513` probe 里 `analog_usb_typec_event_setup`）：驱动额外注册一个 **"USB_3_5 Jack"**（`SND_JACK_VIDEOOUT | SND_JACK_HEADSET`），并把 **TypeC 的 `TYPEC_ATTACHED_AUDIO`** 事件映射成"3.5 耳机插入"：
```c
case TCP_NOTIFY_TYPEC_STATE:
  if (old==TYPEC_UNATTACHED && new==TYPEC_ATTACHED_AUDIO)
      snd_soc_jack_report(&g_usb_3_5_jack, SND_JACK_HEADSET|SND_JACK_VIDEOOUT, ...);
  else if (old==TYPEC_ATTACHED_AUDIO && new==TYPEC_UNATTACHED)
      snd_soc_jack_report(&g_usb_3_5_jack, 0, ...);
```
即：**xaga 的"3.5 耳机"在软件上有两条独立上报路径** —— 一条是真正的 MT6368 accdet（`Headset Jack`），一条是 TypeC 模拟音频（`USB_3_5 Jack`）。在 HakiOS 里做耳机支持时这两条要分开处理，别把 TypeC 的当 accdet。

### 3.7 真机上怎么 dump accdet 寄存器（不写内核也能做）

驱动暴露了两条简单路径（`mt6368-accdet.c:555-610` `cat_register`、`:680-745` `set_reg_store`/`dump_reg`、`:745-800` `set_headset_mode_store`、`:802-820` `state_show`）：

| sysfs（driver attr）| 用法 |
|---|---|
| `/sys/bus/platform/drivers/pmic-codec-accdet/dump_reg` | `echo 1 > …` 开始转储，读它返回全部 ACCDET + AUDDEC_ANA 寄存器；`echo 0 >` 停止。转储范围：`ACCDET_AUXADC_SEL_ADDR`(`0x2688`) → `ACCDET_MON_FLAG_EN_ADDR`(`0x26d1`)，以及 `RG_AUDPREAMPLON_ADDR` → `RG_CLKSQ_EN_ADDR`（`:555-610`）|
| `/sys/bus/platform/drivers/pmic-codec-accdet/set_reg` | `echo "0x268a,0x02" > …` 直接写任意寄存器（**地址必须 ≥ `AUD_TOP_ANA_ID_ADDR`=0x2300**，否则只打印 "Illegal addr"）|
| `/sys/bus/platform/drivers/pmic-codec-accdet/state` | 读回 `cable_type`（0..4）|
| `/sys/bus/platform/drivers/pmic-codec-accdet/set_headset_mode` | `echo 2` / `echo 6` 运行中切 mic mode（mode 1 明确不支持）|
| `/sys/kernel/debug/accdet/headset_status` | 5 个 u16 事件计数（plugin / prev / next / media / plugout）|

另有一个字符设备 `/dev/accdet`（`ACCDET_DEVNAME = "accdet"`，`:3431` `alloc_chrdev_region`），只实现 `GET_BUTTON_STATUS` ioctl（`mt6368-accdet.c:3541-3551`）。

`init.mt6895.rc:295-296`（LineageOS 公共树）给了权限线索：
```
#Accdet
chown root radio /sys/devices/platform/Accdet_Driver/driver/accdet_call_state
```

### 3.8 我们从来没读到过真机寄存器的原因（有据可查的三条）

1. **`mtk_printk_ctrl.disable_uart=1`** 在原厂 cmdline 里（`dump/cmdline.txt`）—— 串口静默。
2. accdet 的 sysfs 在 `/sys/bus/platform/drivers/pmic-codec-accdet/`，**需要 `set_reg` 才能反读**，没有任何"只读全部"的 debugfs 文件（`dump_reg` 是 driver attr 且要 `echo 1` 触发）。
3. accdet 走 **SPMI**，不是 i2c —— 想从用户态用 i2c-dev 直接摸寄存器**做不到**，必须经过 `mtk-spmi-pmic` 的 regmap。

---

## 4. UFS 与存储

### 4.1 控制器（`mt6895.dts:5247-5290`）[源码；`dump/xaga_live.dts:6584-6598` 实机一致]

| 项 | 值 |
|---|---|
| 节点 | `ufshci@112b0000` |
| compatible | `mediatek,mt8183-ufshci` |
| reg | `0x112b0000`，size **`0x2300`**（实机 iomem：`112b0000-112b22ff : 112b0000.ufshci`）|
| 中断 | **`GIC_SPI 137`**，`IRQ_TYPE_LEVEL_HIGH`（实机 DTS 编码成 `<0x00 0x89 0x04 0x00>`，0x89=137）|
| power-domain | `MT6895_POWER_DOMAIN_UFS0_SHUTDOWN`（实机 phandle `0x04 0x02`）|
| vcc-supply | `mt6363_vemc` |
| vccq-supply | `mt6363_vufs12` |
| resets | `ufsaocfg_rst 0`(unipro)、`ufspdncfg_rst 0`(crypto)、`ufspdncfg_rst 1`(hci) |
| 其它 | `mediatek,ufs-qos`、`bootmode = <&chosen>` |
| freq-table-hz | **全 0**（由 clk 驱动动态决定，DTS 不写死）|

### 4.2 时钟（10 路，名字是 ABI）

```
clocks = <&ufs_ao_config_clk TXSYMBLOCLK>, <&ufs_ao_config_clk RXSYMBLOCLK0>,
         <&ufs_ao_config_clk RXSYMBLOCLK1>, <&ufs_ao_config_clk SYSCLK>,
         <&ufs_ao_config_clk UMPSAP>, <&ufs_ao_config_clk MMPSAP>,
         <&ufs_pdn_cfg_clk HCIUFS>, <&ufs_pdn_cfg_clk HCIAES>,
         <&topckgen_clk CLK_TOP_AES_UFSFDE_SEL>, <&topckgen_clk CLK_TOP_U_SEL>
clock-names = "phy_tx_sym","phy_rx_sym0","phy_rx_sym1","unipro_sys",
              "unipro_sap","phy_sap","ufs_ck","ufs_fde_ck","ufs_fde","ufs"
```

### 4.3 PHY

**没有独立 UFS PHY 节点**（`mt6895.dts` 里搜不到 `ufsphy`/`phy@112b…`；live DTS 也没有）。
PHY 控制寄存器疑似就在 `ufshci` 的 `0x112b0000+0x2300` 窗口内，或由 `ufs_ao_config`(`0x112b8000`) 代理 —— **未找到明确的 PHY reg 基址**。

`ufs_ao_config_clk`(`0x112b8000`, 4 KiB) 与 `ufs_pdn_cfg_clk`(`0x112bc000`, 4 KiB) 是两个 `simple-mfd` syscon，同时提供 clock 与 reset。
reset 位定义（`mt6895.dts:5099-5127`）：
```
ufsaocfg:  0x44 bit1 (assert) / 0x48 bit1 (deassert)  → unipro_rst
ufspdncfg: 0x44 bit0 / 0x48 bit0                      → crypto_rst
           0x44 bit1 / 0x48 bit1                      → hci_rst
```

### 4.4 关键结论：**UFS 在 mainline 5.10 里已经有驱动**

- `arch/arm64/configs/vendor/xiaomi_mt6895.config:68-69`：`CONFIG_SCSI_UFS_MEDIATEK=m`、`CONFIG_SCSI_UFS_MEDIATEK_DBG=m`
- 模块产物名：`ufs-mediatek-mod.ko`、`ufs-mediatek-dbg.ko`（`modules/modules.load.vendor_boot`、`modules.load.recovery`）
- `phy-mtk-ufs.ko` 也在 vendor_boot 模块列表里 —— **所以 PHY 驱动是存在的**，只是 DTS 里没有独立节点（PHY 由 ufshci 驱动内部处理）
- `cqhci.ko`（Command Queue）也在列表里
- `rpmb.ko` / `rpmb-mtk.ko` 提供 RPMB 字符设备

`zhaoqing412/xaga-mt6895-k6.12/docs/05-dts-driver-matrix.md` §3 P0 [自述]：
> UFS（`mediatek,mt8183-ufshci`）**就绪，无需移植** —— mainline `kernel-6.12/drivers/ufs/host/ufs-mediatek.c:52`，`CONFIG_SCSI_UFS_MEDIATEK=m`。dm 侧 vendor 分支 `drivers/ufs/Makefile:13-14` 两行 `obj-` 均被注释，但内核树驱动已覆盖。

### 4.5 UFS 的其他事实

| 事实 | 值 | 来源 |
|---|---|---|
| RPMB 容量 | **16 777 216 B = 16 MiB** | `Void1315/xaga-beanpod-keybox-rkp-research/devices/xaga/profile.json` [源码] |
| RPMB 页大小 | `0x100`（256 B）| 同上 |
| keybox 分区起始块 | `0x36af`，大小 `0x10000`（64 KiB）| 同上 |
| keybox 元数据偏移 | primary `0x3200`，backup `0x8200` | 同上 |
| TEE 参数物理地址 | **`0x7c600000`**，dump 大小 `0x1000` | 同上 |
| boot secret 偏移 | `0x328` | 同上 |
| 存储介质 | UFS（LUN0=preloader，LUN1=preloader_backup，LUN2=主分区）| scatter `region` 字段 |
| 分区总容量 | 最后一项结束于 `0x3eb800000` = **15.68 GiB**（这解释了 userdata 3 GiB 的 `NEEDRESIZE`：开机按实际容量扩）| scatter 计算 |
| `preloader_raw_a` 是 **dynamic partition** | `→ /dev/block/mapper/pl_a` | `dump/byname.txt` |

### 4.6 真机 iomem 里 UFS 相关窗口

```
112b0000-112b22ff : 112b0000.ufshci  ufshci@112b0000
```
（`dump/iomem.txt:66`）—— 只有这一条，**`0x112b8000` / `0x112bc000` 两个 syscon 没被 ioremap 到独立区域**（被 ufshci 驱动内部 map 或未 map）。

---

## 5. 充电与电源

### 5.1 充电 IC 组合

> 命名澄清：DTS 里有两套充电参数 —— `&charger` 里带 `xaga_product` 标记（`max_fcc` 由 `thermal_limit_pd` 首档给出 **12 400 mA**），
> `&xagapro_charger` 里带 `xagapro_product` 标记（**21 500 mA**）。所以 **`xaga` = 67 W SKU、`xagapro` = 120 W SKU**，
> 两份 DTS 共用；`xaga`/`xagapro` 由内核 DTS 里的 `xaga_product` / `xagapro_product` 标记二选一。
> ⚠️ 「67 W / 120 W」这两个功率数字**不是我从 DTS 读到的**（DTS 只给电流/电压），来自产品宣传，标 [自述]。
> `dt-android_device_xiaomi_xaga/sku/` 下只有 `manifest_xagain.xml` + `properties`（K50i 的 SKU 差异），**没有功率区分**。

| 器件 | compatible | 总线/地址 | 角色 | 出处 |
|---|---|---|---|---|
| MT6375 | `mediatek,mt6375` | i2c5 `0x34` | **主充电器 + TypeC + gauge 三合一**（子节点 `mtk_gauge` compatible `mediatek,mt6375-gauge`，live DTS `:4150-4151`、`:11174`；模块 `mt6375-charger.ko`/`tcpc_mt6375.ko`/`mt6375-adc.ko`）| live DTS `:3866` 起 |
| SC8551 (master) | `sc8551_i2c9` | i2c9 `0x66` | 电荷泵（`charger_name="tertiary_chg"` 体系）| `xaga-mt6895.dtsi:70-79` |
| SC8551 (slave) | `sc8551_i2c7` | i2c7 `0x66` | 第二颗电荷泵 | `:156-165` |
| SC8561 (master) | `sc8561_master` | i2c9 `0x64` | 新一代电荷泵（与 SC8551 二选一/共存）| `:81-85` |
| SC8561 (slave) | `sc8561_slave` | i2c7 `0x65` | 第二颗 | `:167-170` |
| LN8000 (master) | `lionsemi,ln8000` | i2c9 `0x51` | 电荷泵（Lion Semiconductor）| `:86-125` |
| LN8000 (slave) | `lionsemi,ln8000-secondary` | i2c7 `0x51` | 第二颗 | `:171-212` |
| BQ28Z610 | `bq28z610` | i2c7 `0x55` | **电量计**（`CONFIG_GAUGE_BQ28Z610`）| `:131-155` |
| FAN53870 | `onsemi,fan53870` | i2c9 `0x35` | 7 路 LDO（主要给摄像头）| camera dtsi `:303-386` |
| RT5133 | `richtek,rt5133` | i2c5 `0x18` | 8 路 LDO（`regulator_nb` 列了 ldo1..ldo8）| live DTS `:3873-3960` |
| RT6160 | `richtek,rt6160` | i2c5 `0x75` | 另一颗 regulator（**具体功能本次未查**）| live DTS |

中断脚：SC8551/SC8561/LN8000 master 全用 **GPIO13**，slave 全用 **GPIO14**（`&pio` 里 `bias-pull-up = <11>`）。
MT6375 的 PMU EINT 用 **GPIO116**（`cust/xaga.dtsi:315-319`，`LEVEL_LOW`）。

### 5.2 充电参数（`xaga-mt6895.dtsi`）

**`&charger`（xaga，67 W）** `:215-267`：
```
max_charger_voltage = 12000000 (12 V)   fv = 4450   fv_ffc = 4480
iterm = 200   iterm_ffc = 800   iterm_ffc_warm = 900
usb_charger_current = 500000   ac_charger_current = 1500000
ac_charger_input_current = 1500000   pd2_input_current = 1700000
ffc_low_tbat = 150 (1.5℃)  ffc_medium_tbat = 350  ffc_high_tbat = 480  ffc_high_soc = 95
xaga_product; mt6368_moscon1_control;
```
JEITA FCC 表（温度 ℃ / 电压 mV / 电流 mA）：
```
(-100)~0     4200  1000   747
1~50         4200  2390  2390
51~100       4200  3386  3386
101~150      4200  5826  5826
151~480      4200 12400 12400
481~600      4200  2490  2490
```
JEITA FV 表：`(-100)~0 → 4440`，`1~50 → 4450`，`51~100 → 4450`，`101~150 → 4450`，`151~480 → 4480`，`481~600 → 4100`。
步进充电：`step_chg_cfg_low_cycle` / `high_cycle` 各 6 段（3000–4480 mV，电流 12310 → 5786 mA）。
`thermal_limit_pd = <12400 10000 9000 8000 7400 6100 5600 5000 4400 3500 3000 2400 2000 1500 1000 500>`（16 档）。

**`&xagapro_charger`（Pro+，120 W）** `:269-322`：
```
max_fcc = 21500   max_vbus 由 thermal_limit_pd 首档给出 21500 mA
fv = 4445   fv_ffc = 4500   iterm = 200   iterm_ffc = 2160   iterm_ffc_warm = 3370
thermal_limit_pd = <21500 17500 16000 12000 10500 10300 10000 8000 6800 6000 5000 4000 3000 2000 1000 500>
```

**`&pd_cp_manager`（67 W 电荷泵管理）** `:347-370`：
```
fv=4450 fv_ffc=4475 max_fcc=12400 max_vbus=12000 max_ibus=6200
cv_vbat=4440 cv_vbat_ffc=4470 cv_ibat=2000 cv_ibat_bypass=1300
min_pdo_vbus=8000 max_pdo_vbus=11000  low_fcc=2000  high_soc=95
```

**`&xagapro_PCM`（120 W）** `:372-394`：
```
fv=4450 fv_ffc=4495 max_fcc=21500 max_vbus=21000 max_ibus=6200
cv_vbat=4440 cv_vbat_ffc=4490 cv_ibat=2500
min_pdo_vbus=8000  max_pdo_vbus=21000
mt6375_control_gpio = <&pio 11 0>
```

**LK 阶段充电**（`&lk_charger`，`:324-341`）：`ta_ac_charger_current = 3 A`、`pre_charger_current = 250 mA`、`boot_battery_voltage = 3.6 V`、`temp_t4=60 / t3=45 / t1=-10`。

**`&mtk_gauge { DISABLE_MTKBATTERY = <1>; }`**（`:343-345`）—— **MTK 内置电量计被禁用**，改由 BQ28Z610 上报。

### 5.3 PMIC 电源域（MT6363 主 + MT6368 副）

MT6368 的 22 路 regulator（`mt6368.dtsi:130-290`，行号见 `mt6368.dtsi`）：

| Regulator | 范围 | 备注 |
|---|---|---|
| `VBUCK1..6` | 300 mV – 1.19375 V | `allowed-modes = <0 1 2>`，ramp 200/180 µs |
| `VPA` | 600 mV – 3.6 V | — |
| **`VUSB`** | 1.2–3.5 V | `regulator-always-on` |
| `VAUX18` | 1.8–3.3 V | — |
| `VRF13_AIF` / `VRF18_AIF` | 600 mV – 2.1 V | `_AIF` 变体给音频/显示 |
| `VANT18` | 600 mV – 2.1 V | — |
| `VIBR` | 1.2–3.5 V | 震动马达 |
| **`VIO28`** | 固定 2.8 V | `regulator-always-on` |
| `VFP` / `VTP` | 1.2–3.5 V | 指纹 / 触摸 |
| `VMCH` / `VMC` | 1.2–3.5 V | SD / eMMC |
| `VAUD18` | 600 mV – 2.1 V | 音频 |
| `VCN33_1/2` | 1.2–3.5 V | Wi-Fi/BT |
| `VEFUSE` | 1.2–3.5 V | — |
| `VMCH_EINT_HIGH` / `VMCH_EINT_LOW` | 1.2–3.5 V | 双档 |
| `VBUCK_TOP` / `LDO_TOP` 中断 | `0x1411`/`0x1417`（BUCK）、`0x1b12`/`0x1b15`/`0x1b18`/`0x1b24-26`（LDO）| `include/linux/mfd/mt6368/registers.h:36-46` |

板级微调：`xiaomi-mt6895-xaga-display.dtsi:27-30` 把 **`mt6368_vrf18_aif` 锁死到 1.8 V**（min=max=1800000）。

### 5.4 电量计阈值（BQ28Z610，`:131-155`）

```
enable_shutdown_delay
normal_shutdown_vbat_1s   = 3400 mV
critical_shutdown_vbat_1s = 3310 mV
cool_critical_shutdown_vbat_1s = 3290 mV
old_critical_shutdown_vbat_1s  = 3300 mV
report_full_rsoc_1s = 9700 (97.00%)
soc_gap_1s = 2
soc_decimal_rate = <0 38  10 35  20 33  30 33  40 33  50 33  60 33  70 30  80 25  90 20  95 10>
```

### 5.5 充电相关模块（`modules/modules.load`，172 条）

关键项：`mt6375.ko`、`mt6375-charger.ko`、`mt6375-adc.ko`、`mt6375-auxadc.ko`、`mt6375-battery.ko`、`tcpc_mt6375.ko`、`tcpci_late_sync.ko`、`mtk_chg_type_det.ko`、`mtk_pd_adapter.ko`、`mtk_pd_charging.ko`、`mtk_pep.ko`、`mtk_pep20.ko`、`mtk_pep40.ko`、`mtk_charger_framework.ko`、`rt_pd_manager.ko`、`charger_class.ko`、`adapter_class.ko`、`mtk_charger_algorithm_class.ko`、**`bq28z610.ko`**、**`sc8551.ko`**、**`sc8561.ko`**、**`ln8000_charger.ko`**、`pd_cp_manager.ko`、`fan53870.ko`、`wl2868c.ko`、`pwm-mtk-disp.ko`。
`xaga.config:8-14`：`CONFIG_MTK_PUMP_EXPRESS_PLUS{,_20}_SUPPORT=m`、`CONFIG_MTK_PUMP_EXPRESS_40/50_SUPPORT=m`、`CONFIG_GAUGE_MT6375=m`、`CONFIG_REGULATOR_FAN53870=m`、`CONFIG_REGULATOR_WL2868C=m`、**`CONFIG_XMEXT_SC8551A_CHG_PUMP=m`**、**`CONFIG_XMEXT_LN8000_CHG_PUMP=m`**。

---

## 6. 摄像头与传感器

### 6.1 六颗摄像头（`xaga.config:2` 一行给全）

```
CONFIG_CUSTOM_KERNEL_IMGSENSOR="xagas5khm2_mipi_raw xagas5k4h7_mipi_raw xagaov16a1_mipi_raw
                               xagas5kgw1_mipi_raw xagagc02m1_mipi_raw xagaov02b10_mipi_raw"
```

| 驱动目录名 | 实际型号 | sensor ID | I2C 写地址 | **驱动最大 grabwindow** | DTS 节点 |
|---|---|---|---|---|---|
| `xagas5kgw1_mipi_raw` | Samsung **S5KGW1** | `0xF971` | **`0x20`** | **9280×6944** | i2c8 `sensor0@10` |
| `xagas5khm2_mipi_raw` | Samsung **S5KHM2** | `0xFAD2` | **`0x20`** | **12000×9000** | **xaga 的 DTS 里没有挂载**（`xaga.config` 仍编了它；原因未找到）|
| `xagas5k4h7_mipi_raw` | Samsung **S5K4H7** | `0x487B` | **`0x5a`** | **3264×2448** | i2c2 `sensor3@2d` |
| `xagaov16a1_mipi_raw` | OmniVision **OV16A1** | `0x1641` | **`0x20`** | **2304×1728** | i2c2 `sensor1@10` |
| `xagagc02m1_mipi_raw` | GalaxyCore **GC02M1** | `0x02E0` | **`0x6e`** | **1600×1200** | i2c4 `sensor2@37` |
| `xagaov02b10_mipi_raw` | OmniVision **OV02B10** | `0x002B` | **`0x78`** | **1600×1200** | i2c4 `sensor2@37`（与 GC02M1 共用节点）|

> 分辨率来自驱动 `struct imgsensor_info` 里 `.grabwindow_*` 的**全部模式**（`ESK-Project` 树内 `*_mipi_raw/*Sensor.c`，用 `grep -oE "grabwindow_(width|height) += [0-9]+" | sort -u` 抽出）：

| 驱动 | 全部 grabwindow 模式 |
|---|---|
| `xagas5kgw1` | 1920×1080 / 4640×2612 / 4640×3472 / **9280×6944** |
| `xagas5khm2` | 1920×1080 / 4000×2252 / 4000×3000 / **12000×9000** |
| `xagas5k4h7` | 1960×1472 / 2460×1844 / 3264×1836 / **3264×2448** |
| `xagaov16a1` | 1920×1080 / **2304×1728** |
| `xagagc02m1` | **1600×1200**（仅一档）|
| `xagaov02b10` | **1600×1200**（仅一档）|

> 由最大栅格反推像素：S5KGW1 ≈ 64 MP、S5KHM2 = 108 MP、S5K4H7 = 8 MP、OV16A1 ≈ 4 MP（**注意：型号里的 "16" 不代表 16 MP，驱动最大只有 2304×1728**）、GC02M1 / OV02B10 = 2 MP。
> ⚠️ **每个 sensor 在机身里充当主摄/前摄/超广/微距，本次没有从源码确证** —— DTS 只给出 sensor0..3 的编号与供电/EEPROM 关系。网上资料常说主摄 64 MP（S5KGW1）、前摄 16 MP，与上面驱动数据一致；但 S5KHM2（108 MP）为什么也编进 xaga 而不挂 DTS，**未找到解释**。

**这些 I2C 地址与 sensor ID 是从 `ESK-Project/android_kernel_xiaomi_mt6895` 的实际驱动源码读出的**（这是本次唯一找到 6 个 xaga sensor 驱动完整源码的地方）：
- `drivers/misc/mediatek/imgsensor/src-v4l2/common/xagas5kgw1_mipi_raw/xagas5kgw1mipiraw_Sensor.c:254`（`.i2c_addr_table = {0x20, 0xff}`）
- `.../xagas5khm2_mipi_raw/xagas5khm2mipiraw_Sensor.c:272`（`{0x20, 0xff}`）
- `.../xagas5k4h7_mipi_raw/xagas5k4h7mipiraw_Sensor.c:201`（`{0x5a, 0xff}`）
- `.../xagaov16a1_mipi_raw/xagaov16a1mipiraw_Sensor.c:181`（`{0x20, 0xff}`）
- `.../xagagc02m1_mipi_raw/xagagc02m1mipiraw_Sensor.c:150`（`{0x6e, 0xff}`）
- `.../xagaov02b10_mipi_raw/xagaov02b10mipiraw_Sensor.c:140`（`{0x78, 0xff}`）
- sensor ID 宏：`drivers/misc/mediatek/imgsensor/inc/kd_imgsensor.h:175-185`（175=S5KHM2 0xFAD2、177=GC02M1 0x02E0、179=S5K4H7 0x487B、181=OV16A1 0x1641、183=S5KGW1 0xF971、185=OV02B10 0x002B）

> 注：S5KGW1/S5KHM2 的 `i2c_speed = 1000`（1 MHz）；其余默认。

### 6.2 摄像头外围

| 器件 | 型号 | 总线/地址 | 用途 |
|---|---|---|---|
| LDO | **FAN53870** | i2c9 `0x35` | 7 路 650 mA，l1–l7（offset `0x0000`–`0x0006`）|
| 闪光灯 | **KTD2687** | i2c6 `0x63` | 双路 flash（`flash@0` / `flash@1`）|
| VCM（主摄）| **DW9800V** | i2c8 `0x0c` | 对焦马达 |
| EEPROM ×4 | `mediatek,camera_eeprom` | i2c8 `0x51`(cam0) / i2c2 `0x51`(cam1) / i2c4 `0x52`(cam2) / i2c2 `0x50`(cam3) | 模组校准数据 |
| 其他 VCM 驱动（编译进去但 xaga 没用）| `lc898229` `gt9764` `gt9772a/b` `dw9800w` `ak7377a` `ak7375c` `bu64253gwz` | — | `modules.load` |
| seninf | 4 个 CSI port | `seninf_csi_port_0..3` | sensor0→port2、sensor1→port3、sensor2→port3、sensor3→port1 |

### 6.3 传感器（非摄像头）

**未找到 xaga 的 sensorhub / 加速度计 / 陀螺仪 / 光感的 DTS 节点或 I2C 地址。**
理由：MTK 的传感器走 **SCP（Tinysys）**，配置在 vendor 侧的 `sensorhub`/`hf_manager` 里，DTS 层面只体现为 `scp@…` + SCP IPI 通道，不暴露具体 sensor 的 I2C 地址。
相关模块（`modules.load`）：`scp.ko`、`mtk-scp-ultra.ko`、`snd-soc-mtk-scp-ultra.ko`、`sensorhub.ko`、`hf_manager.ko`、`mtk-ssc.ko`、`mtk-ssc-dbg-v2.ko`。
live DTS 里**没有** `sensorhub` 节点（grep 0 命中）—— 说明它是运行时由 SCP 固件加载的，不进内核 DTS。

**能查到的替代线索**：MT6368 的 AUXADC 通道（`mt6368.dtsi:47-80`）：
`chip_temp=0x05`、`vcore_temp=0x06`、`vproc_temp=0x07`、`vgpu_temp=0x08`、**`accdet=0x09`**、`hpofs_cal=0x0c`（`avg-num=256`）、`vin1_open=0x13`、`vin2_open=0x14`；
xaga 额外用 `AUXADC_VIN3=0x15`（`&pmic_adc/vin3_open`，`pures=ADC_PURES_100K`）做 `tboard_thermistor8`（背光 NTC）。
**hwid 的 ADC 值也在 cmdline 里**：`hwid.project_adc=475`、`hwid.build_adc=71`。

### 6.4 指纹 / NFC / 触觉（顺带）

| 器件 | compatible | 总线 | 中断 |
|---|---|---|---|
| 指纹（Goodix）| `goodix,goodix-fp` | **spi3**，`reg=<0>`，8 MHz，`netlink-event=30`，`power-type=1` | GPIO136 `EDGE_RISING` |
| 指纹（FPC）| `mediatek,fpc1022` | — | GPIO136 |
| NFC | `qcom,nq-nci` | i2c3 `0x28`，400 kHz | GPIO143；VEN=GPIO18、FIRM=GPIO128、CLKREQ=GPIO194 |
| eSE | `nxp,p61` | spi1 CS0，8 MHz | — |
| 触觉 | `awinic,awinic_haptic`（AW8697）| i2c1 `0x5A` | reset=GPIO117、irq=GPIO127；`f0_pre=1700`、`bst_vol=0x53` |
| 扬声器功放 | `goodix,tfa9874` ×2 | i2c6 `0x34`(L) / `0x35`(R) | reset=GPIO213、irq=GPIO20/25、spk-sw=GPIO160 |

### 6.5 GPU（顺带，来自 live DTS + mali-r54 仓库）

| 节点 | compatible | 地址 | 来源 |
|---|---|---|---|
| `mali` | （`13000000.mali`）| **`0x13000000`–`0x1347ffff`**（5 MiB）| `dump/iomem.txt:96` |
| `gpueb@13c00000` | `mediatek,gpueb` | `0x13c00000` | live DTS `:4874` |
| `g3d_gpueb@…` ×6 | `mediatek,g3d_gpueb` | `0x13c40000` / `0x13c60200` / `0x13c60300` / `0x13c60400` / `0x13c60800` / `0x13c62000` | live DTS `:4838-4867` |
| `gpu_fdvfs@00112000` | `mediatek,gpu_fdvfs` | `0x00112000` | live DTS `:4868` |
| GPU DVFS 相关寄存器 | — | `0x13c5fd1c`、`0x13c60600` | `BkaNeko/xaga-mali-r54-port/docs/` |

`hwrng` 用 SMC fid `0x26a`（live DTS 里 `0x26a`）。

---

## 7. 启动链 / 刷机 / 救砖

### 7.1 MTK 启动链（`xiaomi-mt6895-devs/fenrir/README.md`）[自述]

```
BootROM (SoC)  →  Preloader  →  bl2_ext (EL3，被校验)  →  TEE  →  GenieZone (GZ)
               →  LK / AEE  →  Linux kernel (EL1)
```

**核心漏洞**（README 原文）：
> "When the bootloader is unlocked (seccfg), the Preloader **skips verification of the `bl2_ext` partition**, even though `bl2_ext` is responsible for verifying everything that comes after it. … Preloader jumps directly into `bl2_ext` while still running at **EL3**."

→ 改 `bl2_ext` 就能把整条信任链打断，拿到 EL3 代码执行。仓库已 **archived**（作者自述"deprecated"），但 README 里的 boot chain 图 + 漏洞描述仍是最清晰的一份。

⚠️ **scatter 里没有 `bl2_ext` 这个名字**（91 项里没有）—— 可能是 LK/preloader 内部的子分区，或者名字不同。**未找到**它在 xaga 上的具体分区名/偏移。

### 7.2 preloader 逆向（`citie114514/xaga-windows-arm64-kvm/docs/appendix-atf-reverse.md`）[自述 + 我核对了 hexdump]

| 事实 | 值 |
|---|---|
| 文件 | `preloader_raw_a.img`，**4 190 208 B（`0x3FF000`）**，sha256 `056ed47a97391139fd3553575a276afbaaa110c103bcf04c97cdc106f1fa68d1` |
| 容器 | `MMM\x01` 分段头：`0x0000` FILE_INFO(len 0x38)、`0x0038` type1 val1、`0x0044` type7 val0x90、`0x00A8` type2 val0、`0x00BC` type8 val0；代码从 `0x00F0` 起 |
| 加载 | `load=0x02000F10`、`size=0x0007A0B0`、`header=0xF0`、运行时 **base `0x02001000`**，代码 `0xF0..0x7A1A0`，之后 `0x384E60` 字节全 0 填充 |
| **SBC 来源** | **运行时从 eFuse 读**：`0x02052300 movs r0,#0x1F`（word 31）→ `bl 0x02054860`（efuse read）→ `ubfx r0,r0,#1,#1`（取 bit 1）|
| 校验逻辑 | **条件执行，不是被旁路**：`0x0204FA18` 打印 `sbc_en` → `cbz r0, 0x0204FA5C`（`sbc_en==0` 就直接跳过校验）|
| 证书 OID | `2.16.886.2454.1.1/.1.2/.1.3/.2.1 … .3.2`（2.16.886 = TW, 2454 = MediaTek）、`1.2.840.113549.1.1.1`(rsaEncryption)、`1.2.840.113549.1.1.10`(RSASSA-PSS)、证书库版本串 `V.Mon May 30 17:26:17 2022` |
| DA 校验 | `usbdl_verify_da` @ `0x0201144C`，含长度检查 + 类型跳表分发，**无短路** |

**结论（作者原话）**：
> "这份文件与手机当前正在运行的 preloader 逐字节相同 … SBC 是运行时从 eFuse 读出来的（word 0x1F 的 bit 1）。eFuse 是 OTP，一次性烧写，**改 preloader 无法改变它**。"

### 7.3 刷机流程（`zhaoqing412/xaga-mt6895-k6.12/docs/09-flashing.md`）[自述]

```bash
# 1. 关 AVB
fastboot --disable-verification --disable-verity flash vbmeta stock/vbmeta.img
# 2. 三个物理分区
fastboot flash boot         out/images/boot.img
fastboot flash vendor_boot  out/images/vendor_boot.img
fastboot flash dtbo         out/images/dtbo.img
# 3. vendor_dlkm 在 super 里 → 必须进 fastbootd
fastboot reboot fastboot
fastboot flash vendor_dlkm  out/images/vendor_dlkm.img
```

实测镜像大小：`boot.img` 14 925 824 B、`vendor_boot.img` 1 122 304 B、`vendor_dlkm.img` 9 179 136 B（erofs lz4hc，111 个模块）、`dtbo.img` 86 519 B。
`vendor_dlkm` **必须 erofs**（ext4 版 26 MB 会超 18.5 MiB 上限）。

**抓日志（无需串口）** —— 这个思路值得抄：内核把 printk 镜像进 DRAM 保留区 `log_store`（**`0x7ffbf000`，256 KB**），LK 每次启动把它恢复到 **`expdb` 分区**：
```bash
adb shell su -c 'dd if=/dev/block/by-name/expdb of=/sdcard/expdb.bin'
strings expdb.bin | grep -A200 "XAGR ring armed"
```
串口（如果有）：`earlycon=uart8250,mmio32,0x11001000 console=ttyS0,921600n8`。

### 7.4 TWRP（`dimensity-twrp-dev/device_xiaomi_xaga-TWRP`，branch `twrp-12.1_5.10.66`）[源码]

README 关键句（中文）：
> 本 tree 同时支持 Redmi Note 11T Pro (xaga) 和 Redmi Note 11T pro+ (xagapro)
> 目前分支能引导版本：**5.10.66** 内核版本，即当前所有版本。
> **不能引导**：5.10.81 内核版本，即之后将推送的开发版版本。

—— 这印证了"TWRP 必须匹配 ROM 原厂内核版本，错配可硬砖"。
刷入方式：releases 里的 `.bat`（仅 bootloader 状态）或 `fastboot flash vendor_boot_ab recovery.img`。

`BoardConfig.mk` 里 `TARGET_NO_KERNEL := true`（**树里没有内核**，只有预编译 `prebuilt/dtb` 322 337 B）、`BOARD_RAMDISK_USE_LZ4 := true`、`BOARD_SIZE` 系列见 §2.4。
`recovery/root/` 里有 `init.recovery.mt6895.rc`、`init.recovery.usb.rc`、`ueventd.mt6895.rc`、`microtrust.rc`、`first_stage_ramdisk/{fstab.emmc,fstab.mt6895}`。

### 7.5 救砖

| 事实 | 来源 |
|---|---|
| **必须先刷工程 preloader 才能用 SP Flash Tool 免授权** | `xaga-facts.md`（本机既有笔记）+ `citie114514/xaga-windows-arm64-kvm` README §3 |
| **不要用 SP Flash 深刷 `tee`**（会把 BL 重新锁上）| 同上 |
| `tee_a` 5 MiB，`tee_b` 5 MiB —— 改 ATF 只改 `tee_a`，`tee_b` 保持原厂 → **切 B 槽是天然兜底** | scatter + 同上 |
| `expdb` 128 MiB = LK 日志恢复区 | scatter |
| `seccfg` 8 MiB = 解锁状态 | scatter |
| `misc` 4 MiB（bootloader message / reboot reason）| scatter |
| `rescue` 128 MiB 被当 `/cache` 用 | fstab |

`citie114514/xaga-windows-arm64-kvm` 仓库里直接放了两份 64 MiB 的 vendor_boot 镜像（`vendor_boot_a_orig.img` / `vendor_boot_a_swcodec.img`）和 `lk-archive/lk_a.img`（8 MiB）—— **可以直接拿来做 diff 参考**。

### 7.6 KVM / EL2（顺带，因为它是启动链的一部分）

`citie114514/xaga-windows-arm64-kvm` [自述] 的核心事实：
- 原厂 cmdline 有 **`kvm-arm.mode=protected`**；要跑 KVM 必须替换 `tee_a` 里的 ATF（抬到 EL2）。
- 代价：**VCP 握手依赖 EL2/安全世界链 → 硬编解码全废**；再刷一个"vendor_boot 软编回退补丁"（**改 dtb 2 字节**）→ 框架回退软编 → KVM + 录屏/串流同时可用。
- `tee/tee_nogz_rk_5M.img`、`tee_nogz_shuilanA15_5M.img` 两个 5 MiB 镜像，`tee/verify.sh` 校验。
- `lk-archive/lk_a.img` 8 MiB。

---

## 8. 对我们最有价值的 10 条事实（按优先级）

1. **MT6368 accdet 的完整寄存器地图已经在源码里**：`0x2680`–`0x26d1` 共 **82 个连续 8-bit 寄存器**（逐字节连续），**257 个位域字段**。
   全表已导出到 `research/raw/mt6368-accdet-regmap.txt`。
   最关键的一组：`ACCDET_SEQ_INIT` = `0x268a` bit1；`ACCDET_IRQ` = `0x26a9` bit0；`ACCDET_IRQ_CLR` = `0x26aa` bit0；`ACCDET_STATE` = `0x26b7` bit0..2；`ACCDET_EN` = `0x26c0` bit0；插入状态 `ACCDET_EINT0_PLUG_IN` = `0x26c0` bit5。
   来源：`XagaForge/android_kernel_xiaomi_mt6895@17.0` `sound/soc/codecs/mt6368.h` + `mt6368-accdet.c`。

2. **xaga 的耳机电气参数已经有了确切数值，不用猜**：
   mic bias = **2.8 V**（`mic_vol=8` → `RG_AUDMICBIAS1HVEN(0x252a bit4..5) = 3`）；mic mode = **2**（DCC，无内部 bias）；
   3 键阈值 **0 / 80 / 220 / 400 mV**；4 键阈值 0/58/121/192/400 mV（但 `headset-key-mode=0` → **只走 3 键**）；
   **耳机 vs LineOut 的硬阈值 = 5 kΩ**（`<5000 Ω → 耳机`）。
   出处：`xaga-mt6895.dtsi:607-615`、`mt6368.dtsi:22-45`、`mt6368-accdet.c:845-864`（5 kΩ 判定）、`:1029-1055`（`key_check`）、`:3013-3045`（mic bias 写入）、`:2660-2700`（`headset-key-mode` 与阈值解析）。

3. **xaga 有两条独立的"3.5mm 耳机"上报路径** —— 编译宏 `CONFIG_TARGET_PRODUCT_XAGA=y` 打开 `USB_3_5_UNSUPPORT`，额外注册一个 `USB_3_5 Jack` 并从 **TypeC `TYPEC_ATTACHED_AUDIO`** 上报耳机。
   做 HakiOS 音频时必须分开处理，否则会把 TypeC 模拟耳机当成 3.5mm。出处：`mt6368-accdet.c:110-112`、`:259-333`、`xaga.config:31`。
   > 补充：`CONFIG_TARGET_PRODUCT_PEARL`（小米另一机型）也走同一分支，所以这不是 xaga 独有逻辑，是小米 PEARL/XAGA 共用的。

4. **boot image 格式已经全部确认（v4，可直接构造）**：
   `BOARD_KERNEL_BASE=0x3fff8000`、`KERNEL_OFFSET=0x8000`、`PAGESIZE=4096`、`RAMDISK_OFFSET=0x26f08000`、`TAGS_OFFSET=DTB_OFFSET=0x07c88000`、`HEADER_VERSION=4`、`RAMDISK=LZ4`；
   真机 `boot_a.img` 头 `header_size=1584`、`kernel_size=0xe9a2c3`、`ramdisk=0x1c8061`、**cmdline 为空**；
   `vendor_boot_a.img` v4：`page=4096`、`kernel_addr=0x40000000`、`ramdisk_addr=0x66f00000`、`cmdline="bootopt=64S3,32N2,64N2"`、`dtb_size=0`、`entry_num=0`。
   Lineage 注释原文警告："our bootloader is **fussy** and cares about these offsets"。

5. **UFS 就是标准 `mediatek,mt8183-ufshci`，地址 `0x112b0000` size `0x2300`，SPI 137，10 路时钟，vcc=`mt6363_vemc`，vccq=`mt6363_vufs12`，power-domain `UFS0_SHUTDOWN`**。
   而且 **mainline 5.10 已经有 `CONFIG_SCSI_UFS_MEDIATEK=m` 驱动**（`phy-mtk-ufs.ko` + `cqhci.ko` + `ufs-mediatek-mod.ko`），**不需要自己写**。
   **没有独立 UFS PHY 节点**（2 个 syscon `0x112b8000`/`0x112bc000` 提供 clock+reset，reset 位是 `0x44/0x48` 的 bit0/bit1）。

6. **充电是"MT6375 + 双电荷泵"架构，`xaga_product` 与 `xagapro_product` 两套参数共用一个 DTS，靠 `thermal_limit_pd` 首档 `12 400 mA` / `21 500 mA` 区分**（对应 67 W / 120 W SKU，功率数字来自产品宣传而非 DTS）。
   电荷泵有三家可选：SC8551（i2c9 `0x66` / i2c7 `0x66`）、SC8561（`0x64` / `0x65`）、LN8000（`0x51` / `0x51`）。
   **电量计是 BQ28Z610 @ i2c7 `0x55`，MTK 内置 gauge 被显式禁用**（`DISABLE_MTKBATTERY=<1>`）。
   中断脚：master 全 GPIO13、slave 全 GPIO14。

7. **摄像头 6 颗型号 + I2C 地址 + sensor ID 全部拿到**（这是唯一一处能找到这些的地方，在 `ESK-Project` 内核里）：
   S5KGW1 `0x20`/`0xF971`、S5KHM2 `0x20`/`0xFAD2`、S5K4H7 `0x5a`/`0x487B`、OV16A1 `0x20`/`0x1641`、GC02M1 `0x6e`/`0x02E0`、OV02B10 `0x78`/`0x002B`。
   外围：FAN53870 LDO @ i2c9 `0x35`（7 路）、KTD2687 闪光灯 @ i2c6 `0x63`、DW9800V VCM @ i2c8 `0x0c`、4 个 EEPROM。

8. **分区表 91 项已完整列出（UFS 版），`super` = 8 704 MiB，`userdata` 3 GiB `NEEDRESIZE`，无 `recovery` 分区。**
   真机 `byname.txt` 还给出了实号：主区 `sdc`，`preloader_raw_a → /dev/block/mapper/pl_a`（**preloader 是动态分区**），LUN0/LUN1 = `sda`/`sdb`。
   **`/cache` 被映射到 `rescue` 分区**（`fstab.mt6895`）。

9. **preloader 的 Secure Boot 状态是 eFuse word 31 bit 1，改 preloader 无效** —— 这条能省掉大量弯路。
   同时确认：预编译的"工程 preloader"与零售版**逐字节相同**（sha256 `056ed47a…`），所谓"免授权工程版"是个误会。
   `ramoops` 在 `0x48090000`（896 KiB），内核 log 环在 `0x7ffbf000`（256 KiB）→ **挂死也能捞日志**（经 `expdb`）。

10. **真机已经有一份完整的 live DTS（11583 行）和 iomem（583 行）在手**，且 `xaga_live.dts` 与 `lk_live.dts` **完全相同** —— 说明 LK 传给内核的 DTB 就是这一份，**不需要再猜 DTS 结构**。
    可直接用 `dtc -I dtb -O dts` 复算（`dump/xaga_live.fdt`，md5 `c1ab86afa6d57a9ff97d81dfdbf73232`）。
    `dtbo_a.img` 已反编译成 86 个顶层 fragment，`accdet` 在 `fragment@61`（只覆写 `accdet-mic-vol`/`accdet-mic-mode`）。

---

## 9. 明确没找到的

| 项目 | 状态 | 说明 |
|---|---|---|
| **UFS PHY 独立节点 / PHY 寄存器基址** | **未找到** | `mt6895.dts`、live DTS、`ufs-mediatek.c` 里都没有独立 phy 节点；`phy-mtk-ufs.ko` 存在但 DTS 无对应 reg。**需要在 `ufs-mediatek.c` 源码里找它怎么拿 PHY 寄存器** |
| **MT6368 accdet 的官方寄存器手册 / datasheet** | **未找到** | 全网没有公开的 MT6368 datasheet。目前只有内核源码里的符号名（已 100% 覆盖 `0x2680`–`0x26d1`），但**没有字段的电气含义说明**（例如 `ACCDET_IVAL_CUR_IN` 4 档分别对应多少 µA）|
| **`ACCDET_*_IVAL_*_IN` / `CUR_IN` 的数值单位** | **未找到** | 源码只写枚举，没有 mV/µA 换算表 |
| **传感器（加速度/陀螺/光感/霍尔）型号与 I2C 地址** | **未找到** | MTK 走 SCP，DTS 不暴露。要看只能 dump `/vendor/lib64/hw/sensors.*.so` 或 SCP 固件 |
| **`bl2_ext` 分区在 xaga scatter 里的名字/偏移** | **未找到** | 91 个分区里无此名。可能藏在 `lk_a`（8 MiB）内部，或叫别的名字。**fenrir 仓库没给 xaga 的具体参数**（它针对 Nothing Phone 2a）|
| **xaga 的 `mt6368-accdet` 真机寄存器 dump** | **未找到**（也无法从公开仓库获得）| 需要真机 + `set_reg`/`dump_reg`，或从 `expdb` 里挖 accdet 的 printk（驱动有大量 `pr_info`）|
| **`xaga.dtb` 的完整反编译（含所有 node 的 reg）** | 部分 | `dump/xaga_live.dts` 有，但里面大量 `phandle` 未解析成 label（`0xffffffff`），要手工映射。**建议直接在真机上 `cat /proc/device-tree/...`** |
| **TWRP 能引导 5.10.81+ 的版本** | **未找到** | `dimensity-twrp-dev` 的 README 明确说"不能引导 5.10.81" |
| **`Fuutao` / `Qn_miao` 内核作者的仓库** | **未找到** | `5.10.237-Fuutao-Qn_miao` 这个 localversion 在 GitHub 上**没有任何仓库匹配**（`gh api search/code` 返回 0；`ESK` 的 `CONFIG_LOCALVERSION="-esk"` 也不符）。可能是私人构建。**建议从真机 `/proc/version` + `uname -a` 拿完整串再去搜** |
| **`xaga` 的 `bl2_ext` / `preloader` 签名私钥** | 不适用 | — |
| **Mali-G610 的 MMU/寄存器完整表** | **未找到**（未深挖）| `BkaNeko/xaga-mali-r54-port` 有 patch 和 docs，但没系统整理寄存器表。这属于 GPU 子任务，本次不展开 |
| **xaga 的 `gpueb` 固件格式** | 部分 | 本地 `fw/GPUEB-RE.md`（既有笔记）+ `BkaNeko` 仓库有，本次未复核 |
| **`pmic-codec-accdet` 在 MT6895 上是否真的 probe 成功** | **未验证** | 需要真机 `ls /sys/bus/platform/drivers/pmic-codec-accdet/`。理论上 MFD cell 会创建，但没有真机证据 |
| **xaga 的 `subpmic_pmu_eint`（GPIO116）对应 MT6375 的哪个中断** | **未找到** | 只知道是 `LEVEL_LOW` |

---

## 附录 A：本次读过/克隆过的仓库与关键路径

| 仓库 | 分支/commit | 本地路径 | 读了什么 |
|---|---|---|---|
| `XagaForge/android_kernel_xiaomi_mt6895` | `17.0`（本地 `0a8d13c1`）| `/home/lizi/IOS/work-xaga/k-xaga`（sparse）| `mt6368-accdet.c/.h`、`mt6368.h`、`mt6368.dtsi`、`xaga.dts`、`xaga-mt6895.dtsi`、`xiaomi-mt6895-common.dtsi`、`mt6895.dts`、`cust/xaga.dtsi`、`xaga.config`、`xiaomi_mt6895.config`、camera/display/touch dtsi、`mt6368/{core,registers}.h` |
| `XagaForge/android_device_xiaomi_mt6895-common` | `17.0`（`e99e4be`）| `/home/lizi/IOS/work-xaga/com-android_device_xiaomi_mt6895-common` | `BoardConfigCommon.mk`、`init/fstab.mt6895`、`init/init.mt6895.rc`、`configs/audio/audio_policy_configuration.xml` |
| `xiaomi-mt6895-devs/android_device_xiaomi_xaga` | `lineage-24.0`（`6a08306`）| `/tmp/repo-search/clones/xmd_device_xaga` | `BoardConfig.mk`、`modules/modules.load{,.recovery,.vendor_boot}` |
| `dimensity-twrp-dev/device_xiaomi_xaga-TWRP` | `twrp-12.1_5.10.66`（`f947e85`）| `/home/lizi/IOS/work-xaga/dt-device_xiaomi_xaga-TWRP` | `BoardConfig.mk`、`README.md`、`recovery/root/system/etc/recovery.fstab` |
| `citie114514/xaga-windows-arm64-kvm` | `master`（`?`）| `/tmp/repo-search/clones/xaga_kvm` | `README.md`、`docs/appendix-atf-reverse.md`、`docs/06-mainline.md` |
| `Void1315/xaga-beanpod-keybox-rkp-research` | `?` | `/tmp/repo-search/clones/beanpod` | `devices/xaga/profile.json`、`README.md` |
| `ESK-Project/android_kernel_xiaomi_mt6895` | `main`（`7373d58`）| `/tmp/repo-search/clones/esk_kernel`（1.5 GB）| 6 个 xaga imgsensor 驱动、`drivers/mfd/mtk-spmi-pmic.c`、`include/linux/of.h` |
| `zhaoqing412/xaga-mt6895-k6.12` | `main` | 未克隆（用 raw 抓 docs）| `docs/05-dts-driver-matrix.md`、`docs/09-flashing.md`、`docs/11-bringup-log.md`、`docs/02-dt-port.md` |
| `xiaomi-mt6895-devs/fenrir` | `main` | `/tmp/repo-search/clones/fenrir` | `README.md` |
| `BkaNeko/xaga-mali-r54-port` | `master` | `/tmp/repo-search/clones/mali_r54` | `README.md`、`docs/*.md` |
| `XagaForge/android_device_xiaomi_xaga` | `17.0` | `/tmp/repo-search/clones/xf_device_xaga` | 目录结构 |
| `LineageOS/android_device_xiaomi_xaga` | `lineage-24.0` | `/tmp/repo-search/clones/los_device_xaga` | 与 `xiaomi-mt6895-devs` 版本 commit 相同 |

## 附录 B：真机 dump 清单（本机既有，本次复核）

| 文件 | 大小 | 说明 |
|---|---|---|
| `dump/xaga_live.dts` | 414 270 B / 11 583 行 | **与 `lk_live.dts` 完全相同**（md5 `564b0f45…`）|
| `dump/xaga_live.fdt` | 327 308 B | 与 `lk.fdt` 同（md5 `c1ab86af…`）|
| `dump/iomem.txt` | 26 869 B / 583 行 | `/proc/iomem` |
| `dump/byname.txt` | 6 567 B / 91 项 | `/dev/block/by-name/` |
| `dump/cmdline.txt` | 849 B | 真机内核 cmdline（完整内容见 §2.8）|
| `dump/boot_a.img` | 67 108 864 B | v4 头已解析 |
| `dump/vendor_boot_a.img` | 67 108 864 B | v4 头已解析 |
| `dump/dtbo_a.img` | 33 554 432 B | 1 个 dtb 条目，反编译出 86 个顶层 fragment |
| `dump/lk_a.img` | 8 388 608 B | MTK LK 头 `88 16 88 58` |
| `rom/MT6895_Android_scatter.txt` | 3 312 行 / 182 分区块 | EMMC + UFS 双布局 |
| `rom/{boot,dtbo,lk,vendor_boot}.img`、`rom/vendor_boot.dtb` | — | 原厂固件 |
| `xaga_scatter.xml` | — | ItsVixano 改版（不默认刷 preloader + 双槽）|
| `research/raw/mt6368-accdet-regmap.txt` | 261 行 | **本次生成**：257 个 ACCDET 字段 |

### 附录 B2：**本次落盘的可复用原件** `research/raw/xaga-ecosystem/`（548 KB）

| 文件 | 说明 |
|---|---|
| `mt6368-accdet.c` | 3609 行，MT6368 accdet 驱动全文（XagaForge `17.0`）|
| `mt6368-accdet.h` | 141 行，IOCTL / 枚举 / 阈值结构 |
| `mt6368.h` | 5974 行，MT6368 全寄存器头（含 ACCDET 257 字段）|
| `mt6368-core.h` / `mt6368-registers.h` | MFD core 的 IRQ 编号与 PMIC top 中断寄存器 |
| `mt6368.dtsi` | 298 行，PMIC DTS（accdet 节点 + 22 路 regulator）|
| `xaga.dts` / `xaga-mt6895.dtsi` / `xaga.dtsi` | 板级 DTS 三板斧（含 `&accdet` 覆写、充电参数、GPIO）|
| `dtbo_a.decompiled.dts` | 真机 `dtbo_a.img` 反编译（86 fragment，98 KB）|

> 这些是我从 sparse 克隆里用 `git cat-file -p HEAD:<path>` 取出、再从 `dtbo_a.img` 反编译得到的**原件**，
> 不依赖 `/tmp`、不依赖网络、不依赖本机那几份 git 工作区（sparse 克隆里 `sound/` 根本没 checkout）。

---

## 附录 C：与其它调研文档的分工

| 文档 | 覆盖 |
|---|---|
| **本文件** `REPO-xaga-ecosystem.md` | 除 MT6895-Mainline 外的全部 xaga 仓库：设备树节点、分区表、boot image、accdet 寄存器、UFS、充电、摄像头、启动链 |
| `research/raw/mt6368-accdet-regmap.txt` | **本次生成**：MT6368 ACCDET 全部 257 个位域（`addr / bit / mask / field / reg`），可直接喂给代码生成 |
| `research/DEEP-mt6895-mainline.md`、`WEB-mt6895-mainline-linux.md` | MT6895-Mainline 仓库与主线 Linux 现状（**不在本次范围**）|
| `research/REPO-SLNeko-XNU.md`、`WEB-xnu-*` | XNU/Darwin 移植同行（不在本次范围）|
| `/home/lizi/IOS/work-xaga/xaga-facts.md` | 早前整理的快速事实清单（分区/boot/触控/异常等），本文件与之交叉验证过多处 |
| `/home/lizi/IOS/work-xaga/kbase-mt6895-RE.md`、`fw/GPUEB-RE.md` | GPU/kbase/GPUEB 专题（本文件只在 §6.5 给了地址）|
