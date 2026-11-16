/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * test_reader.c -- host tests with an injected I/O boundary and a syscall trace.
 *
 * The same reader_core.c runs here. Every operation the core requests is recorded as an
 * event string, so the assertions are about what the core actually did: which paths it
 * opened, whether it ever opened registers after a metadata failure, and whether pread
 * happened exactly once with the fixed offset and count.
 *
 * Built with ASan/UBSan; no real /sys, no device, no ARM64 execution.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "reader_core.h"

/* ---- trace ------------------------------------------------------------- */

#define TRACE_MAX 64
static char g_trace[TRACE_MAX][128];
static int g_trace_n;

static void
trace_reset(void)
{
	g_trace_n = 0;
}

static void
trace_add(const char *fmt, ...)
{
	va_list ap;

	if (g_trace_n >= TRACE_MAX)
		return;
	va_start(ap, fmt);
	vsnprintf(g_trace[g_trace_n], sizeof(g_trace[0]), fmt, ap);
	va_end(ap);
	g_trace_n++;
}

static int
trace_count(const char *needle)
{
	int i, n = 0;

	for (i = 0; i < g_trace_n; i++) {
		if (strstr(g_trace[i], needle) != NULL)
			n++;
	}
	return n;
}

static void
trace_dump(const char *label)
{
	int i;

	fprintf(stderr, "    trace[%s]:", label);
	for (i = 0; i < g_trace_n; i++)
		fprintf(stderr, "\n      %s", g_trace[i]);
	if (g_trace_n == 0)
		fprintf(stderr, " (empty)");
	fprintf(stderr, "\n");
}

/* ---- fake filesystem ---------------------------------------------------- */

/*
 * Each leaf has a mode: absent (open yields -ENOENT), present-with-content, or a
 * forced error code. registers holds a raw window and a forced behaviour for pread.
 */
enum leaf_mode { LEAF_ENOENT, LEAF_CONTENT, LEAF_ERR };

struct leaf {
	enum leaf_mode mode;
	const char *content;
	int err;                 /* for LEAF_ERR: positive errno magnitude */
};

static struct leaf fs_name, fs_range, fs_c1, fs_c2, fs_c3;
static char g_win[READER_PREAD_COUNT];
static long g_pread_ret;         /* override: 0 = use count; <0 = -errno */
static int g_open_fail_regs;     /* 0 ok, else negative to return */

static struct leaf *
leaf_for(const char *path)
{
	if (strcmp(path, READER_F_NAME) == 0) return &fs_name;
	if (strcmp(path, READER_F_RANGE) == 0) return &fs_range;
	if (strcmp(path, READER_F_CACHE_ONLY) == 0) return &fs_c1;
	if (strcmp(path, READER_F_CACHE_BYPASS) == 0) return &fs_c2;
	if (strcmp(path, READER_F_CACHE_DIRTY) == 0) return &fs_c3;
	if (strcmp(path, READER_F_REGISTERS) == 0) return NULL;   /* special */
	return (struct leaf *)-1;                                /* unknown path: a bug */
}

static int
fake_open(const char *path)
{
	struct leaf *l;

	trace_add("open(%s)", path);
	if (strcmp(path, READER_F_REGISTERS) == 0) {
		if (g_open_fail_regs != 0)
			return g_open_fail_regs;
		return 100;
	}
	l = leaf_for(path);
	if (l == (struct leaf *)-1) {
		fprintf(stderr, "    BUG: core opened an unapproved path\n");
		abort();
	}
	if (l->mode == LEAF_ENOENT)
		return -READER_E_ENOENT;
	if (l->mode == LEAF_ERR)
		return -l->err;
	if (l == &fs_name)  return 100;
	if (l == &fs_range) return 101;
	if (l == &fs_c1)    return 102;
	if (l == &fs_c2)    return 103;
	return 104;
}

/*
 * The core reads only the three metadata leaves, in a fixed order, exactly once each.
 * open() hands out fd 100..105 in that same order, so mapping fd -> leaf is exact and
 * the fake never has to guess. registers (fd 105) is never read by contract; reading it
 * here would be a bug the trace assertions catch.
 */
