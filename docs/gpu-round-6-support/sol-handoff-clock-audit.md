# Sol 移交：时钟路径静态核查（未真机验证）

时间：2026-11-13 02:29 Asia/Shanghai。仅供已接手的 Astra 继续；Sol 已停止设备操作。

没有修改源码、没有构建或刷机。以下是本地源码与既有 613 日志的证据快照。

## 来源及原文

/home/lizi/IOS/hakios-mtk/mtk/mt6895/drivers/gpu/hakios.mali610.c

SHA256: `403e91cd7d9a63083a487736b21f994ada9074050f396529091a8a8f6d6f1a13`

```text
120: #define MALI_CLK_CFG_5          0x0060u         /* mfg_sel_1_sel in bits 1:0 */
121: #define MALI_CLK_CFG_5_SET      0x0064u
122: #define MALI_CLK_CFG_5_CLR      0x0068u
123: #define MALI_MFG_SEL_1_MASK     0x00000003u     /* 2 = mainpll_d5_d2, what the phone uses */
124: #define MALI_CLK_CFG_30         0x01f0u         /* mfg_sel_0_sel in bit 16 */
125: #define MALI_CLK_CFG_30_SET     0x01f4u
126: #define MALI_CLK_CFG_30_CLR     0x01f8u
127: #define MALI_CLK_UPDATE         0x0004u         /* one bit per mux, latched by writing it */
128: #define MALI_MFG_REF_MASK       0x03000000u
129: #define MALI_MFG_REF_26M        (0u << 24)      /* tck_26m_mx9_ck: the always-on crystal */
130: #define MALI_MFG_REF_SHIFT      19              /* CLK_UPDATE bit for mfg_ref_sel */
```

```text
2091: static void
2092: pll_300mhz(void)
2093: {
2094: 	uint32_t v;
2095:
2096: 	reg_wr(gMfgpll, MALI_MFGPLL_CON3, MALI_PLL_PWR_ON);
2097: 	delay(2);
2098: 	v = reg_rd(gMfgpll, MALI_MFGPLL_CON3);
2099: 	reg_wr(gMfgpll, MALI_MFGPLL_CON3, v & ~MALI_PLL_ISO_EN);
2100: 	delay(2);
2101: 	reg_wr(gMfgpll, MALI_MFGPLL_CON1,
2102: 	    MALI_PLL_PCW_CHG | MALI_PLL_POSDIV_8 | MALI_PLL_PCW_300MHZ);
2103: 	v = reg_rd(gMfgpll, MALI_MFGPLL_CON0);
2104: 	reg_wr(gMfgpll, MALI_MFGPLL_CON0, v | MALI_PLL_EN);
2105: 	delay(50);
2106: 	reg_wr(gTopckgen, MALI_CLK_CFG_30_SET, MALI_MFG_SEL_0_BIT);
2107: 	delay(2);
2108:
2109: 	/*
2110: 	 * The second selector, which this driver had never touched.
2111: 	 *
2112: 	 * The vendor's power-on enables both muxes -- clk_mux for the GPU and clk_sc_mux for the
2113: 	 * shader stacks -- and then switches both to CLOCK_MAIN. The phone's own clock summary reads
2114: 	 * mfg_sel_0_ck at 299999955 and mfg_sel_1_ck at 218400000, which is mainpll_d5_d2, so the
2115: 	 * second one is not left on the crystal. Selector 2 in this field is that parent; the field
2116: 	 * is bits 1:0 of CLK_CFG_5 and has no update bit of its own, the same as CLK_CFG_30.
2117: 	 *
2118: 	 * Whether the MCU fetches through this mux is not established. It is set because the only
2119: 	 * working configuration in evidence sets it, and because a mux left on a stopped parent is
2120: 	 * exactly the kind of silent stall this search keeps meeting.
2121: 	 */
2122: 	reg_wr(gTopckgen, MALI_CLK_CFG_5_CLR, MALI_MFG_SEL_1_MASK);
2123: 	reg_wr(gTopckgen, MALI_CLK_CFG_5_SET, 2u);
2124: 	delay(2);
2125:
2126: 	printf("mali: pll: con0 %#x con1 %#x con3 %#x cfg5 %#x cfg30 %#x\n",
2127: 	    reg_rd(gMfgpll, MALI_MFGPLL_CON0), reg_rd(gMfgpll, MALI_MFGPLL_CON1),
2128: 	    reg_rd(gMfgpll, MALI_MFGPLL_CON3), reg_rd(gTopckgen, MALI_CLK_CFG_5),
2129: 	    reg_rd(gTopckgen, MALI_CLK_CFG_30));
2130: }
```

/home/lizi/IOS/work-xaga/k-xaga-16.2/drivers/clk/mediatek/clk-mt6895.c

