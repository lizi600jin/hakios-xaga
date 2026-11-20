/*
 * Trial739. A directory listed the way a program would list it: every entry, in the order the
 * tree stores them, with the type a caller needs to tell a file from a directory.
 *
 * Two directories are used. One holds 44 entries that all live in a single leaf, which pins the
 * enumeration order against what the volume actually stores -- that order is by a hash of the
 * name, so it is not alphabetical and cannot be guessed. The other holds three.
 *
 * The blocks are the ones this phone returned; nothing here is synthesised.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include "ufs_apfs_volume.h"

#define BLOCK 4096u

static const char *refdir = "reference/walk738";
static int failures;

static void note(bool ok, const char *what)
{
    printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) failures++;
}

static uint8_t *load(uint32_t rel)
{
    char path[512];
    FILE *f;
    uint8_t *p = malloc(BLOCK);

    if (!p) return NULL;
    snprintf(path, sizeof(path), "%s/%u.bin", refdir, rel);
    f = fopen(path, "rb");
    if (!f) { free(p); return NULL; }
    if (fread(p, 1, BLOCK, f) != BLOCK) { fclose(f); free(p); return NULL; }
    fclose(f);
    return p;
}

int main(int argc, char **argv)
{
    static const uint8_t *tree[8];
    struct av_dir it;
    const char *name;
    uint32_t name_len, dtype, count;
    uint64_t file_id;
    bool saw_fiji = false, saw_plist = false;
    uint64_t fiji_id = 0, plist_id = 0;

    if (argc > 1) refdir = argv[1];

    /* A directory whose 44 entries share one leaf. */
    tree[0] = load(14705);
    note(tree[0] != NULL, "the leaf holding Pacific");
    if (!tree[0]) return 1;

    av_dir_init(&it, 62297);
    count = 0;
    while (av_dir_next(tree, 1, &it, &name, &name_len, &file_id, &dtype)) {
        if (name_len == 4 && !memcmp(name, "Fiji", 4)) { saw_fiji = true; fiji_id = file_id; }
        count++;
    }
    note(count == 44, "Pacific: forty-four entries");
    note(saw_fiji && fiji_id == 62307, "Pacific/Fiji -> 62307, a file");
    note(dtype == 8, "the last one listed is a regular file");

    /* Every entry must be a file, and none may repeat. */
    {
        uint64_t ids[64];
        uint32_t n = 0, i, j;
        bool dup = false, allfiles = true;

        av_dir_init(&it, 62297);
        while (av_dir_next(tree, 1, &it, &name, &name_len, &file_id, &dtype)) {
            if (dtype != 8u) allfiles = false;
            if (n < 64u) ids[n++] = file_id;
        }
        for (i = 0; i < n; ++i)
            for (j = i + 1u; j < n; ++j)
                if (ids[i] == ids[j]) dup = true;
        note(!dup, "no entry is listed twice");
        note(allfiles, "every entry under Pacific is a file");
        note(n == 44u, "the second pass agrees on the count");
    }

    /* A small directory, to be sure the count is not an accident of size. */
    free((void *)tree[0]);
    tree[0] = load(14536);
    note(tree[0] != NULL, "the leaf holding GKCentralCache.momd");
    if (tree[0]) {
        av_dir_init(&it, 61311);
        count = 0;
        while (av_dir_next(tree, 1, &it, &name, &name_len, &file_id, &dtype)) {
            if (name_len == 17 && !memcmp(name, "VersionInfo.plist", 17)) {
                saw_plist = true;
                plist_id = file_id;
            }
            count++;
        }
        note(count == 3, "GKCentralCache.momd: three entries");
        note(saw_plist && plist_id == 61314, "and VersionInfo.plist is one of them");
    }

    /* A directory that is not there yields nothing rather than reading past its leaf. */
    av_dir_init(&it, 61019);
    count = 0;
    while (av_dir_next(tree, 1, &it, &name, &name_len, &file_id, &dtype)) count++;
    note(count == 0, "a directory with no records here lists nothing");

    free((void *)tree[0]);
    printf("test-ufs-dir-enum: %s\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
