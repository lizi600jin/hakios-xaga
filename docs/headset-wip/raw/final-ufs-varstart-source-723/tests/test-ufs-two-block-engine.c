/* Host-only test of the two-block engine candidate against a synthetic controller model.
 * The model NEVER receives an expected LBA: it decodes the address from the command the
 * engine actually placed in memory. That is the point of this harness.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "ufs_two_block_engine.h"

static unsigned checks, scenarios, inject_hits;
#define CHECK(x) do { checks++; if(!(x)){ fprintf(stderr,"line%d: %s\n",__LINE__,#x); exit(1);} } while(0)
#define SCENARIO(n) do { scenarios++; printf("  scenario %02u: %s\n", scenarios, n); } while(0)
#define INJECT(n) do { inject_hits++; printf("      injected at: %s\n", n); } while(0)

/* ---- synthetic controller ------------------------------------------------------- */
#define REG_BYTES 0x2300u
#define UCD_WORDS (UR_UCD_BYTES/4)
#define DATA_WORDS VRQ_DATA_WORDS

struct model {
    uint32_t table[8], ucd[UCD_WORDS], data[DATA_WORDS];
    uint32_t regs[REG_BYTES/4];
    /* fault injection */
    int fail_at, call_ordinal, fail_kind;
    bool partial_response;
    unsigned partial_data_after, data_reads_seen, response_reads_seen;
    bool restore_fails, finish_fails, claim_fails;
    bool corrupt_second_prefill, corrupt_second_restore_readback;
    unsigned restore_data_writes;
    bool dbr_never_clears;
    /* STOP boundaries, each a real state the probe can observe */
    bool stop_at_admission, stop_at_ring, stop_at_drain;
    bool drained;
    /* FIX1 probe */
    bool mutate_data_after_first_sample;
    unsigned mutation_seen, data_samples_total;
    bool response_area_was_zero;
    uint32_t answered[DATA_WORDS];   /* the payload the model installed, kept for comparison */
    unsigned stop_callback_true;
    uint64_t clock_us, clock_step_us;
    /* observation: what the model decoded from the placed command */
    bool saw_command, rung;
    unsigned polls_seen;
    uint32_t decoded_lba, decoded_blocks, decoded_opcode;
};
static struct model M;

static void model_reset(void)
{
    memset(&M, 0, sizeof(M));
    M.regs[0] = 0x3587071fu; M.regs[8/4] = 0x200u;
    M.regs[0x30/4] = 0x10fu; M.regs[0x34/4] = 1u;
    M.regs[0x50/4] = (uint32_t)UR_LIST_PA; M.regs[0x54/4] = 0;
    M.regs[0x58/4] = 0; M.regs[0x60/4] = 1u;
    M.clock_step_us = 1;
    for (uint32_t i = 0; i < DATA_WORDS; i++) M.data[i] = 0x13428c00u ^ i;
    /* Inherited READ(10) baseline the sealed predicate requires to still hold. */
    M.table[0] = 0x14000000u; M.table[2] = 0u; M.table[4] = (uint32_t)UR_UCD_PA;
    M.table[5] = 0u; M.table[6] = 0x00800080u; M.table[7] = 0x01000001u;
    M.ucd[0] = 0x00024001u;                 /* UPIU 01 40 02 00 */
    M.ucd[3] = 0x00100000u;                 /* bytes 12..15 BE = 4096 */
    M.ucd[4] = 0x00000028u;                 /* c[16] = 0x28 READ(10) */
    M.ucd[5] = 0x00000000u;                 /* c[23] = 0 */
    M.ucd[6] = 0x00000001u;                 /* c[24] = 1 => blocks 1 */
}

