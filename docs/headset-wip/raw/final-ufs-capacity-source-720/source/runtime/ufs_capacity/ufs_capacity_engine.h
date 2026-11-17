/* Host-only720 query engine proposal. No platform mapping/adapter/integration exists here.
 * Reuses sealed719 queue/memory/restore helpers, not its READ10 execution entry.
 */
#ifndef MT6895_UFS_CAPACITY_ENGINE_H
#define MT6895_UFS_CAPACITY_ENGINE_H
#include "../support/ufs_read_engine.h"
#include "ufs_capacity_codec.h"
_Static_assert(UC_UCD_PA==UR_UCD_PA && UC_DATA_PA==UR_DATA_PA,"scratch contracts agree");
_Static_assert(sizeof(struct uc_command)==sizeof(struct ur_command),"command layouts agree");
_Static_assert(UC_DATA_BYTES<=UR_BLOCK_BYTES,"query fits existing scratch");

enum uc_query_result { UC_QUERY_REFUSED, UC_QUERY_NO_LEASE, UC_QUERY_PREPARE_FAILED,
    UC_QUERY_COMPLETED_ERROR, UC_QUERY_TAIL_CHANGED, UC_QUERY_OK, UC_QUERY_UNCERTAIN,
    UC_QUERY_DATA_UNCHANGED };
struct uc_outcome {
    enum uc_query_result result;
    bool claimed,issued,restored,query_valid,raw_valid,tail_complete,tail_ok,android_reference_matches;
    uint32_t polls,ocs,response_attempted,response_valid,response_failures;
    uint32_t raw_attempted,raw_words_valid,raw_words_changed,tail_checked_words,tail_first_bad;
    uint8_t response[UR_RESPONSE_BYTES],raw[UC_DATA_BYTES];
    enum uc_decode_result decode;
    struct uc_geometry geometry;
};

static bool uc_readback(const struct ur_io *io)
{
    struct uc_command back;
    uint32_t word;
    if (!ur_copy_from(io,UR_TABLE,back.utrd,8)) return false;
    for (unsigned i=0;i<8;i++) {
        if (!io->read_word(io->context,UR_UCD,i*4,&word)) return false;
        ur_bytes(word,back.upiu+i*4);
    }
    for (unsigned i=0;i<4;i++)
        if (!io->read_word(io->context,UR_UCD,1024+i*4,&back.prd[i])) return false;
    if (!uc_command_allowed(&back)) return false;
    for (unsigned i=0;i<512/4;i++)
        if (!io->read_word(io->context,UR_UCD,512+i*4,&word) || word!=0) return false;
    for (unsigned i=0;i<UR_BLOCK_BYTES/4;i++)
        if (!io->read_word(io->context,UR_DATA,i*4,&word) || word!=0xa5a5a5a5u) return false;
    return true;
}

/* finish(true) is the last callback. Geometry/reference evaluation afterward is CPU-only.
 * The provider must supply the lease/STOP/return gate; no ownership follows from idle flags.
 * Host false-return faults do not model MMIO abort recovery.
 */
