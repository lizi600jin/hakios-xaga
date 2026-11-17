# CMD-REVIEW-PARSER-719

Host only, no phone queries, no build/package/source edits. Thank you for pack review00630b6.

R=/home/lizi/IOS/hakios-mtk/logs/ufs-service-20261117

Read review-device-719.py and test-review-device-719.py. Copy both plus arguments719.json
to a NEW private directory, run the synthetic test there (108 cases), keep its result there.
Review especially: exact3 requests and real VFS reports, held sharedCLAIMED between requests,
per-request memory restore vs service lease release, full13-word/OCS0 response evaluation,
CRC32-only language, write-open/EOF no-command counters, and the unique actual loader audit
at HLOG EOF. Diagnostic-only/missing/partial evidence must never become PASS. A clean early
failure may prove lease return without proving3 reads. Image/stock checks are separate.

The existing host output format was checked too: host-checks/platform-parser-format-719.json.
That is fake VFS output plus an explicitly synthetic return audit, NOT hardware evidence.

If no blocker, archive parser/scripts/108-case report and concise review in a NEW
docs/headset-wip/raw/ufs-service-parser-719/ directory, commit and push docs only. Do not
include raw GPT/GUID/images/pre-log or stage GPUEB WIP. Do not edit the frozen57 inputs.
No rerun of old service/engine/race suites is needed absent a concrete concern.

One wording correction for pack review: first-return guard and final-boot-write-before-guard
ordering already existed in718.719 changes capture of the gate's accepted double-read lease
snapshot and appends its formatted audit as the second span. Please append this clarification
to the pack README rather than describing all three as newly introduced719 behaviors.

Report blockers or PASS. Device remains stock until a separate ONE DEVICE719 command.
