/* Host-only bounded variable-LBA READ(10) transaction engine, trial721-B v1.
 * Pure transaction logic over injected callbacks. No MMIO, no real address, no device.
 *
 * One admitted request, at most one ring, never reissued. Geometry is decoded only after a
 * fully covered response and data read, a certified whole-area restore and a verified
 * readback. Query acceptance, CRC observation, byte comparison and outer lease release are
 * four separate facts, reported separately.
 */
#ifndef MT6895_UFS_VARIABLE_READ_ENGINE_H
#define MT6895_UFS_VARIABLE_READ_ENGINE_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "ufs_variable_read_codec.h"
#include "support/ufs_read_contract.h"
#include "support/ufs_read_engine.h"
#include "support/ufs_service_lease.h"

#define VRQ_DATA_WORDS   (VR_BLOCK_BYTES / 4u)          /* 1024 */
#define VRQ_HEAD_WORDS   2u                             /* the decoded 8 bytes live here */
#define VRQ_TAIL_WORDS   (VRQ_DATA_WORDS - VRQ_HEAD_WORDS) /* 1022 */
#define VRQ_RESP_WORDS   13u
#define VRQ_RESP_OFF     512u
#define VRQ_RESP_BYTES   512u
#define VRQ_MAX_POLLS    500000u
#define VRQ_TIMEOUT_US   1000000ull

enum vrq_result {
    VRQ_REFUSED = 0, VRQ_NO_LEASE, VRQ_PREPARE_FAILED, VRQ_READBACK_FAILED,
    VRQ_RING_FAILED, VRQ_UNCERTAIN, VRQ_PARTIAL, VRQ_REJECTED, VRQ_DELIVERED
};

/* CRC is an observation. Equal checksums are NOT byte equality, so this is a separate
 * verdict from "the delivery was accepted".
 */
enum vrq_data_match { VRQ_DATA_UNKNOWN = 0, VRQ_DATA_DIFFERENT, VRQ_DATA_SAME_CHECKSUM };

struct vrq_outcome {
    enum vrq_result result;
    bool claimed, prepared, readback_ok, issued, completed;
    bool response_complete, data_complete, restored, readback_restored;
    uint32_t polls, ocs, response_attempted, response_valid, response_failures;
    uint32_t data_words_attempted, data_words_valid;   /* exact counts, 0..1024 */
    uint32_t lba, blocks, block_bytes;      /* decoded, VRQ_DELIVERED only */
    uint32_t response[VRQ_RESP_WORDS];      /* raw response words, kept for the parser */
    uint8_t  data[VR_BLOCK_BYTES];          /* delivered payload */
    uint32_t data_crc32, reference_crc32;
    bool reference_present;
    enum vrq_data_match data_match;
};

struct vrq_io {
    bool (*read_reg)(void *, uint16_t off, uint32_t *out);
    bool (*read_word)(void *, int region, uint32_t off, uint32_t *out);
    bool (*write_word)(void *, int region, uint32_t off, uint32_t value);
    bool (*claim)(void *);
    bool (*finish)(void *, bool restored);
    bool (*stopped)(void *);
    bool (*ring)(void *);
    void (*barrier)(void *);
    uint64_t (*now_us)(void *);
    void *context;
};

enum { VRQ_TABLE = 0, VRQ_UCD = 1, VRQ_DATA = 2 };

/* Every callback invocation the engine attempted. A refusal must leave all of these zero. */
struct vrq_counters {
    uint32_t read_reg, read_word, write_word, claim, finish, ring, barrier, stopped;
};

/* Counters live in one place so every callback site is instrumented identically. */
static inline void vrq_note(struct vrq_counters *c, int which)
{
    if (!c) return;
    switch (which) {
    case 0: c->read_reg++; break;
    case 1: c->read_word++; break;
    case 2: c->write_word++; break;
    case 3: c->claim++; break;
    case 4: c->finish++; break;
    case 5: c->ring++; break;
    case 7: c->stopped++; break;
    default: c->barrier++; break;
    }
}

