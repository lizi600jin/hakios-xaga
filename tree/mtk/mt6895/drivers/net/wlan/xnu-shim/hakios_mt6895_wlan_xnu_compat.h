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

/* Android V 版 mgmt/aps.c 用 struct AP_COLLECTION，而 precomp.h:269 是按
 * CFG_SUPPORT_APS 选 aps.h 还是 ap_selection.h；config.h:1444 默认给 0，
 * 于是 aps.c 拿到的是另一套头、128 处 "undefined type AP_COLLECTION"。 */
#undef CFG_SUPPORT_APS
#define CFG_SUPPORT_APS 1

/* os/linux/hif/axi/include/hif.h:144 把第二路中断号 u4IrqId_1 放在
 * #if (CFG_SUPPORT_CONNINFRA == 1) 里，而 cmm_asic_connac2x.c:1033 无条件读它。
 * Xaga 是带 conninfra 的 soc7_0，这个开关必须是 1。 */
#undef CFG_SUPPORT_CONNINFRA
#define CFG_SUPPORT_CONNINFRA 1

/* config.h:2494 按 CFG_SUPPORT_802_11AX 决定 BSS_MAX_IDLE_PERIOD，但它在
 * config.h 内部就求值完了（那时 802_11AX 还是文件里的旧值 0），所以这里
 * 必须再显式盖一次，否则 mgmt/he_rlm.c:2459 用的 WIFI_VAR.fgBssMaxIdle
 * 整个成员都不会存在（adapter.h:1396 那段被 #if 掉了）。 */
#undef CFG_SUPPORT_BSS_MAX_IDLE_PERIOD
#define CFG_SUPPORT_BSS_MAX_IDLE_PERIOD 1

/* Android V 版 os/linux/include/gl_kal.h:141 的同一常量，wlan_lib.c:9160 用。 */
#ifndef HIF_DETECT_TX_HANG_INTERVAL
#define HIF_DETECT_TX_HANG_INTERVAL 10000
#endif

/* os/linux/include/gl_cfg80211.h:49 的同一枚举位，mgmt/scan.c:439 用。 */
#ifndef NL80211_SCAN_FLAG_HIGH_ACCURACY
#define NL80211_SCAN_FLAG_HIGH_ACCURACY (1 << 10)
#endif

/* Kbuild.main:1240 按 CONFIG_DISABLE_HIF_TX_RESOURCE_CTRL 取 0/1，出厂默认是 1。
 * os/none 的旧 hif 路径不需要它，新版 hif_pdma.h:169 直接拿它当 HIF_TX_RESOURCE_CTRL。 */
#ifndef CFG_SUPPORT_HIF_TX_RESOURCE_CTRL
#define CFG_SUPPORT_HIF_TX_RESOURCE_CTRL 1
#endif

#undef CFG_SUPPORT_WFD
#define CFG_SUPPORT_WFD 1   /* Wi-Fi Display 是 Xaga 的 P2P 子系统的一部分 */
#undef CFG_SUPPORT_WFD_COMPOSE_IE
#define CFG_SUPPORT_WFD_COMPOSE_IE 0
#undef CFG_SUPPORT_LLS
#define CFG_SUPPORT_LLS 0
#undef CFG_SUPPORT_TWT
#define CFG_SUPPORT_TWT 1

#undef CFG_SUPPORT_NAN
#define CFG_SUPPORT_NAN 0
#undef CFG_ENABLE_WIFI_DIRECT
#define CFG_ENABLE_WIFI_DIRECT 1   /* Xaga 手机必须支持 P2P/Wi-Fi Direct */

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
/* ---- 5b. 新版 os/linux/hif 需要的 Linux 类型（Android V 版 gen4m） ----
 * 旧版（Android S 之前）的 hif_pdma.h 不用承担这些，所以这一组是随
 * bsp-rodin-v-oss 的 nic 一起长出来的。全部是"编译期形状"：
 *   - spinlock_t 目前是空实现，运行期必须换成 XNU 的 lck_spin_*（否则真机
 *     并发下静默数据竞争）。这里的 inline 只是让头能过。
 *   - irqreturn_t / LINUX_VERSION_CODE 是纯常量，无运行期语义。
 */
#ifndef _XAGA_SHIM_LINUX_BASE_TYPES
#define _XAGA_SHIM_LINUX_BASE_TYPES

typedef struct {
	unsigned long _xaga_opaque;
} spinlock_t;

