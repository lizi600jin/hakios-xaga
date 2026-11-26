# xaga 硬件事实：WiFi 不是高通的

2026-11-26。为对接外部规划文档而做的一次探查，顺带纠正那份文档的两处错误。

## WiFi：MTK 自家的，不是 WCN6750

外部文档说「射频是 WCN6750，跑在 SoC 内的 WPSS（Hexagon DSP）上」。
**那是另一台手机（redwood）的架构，xaga 完全不是。**

从 `xaga.dts` 和 `vendor_boot_a.img` 查到的：

```
wifi@18000000 {
        compatible = "mediatek,wifi";
        reg = <0x00 0x18000000 0x00 0x700000>;   /* 7 MB 窗口 */
        interrupts = <0x00 0x20a 0x04 0x00>, <0x00 0x219 0x04 0x00>;
        flavor_bin = "b";
        emi-addr = <0x00>;
        emi-size = <0x1400000>;                  /* 20 MB EMI 预留 */
        emi-alignment = <0x1000000>;
        emi-max-addr = <0xc0000000>;
};
```

**射频前端是外置 FEM，不是 SoC 内置：**

```
connfem@18000000 {
        compatible = "mediatek,mt6895-connfem";
        pinctrl-names = "wlan7207h_wlan7207c", "wlan7207h_wlan7205c", "qm42195_qm45197";

        nofem     { vid = 0x00; pid = 0x00; };
        wlan7207h { vid = 0x02; pid = 0x04; };
        wlan7205c { vid = 0x02; pid = 0x05; };
        wlan7207c { vid = 0x02; pid = 0x07; };
        qm42195   { vid = 0x03; pid = 0x01; };
        qm45197   { ... };
};
```

`wlan72xx` 是 Qorvo 的 WiFi 前端模块，`qm42/qm45` 也是。
所以 xaga 是「MTK SoC 内置 MAC + 外置 Qorvo FEM」的方案，和 WCN6750 那条路不同。

**分区：**

```
connsys_wifi_a → /dev/block/sdc42      connsys_wifi_b → /dev/block/sdc71
connsys_bt_a   → /dev/block/sdc41      connsys_bt_b   → /dev/block/sdc70
```

**结论**：别的机型那套 WiFi 栈（WCN6750 + WPSS + `wpss.mdt`）对 xaga 只能做
**协议参考**，不能「框架可抄」。要做得从 `mediatek,wifi` 这个节点自己起。

## 核对过的两条「文档说有、实际没有」的代价

### 8 核不会失去自动返回

外部文档说「watcher 没了 → 自动返回也没了」，并给了三个补偿方案
（SPM 定时唤醒 / 外挂 WDT / 降级 7 核）。

**我们的 loader 一直是 `XNU_WATCH_SECONDS=0`，从来没有 watcher**：

```
-DXNU_CORES=4
-DXNU_WATCH_SECONDS=0
-DXNU_RETURN_AFTER="3600"
```

而自动返回照常工作（30–31 秒）。因为它是内核里的 `thread_call`：

```c
/* kernel-src/osfmk/arm64/loader_return.c:504 */
thread_call_t call = thread_call_allocate(loader_return_timer, NULL);
thread_call_enter_delayed(call, deadline);
```

不占核、不需要别的核「看着」。所以 `XNU_CORES=8` 与自动 RETURN 可以同时要，
那三个补偿方案都不必要。

### UFS 不卡在租约死结上

外部文档引的是 722–739 那一轮的状态（「租约 state=3 POISONED、十一次尝试」）。
**那是三个月前。** 现在有真机证据：

```
ufs-apfs-64g-796:  c->upiu[16] = 0x2a;      /* CDB[0] WRITE(10) */
ufs-apfs-flush-754: 608457 FUA 写  a9ad0ec32e7ca23c  ✅
```

真实 `WRITE(10)` + FUA 落盘已通，`/private/var` 63 GiB、4980 个目录跨重启零丢失、
35 分钟长跑验证过。

所以「换 `0x112b0000` 基址」不是解死结，而是**架构升级**：
让内核自己持有队列，不再靠 loader 代写。价值仍然有，但性质不同。

## UFS 控制器基址：外部文档说对了

`ufshci@112b0000`，我们自己的 `android-ufs-reference-716` 也确认了：

```
/sys/devices/platform/soc/112b0000.ufshci/host0/...
```

而我们现在继承的是 LK 留下的列表 `0x51266400`（`UFS_USE_LK_LIST=1`），
因为自己写 `UTRLRSR = 1` 读回是 0（`docs/UFS.md:166`）。

**「基址不对」是一个合理的解释** —— 如果当初映射的窗口不对，
`UTRLRSR` 写进去自然读不回来。这一条值得试。

## GPU：外部文档判断正确

对方走 panfrost（G52 = Bifrost），我们走 panthor（G610 = Valhall CSF）。
G610 上 panfrost 永远走不到「指令执行并写回 DRAM」，而我们已经做到（M2b）。

**两边拼起来才完整**：他做用户态，我们做 CSF 后端。

## 一个字没说错的地方

外部文档说「触摸两边是同一份代码，不存在谁领先」。
我们这边的 `hakios.nvt.c` 确实在跑（19 行日志，基线已校准），
不是路线图里说的「未接、完全缺」。
