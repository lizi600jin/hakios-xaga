# 720 query engine host proposal

No platform/kernel/loader/image/device integration. The frozen codec3f95d344 is unchanged.
Support headers/model are byte-exact sealed719 dependencies, including its719 cookie solely
for host-model lease behavior. They are not a prepared720 runtime contract. Future integration
requires a new numbered contract and separate reviewed provider; never run these as-is on phone.

The new uc_run constructs only RC10LUN2/tag0/8bytes/DBC7 through the separately reviewed codec.
It reuses idle/copy/prepare/response/restore helpers; old ur_run READ10 entry is never called.
No controller callbacks or register whitelist are extended. Single ring, ambiguous false
submission/timeout/reservedOCS/STOP remain poisoned; no restore while DMA is unconfirmed.

Before claim it reads and compares table/UCD/DATA baseline plus idle; inherited predecessor
must remain a READ. Prepare writes/readbacks all4096 DATA canary bytes and the UCD/table,
with the NEW80-byte query whitelist. Advertised transfer remains8, not4096.

After known completion it collects13response words, checks OCS0 and the718 bounded response
predicate, then samples the two payload words and all1022tail words. Tail mismatch/partial
sampling or an unchanged prefill word cannot produce accepted query data. Raw attempted/
valid/changed masks distinguish partial/stale-looking observations. Requiring both words
different from0xa5a5a5a5 is deliberately conservative: a legitimate matching word will remain
raw diagnostic data, not accepted geometry. This can cause false negatives and does not prove
global DMA coherence or provider honesty. Tail canary is not proof against writes outside it.

Every clean path restores ALL original table/UCD4096DATA, verifies bytes and idle, and calls
finish(true) as the last callback. Geometry decoding occurs CPU-only after confirmed restore.
Query transport/data acceptance is independent of decode success or Android comparison:
different parsed capacity/blocksize512 is visible; saturated/zero-block reports preserve raw
bytes and separate decode state. There is no fallback/RC16/retry or automatic block registration.

Tests inject every baseline memory-read/register-read/committed-write failure and every tail
word corruption, plus no/partial payload DMA, protocol errors, timeout/ambiguous ring/STOP.
The provider's real cached service lease, mappings and return publication are NOT yet tested
by this single-request host model and must not be inferred from it.

Current root engine-v2 model:6554 memory-read,49 register-read and2908 committed-write fault
positions (9511 injections),1022 separate tail-word corruptions,13partial response positions,
2partial raw-data positions and1022partial tail positions. Printed65955732checks includes
per-callback assertions that no IO occurs after finish; it is not65955732 independent cases.
ASan+UBSan/MSan each native0/stderr0; freestanding ARM64 native0. Initial engine-v1 did not
compile because of a duplicated outcome field; source and rc1 diagnostic are preserved in
host-checks/engine-v1/, never counted as a pass. No kernel was built.
