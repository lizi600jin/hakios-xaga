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
#include <limits.h>

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
		if (strncmp(g_trace[i], needle, strlen(needle)) == 0)
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
static long g_pread_ret;
static long g_read_ret[2], g_stdout_ret;
static int g_close_fail_fd;
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
		return 105;
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
	if (len != 64 || fd < 100 || fd > 101) abort();
	if (g_read_ret[fd - 100] < 0) return g_read_ret[fd - 100];
	n = strlen(l->content);
	if (n > len)
		n = len;
	if (g_read_ret[fd - 100] != LONG_MAX && n > (unsigned long)g_read_ret[fd - 100])
		n = (unsigned long)g_read_ret[fd - 100];
	memcpy(buf, l->content, n);
	return (long)n;
}

static long
fake_pread(int fd, void *buf, unsigned long len, long off)
{
	if (fd != 105) abort();
	trace_add("pread(fd=%d,len=%lu,off=%ld)", fd, len, off);
	if (g_pread_ret < 0)
		return g_pread_ret;
	if (len != 18 || off != 86526)
		return -1;                 /* the core must never ask for anything else */
	memcpy(buf, g_win, g_pread_ret < 18 ? (unsigned long)g_pread_ret : 18);
	return g_pread_ret;
}

static int
fake_close(int fd)
{
	trace_add("close(%d)", fd);
	return fd == g_close_fail_fd ? -5 : 0;
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
	if (fd != 1 || len != 18 || memcmp(buf, g_win, 18)) abort();
	return g_stdout_ret == LONG_MAX ? (long)len : g_stdout_ret;
}

static const struct reader_ops fake_ops = {
	.open_ro = fake_open,
	.read_once = fake_read,
	.pread_once = fake_pread,
	.close_fd = fake_close,
	.write_all = fake_write,
};

/* ---- harness ------------------------------------------------------------ */

static int g_pass, g_fail, g_cases;
static FILE *g_trace_file;

static void
reset_fs(void)
{
	trace_reset();
	fs_name  = (struct leaf){ LEAF_CONTENT, "mtk-spmi-pmic\n", 0 };
	fs_range = (struct leaf){ LEAF_CONTENT, "0-ffff\n", 0 };
	fs_c1 = fs_c2 = fs_c3 = (struct leaf){ LEAF_ENOENT, NULL, 0 };
	memcpy(g_win, "258e: 00\n258f: ff\n", READER_PREAD_COUNT);
	g_pread_ret = 18;
	g_read_ret[0] = g_read_ret[1] = g_stdout_ret = LONG_MAX;
	g_close_fail_fd = -1;
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
	g_cases++;
	regs_opened = trace_count("open(" READER_F_REGISTERS ")");
	reads = trace_count("read(fd=");
	preads = trace_count("pread(fd=");
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
		check(trace_count("pread(fd=105,len=18,off=86526)") == 1,
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
		    expect_reason == R_CACHE_DIRTY || expect_reason == R_NAME_CLOSE ||
		    expect_reason == R_RANGE_CLOSE);

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

    /* Boundary failures missing from the original fixture. */
    for (int leaf = 0; leaf < 2; leaf++) {
        reset_fs(); g_read_ret[leaf] = -5;
        case_expect(leaf ? "range read EIO" : "name read EIO", 0, leaf ? R_RANGE_READ : R_NAME_READ);
        reset_fs(); g_read_ret[leaf] = -4;
        case_expect(leaf ? "range read EINTR" : "name read EINTR", 0, leaf ? R_RANGE_READ : R_NAME_READ);
        reset_fs(); g_read_ret[leaf] = 0;
        case_expect(leaf ? "range read zero" : "name read zero", 0, leaf ? R_RANGE_CONTENT : R_NAME_CONTENT);
        reset_fs(); g_read_ret[leaf] = 1;
        case_expect(leaf ? "range read short" : "name read short", 0, leaf ? R_RANGE_CONTENT : R_NAME_CONTENT);
    }
    reset_fs(); g_pread_ret = 0;
    case_expect("pread zero", 0, R_REGS_COUNT);
    const int close_fds[] = {100, 101, 105};
    const int close_reasons[] = {R_NAME_CLOSE, R_RANGE_CLOSE, R_REGS_CLOSE};
    for (unsigned i = 0; i < 3; i++) {
        reset_fs(); g_close_fail_fd = close_fds[i];
        case_expect("close EIO", 0, close_reasons[i]);
    }
    const long stdout_errors[] = {17, 0, -5, -4};
    for (unsigned i = 0; i < sizeof(stdout_errors)/sizeof(stdout_errors[0]); i++) {
        reset_fs(); g_stdout_ret = stdout_errors[i];
        case_expect("stdout incomplete/error", 0, R_STDOUT_WRITE);
    }


	if (g_trace_file != NULL) {
		fclose(g_trace_file);
		g_trace_file = NULL;
	}
	fprintf(stderr, "\n%d cases; %d assertions passed, %d failed\n", g_cases, g_pass, g_fail);
	return g_fail == 0 ? 0 : 1;
}
