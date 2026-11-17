# DELIVER-UFS-718: final host archive after stock recovery

Phone is verified stock f745b1c6-ee43-4abc-8ed2-06ee9629bf27. No device query/extra experiment.
R=/home/lizi/IOS/hakios-mtk/logs/ufs-response-20261117. Preserve all716/717/718 source/results.

1. Supplement device-ufs-read-718 with RESULT-TRIAL718.md,
   independent-device-review-718.json, independent-recovery-review-718.json and this CMD.
   Verify the supplied root reports match your observations. They record six PASS labels plus
   diagnostic response PASS, CRC32 agreement only, and automatic full stock recovery.
2. Correct the two overclaims in your latest narrative, preserving the original text plus
   appended correction:716 recorded no response words, so its exact rejected field is unknown.
  717 proves bit5/data segment20 for717;718 passes its revised bounded parser. Do not state that
  716 was proved bit5 or that all716/717 data/transfer issues were exclusively this one cause.
  717/718 CRC32 agreement is not mathematical byte equality or proof that all prior data was good.
3. Copy R/delivery-source-718 unchanged into a NEW
   D/docs/headset-wip/raw/final-ufs-source-718/source/. It contains19 source files and4 frozen
   metadata files,23 manifest entries, plus its own SOURCE-MANIFEST.json/.sha256/README.
   Before copy and after copy verify23/23 bytes+hashes, then verify committed git blobs for all
  23 listed paths. Preserve relative layout. No need to repeat unchanged tests: contents are
   identical; readme supplies reproduction commands and identifies the full-kernel dependencies.
4. Add a concise entry D/docs/headset-wip/UFS-DELIVERY-20261117.md: final READ_OK/CRC scope,
   response-layout correction,32,513 engine checks/58 parser cases, make2 vs judge0, three-round
   verdict table (716 unknown field,717 bit5,718 pass), exact source entry, host/pack/device links,
   current stock proof and remaining persistent-block/filesystem work. Source snapshot alone
   is not a complete standalone5210-file kernel tree; full input manifest and original workspace
   are identified. No production merge/push is claimed.
5. Include R/HANDOFF-718-CURRENT.md after Codex appends final state, and final small independent
   archive reviews if available. Avoid stale in-flight status in the delivery entry. Commit/push
   docs only and verify remote. Return commit,23 blob identities and final status.

Do not add image binaries/raw GPT/GUID/pre-log; only the two already approved runtime HLOG
binaries belong in device archive. No ignore-rule change, GPUEB WIP staging, production push,
old source/BUILD/image overwrite, stash/reset/checkout/clean/gc/prune. No device work.
