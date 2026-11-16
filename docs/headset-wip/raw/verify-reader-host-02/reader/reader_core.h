/* SPDX-License-Identifier: GPL-2.0 */
/*
 * reader_core.h -- fixed-point regmap reader, contract-compliant core.
 *
 * The only target is /sys/kernel/debug/regmap/0-05/. No path, address, length or
 * offset is caller-supplied: every constant below comes from READER-CONTRACT.md.
 *
 * This header deliberately defines its own errno magnitudes so the ARM64 build needs
 * no libc and the host test needs no /sys. They match Linux asm-generic values.
 */
#ifndef READER_CORE_H
#define READER_CORE_H

#define READER_MAP_DIR        "/sys/kernel/debug/regmap/0-05/"
#define READER_F_NAME         READER_MAP_DIR "name"
#define READER_F_RANGE        READER_MAP_DIR "range"
#define READER_F_CACHE_ONLY   READER_MAP_DIR "cache_only"
#define READER_F_CACHE_BYPASS READER_MAP_DIR "cache_bypass"
#define READER_F_CACHE_DIRTY  READER_MAP_DIR "cache_dirty"
#define READER_F_REGISTERS    READER_MAP_DIR "registers"

/* One read per metadata leaf, at most this many bytes. */
#define READER_META_MAX       64

/*
 * registers is addressed, never parsed as a stream: one pread64 at the byte offset
 * that is 9614 * 9, because a readable range prints "xxxx: NN\n" = 9 bytes per line.
 * Two lines -> 18 bytes. 0x258e * 9 = 86526, so the window starts at 0x258e.
 */
#define READER_PREAD_OFFSET   86526L
#define READER_PREAD_COUNT    18

#define READER_E_ENOENT       2
#define READER_E_EINTR        4
#define READER_E_EACCES       13
#define READER_E_ENOTDIR      20

/* Failure reasons. Only R_OK maps to exit 0. */
enum reader_reason {
	R_OK = 0,
	R_NAME_OPEN, R_NAME_READ, R_NAME_CONTENT,
	R_RANGE_OPEN, R_RANGE_READ, R_RANGE_CONTENT,
	R_CACHE_ONLY, R_CACHE_BYPASS, R_CACHE_DIRTY,
	R_REGS_OPEN, R_REGS_PREAD, R_REGS_COUNT,
	R_FMT_ADDR, R_FMT_SEP, R_FMT_HEX, R_FMT_NL,
	R_STDOUT_WRITE,
	R_NAME_CLOSE, R_RANGE_CLOSE, R_REGS_CLOSE,
	R__MAX
};

/*
 * Every operation the core may perform. Returning a negative value means -errno.
 * There is no way to request a write, an ioctl, an mmap, a second open of
 * registers, or a path other than the six above: the core has no other vocabulary.
 */
struct reader_ops {
	int  (*open_ro)(const char *path);
	long (*read_once)(int fd, void *buf, unsigned long len);
	long (*pread_once)(int fd, void *buf, unsigned long len, long off);
	int  (*close_fd)(int fd);
	long (*write_all)(int fd, const void *buf, unsigned long len);
};

const char *reader_reason_name(int reason);

/*
 * Runs the contract. On success writes the 18 raw bytes to fd 1 and returns 0.
 * On any failure writes nothing to fd 1 (except the raw window when the pread itself
 * succeeded, which the contract allows for evidence) and returns -1 with *reason set.
 */
int reader_run(const struct reader_ops *ops, int *reason);

#endif /* READER_CORE_H */
