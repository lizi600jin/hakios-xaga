# Root preview of in-progress adapter, not frozen review

Source was still changing during this read. Do not treat this as a final hash-bound review.
Please resolve these before freezing; no kernel or phone work is authorized.

1. After us_open, cached phase is IDLE. Current draft calls uc_run directly; capacity_claim
   requires ACTIVE, so the request cannot be claimed. Add the exact us_admit CAS before uc_run;
   STOP/admission failure must return without issuing a command. This is NOT a publish bug.
2. Do not require full control equality DRAINED for post-engine cleanup. STOP at clean DRAINED
   sets STOP/RELEASED and may already publish RESTORED. A later us_poison would violate the
   clean release invariant. Use restored+maskedphaseDRAINED, then handle STOP by suppressing
   outcome delivery and returning without poisoning or hardware IO. copy_done CAS failure due
   STOP is similarly a clean no-delivery path. Test STOP from finish at DRAINED and prove no
   later POISONED publication after the clean2. Do not rely on all other CPUs being stopped.
3. Mock table placement is wrong in the current healthy()/install_completion: ml_io_map of
   page0x51266000 then +0x400 means actual slot0 is emu.table+0x400. The model plants/reads
   descriptor at emu.table+0. Plant/update/validate descriptor at the actual slot address.
   The fake model currently fails baseline before us_open; searching publish is premature.
4. Run each independent scenario in a fresh process, as719 did. reset_all only clears emulator
   bytes, not cached lease/platform globals. After STOP/success the lease must remain non-NEW;
   never reset it for a later scenario. Repeated-init/quiesce tests should stay in one scenario.
5. g_mode as int with-1 meaning absent also makes UINT32_MAX appear absent. Use an explicit
   presence bool + uint32 value so invalidmax really exercises parse/reject, not absent path.
6. The emulator must assert exact80-byte RC10 whitelist when it observes the actual doorbell
   store. Count one command, preserve all original table/UCD/DATA for restoration checks, and
   record mapping/counter checks. Production callback path must also be exercised: current
   US_HOST_TEST replaces the read_reg switch with a separate hook. Prefer same whitelist/load
   path plus test-only completion at barrier as719, or prove the production branch separately.

The current comment that the engine calls us_* is inaccurate: it calls only ur_io callbacks;
the provider is what implements service lease transitions. Preserve this layering in docs.

Keep rejected runs/source snapshots and record native rc/stderr; do not overwrite them as
passed. Root will inspect the finished provider/tests and real ARM64 object independently.
