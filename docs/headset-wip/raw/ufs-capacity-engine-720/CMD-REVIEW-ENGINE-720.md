# CMD-REVIEW-ENGINE-720

Host only. No platform/kernel/loader integration/build/image/device work.719 remains sealed.
Root prepared a new separate RC10 query engine; pure codec3f95d344 is unchanged after your
de902d3 review. No changes to719 files. Existing support helpers are exact719 bytes.

R=/home/lizi/IOS/hakios-mtk/logs/ufs-capacity-20261117

## Review and independent run

1. Verify MANIFEST-ENGINE720.json and codec manifest. Read DESIGN-ENGINE720.md. Preserve an
   exact private input snapshot. For running, create ANOTHER fresh private directory containing
   candidate/ and support/ and run-engine-checks-720-v2.py only; its host-checks must not exist.
   Root's reports/input snapshot must not be overwritten. Run the script in this fresh layout.
2. Expected model6554memory reads/49register reads/2908writes;9511 fault injections,1022tail
   corruptions,13/2/1022 partial observations.65955732checks includes per-IO assertions and is
   NOT that many independent cases. Both sanitizers native0/stderr0; ARM64 native0/7096bytes.
3. Audit fixed RC10 construction,80byte readback whitelist,expected8/DBC7. Imported ur_run
   address is discarded only to keep sealed helpers unchanged; it is NEVER called. Real
   controller/register callback set is unchanged, only a different fixed command is prepared.
4. Preserve single-ring ambiguity/timeout/STOP/reserved-OCS poison behavior. No restore of
   uncertain DMA. All clean paths must restore and verify whole table/UCD/DATA and idle.
   finish(true) is the last callback, so decoding afterward is CPU-only.
5. Response gate requires full13words and OCS0. Raw8 has attempted/valid/changed masks;
   all1022tail words must be sampled/unchanged. Matching prefill in either raw word deliberately
   yields DATA_UNCHANGED, a conservative false-negative possibility, not fabricated geometry.
   Tail canary is not proof against out-of-region writes or a dishonest/stale provider.
6. Query acceptance is separate from decode state and Android match: DIFFERENT512-byte geometry
   is visible and QUERY_OK; saturated or zero-block reports preserve raw8 and their separate
   decode status. Rejected/partial response/data/tail must never authorize decoded geometry.
7. Provider mappings/cached atomic service composition/720 cookie/loader guard are NOT present.
   Support contract still719 for HOST MODELS ONLY; do not reuse it as a720 runtime package.
   Old loader capacity helper must not be called. No registration, reset, RC16 or retry.

## Evidence

Initial engine-v1 compile rc1 due duplicated field is retained, with old source. Do not hide
it or claim it passed. engine-v2 is the corrected source tested before this freeze.
No719 suite needs rerun. Report any concrete blocker and pause dependent integration while
root fixes a NEW version; do not edit candidates yourself.

Archive exact source/support/frozen manifests/design/runner and root/private textual results
to NEW D/docs/headset-wip/raw/ufs-capacity-engine-720/. Preserve failure evidence as text/source,
no native binaries/raw disk/GUID/image/pre-log. GPUEB WIP remains unstaged; docs-only commit/push.
Return review conclusion and any remaining platform obligations. Do not start a kernel build.
