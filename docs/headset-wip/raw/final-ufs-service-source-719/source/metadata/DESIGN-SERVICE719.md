# Service719: serialized fixed-block reads across a held loader lease

This new revision starts from718's accepted READ10/LUN2/LBA1/4096 path.718 and its artifacts
remain immutable. Command/range/geometry, data prefill/readback, response bounds and CRC stay
fixed. This is an incremental service for one known block, not a whole-disk block provider.

## Cached software state and proof obligations

One atomic uint32 in ordinary cached kernel memory holds a low-byte phase plus STOP/POISON/
RELEASED flags. No atomic instruction targets Device or shared-mapped DRAM. No init resets it.

| Phase | Meaning | STOP action |
|---|---|---|
| NEW | no lease taken or hardware write | prevent open; shared UNUSED can remain |
| OPENING | claiming shared lease after clean baseline preflight | poison, deny loader |
| IDLE | no caller owns result workspace; last hardware/memory proof clean | release RESTORED |
| ACTIVE | one admitted request may prepare/ring/poll/restore | poison, deny loader |
| DRAINED | request restored and verified; exclusive CPU result copy remains | release RESTORED |

IDLE is established at open only after unchanged clean baseline is independently verified,
or by a successful DRAINED-to-IDLE CAS after result copy. DRAINED is established only by the
engine's successful final restore/readback/idle checks, before its finish callback returns.
After that true callback the engine performs no hardware access. Copying the cached result
to a caller-owned kernel buffer performs no UFS/shared state operation. A second request may
enter only from exact IDLE without flags, so it cannot overwrite an in-progress result copy.

Open: NEW->OPENING CAS, publish shared CLAIMED once, then OPENING->IDLE CAS. Request admission:
IDLE->ACTIVE CAS. The per-request engine's claim callback checks this existing held lease;
it does not publish CLAIMED again. Successful memory restore does ACTIVE->DRAINED CAS instead
of releasing the outer lease. Copy success does DRAINED->IDLE CAS. Shared remains CLAIMED
across calls. A failed CAS due to STOP never resets or retries the lease.

STOP sets its flag atomically. ACTIVE/OPENING STOP sets POISON and publishes shared POISONED;
late stores may occur, but no clean transition succeeds and the loader stays denied. For
IDLE/DRAINED STOP, no future admission or hardware operation is possible; one RELEASED-flag
winner publishes RESTORED from the cached clean proof. Other closers do not republish it.
If the publication fails, poison. NEW STOP does not touch an uninitialized mapping.
Safety does not assume that all other CPUs have stopped. Quiesce takes no mutex, waits for
no thread, performs no controller access and emits no kernel log/printf.

## Integration and caller buffer

The platform copies the engine block into a caller-owned4096-byte kernel buffer before
releasing result ownership. User uiomove happens only after that, outside the service workspace.
No public result buffer aliases the shared DMA bounce or static engine outcome. A request
refused before the engine's finish callback is conservatively poisoned after admission;
there is no automatic retry. Non-success with a proven clean restore may close cleanly but
does not deliver bytes. Invalid requests before admission do no hardware IO.

Initial integration will expose a root read-only character snapshot of logical LBA1. Its
visible size is4096; reads outside that virtual range are refused/EOF as appropriate. The
physical command always remains the one verified block. Write/open-for-write/ioctl/mmap do
not issue UFS commands. A three-call delayed kernel exercise can use the same service API;
it does not establish userland execution or a persistent whole-disk device.

Known native HAKIOS_UFS must be compile-time incompatible with this service. Actual loader
handoff and absence of any other UFS user remain integration preconditions; excluding known
native drivers is evidence about those paths, not proof of global hardware exclusion.

## Return evidence

Use the strict loader gate's actual double-read accepted snapshot. On successful return only,
append a bounded ASCII audit as a second span in the existing save_log_spans_to_flash call.
The helper keeps the tail, so the audit survives capacity truncation. Gate failure still halts
before storage. No kernel logger in quiesce, no extra HLOG record, no extra storage address,
no hand-written xnusys operation. One save invocation is not one WRITE10: body/header write
counts can depend on length and span boundaries. We do not claim identical physical write count.

## Required host model evidence before build/device

Cover one open/one CLAIMED publication; several separated requests; overlapping admission;
caller buffer copies; STOP at every phase and at publication/copy boundaries; poison never
releasing; publish-false-after-store; repeated stop with one release publisher; failed restore;
fresh OCS/response/data on each command; uncollected response/data never valid; and no register
or bounce writes after an idle STOP. Preserve engine718 semantics under the scoped adapter.
No physical experiment or whole-disk service is claimed by this design document.