/* The model builds its answer ONLY from the command bytes already in memory. */
static void model_complete_from_placed(void)
{
    uint8_t upiu[32];
    for (unsigned i=0;i<8;i++) vrq_put32le(upiu + i*4, M.ucd[i]);
    M.saw_command = true;
    {   uint32_t z=0;
        for (unsigned i=0;i<512/4;i++) z |= M.ucd[512/4+i] & 0xffffffffu;
        /* At ring time the response area must be all zero: caller's own header not yet written */
        M.response_area_was_zero = z == 0u;
    }
    M.decoded_opcode = upiu[16];
    M.decoded_lba = ((uint32_t)upiu[18]<<24)|((uint32_t)upiu[19]<<16)|
                    ((uint32_t)upiu[20]<<8)|(uint32_t)upiu[21];
    M.decoded_blocks = ((uint32_t)upiu[23]<<8)|(uint32_t)upiu[24];
    const uint32_t bytes = ur_be32(upiu + 12);
    CHECK(M.decoded_blocks == 2 && bytes == 8192);
    CHECK(M.ucd[1024/4] == (uint32_t)UR_DATA_PA && M.ucd[1024/4+3] == bytes - 1);
    CHECK((M.table[7] & 0xffffu) == 1u);
    /* Response header: transaction type, LUN2, response0, status0, EHS0, segment0. */
    uint8_t resp[52]; memset(resp,0,sizeof(resp));
    resp[0] = 0x21; resp[2] = 2;
    for (uint32_t i=0;i<13u;i++)
        M.ucd[512/4 + i] = (uint32_t)resp[i*4] | ((uint32_t)resp[i*4+1]<<8) |
                           ((uint32_t)resp[i*4+2]<<16) | ((uint32_t)resp[i*4+3]<<24);
    /* READ(10) moves 8192 bytes of payload: every byte is data and may legitimately change.
     * The model fills all of it from the address it decoded, never from a test argument.
     * Partial coverage is NOT modelled by writing fewer words; it is injected by making the
     * n-th sampling callback return false (see cb_read_word), so coverage is real.
     */
    for (uint32_t block = 0; block < M.decoded_blocks; block++) {
        uint32_t seed = (M.decoded_lba + block) * 2654435761u + 0x9e3779b9u;
        for (uint32_t word = 0; word < VRQ_BLOCK_WORDS; word++) {
            seed = seed * 1103515245u + 12345u;
            M.data[block * VRQ_BLOCK_WORDS + word] = seed;
        }
    }
    memcpy(M.answered, M.data, sizeof(M.answered));
    M.table[2] = 0;                 /* OCS 0: success */
    M.regs[0x58/4] = 0;             /* doorbell cleared */
}

