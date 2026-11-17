/* Host test of the REAL ur_guard_format_return from the private loader source.
 * The header is included unmodified; no string is hand-written here.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "ufs_return_audit.h"

static unsigned checks;
#define CHECK(x) do { checks++; if(!(x)){ printf("FAIL line %d: %s\n",__LINE__,#x); return 1; } } while(0)

int main(void)
{
    struct ur_lease r; char out[256]; size_t n;

    memset(&r, 0, sizeof(r));
    r.magic = UR_MAGIC; r.version = UR_VERSION; r.trial = UR_TRIAL; r.state = UR_RESTORED;
    n = ur_guard_format_return(&r, out, sizeof(out));
    CHECK(n > 0);
    printf("  real output (%zu B):%s", n, out);

    /* the prefix must be varread, exactly once, and capacity must be absent */
    CHECK(strstr(out, "mt6895-ufs-varread: return-gate magic=0x") != NULL);
    CHECK(strstr(out, "mt6895-ufs-capacity") == NULL);
    CHECK(strstr(out, "trial=721") != NULL);
    CHECK(strstr(out, "state=2") != NULL);
    CHECK(strstr(out, "allowed=1") != NULL);
    CHECK(out[0] == '\n');

    /* a not-allowed lease must format to nothing */
    r.state = 3;   /* POISONED */
    CHECK(ur_guard_format_return(&r, out, sizeof(out)) == 0);
    r.state = UR_RESTORED;

    /* a tiny buffer must be refused, not truncated silently */
    CHECK(ur_guard_format_return(&r, out, 8) == 0);

    printf("PASS %u checks in the real guard formatter\n", checks);
    return 0;
}
