#ifndef MT6895_UFS_SERVICE_FRONTEND_H
#define MT6895_UFS_SERVICE_FRONTEND_H
#include "ufs_read_contract.h"
enum us_plan_result { US_PLAN_READ, US_PLAN_EOF, US_PLAN_INVALID };
struct us_read_plan { uint32_t offset, bytes; };
static inline enum us_plan_result
us_plan_read(int64_t offset, int64_t residual, struct us_read_plan *plan)
{
    if (!plan || offset < 0 || offset > UR_BLOCK_BYTES || residual < 0) return US_PLAN_INVALID;
    if (!residual || offset == UR_BLOCK_BYTES) return US_PLAN_EOF;
    plan->offset = (uint32_t)offset;
    uint32_t left = UR_BLOCK_BYTES - plan->offset;
    plan->bytes = (uint64_t)residual < left ? (uint32_t)residual : left;
    return US_PLAN_READ;
}
#endif
