# CMD-REVIEW-CODEC-720

Host-only independent review. No kernel/loader integration/build/image or device operation.
719 remains sealed, phone stock. No old source/BUILD/whitelist changes.

R=/home/lizi/IOS/hakios-mtk/logs/ufs-capacity-20261117

1. Verify MANIFEST-CODEC720.json entries before copying to a NEW private host-review directory.
   Read DESIGN-CAPACITY720.md and REVIEW-SURVEY-720.md. Copy candidate/ plus runner to private
   directory and run run-codec-checks-720.py there. Expected23295 checks under ASan+UBSan/MSan,
   all native0/stderr0; separate freestanding ARM64 native0. This is NOT a real XNU-header build.
2. Review full80-byte whitelist, exact LUN2/tag0/CDB0x25/PMI0 and expected8/PRDTDBC7. Only memory
   construction/comparison, no callback/ring/MMIO or queue writes. Check all80bytes exhaustively
   mutated and invalid LUN/null leave outputs untouched. Different opcode/WRITE/RC16 cannot pass.
3. Geometry: twoBE32 fields, widened count+1/product, nonzero block bytes observed separately
   from Android comparison.512/520/different capacity may parse without authorizing xaga READ.
   0xffffffff is unresolved/saturated, NEVER no-medium/fallback/autoRC16. Non-OK output remains
   untouched. Android-reference mismatch is separate from query/transport completion (not implemented).
4. The eight-byte synthetic decoder does not prove response/OCS, DMA, restored queue, ownership,
   device geometry or block device. Owned scratch/tail/restore must be defined and fault-tested
   in a new engine candidate before any kernel or hardware work. Do not call old loader helper.
5. Archive exact codec3 source files, DESIGN/runner/manifest, your private result and concise
   review in D/docs/headset-wip/raw/ufs-capacity-codec-720/. Preserve root host result as separate
   evidence, not overwritten. No need rerun any719 test. Commit/push docs only, GPUEB WIP unstaged.

Report blockers with exact bytes/field/hash or PASS. Do not proceed to kernel build or device
activity. Root will define the query-engine candidate as the next step after this review.
