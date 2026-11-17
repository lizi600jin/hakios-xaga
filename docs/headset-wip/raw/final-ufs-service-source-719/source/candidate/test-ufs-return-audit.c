#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "ufs_return_audit.h"
static unsigned checks;
#define CHECK(x) do { assert(x);checks++; } while(0)
struct change { struct ur_lease *r; unsigned field,bit; bool enabled; };
static void barrier(void *ctx)
{
    struct change *c=ctx;
    if(c&&c->enabled) {
        uint32_t mask=1u<<c->bit;
        switch(c->field) {
        case 0:c->r->magic^=mask;break;case 1:c->r->version^=mask;break;
        case 2:c->r->trial^=mask;break;case 3:c->r->state^=mask;break;
        default:assert(!"invalid model field");
        }
    }
}
int main(void)
{
    struct ur_lease r={UR_MAGIC,UR_VERSION,UR_TRIAL,UR_RESTORED},observed;
    struct change c={&r,0,0,false};
    char text[256],reference[256];
    CHECK(ur_guard_observe_allowed(&r,barrier,&c,&observed));
    CHECK(!memcmp(&r,&observed,sizeof(r)));
    size_t n=ur_guard_format_return(&observed,text,sizeof(text));
    snprintf(reference,sizeof(reference),"\nmt6895-ufs-service: return-gate magic=0x554c5345 version=1 trial=%u state=2 allowed=1\n",UR_TRIAL);
    CHECK(n==strlen(reference)&&!strcmp(text,reference));
    for(size_t cap=0;cap<=sizeof(text);cap++) {
        memset(text,0x66,sizeof(text));
        size_t got=ur_guard_format_return(&r,text,cap);
        CHECK((got==n)==(cap>n));
        if(got)CHECK(!memcmp(text,reference,n+1));
        else for(size_t i=0;i<sizeof(text);i++)CHECK(text[i]==0x66);
    }
    for(unsigned field=0;field<4;field++)for(unsigned bit=0;bit<32;bit++) {
        r=(struct ur_lease){UR_MAGIC,UR_VERSION,UR_TRIAL,UR_RESTORED};
        observed=(struct ur_lease){0x66,0x66,0x66,0x66};
        c=(struct change){&r,field,bit,true};
        CHECK(!ur_guard_observe_allowed(&r,barrier,&c,&observed));
        CHECK(observed.magic==0x66&&observed.state==0x66);
    }
    c.enabled=false;
    for(unsigned state=0;state<=4;state++) {
        r=(struct ur_lease){UR_MAGIC,UR_VERSION,UR_TRIAL,state};
        CHECK(ur_guard_observe_allowed(&r,barrier,&c,&observed)==(state==0||state==2));
        CHECK((ur_guard_format_return(&r,text,sizeof(text))!=0)==(state==0||state==2));
    }
    CHECK(!ur_guard_observe_allowed(NULL,barrier,&c,&observed));
    CHECK(!ur_guard_observe_allowed(&r,NULL,&c,&observed));
    CHECK(!ur_guard_format_return(&r,NULL,100));
    printf("PASS %u return snapshot/format/bounds checks; host only\n",checks);
    return 0;
}
