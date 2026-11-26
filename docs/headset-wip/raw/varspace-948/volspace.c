/*
 * volspace: the two ways a program can ask how much room a volume has, side by side.
 *
 * The Settings app shows "Available: Zero KB" for /private/var while statfs() reports 63.40 GiB
 * free and the system writes to that volume without complaint. So one of two things is true:
 * the display path asks a different question than statfs(), or it asks statfs() and mangles the
 * answer.
 *
 * This tool asks both questions and prints both answers in the units the kernel uses, so the two
 * can be compared without any arithmetic in between.
 *
 *   statfs()      f_bavail blocks of f_bsize bytes
 *   getattrlist() ATTR_VOL_SPACEFREE, ATTR_VOL_SPACEUSED, ATTR_VOL_FSTYPE
 *
 * usage: volspace PATH
 */
#include "mini.h"

#define ATTR_VOL_FSTYPE      0x00000001
#define ATTR_VOL_SPACEFREE   0x00000008
#define ATTR_VOL_SPACEUSED   0x00800000
#define ATTR_BIT_MAP_COUNT   5

struct attrlist {
	unsigned short bitmapcount;
	unsigned short reserved;
	unsigned int commonattr;
	unsigned int volattr;
	unsigned int dirattr;
	unsigned int fileattr;
	unsigned int forkattr;
};

int
main(int argc, char **argv, char **envp)
{
	struct statfs fs;
	struct attrlist alist;
	unsigned char buf[512];
	const char *path;
	int rc;
	unsigned int *w;
	unsigned int fstype = 0;
	unsigned long long spacefree = 0;
	unsigned long long spaceused = 0;
	unsigned int seen = 0;

	(void)envp;
	path = (argc >= 2) ? argv[1] : "/private/var";

	/* --- the first question --- */
	rc = statfs(path, &fs);
	printf("volspace: statfs(\"%s\") = %d\n", path, rc);
	if (rc == 0) {
		printf("volspace:   f_bsize %ld f_blocks %ld f_bavail %ld\n",
		    (long)fs.f_bsize, (long)fs.f_blocks, (long)fs.f_bavail);
		printf("volspace:   free bytes %lld\n",
		    (long long)fs.f_bavail * (long long)fs.f_bsize);
	}

	/* --- the second question --- */
	memset(&alist, 0, sizeof(alist));
	alist.bitmapcount = ATTR_BIT_MAP_COUNT;
	alist.volattr = ATTR_VOL_FSTYPE | ATTR_VOL_SPACEFREE | ATTR_VOL_SPACEUSED;

	memset(buf, 0, sizeof(buf));
	rc = getattrlist(path, &alist, buf, sizeof(buf), 0);
	printf("volspace: getattrlist(\"%s\") = %d\n", path, rc);
	if (rc != 0) {
		printf("volspace:   errno %d (%s)\n", errno, strerror(errno));
		return 0;
	}

	/*
	 * Dump the buffer as raw words.
	 *
	 * The first version of this tried to walk the packed fields and match them to the attribute
	 * names by their length, which assumed every value is 32 bits. They are not: ATTR_VOL_SPACEFREE
	 * and ATTR_VOL_SPACEUSED are off_t, so their length prefix is 8, and a walker that only
	 * understands 4 reads them as noise. Printing the words takes no position on the layout --
	 * the numbers are what they are, and the offsets in this output say where they sit.
	 */
	{
		unsigned int words;
		unsigned int i;

		w = (unsigned int *)(void *)buf;
		words = w[0] / 4u;

		if (words > sizeof(buf) / 4u) {
			words = sizeof(buf) / 4u;
		}
		printf("volspace:   buffer length %u (%u words)\n", w[0], words);
		for (i = 0; i < words; i++) {
			printf("volspace:   +%02u: %08x  %u\n", i * 4u, w[i], w[i]);
		}
		/*
		 * The two 64-bit values, read as the pairs of words they are, at the offsets the dump
		 * above will confirm: length(4) + fstype(4+4) leaves the free space at +12 low and
		 * +16 high, and the used space after it.
		 */
		printf("volspace:   as u64 at +12: %llu\n",
		    (unsigned long long)((unsigned long long)w[4] |
		    ((unsigned long long)w[5] << 32)));
		printf("volspace:   as u64 at +20: %llu\n",
		    (unsigned long long)((unsigned long long)w[6] |
		    ((unsigned long long)w[7] << 32)));
	}

	if (seen & 1u) {
		printf("volspace:   ATTR_VOL_FSTYPE     %u\n", fstype);
	}
	if (seen & 2u) {
		printf("volspace:   ATTR_VOL_SPACEFREE  %llu\n", spacefree);
	}
	if (seen & 4u) {
		printf("volspace:   ATTR_VOL_SPACEUSED  %llu\n", spaceused);
	}
	return 0;
}
