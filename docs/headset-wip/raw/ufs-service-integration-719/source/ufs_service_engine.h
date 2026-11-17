/* Service composition of the unchanged718 transaction engine. The platform's claim checks
 * an already held shared lease; finish(true) must perform us_mark_drained, not release it.
 */
#ifndef MT6895_UFS_SERVICE_ENGINE_H
#define MT6895_UFS_SERVICE_ENGINE_H
#include "ufs_read_engine.h"
#include "ufs_service_lease.h"

enum us_read_result { US_READ_OK, US_BAD_REQUEST, US_NOT_READY, US_BUSY,
                      US_STOPPED, US_COMMAND_ERROR, US_COPY_ERROR, US_UNCERTAIN };
typedef bool (*us_deliver_fn)(void *, const uint8_t *, size_t);

/* deliver copies to a distinct caller-owned cached kernel buffer. It must not touch UFS or
 * the service workspace. Never hand out engine.block/shared-DMA pointers to another caller.
 * A nonzero return never authorizes user copy-out even if a callback partially copied bytes.
 */
static enum us_read_result
us_read_fixed(struct us_lease *s, const struct ur_io *io, struct ur_workspace *workspace,
              struct ur_outcome *out, const struct ur_request *request,
              us_deliver_fn deliver, void *destination, size_t bytes,
              uint32_t expected_crc, uint32_t polls, uint64_t timeout_us)
{
    struct ur_command check;
    uint32_t phase;
    bool copied;
    if (!s || !io || !workspace || !out || !deliver || !destination ||
        bytes != UR_BLOCK_BYTES || !polls || !timeout_us || !ur_make_read(request, &check) ||
        !io->read_reg || !io->read_word || !io->write_word || !io->claim || !io->finish ||
        !io->stopped || !io->ring_once || !io->barrier || !io->now_us) return US_BAD_REQUEST;
    if (!us_admit(s)) {
        phase = us_control(s);
        return phase & US_STOP_BIT ? US_STOPPED :
               (phase & US_PHASE_MASK) == US_NEW ? US_NOT_READY : US_BUSY;
    }
    ur_run(io, workspace, out, expected_crc, polls, timeout_us);
    phase = us_control(s);
    if (phase & US_POISON_BIT) return US_UNCERTAIN;
    /* Refused preflight/claim did not call finish and cannot certify the current baseline. */
    if (!out->restored || (phase & US_PHASE_MASK) != US_DRAINED) {
        us_poison(s);
        return US_UNCERTAIN;
    }
    if (phase & US_STOP_BIT) return US_STOPPED; /* DRAINED STOP is already safe for loader */
    if (out->result != UR_READ_OK || !out->data_valid) {
        return us_copy_done(s) ? US_COMMAND_ERROR : US_STOPPED;
    }
    copied = deliver(destination, out->block, UR_BLOCK_BYTES);
    if (!us_copy_done(s)) return US_STOPPED;
    return copied ? US_READ_OK : US_COPY_ERROR;
}

#endif
