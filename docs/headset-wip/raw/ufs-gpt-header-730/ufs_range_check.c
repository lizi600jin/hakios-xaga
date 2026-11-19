/* Validate the primary GPT header from the read-only LUN 2 snapshot. */
#include "mini.h"

#define NODE "/dev/mt6895-ufs-range"
#define BLOCK_BYTES 4096u
/* Last logical block from the previously measured xaga LUN 2 capacity. */
#define LAST_LBA UINT64_C(31240191)

static uint32_t
le32(const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
	    ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t
le64(const uint8_t *p)
{
	return le32(p) | ((uint64_t)le32(p + 4) << 32);
}

/* UEFI 5.3.2 includes HeaderSize bytes with the CRC field treated as zero. */
static uint32_t
header_crc(const uint8_t *p, uint32_t size)
{
	uint32_t crc = ~0u;
	for (uint32_t i = 0; i < size; ++i) {
		crc ^= i >= 16u && i < 20u ? 0u : p[i];
		for (int bit = 0; bit < 8; ++bit)
			crc = (crc >> 1) ^ (UINT32_C(0xedb88320) & (0u - (crc & 1u)));
	}
	return ~crc;
}

static int
check_header(const uint8_t *p, int pid)
{
	uint32_t size = le32(p + 12);
	if (memcmp(p, "EFI PART", 8) || le32(p + 8) != 0x10000 || size < 92 ||
	    size > BLOCK_BYTES || le32(p + 20) != 0) return 0;
	uint32_t actual = header_crc(p, size);
	if (actual != le32(p + 16)) return 0;
	uint64_t first = le64(p + 40), last = le64(p + 48), entries = le64(p + 72);
	uint32_t count = le32(p + 80), stride = le32(p + 84);
	if (le64(p + 24) != 1 || le64(p + 32) != LAST_LBA || first > last ||
	    last >= LAST_LBA || entries < 2 || entries >= first || !count ||
	    stride < 128 || (stride & (stride - 1u))) return 0;
	uint64_t bytes = (uint64_t)count * stride;
	uint64_t blocks = bytes / BLOCK_BYTES + (bytes % BLOCK_BYTES != 0);
	if (bytes < 16384 || blocks > first - entries || blocks > LAST_LBA - last - 1) return 0;
	printf("ufs-range-user: header pid=%d size=%u crc=0x%x self=1 alternate=%llu first=%llu last=%llu\n",
	    pid, size, actual, (unsigned long long)LAST_LBA, (unsigned long long)first,
	    (unsigned long long)last);
	printf("ufs-range-user: layout pid=%d entries_lba=%llu count=%u stride=%u blocks=%llu\n",
	    pid, (unsigned long long)entries, count, stride, (unsigned long long)blocks);
	return 1;
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
	printf("ufs-range-user: trial=730 pid=%d write_error=%d\n", pid, error);
	if (writer >= 0) {
		close(writer);
		return 1;
	}
	if (pid <= 1 || (error != 13 && error != 30)) return 1;
	int fd = open(NODE, O_RDONLY, 0);
	if (fd < 0) return 1;
	static uint8_t data[BLOCK_BYTES + 2u];
	memset(data, 0xa5, sizeof(data));
	int bytes = (int)read(fd, data + 1, BLOCK_BYTES);
	int guards = data[0] == 0xa5 && data[sizeof(data)-1] == 0xa5;
	printf("ufs-range-user: block pid=%d bytes=%d errno=%d guards=%d\n",
	    pid, bytes, bytes < 0 ? errno : 0, guards);
	int ok = bytes == (int)BLOCK_BYTES && guards && check_header(data + 1, pid);
	if (ok) {
		uint8_t sentinel = 0x5a;
		bytes = (int)read(fd, &sentinel, 1);
		printf("ufs-range-user: eof pid=%d bytes=%d unchanged=%d\n", pid, bytes, sentinel == 0x5a);
		ok = bytes == 0 && sentinel == 0x5a;
	}
	int closed = close(fd);
	ok = ok && closed == 0;
	printf("ufs-range-user: final pid=%d passed=%d close=%d\n", pid, ok, closed);
	return ok ? 0 : 1;
}
