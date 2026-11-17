# REVIEW-CORE-719: frozen host core only

R=/home/lizi/IOS/hakios-mtk/logs/ufs-service-20261117. Read DESIGN-SERVICE719.md and review
sealed-core-719-v1/MANIFEST.json (8 exact files). Do not edit candidate or sealed input.
Codex is independently implementing platform/loader; those are NOT part of this review yet.

Focus on the actual atomic control/flags and service composition, not merely the design text:
NEW/OPENING/IDLE/ACTIVE/DRAINED, STOP/POISON/RELEASED; no reset; one CLAIMED publisher across
calls; exact IDLE admission; ACTIVE-to-DRAINED only after ur_run's complete restore/readback;
caller-private CPU copy before DRAINED-to-IDLE. In this scoped composition engine.restored is
per-request memory evidence; shared remains CLAIMED, not a released outer lease.

Verify that after a true finish callback the unchanged engine performs no hardware operation;
thus STOP at DRAINED can release while CPU-only copy completes. STOP at ACTIVE/OPENING poisons,
late stores cannot allow loader IO. Check repeated/reentrant STOP, the one RELEASED-flag
publisher, publication false-after-store and absence of late RESTORED after a valid poison path.
Do not assume that all other CPUs stopped. Clean baseline/exclusive handoff remain provider
preconditions, not proved by the pure cached-state header.

Independently copy8 files to your own fresh private directory and compile/run test-ufs-service.c
under ASan+UBSan and MSan, expected34121 checks/reads5530/regs49/writes2908. Also compile/run
test-ufs-service-race.c with -pthread under both sanitizers:2500 finite schedules, including
forced real-thread ACTIVE/DRAINED STOP rendezvous. Exact allowed/denied distribution can vary;
check invariants, not scheduler-count equality. Freestanding ARM64 codec is service-arm64.c.
Flags from run-core-checks-719.py; current race source is v2, replacing its prior1500-schedule
variant. Older report's matching old source is preserved in host-checks/core-v1/source/.
Do not rerun unchanged718 standalone tests without a concern.

Review invalid requests no IO/workspace mutation, overlapping admission during result copy,
copy failure/STOP not authorizing user delivery, all per-IO failure positions, repeated calls
with the shared lease held, and clean closure versus uncertain DMA. Return concrete blockers.

Archive NEW D/docs/headset-wip/raw/ufs-service-core-719/ with exact reviewed source8, manifest,
your independent report, source analysis and objections. No device/build/image/production
activity or old overwrite, no GPUEB WIP staging, no forbidden git operations. Do not implement
your suggested fixes in our candidates; report them for Codex. Await platform/loader release.
