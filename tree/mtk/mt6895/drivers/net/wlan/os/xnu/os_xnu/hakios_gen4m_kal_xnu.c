/*
 * hakios_gen4m_kal_xnu.c — gen4m (MT6895 / soc7_0, flavor "b_t") 的
 *                          XNU / 宿主 KAL + MMIO + conninfra 适配层。
 *
 * 每个函数的签名都在 .h 里注明依据。本文件不 include 任何 wlan 头，
 * 因此不会把驱动的宏/结构拖进来；sk_buff 只在内部镜像其布局（见下）。
 */
#include "hakios_gen4m_kal_xnu.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static struct hakios_gen4m_kal_ops g_ops;
static struct hakios_gen4m_kal_stats g_stats;
static char g_last_uevent[128];

void hakios_gen4m_kal_bind_ops(const struct hakios_gen4m_kal_ops *ops)
{
	memset(&g_stats, 0, sizeof(g_stats));
	memset(g_last_uevent, 0, sizeof(g_last_uevent));
	if (ops)
		g_ops = *ops;
	else
		memset(&g_ops, 0, sizeof(g_ops));
}

const struct hakios_gen4m_kal_stats *hakios_gen4m_kal_get_stats(void)
{
	return &g_stats;
}

/* ---- 1. 内存与 skb ----
 *
 * sk_buff 在这里镜像驱动 os/none/include/gl_dependent.h:250 的布局
 * （char cb[48] / data / len / dev），并补上 Stage 10 要加的 mark 字段。
 * 偏移必须一致，否则驱动按自己结构写字段就会踩坏我们的堆块。
 */
struct sk_buff {
	char cb[48];
	unsigned char *data;
	unsigned int len;
	void *dev;
	unsigned int mark;
};

void *kal_kmalloc(size_t size, int gfp_type)
{
	void *p;

	(void)gfp_type;
	if (size == 0)
		size = 1;
	p = calloc(1, size);
	if (p)
		g_stats.allocs++;
	return p;
}

void kal_kfree(void *addr)
{
	if (!addr)
		return;
	g_stats.frees++;
	free(addr);
}

void *kal_vmalloc(size_t size)
{
	void *p = NULL;

	if (size == 0)
		size = 1;
	p = malloc(size);
	if (p)
		g_stats.allocs++;
	return p;
}

void kal_vfree(void *addr)
{
	if (!addr)
		return;
	g_stats.frees++;
	free(addr);
}

struct sk_buff *kal_dev_alloc_skb(unsigned int length)
{
	struct sk_buff *skb;

	skb = calloc(1, sizeof(*skb));
	if (!skb)
		return NULL;
	if (length) {
		skb->data = calloc(1, length);
		if (!skb->data) {
			free(skb);
			return NULL;
		}
	}
	skb->len = length;
	g_stats.skb_allocs++;
	return skb;
}

int hakios_gen4m_skb_probe(struct sk_buff *skb, unsigned int *len,
			   unsigned char **data)
{
	if (!skb)
		return -1;
	if (len)
		*len = skb->len;
	if (data)
		*data = skb->data;
	return 0;
}

void kal_kfree_skb(struct sk_buff *skb)
{
	if (!skb)
		return;
	free(skb->data);
	free(skb);
	g_stats.skb_frees++;
}

/* ---- 2. 位操作 / 字符串 / 打印 ---- */

void kal_set_bit(unsigned long bit, unsigned long *p)
{
	p[bit / (8 * sizeof(unsigned long))] |= 1UL << (bit % (8 * sizeof(unsigned long)));
}

int kal_test_bit(unsigned long bit, unsigned long *p)
{
	return (p[bit / (8 * sizeof(unsigned long))] >> (bit % (8 * sizeof(unsigned long)))) & 1UL;
}

int kal_scnprintf(char *buf, size_t size, const char *fmt, ...)
{
	va_list ap;
	int ret;

	va_start(ap, fmt);
	ret = vsnprintf(buf, size, fmt, ap);
	va_end(ap);
	return ret;
}

int kal_strtou8(const char *s, unsigned int base, uint8_t *res)
{
	char *end = NULL;
	unsigned long v;

	if (!s || !res)
		return -1;
	v = strtoul(s, &end, (int)base);
	if (end == s)
		return -1;
	if (v > 0xFF)
		return -1;
	*res = (uint8_t)v;
	return 0;
}

