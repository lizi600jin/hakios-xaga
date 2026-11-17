#include "ufs_atomic_lease.h"
bool candidate_claim(struct ur_atomic_lease *r) { return ur_atomic_claim(r); }
bool candidate_finish(struct ur_atomic_lease *r, bool clean) { return ur_atomic_finish(r, clean); }
void candidate_stop(struct ur_atomic_lease *r) { ur_atomic_stop(r); }