static inline void vrq_put32le(uint8_t *b, uint32_t v)
{ b[0]=(uint8_t)v; b[1]=(uint8_t)(v>>8); b[2]=(uint8_t)(v>>16); b[3]=(uint8_t)(v>>24); }

static inline uint32_t vrq_get32le(const uint8_t *b)
{ return (uint32_t)b[0] | ((uint32_t)b[1]<<8) | ((uint32_t)b[2]<<16) | ((uint32_t)b[3]<<24); }

static inline uint32_t vrq_crc32(const uint8_t *b, size_t n)
{
    uint32_t c = 0xffffffffu;
    for (size_t i=0;i<n;i++) {
        c ^= b[i];
        for (int k=0;k<8;k++) c = (c>>1) ^ (0xedb88320u & (0u - (c & 1u)));
    }
    return ~c;
}

static inline bool vrq_idle(struct vrq_counters *ctr, const struct vrq_io *io)
{
    static const uint16_t offs[8] = { 0, 8, 0x30, 0x34, 0x50, 0x54, 0x58, 0x60 };
    uint32_t w[8];
    for (unsigned k=0;k<8;k++) {
        vrq_note(ctr,0);
        if (!io->read_reg(io->context, offs[k], &w[k])) return false;
    }
    return w[0] != UINT32_MAX && (w[0] & 31) == 31 && w[1] == 0x200 &&
           w[2] != UINT32_MAX && (w[2] & 3) == 3 && w[3] != UINT32_MAX && (w[3] & 1) &&
           w[4] == (uint32_t)UR_LIST_PA && w[5] == 0 && w[6] == 0 && (w[7] & 1);
}

static inline bool vrq_prepare(struct vrq_counters *ctr, const struct vrq_io *io,
                               const struct vr_command *c)
{
    /* Clear the WHOLE UCD first, exactly as the sealed 719 engine does. Writing only the
     * eight UPIU words left a stale response descriptor intact, and it reached the ring.
     * The range stays UR_UCD_BYTES — the inherited mapping is not enlarged.
     */
    for (unsigned i=0;i<UR_UCD_BYTES/4;i++) {
        vrq_note(ctr,2);
        if (!io->write_word(io->context, VRQ_UCD, i*4, 0u)) return false;
    }
    for (unsigned i=0;i<8;i++) {
        vrq_note(ctr,2);
        if (!io->write_word(io->context, VRQ_TABLE, i*4, c->utrd[i])) return false;
    }
    for (unsigned i=0;i<8;i++) {
        vrq_note(ctr,2);
        if (!io->write_word(io->context, VRQ_UCD, i*4, vrq_get32le(c->upiu + i*4))) return false;
    }
    for (unsigned i=0;i<4;i++) {
        vrq_note(ctr,2);
        if (!io->write_word(io->context, VRQ_UCD, 1024 + i*4, c->prd[i])) return false;
    }
    for (unsigned i=0;i<VRQ_DATA_WORDS;i++) {
        vrq_note(ctr,2);
        if (!io->write_word(io->context, VRQ_DATA, i*4, 0xa5a5a5a5u)) return false;
    }
    return true;
}

/* The readback must equal THIS request's command. vr_command_lba is only a local field
 * extractor; acceptance always goes through vr_command_allowed, never through the extractor
 * alone, so a descriptor that differs in UTRD, PRDT, LUN or flags is refused here.
 */
static inline bool vrq_readback(struct vrq_counters *ctr, const struct vrq_io *io,
                                const struct vr_request *r, const struct vr_limits *lim)
{
    struct vr_command back;
    uint32_t w;
    for (unsigned i=0;i<8;i++) {
        vrq_note(ctr,1);
        if (!io->read_word(io->context, VRQ_TABLE, i*4, &back.utrd[i])) return false;
    }
    for (unsigned i=0;i<8;i++) {
        vrq_note(ctr,1);
        if (!io->read_word(io->context, VRQ_UCD, i*4, &w)) return false;
        vrq_put32le(back.upiu + i*4, w);
    }
    for (unsigned i=0;i<4;i++) {
        vrq_note(ctr,1);
        if (!io->read_word(io->context, VRQ_UCD, 1024 + i*4, &back.prd[i])) return false;
    }
    return vr_command_allowed(&back, r, lim);
}

