# UFS718 exact source snapshot

19 source files:12 candidate/host-model files,3 private-loader files and4 kernel integration
files.23 manifest entries include frozen metadata. Copies are byte-identical to the reviewed
inputs; verify with sha256sum -c SOURCE-MANIFEST.sha256 from this directory.

For standalone host models, build candidate/test-ufs-read-engine.c, test-ufs-read-contract.c,
test-ufs-atomic-lease.c and test-ufs-loader-guard.c with clang -std=c11 -O1 -g -Wall -Wextra
-Werror -fno-omit-frame-pointer -fsanitize=address,undefined, each to its own fresh output.
MSan uses -fsanitize=memory -fPIE -pie instead. Frozen source reports engine32513,
contract983, atomic40 and guard23. This archive does not claim another test rerun.

The complete kernel build requires the5210-file frozen input tree whose hashes are included
in metadata/kernel-inputs.sha256, XNU toolchain and project build scripts. This source snapshot
alone is not a complete standalone kernel tree. Original workspace R/kernel-src preserves it.
Loader depends on the reviewed39-file private mirror; only its changed files are copied here.
Native argv, strict package checks, kernel/image hashes and hardware evidence live in the
separate718 host/pack/device records. No raw GPT, disk GUID, phone serial or image binary belongs
in this source snapshot. Production tree and preexisting GPUEB WIP were not committed.

At snapshot time DEVICE718 is in progress; no hardware success is implied by these sources.

Final runtime update:718 accepted one fixed READ10 and CRC32 agreement with clean restore; stock recovered automatically. See separate RESULT-TRIAL718.md and original runtime evidence. This does not make the snapshot a complete standalone kernel or persistent block-device implementation.
