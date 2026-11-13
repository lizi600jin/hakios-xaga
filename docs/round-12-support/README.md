# Round 12 supporting evidence

Trial 684 adds only the four zero writes controlled by mali_hwissues=1 to the semantic baseline of 681. Reset/cache/coherency options stay off. The firmware witness remained at its sentinel, and stock Android was restored and verified.

- trial-681..684-build.json and trial-681..684.json: arguments, hashes, device run and stock restoration metadata.
- trial-681..684-arg-delivery.json and loader-strings.txt: exact token counts (3 each) in the actual loader strings.
- trial-681..684-mali.txt, mcu.txt, writes.tsv and parse.json: complete-window parsing, register observations and stores captured before AUTO.
- write-diff-681-684.json: exactly four inserted stores after only AS0 TRANSTAB PA normalization; raw differences are included too.
- write-diff-678-681.json, write-diff-681-682.json, write-diff-681-683.json: supplemental comparisons for the prior completed experiments.
- hakios.mali610.trials-681-683.c: the exact prior source snapshot; current published tree source matches trial 684.
- hwissues-only.patch: the entire new experiment patch relative to that snapshot.
- kernel/payload build logs and publish-source-check.json: build gate and source/kernel identity evidence.
- stock-final-check.json: an independent post-run adb check after the runner restored stock.

Counts are GPU stores captured before AUTO, excluding the AUTO request and failure-tail cleanup. Trials 681 and 684 use different kernel binaries; the report does not claim same-binary A/B.

Full raw logbooks and boot images are local artifacts. Reparse a saved complete window with:

```sh
python3 analyze.py 684 --input-dir /home/lizi/IOS/work-xaga/codex/out/round-8-support --baseline 681
```

The parser reads the entire 4,198,400-byte reserved window, requires only the current trial marker, and validates the store record count. It performs no device I/O. Raw logbook SHA256 is recorded in each parse/run JSON.
