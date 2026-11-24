/*
 * klog_probe: find out, without a rebuild, what /dev/klog does.
 *
 * The daemon that would normally read it is not running in this ramdisk, so the device is
 * probably idle and simply has to be opened the right way. The earlier attempt failed because
 * CAT opened it O_NONBLOCK and treated a failing read() as end-of-file; this program says what
 * the errno actually was, so the next step is a fact rather than a guess.
 *
 * It also reads a file of known content through the same code path, which separates "the
 * kernel gave us nothing" from "the bytes were lost on the way back to the host".
 *
 *   klog_probe [seconds]        default 3: how long to keep polling for log text
 */

#include "mini.h"

/* mini.h does not carry these; the values are the ones bsd/sys/fcntl.h uses. */
#ifndef O_NONBLOCK
#define O_NONBLOCK      0x0004
#endif

#define PROBE_LIMIT     (24u * 1024u)

static void
hexdump(const char *tag, const unsigned char *data, unsigned length)
{
	unsigned shown = length < 64 ? length : 64;

	printf("%s: %u bytes, first %u:\n", tag, length, shown);
	for (unsigned i = 0; i < shown; i += 16) {
		printf("  %04x  ", i);
		for (unsigned j = 0; j < 16; j++) {
			if (i + j < shown) {
				printf("%02x ", data[i + j]);
			} else {
				printf("   ");
			}
		}
		printf(" |");
		for (unsigned j = 0; j < 16 && i + j < shown; j++) {
			unsigned char c = data[i + j];

			printf("%c", (c >= 32 && c < 127) ? c : '.');
		}
		printf("|\n");
	}
}

/* Read a file the plain way, so that "nothing arrived" can be pinned on the device. */
static void
check_control(const char *path)
{
	int fd = open(path, O_RDONLY, 0);
	char buffer[512];
	ssize_t got;

	if (fd < 0) {
		printf("control %s: open failed errno %d (%s)\n", path, errno, strerror(errno));
		return;
	}
	got = read(fd, buffer, sizeof(buffer) - 1);
	printf("control %s: read returned %d", path, (int)got);
	if (got < 0) {
		printf(" errno %d (%s)\n", errno, strerror(errno));
	} else {
		buffer[got] = 0;
		printf("\n");
		hexdump("  control", (const unsigned char *)buffer, (unsigned)got);
	}
	(void)close(fd);
}

int
main(int argc, char **argv, char **envp)
{
	static char buffer[PROBE_LIMIT + 1];
	unsigned seconds = 3;
	int fd;
	ssize_t got;
	unsigned total = 0;
	struct timeval start, now;

	(void)envp;
	if (argc > 1) {
		unsigned value = 0;

		for (int i = 0; argv[1][i] >= '0' && argv[1][i] <= '9'; i++) {
			value = value * 10 + (unsigned)(argv[1][i] - '0');
		}
		if (value > 0 && value < 60) {
			seconds = value;
		}
	}

	printf("klog_probe: CANARY-7f3a9c -- if you can read this, the return path works\n");

	/* first, prove the transport: a file whose content is known */
	check_control("/private/var/hakios/hakid.starts.log");
	check_control("/redwood/bin/hakid");

	/* then the device */
	printf("\n/dev/klog:\n");
	fd = open("/dev/klog", O_RDONLY | O_NONBLOCK, 0);
	printf("  open -> fd %d", fd);
	if (fd < 0) {
		printf(", errno %d (%s)\n", errno, strerror(errno));
		if (errno == 16) {
			printf("  EBUSY: something already holds the device\n");
		} else if (errno == 13 || errno == 1) {
			printf("  EACCES/EPERM: the open was refused\n");
		}
		return 0;
	}
	printf("\n");

	(void)gettimeofday(&start, NULL);
	for (;;) {
		got = read(fd, buffer + total, PROBE_LIMIT - total);

		if (got > 0) {
			total += (unsigned)got;
			if (total >= PROBE_LIMIT) {
				printf("  reached the %u byte limit\n", PROBE_LIMIT);
				break;
			}
			continue;
		}
		if (got == 0) {
			printf("  read returned 0 (end of file after %u bytes)\n", total);
			break;
		}
		/* got < 0 */
		if (total == 0) {
			printf("  first read failed: errno %d (%s)\n", errno, strerror(errno));
			if (errno == 35) {
				printf("  EAGAIN: no text queued yet -- this is the 'nothing new' case,\n"
				    "  not an error. The device is a stream that blocks or returns EAGAIN\n"
				    "  until something is written to the log.\n");
			}
		}
		(void)gettimeofday(&now, NULL);
		if (now.tv_sec - start.tv_sec >= (long)seconds) {
			break;
		}
		(void)usleep(100000);
	}
	(void)close(fd);

	buffer[total] = 0;
	printf("  total %u bytes\n", total);
	if (total > 0) {
		hexdump("  klog", (const unsigned char *)buffer, total);
	} else {
		printf("  nothing came out of /dev/klog in %u s\n", seconds);
	}

	/* and the same question about the native os_log side */
	printf("\n/dev/oslog:\n");
	fd = open("/dev/oslog", O_RDONLY | O_NONBLOCK, 0);
	if (fd < 0) {
		printf("  open failed errno %d (%s)\n", errno, strerror(errno));
		return 0;
	}
	got = read(fd, buffer, 512);
	printf("  read -> %d", (int)got);
	if (got < 0) {
		printf(", errno %d (%s)\n", errno, strerror(errno));
	} else {
		printf("\n");
		if (got > 0) {
			hexdump("  oslog", (const unsigned char *)buffer, (unsigned)got);
		}
	}
	(void)close(fd);
	return 0;
}
