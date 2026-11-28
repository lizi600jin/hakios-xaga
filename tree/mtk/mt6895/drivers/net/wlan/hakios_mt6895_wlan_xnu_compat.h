#ifndef XAGA_XNU_COMPAT_H
#define XAGA_XNU_COMPAT_H

/*
 * os/xnu 兼容头（第二版：裁剪 + 最小垫片）。
 *
 * 第一步把 config.h 拉进来，它的 include guard (_CONFIG_H) 就此置上，
 * 之后 precomp.h 再 include 同一个文件会直接被跳过。于是这里就能把
 * 非核心的 Android 特性改成 0，而不用改任何上游文件。
 */

#include "config.h"

#undef CFG_SUPPORT_WFD
#define CFG_SUPPORT_WFD 0
#undef CFG_SUPPORT_WFD_COMPOSE_IE
#define CFG_SUPPORT_WFD_COMPOSE_IE 0
#undef CFG_SUPPORT_LLS
#define CFG_SUPPORT_LLS 0
#undef CFG_SUPPORT_NAN
#define CFG_SUPPORT_NAN 0
#undef CFG_ENABLE_WIFI_DIRECT
#define CFG_ENABLE_WIFI_DIRECT 0

/* ---- 1. 内核类型：wlan_lib.h:1287 的 rLongestTxTime ---- */
struct timespec64 {
	long long tv_sec;
	long long tv_nsec;
};

/* ---- 2. HIF 选择：Xaga 的 connsys 走 AXI ----
 * os/none/hif/none/include/hif.h 只认 _HIF_NONE，而 include/debug.h:514 的
 * CHIP_DBG_OPS 成员（get_rx_rate_info / show_wfdma_dbg_probe_info /
 * show_wfdma_wrapper_info）只在 _HIF_PCIE 或 _HIF_AXI 下才存在。
 * 所以用 AXI，并给一份接受 _HIF_AXI 的 hif.h 垫片。
 */
#ifndef _HIF_AXI
#define _HIF_AXI 1
#endif

/* ---- 3. 常量（结构体 gl_dependent.h 已经自带，别重复定义） ---- */
struct timer_list {
	struct timer_list *next;
	struct timer_list *prev;
	unsigned long      expires;
	void             (*function)(struct timer_list *);
	unsigned long      data;
};

#ifndef OID_HDLR_REC_NUM
#define OID_HDLR_REC_NUM 5     /* os/linux/include/gl_kal.h:136 的同一个值 */
#endif

/* ---- 3b. pm_qos：XNU 没有 Linux 的 pm_qos 子系统，这些只在本驱动内部用 ---- */
#ifndef PM_QOS_VCORE_OPP
#define PM_QOS_VCORE_OPP                7   /* 占位：XNU 侧自实现，与 Linux 编号无关 */
#endif
#ifndef PM_QOS_VCORE_OPP_DEFAULT_VALUE
#define PM_QOS_VCORE_OPP_DEFAULT_VALUE  0
#endif

#ifndef LIST_SEG_MAX
#define LIST_SEG_MAX 8         /* gl_qa_agent.h:315 用到但全树没有定义 */
#endif

/* ---- 4. 万一有分支绕过 CFG 关断，仍要能编 ---- */
#ifndef STATS_LLS_MAX_NSS_NUM
#define STATS_LLS_MAX_NSS_NUM     2
#define STATS_LLS_CCK_NUM         4
#define STATS_LLS_OFDM_NUM        8
#define STATS_LLS_HT_NUM          16
#define STATS_LLS_VHT_NUM         10
#define STATS_LLS_HE_NUM          12
#define STATS_LLS_MAX_CCK_BW_NUM  1
#define STATS_LLS_MAX_OFDM_BW_NUM 1
#define STATS_LLS_MAX_HT_BW_NUM   2
#define STATS_LLS_MAX_VHT_BW_NUM  3
#define STATS_LLS_MAX_HE_BW_NUM   4
#endif

#endif
