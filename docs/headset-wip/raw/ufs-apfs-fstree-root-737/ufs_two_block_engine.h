/* Trial724 host model for one READ(10) of one or two blocks.
 * All shared data accesses are bounded by the validated command length.
 * The controller and memory accesses are supplied by callbacks.
 */
#ifndef MT6895_UFS_TWO_BLOCK_ENGINE_H
#define MT6895_UFS_TWO_BLOCK_ENGINE_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "../codec/ufs_two_block_codec.h"
#include "../support/ufs_read_contract.h"
#include "../support/ufs_read_engine.h"
#include "../support/ufs_service_lease.h"

#define VRQ_BLOCK_WORDS  (VR_BLOCK_BYTES / 4u)
#define VRQ_DATA_WORDS   (VR2_TRANSFER_BYTES / 4u)
#define VRQ_RESP_WORDS   13u
#define VRQ_RESP_OFF     512u
#define VRQ_RESP_BYTES   512u
#define VRQ_MAX_POLLS    500000u
#define VRQ_TIMEOUT_US   1000000ull

_Static_assert(VR_BLOCK_BYTES == UR_BLOCK_BYTES, "inherited snapshot block size");
_Static_assert(VRQ_DATA_WORDS == 2u * VRQ_BLOCK_WORDS, "two-block snapshot size");

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
    uint32_t data_words_attempted, data_words_valid;   /* exact counts, 0..2048 */
    uint32_t lba, blocks, block_bytes;      /* decoded, VRQ_DELIVERED only */
    uint32_t response[VRQ_RESP_WORDS];      /* raw response words, kept for the parser */
    uint8_t  data[VR2_TRANSFER_BYTES];       /* capacity; delivered length is blocks * block_bytes */
    uint32_t data_crc32, reference_crc32;
    bool reference_present;
    enum vrq_data_match data_match;
};

struct vrq_workspace {
    struct ur_workspace baseline;
    uint32_t second_block[VRQ_BLOCK_WORDS];
};

static inline uint32_t *vrq_saved_word(struct vrq_workspace *saved, uint32_t index)
{
    if (index < VRQ_BLOCK_WORDS) {
        return &saved->baseline.data[index];
    }
    return &saved->second_block[index - VRQ_BLOCK_WORDS];
}

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

/* The host candidate admits the same 16-LBA trial window for both lengths. */
static inline bool vrq_make_read(const struct vr_request *request, const struct vr_limits *limits,
                                 struct vr_command *command)
{
    if (!request || !limits || limits->lba_max_exclusive > vr_runtime_limits().lba_max_exclusive)
        return false;
    if (request->blocks == VR_BLOCKS)
        return vr_make_read(request, limits, command);
    return vr2_make_read(request, limits, command);
}

#define GPT_BACKUP_HEADER_LBA (VR_CAPACITY_BLOCKS - 1u)
#define GPT_BACKUP_ARRAY_LBA  UINT32_C(31240184)
/* Trial734: first block of the xnusys partition, measured on this device. */
#define XNUSYS_LBA            UINT32_C(30580480)

/* Only these three READ(10) commands belong to the captured backup layout. */
static inline bool
vrq_make_backup_read(const struct vr_request *request, const struct vr_limits *limits,
                     struct vr_command *command)
{
    if (!request || !limits || limits->lba_max_exclusive != VR_CAPACITY_BLOCKS) return false;
    if (request->lba == GPT_BACKUP_HEADER_LBA)
        return vr_make_read(request, limits, command);
    if (request->lba == GPT_BACKUP_ARRAY_LBA || request->lba == GPT_BACKUP_ARRAY_LBA + 2u)
        return vr2_make_read(request, limits, command);
    return false;
}

enum vrq_read_scope { VRQ_STAGE_READ, VRQ_BACKUP_READ, VRQ_XNUSYS_READ, VRQ_APFS_READ };

/* Trial735: the APFS chain. Only these five offsets exist, and only with the length each step is
 * allowed to move. Step 5 is a pair; steps 6..8 are single blocks. Anything else is refused here,
 * so a wrong answer from the walker cannot turn into a command. */