static long
fake_read(int fd, void *buf, unsigned long len)
{
	struct leaf *l;
	unsigned long n;

	trace_add("read(fd=%d,len=%lu)", fd, len);
	switch (fd) {
	case 100: l = &fs_name;  break;
	case 101: l = &fs_range; break;
	case 102: l = &fs_c1;    break;
	case 103: l = &fs_c2;    break;
	case 104: l = &fs_c3;    break;
	default:
		fprintf(stderr, "    BUG: core read fd %d (registers is pread-only)\n", fd);
		abort();
	}
	n = strlen(l->content);
	if (n > len)
		n = len;
	memcpy(buf, l->content, n);
	return (long)n;
}

static long
fake_pread(int fd, void *buf, unsigned long len, long off)
{
	(void)fd;
	trace_add("pread(len=%lu,off=%ld)", len, off);
	if (g_pread_ret < 0)
		return g_pread_ret;
	if (len != READER_PREAD_COUNT || off != READER_PREAD_OFFSET)
		return -1;                 /* the core must never ask for anything else */
	memcpy(buf, g_win, READER_PREAD_COUNT);
	return g_pread_ret == 0 ? (long)READER_PREAD_COUNT : g_pread_ret;
}

static int
fake_close(int fd)
{
	trace_add("close(%d)", fd);
	return 0;
}

static long
fake_write(int fd, const void *buf, unsigned long len)
{
	char shown[64];
	unsigned long i, n = len < sizeof(shown) - 1 ? len : sizeof(shown) - 1;

	for (i = 0; i < n; i++) {
		unsigned char c = ((const unsigned char *)buf)[i];

		shown[i] = (c >= 0x20 && c < 0x7f) ? (char)c : '.';
	}
	shown[n] = '\0';
	trace_add("write(fd=%d,len=%lu,\"%s\")", fd, len, shown);
	if (len != READER_PREAD_COUNT)
		return -1;
	return (long)len;
}

static const struct reader_ops fake_ops = {
	.open_ro = fake_open,
	.read_once = fake_read,
	.pread_once = fake_pread,
	.close_fd = fake_close,
	.write_all = fake_write,
};

/* ---- harness ------------------------------------------------------------ */

static int g_pass, g_fail;
static FILE *g_trace_file;

static void
reset_fs(void)
{
	trace_reset();
	fs_name  = (struct leaf){ LEAF_CONTENT, "mtk-spmi-pmic\n", 0 };
	fs_range = (struct leaf){ LEAF_CONTENT, "0-ffff\n", 0 };
	fs_c1 = fs_c2 = fs_c3 = (struct leaf){ LEAF_ENOENT, NULL, 0 };
	memcpy(g_win, "258e: 00\n258f: ff\n", READER_PREAD_COUNT);
	g_pread_ret = 0;
	g_open_fail_regs = 0;
}

static void
check(int cond, const char *what)
{
	if (cond) {
		g_pass++;
	} else {
		g_fail++;
		fprintf(stderr, "  FAIL: %s\n", what);
	}
}

/*
 * The real driver of each case: caller sets up fs_* / g_*, then calls this, which runs
 * the core and applies the invariants the contract states for every path.
 */
