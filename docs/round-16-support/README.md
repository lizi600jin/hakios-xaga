# Round 16 evidence: AS0 ordering, trial 691

Only mali_assorder=1 is newly active relative to semantic baseline 681. The default-off flag changes AS0 configuration groups to CFG/TAB/ATTR, preserving their values, IRQ clears, barriers, readbacks and the single UPDATE. The five step rows and full 82-store sequence verify activation.

After normalizing only AS0 TRANSTAB allocator PAs, all 82 stores match baseline[0:70] + baseline[76:79] + baseline[70:76] + baseline[79:82]. UPDATE still reports raw 0x10000; witness remains sentinel, JOB IRQ is zero and the version word is untouched. The ordering change did not restore observed stores.

The runner waits up to 150 seconds after adbd returns and requires current-trial witness readback. log_complete is true, with spin500000, start-loop end, trace, witness verdict and failure-tail return present. Raw windows and images remain local, not included here. Independent stock restoration proof and exact tested source are included.

The trace freezes before AUTO and failure-tail DISABLE; those writes are excluded from 82. MCU_STATUS=1 is recorded as raw state, not used to prove boot. Earlier ID691 statussem metadata in round15 describes an unflashed package, separate from this actual run.

The user's later skipprot correction is recorded in STATE-MINI, but no skipprot code, build or phone trial was performed. It must preserve the loop-tail offset increment; use the allocation-condition guard and do not copy the superseded continue draft.

Offline reparse with local raw and baseline TSV:

```sh
python3 analyze.py 691 --input-dir /home/lizi/IOS/work-xaga/codex/out/round-8-support --baseline 681
```