/* The region a Trial737 walk may reach, and how many blocks it may fetch doing so. */
#define VRQ_APFS_OMAP_LEAF_LOW  UINT32_C(2399)
#define VRQ_APFS_OMAP_LEAF_HIGH UINT32_C(2509)
#define VRQ_APFS_TREE_LOW       UINT32_C(2510)
/*
 * The tree's nodes are not packed next to the map: this volume's run from 2510 up to 14822,
 * because the directories' leaf blocks sit far above their parents. The bound has to cover the
 * whole run or a legitimate descent is refused, which is exactly how this number was found --
 * the first attempt stopped at 4095 and the walk died one level short of an answer.
 */
#define VRQ_APFS_TREE_HIGH      UINT32_C(16383)
#define VRQ_APFS_WALK_MAX       40u

static inline bool
vrq_make_apfs_read(const struct vr_request *request, const struct vr_limits *limits,
                   struct vr_command *command, uint32_t ordinal)
{
    if (!request || !limits || limits->lba_max_exclusive != VR_CAPACITY_BLOCKS) return false;
    if (request->lun != VR_LUN || request->logical_block_bytes != VR_BLOCK_BYTES) return false;
    const uint32_t rel = request->lba - XNUSYS_LBA;
    if (request->lba < XNUSYS_LBA) return false;
    /* Ordinals match the published command numbering: 5 is the checkpoint descriptor pair,
     * 6 the OMAP, 7 the root leaf, 8 the APSB. */
    switch (ordinal) {
    case 5u:
        return rel == 1u && request->blocks == VR2_BLOCKS &&
            vr2_make_read(request, limits, command);
    case 6u:
        return rel == 2394u && request->blocks == VR_BLOCKS &&
            vr_make_read(request, limits, command);
    case 7u:
        return rel == 2395u && request->blocks == VR_BLOCKS &&
            vr_make_read(request, limits, command);
    case 8u:
        return rel == 2396u && request->blocks == VR_BLOCKS &&
            vr_make_read(request, limits, command);
    /*
     * Trial736. The walker derives these four from objects it has already checked, and this table
     * is the separate authority that says which addresses may actually be asked for. Keeping the
     * two apart is what makes a parser bug unable to widen the read: an ordinal that names a
     * different block than the table allows is refused before any command is built.
     */
    case 9u:
        return rel == 2397u && request->blocks == VR_BLOCKS &&
            vr_make_read(request, limits, command);
    case 10u:
        return rel == 2398u && request->blocks == VR_BLOCKS &&
            vr_make_read(request, limits, command);
    case 11u:
        return rel == 2399u && request->blocks == VR_BLOCKS &&
            vr_make_read(request, limits, command);
    case 12u:
        return rel == 2510u && request->blocks == VR_BLOCKS &&
            vr_make_read(request, limits, command);
    /*
     * Trial737 walks the file-system tree, so the blocks it asks for cannot be listed in advance:
     * each one is named by a key of a node that was already checked, and the object map is what
     * turns a virtual identifier into an address. A fixed table would either be wrong the moment
     * the volume changed, or so wide that it stopped being a check.
     *
     * What is kept is the bound. Everything the walk may touch lies in the region these two
     * ranges describe, and outside them nothing is expressible at all:
     *
     *   2399 .. 2509   the object map's leaves, one per run of identifiers
     *   2510 .. 4095   the file-system tree's own nodes
     *
     * The walker is what decides which block inside that region is correct, and it can only
     * arrive there from a value that survived a header check; this says the walker may not point
     * anywhere else even if it is wrong. Keeping the two roles apart is the same arrangement the
     * fixed steps use -- only the shape of the second role changed, from a list to a range.
     */
    default:
        if (ordinal <= 12u) return false;
        if (ordinal > VRQ_APFS_WALK_MAX) return false;
        if (request->blocks != VR_BLOCKS) return false;
        if (rel < VRQ_APFS_OMAP_LEAF_LOW || rel > VRQ_APFS_TREE_HIGH) return false;
        if (rel > VRQ_APFS_OMAP_LEAF_HIGH && rel < VRQ_APFS_TREE_LOW) return false;
        return vr_make_read(request, limits, command);
    }
}

