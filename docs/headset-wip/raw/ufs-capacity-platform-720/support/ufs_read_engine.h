/* Bounded single-request engine candidate. No platform adapter or production integration. */
#ifndef MT6895_UFS_READ_ENGINE_H
#define MT6895_UFS_READ_ENGINE_H
#include "ufs_read_contract.h"
#define UR_RESPONSE_BYTES 52u
#define UR_RESPONSE_WORDS (UR_RESPONSE_BYTES / 4u)
#define UR_RESPONSE_VALID_MASK ((1u << UR_RESPONSE_WORDS) - 1u)

enum ur_memory { UR_TABLE, UR_UCD, UR_DATA };
enum ur_result { UR_REFUSED, UR_NO_LEASE, UR_PREPARE_FAILED, UR_COMPLETED_ERROR,
                 UR_REFERENCE_MISMATCH, UR_READ_OK, UR_UNCERTAIN_RESULT };
struct ur_io {
    bool (*read_reg)(void *, uint16_t, uint32_t *);
    bool (*read_word)(void *, enum ur_memory, uint32_t, uint32_t *);
    bool (*write_word)(void *, enum ur_memory, uint32_t, uint32_t);
    bool (*claim)(void *); /* publishes shared CLAIMED before returning true */
    bool (*finish)(void *, bool); /* true only if clean restore won against STOP */
    bool (*stopped)(void *);
    bool (*ring_once)(void *); /* exactly UTRLDBR=1; false is ambiguous, not "no write" */
    void (*barrier)(void *);
    uint64_t (*now_us)(void *);
    void *context;
};
struct ur_workspace {
    uint32_t table[8], ucd[UR_UCD_BYTES / 4], data[UR_BLOCK_BYTES / 4];
};
struct ur_outcome {
    enum ur_result result;
    bool claimed, issued, restored, data_valid;
    uint32_t ocs, crc32, polls;
    uint32_t response_attempted, response_valid, response_failures;
    bool diagnostic_valid;
    uint32_t diagnostic_crc32;
    uint8_t response[UR_RESPONSE_BYTES], block[UR_BLOCK_BYTES];
};

static inline uint32_t ur_le32(const uint8_t *b)
{ return (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24; }
static inline uint32_t ur_be32(const uint8_t *b)
{ return (uint32_t)b[0] << 24 | (uint32_t)b[1] << 16 | (uint32_t)b[2] << 8 | b[3]; }
static inline void ur_bytes(uint32_t w, uint8_t *b)
{ for (unsigned i = 0; i < 4; i++) b[i] = (uint8_t)(w >> (8*i)); }
static inline uint32_t ur_crc32(const uint8_t *b, size_t n)
{
    uint32_t crc = UINT32_MAX;
    for (size_t i = 0; i < n; i++) {
        crc ^= b[i];
        for (unsigned bit = 0; bit < 8; bit++)
            crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320u : 0);
    }
    return ~crc;
}

/* Revision718 models the standard fixed response plus bounded sense-data segment.
 * UPIU data-segment length is not the4096-byte READ DMA length. Accept no segment or
 * the fixed20-byte sense container only; require its big-endian sense length <=18.
 * Nonzero EHS is refused because this parser does not locate data after extended headers.
 */
static uint32_t ur_response_failures(const uint8_t *r)
{
    uint32_t segment = (uint32_t)r[10] << 8 | r[11];
    uint32_t sense = (uint32_t)r[32] << 8 | r[33];
    return ((r[0] & 0x3f) != 0x21 ? 1u : 0u) |
           (r[2] != 2 ? 2u : 0u) |
           (r[3] != 0 ? 4u : 0u) |
           (r[6] != 0 ? 8u : 0u) |
           ((r[7] & 0x3f) != 0 ? 16u : 0u) |
           (segment != 0 && segment != 20 ? 32u : 0u) |
           (ur_be32(r+12) != 0 ? 64u : 0u) |
           (segment == 20 && sense > 18 ? 128u : 0u) |
           (r[8] != 0 ? 256u : 0u);
}

static bool ur_idle(const struct ur_io *io)
{
    static const uint16_t offsets[8] = { 0, 8, 0x30, 0x34, 0x50, 0x54, 0x58, 0x60 };
    uint32_t w[8];
    for (unsigned i = 0; i < 8; i++)
        if (!io->read_reg(io->context, offsets[i], &w[i])) return false;
    return w[0] != UINT32_MAX && (w[0] & 31) == 31 && w[1] == 0x200 &&
           w[2] != UINT32_MAX && (w[2] & 3) == 3 && w[3] != UINT32_MAX && (w[3] & 1) &&
           w[4] == (uint32_t)UR_LIST_PA && w[5] == 0 && w[6] == 0 && (w[7] & 1);
}

