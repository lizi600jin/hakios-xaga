/* SPDX-License-Identifier: GPL-2.0 */
/*
 * reader_start.c -- ARM64 Linux entry and the real syscall boundary.
 *
 * Freestanding: no libc, no crt, no dynamic loader. Syscall numbers are the
 * asm-generic values the contract lists (openat=56 close=57 read=63 write=64
 * pread64=67 exit=93), which the fixed-commit include/uapi/asm-generic/unistd.h
 * confirms. The host test replaces this whole file with an injected boundary and
 * runs the same reader_core.c.
 */
#include "reader_core.h"

#define SYS_OPENAT 56
#define SYS_CLOSE  57
#define SYS_READ   63
#define SYS_WRITE  64
#define SYS_PREAD64 67
#define SYS_EXIT   93

#define AT_FDCWD     (-100)
#define O_RDONLY     0
#define O_CLOEXEC    02000000
#define O_NOFOLLOW   0400000

static inline long
sc1(long n, long a)
{
	register long x8 __asm__("x8") = n;
	register long x0 __asm__("x0") = a;
	__asm__ volatile("svc #0" : "+r"(x0) : "r"(x8) : "memory");
	return x0;
}

static inline long
sc3(long n, long a, long b, long c)
{
	register long x8 __asm__("x8") = n;
	register long x0 __asm__("x0") = a;
	register long x1 __asm__("x1") = b;
	register long x2 __asm__("x2") = c;
	__asm__ volatile("svc #0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2) : "memory");
	return x0;
}

static inline long
sc4(long n, long a, long b, long c, long d)
{
	register long x8 __asm__("x8") = n;
	register long x0 __asm__("x0") = a;
	register long x1 __asm__("x1") = b;
	register long x2 __asm__("x2") = c;
	register long x3 __asm__("x3") = d;
	__asm__ volatile("svc #0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2), "r"(x3) : "memory");
	return x0;
}

/* openat(AT_FDCWD, path, O_RDONLY|O_CLOEXEC|O_NOFOLLOW, 0) -> fd or -errno */
static int
sys_open_ro(const char *path)
{
	return (int)sc4(SYS_OPENAT, AT_FDCWD, (long)path,
	    O_RDONLY | O_CLOEXEC | O_NOFOLLOW, 0);
}

static long
sys_read_once(int fd, void *buf, unsigned long len)
{
	return sc3(SYS_READ, fd, (long)buf, (long)len);
}

static long
sys_pread_once(int fd, void *buf, unsigned long len, long off)
{
	register long x8 __asm__("x8") = SYS_PREAD64;
	register long x0 __asm__("x0") = fd;
	register long x1 __asm__("x1") = (long)buf;
	register long x2 __asm__("x2") = (long)len;
	register long x3 __asm__("x3") = off;
	__asm__ volatile("svc #0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2), "r"(x3) : "memory");
	return x0;
}

static int
sys_close(int fd)
{
	return (int)sc1(SYS_CLOSE, fd);
}

static long
sys_write_all(int fd, const void *buf, unsigned long len)
{
	long total = 0;

	/* stdout only: a short write is reported, never silently padded. */
	while ((unsigned long)total < len) {
		long n = sc3(SYS_WRITE, fd, (long)buf + total, (long)(len - (unsigned long)total));

		if (n < 0)
			return n;
		if (n == 0)
			break;
		total += n;
	}
	return total;
}

static unsigned long
str_len(const char *s)
{
	unsigned long n = 0;

	while (s[n] != '\0')
		n++;
	return n;
}

static void
sys_exit(int code)
{
	(void)sc1(SYS_EXIT, code);
	for (;;)
		;
}

static const struct reader_ops real_ops = {
	.open_ro = sys_open_ro,
	.read_once = sys_read_once,
	.pread_once = sys_pread_once,
	.close_fd = sys_close,
	.write_all = sys_write_all,
};

/*
 * _start is the entry. The kernel passes argc/argv on the stack; the contract accepts
 * no arguments at all, so they are ignored and the fixed map is the only target.
 */
void
_start(void)
{
	int reason;
	int rc = reader_run(&real_ops, &reason);

	if (rc != 0) {
		const char *prefix = "regmap-reader: ";
		const char *msg = reader_reason_name(reason);

		(void)sys_write_all(2, prefix, str_len(prefix));
		(void)sys_write_all(2, msg, str_len(msg));
		(void)sys_write_all(2, "\n", 1);
		sys_exit(1);
	}
	sys_exit(0);
}
