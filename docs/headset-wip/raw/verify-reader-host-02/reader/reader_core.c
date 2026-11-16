/* SPDX-License-Identifier: GPL-2.0 */
/*
 * reader_core.c -- contract-compliant core. No libc, no allocation, no state.
 *
 * Order is fixed by the contract and enforced by control flow, not by data:
 * name -> range -> three cache leaves -> registers (one pread64) -> stdout.
 * Anything that fails stops; nothing is retried and no alternative path is tried.
 */
#include "reader_core.h"

static int
streq_exact(const char *got, unsigned long got_len, const char *want, unsigned long want_len)
{
	unsigned long i;

	if (got_len != want_len)
		return 0;
	for (i = 0; i < want_len; i++) {
		if (got[i] != want[i])
			return 0;
	}
	return 1;
}

static const char want_name[] = "mtk-spmi-pmic\n";
static const char want_range[] = "0-ffff\n";

const char *
reader_reason_name(int reason)
{
	switch (reason) {
	case R_OK:              return "ok";
	case R_NAME_OPEN:       return "name: open failed";
	case R_NAME_READ:       return "name: read failed";
	case R_NAME_CONTENT:    return "name: content is not \"mtk-spmi-pmic\\n\"";
	case R_RANGE_OPEN:      return "range: open failed";
	case R_RANGE_READ:      return "range: read failed";
	case R_RANGE_CONTENT:   return "range: content is not \"0-ffff\\n\"";
	case R_CACHE_ONLY:      return "cache_only: open did not return ENOENT";
	case R_CACHE_BYPASS:    return "cache_bypass: open did not return ENOENT";
	case R_CACHE_DIRTY:     return "cache_dirty: open did not return ENOENT";
	case R_REGS_OPEN:       return "registers: open failed";
	case R_REGS_PREAD:      return "registers: pread failed or was interrupted";
	case R_REGS_COUNT:      return "registers: pread did not return exactly 18";
	case R_FMT_ADDR:        return "registers: window address is not 258e/258f";
	case R_FMT_SEP:         return "registers: separator is not ':' + space";
	case R_FMT_HEX:         return "registers: value is not two lowercase hex digits";
	case R_FMT_NL:          return "registers: line terminator or extent is wrong";
	case R_STDOUT_WRITE:    return "stdout: write failed or was short";
	case R_NAME_CLOSE:      return "name: close failed";
	case R_RANGE_CLOSE:     return "range: close failed";
	case R_REGS_CLOSE:      return "registers: close failed";
	default:                return "unknown";
	}
}

/* Reads one metadata leaf with a single read, closes it, compares exactly. */
static int
read_meta(const struct reader_ops *ops, const char *path, const char *want,
    unsigned long want_len, int reason_open, int reason_read, int reason_content,
    int reason_close, int *out_reason)
{
	char buf[READER_META_MAX];
	int fd;
	long n;
	int closed;

	fd = ops->open_ro(path);
	if (fd < 0) {
		*out_reason = reason_open;
		return -1;
	}
	/* One read. No loop, so a short read is a content mismatch, not a retry. */
	n = ops->read_once(fd, buf, (unsigned long)READER_META_MAX);
	closed = ops->close_fd(fd);
	if (n < 0) {
		*out_reason = reason_read;
		return -1;
	}
	if (closed < 0) {
		*out_reason = reason_close;
		return -1;
	}
	if (!streq_exact(buf, (unsigned long)n, want, want_len)) {
		*out_reason = reason_content;
		return -1;
	}
	return 0;
}

/*
 * A cache leaf must be absent. Only -ENOENT proves absence: anything that opens, and
 * EACCES/ENOTDIR/EINTR or any other errno, fails. open_ro returns -errno, so the
 * magnitude is compared directly against READER_E_ENOENT.
 */
