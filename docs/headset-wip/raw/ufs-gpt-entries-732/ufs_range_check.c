/* Validate the primary GPT header and the entry array from the read-only LUN 2 snapshot.
 *
 * The snapshot is one 20480-byte window: offset 0 is the 4096-byte header block, offset 4096 is the
 * 16384-byte entry array. The array is read whole and its CRC32 is recomputed over exactly the
 * count*stride bytes the header declares, then compared with the value stored in the header.
 */
#include "mini.h"

#define NODE "/dev/mt6895-ufs-range"
#define BLOCK_BYTES 4096u
#define ARRAY_BYTES 16384u
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

static uint32_t
crc_update(uint32_t crc, const uint8_t *p, size_t length)
{
	for (size_t i = 0; i < length; ++i) {
		crc ^= p[i];
		for (int bit = 0; bit < 8; ++bit)
			crc = (crc >> 1) ^ (UINT32_C(0xedb88320) & (0u - (crc & 1u)));
	}
	return crc;
}

/* UEFI 5.3.2 includes HeaderSize bytes with the CRC field treated as zero. */
static uint32_t
header_crc(const uint8_t *p, uint32_t size)
{
	static const uint8_t zero[4];
	uint32_t crc = crc_update(~0u, p, 16);
	crc = crc_update(crc, zero, sizeof(zero));
	return ~crc_update(crc, p + 20, size - 20);
}

struct gpt_layout {
	uint64_t first_usable;
	uint64_t last_usable;
	uint32_t count;
	uint32_t stride;
	uint32_t array_crc;
};
static int
check_header(const uint8_t *p, int pid, struct gpt_layout *layout)
{
	uint32_t size = le32(p + 12);
	if (memcmp(p, "EFI PART", 8) || le32(p + 8) != 0x10000 || size < 92 ||
	    size > BLOCK_BYTES || le32(p + 20) != 0) return 0;
	uint32_t actual = header_crc(p, size);
	if (actual != le32(p + 16)) return 0;
	uint64_t first = le64(p + 40), last = le64(p + 48), entries = le64(p + 72);
	uint32_t count = le32(p + 80), stride = le32(p + 84);
	if (le64(p + 24) != 1 || le64(p + 32) != LAST_LBA || first > last ||
	    last >= LAST_LBA || entries != 2 || entries >= first || !count ||
	    stride < 128 || (stride & (stride - 1u))) return 0;
	uint64_t bytes = (uint64_t)count * stride;
	uint64_t blocks = bytes / BLOCK_BYTES + (bytes % BLOCK_BYTES != 0);
	/* The declared array must fit the approved window, and the LBA geometry must allow it. */
	if (bytes != ARRAY_BYTES) return 0;
	if (blocks > first - entries || blocks > LAST_LBA - last - 1) return 0;
	printf("ufs-range-user: header pid=%d size=%u crc=0x%x self=1 alternate=%llu first=%llu last=%llu\n",
	    pid, size, actual, (unsigned long long)LAST_LBA, (unsigned long long)first,
	    (unsigned long long)last);
	printf("ufs-range-user: layout pid=%d entries_lba=%llu count=%u stride=%u blocks=%llu\n",
	    pid, (unsigned long long)entries, count, stride, (unsigned long long)blocks);
	*layout = (struct gpt_layout){first, last, count, stride, le32(p + 88)};
	return 1;
}

/* check_header bounds count to at most 128 records of at least 128 bytes. */
static void
audit_entries(const uint8_t *array, const struct gpt_layout *layout, int pid)
{
	static const uint8_t zero_guid[16];
	uint32_t used = 0, reversed = 0, before = 0, after = 0;
	uint32_t overlaps = 0, zero_ids = 0, duplicates = 0;
	for (uint32_t i = 0; i < layout->count; ++i) {
		const uint8_t *entry = array + (size_t)i * layout->stride;
		if (memcmp(entry, zero_guid, sizeof(zero_guid)) == 0) continue;
		++used;
		uint64_t lo = le64(entry + 32), hi = le64(entry + 40);
		int zero_id = memcmp(entry + 16, zero_guid, sizeof(zero_guid)) == 0;
		zero_ids += (uint32_t)zero_id;
		reversed += lo > hi;
		before += lo < layout->first_usable;
		after += hi > layout->last_usable;

		for (uint32_t j = 0; j < i; ++j) {
			const uint8_t *other = array + (size_t)j * layout->stride;
			if (memcmp(other, zero_guid, sizeof(zero_guid)) == 0) continue;
			uint64_t other_lo = le64(other + 32), other_hi = le64(other + 40);
			overlaps += lo <= hi && other_lo <= other_hi && lo <= other_hi && other_lo <= hi;
			duplicates += !zero_id && memcmp(entry + 16, other + 16, sizeof(zero_guid)) == 0;
		}
	}
	unsigned consistent = !(reversed || before || after || overlaps || zero_ids || duplicates);
	printf("ufs-range-user: entries pid=%d used=%u unused=%u reversed=%u before=%u after=%u "
	    "overlaps=%u zero_ids=%u duplicates=%u consistent=%u\n", pid, used, layout->count-used,
	    reversed, before, after, overlaps, zero_ids, duplicates, consistent);
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
	printf("ufs-range-user: trial=732 pid=%d write_error=%d\n", pid, error);
	if (writer >= 0) {
		close(writer);
		return 1;
	}
	if (pid <= 1 || (error != 13 && error != 30)) return 1;
	int fd = open(NODE, O_RDONLY, 0);
	if (fd < 0) return 1;

	static uint8_t data[ARRAY_BYTES + 2u];
	memset(data, 0xa5, sizeof(data));
	int bytes = (int)read(fd, data + 1, BLOCK_BYTES);
	int guards = data[0] == 0xa5 && data[1 + BLOCK_BYTES] == 0xa5;
	printf("ufs-range-user: block pid=%d bytes=%d errno=%d guards=%d\n",
	    pid, bytes, bytes < 0 ? errno : 0, guards);
	struct gpt_layout layout;
	int ok = bytes == (int)BLOCK_BYTES && guards &&
	    check_header(data + 1, pid, &layout);

	/* Read the whole array in one call and check both sentinels on either side of it. */
	if (ok) {
		memset(data, 0xa5, sizeof(data));
		bytes = (int)read(fd, data + 1, ARRAY_BYTES);
		guards = data[0] == 0xa5 && data[1 + ARRAY_BYTES] == 0xa5;
		uint32_t actual = bytes == (int)ARRAY_BYTES ? ~crc_update(~0u, data + 1, ARRAY_BYTES) : 0u;
		printf("ufs-range-user: array pid=%d bytes=%d crc=0x%x stored=0x%x guards=%d\n",
		    pid, bytes, actual, layout.array_crc, guards);
		ok = bytes == (int)ARRAY_BYTES && guards && actual == layout.array_crc;
		if (ok) audit_entries(data + 1, &layout, pid);
	}
	if (ok) {
		uint8_t sentinel = 0x5a;
		bytes = (int)read(fd, &sentinel, 1);
		printf("ufs-range-user: eof pid=%d bytes=%d unchanged=%d\n", pid, bytes,
		    sentinel == 0x5a);
		ok = bytes == 0 && sentinel == 0x5a;
	}
	int closed = close(fd);
	ok = ok && closed == 0;
	printf("ufs-range-user: final pid=%d passed=%d close=%d\n", pid, ok, closed);
	return ok ? 0 : 1;
}
