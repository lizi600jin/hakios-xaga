# Trial720 source snapshot

This immutable change set contains host codec/engine sources and their model dependencies,
runtime provider with its720 contract namespace,4 kernel integration files,4 loader files
and6 metadata files. It is NOT a standalone5220-file kernel tree or a production merge.
Complete original workspace: /home/lizi/IOS/hakios-mtk/logs/ufs-capacity-20261117/kernel-src.
Complete input hashes: metadata/kernel-inputs.sha256. Verify sha256sum-c SOURCE-MANIFEST.sha256.

Host candidate/ engine uses support/719-cookie ONLY for models. Runtime/ has a distinct
support/720-cookie matching the new loader. Do not substitute host headers into a kernel.
The runtime original test remains as historicalv2: its stop_at_ring merely timed out and
partial_response was actually a full nonzero-status reply. Independent corrected test is
runtime/test-ufs-capacity-platform-root.c; it actually injects STOP and uses status2.
Neither correction changes runtime provider source. Primary evidence/review reports explain
these limits; native object/image/runtime results are separate from this source snapshot.

From candidate/: build/run test-ufs-capacity-codec.c or test-ufs-capacity-engine.c using clang
-std=c11 -O1 -Wall -Wextra -Werror -fsanitize=address,undefined and fresh output paths.
From runtime/: build the corrected host test with -DUS_HOST_TEST -D_GNU_SOURCE and the same
sanitizer flags, then invoke one scenario per fresh process. No actual phone memory is used.
65955732 engine assertions include per-IO assertions, not that many independent cases.

One fixed RC10 data-IN8-byte command only; no RC16/retry/fallback or whole-disk registration.
Query acceptance,geometry decode and Android comparison are separate.0xffffffff is unresolved,
not no-medium. Tailcanary covers sampled locations, not all possible physicalDMA corruption.
No rawGPT/GUID/reference/image/prelog/native binaries are included. Full source plus model
snapshots preserve previous mistakes separately; no historical718/719 result is rewritten.
