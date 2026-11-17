/* Host-only tests for the bounded variable-LBA READ(10) codec, v2.
 * No IO, no hardware. Each scenario counts itself so coverage is reported honestly.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "ufs_variable_read_codec.h"

static unsigned checks, scenarios;
#define CHECK(x) do { checks++; if(!(x)){ fprintf(stderr,"line%d: %s\n",__LINE__,#x); exit(1);} } while(0)
#define SCENARIO(n) do { scenarios++; printf("  scenario %02u: %s\n", scenarios, n); } while(0)

/* 719's frozen constructor, transcribed only to prove LBA=1 byte-identity.
 * Sealed source: ufs-service-20261117/sealed-core-719-v1/ufs_read_contract.h:85-107.
 */
static void ref719_make_lba1(struct vr_command *c)
{
    memset(c, 0, sizeof(*c));
    c->upiu[0]=0x01; c->upiu[1]=0x40; c->upiu[2]=2;
    c->upiu[14]=0x10;
    c->upiu[16]=0x28;
    c->upiu[21]=1;
    c->upiu[24]=1;
    c->utrd[0]=0x14000000; c->utrd[2]=0x0f;
    c->utrd[4]=(uint32_t)VR_UCD_PA; c->utrd[5]=(uint32_t)(VR_UCD_PA>>32);
    c->utrd[6]=0x00800080; c->utrd[7]=0x01000001;
    c->prd[0]=(uint32_t)VR_DATA_PA; c->prd[1]=(uint32_t)(VR_DATA_PA>>32);
    c->prd[3]=4096-1;
}

static struct vr_request req(uint32_t lba)
{ struct vr_request r={VR_LUN,lba,VR_BLOCKS,VR_BLOCK_BYTES}; return r; }