/* After prepare the whole 4 KiB must read back as the prefill pattern, so a later change is
 * attributable to the device rather than to a stale buffer.
 */
static inline bool vrq_prefill_ok(struct vrq_counters *ctr, const struct vrq_io *io)
{
    uint32_t w;
    for (unsigned i=0;i<VRQ_DATA_WORDS;i++) {
        vrq_note(ctr,1);
        if (!io->read_word(io->context, VRQ_DATA, i*4, &w)) return false;
        if (w != 0xa5a5a5a5u) return false;
    }
    /* The 512-byte response area must also read back as zero: a stale descriptor there would
     * otherwise be accepted as this command's answer.
     */
    for (unsigned i=0;i<VRQ_RESP_BYTES/4;i++) {
        vrq_note(ctr,1);
        if (!io->read_word(io->context, VRQ_UCD, VRQ_RESP_OFF + i*4, &w)) return false;
        if (w != 0u) return false;
    }
    return true;
}

static inline bool vrq_restore(struct vrq_counters *ctr, const struct vrq_io *io,
                               const struct ur_workspace *saved)
{
    uint32_t w;
    if (!vrq_idle(ctr, io)) return false;
    for (unsigned i=0;i<UR_UCD_BYTES/4;i++) {
        vrq_note(ctr,2);
        if (!io->write_word(io->context, VRQ_UCD, i*4, saved->ucd[i])) return false;
    }
    for (unsigned i=0;i<VRQ_DATA_WORDS;i++) {
        vrq_note(ctr,2);
        if (!io->write_word(io->context, VRQ_DATA, i*4, saved->data[i])) return false;
    }
    for (unsigned i=0;i<8;i++) {
        vrq_note(ctr,2);
        if (!io->write_word(io->context, VRQ_TABLE, i*4, saved->table[i])) return false;
    }
    vrq_note(ctr,6);
    io->barrier(io->context);
    for (unsigned i=0;i<UR_UCD_BYTES/4;i++) {
        vrq_note(ctr,1);
        if (!io->read_word(io->context, VRQ_UCD, i*4, &w) || w != saved->ucd[i]) return false;
    }
    for (unsigned i=0;i<VRQ_DATA_WORDS;i++) {
        vrq_note(ctr,1);
        if (!io->read_word(io->context, VRQ_DATA, i*4, &w) || w != saved->data[i]) return false;
    }
    for (unsigned i=0;i<8;i++) {
        vrq_note(ctr,1);
        if (!io->read_word(io->context, VRQ_TABLE, i*4, &w) || w != saved->table[i]) return false;
    }
    return vrq_idle(ctr, io);
}

/* ---- the engine ----------------------------------------------------------------
 *
 * Safety steps preserved from the frozen 719 transaction: verify idle, snapshot, confirm the
 * inherited baseline with ur_previous_read, sample table/UCD/data TWICE to prove nothing moved,
 * then claim; prepare clears the whole UCD (including stale response bytes) and prefills the
 * whole 4 KiB; readback must match this request exactly AND the prefill; every completion path
 * restores, compares and re-checks idle before finish is called.
 *
 * READ(10) moves 4096 bytes of payload. All of them are data, so any byte may legitimately
 * change. There is no partial-payload canary here: that rule belongs to the READ CAPACITY(10)
 * query, does not apply to READ(10), and must not be carried over.
 */
static inline bool vrq_stopped(struct vrq_counters *ctr, const struct vrq_io *io)
{
    vrq_note(ctr,7);
    return io->stopped(io->context);
}

