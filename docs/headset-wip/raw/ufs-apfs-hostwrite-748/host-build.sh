#!/bin/sh
# Build the host rehearsal against the kernel's own apfs_write.c.
#
# Two things make this need a copy rather than a plain -I: apfs_lib.h wants <time.h> (a kernel
# header does not include it), and Ubuntu's headers define ARCH_X86_64, which sends libdeflate
# looking for an x86 assembly directory this tree does not carry. Both are handled in a temporary
# copy; the kernel sources are untouched.
set -e
APFS=${1:?path to bsd/miscfs/apfs}
OUT=${2:-/tmp/taw}
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

cp -a "$APFS" "$WORK/apfs"
cat > "$WORK/shim.h" <<'HDR'
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>
#include <time.h>
#undef ARCH_X86_64
#undef ARCH_X86_32
HDR
# libdeflate's x86 paths are absent from this tree; the portable C ones are used instead.
sed -i 's|^#  include "x86/decompress_impl.h"|/* host build */|' \
    "$WORK/apfs/libdeflate/lib/deflate_decompress.c"
sed -i 's|^#  include "x86/adler32_impl.h"|/* host build */|' \
    "$WORK/apfs/libdeflate/lib/adler32.c"

cat > "$WORK/stub.c" <<'STUB'
#include <stdio.h>
#include <stddef.h>
/* The kernel's crypto library; only a sealed volume reaches it. */
int SHA256(const unsigned char *d, size_t n, unsigned char *md)
{ (void)d; (void)n; (void)md; fprintf(stderr, "SHA256 called\n"); return 0; }
STUB

clang -std=gnu11 -O2 -Wall -Wextra -include "$WORK/shim.h" -I"$WORK/apfs" \
    "$(dirname "$0")/test-apfs-write-host.c" \
    "$WORK/apfs/apfs_write.c" "$WORK/apfs/apfs_lib.c" \
    "$WORK/apfs/apfs_deflate.c" "$WORK/apfs/apfs_zbitmap.c" \
    "$WORK/stub.c" -lz -o "$OUT"
echo "built $OUT"
