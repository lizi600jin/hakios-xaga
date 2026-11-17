#include "../support/ufs_service_model.h"
#include "ufs_capacity_engine.h"
enum capacity_case { CAP_REFERENCE,CAP_DIFFERENT,CAP_SATURATED,CAP_ZERO_BLOCK,
                     CAP_TAIL_OVERRUN,CAP_NO_DATA,CAP_PARTIAL_DATA };
static enum capacity_case cap_case;
static unsigned tail_word=2;
static void put_be(uint8_t *p,uint32_t word)
{ for (unsigned i=0;i<4;i++) p[i]=(uint8_t)(word>>(24-i*8)); }
static bool cap_ring(void *ctx)
{
    struct model *m=ctx;
    struct uc_command command;
    memcpy(command.utrd,m->table,32);
    for (unsigned i=0;i<8;i++) ur_bytes(m->ucd[i],command.upiu+i*4);
    memcpy(command.prd,m->ucd+256,16);
    CHECK(uc_command_allowed(&command));
    CHECK(m->lease.state==UR_CLAIMED && !m->stop && m->rings==0);
    m->rings++;m->dbr=1;
    if (m->behavior==RING_AMBIGUOUS) return false;
    if (m->behavior==NO_COMPLETION) return true;
    if (m->behavior==STOP_AFTER_RING) { m->stop=true;ur_lease_step(&m->lease,UR_STOP);return true; }
    reply(m);
    for (unsigned i=0;i<UR_BLOCK_BYTES/4;i++) m->data[i]=0xa5a5a5a5u;
    uint8_t data[8];put_be(data,31240191);put_be(data+4,4096);
    if (cap_case==CAP_DIFFERENT) { put_be(data,12345);put_be(data+4,512); }
    if (cap_case==CAP_SATURATED) put_be(data,UINT32_MAX);
    if (cap_case==CAP_ZERO_BLOCK) put_be(data+4,0);
    if (cap_case!=CAP_NO_DATA) m->data[0]=ur_le32(data);
    if (cap_case!=CAP_NO_DATA && cap_case!=CAP_PARTIAL_DATA) m->data[1]=ur_le32(data+4);
    if (cap_case==CAP_TAIL_OVERRUN) m->data[tail_word]^=1;
    return true;
}
static bool cap_reg(void *c,uint16_t o,uint32_t *v)
{ CHECK(((struct model *)c)->finishes==0);return read_reg(c,o,v); }
static bool cap_read(void *c,enum ur_memory r,uint32_t o,uint32_t *v)
{ CHECK(((struct model *)c)->finishes==0);return read_word(c,r,o,v); }
static bool cap_write(void *c,enum ur_memory r,uint32_t o,uint32_t v)
{ CHECK(((struct model *)c)->finishes==0);return write_word(c,r,o,v); }
static void cap_barrier(void *c)
{ CHECK(((struct model *)c)->finishes==0);barrier(c); }
static uint64_t cap_time(void *c)
{ CHECK(((struct model *)c)->finishes==0);return now_us(c); }
static struct ur_io cap_io(struct model *m)
{
    struct ur_io io=adapter(m);
    io.read_reg=cap_reg;io.read_word=cap_read;io.write_word=cap_write;io.ring_once=cap_ring;
    io.barrier=cap_barrier;io.now_us=cap_time;return io;
}
static void invariant(struct model *m,struct uc_outcome *out)
{
    CHECK(m->rings<=1 && m->write_before_claim==0);
    CHECK(!out->issued || out->claimed);
    if (out->restored) CHECK(out->claimed && memory_restored(m) && m->lease.state==UR_RESTORED);
    else if (out->claimed) CHECK(m->lease.state==UR_POISONED && !ur_loader_may_use_storage(&m->lease));
    else CHECK(m->mem_writes==0 && m->rings==0 && m->lease.state==UR_UNUSED);
    if (out->query_valid) {
        CHECK(out->result==UC_QUERY_OK && out->claimed && out->issued && out->restored);
        CHECK(out->response_valid==0x1fff && out->response_failures==0 && out->ocs==0);
        CHECK(out->raw_valid && out->raw_words_valid==3 && out->raw_words_changed==3);
        CHECK(out->tail_complete && out->tail_ok && out->tail_checked_words==1022);
    } else CHECK(out->decode==UC_BAD_ARGUMENT && !out->android_reference_matches);
}
static void run_one(struct model *m,struct uc_outcome *out,enum behavior behavior,enum capacity_case which)
{
    init(m,behavior);cap_case=which;struct ur_io io=cap_io(m);struct ur_workspace saved;
    uc_run(&io,&saved,out,40,1000);invariant(m,out);
}
int main(void)
{
    struct model m;struct uc_outcome out;struct ur_workspace saved;
    run_one(&m,&out,SUCCESS,CAP_REFERENCE);
    CHECK(out.query_valid && out.decode==UC_DECODE_OK && out.android_reference_matches);
    CHECK(out.geometry.capacity_bytes==UINT64_C(127959826432));
    unsigned reads=m.mem_reads,regs=m.reg_reads,writes=m.mem_writes;
    run_one(&m,&out,SUCCESS,CAP_DIFFERENT);
    CHECK(out.query_valid && out.decode==UC_DECODE_OK && !out.android_reference_matches);
    CHECK(out.geometry.logical_block_bytes==512 && out.geometry.block_count==12346);
    run_one(&m,&out,SUCCESS,CAP_SATURATED);
    CHECK(out.query_valid && out.decode==UC_RC10_SATURATED && !out.android_reference_matches);
    run_one(&m,&out,SUCCESS,CAP_ZERO_BLOCK);
    CHECK(out.query_valid && out.decode==UC_ZERO_BLOCK_BYTES && !out.android_reference_matches);
    run_one(&m,&out,SUCCESS,CAP_NO_DATA);
    CHECK(out.result==UC_QUERY_DATA_UNCHANGED && !out.query_valid && out.raw_words_changed==0);
    run_one(&m,&out,SUCCESS,CAP_PARTIAL_DATA);
    CHECK(out.result==UC_QUERY_DATA_UNCHANGED && !out.query_valid && out.raw_words_changed==1);
    for (tail_word=2;tail_word<1024;tail_word++) {
        run_one(&m,&out,SUCCESS,CAP_TAIL_OVERRUN);
        CHECK(out.result==UC_QUERY_TAIL_CHANGED && out.restored && !out.query_valid);
        CHECK(out.tail_complete && !out.tail_ok && out.tail_first_bad==tail_word*4);
    }
    struct { enum behavior behavior;enum uc_query_result result; } cases[]={
        {NO_COMPLETION,UC_QUERY_UNCERTAIN},{RING_AMBIGUOUS,UC_QUERY_UNCERTAIN},
        {STOP_AFTER_RING,UC_QUERY_UNCERTAIN},{STOP_AT_FINISH,UC_QUERY_UNCERTAIN},
        {RESTORE_REFUSED,UC_QUERY_UNCERTAIN},{NO_FRESH_OCS,UC_QUERY_UNCERTAIN},
        {RESERVED_OCS,UC_QUERY_UNCERTAIN},{READBACK_CORRUPTED,UC_QUERY_PREPARE_FAILED},
        {DATA_WRITE_IGNORED,UC_QUERY_PREPARE_FAILED},{SCSI_ERROR,UC_QUERY_COMPLETED_ERROR},
        {BAD_TAG,UC_QUERY_COMPLETED_ERROR},{BAD_LUN,UC_QUERY_COMPLETED_ERROR},
        {BAD_OCS,UC_QUERY_COMPLETED_ERROR},{SHORT_RESPONSE,UC_QUERY_COMPLETED_ERROR},
        {UNSUPPORTED_SEGMENT,UC_QUERY_COMPLETED_ERROR},{TOO_LONG_SENSE,UC_QUERY_COMPLETED_ERROR},
        {EXTENDED_HEADER,UC_QUERY_COMPLETED_ERROR},{SENSE_EMPTY,UC_QUERY_OK},{SENSE_FULL,UC_QUERY_OK}};
    for (unsigned i=0;i<sizeof(cases)/sizeof(cases[0]);i++) {
        run_one(&m,&out,cases[i].behavior,CAP_REFERENCE);CHECK(out.result==cases[i].result);
        if (cases[i].behavior==RING_AMBIGUOUS || cases[i].behavior==NO_COMPLETION ||
            cases[i].behavior==STOP_AFTER_RING || cases[i].behavior==NO_FRESH_OCS ||
            cases[i].behavior==RESERVED_OCS) CHECK(m.writes_after_ring==0);
    }
    unsigned partial_response=0,partial_raw=0,partial_tail=0;
    for (unsigned kind=0;kind<3;kind++) {
        unsigned total=kind==0?reads:kind==1?regs:writes;
        for (unsigned fail=1;fail<=total;fail++) {
            init(&m,SUCCESS);cap_case=CAP_REFERENCE;struct ur_io io=cap_io(&m);
            if (kind==0) m.fail_read=fail;else if (kind==1) m.fail_reg=fail;else m.fail_write=fail;
            uc_run(&io,&saved,&out,40,1000);invariant(&m,&out);CHECK(!out.query_valid);
            if (kind==0) {
                if (out.response_attempted!=out.response_valid) partial_response++;
                if (out.raw_attempted!=out.raw_words_valid) {
                    partial_raw++;
                    if (!(out.raw_words_valid&1)) CHECK(ur_le32(out.raw)==0);
                    if (!(out.raw_words_valid&2)) CHECK(ur_le32(out.raw+4)==0);
                }
                if (out.raw_valid && !out.tail_complete) partial_tail++;
            }
        }
    }
    CHECK(partial_response==13 && partial_raw==2 && partial_tail==1022);
    init(&m,SUCCESS);m.deny_claim=true;cap_case=CAP_REFERENCE;
    struct ur_io io=cap_io(&m);uc_run(&io,&saved,&out,40,1000);invariant(&m,&out);
    CHECK(out.result==UC_QUERY_NO_LEASE && !out.claimed);
    init(&m,SUCCESS);io=cap_io(&m);uc_run(&io,&saved,&out,0,1000);CHECK(m.reg_reads==0 && m.mem_reads==0 && m.mem_writes==0);
    uc_run(&io,&saved,&out,40,0);CHECK(m.reg_reads==0 && m.mem_reads==0 && m.mem_writes==0);
    uc_run(NULL,&saved,&out,40,1000);uc_run(&io,NULL,&out,40,1000);uc_run(&io,&saved,NULL,40,1000);
    CHECK(m.reg_reads==0 && m.mem_reads==0 && m.mem_writes==0);
    printf("PASS %u capacity-engine checks; reads=%u regs=%u writes=%u partial=%u/%u/%u; model only\n",
           checks,reads,regs,writes,partial_response,partial_raw,partial_tail);
    return 0;
}