static bool ur_copy_from(const struct ur_io *io, enum ur_memory region, uint32_t *out, unsigned n)
{
    for (unsigned i = 0; i < n; i++)
        if (!io->read_word(io->context, region, i*4, &out[i])) return false;
    return true;
}

static bool ur_copy_back(const struct ur_io *io, enum ur_memory region, const uint32_t *data, unsigned n)
{
    for (unsigned i = 0; i < n; i++)
        if (!io->write_word(io->context, region, i*4, data[i])) return false;
    return true;
}

static bool ur_same(const struct ur_io *io, enum ur_memory region, const uint32_t *data, unsigned n)
{
    uint32_t w;
    for (unsigned i = 0; i < n; i++)
        if (!io->read_word(io->context, region, i*4, &w) || w != data[i]) return false;
    return true;
}

static bool ur_previous_read(const struct ur_workspace *saved)
{
    uint8_t c[32];
    for (unsigned i = 0; i < 8; i++) ur_bytes(saved->ucd[i], c + i*4);
    uint32_t blocks = (uint32_t)c[23] << 8 | c[24];
    uint32_t bytes = ur_be32(c+12);
    return (saved->table[0] & 0xfe000000u) == 0x14000000u &&
           (saved->table[2] & 0xff) == 0 && saved->table[4] == (uint32_t)UR_UCD_PA &&
           saved->table[5] == 0 && saved->table[6] == 0x00800080u &&
           (saved->table[7] >> 16) == 256 && (saved->table[7] & 0xffff) >= 1 &&
           (saved->table[7] & 0xffff) <= 33 && c[0] == 1 && c[1] == 0x40 &&
           c[2] == 2 && c[3] == 0 && c[16] == 0x28 && blocks != 0 &&
           bytes <= (8u << 20) && (uint64_t)blocks * UR_BLOCK_BYTES == bytes;
}

static bool ur_prepare(const struct ur_io *io, const struct ur_command *c)
{
    for (unsigned i = 0; i < UR_UCD_BYTES/4; i++)
        if (!io->write_word(io->context, UR_UCD, i*4, 0)) return false;
    for (unsigned i = 0; i < 8; i++)
        if (!io->write_word(io->context, UR_UCD, i*4, ur_le32(c->upiu+i*4))) return false;
    for (unsigned i = 0; i < 4; i++)
        if (!io->write_word(io->context, UR_UCD, 1024+i*4, c->prd[i])) return false;
    for (unsigned i = 0; i < UR_BLOCK_BYTES/4; i++)
        if (!io->write_word(io->context, UR_DATA, i*4, 0xa5a5a5a5u)) return false;
    return ur_copy_back(io, UR_TABLE, c->utrd, 8);
}

static bool ur_readback(const struct ur_io *io)
{
    struct ur_command back;
    uint32_t word;
    if (!ur_copy_from(io, UR_TABLE, back.utrd, 8)) return false;
    for (unsigned i = 0; i < 8; i++) {
        if (!io->read_word(io->context, UR_UCD, i*4, &word)) return false;
        ur_bytes(word, back.upiu+i*4);
    }
    for (unsigned i = 0; i < 4; i++)
        if (!io->read_word(io->context, UR_UCD, 1024+i*4, &back.prd[i])) return false;
    if (!ur_command_allowed(&back)) return false;
    for (unsigned i = 0; i < 512/4; i++)
        if (!io->read_word(io->context, UR_UCD, 512+i*4, &word) || word != 0) return false;
    for (unsigned i = 0; i < UR_BLOCK_BYTES/4; i++)
        if (!io->read_word(io->context, UR_DATA, i*4, &word) || word != 0xa5a5a5a5u) return false;
    return true;
}

static bool ur_restore(const struct ur_io *io, const struct ur_workspace *saved)
{
    if (!ur_idle(io)) return false;
    if (!ur_copy_back(io, UR_UCD, saved->ucd, UR_UCD_BYTES/4) ||
        !ur_copy_back(io, UR_DATA, saved->data, UR_BLOCK_BYTES/4) ||
        !ur_copy_back(io, UR_TABLE, saved->table, 8)) return false;
    io->barrier(io->context);
    return ur_same(io, UR_UCD, saved->ucd, UR_UCD_BYTES/4) &&
           ur_same(io, UR_DATA, saved->data, UR_BLOCK_BYTES/4) &&
           ur_same(io, UR_TABLE, saved->table, 8) && ur_idle(io);
}

