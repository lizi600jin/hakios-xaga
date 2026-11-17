#include "ufs_capacity_engine.h"
void capacity_engine_arm64(const struct ur_io *io,struct ur_workspace *workspace,struct uc_outcome *out)
{ uc_run(io,workspace,out,500000,1000000); }