static inline void vrq_run(const struct vrq_io *io, const struct vr_request *r,
                           const struct vr_limits *lim, struct ur_workspace *saved,
                           struct vrq_outcome *out, struct vrq_counters *ctr,
                           bool reference_present, uint32_t reference_crc32)
{
    struct vr_command cmd;
    uint64_t started;
    uint32_t dbr, w, i;
    bool completed = false;

    if (ctr) { ctr->read_reg=ctr->read_word=ctr->write_word=0;
               ctr->claim=ctr->finish=ctr->ring=ctr->barrier=ctr->stopped=0; }
    if (!out) return;
    for (i=0;i<sizeof(*out);i++) ((uint8_t*)out)[i]=0;
    out->result = VRQ_REFUSED;
    out->data_match = VRQ_DATA_UNKNOWN;

    /* 1. Validate first: a refused request must not reach any callback. */
    if (!io || !r || !lim || !saved) return;
    if (!io->read_reg || !io->read_word || !io->write_word || !io->claim || !io->finish ||
        !io->stopped || !io->ring || !io->barrier || !io->now_us) return;
    if (!vr_request_valid(r, lim)) return;

    /* 2. Idle, snapshot, inherited-baseline check, then a second sample proving stillness. */
    if (vrq_stopped(ctr,io) || !vrq_idle(ctr,io)) return;
    for (i=0;i<8;i++) { vrq_note(ctr,1); if (!io->read_word(io->context,VRQ_TABLE,i*4,&saved->table[i])) return; }
    for (i=0;i<UR_UCD_BYTES/4;i++) { vrq_note(ctr,1); if (!io->read_word(io->context,VRQ_UCD,i*4,&saved->ucd[i])) return; }
    if (!ur_previous_read(saved)) return;         /* inherited READ(10) baseline must still hold */
    for (i=0;i<VRQ_DATA_WORDS;i++) { vrq_note(ctr,1); if (!io->read_word(io->context,VRQ_DATA,i*4,&saved->data[i])) return; }
    /* Second sample of ALL THREE areas. A first round that read data, then a caller that
     * changed data[0], must be caught here — comparing only table and UCD was a real hole.
     */
    for (i=0;i<8;i++) { vrq_note(ctr,1); if (!io->read_word(io->context,VRQ_TABLE,i*4,&w) || w!=saved->table[i]) return; }
    for (i=0;i<UR_UCD_BYTES/4;i++) { vrq_note(ctr,1); if (!io->read_word(io->context,VRQ_UCD,i*4,&w) || w!=saved->ucd[i]) return; }
    for (i=0;i<VRQ_DATA_WORDS;i++) { vrq_note(ctr,1); if (!io->read_word(io->context,VRQ_DATA,i*4,&w) || w!=saved->data[i]) return; }
    if (!vrq_idle(ctr,io)) return;

    /* 3. Claim the outer lease through the real admission protocol. */
    vrq_note(ctr,3);
    if (!io->claim(io->context)) { out->result = VRQ_NO_LEASE; return; }
    out->claimed = true;

    /* 4. Build for THIS request, place it, and verify both the command and the prefill. */
    if (!vr_make_read(r, lim, &cmd)) { out->result = VRQ_PREPARE_FAILED; goto clean; }
    out->lba = r->lba; out->blocks = r->blocks; out->block_bytes = r->logical_block_bytes;
    if (!vrq_prepare(ctr, io, &cmd)) { out->result = VRQ_PREPARE_FAILED; goto clean; }
    out->prepared = true;
    vrq_note(ctr,6); io->barrier(io->context);
    if (!vrq_readback(ctr, io, r, lim)) { out->result = VRQ_READBACK_FAILED; goto clean; }
    out->readback_ok = true;
    if (!vrq_prefill_ok(ctr, io)) { out->result = VRQ_READBACK_FAILED; goto clean; }
    if (vrq_stopped(ctr,io) || !vrq_idle(ctr,io)) goto poison;

    /* 5. Ring exactly once. */
    vrq_note(ctr,6); io->barrier(io->context);
    started = io->now_us(io->context);
    out->issued = true;
    vrq_note(ctr,5);
    if (!io->ring(io->context)) { out->result = VRQ_RING_FAILED; goto poison; }
    vrq_note(ctr,6); io->barrier(io->context);

    /* 6. Poll; never reissue. */
    while (out->polls < VRQ_MAX_POLLS && io->now_us(io->context) - started < VRQ_TIMEOUT_US) {
        out->polls++;
        if (vrq_stopped(ctr,io)) goto poison;
        vrq_note(ctr,0);
        if (!io->read_reg(io->context, 0x58, &dbr)) goto poison;
        if ((dbr & 1u) == 0u) {
            vrq_note(ctr,6); io->barrier(io->context);
            vrq_note(ctr,1);
            if (!io->read_word(io->context, VRQ_TABLE, 8, &w)) goto poison;
            out->ocs = w & 0xffu;
            if (out->ocs > 0x0a && out->ocs != 0x0f) goto poison;
            if (out->ocs != 0x0f) { completed = true; break; }
        }
    }
    if (!completed) { out->result = VRQ_UNCERTAIN; goto poison; }
    if (!vrq_idle(ctr,io)) goto poison;
    out->completed = true;

    /* 7. Response: thirteen words or nothing. Acceptance uses the sealed predicate. */
    for (i=0;i<VRQ_RESP_WORDS;i++) {
        out->response_attempted |= 1u<<i;
        vrq_note(ctr,1);
        if (!io->read_word(io->context, VRQ_UCD, 512 + i*4, &out->response[i])) {
            out->result = VRQ_PARTIAL; goto clean;
        }
        out->response_valid |= 1u<<i;
    }
    out->response_complete = true;
    {   uint8_t image[52];
        for (i=0;i<VRQ_RESP_WORDS;i++) vrq_put32le(image + i*4, out->response[i]);
        out->response_failures = ur_response_failures(image); }
    if (out->ocs != 0u || out->response_failures != 0u) { out->result = VRQ_REJECTED; goto clean; }

    /* 8. Data: the whole 4096 bytes. Every byte is payload, so no byte is a canary. */
    for (i=0;i<VRQ_DATA_WORDS;i++) {
        out->data_words_attempted++;
        vrq_note(ctr,1);
        if (!io->read_word(io->context, VRQ_DATA, i*4, &w)) {
            out->result = VRQ_PARTIAL; goto clean;      /* coverage incomplete */
        }
        vrq_put32le(out->data + i*4, w);
        out->data_words_valid++;
    }
    out->data_complete = true;
    out->result = VRQ_DELIVERED;
clean:
    if (!vrq_restore(ctr, io, saved)) goto poison;
    out->restored = true;
    vrq_note(ctr,4);
    if (!io->finish(io->context, true)) goto poison;
    out->readback_restored = true;
    out->data_crc32 = vrq_crc32(out->data, VR_BLOCK_BYTES);
    out->reference_crc32 = reference_crc32;
    out->reference_present = reference_present;
    /* Acceptance, reference-CRC agreement and byte comparison are three separate reports.
     * A checksum comparison is only meaningful for a FULL, accepted 4096-byte read, and only
     * when a reference was actually supplied. A partial or rejected outcome never gets a
     * full-block reference label, and 0 is not silently treated as "no reference".
     */
    if (!reference_present)            out->data_match = VRQ_DATA_UNKNOWN;
    else if (out->result != VRQ_DELIVERED || !out->data_complete || out->data_words_valid != VRQ_DATA_WORDS)
                                       out->data_match = VRQ_DATA_UNKNOWN;
    else if (out->data_crc32 == reference_crc32) out->data_match = VRQ_DATA_SAME_CHECKSUM;
    else                               out->data_match = VRQ_DATA_DIFFERENT;
    return;
poison:
    vrq_note(ctr,4);
    (void)io->finish(io->context, false);
    out->result = VRQ_UNCERTAIN;
    out->restored = false;
}
#endif
