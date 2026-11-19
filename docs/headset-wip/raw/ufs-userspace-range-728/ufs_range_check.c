/* Read the same two-block window as Trial727 through an ordinary process file descriptor. */
#include "mini.h"

#define NODE "/dev/mt6895-ufs-range"
#define RANGE_LENGTH 770
#define RANGE_OFFSET 12031
#define WINDOW_END 65536

static uint32_t
checksum(const uint8_t *data, size_t size)
{
	uint32_t crc = ~0u;
	for (size_t i = 0; i < size; ++i) {
		crc ^= data[i];
		for (int bit = 0; bit < 8; ++bit)
			crc = (crc >> 1) ^ (UINT32_C(0xedb88320) & (0u - (crc & 1u)));
	}
	return ~crc;
}

int
main(int argc, char **argv, char **envp)
{
	(void)argc;
	(void)argv;
	(void)envp;
	int pid = getpid();
	int writer = open(NODE, O_WRONLY, 0);
	int error = writer < 0 ? errno : 0;
	printf("ufs-range-user: trial=728 pid=%d write_error=%d\n", pid, error);
	if (writer >= 0) {
		close(writer);
		return 1;
	}
	/* Either the read-only mode or the device's write-open handler can refuse the open. */
	if (error != 13 && error != 30) return 1;

	int fd = open(NODE, O_RDONLY, 0);
	if (fd < 0) {
		printf("ufs-range-user: open failed errno=%d\n", errno);
		return 1;
	}

	uint8_t data[RANGE_LENGTH + 2];
	memset(data, 0xa5, sizeof(data));
	int bytes = (int)pread(fd, data + 1, RANGE_LENGTH, RANGE_OFFSET);
	error = bytes < 0 ? errno : 0;
	uint32_t crc = checksum(data + 1, RANGE_LENGTH);
	int guards = data[0] == 0xa5 && data[sizeof(data)-1] == 0xa5;
	printf("ufs-range-user: read pid=%d bytes=%d errno=%d crc=0x%x guards=%d\n",
	    pid, bytes, error, crc, guards);
	int ok = bytes == RANGE_LENGTH && error == 0 && crc == UINT32_C(0x8f350378) && guards;
	if (ok) {
		uint8_t at_end = 0x5a;
		bytes = (int)pread(fd, &at_end, 1, WINDOW_END);
		error = bytes < 0 ? errno : 0;
		printf("ufs-range-user: eof pid=%d bytes=%d errno=%d unchanged=%d\n",
		    pid, bytes, error, at_end == 0x5a);
		ok = bytes == 0 && !error && at_end == 0x5a;
	}
	if (ok) {
		memset(data, 0x5a, sizeof(data));
		bytes = (int)pread(fd, data + 1, RANGE_LENGTH, RANGE_OFFSET);
		error = bytes < 0 ? errno : 0;
		int unchanged = 1;
		for (size_t i = 0; i < sizeof(data); ++i)
			if (data[i] != 0x5a) unchanged = 0;
		printf("ufs-range-user: repeat pid=%d bytes=%d errno=%d unchanged=%d\n",
		    pid, bytes, error, unchanged);
		ok = bytes == -1 && error == 16 && unchanged;
	}
	int closed = close(fd);
	ok = ok && closed == 0 && pid > 1;
	printf("ufs-range-user: final pid=%d passed=%d close=%d\n", pid, ok, closed);
	return ok ? 0 : 1;
}