SHA256: `aab7583ba5563911bfc3ba0c9a17ae1b07dee4160931a3f91ace6daa4d2aa596`

```text
141: #define TOP_MUX_MFG_REF_SHIFT			19
142: #define TOP_MUX_MFGSC_REF_SHIFT			20
```

```text
1243: static const char * const mfgsc_ref_parents[] = {
1244: 	"tck_26m_mx9_ck",
1245: 	"univpll_d6",
1246: 	"mainpll_d5_d2"
1247: };
```

```text
1774: static const char * const mfg_sel_0_parents[] = {
1775: 	"mfg_ref_ck",
1776: 	"mfgpll_ck"
1777: };
1778:
1779: static const char * const mfg_sel_1_parents[] = {
1780: 	"mfgsc_ref_ck",
1781: 	"mfgscpll_ck"
1782: };
```

```text
1913: 	MUX_CLR_SET_UPD(CLK_TOP_MFG_REF_SEL/* dts */, "mfg_ref_sel",
1914: 		mfg_ref_parents/* parent */, CLK_CFG_4, CLK_CFG_4_SET,
1915: 		CLK_CFG_4_CLR/* set parent */, 24/* lsb */, 2/* width */,
1916: 		CLK_CFG_UPDATE/* upd ofs */, TOP_MUX_MFG_REF_SHIFT/* upd shift */),
1917: 	/* CLK_CFG_5 */
1918: 	MUX_CLR_SET_UPD(CLK_TOP_MFGSC_REF_SEL/* dts */, "mfgsc_ref_sel",
1919: 		mfgsc_ref_parents/* parent */, CLK_CFG_5, CLK_CFG_5_SET,
1920: 		CLK_CFG_5_CLR/* set parent */, 0/* lsb */, 2/* width */,
1921: 		CLK_CFG_UPDATE/* upd ofs */, TOP_MUX_MFGSC_REF_SHIFT/* upd shift */),
```

```text
2163: 	MUX_CLR_SET_UPD(CLK_TOP_MFG_SEL_0_SEL/* dts */, "mfg_sel_0_sel",
2164: 		mfg_sel_0_parents/* parent */, CLK_CFG_30, CLK_CFG_30_SET,
2165: 		CLK_CFG_30_CLR/* set parent */, 16/* lsb */, 1/* width */,
2166: 		INV_OFS/* upd ofs */, INV_BIT/* upd shift */),
2167: 	MUX_CLR_SET_UPD(CLK_TOP_MFG_SEL_1_SEL/* dts */, "mfg_sel_1_sel",
2168: 		mfg_sel_1_parents/* parent */, CLK_CFG_30, CLK_CFG_30_SET,
2169: 		CLK_CFG_30_CLR/* set parent */, 17/* lsb */, 1/* width */,
2170: 		INV_OFS/* upd ofs */, INV_BIT/* upd shift */),
```

```text
2273: 	MUX_GATE_CLR_SET_UPD(CLK_TOP_MFG_REF_SEL/* dts */, "mfg_ref_sel",
2274: 		mfg_ref_parents/* parent */, CLK_CFG_4, CLK_CFG_4_SET,
2275: 		CLK_CFG_4_CLR/* set parent */, 24/* lsb */, 2/* width */,
2276: 		31/* pdn */, CLK_CFG_UPDATE/* upd ofs */,
2277: 		TOP_MUX_MFG_REF_SHIFT/* upd shift */),
2278: 	/* CLK_CFG_5 */
2279: 	MUX_GATE_CLR_SET_UPD(CLK_TOP_MFGSC_REF_SEL/* dts */, "mfgsc_ref_sel",
2280: 		mfgsc_ref_parents/* parent */, CLK_CFG_5, CLK_CFG_5_SET,
2281: 		CLK_CFG_5_CLR/* set parent */, 0/* lsb */, 2/* width */,
2282: 		7/* pdn */, CLK_CFG_UPDATE/* upd ofs */,
2283: 		TOP_MUX_MFGSC_REF_SHIFT/* upd shift */),
```

/home/lizi/IOS/hakios-mtk/xnu-src/xnu-8796.121.2/osfmk/arm64/loader_return.c

SHA256: `f20ea69ae40c30b8e4644377093b20aa08f1b22b9a99c820a587352004dd6d89`

