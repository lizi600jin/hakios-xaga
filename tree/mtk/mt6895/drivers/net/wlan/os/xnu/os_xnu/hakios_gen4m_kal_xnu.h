/*
 * hakios_gen4m_kal_xnu.h — gen4m (MT6895 / soc7_0, flavor "b_t") KAL + MMIO + conninfra
 *
 * 签名不是我自己定的：每一条都与驱动树里的真实声明或调用点对齐，
 * 依据写在每个分组的注释里。Stage 9 的 nm -u（commit 598caf0）证明
 * libgen4m_soc7_core.a 只需要这 27 条 kal* + 2 条 wf_ioremap + 5 条 conninfra。
 *
 * 之前的版本把签名写成了"干净语义"，那会在 ABI 上错位（例如
 * kalGetFwFlavor 实际是无参返回 const char *，kalCancelTimer 返回 uint8_t，
 * KAL_NEED_IMPLEMENT 是可变参数且返回 long）。这里全部按真实修正。
 */
#ifndef HAKIOS_GEN4M_KAL_XNU_H
#define HAKIOS_GEN4M_KAL_XNU_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 驱动侧类型在这里只用前向声明：适配层不该把 wlan 头拖进来 */
struct GLUE_INFO;
struct ADAPTER;
struct RTMP_TX_RING;
struct RTMP_RX_RING;
struct firmware;
struct device;
struct timespec;
struct sk_buff;

/* wf_ioremap_read/write 的参数类型是 phys_addr_t，在本树展开为 64 位 */
typedef uint64_t xaga_phys_addr_t;

/* ---- 宿主/ XNU 后端：XNU 侧接 hakios.mt6895_connsys.c 的窗口，宿主侧用 mock ---- */
struct hakios_gen4m_kal_ops {
	void *ctx;
	/* MMIO：返回 0 成功、非 0 失败，与 wf_ioremap_* 的 int 返回一致 */
	int (*read32)(void *ctx, xaga_phys_addr_t addr, uint32_t *val);
	int (*write32)(void *ctx, xaga_phys_addr_t addr, uint32_t val);
	/* 固件：返回 0 成功；out_buf/out_len 由后端填 */
	int (*load_fw)(void *ctx, const char *name,
		       const uint8_t **out_buf, size_t *out_len);
	void (*release_fw)(void *ctx, const uint8_t *buf);
};

void hakios_gen4m_kal_bind_ops(const struct hakios_gen4m_kal_ops *ops);

struct hakios_gen4m_kal_stats {
	uint32_t allocs;
	uint32_t frees;
	uint32_t skb_allocs;
	uint32_t skb_frees;
	uint32_t mmio_reads;
	uint32_t mmio_writes;
	uint32_t mmio_errors;
	uint32_t hif_int_events;
	uint32_t rst_events;
	uint32_t fw_loads;
	uint32_t uevents;
	bool     whole_chip_resetting;
	bool     conninfra_powered;
};
const struct hakios_gen4m_kal_stats *hakios_gen4m_kal_get_stats(void);

/* ---- 1. 内存与 skb ---- 依据 os/none/include/gl_dependent.h:790-796, 938-943 */
void *kal_kmalloc(size_t size, int gfp_type);   /* enum gfp_t 在 C 里就是 int */
void  kal_kfree(void *addr);
void *kal_vmalloc(size_t size);
void  kal_vfree(void *addr);
struct sk_buff *kal_dev_alloc_skb(unsigned int length);
void  kal_kfree_skb(struct sk_buff *skb);
/* 供宿主/单测观察 skb 而不必看见驱动结构体内部布局 */
int   hakios_gen4m_skb_probe(struct sk_buff *skb, unsigned int *len,
			     unsigned char **data);

/* ---- 2. 位操作 / 字符串 / 打印 ---- 依据 gl_dependent.h:1080,1086,790,757,702,716 */
void kal_set_bit(unsigned long bit, unsigned long *p);
int  kal_test_bit(unsigned long bit, unsigned long *p);
int  kal_scnprintf(char *buf, size_t size, const char *fmt, ...);
int  kal_strtou8(const char *s, unsigned int base, uint8_t *res);
int  kal_hex_dump_to_buffer(const void *buf, size_t len, int rowsize,
			    int groupsize, char *linebuf, size_t linebuflen,
			    bool ascii);
void kal_warn_on(uint8_t condition);

/* ---- 3. IE 解析 ---- 依据 os/none/include/gl_os.h:1277-1285 */
const uint8_t *kalFindIeMatchMask(uint8_t eid, const uint8_t *ies, int len,
				  const uint8_t *match, int match_len,
				  int match_offset, const uint8_t *match_mask);
const uint8_t *kalFindIeExtIE(uint8_t eid, uint8_t exteid,
			      const uint8_t *ies, int len);

/* ---- 4. 时间与定时器 ---- 依据 gl_dependent.h:725、gl_kal.h:1459 */
void     kal_get_monotonic_boottime(struct timespec *ts);
uint8_t kalCancelTimer(struct GLUE_INFO *prGlueInfo);

/* ---- 5. 固件与 flavor ---- 依据 gl_kal.h:1721、os/linux/include/gl_kal.h:853
 *      （_kalRequestFirmware 在 Linux 侧是 request_firmware 的别名）。 */
const char *kalGetFwFlavor(void);
int _kalRequestFirmware(const struct firmware **fw, const char *name,
			struct device *device);

/* ---- 6. 事件 / 复位 / 诊断 ---- 依据 gl_kal.h:1485,1825,1805,1789、gl_rst.h:218、
 *      hif_pdma.h:650-656。 */
uint32_t kalSyncTimeToFW(struct ADAPTER *prAdapter, uint8_t fgInitCmd,
			 unsigned int second, unsigned int usecond);
void     kalSetHifIntEvent(struct GLUE_INFO *pr, unsigned long ulBit);
void     kalSetRstEvent(void);
uint8_t kalIsWholeChipResetting(void);
void     kalDumpTxRing(struct GLUE_INFO *prGlueInfo, struct RTMP_TX_RING *prTxRing,
		       uint32_t u4Num, bool fgDumpContent);
void     kalDumpRxRing(struct GLUE_INFO *prGlueInfo, struct RTMP_RX_RING *prRxRing,
		       uint32_t u4Num, bool fgDumpContent, uint32_t u4DumpLen);
uint8_t kalSendUevent(const char *src);
int      kalExternalAuthRequest(struct ADAPTER *prAdapter, uint8_t uBssIndex);

/* KAL_NEED_IMPLEMENT 在 gl_dependent.h:678 是可变参数且返回 long，
 * 驱动把它当表达式用（见 gl_cfg80211.h:743 的强制转换）。 */
long KAL_NEED_IMPLEMENT(const char *file, const char *func, int line, ...);

/* ---- 7. AXI MMIO 与 conninfra ---- 依据 hif_pdma.h:657-658；
 *      conninfra 三条取自 conninfra/include/conninfra.h:207/215/221，
 *      pwr_on/pwr_off 在 kernel 树内没有声明，签名沿用本次约定的 int(int)。 */
int wf_ioremap_read(xaga_phys_addr_t addr, unsigned int *val);
int wf_ioremap_write(xaga_phys_addr_t addr, unsigned int val);

int conninfra_reg_readable(void);
int conninfra_reg_readable_for_coredump(void);
int conninfra_is_bus_hang(void);
int conninfra_pwr_on(int drv_type);
int conninfra_pwr_off(int drv_type);

#ifdef __cplusplus
}
#endif

#endif /* HAKIOS_GEN4M_KAL_XNU_H */
