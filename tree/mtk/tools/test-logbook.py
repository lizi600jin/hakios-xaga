#!/usr/bin/env python3
"""Exercise the production loader HLOG writer against a memory-backed UFS stub."""

import importlib.util
from pathlib import Path
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
SOURCE = (HERE / '../loader/mtkloader.c').read_text()
start = SOURCE.index('#ifndef XNU_LOGBOOK_SLACK')
end = SOURCE.index('/*\n * A second record,', start)
writer = SOURCE[start:end]
start = SOURCE.index('static void __attribute__((unused)) probe_flash(uint64_t reason)\n{')
end = SOURCE.index('\n}', start) + 2
probe = SOURCE[start:end]

PREFIX = r'''
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#define BOARD_STORAGE_UFS 1
#define SYSTEM_PART "xnusys"
#define BASE (65536u + 4096u)
#define PROBE_LOG_MAX 128u
struct span { const volatile char *p; uint64_t n; };
static uint32_t gpt_block_size = 512;
static unsigned calls, header_writes, fail_call;
static uint8_t disk[(4u << 20) + 8192];
static char probe_log[PROBE_LOG_MAX] = "short power-down probe";
static uint32_t probe_log_len = sizeof("short power-down probe") - 1;
static int gpt_find(const char *name, uint64_t *lba, uint64_t *blocks) {
    (void)name; *lba = 8; *blocks = ((8u << 20) + 65536u) / 512u; return 1;
}
static int ufs_write(uint64_t lba, uint32_t blocks, const void *data) {
    uint64_t at = lba * 512u, bytes = blocks * 512u;
    calls++;
    if (calls == fail_call) return 0;
    assert(at >= BASE && (at % 4096u) == 0 && (bytes % 4096u) == 0);
    assert(at - BASE + bytes <= sizeof(disk));
    if (at == BASE) header_writes++;
    memcpy(disk + at - BASE, data, bytes);
    return 1;
}
static const char *board_storage_what(void) { return "injected failure"; }
static uint64_t dbg_ticks(void) { return 1234; }
static void out(const char *s) { (void)s; }
static void out_dec(uint64_t n) { (void)n; }
static void out_hex(uint64_t n) { (void)n; }
static void dcache_clean_range(uint64_t a, uint64_t n) { (void)a; (void)n; }
'''