int kal_hex_dump_to_buffer(const void *buf, size_t len, int rowsize,
			   int groupsize, char *linebuf, size_t linebuflen,
			   bool ascii)
{
	const uint8_t *p = buf;
	size_t i, pos = 0;
	int per_line = rowsize ? rowsize : (int)len;

	if (!buf || !linebuf || linebuflen == 0)
		return 0;
	for (i = 0; i < len && i < (size_t)per_line; i++) {
		int n;

		if (groupsize && i && (i % (size_t)groupsize) == 0 && pos + 1 < linebuflen)
			linebuf[pos++] = ' ';
		n = snprintf(linebuf + pos, linebuflen - pos, "%02x", p[i]);
		if (n <= 0)
			break;
		pos += (size_t)n;
		if (pos >= linebuflen)
			break;
	}
	if (ascii && pos + 3 < linebuflen) {
		linebuf[pos++] = ' ';
		linebuf[pos++] = '|';
		linebuf[pos++] = ' ';
		for (i = 0; i < len && i < (size_t)per_line && pos + 1 < linebuflen; i++)
			linebuf[pos++] = (p[i] >= 0x20 && p[i] <= 0x7e) ? (char)p[i] : '.';
		if (pos + 1 < linebuflen)
			linebuf[pos++] = '|';
	}
	linebuf[pos] = '\0';
	return (int)pos;
}

void kal_warn_on(uint8_t condition)
{
	if (condition)
		fprintf(stderr, "[xaga-kal] WARN_ON\n");
}

/* ---- 3. IE 解析（驱动真的靠它找 IE，所以必须真实现） ---- */

const uint8_t *kalFindIeMatchMask(uint8_t eid, const uint8_t *ies, int len,
				  const uint8_t *match, int match_len,
				  int match_offset, const uint8_t *match_mask)
{
	int pos = 0;

	if (!ies || len < 2)
		return NULL;
	while (pos + 2 <= len) {
		uint8_t ie_id = ies[pos];
		uint8_t ie_len = ies[pos + 1];
		const uint8_t *body = ies + pos + 2;
		int i, ok = 1;

		if (pos + 2 + ie_len > len)
			break;
		if (ie_id == eid && ie_len >= (uint8_t)(match_offset + match_len)) {
			for (i = 0; i < match_len; i++) {
				uint8_t m = match_mask ? match_mask[i] : 0xFF;
				uint8_t v = body[match_offset + i];

				if (match && m && (v & m) != (match[i] & m)) {
					ok = 0;
					break;
				}
			}
			if (ok)
				return body + match_offset;
		}
		pos += 2 + ie_len;
	}
	return NULL;
}

const uint8_t *kalFindIeExtIE(uint8_t eid, uint8_t exteid,
			      const uint8_t *ies, int len)
{
	int pos = 0;

	if (!ies || len < 2)
		return NULL;
	while (pos + 2 <= len) {
		uint8_t ie_id = ies[pos];
		uint8_t ie_len = ies[pos + 1];

		if (pos + 2 + ie_len > len)
			break;
		/* 扩展元素：255 后面第 1 字节是 ext id */
		if (ie_id == eid && ie_len >= 1 && ies[pos + 2] == exteid)
			return ies + pos + 2;
		pos += 2 + ie_len;
	}
	return NULL;
}

/* ---- 4. 时间与定时器 ---- */

void kal_get_monotonic_boottime(struct timespec *ts)
{
	struct timespec now;

	if (!ts)
		return;
	clock_gettime(CLOCK_MONOTONIC, &now);
	*ts = now;
}

u_int8_t kalCancelTimer(struct GLUE_INFO *prGlueInfo)
{
	/* XNU 侧对应的是 cancel_timer；宿主没有待取消的定时器，
	 * 返回 1 表示"取消成功"（驱动的语义是 bool）。
	 */
	(void)prGlueInfo;
	return 1;
}

/* ---- 5. 固件与 flavor ---- */

const char *kalGetFwFlavor(void)
{
	return "b_t";
}

int _kalRequestFirmware(const struct firmware **fw, const char *name,
			struct device *device)
{
	const uint8_t *buf = NULL;
	size_t len = 0;

	(void)device;
	if (fw)
		*fw = NULL;
	if (!name || !g_ops.load_fw)
		return -1;
	if (g_ops.load_fw(g_ops.ctx, name, &buf, &len))
		return -1;
	g_stats.fw_loads++;
	/* 真正持有 firmware 结构的是 XNU 侧的 fw 层；这里只回报"已加载" */
	(void)buf;
	(void)len;
	return 0;
}