#define spin_lock_init(l)		do { (l)->_xaga_opaque = 0UL; } while (0)
#define spin_lock(l)			do { (void)(l); } while (0)
#define spin_unlock(l)			do { (void)(l); } while (0)
#define spin_lock_bh(l)			spin_lock(l)
#define spin_unlock_bh(l)		spin_unlock(l)
#define spin_lock_irq(l)		spin_lock(l)
#define spin_unlock_irq(l)		spin_unlock(l)
#define spin_lock_irqsave(l, f)		do { (f) = 0UL; (void)(l); } while (0)
#define spin_unlock_irqrestore(l, f)	do { (void)(l); (void)(f); } while (0)
#define spin_trylock(l)			((void)(l), 1)
#define spin_is_locked(l)		((void)(l), 0)

/* work_struct 本身由 os/none/include/gl_dependent.h:155 提供（空结构），
 * 这里只补 nic.c 用到的三个动作。XNU 侧必须接真 work 队列，否则 L0.5 reset
 * 钩子永远不会被执行 —— 这是编译期垫片，不是实现。 */
#define INIT_WORK(w, fn)	do { (void)(w); (void)(fn); } while (0)
#define schedule_work(w)	((void)(w), 0)
#define schedule_delayed_work(w, d) ((void)(w), (void)(d), 0)
#define cancel_work_sync(w)	((void)(w), 0)

typedef unsigned int irqreturn_t;
#define IRQ_NONE	0U
#define IRQ_HANDLED	1U
#define IRQ_WAKE_THREAD	2U

#define KERNEL_VERSION(a, b, c)	(((a) << 16) + ((b) << 8) + (c))
#ifndef LINUX_VERSION_CODE
#define LINUX_VERSION_CODE	KERNEL_VERSION(5, 15, 0)
#endif

#define IS_ENABLED(x)	(x)
#define IS_BUILTIN(x)	0
#define IS_MODULE(x)	0
#define IS_REACHABLE(x)	(x)

#endif /* _XAGA_SHIM_LINUX_BASE_TYPES */

/* Android V 版 chips/soc7_0/soc7_0.c:656 把 fw_log_wifi_irq_handler 填进
 * CHIP_DBG_OPS，实现却在 chips/common/cmm_asic_connac2x.c:3000（同库单元），
 * 只是没有对外声明。这里补一条。 */
int fw_log_wifi_irq_handler(void); /* 实现见 chips/common/cmm_asic_connac2x.c；返回 int32_t，此处用 int（同宽） */

#ifndef _HIF_AXI
#define _HIF_AXI 1
/* ---- 5. 内核双向链表：AXI hif.h / hif_pdma.h 用 intrusive list ----
 * XNU 没有 linux/list.h，这里只给 soc7_0 编译路径真正用到的那部分语义。
 */
#ifndef _XAGA_SHIM_LIST_H
#define _XAGA_SHIM_LIST_H

struct list_head {
	struct list_head *next;
	struct list_head *prev;
};

#define LIST_HEAD_INIT(name) { &(name), &(name) }

#define INIT_LIST_HEAD(ptr) do { \
	(ptr)->next = (ptr); (ptr)->prev = (ptr); \
} while (0)

static inline void _xaga_list_add(struct list_head *n,
				  struct list_head *prev,
				  struct list_head *next)
{
	next->prev = n;
	n->next = next;
	n->prev = prev;
	prev->next = n;
}

static inline void list_add(struct list_head *n, struct list_head *head)
{
	_xaga_list_add(n, head, head->next);
}

static inline void list_add_tail(struct list_head *n, struct list_head *head)
{
	_xaga_list_add(n, head->prev, head);
}

static inline void list_del(struct list_head *entry)
{
	entry->prev->next = entry->next;
	entry->next->prev = entry->prev;
	entry->next = entry->prev = entry;
}

static inline int list_empty(const struct list_head *head)
{
	return head->next == head;
}

#define list_entry(ptr, type, member) \
	((type *)((char *)(ptr) - __builtin_offsetof(type, member)))

#define list_first_entry(ptr, type, member) \
	list_entry((ptr)->next, type, member)

#define list_for_each(pos, head) \
	for (pos = (head)->next; pos != (head); pos = pos->next)

#define list_for_each_entry(pos, head, member) \
	for (pos = list_entry((head)->next, __typeof__(*pos), member); \
	     &pos->member != (head); \
	     pos = list_entry(pos->member.next, __typeof__(*pos), member))

#endif /* _XAGA_SHIM_LIST_H */



/* ---- 6. completion：只在 soc7_0 的 trigger-assert 路径用 ----
 * XNU 侧用 lck_grp + wakeup 实现；这里给编译期形状，运行期值由 XNU 层填。
 */
#ifndef _XAGA_SHIM_COMPLETION_H
#define _XAGA_SHIM_COMPLETION_H

