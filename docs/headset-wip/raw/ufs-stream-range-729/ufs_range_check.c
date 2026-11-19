/* Read a bounded UFS snapshot through a single open file description. */
#include "mini.h"

#define NODE "/dev/mt6895-ufs-range"

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

/* A negative position selects ordinary read(2) on the same open description. */
struct sample {
	const char *name;
	int position;
	unsigned requested;
	unsigned expected;
	uint32_t crc;
};

static const struct sample samples[] = {
	{ "cold-eof", 770, 1, 0, 0 },
	{ "head", -1, 256, 256, 0x9de8fce7u },
	{ "overlap", 250, 19, 19, 0x678d8cf7u },
	{ "middle", -1, 259, 259, 0xa6a8c96bu },
	{ "tail", -1, 512, 255, 0xc275a37cu },
	{ "eof", -1, 1, 0, 0 },
};

int
main(int argc, char **argv, char **envp)
{
	(void)argc;
	(void)argv;
	(void)envp;
	int pid = getpid();
	int writer = open(NODE, O_WRONLY, 0);
	int error = writer < 0 ? errno : 0;
	printf("ufs-range-user: trial=729 pid=%d write_error=%d\n", pid, error);
	if (writer >= 0) {
		close(writer);
		return 1;
	}
	if (error != 13 && error != 30) return 1;

	int fd = open(NODE, O_RDONLY, 0);
	if (fd < 0) {
		printf("ufs-range-user: open failed errno=%d\n", errno);
		return 1;
	}
	int ok = pid > 1;
	for (size_t i = 0; ok && i < sizeof(samples) / sizeof(samples[0]); ++i) {
		const struct sample *s = &samples[i];
		uint8_t data[514];
		memset(data, 0xa5, sizeof(data));
		int bytes = (int)(s->position < 0 ? read(fd, data + 1, s->requested) :
		    pread(fd, data + 1, s->requested, s->position));
		error = bytes < 0 ? errno : 0;
		uint32_t crc = checksum(data + 1, s->expected);
		int guards = data[0] == 0xa5;
		for (size_t j = 1 + s->expected; j < sizeof(data); ++j)
			if (data[j] != 0xa5) guards = 0;
		printf("ufs-range-user: %s pid=%d bytes=%d errno=%d crc=0x%x guards=%d\n",
		    s->name, pid, bytes, error, crc, guards);
		ok = bytes == (int)s->expected && !error && crc == s->crc && guards;
	}
	int closed = close(fd);
	ok = ok && closed == 0;
	printf("ufs-range-user: final pid=%d passed=%d close=%d\n", pid, ok, closed);
	return ok ? 0 : 1;
}
