# Round 17 evidence: matched normal firmware entry, trial 692

692 reuses the exact 691 driver/kernel/payload. Only mali_witness=1 to 0 and the trial ID change; assorder=1 and all other arguments remain identical. The actual normal reset entry is 0x010018ed, with no witness installation or readback. All 82 pre-AUTO GPU stores match 691 after only TRANSTAB PA normalization.

The captured window has 264 Mali rows and complete version/fault/return evidence. No selected JOB/version/fault divergence is observed. This does not prove identical internal execution or absence of PROT accesses: the three PROT sections remain mapped and writable. Disabling witness also cancels code overwrite, vector replacement and full-section flush.

The exact runner requires a current-trial version result and a completed failure/success return tail for normal firmware. The user's pre-extension runner, the incremental guard patch and six predicate cases are preserved. An early sentinel message or loop-end alone cannot pass. Raw/image remain local; curated text, fingerprints and independent stock verification are included.

Decision/review/plan documents are preserved as pre-execution records; their review-only status and the decision's 4K-history/parameter-typo statements are contextualized by the final report. Current runtime status is trial-692.json and STATE-MINI. Follow-up reasoning is separately reviewed, not a newly run experiment.

Offline reparse with the local raw and 691 baseline TSV:

```sh
python3 analyze.py 692 --input-dir /home/lizi/IOS/work-xaga/codex/out/round-8-support --baseline 691
```