struct completion {
	volatile int done;
};

static inline void init_completion(struct completion *c) { c->done = 0; }
static inline void complete(struct completion *c) { c->done = 1; }
static inline long wait_for_completion_timeout(struct completion *c,
					       unsigned long timeout)
{
	(void)timeout;
	return c->done ? 1 : 0;
}

#define MSEC_TO_JIFFIES(ms) ((ms) / 10)   /* 占位换算，真值由 XNU 侧定时器决定 */

#endif /* _XAGA_SHIM_COMPLETION_H */

/* soc7_0.c:1085/1094 用到的两个全局，上游定义在
 * include/chips/cmm_asic_connac2x.h:1100，但该头在 _HIF_AXI 的 include 链里
 * 没有被 precomp.h 带进来，这里补 extern（定义仍由 chip 层提供）。
 */
extern unsigned char g_IsWfsysBusHang;
extern unsigned char fgIsResetting;
extern struct completion g_triggerComp;

/* ---- 7. 802.11ax 能力宏 ----
 * include/nic/mt66xx_reg.h:1320 的 arb_ac_mode_addr 被
 * #if (CFG_SUPPORT_802_11AX == 1) 门控，而 soc7_0.c:525 无条件初始化它。
 * MT6895 的 soc7_0 本身是 Wi-Fi 6 平台，这里按真实能力打开。
 */
#undef CFG_SUPPORT_802_11AX
#define CFG_SUPPORT_802_11AX 1

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


/* ---- 5. 内核双向链表：AXI hif.h / hif_pdma.h 用 intrusive list ----
 * XNU 没有 linux/list.h，这里只给 soc7_0 编译路径真正用到的那部分语义。
 */
#ifndef _XAGA_SHIM_LIST_H
#define _XAGA_SHIM_LIST_H

struct list_head {
	struct list_head *next;
	struct list_head *prev;
};

#define LIST_HEAD_INIT(name) { &(name), &(name) }

#define INIT_LIST_HEAD(ptr) do { \
	(ptr)->next = (ptr); (ptr)->prev = (ptr); \
} while (0)

static inline void _xaga_list_add(struct list_head *n,
				  struct list_head *prev,
				  struct list_head *next)
{
	next->prev = n;
	n->next = next;
	n->prev = prev;
	prev->next = n;
}

static inline void list_add(struct list_head *n, struct list_head *head)
{
	_xaga_list_add(n, head, head->next);
}

static inline void list_add_tail(struct list_head *n, struct list_head *head)
{
	_xaga_list_add(n, head->prev, head);
}

static inline void list_del(struct list_head *entry)
{
	entry->prev->next = entry->next;
	entry->next->prev = entry->prev;
	entry->next = entry->prev = entry;
}

static inline int list_empty(const struct list_head *head)
{
	return head->next == head;
}

#define list_entry(ptr, type, member) \
	((type *)((char *)(ptr) - __builtin_offsetof(type, member)))

#define list_first_entry(ptr, type, member) \
	list_entry((ptr)->next, type, member)

#define list_for_each(pos, head) \
	for (pos = (head)->next; pos != (head); pos = pos->next)

#define list_for_each_entry(pos, head, member) \
	for (pos = list_entry((head)->next, __typeof__(*pos), member); \
	     &pos->member != (head); \
	     pos = list_entry(pos->member.next, __typeof__(*pos), member))

#endif /* _XAGA_SHIM_LIST_H */

/* ---- 6. completion：只在 soc7_0 的 trigger-assert 路径用 ----
 * XNU 侧用 lck_grp + wakeup 实现；这里给编译期形状，运行期值由 XNU 层填。
 */
#ifndef _XAGA_SHIM_COMPLETION_H
#define _XAGA_SHIM_COMPLETION_H

struct completion {
	volatile int done;
};

static inline void init_completion(struct completion *c) { c->done = 0; }
static inline void complete(struct completion *c) { c->done = 1; }
static inline long wait_for_completion_timeout(struct completion *c,
					       unsigned long timeout)
{
	(void)timeout;
	return c->done ? 1 : 0;
}

#define MSEC_TO_JIFFIES(ms) ((ms) / 10)   /* 占位换算，真值由 XNU 侧定时器决定 */

#endif /* _XAGA_SHIM_COMPLETION_H */

/* soc7_0.c:1085/1094 用到的两个全局，上游定义在
 * include/chips/cmm_asic_connac2x.h:1100，但该头在 _HIF_AXI 的 include 链里
 * 没有被 precomp.h 带进来，这里补 extern（定义仍由 chip 层提供）。
 */