/* ---- callbacks ------------------------------------------------------------------ */
static bool cb_read_reg(void *c, uint16_t off, uint32_t *out)
{
    (void)c;
    if (M.fail_kind=='r' && ++M.call_ordinal==M.fail_at) { INJECT("read_reg"); M.fail_kind=0; return false; }
    switch (off) {
    case 0: *out=M.regs[0]; break;      case 8: *out=M.regs[8/4]; break;
    case 0x30: *out=M.regs[0x30/4]; break; case 0x34: *out=M.regs[0x34/4]; break;
    case 0x50: *out=M.regs[0x50/4]; break; case 0x54: *out=M.regs[0x54/4]; break;
    case 0x58:
        M.polls_seen++;
        if (M.dbr_never_clears && M.rung) { M.clock_us += M.clock_step_us; *out=1; break; }
        if (!M.rung) { *out=M.regs[0x58/4]; break; }   /* not yet rung: report the real doorbell */
        if (M.rung && !M.saw_command) {
            model_complete_from_placed();
                    }
        *out=0; M.clock_us += M.clock_step_us; break;
    case 0x60: *out=M.regs[0x60/4]; break;
    default: return false;
    }
    return true;
}
static bool cb_read_word(void *c, int region, uint32_t off, uint32_t *out)
{
    (void)c;
    /* Only after the command has completed: before that, the engine is still reading back
     * the command it placed, and failing there is a different (earlier) failure mode.
     */
    if (region==VRQ_UCD && off>=512u && off<512u+52u && M.partial_response && M.saw_command) {
        if (M.response_reads_seen >= 2u) { INJECT("response sampling"); M.partial_response=false; return false; }
        M.response_reads_seen++;
    }
    if (M.fail_kind=='w' && ++M.call_ordinal==M.fail_at) { INJECT("read_word"); M.fail_kind=0; return false; }
    /* Partial data coverage: fail on the n-th DATA sampling read after completion. */
    /* Count only the POST-completion payload sampling. The prefill verification read the
     * same region earlier, and failing there is a different, earlier failure mode.
     */
    if (region==VRQ_DATA && M.partial_data_after && M.saw_command) {
        if (M.data_reads_seen >= M.partial_data_after) {
            INJECT("data sampling"); M.partial_data_after=0; return false;
        }
        M.data_reads_seen++;
    }
    uint32_t i = off/4;
    if (region == VRQ_DATA && off == VR_BLOCK_BYTES) {
        if (M.corrupt_second_prefill && !M.rung && M.data[i] == 0xa5a5a5a5u) {
            M.corrupt_second_prefill = false;
            INJECT("second-block prefill readback");
            *out = M.data[i] ^ 1u;
            return true;
        }
        if (M.corrupt_second_restore_readback && M.restore_data_writes == DATA_WORDS) {
            M.corrupt_second_restore_readback = false;
            INJECT("second-block restore readback");
            *out = M.data[i] ^ 1u;
            return true;
        }
    }
    /* Two independent counters: total DATA samples (for the FIX1 mutation probe) and
     * post-completion payload samples (for the partial-coverage probe).
     */
    if (region==VRQ_DATA) {
        if (++M.data_samples_total == DATA_WORDS) {
            /* End of the engine's first full sampling round. */
            if (M.mutate_data_after_first_sample) {
                M.mutate_data_after_first_sample=false; M.mutation_seen++;
                M.data[VRQ_BLOCK_WORDS] ^= 0xffffffffu;
            }
        }
    }
    if (region==VRQ_TABLE) { if (i>=8) return false; *out=M.table[i]; return true; }
    if (region==VRQ_UCD)   { if (i>=UCD_WORDS) return false; *out=M.ucd[i]; return true; }
    if (region==VRQ_DATA)  { if (i>=DATA_WORDS) return false; *out=M.data[i]; return true; }
    return false;
}
static bool cb_write_word(void *c, int region, uint32_t off, uint32_t v)
{
    (void)c;
    if (M.fail_kind=='W' && ++M.call_ordinal==M.fail_at) { INJECT("write_word"); M.fail_kind=0; return false; }
    /* A one-shot write failure at the second block, after the model completed the command. */
    if (M.restore_fails && M.saw_command && region==VRQ_DATA && off==VR_BLOCK_BYTES) {
        M.restore_fails=false; INJECT("second-block restore write"); return false;
    }
    uint32_t i = off/4;
    if (region==VRQ_TABLE) { if (i>=8) return false; M.table[i]=v; return true; }
    if (region==VRQ_UCD)   { if (i>=UCD_WORDS) return false; M.ucd[i]=v; return true; }
    if (region==VRQ_DATA) {
        if (i>=DATA_WORDS) return false;
        M.data[i] = v;
        if (M.saw_command) M.restore_data_writes++;
        return true;
    }
    return false;
}
static bool cb_claim(void *c)   { (void)c;  if (M.claim_fails) { INJECT("claim"); return false; } return true; }
static bool cb_finish(void *c, bool ok) { (void)c; if (M.finish_fails) { INJECT("finish"); return false; } return ok; }
/* STOP is a real state: it becomes true at the configured moment and stays true. */
static bool cb_stopped(void *c)
{
    (void)c;
    if (M.stop_at_admission && !M.rung) { M.stop_callback_true++; return true; }
    if (M.stop_at_ring && M.rung)       { M.stop_callback_true++; return true; }
    if (M.stop_at_drain && M.drained)   { M.stop_callback_true++; return true; }
    return false;
}
static bool cb_ring(void *c)
{
    (void)c;
    /* The posted store happens FIRST. A ring that reports failure after the doorbell was
     * written is ambiguous: the controller may have seen the command.
     */
    M.regs[0x58/4] = 1;
    M.rung = true;
    if (M.fail_kind=='g' && ++M.call_ordinal==M.fail_at) {
        INJECT("ring false AFTER posted store"); M.fail_kind=0; return false;
    }
    return true;
}
static void cb_barrier(void *c) { (void)c; }
static uint64_t cb_now(void *c) { (void)c; uint64_t t=M.clock_us; M.clock_us+=M.clock_step_us; return t; }