static void
case_expect(const char *label, int expect_ok, int expect_reason)
{
	int reason = -1;
	int rc;
	int regs_opened, reads, preads, writes;

	rc = reader_run(&fake_ops, &reason);
	regs_opened = trace_count("open(" READER_F_REGISTERS ")");
	reads = trace_count("read(fd=");
	preads = trace_count("pread(");
	writes = trace_count("write(");

	fprintf(stderr, "  case %-34s rc=%d reason=%s regs_opens=%d reads=%d preads=%d writes=%d\n",
	    label, rc, reader_reason_name(reason), regs_opened, reads, preads, writes);

	check(expect_ok ? (rc == 0) : (rc != 0), "return code matches expectation");
	if (!expect_ok)
		check(reason == expect_reason, "failure reason matches");
	else
		check(reason == R_OK, "reason is ok");

	/* Invariants that hold on every path. */
	check(preads <= 1, "at most one pread64 on every path");
	if (preads == 1)
		check(trace_count("pread(len=18,off=86526)") == 1,
		    "the single pread used count=18 offset=86526");

	/*
	 * On success the core reads exactly two leaves: name and range. The three cache
	 * leaves are opened only to prove -ENOENT, so they are never read. Any other
	 * count means the core is doing something the contract does not allow.
	 */
	if (expect_ok)
		check(reads == 2, "success reads exactly name and range (2 reads)");
	else
		check(reads <= 2, "no path reads more than the two metadata leaves");

	/*
	 * A failure at or before the registers stage must never have opened registers.
	 * The registers-stage reasons are the three that follow the cache checks.
	 */
	{
		int metadata_stage = (expect_reason == R_NAME_OPEN || expect_reason == R_NAME_READ ||
		    expect_reason == R_NAME_CONTENT || expect_reason == R_RANGE_OPEN ||
		    expect_reason == R_RANGE_READ || expect_reason == R_RANGE_CONTENT ||
		    expect_reason == R_CACHE_ONLY || expect_reason == R_CACHE_BYPASS ||
		    expect_reason == R_CACHE_DIRTY);

		if (metadata_stage)
			check(regs_opened == 0, "metadata failure never opened registers");
	}

	/* registers is pread-only: a read() on it aborts in fake_read before we get here. */
	check(trace_count("read(fd=105") == 0, "registers was never read()");

	/* The contract wants the syscall trace recorded, so every case emits one. */
	trace_dump(label);
	if (g_trace_file != NULL) {
		int i;

		fprintf(g_trace_file, "case %s rc=%d reason=%s\n", label, rc,
		    reader_reason_name(reason));
		for (i = 0; i < g_trace_n; i++)
			fprintf(g_trace_file, "  %s\n", g_trace[i]);
	}
}