extern unsigned char g_IsWfsysBusHang;
extern unsigned char fgIsResetting;
extern struct completion g_triggerComp;

/* ---- 7. 802.11ax 能力宏 ----
 * include/nic/mt66xx_reg.h:1320 的 arb_ac_mode_addr 被
 * #if (CFG_SUPPORT_802_11AX == 1) 门控，而 soc7_0.c:525 无条件初始化它。
 * MT6895 的 soc7_0 本身是 Wi-Fi 6 平台，这里按真实能力打开。
 */
#undef CFG_SUPPORT_802_11AX
#define CFG_SUPPORT_802_11AX 1

/* ---- 8. conninfra：soc7_0 的 bus-hang / reset-ongoing 判定 ----
 * 真值来自 vendor 头（见 conninfra.h 注释）。
 */
#include "conninfra.h"


/* ---- 9. struct firmware ----
 * chips/common/fw_dl.c:2652 dereferences prFw->data/size, so the opaque
 * pointer declared in os/none/include/gl_os.h is not enough. Field order and
 * types match the Linux firmware API; the XNU loader fills them from the
 * kext-bundled blob.
 */
#ifndef _XAGA_SHIM_FIRMWARE_H
#define _XAGA_SHIM_FIRMWARE_H
struct firmware {
	const unsigned char *data;
	unsigned long size;
};
#endif


/* ---- 9. 缺失常量（值有出处，不是占位） ---- */
/* Linux nl80211.h：WLAN_AKM_SUITE_SAE = 8 */
#ifndef WLAN_AKM_SUITE_SAE
#define WLAN_AKM_SUITE_SAE 8
#endif
/* IEEE 802.11：Vendor Specific EID = 221 */
#ifndef WLAN_EID_VENDOR_SPECIFIC
#define WLAN_EID_VENDOR_SPECIFIC 221
#endif
/* os/linux/include/gl_cfg80211.h:98 */
#ifndef NL80211_SCAN_FLAG_LOW_SPAN
#define NL80211_SCAN_FLAG_LOW_SPAN (1 << 8)
#endif


/* ---- 10. 802.11w (PMF) ----
 * include/config.h:782 only sets CFG_SUPPORT_802_11W 0 because the LINUX
 * macro is undefined on this os/none build; os/linux keeps it 1 (config.h:770).
 * Without it include/nic/adapter.h drops BSS_INFO.rApPmfCfg and
 * AIS_SPECIFIC_BSS_INFO.fgMgmtProtection, and include/nic/mac.h drops
 * STATUS_CODE_ASSOC_REJECTED_TEMPORARILY (mgmt/ais_fsm.c:3260, mgmt/rsn.c).
 */
#undef CFG_SUPPORT_802_11W
#define CFG_SUPPORT_802_11W 1

/* ---- 11. TX latency stats thresholds ----
 * os/linux/include/gl_kal.h:172-195; common/wlan_lib.c:8026+ uses them.
 */
#define TX_LATENCY_STATS_UPDATE_INTERVAL (0)
#define TX_LATENCY_STATS_CONTINUOUS_FAIL_THREHOLD (10)

#define TX_LATENCY_STATS_MAX_DRIVER_DELAY_L1 (1)
#define TX_LATENCY_STATS_MAX_DRIVER_DELAY_L2 (5)
#define TX_LATENCY_STATS_MAX_DRIVER_DELAY_L3 (10)
#define TX_LATENCY_STATS_MAX_DRIVER_DELAY_L4 (20)

#define TX_LATENCY_STATS_MAX_CONNSYS_DELAY_L1 (10)
#define TX_LATENCY_STATS_MAX_CONNSYS_DELAY_L2 (20)
#define TX_LATENCY_STATS_MAX_CONNSYS_DELAY_L3 (50)
#define TX_LATENCY_STATS_MAX_CONNSYS_DELAY_L4 (80)

#define TX_LATENCY_STATS_MAX_MAC_DELAY_L1 (5)
#define TX_LATENCY_STATS_MAX_MAC_DELAY_L2 (10)
#define TX_LATENCY_STATS_MAX_MAC_DELAY_L3 (20)
#define TX_LATENCY_STATS_MAX_MAC_DELAY_L4 (50)

#define TX_LATENCY_STATS_MAX_FAIL_CONNSYS_DELAY_L1 (10)
#define TX_LATENCY_STATS_MAX_FAIL_CONNSYS_DELAY_L2 (20)
#define TX_LATENCY_STATS_MAX_FAIL_CONNSYS_DELAY_L3 (50)
#define TX_LATENCY_STATS_MAX_FAIL_CONNSYS_DELAY_L4 (80)

