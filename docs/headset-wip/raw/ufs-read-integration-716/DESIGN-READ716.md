# Minimal UFS READ716: proposed lease and integration contract

2026-11-17. User requested continued progress after completed715. This is design/candidate
work;716 has NOT been built or flashed. Source712/713/714/715 and production remain sealed.

## Verified basis

715 sampled UTRLBA0x51266400, UTRLBAU0, doorbell0, RSR1, HCE1, HCS0x10f; two reads equal.
The active715 preprocessed loader uses LK's list, but its UCDs remain at0x60101000 onward.
UCD_SIZE is1664(0x680), UFS_BUF_OFFSET0xe000, data buffer0x6010e000.
The list and these regions are below XNU's4GiB physical base. XNU pmap's pa_valid tests the
boot-argument managed window, not Android's iomem; these addresses are not XNU VM pages.
Mapping/readability and current descriptor bytes still need runtime validation.

Do NOT copy loader ufs_init: its compiled path includes SMC, clock and UIC activity despite
stale read-only comments. Do NOT move the transfer-list base or stop/restart it as a recipe.
The loader already has ufs_ready1 after loading the system. On return its HLOG writer calls
ufs_write/ufs_cmd_once and overwrites slot0/UCD. There is presently no kernel ownership lease.

## Candidate scope

One synchronous READ(10), LUN2, logical LBA1, one4096-byte block; fixed known scratch DMA
destination. DSH is collecting a read-only Android geometry/GPT reference, with explicit
parent-disk/LUN identification, to establish the block size and comparison oracle.
No query, device reset, clock/power/SMC/UIC, queue rebase, UTRLCLR or IS write is included.
Controller write surface would be only UTRLDBR=1 after exact descriptor readback validation.
The target is the primary GPT header, never a writable command or RPMB/well-known LUN.

Before any descriptor write: validate a fresh loader lease, live managed-memory exclusion,
exact list address, HCE/ready/RSR flags, no sampled doorbell bits, consistent slot0 UTRD and
read-direction UPIU. Map only the fixed list page and known scratch region; do not follow
arbitrary descriptor pointers. Decode the previous READ's byte/block ratio and require4096.
Any mismatch returns a diagnostic without borrowing or ringing. Snapshot raw metadata for
audit. A no-DMA refusal is a valid trial outcome, not grounds to widen the whitelist.

## Shared lease: guard BEFORE the loader's HLOG writer

Use16 bytes of the loader's unused private list at0x60100000 (active list stays at0x51266400).
The new private loader initializes magic/version/trial/state AFTER its initial UFS reads and
before XNU entry. The new kernel must validate that exact header before touching descriptors.
This requires modifying a new frozen loader mirror and isolated kernel; production is untouched.

- UNUSED: kernel has not borrowed anything; loader may use storage.
- CLAIMED: kernel may own a command/DMA; loader must refuse storage and Android handover.
- RESTORED: all owned work has completed, original memory/controller checks passed; loader may use.
- POISONED: timeout, uncertain completion, failed restore or concurrent stop; never becomes clean.

The loader gate must be the FIRST UFS-related action in xnu_returned, before save_log,
gpt_find or any logbook read/write. Invalid/missing/CLAIMED/POISONED record: print a bounded
diagnostic and halt for manual fastboot; do not write HLOG, warm-start Android or guess reset.
No watchdog fallback is added. This deliberate failure path may lose the current HLOG.
User fallback is available, but a fresh DEVICE command is still required after host review.

## Ordering and return/panic races

The pure candidate ur_lease_step is NOT an atomic shared-memory implementation.
Do not use LL/SC atomics on a Device mapping. Proposed kernel integration uses an atomic
software phase in ordinary XNU-managed cached memory and aligned32-bit stores to the
Normal-noncacheable shared record, with audited barriers/mapping attributes.

Software phase carries a STOP bit. Claim is CAS UNUSED->CLAIMED with STOP clear, then shared
CLAIMED is published before the first descriptor store. Publish the shared pointer before that
CAS. Quiesce uses an atomic fetch-or STOP; if it observes CLAIMED, it publishes POISONED.
It never waits, allocates, prints or touches controller registers. Other CPUs may not all stop.

After actual complete restore, CAS CLAIMED->RESTORED succeeds only if STOP is absent. Only
that winner may publish shared RESTORED. If quiesce raced first, the CAS fails and shared state
cannot become clean. A late shared CLAIMED write can only cause a conservative halt, never
permission for loader IO. If restoration completes first, no new claim is allowed in this run.
This must be exercised by deterministic interleaving tests, not merely sequential enum tests.

## Request and restoration

Save original slot0 UTRD32 bytes, UCD1664 bytes and first4096 bytes of data scratch before
claim. After claim, prepare only the whitelisted READ and sentinel OCS, response and data.
Read back the complete allowed UTRD/UPIU/PRD tuple before ringing once. Poll with both elapsed
time and iteration bounds. Do not treat a previously latched IS completion bit as a new result.

Completion needs the owned doorbell bit clear and a fresh non-invalid OCS plus valid response
transaction/tag/LUN/status/length. On uncertainty, do not overwrite a possibly active DMA
descriptor or restore old data: poison and deny loader storage. On completed success or known
completed error, verify the entire list doorbell is clear, list identity/RSR unchanged, restore
all saved bytes and read them back, then and only then mark restored. Return captured data only
after validating it against the independent GPT reference. No kernel block device is exposed yet.

## Implemented so far

candidate/ufs_read_contract.h: pure lease transitions, managed-range exclusion and exact80-byte
READ tuple constructor/whitelist. No MMIO or shared-memory integration. Test suite passed983
checks under ASan/UBSan and MSan, plus freestanding ARM64 codec compilation. These are model
checks, not evidence of controller ownership, atomicity or storage IO.

Next: finish the bounded engine and serialization/guard models; independently review DSH's
memory/geometry evidence; wire a fresh loader+kernel ONLY after those checks. No716 DEVICE
command exists yet. Keep715 stock and its old successful evidence unchanged.