static inline bool
vrq_encode(const struct vr_request *request, const struct vr_limits *limits,
           struct vr_command *cmd, enum vrq_read_scope scope, uint32_t ordinal)
{
    switch (scope) {
    case VRQ_STAGE_READ: return vrq_make_read(request, limits, cmd);
    case VRQ_BACKUP_READ: return vrq_make_backup_read(request, limits, cmd);
    case VRQ_XNUSYS_READ:
        return request && request->lba == XNUSYS_LBA && limits &&
            limits->lba_max_exclusive == VR_CAPACITY_BLOCKS && vr_make_read(request, limits, cmd);
    case VRQ_APFS_READ: return vrq_make_apfs_read(request, limits, cmd, ordinal);
    }
    return false;
}

static inline bool vrq_prepare(struct vrq_counters *ctr, const struct vrq_io *io,
                               const struct vr_command *c, uint32_t data_words)
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
    for (unsigned i=0;i<data_words;i++) {
        vrq_note(ctr,2);
        if (!io->write_word(io->context, VRQ_DATA, i*4, 0xa5a5a5a5u)) return false;
    }
    return true;
}

/* Readback compares the whole descriptor, including the request length, LBA and PRD. */
static inline bool vrq_readback(struct vrq_counters *ctr, const struct vrq_io *io,
                                const struct vr_command *expected)
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
    return memcmp(&back, expected, sizeof(back)) == 0;
}

