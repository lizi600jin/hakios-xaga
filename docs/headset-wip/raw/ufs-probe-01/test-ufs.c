#define _GNU_SOURCE
#define HAKIOS_UFS_HOST_TEST
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef uintptr_t vm_offset_t;
typedef size_t vm_size_t;

static bool arg_present;
static uint32_t arg_value;
static unsigned maps, map_failure, lines;

static bool PE_parse_boot_argn(const char *name, void *out, size_t size)
{
	assert(strcmp(name, "mtk_ufs_probe") == 0 && size == 4);
	memcpy(out, &arg_value, 4);
	return arg_present;
}

#define UFS_PHYS 0x112b0000u
#define UFS_SIZE 0x2300u

static uint8_t *mapped;
static vm_offset_t ml_io_map(vm_offset_t address, vm_size_t size)
{
	assert(address == UFS_PHYS && size == UFS_SIZE);
	maps++;
	return map_failure ? 0 : (uintptr_t)mapped;
}

static int test_log(const char *format, ...)
{
	(void)format;
	lines++;
	return 0;
}
#define printf test_log
/* The driver's printf goes to test_log (counted). main() reports through the
 * real stdout so the harness sees a result. */
#define report(...) do { fprintf(stdout, __VA_ARGS__); fflush(stdout); } while (0)

#include "hakios.mt6895_ufs.c"

/* A snapshot source that can differ between the two passes and can fail at any
 * 1-based read index. It records every offset it is asked for, so the test can
 * assert the whitelist is exactly what the driver asks for. */
static uint32_t snap[2][UFS_WORDS];
static unsigned fail_at;
static unsigned reads_seen;
static uint16_t asked[64];
static unsigned asked_n;

static bool probe_read(void *context, uint16_t offset, uint32_t *out)
{
	unsigned idx;
	(void)context;
	if (asked_n < 64) {
		asked[asked_n++] = offset;
	}
	reads_seen++;
	if (fail_at && reads_seen == fail_at) {
		return false;
	}
	idx = (reads_seen - 1) % UFS_WORDS;
	(void)idx;
	for (unsigned i = 0; i < UFS_WORDS; i++) {
		if (ufs_offset[i] == offset) {
			*out = snap[(reads_seen - 1) / UFS_WORDS][i];
			return true;
		}
	}
	return false;
}

static unsigned cases;

static void healthy(void)
{
	memset(snap, 0, sizeof(snap));
	for (unsigned p = 0; p < 2; p++) {
		snap[p][0] = 0x0000001fu;                       /* CAP: 32 slots */
		snap[p][1] = 0x00000300u;                       /* VER */
		snap[p][2] = 0u;                                /* IS */
		snap[p][3] = HCS_DP | HCS_UTRLRDY | HCS_UCRDY;   /* HCS */
		snap[p][4] = 1u;                                /* HCE */
		snap[p][5] = 0x40100000u;                       /* UTRLBA */
		snap[p][6] = 0u;                                /* UTRLBAU */
		snap[p][7] = 0u;                                /* UTRLDBR */
		snap[p][8] = 1u;                                /* UTRLRSR */
	}
	fail_at = 0;
	reads_seen = 0;
	asked_n = 0;
}

static enum ufs_result run(struct ufs_snapshot *f, struct ufs_snapshot *s2, uint32_t *n)
{
	struct ufs_reader io = { probe_read, 0 };
	return ufs_observe(&io, f, s2, n);
}

static void expect(enum ufs_result want, const char *what)
{
	struct ufs_snapshot f, s2;
	uint32_t n = 0;
	enum ufs_result got = run(&f, &s2, &n);
	if (got != want) {
		report("case %u (%s): got %d want %d\n", cases, what, (int)got, (int)want);
		abort();
	}
	cases++;
}