static struct vrq_io make_io(void)
{
    struct vrq_io io = { cb_read_reg, cb_read_word, cb_write_word, cb_claim, cb_finish,
                         cb_stopped, cb_ring, cb_barrier, cb_now, &M };
    return io;
}
static struct vr_request req(uint32_t lba)
{ struct vr_request r={VR_LUN,lba,VR2_BLOCKS,VR_BLOCK_BYTES}; return r; }

static struct vrq_workspace WS;
static struct vrq_outcome OUT;
static struct vrq_counters CTR;

/* Run one transaction; returns the outcome result. */
static int run_one(struct vrq_io *io, struct vr_request *r, struct vr_limits *lim)
{
    vrq_run(io, r, lim, &WS, &OUT, &CTR, false, 0u);
    return (int)OUT.result;
}

/* ---- real outer lease composition (not a stub that always returns true) ---- */
static struct us_lease LEASE;
static uint32_t PUBLISHED;
static bool publish(void *ctx, uint32_t state)
{ (void)ctx; PUBLISHED = state; return true; }
static bool cb_claim_real(void *c)
{
    (void)c;
    /* The real admission protocol: IDLE -> ACTIVE, only if the shared record is CLAIMED. */
    return us_admit(&LEASE);
}
static bool cb_finish_real(void *c, bool ok)
{
    (void)c;
    if (!ok) { us_poison(&LEASE); return false; }
    return us_mark_drained(&LEASE);
}
static bool cb_stopped_real(void *c) { (void)c; return us_stopped(&LEASE); }