/* Verify the prefill over the requested data span before issuing the request. */
static inline bool vrq_prefill_ok(struct vrq_counters *ctr, const struct vrq_io *io,
                                  uint32_t data_words)
{
    uint32_t w;
    for (unsigned i=0;i<data_words;i++) {
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
                               struct vrq_workspace *saved, uint32_t data_words)
{
    uint32_t w;
    if (!vrq_idle(ctr, io)) return false;
    for (unsigned i=0;i<UR_UCD_BYTES/4;i++) {
        vrq_note(ctr,2);
        if (!io->write_word(io->context, VRQ_UCD, i*4, saved->baseline.ucd[i])) return false;
    }
    for (unsigned i=0;i<data_words;i++) {
        vrq_note(ctr,2);
        if (!io->write_word(io->context, VRQ_DATA, i*4, *vrq_saved_word(saved, i))) return false;
    }
    for (unsigned i=0;i<8;i++) {
        vrq_note(ctr,2);
        if (!io->write_word(io->context, VRQ_TABLE, i*4, saved->baseline.table[i])) return false;
    }
    vrq_note(ctr,6);
    io->barrier(io->context);
    for (unsigned i=0;i<UR_UCD_BYTES/4;i++) {
        vrq_note(ctr,1);
        if (!io->read_word(io->context, VRQ_UCD, i*4, &w) || w != saved->baseline.ucd[i]) return false;
    }
    for (unsigned i=0;i<data_words;i++) {
        vrq_note(ctr,1);
        if (!io->read_word(io->context, VRQ_DATA, i*4, &w) || w != *vrq_saved_word(saved, i)) return false;
    }
    for (unsigned i=0;i<8;i++) {
        vrq_note(ctr,1);
        if (!io->read_word(io->context, VRQ_TABLE, i*4, &w) || w != saved->baseline.table[i]) return false;
    }
    return vrq_idle(ctr, io);
}

/* ---- the engine ----------------------------------------------------------------
 *
 * Safety steps preserved from the frozen 719 transaction: verify idle, snapshot, confirm the
 * inherited baseline with ur_previous_read, sample table/UCD/data TWICE to prove nothing moved,
 * then claim; prepare clears the whole UCD (including stale response bytes) and prefills the
 * requested data span; readback must match this request exactly AND the prefill; every completion path
 * restores, compares and re-checks idle before finish is called.
 *
 * The completed READ(10) has 4096 or 8192 bytes of payload. The host observes CPU sampling coverage.
 */
static inline bool vrq_stopped(struct vrq_counters *ctr, const struct vrq_io *io)
{
    vrq_note(ctr,7);
    return io->stopped(io->context);
}

static inline void vrq_run_scope(const struct vrq_io *io, const struct vr_request *r,
                           const struct vr_limits *lim, struct vrq_workspace *saved,
                           struct vrq_outcome *out, struct vrq_counters *ctr,
                           bool reference_present, uint32_t reference_crc32,
                           enum vrq_read_scope scope, uint32_t ordinal)
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
    const struct vr_request request = *r;
    const struct vr_limits limits = *lim;
    if (!vrq_encode(&request, &limits, &cmd, scope, ordinal)) return;
    const uint32_t data_words = request.blocks * VRQ_BLOCK_WORDS;
    const uint32_t transfer_bytes = request.blocks * VR_BLOCK_BYTES;

    /* 2. Idle, snapshot, inherited-baseline check, then a second sample proving stillness. */
    if (vrq_stopped(ctr,io) || !vrq_idle(ctr,io)) return;
    for (i = 0; i < 8; i++) {
        vrq_note(ctr,1);
        if (!io->read_word(io->context, VRQ_TABLE, i * 4, &saved->baseline.table[i])) return;
    }
    for (i = 0; i < UR_UCD_BYTES / 4; i++) {
        vrq_note(ctr,1);
        if (!io->read_word(io->context, VRQ_UCD, i * 4, &saved->baseline.ucd[i])) return;
    }
    if (!ur_previous_read(&saved->baseline)) return;
    for (i = 0; i < data_words; i++) {
        vrq_note(ctr,1);
        if (!io->read_word(io->context, VRQ_DATA, i * 4, vrq_saved_word(saved, i))) return;
    }
    /* Confirm the table, command and the requested data words are stable before claiming. */
    for (i = 0; i < 8; i++) {
        vrq_note(ctr,1);
        if (!io->read_word(io->context, VRQ_TABLE, i * 4, &w) || w != saved->baseline.table[i]) return;
    }
    for (i = 0; i < UR_UCD_BYTES / 4; i++) {
        vrq_note(ctr,1);
        if (!io->read_word(io->context, VRQ_UCD, i * 4, &w) || w != saved->baseline.ucd[i]) return;
    }
    for (i = 0; i < data_words; i++) {
        vrq_note(ctr,1);
        if (!io->read_word(io->context, VRQ_DATA, i * 4, &w) || w != *vrq_saved_word(saved, i)) return;
    }
    if (!vrq_idle(ctr,io)) return;

    /* 3. Claim the outer lease through the real admission protocol. */
    vrq_note(ctr,3);
    if (!io->claim(io->context)) { out->result = VRQ_NO_LEASE; return; }
    out->claimed = true;

    /* 4. Place the validated request and verify both the command and the prefill. */
    out->lba = request.lba; out->blocks = request.blocks; out->block_bytes = request.logical_block_bytes;
    if (!vrq_prepare(ctr, io, &cmd, data_words)) { out->result = VRQ_PREPARE_FAILED; goto clean; }
    out->prepared = true;
    vrq_note(ctr,6); io->barrier(io->context);
    if (!vrq_readback(ctr, io, &cmd)) { out->result = VRQ_READBACK_FAILED; goto clean; }
    out->readback_ok = true;
    if (!vrq_prefill_ok(ctr, io, data_words)) { out->result = VRQ_READBACK_FAILED; goto clean; }
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

    /* 8. Sample the requested blocks in order. */
    for (i=0;i<data_words;i++) {
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
    if (!vrq_restore(ctr, io, saved, data_words)) goto poison;
    out->restored = true;
    vrq_note(ctr,4);
    if (!io->finish(io->context, true)) goto poison;
    out->readback_restored = true;
    if (out->result == VRQ_DELIVERED && out->data_complete)
        out->data_crc32 = vrq_crc32(out->data, transfer_bytes);
    out->reference_crc32 = reference_crc32;
    out->reference_present = reference_present;
    /* Acceptance, reference-CRC agreement and byte comparison are three separate reports.
     * A checksum comparison is only meaningful for a full, accepted read, and only
     * when a reference was actually supplied. A partial or rejected outcome never gets a
     * complete-transfer reference label, and 0 is not silently treated as "no reference".
     */
    if (!reference_present)            out->data_match = VRQ_DATA_UNKNOWN;
    else if (out->result != VRQ_DELIVERED || !out->data_complete || out->data_words_valid != data_words)
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

/* The unchanged entry point: always the stage window. */
static inline void vrq_run(const struct vrq_io *io, const struct vr_request *r,
                           const struct vr_limits *lim, struct vrq_workspace *saved,
                           struct vrq_outcome *out, struct vrq_counters *ctr,
                           bool reference_present, uint32_t reference_crc32)
{
    vrq_run_scope(io, r, lim, saved, out, ctr, reference_present, reference_crc32,
                  VRQ_STAGE_READ, 0u);
}
#endif
