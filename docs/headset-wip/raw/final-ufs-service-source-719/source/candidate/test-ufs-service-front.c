#include <assert.h>
#include <stdio.h>
#include "ufs_service_frontend.h"
int main(void)
{
    unsigned checks=0;
    const int64_t lengths[]={0,1,17,4095,4096,4097,INT64_MAX,-1,INT64_MIN};
    for(int64_t off=-1;off<=4097;off++)for(unsigned j=0;j<sizeof(lengths)/sizeof(lengths[0]);j++) {
        struct us_read_plan plan={0x66666666u,0x66666666u};
        int64_t count=lengths[j];enum us_plan_result action=us_plan_read(off,count,&plan);
        if(off<0||off>4096||count<0) {
            assert(action==US_PLAN_INVALID&&plan.offset==0x66666666u&&plan.bytes==0x66666666u);
        }else if(count==0||off==4096) {
            assert(action==US_PLAN_EOF&&plan.offset==0x66666666u&&plan.bytes==0x66666666u);
        }else {
            assert(action==US_PLAN_READ&&plan.offset==(uint32_t)off&&plan.bytes>0);
            assert((uint64_t)plan.offset+plan.bytes<=4096 && plan.bytes<=(uint64_t)count);
            assert(plan.bytes==(count<4096-off?(uint32_t)count:(uint32_t)(4096-off)));
        }
        checks++;
    }
    struct us_read_plan plan;
    assert(us_plan_read(INT64_MIN,1,&plan)==US_PLAN_INVALID);checks++;
    assert(us_plan_read(INT64_MAX,1,&plan)==US_PLAN_INVALID);checks++;
    assert(us_plan_read(0,1,NULL)==US_PLAN_INVALID);checks++;
    printf("PASS %u bounded snapshot-range checks; no hardware\n",checks);
    return 0;
}
