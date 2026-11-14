/* One deadline and poll budget for an entire SWINF transaction, including recovery. */
#ifndef _SYS_HAKIOS_PMIF_H_
#define _SYS_HAKIOS_PMIF_H_

#include <stdbool.h>
#include <stdint.h>
#include <kern/clock.h>

#define HAKIOS_PMIF_TIMEOUT_NS  2000000ull
#define HAKIOS_PMIF_POLL_LIMIT  20000u /* also terminates if the counter stops */

struct hakios_pmif_budget {
	uint64_t deadline;
	uint32_t polls_left;
};

static inline struct hakios_pmif_budget
hakios_pmif_budget_begin(void)
{
	uint64_t ticks;

	nanoseconds_to_absolutetime(HAKIOS_PMIF_TIMEOUT_NS, &ticks);
	return (struct hakios_pmif_budget){ mach_absolute_time() + ticks, HAKIOS_PMIF_POLL_LIMIT };
}

static inline bool
hakios_pmif_budget_poll(struct hakios_pmif_budget *budget)
{
	if (budget->polls_left == 0 || mach_absolute_time() >= budget->deadline) {
		return false;
	}
	budget->polls_left--;
	return true;
}

#endif