/* Provider must establish exclusive lease/mapping/return guard separately. This engine never
 * turns FLAG observations into ownership. Workspace must be persistent, not kernel stack.
 * callbacks returning false do not model a hardware abort; integration must fail closed there.
 */
static void ur_run(const struct ur_io *io, struct ur_workspace *saved, struct ur_outcome *out,
                   uint32_t expected_crc32, uint32_t max_polls, uint64_t timeout_us)
{
    const struct ur_request request = { 2, 1, 1, UR_BLOCK_BYTES };
    struct ur_command command;
    uint64_t started;
    uint32_t dbr, word;
    bool completed = false;
    if (!out) return;
    for (size_t i = 0; i < sizeof(*out); i++) ((uint8_t *)out)[i] = 0;
    out->result = UR_REFUSED;
    if (!io || !saved || !max_polls || !timeout_us || !io->read_reg || !io->read_word ||
        !io->write_word || !io->claim || !io->finish || !io->stopped || !io->ring_once ||
        !io->barrier || !io->now_us || !ur_make_read(&request, &command)) return;
    if (io->stopped(io->context) || !ur_idle(io) ||
        !ur_copy_from(io, UR_TABLE, saved->table, 8) ||
        !ur_copy_from(io, UR_UCD, saved->ucd, UR_UCD_BYTES/4) ||
        !ur_previous_read(saved) || !ur_copy_from(io, UR_DATA, saved->data, UR_BLOCK_BYTES/4) ||
        !ur_same(io, UR_TABLE, saved->table, 8) ||
        !ur_same(io, UR_UCD, saved->ucd, UR_UCD_BYTES/4) ||
        !ur_idle(io)) return;
    if (!io->claim(io->context)) { out->result = UR_NO_LEASE; return; }
    out->claimed = true;
    out->result = UR_PREPARE_FAILED;
    if (!ur_prepare(io, &command)) goto clean;
    io->barrier(io->context);
    if (!ur_readback(io) || io->stopped(io->context)) goto clean;
    if (!ur_idle(io)) goto poison;
    io->barrier(io->context);
    started = io->now_us(io->context);
    out->issued = true; /* failure may mean the MMIO store was already posted */
    if (!io->ring_once(io->context)) goto poison;
    io->barrier(io->context);
    while (out->polls < max_polls && io->now_us(io->context) - started < timeout_us) {
        out->polls++;
        if (io->stopped(io->context) || !io->read_reg(io->context, 0x58, &dbr)) goto poison;
        if ((dbr & 1) == 0) {
            io->barrier(io->context);
            if (!io->read_word(io->context, UR_TABLE, 8, &word)) goto poison;
            out->ocs = word & 0xff;
            if (out->ocs > 0x0a && out->ocs != 0x0f) goto poison; /* reserved/untrusted OCS */
            if (out->ocs != 0x0f) { completed = true; break; }
        }
    }
    if (!completed || !ur_idle(io)) goto poison;
    out->result = UR_COMPLETED_ERROR;
    for (unsigned i = 0; i < UR_RESPONSE_WORDS; i++) {
        out->response_attempted |= 1u << i;
        if (!io->read_word(io->context, UR_UCD, 512+i*4, &word)) goto clean;
        ur_bytes(word, out->response+i*4);
        out->response_valid |= 1u << i;
    }
    if (out->ocs != 0) goto clean;
    out->response_failures = ur_response_failures(out->response);
    /* After known completion, sample the fixed scratch data even
     * if a response predicate failed. This never makes data_valid true or changes restore.
     */
    for (unsigned i = 0; i < UR_BLOCK_BYTES/4; i++) {
        if (!io->read_word(io->context, UR_DATA, i*4, &word)) goto clean;
        ur_bytes(word, out->block+i*4);
    }
    out->diagnostic_crc32 = ur_crc32(out->block, UR_BLOCK_BYTES);
    out->diagnostic_valid = true;
    if (out->response_failures != 0) goto clean;
    out->crc32 = out->diagnostic_crc32;
    out->result = out->crc32 == expected_crc32 ? UR_READ_OK : UR_REFERENCE_MISMATCH;
clean:
    if (!ur_restore(io, saved)) goto poison;
    if (!io->finish(io->context, true)) goto poison;
    out->restored = true;
    out->data_valid = out->result == UR_READ_OK;
    return;
poison:
    (void)io->finish(io->context, false);
    out->result = UR_UNCERTAIN_RESULT;
    out->data_valid = false;
}
#endif
