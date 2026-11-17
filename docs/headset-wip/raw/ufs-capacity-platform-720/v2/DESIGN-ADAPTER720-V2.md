# 720 capacity adapter v2

Supersedes adapter-candidate (v1, driver 2fd792c2…), which is retained unchanged. v1 is not
silently replaced: its source, results and the invalid self-referencing manifest stay on disk.

## What changed from v1

**Diagnostic emission (the only production-code change).** The `raw` and `tail` lines are now
emitted for every collected outcome, including partial responses, nonzero OCS and prepare
failures. The masks state what was actually collected, so an all-zero mask is an explicit
"uncollected" and never a fabricated value. Only the two raw words themselves remain behind
the `valid == 3` guard.

No wire, lease or engine rule changed. The codec (`3f95d344`) and engine (`b33c9ae0`) are the
same frozen bytes; support is `81aeee09` / `05432b2c` / `f88c8cfc`.

## Test changes

**Real memory protection instead of a vacuous counter.** v1 declared `mem_reads`/`mem_writes`
and compared them before and after quiesce, but never incremented them, so the comparison
proved nothing. v2 drops those counters. Regions now live in one anonymous mapping laid out as
separate pages: registers, table, and a fifteen-page contiguous scratch (lease at +0x0000,
UCD at +0x1000, DATA at +0xE000 — the layout the adapter's offset arithmetic requires).

For the clean-close check every page except the lease page is set to `PROT_NONE` before
quiesce. A successful quiesce then establishes, in that host run, that no register, table, UCD
or DATA byte was touched. This is host evidence only: it does not prove anything about
physical DMA or other CPU interleavings.

**Three added scenarios**, each still a fresh process with the cached control never reset:

- `pestop` — quiesce before init: STOP bit set, no mapping, no ring, phase stays NEW.
- `partial_data` — only the first raw word written: `changed == 1`, `DATA_UNCHANGED`, no geometry.
- `stop_at_ring` — STOP after the real ring, before completion: uncertain and `shared == 3`,
  never restored, and a repeated stop cannot turn it into RESTORED.

`partial_response` now drives a real status bit (`byte 7 = 0x01`) so the engine's own
predicate produces the failure, rather than the harness faking unreadable words.

## Scenario count

Twenty-four, taken directly from the runner list.

## Manifest

The manifest excludes itself. The v1 manifest is kept as historical evidence under
`host-checks/MANIFEST-V1-INVALID.json`; it is not listed as a current entry.