SUFFIX = r'''
static void reset(void) {
    memset(disk, 0xa5, sizeof(disk));
    calls = header_writes = fail_call = 0;
    logbook_kernel_saved = 0;
}
static void verify(const uint8_t *expected, size_t n, uint64_t reason) {
    const struct logbook_hdr *h = (const void *)disk;
    uint64_t sum = 0;
    assert(h->magic == XNU_LOGBOOK_MAGIC && h->version == 1 && h->len == n);
    assert(h->reason == reason && h->where == BASE + 4096 && h->ticks == 1234);
    assert(memcmp(disk + 4096, expected, n) == 0);
    for (size_t i = 0; i < n; i++) sum += expected[i];
    assert(h->sum == sum);
    for (size_t i = sizeof(*h); i < 4096; i++) assert(disk[i] == 0);
    for (size_t i = n; i < ((n + 4095u) & ~4095u); i++) assert(disk[4096 + i] == 0);
    assert(header_writes == 1);
}
int main(int argc, char **argv) {
    assert(argc == 2);
    const size_t size = (4u << 20) + 8193u;
    uint8_t *data = malloc(size), *expected = malloc(size), *saved = malloc(sizeof(disk));
    assert(data && expected && saved);
    for (size_t i = 0; i < size; i++) data[i] = (uint8_t)((i * 31u + i / 251u) & 255u);
    const size_t sizes[] = {1, 4095, 4096, 4097, 471129, 1310720, size};
    for (size_t j = 0; j < sizeof(sizes) / sizeof(sizes[0]); j++) {
        reset();
        size_t n = sizes[j], kept = n > XNU_LOGBOOK_MAX ? XNU_LOGBOOK_MAX : n;
        assert(save_log_to_flash((uintptr_t)data, n, 0, 1, 2, 3));
        verify(data + n - kept, kept, 0);
        const struct logbook_hdr *h = (const void *)disk;
        assert(h->code == 1 && h->elr == 2 && h->far == 3);
    }
    /* Real head/ring sizes and awkward ring positions exercise cross-span blocks. */
    const size_t head = 256u << 10, ring = 1u << 20;
    const size_t positions[] = {0, 1, 4095, 4096, 54321, ring - 1};
    for (size_t j = 0; j < sizeof(positions) / sizeof(positions[0]); j++) {
        size_t pos = positions[j];
        struct span sp[] = {{(void *)data, head}, {(void *)(data + head + pos), ring - pos},
                            {(void *)(data + head), pos}};
        memcpy(expected, data, head);
        memcpy(expected + head, data + head + pos, ring - pos);
        memcpy(expected + head + ring - pos, data + head, pos);
        reset();
        assert(save_log_spans_to_flash(sp, 3, XNU_LOGBOOK_RESCUE, 0, 0, 0));
        verify(expected, head + ring, XNU_LOGBOOK_RESCUE);
    }
    /* Truncation starts in the middle of a span, while empty spans remain legal. */
    reset();
    struct span split[] = {{NULL, 0}, {(void *)data, 4097},
                           {(void *)(data + 4097), size - 4097}, {NULL, 0}};
    assert(save_log_spans_to_flash(split, 4, 0, 0, 0, 0));
    verify(data + size - XNU_LOGBOOK_MAX, XNU_LOGBOOK_MAX, 0);
    reset();
    assert(!save_log_to_flash(0, 1, 0, 0, 0, 0));
    assert(!save_log_to_flash((uintptr_t)data, 0, 0, 0, 0, 0));
    assert(calls == 0);
    for (unsigned failure = 1; failure <= 3; failure++) {
        reset(); fail_call = failure;
        assert(!save_log_to_flash((uintptr_t)data, 8193, 0, 0, 0, 0));
        assert(header_writes == 0); /* failures in either body write or header commit */
    }
    reset();
    assert(save_log_to_flash((uintptr_t)data, 471129, 0, 0, 0, 0));
    memcpy(saved, disk, sizeof(disk));
    logbook_kernel_saved = 1;
    unsigned before = calls;
    probe_flash(38);
    assert(calls == before && memcmp(saved, disk, sizeof(disk)) == 0);
    logbook_kernel_saved = 0;
    probe_flash(38);
    assert(calls > before && ((struct logbook_hdr *)disk)->reason == 38);
    reset();
    assert(save_log_spans_to_flash((struct span[]){{(void *)expected, head + ring}},
                                   1, XNU_LOGBOOK_RESCUE, 0, 0, 0));
    FILE *f = fopen(argv[1], "wb"); assert(f);
    assert(fwrite(disk, 1, 4096 + head + ring, f) == 4096 + head + ring);
    assert(fclose(f) == 0);
    free(data); free(expected); free(saved);
    puts("production C writer: contiguous/ring/truncation/failure/probe protection passed");
    return 0;
}
'''


def main():
    spec = importlib.util.spec_from_file_location('logbook', HERE / 'xaga-logbook.py')
    decoder = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(decoder)
    with tempfile.TemporaryDirectory(prefix='hakios-logbook-test-') as name:
        temp = Path(name)
        (temp / 'test.c').write_text(PREFIX + writer + probe + SUFFIX)
        subprocess.run(['clang', '-std=c11', '-O1', '-g', '-fsanitize=address,undefined',
                        '-fno-omit-frame-pointer', str(temp / 'test.c'), '-o', str(temp / 'test')],
                       check=True)
        subprocess.run([str(temp / 'test'), str(temp / 'record.bin')], check=True)
        data = (temp / 'record.bin').read_bytes()
        fields, body = decoder.decode(data)
        assert fields[3] == 0x52534355 and len(body) == (256 + 1024) * 1024
        corrupt = bytearray(data)
        corrupt[4096] ^= 1
        wrong_version = bytearray(data)
        wrong_version[4] = 2
        for bad in (data[:64], data[:-1], bytes(corrupt), bytes(wrong_version)):
            try:
                decoder.decode(bad)
            except ValueError:
                pass
            else:
                raise AssertionError('decoder accepted an invalid record')
        print('host decoder: rescue/short header/short body/checksum/version passed')


if __name__ == '__main__':
    main()