/* ---- 12. Android 构建里恒为开的三个开关 ----
 * os/none 的 config.h 默认把这三个置 0，但代码是照 Android 实编写的：
 *  - CONFIG_WLAN_SERVICE：common/wlan_oid.c:17220 无条件引用
 *    nicCmdEventListmode，其声明在 include/nic_cmd_event.h:3671 的
 *    "#if (CONFIG_WLAN_SERVICE == 1)" 内。
 *  - CFG_SUPPORT_PRE_ON_PHY_ACTION：chips/common/pre_cal.c 用
 *    struct HAL_PHY_ACTION_TLV_HEADER / HAL_PHY_ACTION_MAGIC_NUM，
 *    定义在 include/nic_init_cmd_event.h:240-296 的同一门控内。
 *  - CFG_MTK_ANDROID_EMI：Xaga 的校准结果就放在 EMI（真机 EMI PA 见
 *    XAGA_LIVE_WIFI_EMI_PA=0xbd000000），pre_cal.c:38 的全局和被
 *    142/250 行的无条件读写必须落在同一编译域。
 */
#ifndef CONFIG_WLAN_SERVICE
#define CONFIG_WLAN_SERVICE 1
#endif
#undef CFG_SUPPORT_PRE_ON_PHY_ACTION
#define CFG_SUPPORT_PRE_ON_PHY_ACTION 1
#ifndef CFG_MTK_ANDROID_EMI
#define CFG_MTK_ANDROID_EMI 1
#endif

/* ioremap 家族：XNU 侧的映射后端在 gl_kal_xnu.c（EMI 段是固定物理窗），
 * 这里只给编译期形状，调用点语义仍是"映射一段 EMI 读校准结果"。
 */
#ifndef __iomem
#define __iomem
#endif
struct resource;
extern struct resource *request_mem_region(unsigned long start,
					  unsigned long len, const char *name);
extern void release_mem_region(unsigned long start, unsigned long len);
extern void __iomem *ioremap(unsigned long phys, unsigned long size);
extern void iounmap(const void __iomem *addr);

/* ---- 12a. min_t / max_t ----
 * linux/kernel.h 提供；mgmt/rrm.c:1623 拿它截断 SSID 长度。类型来自调用点，
 * 两侧都被求值一次，与 upstream 语义一致（不引入内核的 __cmp 副作用）。
 */
#ifndef min_t
#define min_t(type, x, y) ({ type __a = (type)(x); type __b = (type)(y); \
	__a < __b ? __a : __b; })
#endif
#ifndef max_t
#define max_t(type, x, y) ({ type __a = (type)(x); type __b = (type)(y); \
	__a > __b ? __a : __b; })
#endif

/* ---- 12a. 内核日志/offload 常量（linux/kernel.h / netdev_features.h）----
 * common/wlan_lib.c:8617 用 printk(KERN_INFO "...")，12137 清 net_device 的
 * checksum offload 位。值取 upstream 真值；XNU 侧 features 只做位记账。
 */
#ifndef KERN_INFO
#define KERN_INFO "\001" "6"
#endif
#ifndef NETIF_F_IP_CSUM
#define NETIF_F_IP_CSUM (1 << 1)
#endif
#ifndef NETIF_F_IPV6_CSUM
#define NETIF_F_IPV6_CSUM (1 << 4)
#endif
#ifndef UINT_MAX
#define UINT_MAX (~0U)
#endif
/* ---- 12. vendor driver events ----
 * os/linux/include/gl_kal.h:371; mgmt/cnm.c:4481 declares one on the stack,
 * so a typedef-less "enum" tag is enough.
 */
#if CFG_SUPPORT_DATA_STALL
/* os/linux/include/gl_kal.h:197-201 */
#define REPORT_EVENT_INTERVAL		30
#define EVENT_PER_HIGH_THRESHOLD	80
#define EVENT_TX_LOW_RATE_THRESHOLD	20
#define EVENT_RX_LOW_RATE_THRESHOLD	20
#define TRAFFIC_RHRESHOLD	150

enum ENUM_VENDOR_DRIVER_EVENT {
	EVENT_TEST_MODE,
	EVENT_ARP_NO_RESPONSE,
	EVENT_PER_HIGH,
	EVENT_TX_LOW_RATE,
	EVENT_RX_LOW_RATE,
	EVENT_SG_DISABLE,
	EVENT_SG_1T1R,
	EVENT_SG_2T2R,
	EVENT_TX_DUP_OFF = 100,
	EVENT_TX_DUP_ON = 101,
	EVENT_TX_DUP_CERT_CHANGE = 102
};
#endif

#endif