int main(void)
{
    struct vr_limits rt = vr_runtime_limits();
    struct vr_limits cap = vr_capacity_limits();
    struct vr_command c, c2, keep;

    SCENARIO("LBA=1 byte-identical to sealed 719 command");
    { struct vr_request r=req(1); struct vr_command ref;
      CHECK(vr_make_read(&r,&cap,&c)); ref719_make_lba1(&ref);
      CHECK(memcmp(&c,&ref,sizeof(c))==0); }

    SCENARIO("distinct LBA changes the four big-endian CDB bytes");
    { uint32_t probe[]={0,1,2,15,16,255,256,65535,65536,16777215,16777216,31240191};
      for (unsigned i=0;i<sizeof(probe)/sizeof(probe[0]);i++) {
        struct vr_request r=req(probe[i]);
        if (!vr_request_valid(&r,&cap)) continue;
        CHECK(vr_make_read(&r,&cap,&c));
        CHECK(c.upiu[16]==0x28 && c.upiu[17]==0 && c.upiu[22]==0 && c.upiu[25]==0);
        CHECK(c.upiu[23]==0 && c.upiu[24]==VR_BLOCKS);
        CHECK(((uint32_t)c.upiu[18]<<24|(uint32_t)c.upiu[19]<<16|
               (uint32_t)c.upiu[20]<<8|c.upiu[21])==probe[i]);
        uint32_t got; CHECK(vr_command_lba(&c,&got) && got==probe[i]);
      } }

    SCENARIO("all four LBA bytes participate (0x01020304)");
    { struct vr_request r=req(0x01020304u); CHECK(vr_make_read(&r,&cap,&c));
      CHECK(c.upiu[18]==1 && c.upiu[19]==2 && c.upiu[20]==3 && c.upiu[21]==4); }

    SCENARIO("FIXED INVARIANTS cannot be relaxed by moving request and limits together");
    { struct vr_request bad;
      /* These four were accepted by v1 when both sides moved; they must now be refused. */
      bad=(struct vr_request){3u,0,VR_BLOCKS,VR_BLOCK_BYTES};
      memset(&keep,0x5a,sizeof(keep)); c=keep;
      CHECK(!vr_request_valid(&bad,&rt)); CHECK(!vr_make_read(&bad,&rt,&c));
      CHECK(memcmp(&c,&keep,sizeof(c))==0);
      bad=(struct vr_request){VR_LUN,0,2u,VR_BLOCK_BYTES};          /* two blocks */
      CHECK(!vr_request_valid(&bad,&rt)); CHECK(!vr_make_read(&bad,&rt,&c));
      CHECK(memcmp(&c,&keep,sizeof(c))==0);
      bad=(struct vr_request){VR_LUN,0,VR_BLOCKS,512u};             /* wrong block size */
      CHECK(!vr_request_valid(&bad,&rt)); CHECK(!vr_make_read(&bad,&rt,&c));
      CHECK(memcmp(&c,&keep,sizeof(c))==0);
      bad=(struct vr_request){VR_LUN,0,VR_BLOCKS,0u};               /* zero block size */
      CHECK(!vr_request_valid(&bad,&rt)); CHECK(!vr_make_read(&bad,&rt,&c));
      CHECK(memcmp(&c,&keep,sizeof(c))==0);
      bad=(struct vr_request){VR_LUN,0,0u,VR_BLOCK_BYTES};          /* zero blocks */
      CHECK(!vr_request_valid(&bad,&rt)); CHECK(!vr_make_read(&bad,&rt,&c));
      CHECK(memcmp(&c,&keep,sizeof(c))==0); }

    SCENARIO("limits cannot authorise an over-capacity LBA");
    { struct vr_limits huge={0xffffffffu}; struct vr_request r=req(0);
      CHECK(!vr_request_valid(&r,&huge));         /* rejected for exceeding capacity */
      struct vr_limits zero={0u};
      CHECK(!vr_request_valid(&r,&zero));         /* empty range */
      struct vr_limits atcap={VR_CAPACITY_BLOCKS};
      struct vr_request last=req(VR_CAPACITY_BLOCKS-1), over=req(VR_CAPACITY_BLOCKS);
      CHECK(vr_request_valid(&last,&atcap));
      CHECK(!vr_request_valid(&over,&atcap)); }

    SCENARIO("runtime policy refuses LBA>=16 and leaves output untouched");
    { uint32_t bad[]={16,17,1000,31240191,31240192,0xffffffffu};
      for (unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);i++) {
        struct vr_request r=req(bad[i]);
        memset(&keep,0x5a,sizeof(keep)); c=keep;
        CHECK(!vr_request_valid(&r,&rt)); CHECK(!vr_make_read(&r,&rt,&c));
        CHECK(memcmp(&c,&keep,sizeof(c))==0);
      } }

    SCENARIO("UINT32_MAX is refused by the range check, not by arithmetic");
    { /* Single block: there is no lba+(blocks-1) arithmetic to wrap, so the refusal is
       * purely the exclusive upper bound / capacity bound. Reported as such, not as proof
       * that a wrap branch executed.
       */
      struct vr_limits wide={0xffffffffu};
      struct vr_request r=req(0xffffffffu);
      CHECK(!vr_request_valid(&r,&wide));
      CHECK(wide.lba_max_exclusive > r.lba == false);   /* exclusive bound rejects equal */
      struct vr_limits edge={(uint32_t)VR_CAPACITY_BLOCKS};
      struct vr_request top=req((uint32_t)VR_CAPACITY_BLOCKS);
      CHECK(!vr_request_valid(&top,&edge)); }

    SCENARIO("null pointers refused");
    { struct vr_request r=req(3);
      CHECK(!vr_request_valid(NULL,&cap)); CHECK(!vr_request_valid(&r,NULL));
      CHECK(!vr_make_read(NULL,&cap,&c)); CHECK(!vr_make_read(&r,&cap,NULL));
      CHECK(!vr_make_read(&r,NULL,&c));
      CHECK(!vr_command_allowed(NULL,&r,&cap)); CHECK(!vr_command_allowed(&c,NULL,&cap));
      uint32_t v; CHECK(!vr_command_lba(NULL,&v)); CHECK(!vr_command_lba(&c,NULL)); }

    SCENARIO("readback bound to THIS request, not to any allowed LBA");
    { struct vr_request r5=req(5), r6=req(6), r15=req(15);
      CHECK(vr_make_read(&r5,&cap,&c));
      CHECK(vr_command_allowed(&c,&r5,&cap));
      CHECK(!vr_command_allowed(&c,&r6,&cap));
      CHECK(!vr_command_allowed(&c,&r15,&rt)); }

    SCENARIO("exhaustive 80-byte x 256 mutation: only the original passes");
    { struct vr_request r=req(9); CHECK(vr_make_read(&r,&cap,&c));
      for (size_t i=0;i<sizeof(c);i++) {
        uint8_t *b=(uint8_t*)&c; uint8_t saved=b[i];
        for (unsigned v=0;v<256;v++) { b[i]=(uint8_t)v;
          CHECK(vr_command_allowed(&c,&r,&cap)==(v==saved)); }
        b[i]=saved;
      } }

    SCENARIO("every bit flip in the four LBA bytes breaks the match");
    { struct vr_request r=req(0x00a5c3u); CHECK(vr_make_read(&r,&cap,&c));
      for (unsigned byte=18; byte<=21; byte++)
        for (unsigned bit=0; bit<8; bit++) {
          struct vr_command m=c; m.upiu[byte]^=(uint8_t)(1u<<bit);
          CHECK(!vr_command_allowed(&m,&r,&cap));
        } }

    SCENARIO("CDB flag byte: protection bit 0x80 and FUA bit 0x08 are separate refusals");
    { struct vr_request r=req(4); struct vr_command m;
      CHECK(vr_make_read(&r,&cap,&c));
      m=c; m.upiu[17]=0x80; CHECK(!vr_command_allowed(&m,&r,&cap));  /* RDPROTECT */
      m=c; m.upiu[17]=0x08; CHECK(!vr_command_allowed(&m,&r,&cap));  /* FUA */
      m=c; m.upiu[17]=0x10; CHECK(!vr_command_allowed(&m,&r,&cap));  /* DPO */
      m=c; m.upiu[17]=0x01; CHECK(!vr_command_allowed(&m,&r,&cap));  /* PMI */
      uint32_t got;
      m=c; m.upiu[17]=0x08; CHECK(!vr_command_lba(&m,&got));         /* flags rejected */
    }

    SCENARIO("opcode, transfer length, PRDT and LUN tampering refused");
    { struct vr_request r=req(4); struct vr_command m; CHECK(vr_make_read(&r,&cap,&c));
      m=c; m.upiu[16]=0x2a;  CHECK(!vr_command_allowed(&m,&r,&cap));
      m=c; m.upiu[16]=0xa8;  CHECK(!vr_command_allowed(&m,&r,&cap));
      m=c; m.upiu[24]=2;     CHECK(!vr_command_allowed(&m,&r,&cap));
      m=c; m.upiu[24]=0;     CHECK(!vr_command_allowed(&m,&r,&cap));
      m=c; m.upiu[23]=1;     CHECK(!vr_command_allowed(&m,&r,&cap));
      m=c; m.prd[3]=4095-1;  CHECK(!vr_command_allowed(&m,&r,&cap));
      m=c; m.upiu[2]=1;      CHECK(!vr_command_allowed(&m,&r,&cap));
      m=c; m.upiu[14]=0;     CHECK(!vr_command_allowed(&m,&r,&cap)); }

    SCENARIO("LBA=0 and LBA=1 differ in exactly one byte");
    { struct vr_request r0=req(0), r1=req(1);
      CHECK(vr_make_read(&r0,&cap,&c)); CHECK(vr_make_read(&r1,&cap,&c2));
      unsigned diffs=0; for (size_t i=0;i<sizeof(c);i++)
        if (((uint8_t*)&c)[i]!=((uint8_t*)&c2)[i]) diffs++;
      CHECK(diffs==1 && c.upiu[21]==0 && c2.upiu[21]==1); }

    printf("PASS %u checks over %u reported scenarios (codec only, no IO)\n", checks, scenarios);
    return 0;
}
