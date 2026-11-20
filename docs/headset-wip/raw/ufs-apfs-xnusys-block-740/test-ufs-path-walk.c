/*
 * Trial737: the directory and path walk, against blocks read from the device.
 *
 * The blocks in reference/walk737/ are real: each was read from this phone's xnusys partition
 * with the same dd the driver's own reads go through. What is checked here is that the walker,
 * given only those blocks, arrives at the same answers the host's independent APFS reader does.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "ufs_apfs_volume.h"

#define MAXBLK 64

static uint8_t store[MAXBLK][AV_BLOCK];
static uint32_t used;
/* The blocks sit in walk737/ beside this file, so a fresh checkout runs without arguments. */
static const char *refdir = "reference/walk738";
static int failures;

static void note(int ok, const char *what)
{
    printf("  %s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) failures++;
}

static uint8_t *load(uint32_t rel)
{
    char path[512];
    FILE *f;

    if (used >= MAXBLK) return NULL;
    snprintf(path, sizeof(path), "%s/%u.bin", refdir, rel);
    f = fopen(path, "rb");
    if (!f) return NULL;
    if (fread(store[used], 1, AV_BLOCK, f) != AV_BLOCK) { fclose(f); return NULL; }
    fclose(f);
    return store[used++];
}

/* The object map's root and whichever leaves the walk has needed so far. */
static uint8_t *omap_root;
static struct { uint32_t rel; uint8_t *blk; } leaf[16];
static uint32_t nleaf;

static uint8_t *leaf_for(uint64_t oid, uint32_t *rel_out)
{
    uint32_t rel = 0, i;

    if (!av_omap_leaf_for(omap_root, oid, &rel)) return NULL;
    *rel_out = rel;
    for (i = 0; i < nleaf; ++i)
        if (leaf[i].rel == rel) return leaf[i].blk;
    if (nleaf >= 16) return NULL;
    leaf[nleaf].blk = load(rel);
    if (!leaf[nleaf].blk) return NULL;
    leaf[nleaf].rel = rel;
    return leaf[nleaf++].blk;
}

/*
 * One descent. Returns 1 found, 0 absent, -1 refused. `blocks` counts how many fetches it took,
 * which is what the driver's budget is written against.
 */
static int descend(uint64_t dir, const char *name, uint32_t len,
                   uint64_t *file_id, uint32_t *dtype, uint32_t *blocks)
{
    struct av_look look;
    const uint8_t *tree[24];
    uint32_t depth = 1;
    uint8_t *root = load(2510);

    if (!root) return -1;
    tree[0] = root;
    av_look_init(&look, dir, 1029, name, len);
    *blocks = 0;

    for (unsigned step = 0; step < 16; ++step) {
        uint64_t want = 0, fid = 0;
        uint32_t dt = 0, rel = 0;
        enum av_look_result r = av_look_next(tree, depth, &look, &want, &fid, &dt);

        if (r == AV_LOOK_FOUND) { *file_id = fid; *dtype = dt; return 1; }
        if (r == AV_LOOK_ABSENT) return 0;
        if (r == AV_LOOK_NEXT_LEAF) {
            /* A large directory's entries run past this leaf; try the one after it. */
            uint64_t sib = 0, addr = 0, xid = 0;
            uint8_t *ml, *node;
            uint32_t lrel = 0;

            if (!av_next_leaf_oid(tree, look.index, look.depth, &sib)) return -1;
            if (look.depth > 0) look.index[look.depth - 1u]++;
            ml = leaf_for(sib, &lrel);
            if (!ml) return -1;
            if (!av_leaf_map(ml, lrel, 1, sib, AV_PARTITION_BLOCKS, &addr, &xid)) return -1;
            node = load((uint32_t)addr);
            if (!node) return -1;
            tree[depth - 1u] = node;
            (*blocks)++;
            continue;
        }
        if (r != AV_LOOK_FETCH) return -1;

        {
            uint8_t *ml = leaf_for(want, &rel);
            uint64_t addr = 0, xid = 0;
            uint8_t *node;

            if (!ml) return -1;
            if (!av_leaf_map(ml, rel, 1, want, AV_PARTITION_BLOCKS, &addr, &xid)) return -1;
            node = load((uint32_t)addr);
            if (!node || depth >= 24) return -1;
            tree[depth++] = node;
            (*blocks)++;
        }
    }
    return -1;
}

int main(int argc, char **argv)
{
    uint64_t fid = 0;
    uint32_t dt = 0, blocks = 0;
    int r;

    if (argc > 1) refdir = argv[1];

    omap_root = load(2398);
    note(omap_root != NULL, "object map root reads");

    /* The root directory's own entries, straight from the tree root. */
    r = descend(2, "usr\0", 4, &fid, &dt, &blocks);
    note(r == 1 && fid == 61075 && dt == 4, "root: usr -> 61075, a directory");
    note(blocks <= 4, "root: four or fewer fetches");

    /* A name that is not there ends the walk; it is not an error. */
    r = descend(2, "not-here\0", 9, &fid, &dt, &blocks);
    note(r == 0, "absent name ends the walk cleanly");

    /* Two levels, the second one under a different subtree. */
    r = descend(61075, "bin\0", 4, &fid, &dt, &blocks);
    note(r == 1 && fid == 61076 && dt == 4, "/usr/bin -> 61076");

    r = descend(61076, "brctl\0", 6, &fid, &dt, &blocks);
    note(r == 1 && fid == 61082 && dt == 8, "/usr/bin/brctl -> 61082, a regular file");

    /* A directory that exists but does not hold this name, and one that does. */
    r = descend(61019, "usr\0", 4, &fid, &dt, &blocks);
    note(r == 0, "/bin has no usr");
    r = descend(61019, "df\0", 3, &fid, &dt, &blocks);
    note(r == 1 && fid == 61020 && dt == 8, "/bin/df -> 61020");

    /* /usr holds libexec; the walk must not confuse it with a name it does not hold. */
    r = descend(61075, "libexec\0", 8, &fid, &dt, &blocks);
    note(r == 1 && fid == 61297 && dt == 4, "/usr/libexec -> 61297");

    printf("test-ufs-path-walk: %s\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
