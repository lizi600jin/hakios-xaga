# Trial719 source snapshot

29 manifest entries:17 candidate source/test files,4 kernel integration files,4 loader
files,4 frozen metadata files. This is a change set, not a standalone5214-file kernel tree.
Full original inputs: /home/lizi/IOS/hakios-mtk/logs/ufs-service-20261117/kernel-src;
complete per-file hashes: metadata/kernel-inputs.sha256. No production merge/push implied.

Verify from this directory: `sha256sum -c SOURCE-MANIFEST.sha256`.
The fixed command engine is byte-identical to718. New code adds a cached service lease,
root0400 character snapshot and max3 delayed kernel VFS calls. Actual hardware results
belong in the separate device719 result, not in this immutable source manifest.

Host-only core reproduction from candidate/ (requires clang and pthread):

```sh
clang -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined test-ufs-service.c -o /tmp/service719-check
/tmp/service719-check
clang -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -pthread test-ufs-service-race.c -o /tmp/service719-race
/tmp/service719-race
```

Use fresh output names if occupied. Race distributions vary with scheduling; phase and
publication invariants must pass. Header-only host shims/fake VFS are not real XNU VFS or
hardware evidence. The platform test additionally needs the private Android4096-byte LBA1
reference as an explicit input; it is intentionally not included in this source archive.

No disk/GPT bytes,GUID,image binaries or pre-log are included. Source retains the unchanged
loader HLOG writer and required gate. One HLOG save call can issue multiple WRITE10 commands.