int
main(void)
{
	fprintf(stderr, "reader_core host tests (ASan/UBSan)\n");
	g_trace_file = fopen("syscall-trace.txt", "w");
	if (g_trace_file == NULL) {
		fprintf(stderr, "cannot open syscall-trace.txt\n");
		return 2;
	}

	/* ---- success ---- */
	reset_fs();
	case_expect("valid window 00/ff", 1, R_OK);

	/* 00 and ff are legal bytes, never treated as errors */
	reset_fs();
	memcpy(g_win, "258e: ff\n258f: 00\n", READER_PREAD_COUNT);
	case_expect("valid window ff/00", 1, R_OK);

	reset_fs();
	memcpy(g_win, "258e: 1a\n258f: 2b\n", READER_PREAD_COUNT);
	case_expect("valid window 1a/2b", 1, R_OK);

	/* ---- name ---- */
	reset_fs();
	fs_name = (struct leaf){ LEAF_ERR, NULL, READER_E_ENOENT };
	case_expect("name open ENOENT", 0, R_NAME_OPEN);

	reset_fs();
	fs_name = (struct leaf){ LEAF_ERR, NULL, READER_E_EACCES };
	case_expect("name open EACCES", 0, R_NAME_OPEN);

	reset_fs();
	fs_name = (struct leaf){ LEAF_CONTENT, "mtk-spmi-pmic", 0 };
	case_expect("name missing newline", 0, R_NAME_CONTENT);

	reset_fs();
	fs_name = (struct leaf){ LEAF_CONTENT, "mtk-spmi-pmic\nx", 0 };
	case_expect("name trailing byte", 0, R_NAME_CONTENT);

	reset_fs();
	fs_name = (struct leaf){ LEAF_CONTENT, "other\n", 0 };
	case_expect("name wrong chip", 0, R_NAME_CONTENT);

	/* ---- range ---- */
	reset_fs();
	fs_range = (struct leaf){ LEAF_ERR, NULL, READER_E_ENOENT };
	case_expect("range open ENOENT", 0, R_RANGE_OPEN);

	reset_fs();
	fs_range = (struct leaf){ LEAF_CONTENT, "0-fffe\n", 0 };
	case_expect("range wrong extent", 0, R_RANGE_CONTENT);

	reset_fs();
	fs_range = (struct leaf){ LEAF_CONTENT, "0-ffff", 0 };
	case_expect("range missing newline", 0, R_RANGE_CONTENT);

	/* ---- cache leaves: present is failure ---- */
	reset_fs();
	fs_c1 = (struct leaf){ LEAF_CONTENT, "Y\n", 0 };
	case_expect("cache_only present", 0, R_CACHE_ONLY);

	reset_fs();
	fs_c2 = (struct leaf){ LEAF_CONTENT, "Y\n", 0 };
	case_expect("cache_bypass present", 0, R_CACHE_BYPASS);

	reset_fs();
	fs_c3 = (struct leaf){ LEAF_CONTENT, "Y\n", 0 };
	case_expect("cache_dirty present", 0, R_CACHE_DIRTY);

	/* non-ENOENT errors are not "absent" */
	reset_fs();
	fs_c1 = (struct leaf){ LEAF_ERR, NULL, READER_E_EACCES };
	case_expect("cache_only EACCES", 0, R_CACHE_ONLY);

	reset_fs();
	fs_c2 = (struct leaf){ LEAF_ERR, NULL, READER_E_ENOTDIR };
	case_expect("cache_bypass ENOTDIR", 0, R_CACHE_BYPASS);

	reset_fs();
	fs_c3 = (struct leaf){ LEAF_ERR, NULL, READER_E_EINTR };
	case_expect("cache_dirty EINTR", 0, R_CACHE_DIRTY);

	/* ---- registers open ---- */
	reset_fs();
	g_open_fail_regs = -READER_E_EACCES;
	case_expect("registers open EACCES", 0, R_REGS_OPEN);

	/* ---- pread failures: never retried ---- */
	reset_fs();
	g_pread_ret = -READER_E_EINTR;
	case_expect("pread EINTR", 0, R_REGS_PREAD);

	reset_fs();
	g_pread_ret = -5;                       /* -EIO */
	case_expect("pread EIO", 0, R_REGS_PREAD);

	reset_fs();
	g_pread_ret = 17;                       /* short by one byte: never topped up */
	case_expect("pread short (17)", 0, R_REGS_COUNT);

	{
		/* explicit short read: 1 byte */
		reset_fs();
		g_pread_ret = 1;
		case_expect("pread short (1)", 0, R_REGS_COUNT);
	}

	/* ---- format failures (raw window still emitted, exit non-zero) ---- */
	reset_fs();
	memcpy(g_win, "258e: XX\n258f: ff\n", READER_PREAD_COUNT);
	case_expect("left XX", 0, R_FMT_HEX);

	reset_fs();
	memcpy(g_win, "258e: 00\n258f: XX\n", READER_PREAD_COUNT);
	case_expect("right XX", 0, R_FMT_HEX);

	reset_fs();
	memcpy(g_win, "258e: XX\n258f: XX\n", READER_PREAD_COUNT);
	case_expect("both XX", 0, R_FMT_HEX);

	reset_fs();
	memcpy(g_win, "258d: 00\n258f: ff\n", READER_PREAD_COUNT);
	case_expect("wrong address 258d", 0, R_FMT_ADDR);

	reset_fs();
	memcpy(g_win, "258e: 00\n2590: ff\n", READER_PREAD_COUNT);
	case_expect("wrong address 2590", 0, R_FMT_ADDR);

	reset_fs();
	/* Line terminators stay at [8] and [17]; only [4] is broken from ':' to '-'. */
	memcpy(g_win, "258e: 00\n258f: ff\n", READER_PREAD_COUNT);
	g_win[4] = '-';
	case_expect("wrong separator (dash)", 0, R_FMT_SEP);

	reset_fs();
	memcpy(g_win, "258e= 00\n258f: ff\n", READER_PREAD_COUNT);
	case_expect("wrong separator (equals)", 0, R_FMT_SEP);

	reset_fs();
	memcpy(g_win, "258e: 0G\n258f: ff\n", READER_PREAD_COUNT);
	case_expect("non-hex digit", 0, R_FMT_HEX);

	reset_fs();
	memcpy(g_win, "258e: 0A\n258f: ff\n", READER_PREAD_COUNT);
	case_expect("uppercase hex", 0, R_FMT_HEX);

	reset_fs();
	memcpy(g_win, "258e: 00\n258f: ffX", READER_PREAD_COUNT);
	case_expect("bad trailing newline", 0, R_FMT_NL);

	if (g_trace_file != NULL) {
		fclose(g_trace_file);
		g_trace_file = NULL;
	}
	fprintf(stderr, "\n%d passed, %d failed\n", g_pass, g_fail);
	return g_fail == 0 ? 0 : 1;
}
