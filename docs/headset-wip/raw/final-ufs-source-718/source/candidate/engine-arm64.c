#include "ufs_read_engine.h"
void candidate_run(const struct ur_io *io, struct ur_workspace *workspace,
                   struct ur_outcome *out, uint32_t crc, uint32_t polls, uint64_t timeout)
{ ur_run(io, workspace, out, crc, polls, timeout); }