static int
require_enoent(const struct reader_ops *ops, const char *path, int reason, int *out_reason)
{
	int fd;

	fd = ops->open_ro(path);
	if (fd >= 0) {
		(void)ops->close_fd(fd);
		*out_reason = reason;
		return -1;
	}
	if (fd == -READER_E_ENOENT)
		return 0;
	*out_reason = reason;
	return -1;
}

static int
is_lower_hex(char c)
{
	return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
}

/*
 * Validates "258e: NN\n258f: NN\n" exactly. XX is not hex, so it fails here; 00 and ff
 * are valid bytes and pass.
 */
static int
window_format_ok(const char *w, int *out_reason)
{
	static const char want_addr[2][5] = { "258e", "258f" };
	int line, i;

	if ((unsigned char)w[8] != '\n' || (unsigned char)w[17] != '\n') {
		*out_reason = R_FMT_NL;
		return -1;
	}
	for (line = 0; line < 2; line++) {
		const char *p = w + line * 9;

		for (i = 0; i < 4; i++) {
			if (p[i] != want_addr[line][i]) {
				*out_reason = R_FMT_ADDR;
				return -1;
			}
		}
		if (p[4] != ':' || p[5] != ' ') {
			*out_reason = R_FMT_SEP;
			return -1;
		}
		if (!is_lower_hex(p[6]) || !is_lower_hex(p[7])) {
			*out_reason = R_FMT_HEX;
			return -1;
		}
	}
	return 0;
}

int
reader_run(const struct reader_ops *ops, int *reason)
{
	char win[READER_PREAD_COUNT];
	int fd;
	long n;

	*reason = R_OK;

	/* 1. name */
	if (read_meta(ops, READER_F_NAME, want_name, sizeof(want_name) - 1,
	    R_NAME_OPEN, R_NAME_READ, R_NAME_CONTENT, R_NAME_CLOSE, reason) != 0)
		return -1;

	/* 2. range */
	if (read_meta(ops, READER_F_RANGE, want_range, sizeof(want_range) - 1,
	    R_RANGE_OPEN, R_RANGE_READ, R_RANGE_CONTENT, R_RANGE_CLOSE, reason) != 0)
		return -1;

	/* 3. the three cache leaves must be absent, each independently */
	if (require_enoent(ops, READER_F_CACHE_ONLY, R_CACHE_ONLY, reason) != 0)
		return -1;
	if (require_enoent(ops, READER_F_CACHE_BYPASS, R_CACHE_BYPASS, reason) != 0)
		return -1;
	if (require_enoent(ops, READER_F_CACHE_DIRTY, R_CACHE_DIRTY, reason) != 0)
		return -1;

	/* 4. registers, only now */
	fd = ops->open_ro(READER_F_REGISTERS);
	if (fd < 0) {
		*reason = R_REGS_OPEN;
		return -1;
	}

	/* 5. exactly one pread64. No fallback to read(), no loop, no second pread. */
	n = ops->pread_once(fd, win, (unsigned long)READER_PREAD_COUNT, READER_PREAD_OFFSET);
	if (n < 0) {
		(void)ops->close_fd(fd);
		*reason = R_REGS_PREAD;      /* EINTR is a failure, never retried */
		return -1;
	}
	if (ops->close_fd(fd) < 0) {
		*reason = R_REGS_CLOSE;
		return -1;
	}
	if (n != READER_PREAD_COUNT) {
		*reason = R_REGS_COUNT;      /* short read is a failure, never topped up */
		return -1;
	}

	/*
	 * 7. Format check first. A format failure still emits the raw window so the
	 * evidence survives, but the exit status is non-zero.
	 */
	if (window_format_ok(win, reason) != 0) {
		(void)ops->write_all(1, win, (unsigned long)READER_PREAD_COUNT);
		return -1;
	}

	/* 8. Raw bytes only on stdout, nothing else. */
	n = ops->write_all(1, win, (unsigned long)READER_PREAD_COUNT);
	if (n != (long)READER_PREAD_COUNT) {
		*reason = R_STDOUT_WRITE;
		return -1;
	}
	return 0;
}
