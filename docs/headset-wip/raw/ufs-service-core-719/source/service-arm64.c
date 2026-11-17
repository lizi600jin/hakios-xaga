#include "ufs_service_engine.h"
enum us_read_result
service_codec(struct us_lease *s, const struct ur_io *io, struct ur_workspace *w,
              struct ur_outcome *o, const struct ur_request *r, us_deliver_fn f, void *d)
{ return us_read_fixed(s,io,w,o,r,f,d,UR_BLOCK_BYTES,0x28a2836cu,500000,1000000); }
void service_stop_codec(struct us_lease *s) { us_stop(s); }
