#include "ufs_read_contract.h"
bool candidate_read(const struct ur_request *r, struct ur_command *c) { return ur_make_read(r, c); }
bool candidate_filter(const struct ur_command *c) { return ur_command_allowed(c); }
bool candidate_gate(const struct ur_lease *r) { return ur_loader_may_use_storage(r); }
bool candidate_transition(struct ur_lease *r, enum ur_event e) { return ur_lease_step(r, e); }
