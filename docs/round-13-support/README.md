# Round 13 evidence

Trials 685–687: one STACK_PWRON experiment, complete read-only MCU/SPM/PDC observations, and a same-kernel ENABLE→AUTO comparison.

- trial-*-build.json / trial-*.json: arguments, file hashes, serial, logbook integrity and stock restoration metadata.
- trial-*-arg-delivery.json / loader-strings.txt: exact loader token counts, 3 each.
- trial-*-mali.txt / writes.tsv / parse.json: complete-window parsed logs and stores captured before MCU control requests.
- trial-686/687-observations.json: two complete 64-word snapshots, five raw SPM words, all 32 F3C bits, ten SC words/enables and PDC groups.
- trial-686/687-mcu-window.tsv / F3C-bits.tsv / PDC-SC.tsv: complete tables.
- trial-686/687-mcu-rawblocks.txt: original window fragments, including any interleaved process messages. Normalized mcu.txt retains all numeric values.
- write-diff-*.json: raw and AS0-root-PA-normalized complete sequence differences.
- source JSON / build logs / isolated patches: primary source, identity and build-gate evidence.
- hakios.mali610.trial-685.c and hakios.mali610.trial-686.c: exact tested source snapshots; 686/687 share the latter.

The trace freezes before ENABLE/AUTO. Its 82-store count in 687 does not include the extra ENABLE request; the two mcuseq logs and readbacks are retained separately.

All analysis is offline. Full raw windows and boot images remain local. Reproduce in this order (the second parser preserves complete multi-line MCU windows even when another thread logs between printed words):

```sh
python3 analyze.py 687 --input-dir /home/lizi/IOS/work-xaga/codex/out/round-8-support --baseline 686
python3 analyze-observation.py 687 --input-dir /home/lizi/IOS/work-xaga/codex/out/round-8-support
```

See the report for boundaries: TRANS was sampled only before/after in 685, delay(1) means one microsecond, SC enables are not domain status bits, and a larger power-status mask is not a health ranking.
