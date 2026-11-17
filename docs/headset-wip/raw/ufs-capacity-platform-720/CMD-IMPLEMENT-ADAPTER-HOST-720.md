# CMD-IMPLEMENT-ADAPTER-HOST-720

Implement NEW isolated host-reviewable platform candidate only. No kernel tree/build,
loader change, package, device query or flash. No production edits or719/core17 edits.
You are implementing this adapter; do not call your own review independent. Codex will
independently review the frozen source and outputs before authorizing integration.

R=/home/lizi/IOS/hakios-mtk/logs/ufs-capacity-20261117
A=R/adapter-candidate (must not exist before preparation)
Old=/home/lizi/IOS/hakios-mtk/logs/ufs-service-20261117

## Files and dependencies

Write A/hakios.mt6895_ufs_capacity.c. Preserve a layout that can later be copied to bsd/dev:
A/ufs_capacity/{ufs_capacity_codec.h,ufs_capacity_engine.h} exact reviewed bytes3f95d344/b33c9ae0;
A/support/ufs_read_engine.h exact81aeee09;
A/support/ufs_service_lease.h exact71905432b2c;
A/support/ufs_read_contract.h = exact719 contract except UR_TRIAL719u->720u. These are runtime
candidate dependencies, separate from frozen R/support which stays719 for host models.
Preserve relative includes and avoid dependence on old bsd/dev headers/include order.

Extract provider logic from Old/candidate/hakios.mt6895_ufs_service.c, with no cdev/VFS/worker,
dynamic allocation, mutex or thread. New synchronous init performs at most ONE RC10 query and
then remains clean cachedIDLE/sharedCLAIMED until quiesce. It never registers a block device.

## Exact provider/lifetime requirements

- Entry init/quiesce hakios_mt6895_ufs_capacity_init/_quiesce. quiesce ONLY us_stop(&lease),
  no mutex/wait/MMIO/logger/printf. Never assume other CPUs have stopped.
- Cached lease static once; NEVER reset phase. Repeated init after a non-NEW phase does no IO.
- mtk_ufs_capacity missing/0 returns before mapping; only1 accepted. Old service/read/probe
  nonzero or invalid new value rejects before mapping. #ifdef HAKIOS_UFS build conflict.
- Same719 outsideVM+pmap endpoint checks, exact scratch/table mappings/WCOMB and register
  read whitelist. No new address/controller stores; only ring UTRLDBR=1.
- Fresh720 wire lease0 required. Same fully verified clean baseline before us_open. Open
  publishes CLAIMED once; us_admit exactIDLE->ACTIVE, then uc_run(max500000,timeout1000000).
- io.claim verifies cachedACTIVE+valid720/sharedCLAIMED, never publishes CLAIMED again.
  write_word only exactACTIVE; ring exactACTIVE AND sharedCLAIMED. Same barriers as719.
  finish(true) ONLY us_mark_drained; false us_poison. No physical return/reset code.
- After uc_run, require restored and cleanDRAINED before us_copy_done toIDLE. If an admitted
  query lacks certified restore/drain, poison. STOP race must never be normalized to IDLE.
  Safe query result/decode/mismatch is not a reason to reset the lease or reissue IO.
- Keep held1 until quiesce for all clean completions. Uncertain state remains poison, loader
  must later refuse. No query retry or use of old loader ufs_capacity function.
- Full4096pre-fill/canary/restore are engine responsibilities; don't bypass them or old engine.

## Fixed logging contract for the independent parser

Use prefix `mt6895-ufs-capacity: ` and the following literal shapes. Normal init logging only,
never in quiesce. Guard/raw words are observations, don't fabricate unavailable values.

```
single RC10 lun2 data_bytes8 scratch_bytes4096; guarded handoff required
guard magic=%#x version=%u trial=%u state=%u
opened baseline_verified=1 control=%#x shared=%u
result=%u claimed=%u issued=%u memory_restored=%u query_valid=%u polls=%u
ocs=%#x response_valid=%#x attempted=%#x failures=%#x
response_words <thirteen08x words>
raw valid=%#x attempted=%#x changed=%#x <two08x words only if valid==3>
tail complete=%u ok=%u checked_words=%u first_bad=%#x
decode=%u last_lba=%#x block_bytes=%u blocks=%llu capacity_bytes=%llu android_match=%u
decode=%u; geometry unavailable
final control=%#x shared=%u idle_close_deferred=%u
```

Only print ocs/masks if a response was actually attempted; full raw response words only for
valid1fff. Always print raw masks/tail for a collected outcome; print raw two words only if
valid3. Geometry detailed line only query_valid AND decodeOK; otherwise unavailable line
with actual decode state. query_valid must be suppressed if post-engine drain/copy_done
fails. Success finalcontrol2/shared1/deferred1. QueryOK5 does not imply geometry decodeOK or
Android match; saturated2/zero-block3 remain distinct observations. No CRC/SHA claim for query.
For invalid/baseline/mapping failure print a clear refusal and no fabricated outcome.

## Host tests and ARM64 syntax check

Create US_HOST_TEST shim and test-ufs-capacity-platform.c in A (never copied to kernel).
Test ACTUAL adapter C against synthetic mapped memory/soft DMA. Cover absent/0/invalid/max,
all three competing modes, pre-stopped/repeated init, VM overlap, stale cookie, invalid
controller flags, normal reference geometry, different512 geometry, saturated and zero-block
reports, unchanged/partial DATA, tail overrun, rejected response, and STOP after ring. No real
hardware addresses should be dereferenced. Emulator ring validates exact capacity whitelist.
Check mapping/command counts and final cached/shared phase; quiesce changes clean held1->2
without MMIO reads/writes, poisoned->3 never permitted. Repeat init cannot issue another command.

Run ASan+UBSan and MSan with -Wall-Wextra-Werror/native rc+stderr recorded. Real XNU-header
ARM64 object compile may reuse audited194-element719 argv with ONLY source/output changed
into A/fresh-host-checks; audit outputs before execution. No full kernel build. Preserve
failures and source versions; do not overwrite earlier logs and count them as passed.

Freeze candidate inputs with full sizes/hash and concise DESIGN/host records. Report to Codex
for independent review. Archive docs source/text only under raw/ufs-capacity-platform-720;
don't upload binaries/disk/GUID/pre-log or stage GPUEB WIP. No production push or stash/reset/
checkout/clean/gc/prune. Wait for a separate integration/build command after root review.
