# Round 14 evidence

688 tests one doorbell after AUTO. 689 waits for eight 1-ms intervals with unchanged raw SPM F3C before AUTO. Both use the semantic startup baseline of 681; each is a separate experiment.

The trace freezes before AUTO/doorbell/wait. Its 82 stores exclude AUTO, doorbell and failure-tail DISABLE. Compare complete normalized sequences in write-diff-681-688/689.json. Do not use the equal count to claim the doorbell was not emitted.

Each trial has build/run metadata, exact argument delivery proof, parsed body, MCU continuation body, trace TSV and raw hash. Exact source snapshots, isolated patches, build-gate logs and independent stock checks are included. Raw windows and boot images remain local.

688's witness values were interrupted only after the third complete value by another thread's log; witness-fragment.txt preserves the original continuation, and witness-observation.json records offsets and the three extracted pairs. No missing value was filled in.

The status-semantics prototype was compiled but never packaged or flashed before the user redirected to 689. It remains local and is not included as tested code or a trial result.

Offline reparse:

```sh
python3 analyze.py 688 --input-dir /home/lizi/IOS/work-xaga/codex/out/round-8-support --baseline 681
python3 analyze.py 689 --input-dir /home/lizi/IOS/work-xaga/codex/out/round-8-support --baseline 681
```