/* ---- 6. 事件 / 复位 / 诊断 ---- */

uint32_t kalSyncTimeToFW(struct ADAPTER *prAdapter, u_int8_t fgInitCmd,
			 unsigned int second, unsigned int usecond)
{
	(void)prAdapter;
	(void)fgInitCmd;
	(void)second;
	(void)usecond;
	/* 真实实现要走 MCU 时间同步命令；宿主层不假装成功，返回非 0 由调用方记录 */
	return 0;
}

void kalSetHifIntEvent(struct GLUE_INFO *pr, unsigned long ulBit)
{
	(void)pr;
	(void)ulBit;
	g_stats.hif_int_events++;
}

void kalSetRstEvent(void)
{
	g_stats.rst_events++;
	g_stats.whole_chip_resetting = true;
}

u_int8_t kalIsWholeChipResetting(void)
{
	return g_stats.whole_chip_resetting ? 1 : 0;
}

void kalDumpTxRing(struct GLUE_INFO *prGlueInfo, struct RTMP_TX_RING *prTxRing,
		   uint32_t u4Num, bool fgDumpContent)
{
	(void)prGlueInfo;
	(void)prTxRing;
	(void)u4Num;
	(void)fgDumpContent;
	fprintf(stderr, "[xaga-kal] dump tx ring: num=%u content=%d\n",
		u4Num, (int)fgDumpContent);
}

void kalDumpRxRing(struct GLUE_INFO *prGlueInfo, struct RTMP_RX_RING *prRxRing,
		   uint32_t u4Num, bool fgDumpContent, uint32_t u4DumpLen)
{
	(void)prGlueInfo;
	(void)prRxRing;
	fprintf(stderr, "[xaga-kal] dump rx ring: num=%u content=%d len=%u\n",
		u4Num, (int)fgDumpContent, u4DumpLen);
}

u_int8_t kalSendUevent(const char *src)
{
	if (src) {
		strncpy(g_last_uevent, src, sizeof(g_last_uevent) - 1);
		g_last_uevent[sizeof(g_last_uevent) - 1] = '\0';
	}
	g_stats.uevents++;
	return 1;
}

int kalExternalAuthRequest(struct ADAPTER *prAdapter, uint8_t uBssIndex)
{
	(void)prAdapter;
	(void)uBssIndex;
	/* 真机走 cfg80211 external auth；宿主无 netdev 栈，回报"未发" */
	return -1;
}

long KAL_NEED_IMPLEMENT(const char *file, const char *func, int line, ...)
{
	fprintf(stderr, "[xaga-kal] NOT IMPLEMENTED: %s:%d %s\n",
		file ? file : "?", line, func ? func : "?");
	return 0;
}

/* ---- 7. AXI MMIO 与 conninfra ---- */

int wf_ioremap_read(xaga_phys_addr_t addr, unsigned int *val)
{
	uint32_t v = 0;
	int ret;

	if (!val)
		return -1;
	if (!g_ops.read32) {
		g_stats.mmio_errors++;
		return -1;
	}
	ret = g_ops.read32(g_ops.ctx, addr, &v);
	if (ret) {
		g_stats.mmio_errors++;
		return ret;
	}
	g_stats.mmio_reads++;
	*val = v;
	return 0;
}

int wf_ioremap_write(xaga_phys_addr_t addr, unsigned int val)
{
	int ret;

	if (!g_ops.write32) {
		g_stats.mmio_errors++;
		return -1;
	}
	ret = g_ops.write32(g_ops.ctx, addr, val);
	if (ret) {
		g_stats.mmio_errors++;
		return ret;
	}
	g_stats.mmio_writes++;
	return 0;
}

int conninfra_reg_readable(void)
{
	return g_stats.conninfra_powered ? 1 : 0;
}

int conninfra_reg_readable_for_coredump(void)
{
	return g_stats.conninfra_powered ? 1 : 0;
}

int conninfra_is_bus_hang(void)
{
	/* 0 = 正常；非 0 的错误码见 conninfra.h:139-145 */
	return 0;
}

int conninfra_pwr_on(int drv_type)
{
	(void)drv_type;
	g_stats.conninfra_powered = true;
	return 0;
}

int conninfra_pwr_off(int drv_type)
{
	(void)drv_type;
	g_stats.conninfra_powered = false;
	return 0;
}