int main(void)
{
    struct vr_limits rt = vr_runtime_limits();
    struct vrq_io io;

    /* ---------- refusal: zero IO, zero claim ---------- */
    SCENARIO("invalid request performs no IO and no claim");
    { model_reset(); io=make_io();
      struct vr_request bad={3u,0,2,4096};
      CHECK(run_one(&io,&bad,&rt)==VRQ_REFUSED);
      CHECK(CTR.read_reg==0 && CTR.read_word==0 && CTR.write_word==0);
      CHECK(CTR.claim==0 && CTR.ring==0 && CTR.finish==0);
      bad=(struct vr_request){VR_LUN,0,1u,4096};
      CHECK(run_one(&io,&bad,&rt)==VRQ_REFUSED && CTR.claim==0);
      bad=(struct vr_request){VR_LUN,0,2,512u};
      CHECK(run_one(&io,&bad,&rt)==VRQ_REFUSED && CTR.claim==0);
      bad=req(15);
      CHECK(run_one(&io,&bad,&rt)==VRQ_REFUSED && CTR.ring==0); }

    SCENARIO("null arguments refused with no callbacks");
    { model_reset(); io=make_io();
      vrq_run(&io,NULL,&rt,&WS,&OUT,&CTR,false,0);
      CHECK(OUT.result==VRQ_REFUSED && CTR.read_reg==0 && CTR.claim==0);
      struct vr_request r=req(0);
      vrq_run(&io,&r,NULL,&WS,&OUT,&CTR,false,0);
      CHECK(OUT.result==VRQ_REFUSED); }

    /* ---------- FIX 1: a data change between the two samples is caught ---------- */
    SCENARIO("FIX1 a second-block change after the first sample is refused before claim");
    { model_reset(); io=make_io();
      M.mutate_data_after_first_sample=true;
      struct vr_request r=req(3);
      CHECK(run_one(&io,&r,&rt)==VRQ_REFUSED);
      CHECK(CTR.claim==0 && CTR.ring==0);      /* never reached claim */
      CHECK(M.mutation_seen==1);               /* the mutation really happened */ }

    /* ---------- FIX 2: stale response bytes are cleared ---------- */
    SCENARIO("FIX2 a stale response descriptor never survives prepare");
    { model_reset(); io=make_io();
      M.ucd[512/4] = 0xdeadbeefu;              /* stale header planted */
      struct vr_request r=req(1);
      CHECK(run_one(&io,&r,&rt)==VRQ_DELIVERED);
      CHECK(M.saw_command);                     /* the ring happened */
      /* the model asserts it saw a zeroed response area before answering */
      CHECK(M.response_area_was_zero==true); }

    /* ---------- the address comes from the placed CDB ---------- */
    SCENARIO("model decodes LBA from the CDB; a different LBA yields different payload");
    { uint32_t seen[4], crc[4];
      for (unsigned k=0;k<4;k++) {
        model_reset(); io=make_io();
        struct vr_request r=req(k*3u);
        CHECK(run_one(&io,&r,&rt)==VRQ_DELIVERED);
        CHECK(M.decoded_lba==k*3u && M.decoded_blocks==2u && M.decoded_opcode==0x28u);
        CHECK(CTR.ring==1u);
        seen[k]=M.decoded_lba; crc[k]=OUT.data_crc32;
      }
      for (unsigned a=0;a<4;a++) for (unsigned b=a+1;b<4;b++)
        { CHECK(seen[a]!=seen[b]); CHECK(crc[a]!=crc[b]); } }

    SCENARIO("readback bound to THIS request; another allowed LBA is refused");
    { model_reset(); io=make_io();
      struct vr_request r=req(5), other=req(6);
      CHECK(run_one(&io,&r,&rt)==VRQ_DELIVERED && M.decoded_lba==5u);
      struct vr_command planted; struct vr_limits l=rt;
      CHECK(vr2_make_read(&other,&l,&planted));
      for (unsigned i=0;i<8;i++) M.table[i]=planted.utrd[i];
      for (unsigned i=0;i<8;i++) M.ucd[i]=vrq_get32le(planted.upiu+i*4);
      for (unsigned i=0;i<4;i++) M.ucd[1024/4+i]=planted.prd[i];
      CHECK(!vrq_readback(&CTR,&io,&r,&l));
      CHECK(vrq_readback(&CTR,&io,&other,&l)); }

    /* ---------- whole 8 KiB is payload, byte-for-byte ---------- */
    SCENARIO("all 8192 bytes are payload and match the model byte-for-byte");
    { model_reset(); io=make_io();
      struct vr_request r=req(4);
      CHECK(run_one(&io,&r,&rt)==VRQ_DELIVERED);
      CHECK(OUT.data_complete==true);
      CHECK(OUT.data_words_valid==VRQ_DATA_WORDS);          /* exact coverage, not a mask */
      /* Compare against what the model PLACED, captured when it answered: after delivery the
       * engine has already restored the scratch area, so M.data is back to the snapshot.
       */
      CHECK(memcmp(OUT.data,(const void*)M.answered,VR2_TRANSFER_BYTES)==0);
      uint32_t changed=0;
      for (unsigned i=0;i<VR2_TRANSFER_BYTES;i++) if (OUT.data[i]!=0xa5) changed++;
      CHECK(changed>8000u); }

    SCENARIO("restore is verified byte-for-byte against the snapshot");
    { model_reset(); io=make_io();
      uint32_t before[DATA_WORDS]; memcpy(before,M.data,sizeof(before));
      struct vr_request r=req(2);
      CHECK(run_one(&io,&r,&rt)==VRQ_DELIVERED);
      CHECK(OUT.restored==true && OUT.readback_restored==true);
      CHECK(memcmp(M.data,before,sizeof(before))==0);       /* memcmp, not just a flag */ }

    SCENARIO("second-block prefill mismatch prevents ringing");
    { model_reset(); io=make_io();
      uint32_t before[DATA_WORDS]; memcpy(before,M.data,sizeof(before));
      M.corrupt_second_prefill=true;
      struct vr_request r=req(1);
      CHECK(run_one(&io,&r,&rt)==VRQ_READBACK_FAILED);
      CHECK(!M.corrupt_second_prefill && CTR.ring==0u && OUT.restored);
      CHECK(memcmp(M.data,before,sizeof(before))==0); }

    /* ---------- partial coverage at specific callback positions ---------- */
    SCENARIO("partial response: the 3rd response word returns false");
    { model_reset(); io=make_io();
      M.partial_response=true; M.response_reads_seen=0;
      struct vr_request r=req(1);
      CHECK(run_one(&io,&r,&rt)==VRQ_PARTIAL);
      CHECK(OUT.response_complete==false && M.response_reads_seen==2u);
      CHECK(inject_hits>0); }

    SCENARIO("partial data: a second-block payload read returns false");
    { model_reset(); io=make_io();
      M.partial_data_after=(VRQ_BLOCK_WORDS + 20u); M.data_reads_seen=0;
      struct vr_request r=req(2);
      CHECK(run_one(&io,&r,&rt)==VRQ_PARTIAL);
      CHECK(OUT.data_complete==false);
      CHECK(OUT.data_words_valid==(VRQ_BLOCK_WORDS + 20u));
      CHECK(M.data_reads_seen==(VRQ_BLOCK_WORDS + 20u) && inject_hits>0); }

    SCENARIO("FIX5 a partial result never gets a full-block reference label");
    { model_reset(); io=make_io();
      M.partial_data_after=(VRQ_BLOCK_WORDS + 20u); M.data_reads_seen=0;
      struct vr_request r=req(2);
      vrq_run(&io,&r,&rt,&WS,&OUT,&CTR,true,OUT.data_crc32);
      CHECK(OUT.result==VRQ_PARTIAL);
      CHECK(OUT.data_match==VRQ_DATA_UNKNOWN);  /* not SAME_CHECKSUM */ }

    /* ---------- timing / STOP boundaries ---------- */
    SCENARIO("doorbell never clears: poll bound, one ring, uncertain");
    { model_reset(); M.dbr_never_clears=true; io=make_io();
      struct vr_request r=req(1);
      CHECK(run_one(&io,&r,&rt)==VRQ_UNCERTAIN);
      CHECK(CTR.ring==1u && OUT.polls==VRQ_MAX_POLLS); }

    SCENARIO("FIX4 STOP at ACTIVE is observed: the callback really returns true");
    { model_reset(); io=make_io();
      M.stop_at_ring=true;
      struct vr_request r=req(1);
      CHECK(run_one(&io,&r,&rt)==VRQ_UNCERTAIN);
      CHECK(M.polls_seen>0);                    /* polling happened */
      CHECK(M.stop_callback_true>0);            /* stopped() actually said true */
      CHECK(CTR.ring==1u); }

    SCENARIO("STOP at admission prevents any ring");
    { model_reset(); io=make_io(); M.stop_at_admission=true;
      struct vr_request r=req(1);
      CHECK(run_one(&io,&r,&rt)==VRQ_REFUSED);
      CHECK(CTR.ring==0u); }

    SCENARIO("false ring AFTER the posted store is uncertain, not retried");
    { model_reset(); io=make_io();
      M.fail_kind='g'; M.fail_at=1; M.call_ordinal=0;
      unsigned before=inject_hits;
      struct vr_request r=req(1);
      /* The doorbell was already posted when ring returned false, so this is uncertainty,
       * not a clean prepare failure: the engine must not treat it as "nothing was issued".
       */
      CHECK(run_one(&io,&r,&rt)==VRQ_UNCERTAIN);
      CHECK(M.rung==true);                     /* the posted store really happened */
      CHECK(OUT.issued==true && OUT.restored==false);
      CHECK(inject_hits>before);
      CHECK(CTR.ring==1u);                      /* exactly one attempt */
      CHECK(inject_hits>0); }

    /* ---------- every callback fails at a verified reachable position ---------- */
    SCENARIO("each callback failure closes without delivery, injection verified");
    { struct { char kind; int at; bool claim, fin; } cases[] = {
        {'r',40,false,false}, {'w',600,false,false}, {'W',300,false,false},
        {'c',0,true,false}, {'f',0,false,true} };
      for (unsigned k=0;k<5;k++) {
        model_reset(); io=make_io();
        unsigned before=inject_hits;
        M.fail_kind=cases[k].kind; M.fail_at=cases[k].at;
        M.claim_fails=cases[k].claim; M.finish_fails=cases[k].fin;
        struct vr_request r=req(1);
        CHECK(run_one(&io,&r,&rt)!=VRQ_DELIVERED);
        CHECK(inject_hits>before);
      } }

    SCENARIO("restore write failure poisons, and only during restore");
    { model_reset(); io=make_io();
      unsigned before=inject_hits;
      M.restore_fails=true;
      struct vr_request r=req(1);
      int t=run_one(&io,&r,&rt);
      CHECK(t==VRQ_UNCERTAIN);                  /* poisoned, not delivered */
      CHECK(M.saw_command && CTR.ring==1u && inject_hits==before+1);
      CHECK(M.restore_fails==false); }

    SCENARIO("second-block restore readback mismatch poisons the real lease");
    { model_reset(); io=make_io();
      memset(&LEASE,0,sizeof(LEASE));
      LEASE.publish=publish; LEASE.context=&PUBLISHED; PUBLISHED=0;
      io.claim=cb_claim_real; io.finish=cb_finish_real; io.stopped=cb_stopped_real;
      CHECK(us_open(&LEASE,true));
      M.corrupt_second_restore_readback=true;
      struct vr_request r=req(1);
      CHECK(run_one(&io,&r,&rt)==VRQ_UNCERTAIN);
      CHECK(!M.corrupt_second_restore_readback && M.restore_data_writes==DATA_WORDS);
      CHECK(!OUT.restored && CTR.ring==1u && PUBLISHED==UR_POISONED); }

    /* ---------- CRC is only a checksum ---------- */
    SCENARIO("CRC agreement, disagreement and absence are three distinct reports");
    { model_reset(); io=make_io();
      struct vr_request r=req(7);
      vrq_run(&io,&r,&rt,&WS,&OUT,&CTR,false,0);
      CHECK(OUT.result==VRQ_DELIVERED);
      CHECK(OUT.data_match==VRQ_DATA_UNKNOWN);  /* no reference supplied */
      uint32_t real=OUT.data_crc32;
      model_reset(); io=make_io();
      vrq_run(&io,&r,&rt,&WS,&OUT,&CTR,true,real);
      CHECK(OUT.data_match==VRQ_DATA_SAME_CHECKSUM);
      model_reset(); io=make_io();
      vrq_run(&io,&r,&rt,&WS,&OUT,&CTR,true,real^1u);
      CHECK(OUT.data_match==VRQ_DATA_DIFFERENT); }

    /* ---------- real outer lease composition ---------- */
    SCENARIO("REAL us_open/us_admit/us_mark_drained/us_copy_done/us_stop: CLAIMED held to quiesce");
    { model_reset();
      memset(&LEASE,0,sizeof(LEASE));
      LEASE.publish=publish; LEASE.context=&PUBLISHED; PUBLISHED=0;
      io=make_io();
      io.claim=cb_claim_real; io.finish=cb_finish_real; io.stopped=cb_stopped_real;
      /* open publishes CLAIMED exactly once, then the engine admits IDLE -> ACTIVE */
      CHECK(us_open(&LEASE,true));
      CHECK(PUBLISHED==UR_CLAIMED);
      struct vr_request r=req(3);
      CHECK(run_one(&io,&r,&rt)==VRQ_DELIVERED);
      /* after delivery the outer lease is DRAINED, and the shared record is still CLAIMED */
      CHECK((us_control(&LEASE)&US_PHASE_MASK)==US_DRAINED);
      CHECK(PUBLISHED==UR_CLAIMED);
      CHECK(us_copy_done(&LEASE));
      CHECK((us_control(&LEASE)&US_PHASE_MASK)==US_IDLE);
      CHECK(PUBLISHED==UR_CLAIMED);             /* still held at idle: not released yet */
      /* only quiesce releases */
      us_stop(&LEASE);
      CHECK(PUBLISHED==UR_RESTORED);
      CHECK(us_control(&LEASE)&US_RELEASED_BIT); }

    SCENARIO("REAL lease: an ACTIVE STOP poisons and never publishes RESTORED");
    { model_reset();
      memset(&LEASE,0,sizeof(LEASE));
      LEASE.publish=publish; LEASE.context=&PUBLISHED; PUBLISHED=0;
      io=make_io();
      io.claim=cb_claim_real; io.finish=cb_finish_real;
      CHECK(us_open(&LEASE,true));
      CHECK(us_admit(&LEASE));                 /* ACTIVE */
      us_stop(&LEASE);                         /* STOP while ACTIVE */
      CHECK(us_control(&LEASE)&US_POISON_BIT);
      CHECK(PUBLISHED==UR_POISONED); }

    printf("PASS %u checks over %u scenarios, %u injection points fired\n",
           checks, scenarios, inject_hits);
    return 0;
}
