# CMD-HOST-SURVEY-CAPACITY-720

Host-only next step after sealed7199d6488c. Phone stays stock; no device queries, builds,
packaging, production edits, or experimental boot. Root is preparing a pure fixed-command
codec candidate; this task is independent source/provenance review, not permission to wire IO.

P=/home/lizi/IOS/hakios-mtk
R19=P/logs/ufs-service-20261117
R20=P/logs/ufs-capacity-20261117

Why:719 proved three fixed4096-byte LBA1 kernel VFS reads plus lifetime lease release, but
not a real-capacity block provider. Next requirement is an in-kernel geometry observation.
We choose one bounded READ CAPACITY(10) design before variable-LBA or full block registration.
This intentionally adds a NEW read-only opcode in a NEW candidate; never alter719's whitelist.

Please inspect local PRIMARY source and report these questions to a new MD:

1. R19/private loader ufs.h defines ufs_capacity(), but root rg found no call site, and active
   LK-list init sets ufs_lbs=UFS_BLOCK/known1. Trace exact preprocessed active path and caller
   evidence; do not treat dead capacity code or comments as runtime geometry verification.
2. Existing Android reference from R16/android-reference and archived Android-UFS-reference716
   already established LUN2,logical4096,capacity127959826432. Recompute blocks31240192,
   lastLBA31240191=0x1dcbfff from source evidence only; if this hex is wrong, correct it.
   No fresh phone reference or GUID/raw GPT copy/upload.

   Root arithmetic correction before any candidate: the hex above was a transcription error.
   127959826432/4096=31240192 blocks; last31240191 is **0x01dcafff**, NOT0x01dcbfff.
   Preserve this correction in the survey. No code or hardware used the mistyped value.
3. Compare loader's fixed0x25/expected8-byte/PRDT placement and actual completion parser with
   Linux drivers/scsi/sd.c read_capacity_10 at local/tmp/mt6895-mainline. Explicitly identify
   what is reusable wire-layout evidence versus unsafe init/retry/fallback code we must not call.
4. Confirm two big-endian32-bit response values (lastLBA,blocklength), count=last+1, and the
   0xffffffff sentinel. Do not silently fall back to4096 or autoissue RC16/reset/retry.
5. Identify concrete integration risks for a future8-byte DMA payload in the existing4096-byte
   scratch region: expected transfer length/PRDT8, response/OCS gate, prefill/readback, retained
   full scratch restore and lease poisoning. No MMIO/DMA action or controller extension now.

Keep718/719 sealed. Known redwood native drivers may be read as source, never executed or used
for addresses. Mainline project fork is not proof of official upstream support. Do not use
the old loader helper as an executable recipe. Root will separately freeze codec/tests for review.

Archive findings to D/docs/headset-wip/raw/ufs-capacity-survey-720/ with thisCMD, precise local
file/line/hash references and remaining unknowns; commit/push docs only. GPUEB WIP stays unstaged.
Do not spend effort rerunning prior suites. Report concrete objections and wait for new candidate.
