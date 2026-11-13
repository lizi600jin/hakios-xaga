# Round 15 evidence

Accepted result: trial 690 attempt 2, 281 Mali rows, complete witness and all three failure-tail status probes. The trace freezes before AUTO and failure-tail DISABLE; its 82 stores match 681 after normalizing only AS0 TRANSTAB LO/HI allocator PAs.

Attempt 1 has no tail, witness read or trace and is invalid as a hardware result. Its immutable text/metadata archive is included; its historical runner outcome does not override the completeness verdict. Historical `next: 691` entries describe a superseded plan. The files named `trial-691-not-run*` refer only to the earlier statussem loader package, never flashed. Round 16 later uses ID 691 for AS0 ordering with a different source/image.

`run-once.py` is exactly the runner used for accepted attempt 2. `run-once.user-completionfix.py` preserves the user's initial completion fix. The shared docs/round-8-support/run-once.py is the later round16 version that also requires current-trial witness readback. No prior script or raw evidence was overwritten to imply it was used earlier.

The same boot image, arguments and five build fingerprints were used for both attempts. After backup and zero-window verification, the retry captured a fresh complete window; no fragments were joined. Raw windows and images stay local in /home/lizi/IOS/work-xaga/codex/out/round-8-support and round-15-support/trial-690-attempt-1.

Status 0 before AUTO and 1 throughout this failed-stop window support latch-like behavior on the observed path, not a proof of permanent hardware latching or a running core. The capture guard fixes result acceptance; the cause of attempt 1 truncation is still unproven. See logbook-save-source.json.

Offline reparse with the local raw window and baseline TSV:

```sh
python3 analyze.py 690 --input-dir /home/lizi/IOS/work-xaga/codex/out/round-8-support --baseline 681
```
