# CMD-DELIVER-UFS-SERVICE-719

Host only.719 passed root independent device and recovery review. Keep stock; no phone
queries, experimental boot, build/package, production edit/push or old evidence overwrite.

R=/home/lizi/IOS/hakios-mtk/logs/ufs-service-20261117
D=/home/lizi/IOS/repo/hakios-mt6895

1. Read RESULT-TRIAL719.md, independent-device-review-719.json and
   independent-recovery-review-719.json. Compare with your device record; archive these plus
   review-recovery-719.py and thisCMD in device-ufs-service-719/review or its root as appropriate.
   Do not rerun already passed tests or query the phone again absent a concrete concern.
2. Append a correction to device719 README preserving the original: your sentence
   "数据内容一致" overstates equal CRC32. Correct to "三次4096字节数据的CRC32均为0x28a2836c；
   未逐字节或SHA256验证内容相等". Also label response word indices explicitly ZERO-BASED9
   (and2/8/9); do not infer why request3 uses segment0. Statement of CRC limits elsewhere does
   not erase the stronger sentence, so make this local correction explicit.
3. Copy R/delivery-source-719 ORIGINAL BYTES to a NEW
   D/docs/headset-wip/raw/final-ufs-service-source-719/source/.32 files total;29 manifest entries
   =17 candidate/test+4 kernel-integration+4 loader+4 metadata. Verify all29 before/after copy;
   run sha256sum-c if helpful. After staging, verify all29 git blob bytes/hash, not just worktree.
   This is a change set, NOT a standalone5214-file tree. Complete inputs remain at R/kernel-src
   and metadata/kernel-inputs.sha256. No production merge/push claim or image upload.
4. Include host review/source reproduction entry and root's review scripts/reports as textual
   evidence links. Create docs/headset-wip/UFS-SERVICE-DELIVERY-20261117.md with concrete719
   behavior, three VFS/READ results, held lease vs final release, write/EOF no-command evidence,
   source/reproduction paths, artifact hashes, stock recovery, and limits. Describe kernel VFS,
   CRC32 agreement and fixed one-block/global3 budget precisely. No userland/full-disk claim.
   Keep old718 delivery intact and link it as predecessor, not a rewritten conclusion.
5. Add a concise handoff note (removed in the 2026-11-26 cleanup) with source/evidence entry, hardware/current stock,
   outstanding userland/variable-LBA/whole-disk goals, exclusivity prerequisite, raw response
   observations and remaining CPU/GPU issues. Root will choose the next host-only step.
6. Stage exact new docs paths only; GPUEB WIP9cdcb0da stays unstaged. Raw GPT/GUID/reference/
   pre-log and images remain local. Do not change ignore rules, reset/stash/checkout/clean/gc/
   prune or rewrite historical716/717/718. Commit/push docs and verify remote equality.

Report commit,29/29 staged blob verification, copy/reproduction limits and any discrepancy.
