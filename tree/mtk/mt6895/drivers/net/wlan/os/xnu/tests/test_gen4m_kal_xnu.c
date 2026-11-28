/*
 * test_gen4m_kal_xnu.c — 宿主侧验证 gen4m KAL 适配层。
 * 断言全部走真实签名，任何一条 ABI 错位都会在编译期就暴露。
 */
#include "../os_xnu/hakios_gen4m_kal_xnu.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

static int g_fail;
static int g_pass;

#define CHECK(cond, name) do { \
	if (cond) { g_pass++; } \
	else { g_fail++; printf("FAIL: %s\n", name); } \
} while (0)

/* ---- mock MMIO：16 个 32 位寄存器 + 故障开关 ---- */
static uint32_t g_regs[16];
static int g_mmio_bad;

static int mock_read(void *ctx, xaga_phys_addr_t addr, uint32_t *val)
{
	(void)ctx;
	if (g_mmio_bad)
		return -1;
	if (addr >= sizeof(g_regs))
		return -1;
	*val = g_regs[addr / 4];
	return 0;
}

static int mock_write(void *ctx, xaga_phys_addr_t addr, uint32_t val)
{
	(void)ctx;
	if (g_mmio_bad)
		return -1;
	if (addr >= sizeof(g_regs))
		return -1;
	g_regs[addr / 4] = val;
	return 0;
}

static int mock_load_fw(void *ctx, const char *name,
			const uint8_t **out_buf, size_t *out_len)
{
	static const uint8_t fake[] = { 0xA5, 0x5A };

	(void)ctx;
	if (!name || strcmp(name, "mt6895_b_t.bin") != 0)
		return -1;
	*out_buf = fake;
	*out_len = sizeof(fake);
	return 0;
}

