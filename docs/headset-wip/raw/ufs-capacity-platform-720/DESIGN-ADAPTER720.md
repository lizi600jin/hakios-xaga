# 720 capacity adapter (host-reviewable candidate)

Isolated host candidate. Not wired into any kernel tree, not built as a kernel, not packaged.
The phone stays stock and no device command exists here.

## Layering

`uc_run` from the frozen engine calls only `ur_io` callbacks. The **provider** (this file)
implements those callbacks and is the only place that performs service-lease transitions.
The engine never touches `us_*`; an earlier comment claiming otherwise was wrong and is fixed.

## Lifetime

- `lease` is a single static record initialised once. Its phase is never reset.
- `init` refuses before any controller access unless the boot argument is present, equals 1,
  and no competing mode is set. It then requires the 719 outside-VM/pmap endpoint checks.
- `clean_baseline` must fully verify the inherited descriptor before `us_open`.
- `us_open` publishes `CLAIMED` exactly once, then `us_admit` performs the exact
  `IDLE -> ACTIVE` CAS. A refused admission returns without issuing a command.
- `io.claim` only *verifies* cached `ACTIVE` plus a valid shared `CLAIMED`; it never publishes.
- `write_word` requires exact `ACTIVE`; the ring requires exact `ACTIVE` **and** shared
  `CLAIMED`. One controller store total: `UTRLDBR = 1`.
- `finish(true)` performs `us_mark_drained` only, then a barrier; `finish(false)` poisons.
- `quiesce` calls `us_stop` only: no mutex, wait, MMIO, logger or printf, and it does not
  assume other CPUs have stopped.

## Post-engine cleanup

Reportability requires the engine's restore certification **and** a phase of `DRAINED`.

A STOP that lands on an already-clean `DRAINED` has published `RESTORED`, so this provider
must not poison: it suppresses delivery and returns. `RELEASED` is checked first precisely so
a cleanly released lease is never turned into `POISONED`. A `copy_done` CAS failure is the
same clean no-delivery path — no cleanup, no poison, no hardware access.

## Logging

Fixed shapes under the `mt6895-ufs-capacity: ` prefix, printed only from `init`. Values that
were not observed are suppressed rather than fabricated: `ocs`/masks only when a response was
attempted, full 13 response words only for `0x1fff`, and the two raw words only when
`valid == 3`. Geometry is printed only for `query_valid && decode == UC_DECODE_OK`, otherwise
an `unavailable` line carrying the real decode state. `query_valid` is cleared if post-engine
drain or `copy_done` fails. No CRC or SHA claim is made for the query.

## Tests

Nineteen scenarios, one fresh process each, because the cached lease is a process global and
must never be reset to serve a later scenario. The emulator completes the command at the
`dsb sy` barrier — the same single synchronisation point 719 used — so the production
`read_reg` whitelist/load path executes unmodified; there is no test-only register branch.
On seeing the doorbell the emulator asserts the exact 80-byte RC10 whitelist, counts one
command, and preserves the original table/UCD/DATA for the restore checks.