static void uc_run(const struct ur_io *io,struct ur_workspace *saved,struct uc_outcome *out,
                   uint32_t max_polls,uint64_t timeout_us)
{
    struct uc_command capacity;
    struct ur_command command;
    uint64_t started;
    uint32_t dbr,word;
    bool completed=false;
    (void)ur_run; /* imported sealed READ10 entry is intentionally never called */
    if (!out) return;
    for (size_t i=0;i<sizeof(*out);i++) ((uint8_t *)out)[i]=0;
    out->result=UC_QUERY_REFUSED;out->decode=UC_BAD_ARGUMENT;out->tail_first_bad=UINT32_MAX;
    if (!io || !saved || !max_polls || !timeout_us || !io->read_reg || !io->read_word ||
        !io->write_word || !io->claim || !io->finish || !io->stopped || !io->ring_once ||
        !io->barrier || !io->now_us || !uc_make_capacity(2,&capacity)) return;
    for (size_t i=0;i<sizeof(command);i++) ((uint8_t *)&command)[i]=((const uint8_t *)&capacity)[i];
    if (io->stopped(io->context) || !ur_idle(io) ||
        !ur_copy_from(io,UR_TABLE,saved->table,8) ||
        !ur_copy_from(io,UR_UCD,saved->ucd,UR_UCD_BYTES/4) || !ur_previous_read(saved) ||
        !ur_copy_from(io,UR_DATA,saved->data,UR_BLOCK_BYTES/4) ||
        !ur_same(io,UR_TABLE,saved->table,8) || !ur_same(io,UR_UCD,saved->ucd,UR_UCD_BYTES/4) ||
        !ur_same(io,UR_DATA,saved->data,UR_BLOCK_BYTES/4) || !ur_idle(io)) return;
    if (!io->claim(io->context)) { out->result=UC_QUERY_NO_LEASE;return; }
    out->claimed=true;out->result=UC_QUERY_PREPARE_FAILED;
    if (!ur_prepare(io,&command)) goto clean;
    io->barrier(io->context);
    if (!uc_readback(io) || io->stopped(io->context)) goto clean;
    if (!ur_idle(io)) goto poison;
    io->barrier(io->context);started=io->now_us(io->context);
    out->issued=true; /* false ring callback may follow a posted store */
    if (!io->ring_once(io->context)) goto poison;
    io->barrier(io->context);
    while (out->polls<max_polls && io->now_us(io->context)-started<timeout_us) {
        out->polls++;
        if (io->stopped(io->context) || !io->read_reg(io->context,0x58,&dbr)) goto poison;
        if ((dbr&1)==0) {
            io->barrier(io->context);
            if (!io->read_word(io->context,UR_TABLE,8,&word)) goto poison;
            out->ocs=word&0xff;
            if (out->ocs>0x0a && out->ocs!=0x0f) goto poison;
            if (out->ocs!=0x0f) { completed=true;break; }
        }
    }
    if (!completed || !ur_idle(io)) goto poison;
    out->result=UC_QUERY_COMPLETED_ERROR;
    for (unsigned i=0;i<UR_RESPONSE_WORDS;i++) {
        out->response_attempted|=1u<<i;
        if (!io->read_word(io->context,UR_UCD,512+i*4,&word)) goto clean;
        ur_bytes(word,out->response+i*4);out->response_valid|=1u<<i;
    }
    if (out->ocs!=0) goto clean;
    out->response_failures=ur_response_failures(out->response);
    for (unsigned i=0;i<UC_DATA_BYTES/4;i++) {
        out->raw_attempted|=1u<<i;
        if (!io->read_word(io->context,UR_DATA,i*4,&word)) goto clean;
        ur_bytes(word,out->raw+i*4);out->raw_words_valid|=1u<<i;
        if (word!=0xa5a5a5a5u) out->raw_words_changed|=1u<<i;
    }
    out->raw_valid=true;
    for (unsigned i=UC_DATA_BYTES/4;i<UR_BLOCK_BYTES/4;i++) {
        if (!io->read_word(io->context,UR_DATA,i*4,&word)) goto clean;
        out->tail_checked_words++;
        if (word!=0xa5a5a5a5u && out->tail_first_bad==UINT32_MAX) out->tail_first_bad=i*4;
    }
    out->tail_complete=true;out->tail_ok=out->tail_first_bad==UINT32_MAX;
    if (out->response_failures) goto clean;
    if (!out->tail_ok) { out->result=UC_QUERY_TAIL_CHANGED;goto clean; }
    /* Conservative observation rule: a word identical to prefill is not proven fresh.
     * A legitimate equal-valued word remains raw diagnostic data, not accepted geometry.
     */
    if (out->raw_words_changed!=3) { out->result=UC_QUERY_DATA_UNCHANGED;goto clean; }
    out->result=UC_QUERY_OK;
clean:
    if (!ur_restore(io,saved)) goto poison;
    if (!io->finish(io->context,true)) goto poison;
    out->restored=true;out->query_valid=out->result==UC_QUERY_OK;
    if (out->query_valid) {
        out->decode=uc_decode_capacity(out->raw,UC_DATA_BYTES,&out->geometry);
        out->android_reference_matches=out->decode==UC_DECODE_OK && uc_matches_android(&out->geometry);
    }
    return;
poison:
    (void)io->finish(io->context,false);
    out->result=UC_QUERY_UNCERTAIN;out->query_valid=false;
}
#endif
