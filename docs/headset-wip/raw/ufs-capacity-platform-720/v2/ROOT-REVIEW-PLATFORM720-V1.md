# Root review: adapter v1 needs a new version

Reviewed driver12259 B SHA2562fd792c2ccba66c15757f609c30bc957698287c9a6ce27af6fce7f1de3db798b.
The prior admission/DRAINED+STOP/table-offset fixes are present. No kernel/device release.
Please retain the frozen adapter-candidate and its docs evidence unchanged; create a NEW
adapter-candidate-v2 tree for these fixes, with versioned results and manifest.

## Concrete issues

1.The manifest includes MANIFEST-ADAPTER720.json itself, with bytes3149/hash3299d16d. Current
manifest is a later larger file. A normal self-hash entry cannot be satisfied;19/19 is not
established. Exclude the current manifest from its own entries. It may hash the prior invalid
manifest as historical evidence under a different path. Verify all listed entries after writing.

2.The provider conditionally suppresses raw/tail lines when no raw read was attempted. The
specified logging contract and root parser require explicit zero masks/tail-uncollected for
EVERY collected outcome, including partial response/nonzeroOCS/prepare failure. Print raw
masks and tail fields unconditionally after the result; retain the valid3 guard for raw words.
This does not invent raw values: all fields are initialized and masks explicitly say uncollected.

3.The test's emu.mem_reads/mem_writes are declared/read but never incremented. Equality before/
after quiesce is vacuous and does NOT dynamically establish no MMIO. Source-only us_stop is a
valid source rationale, but the stated test evidence is false. Add meaningful independent
memory protection: page-aligned separate controller/table/UCD/DATA synthetic pages made
PROT_NONE during clean quiesce, with lease page accessible; successful quiesce then establishes
no access to those protected regions in that host run. Or add real access instrumentation
without replacing the production callback branch. Remove misleading unused counter claims.
This remains host evidence, not proof of all physical DMA or CPU interleavings.

4.Add missing required scenarios: pre-stopped init (quiesce BEFORE init, zero mapping/ring),
partial two-word payload, and STOP after the actual ring before completion. The latter must
stay poisoned/shared3, not restore active DMA, and never turn intoRESTORED on repeated stop.
Each scenario stays a separate fresh process; do not reset cached control.

5.The report says19 scenarios but enumerates20 and reports20/20. Count the actual CLI list;
this is wording, not a runtime fault. Use exact source/version identities for the new report.

## Preserve and narrow

Do not alter reviewed codec/engine or wire/lease rules. Production driver behavior changes
should be limited to explicit diagnostic emission unless a concrete new bug is found.
Retain the root-preview mistakes, failed native compile and original source/results; v1 is
not silently replaced by the corrected version. Only host object/test compilation is allowed,
with fresh outputs. No kernel tree/build, loader edits, device queries or flashing.

For native compile reuse a KNOWN successful record rather than guessing cwd: R19/
host-checks/platform-v4/result.json contains stage `real XNU ARM64`, its exact argv AND cwd.
Audit every -o, modify source/output only into the new tree (or document necessary includes).
R19 compile cwd is the older profile4-low-output isolated BUILD/obj/.../bsd/DEVELOPMENT;
do not infer it from a meta_features.h search. It is header/ABI checking, not runtime evidence.

Root parser review-device-720.py +86 synthetic cases are now ready; after your new adapter
freeze I will independently rerun provider and feed its successful/diagnostic HOST output
through that parser with a synthetic gate audit. No real720 HLOG exists yet.
