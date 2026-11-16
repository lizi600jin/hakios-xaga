/* Supplemental assertions for the public diagnostic field; preserve the original fixture. */
#define main ufs_original_model_main
#include "test-ufs.c"
#undef main

int main(void)
{
	assert(ufs_original_model_main() == 0);
	for (unsigned capacity = 1; capacity <= 32; capacity++) {
		struct ufs_snapshot first, second;
		uint32_t count = 0;
		char expected[40];
		healthy();
		snap[0][0] = snap[1][0] = capacity - 1;
		assert(run(&first, &second, &count) == UFS_FLAGS_OBSERVED);
		reset_log();
		ufs_report(&first, &second, UFS_FLAGS_OBSERVED, count);
		snprintf(expected, sizeof(expected), "sdb_slots=%u\n", capacity);
		assert(strstr(log_text, expected) != NULL);
		assert(strstr(log_text, "result=FLAGS_OBSERVED(0)") != NULL);
	}
	report("PASS 32 supplemental formatted-slot assertions; original fixture preserved\n");
	return 0;
}