int main(void)
{
	struct hakios_gen4m_kal_ops ops;
	const struct hakios_gen4m_kal_stats *st;
	struct timespec ts;
	unsigned long bits = 0;
	char line[128];
	char ie_buf[8];
	uint8_t u8v = 0;
	unsigned int mmio_val = 0;
	const uint8_t *found;
	struct sk_buff *skb;
	void *p;

	memset(&ops, 0, sizeof(ops));
	ops.read32 = mock_read;
	ops.write32 = mock_write;
	ops.load_fw = mock_load_fw;
	hakios_gen4m_kal_bind_ops(&ops);

	/* 1. MMIO 往返 */
	CHECK(wf_ioremap_write(0x08, 0x1234u) == 0, "wf_ioremap_write ok");
	CHECK(wf_ioremap_read(0x08, &mmio_val) == 0 && mmio_val == 0x1234u,
	      "wf_ioremap_read back");
	CHECK(wf_ioremap_read(0x0C, &mmio_val) == 0, "second mmio read ok");
	CHECK(wf_ioremap_read(0x100, &mmio_val) != 0, "out-of-range read rejected");

	/* 2. 内存 */
	p = kal_kmalloc(32, 0);
	CHECK(p != NULL, "kal_kmalloc");
	kal_kfree(p);
	p = kal_vmalloc(64);
	CHECK(p != NULL, "kal_vmalloc");
	kal_vfree(p);

	/* 3. skb */
	skb = kal_dev_alloc_skb(100);
	CHECK(skb != NULL, "kal_dev_alloc_skb");
	if (skb) {
		unsigned int skb_len = 0;
		unsigned char *skb_data = NULL;

		CHECK(hakios_gen4m_skb_probe(skb, &skb_len, &skb_data) == 0 &&
		      skb_len == 100 && skb_data != NULL, "skb probe");
		kal_kfree_skb(skb);
	}

	/* 4. 位操作 */
	kal_set_bit(5, &bits);
	CHECK(kal_test_bit(5, &bits) == 1, "set/test bit 5");
	CHECK(kal_test_bit(4, &bits) == 0, "bit 4 stays clear");

	/* 5. 格式化与解析 */
	CHECK(kal_scnprintf(line, sizeof(line), "%d/%s", 42, "xaga") > 0, "kal_scnprintf");
	CHECK(kal_strtou8("255", 10, &u8v) == 0 && u8v == 255, "kal_strtou8");
	CHECK(kal_strtou8("999", 10, &u8v) == -1, "kal_strtou8 rejects overflow");
	CHECK(kal_hex_dump_to_buffer("AB", 2, 16, 1, line, sizeof(line), true) > 0,
	      "kal_hex_dump_to_buffer");

	/* 6. IE 解析：构造 [0xFF 0x04 0x35 XX XX]，找 ext id 0x35 */
	ie_buf[0] = (char)0xFF;
	ie_buf[1] = 3;   /* IE body 只有 3 字节：0x35 0x11 0x22 */
	ie_buf[2] = 0x35;
	ie_buf[3] = 0x11;
	ie_buf[4] = 0x22;
	found = kalFindIeExtIE(0xFF, 0x35, (const uint8_t *)ie_buf, 5);
	CHECK(found == (const uint8_t *)ie_buf + 2, "kalFindIeExtIE finds ext id");
	CHECK(kalFindIeExtIE(0xFF, 0x36, (const uint8_t *)ie_buf, 5) == NULL,
	      "kalFindIeExtIE misses wrong ext id");

	/* 7. 时间 */
	kal_get_monotonic_boottime(&ts);
	CHECK(ts.tv_sec >= 0, "monotonic boottime filled");

	/* 8. 定时器 */
	CHECK(kalCancelTimer(NULL) == 1, "kalCancelTimer returns true");

	/* 9. flavor 与固件 */
	CHECK(kalGetFwFlavor() != NULL && strcmp(kalGetFwFlavor(), "b_t") == 0,
	      "kalGetFwFlavor = b_t");
	CHECK(_kalRequestFirmware(NULL, "mt6895_b_t.bin", NULL) == 0, "request firmware ok");
	CHECK(_kalRequestFirmware(NULL, "nope.bin", NULL) != 0, "request firmware miss");

	/* 10. 事件与复位 */
	kalSetHifIntEvent(NULL, 3);
	kalSetRstEvent();
	CHECK(kalIsWholeChipResetting() == 1, "whole chip resetting set");
	CHECK(kalSendUevent("WIFI:reset") == 1, "kalSendUevent");

	/* 11. conninfra */
	CHECK(conninfra_pwr_on(0) == 0, "conninfra_pwr_on");
	CHECK(conninfra_reg_readable() == 1, "conninfra readable after power on");
	CHECK(conninfra_is_bus_hang() == 0, "conninfra no bus hang");
	CHECK(conninfra_pwr_off(0) == 0, "conninfra_pwr_off");
	CHECK(conninfra_reg_readable() == 0, "conninfra not readable after power off");

	/* 12. KAL_NEED_IMPLEMENT 返回 long，驱动把它当表达式用 */
	CHECK(KAL_NEED_IMPLEMENT("t.c", "fn", 7) == 0, "KAL_NEED_IMPLEMENT returns long");

	/* 13. 统计 */
	st = hakios_gen4m_kal_get_stats();
	CHECK(st != NULL, "stats pointer");
	CHECK(st->mmio_writes >= 1 && st->mmio_reads >= 2, "mmio counters");
	CHECK(st->skb_allocs == 1 && st->skb_frees == 1, "skb counters");
	CHECK(st->fw_loads == 1, "fw counter");
	CHECK(st->hif_int_events == 1 && st->rst_events == 1, "event counters");
	CHECK(st->uevents == 1, "uevent counter");

	/* 14. 错误路径 */
	g_mmio_bad = 1;
	CHECK(wf_ioremap_read(0x00, &mmio_val) != 0, "mmio error surfaced");
	CHECK(hakios_gen4m_kal_get_stats()->mmio_errors >= 1, "mmio error counted");
	g_mmio_bad = 0;

	printf("%d passed, %d failed\n", g_pass, g_fail);
	return g_fail ? 1 : 0;
}