```text
432: loader_return_timer(__unused thread_call_param_t p0, __unused thread_call_param_t p1)
433: {
434: 	static const char banner[] = "\n--- loader_return_after expired ---\n";
435: 	uint32_t cpu = 0, as_panic = 0;
436:
437: 	for (const char *c = banner; *c; c++) {
438: 		loader_log_putc(*c);
439: 	}
440: 	PE_parse_boot_argn("return_panic", &as_panic, sizeof(as_panic));
441: 	if (!as_panic) {
442: 		hakios_var_save_on_return();   /* the data volume kept, while the system is still whole */
443: 		hakios_adreno_stop();          /* a GPU kept for the system is given back while there is a thread to wait in */
444: 		hakios_wifi_stop();            /* and the Wi-Fi DSP, if it runs, shut down */
445: 		hakios_adsp_stop();            /* and the audio DSP */
446: 		hakios_mpss_stop();            /* and the modem */
447: 	}
448: 	if (PE_parse_boot_argn("return_cpu", &cpu, sizeof(cpu)) && cpu <= (uint32_t)ml_get_max_cpu_number() &&
449: 	    cpu_xcall((int)cpu, loader_return_test, (void *)(uintptr_t)(as_panic ? 2 : 1)) == KERN_SUCCESS) {
450: 		for (;;) {
451: 			delay_for_interval(1000, NSEC_PER_MSEC);        /* that processor ends it */
452: 		}
453: 	}
454: 	loader_return_test((void *)(uintptr_t)(as_panic ? 2 : 1));
455: }
456:
```

/home/lizi/IOS/hakios-mtk/xnu-src/xnu-8796.121.2/osfmk/arm64/psci_smp.c

SHA256: `143d4b2a2dae86b628a94395b4034b86f878ef200e7d8e3ce636cffaede2f8b4`

```text
289: psci_smp_stop_others(void)
290: {
291: 	int me, max_cpu;
292: 	uint64_t now, deadline;
293:
294: 	if (psci_smp_started == 0) {
295: 		return;                 /* there is only this one */
296: 	}
297: 	__builtin_arm_wsr("DAIFSet", DAIFSC_ALL);
298: 	me = cpu_number();
299: 	max_cpu = ml_get_max_cpu_number();
300: 	if (!os_atomic_cmpxchg(&psci_smp_stop_owner, -1, me, acq_rel) && psci_smp_stop_owner != me && me != master_cpu) {
301: 		psci_smp_cpu_off();     /* another processor is at it already */
302: 	}
303:
304: 	if (me != master_cpu) {
305: 		/*
306: 		 * Nobody is stopped before the boot processor has answered: a core that is switched
307: 		 * off while it holds a lock the boot processor is waiting for would take the return
308: 		 * with it.
309: 		 */
310: 		psci_smp_note(" ends the system: the return to the boot loader is the boot processor's to make\n", me);
311: 		os_atomic_store(&psci_smp_boot_wanted, true, release);
312: 		__builtin_arm_dsb(DSB_SY);
313: 		gic_v3_irq_signal_stop(cpu_datap(master_cpu)->cpu_phys_id);
314: 		__builtin_arm_sev();
315: 		now = mach_absolute_time();
316: 		nanoseconds_to_absolutetime(PSCI_SMP_STOP_SECONDS * NSEC_PER_SEC, &deadline);
317: 		deadline += now;
318: 		while (!os_atomic_load(&psci_smp_stopping, acquire) && now < deadline) {
319: 			now = mach_absolute_time();
320: 		}
321: 		if (!os_atomic_load(&psci_smp_stopping, acquire)) {
322: 			/*
323: 			 * It does not hear (stuck with its interrupts off). There is no return without
324: 			 * it; a loader that watches from its own core will find the kernel silent and
325: 			 * take the log as it is, and so this goes into the log.
326: 			 */
327: 			psci_smp_note(" cannot return to the boot loader and the boot processor does not answer: all cores stop here\n", me);
328: 			psci_smp_stop_rest();
329: 		}
330: 		psci_smp_cpu_off();
```

## 613 原始日志中的 PLL 行

```text
[   33.090893]: mali: pll: con0 0x15445 con1 0x31713b1 con3 0x80000001 cfg5 0x1010182 cfg30 0xff010000
```

CFG5=0x01010182：parent=2，PDN bit7=1。CFG30=0xff010000：bit16=1，bit17=0。

## 证据边界

- vendor 将 CFG5[1:0] 定义为 mfgsc_ref_sel，需要 UPDATE bit20；正常带门定义的 PDN 是 bit7。
- 现有 pll_300mhz() 的注释将此字段误称为 mfg_sel_1，而真正的 mfg_sel_1 在 CFG30 bit17。
- 现有函数只写 parent=2，没有在该处清 bit7 / 提交 bit20。其他代理应结合完整时钟状态继续核对。
- 这些证据证明源码命名和预期操作存在差异，尚不证明 MCU 时钟来自此链，亦不证明修正后会启动。
- return_cpu=0 是现有返回参数，尚未试验。不得将代码建议写成已测结果。
- 保存的 logbook 有多个启动/残留片段；返回 CPU 的不同读数不能视为同一次运行。