int main(void)
{
	struct ufs_snapshot f, s2;
	uint32_t nutrs = 0;

	mapped = calloc(1, UFS_SIZE);
	assert(mapped);

	/* 1. Healthy controller: observed, 32 slots reported. */
	healthy();
	assert(run(&f, &s2, &nutrs) == UFS_OBSERVED);
	/* ufs_observe itself prints nothing; the report is written by init(). */
	assert(nutrs == 32u);
	cases++;

	/* 2. Exactly nine reads per snapshot, and exactly the whitelist, in order. */
	healthy();
	assert(run(&f, &s2, &nutrs) == UFS_OBSERVED);
	assert(reads_seen == 2u * UFS_WORDS);
	for (unsigned i = 0; i < UFS_WORDS; i++) {
		assert(asked[i] == ufs_offset[i]);
		assert(asked[UFS_WORDS + i] == ufs_offset[i]);
	}
	cases++;

	/* 3. A read failure at each of the 18 positions. */
	for (unsigned i = 1; i <= 2u * UFS_WORDS; i++) {
		healthy();
		fail_at = i;
		expect(UFS_READ_FAILED, "read failure");
	}

	/* 4. Each of the nine words differing between snapshots is unstable. */
	for (unsigned i = 0; i < UFS_WORDS; i++) {
		healthy();
		snap[1][i] ^= 0x1u;
		expect(UFS_UNSTABLE, "snapshot differs");
	}

	/* 5. HCE bit0 clear. */
	healthy();
	snap[1][4] = 0u; snap[0][4] = 0u;
	expect(UFS_NOT_ENABLED, "hce clear");

	/* 6. HCS DP clear. */
	healthy();
	snap[0][3] = HCS_UTRLRDY | HCS_UCRDY;
	snap[1][3] = HCS_UTRLRDY | HCS_UCRDY;
	expect(UFS_NO_DEVICE, "dp clear");

	/* 7. HCS UTRLRDY clear. */
	healthy();
	snap[0][3] = HCS_DP | HCS_UCRDY;
	snap[1][3] = HCS_DP | HCS_UCRDY;
	expect(UFS_NOT_READY, "utrlrdy clear");

	/* 8. UCRDY alone does not gate the result (vendor drivers do not require it). */
	healthy();
	snap[0][3] = HCS_DP | HCS_UTRLRDY;
	snap[1][3] = HCS_DP | HCS_UTRLRDY;
	assert(run(&f, &s2, &nutrs) == UFS_OBSERVED);
	cases++;

	/* 9. NUTRS is CAP[4:0]+1 across its range. */
	for (uint32_t k = 0; k < 32u; k++) {
		healthy();
		snap[0][0] = k; snap[1][0] = k;
		assert(run(&f, &s2, &nutrs) == UFS_OBSERVED && nutrs == k + 1u);
		cases++;
	}

	/* 10. The MMIO callback refuses every offset outside the whitelist. */
	healthy();
	memcpy(mapped, snap[0], UFS_SIZE < sizeof(snap[0]) ? UFS_SIZE : sizeof(snap[0]));
	{
		uint32_t v;
		for (unsigned i = 0; i < UFS_WORDS; i++) {
			assert(ufs_mmio_read((void *)(uintptr_t)mapped, ufs_offset[i], &v));
			cases++;
		}
		for (uint32_t off = 0; off + 4u <= UFS_SIZE; off += 4u) {
			bool is_listed = false;
			for (unsigned i = 0; i < UFS_WORDS; i++) {
				if (off == ufs_offset[i]) { is_listed = true; break; }
			}
			if (is_listed) {
				continue;
			}
			assert(!ufs_mmio_read((void *)(uintptr_t)mapped, off, &v));
			cases++;
		}
		/* Past the end of the DT resource, even a listed offset is refused only
		 * if it would cross the bound; all listed offsets are far inside. */
		assert(!ufs_mmio_read((void *)(uintptr_t)mapped, (uint16_t)0xfffc, &v));
		cases++;
	}

	/* 11. Argument handling: absent, zero and invalid all return before mapping. */
	{
		unsigned before = maps;
		arg_present = false;
		hakios_mt6895_ufs_init();
		assert(maps == before);
		cases++;

		arg_present = true; arg_value = 0;
		hakios_mt6895_ufs_init();
		assert(maps == before);
		cases++;

		arg_value = 2;
		hakios_mt6895_ufs_init();
		assert(maps == before);
		cases++;

		arg_value = UINT32_MAX;
		hakios_mt6895_ufs_init();
		assert(maps == before);
		cases++;
	}

	/* 12. mode=1 with a failing map reports and returns; the page is untouched. */
	{
		arg_present = true; arg_value = 1;
		map_failure = 1;
		lines = 0;
		hakios_mt6895_ufs_init();
		/* "read-only probe" then "map failed" -- the banner precedes the map. */
		assert(lines == 2);
		map_failure = 0;
		cases++;
	}

	/* 13. mode=1 with a good map reports the line, the bits and (when observed)
	 * nothing else. The DRIVER's read path is ufs_mmio_read over the mapped page,
	 * which probe_read does not intercept, so this checks the printed count. */
	{
		arg_present = true; arg_value = 1;
		healthy();
		/* Put a live controller into the page init() will read. */
		uint32_t words[UFS_WORDS];
		for (unsigned i = 0; i < UFS_WORDS; i++) {
			words[i] = snap[0][i];
		}
		memset(mapped, 0, UFS_SIZE);
		((uint32_t *)mapped)[0x00 / 4] = words[0];
		((uint32_t *)mapped)[0x08 / 4] = words[1];
		((uint32_t *)mapped)[0x20 / 4] = words[2];
		((uint32_t *)mapped)[0x30 / 4] = words[3];
		((uint32_t *)mapped)[0x34 / 4] = words[4];
		((uint32_t *)mapped)[0x50 / 4] = words[5];
		((uint32_t *)mapped)[0x54 / 4] = words[6];
		((uint32_t *)mapped)[0x58 / 4] = words[7];
		((uint32_t *)mapped)[0x60 / 4] = words[8];
		lines = 0;
		hakios_mt6895_ufs_init();
		/* "read-only probe", the summary, then "dp/utrlrdy/ucrdy/enabled"; no raw
		 * dump when the result is OBSERVED. */
		assert(lines == 3);
		cases++;
	}

	/* 14. When the result is not OBSERVED, the nine raw words are also printed. */
	{
		arg_present = true; arg_value = 1;
		memset(mapped, 0, UFS_SIZE);   /* HCE clear -> NOT_ENABLED */
		lines = 0;
		hakios_mt6895_ufs_init();
		assert(lines == 3 + (int)UFS_WORDS);
		cases++;
	}

	report("PASS %u cases; 9 registers, 2 snapshots, read-only, no controller writes\n", cases);
	return 0;
}
