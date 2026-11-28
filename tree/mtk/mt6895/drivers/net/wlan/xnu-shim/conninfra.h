/* XNU-compat shim: conninfra.h
 *
 * Values below are copied verbatim from the vendor header
 *   drivers/misc/mediatek/connectivity/conninfra/include/conninfra.h
 * in XagaForge/android_kernel_xiaomi_mt6895 (HEAD d39a261), lines 139-145
 * and 207/215/221. Nothing here is invented.
 *
 * XNU has no conninfra daemon, so the declarations exist for compile shape
 * only; the definitions must come from the XNU-side connsys glue.
 */
#ifndef _XAGA_SHIM_CONNINFRA_H
#define _XAGA_SHIM_CONNINFRA_H

/* enum consys_drv_type, verbatim from the same vendor header (lines 44-51) */
enum consys_drv_type {
	CONNDRV_TYPE_BT = 0,
	CONNDRV_TYPE_FM = 1,
	CONNDRV_TYPE_GPS = 2,
	CONNDRV_TYPE_WIFI = 3,
	CONNDRV_TYPE_CONNINFRA = 4,
	CONNDRV_TYPE_MAX
};

#define CONNINFRA_INFRA_BUS_HANG			0x1
#define CONNINFRA_AP2CONN_RX_SLP_PROT_ERR	0x2
#define CONNINFRA_AP2CONN_TX_SLP_PROT_ERR	0x4
#define CONNINFRA_AP2CONN_CLK_ERR			0x8
#define CONNINFRA_INFRA_BUS_HANG_IRQ		0x10

/* conninfra.h:129-132 (XagaForge d39a261), pre_cal.c 校准回调用 */
#define CONNINFRA_CB_RET_CAL_PASS_POWER_OFF	0x0
#define CONNINFRA_CB_RET_CAL_PASS_POWER_ON	0x2
#define CONNINFRA_CB_RET_CAL_FAIL_POWER_OFF	0x1
#define CONNINFRA_CB_RET_CAL_FAIL_POWER_ON	0x3

#define CONNINFRA_ERR_RST_ONGOING			-0x7788

int conninfra_reg_readable(void);
int conninfra_reg_readable_for_coredump(void);
int conninfra_is_bus_hang(void);

#endif
