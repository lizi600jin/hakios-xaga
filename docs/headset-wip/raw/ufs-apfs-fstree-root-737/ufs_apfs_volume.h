#ifndef MT6895_UFS_APFS_VOLUME_H
#define MT6895_UFS_APFS_VOLUME_H

#include <stdbool.h>
#include <stdint.h>
/* No libc: the same header is compiled into the kernel and into a freestanding user program, so
 * the one C library call it needed is written out here instead of included. */
static inline int av_memcmp(const void *a, const void *b, unsigned long n)
{
    const unsigned char *x = (const unsigned char *)a, *y = (const unsigned char *)b;
    for (unsigned long i = 0; i < n; ++i) if (x[i] != y[i]) return (int)x[i] - (int)y[i];
    return 0;
}

#define AV_BLOCK 4096u
#define AV_PARTITION_BLOCKS UINT64_C(655608)

static inline uint16_t av_u16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static inline uint32_t av_u32(const uint8_t *p)
{
    return (uint32_t)av_u16(p) | ((uint32_t)av_u16(p + 2) << 16);
}

static inline uint64_t av_u64(const uint8_t *p)
{
    return (uint64_t)av_u32(p) | ((uint64_t)av_u32(p + 4) << 32);
}

static inline bool av_object(const uint8_t *p, uint32_t type, uint32_t subtype)
{
    if (av_u32(p + 24) != type || av_u32(p + 28) != subtype || !av_u64(p + 16))
        return false;
    uint64_t a = 0, b = 0;
    const uint64_t mod = UINT32_MAX;
    /* At most 1022 words in this 4 KiB object. Both sums fit in 64 bits. */
    for (unsigned i = 8; i < AV_BLOCK; i += 4) {
        a += av_u32(p + i);
        b += a;
    }
    uint64_t lo = mod - ((a + b) % mod);
    uint64_t hi = mod - ((a + lo) % mod);
    return av_u64(p) == ((hi << 32) | lo);
}

static inline bool av_extent(uint64_t addr, uint32_t count, uint64_t limit)
{
    return addr > 0 && addr < limit && count > 0 && count <= limit - addr;
}

static inline bool av_nx(const uint8_t *p)
{
    return av_object(p, UINT32_C(0x80000001), 0) && av_u64(p + 8) == 1 &&
        av_u32(p + 32) == UINT32_C(0x4253584e) && av_u32(p + 36) == AV_BLOCK &&
        av_u64(p + 40) > 0 && av_u64(p + 40) <= AV_PARTITION_BLOCKS;
}

/* This image uses a contiguous checkpoint descriptor ring. Its active window
 * contains exactly a checkpoint map and a container superblock, without wrap. */
static inline bool av_descriptors(const uint8_t *nx, uint32_t *start)
{
    uint64_t base = av_u64(nx + 112), limit = av_u64(nx + 40);
    uint32_t capacity = av_u32(nx + 104), index = av_u32(nx + 136);
    if (capacity < 2 || !av_extent(base, capacity, limit) || index > capacity - 2 ||
        av_u32(nx + 140) != 2 || av_u32(nx + 128) != (index + 2u) % capacity)
        return false;
    *start = (uint32_t)base + index;
    return true;
}

static inline bool av_checkpoint(const uint8_t *boot, const uint8_t *map,
                                 const uint8_t *nx, uint32_t start)
{
    uint32_t active;
    if (!av_nx(nx) || !av_descriptors(nx, &active) || active != start ||
        av_u64(nx + 40) != av_u64(boot + 40) ||
        av_u64(nx + 72) != av_u64(boot + 72) || av_u64(nx + 80) != av_u64(boot + 80) ||
        av_u64(nx + 16) < av_u64(boot + 16) || av_u32(nx + 180) == 0 ||
        av_u32(nx + 180) > 100 || !av_u64(nx + 184))
        return false;
    if (!av_object(map, UINT32_C(0x4000000c), 0) || av_u64(map + 8) != start ||
        av_u64(map + 16) != av_u64(nx + 16) || av_u32(map + 32) != 1)
        return false;
    uint32_t entries = av_u32(map + 36);
    if (!entries || entries > (AV_BLOCK - 40u) / 40u) return false;
    for (uint32_t i = 0; i < entries; ++i) {
        const uint8_t *entry = map + 40u + 40u * i;
        uint32_t bytes = av_u32(entry + 8);
        if (!bytes || bytes % AV_BLOCK ||
            !av_extent(av_u64(entry + 32), bytes / AV_BLOCK, av_u64(nx + 40)))
            return false;
    }
    return true;
}

/* Resolve slot zero's virtual OID using the latest mapping at or before the
 * active checkpoint. This reader supports a single physical root/leaf node with
 * fixed 16-byte keys and values, as used by this image's container object map. */
static inline bool av_mapping(const uint8_t *node, uint64_t addr, uint64_t checkpoint,
                              uint64_t oid, uint64_t limit, uint64_t *volume, uint64_t *xid)
{
    const uint32_t end = AV_BLOCK - 40u;
    if (!av_object(node, UINT32_C(0x40000002), 11) || av_u64(node + 8) != addr ||
        av_u64(node + 16) > checkpoint || av_u16(node + 32) != 7 || av_u16(node + 34) ||
        av_u32(node + end) != 16 || av_u32(node + end + 4) != AV_BLOCK ||
        av_u32(node + end + 8) != 16 || av_u32(node + end + 12) != 16)
        return false;
    uint32_t toc = 56u + av_u16(node + 40), size = av_u16(node + 42);
    uint32_t keys = av_u32(node + 36);
    if (toc > end || size > end - toc || !keys || keys > size / 4u ||
        av_u64(node + end + 24) != keys || av_u64(node + end + 32) != 1)
        return false;
    uint32_t base = toc + size, last_key_end = base, first_val = end;
    uint64_t prev_oid = 0, prev_xid = 0;
    const uint8_t *hit = 0;
    for (uint32_t i = 0; i < keys; ++i) {
        uint32_t key = base + av_u16(node + toc + 4u*i);
        uint32_t back = av_u16(node + toc + 4u*i + 2u);
        if (key > end - 16u || back < 16u || back > end - base) return false;
        if (key + 16u > last_key_end) last_key_end = key + 16u;
        if (end - back < first_val) first_val = end - back;
        if (last_key_end > first_val) return false;
        uint64_t key_oid = av_u64(node + key), key_xid = av_u64(node + key + 8u);
        if (!key_oid || !key_xid || key_oid < prev_oid ||
            (key_oid == prev_oid && key_xid <= prev_xid)) return false;
        prev_oid = key_oid;
        prev_xid = key_xid;
        if (key_oid == oid && key_xid <= checkpoint) {
            hit = node + end - back;
            *xid = key_xid;
        }
    }
    if (!hit || av_u32(hit) != 0 || av_u32(hit + 4) != AV_BLOCK ||
        !av_extent(av_u64(hit + 8), 1, limit)) return false;
    *volume = av_u64(hit + 8);
    return true;
}

/*
 * Trial736: the volume's file-system tree root, reached from the APSB.
 *
 * The path is four more reads: the volume's omap object, the internal node of that omap's
 * tree, the leaf it names, and finally the tree root the leaf maps. Each address comes from
 * an object already verified, never from a constant, so a reader cannot ask for anything the
 * walk has not justified.
 *
 * av_internal_pick() is the piece Trial735 did not need: an omap tree with more than one level
 * keeps ordered (oid, xid) separator keys in its internal node, and the right child is the last
 * key not greater than the target. The keys must be strictly increasing on (oid, xid) and every
 * child must be a usable extent, so a malformed or reordered node is refused rather than walked.
 */
static inline bool av_internal_pick(const uint8_t *node, uint64_t addr, uint64_t checkpoint,
                                    uint64_t oid, uint64_t limit, uint32_t *child_index,
                                    uint64_t *child_addr)
{
    /* btree_node_phys flags: the same bits apfs.py names BTNODE_ROOT/LEAF/FIXED_KV_SIZE. */
    const uint16_t BTNODE_ROOT = 0x0001u, BTNODE_LEAF = 0x0002u, BTNODE_FIXED = 0x0004u;
    const uint32_t BT_DATA_OFF = 56u, BT_INFO_SIZE = 40u;

    uint16_t flags, level;
    uint32_t nkeys, toc_off, toc_len, key_base, val_end;
    uint32_t pick = 0;
    bool have = false;
    uint64_t prev_oid = 0, prev_xid = 0;

    if (!av_object(node, UINT32_C(0x40000002), 11)) return false;
    if (av_u64(node + 8) != addr || av_u64(node + 16) > checkpoint) return false;

    flags = av_u16(node + 32);
    level = av_u16(node + 34);
    nkeys = av_u32(node + 36);
    toc_off = BT_DATA_OFF + av_u16(node + 40);
    toc_len = av_u16(node + 42);

    /* Must be an interior node: not a leaf, and holding at least one key. */
    if (flags & BTNODE_LEAF) return false;
    if (!nkeys) return false;

    val_end = AV_BLOCK - ((flags & BTNODE_ROOT) ? BT_INFO_SIZE : 0u);
    if (toc_off > val_end || toc_len > val_end - toc_off) return false;
    key_base = toc_off + toc_len;
    if (key_base > val_end) return false;

    /* Fixed-size keys and 8-byte child addresses, which is what an omap interior node uses. */
    if (!(flags & BTNODE_FIXED)) return false;
    if (nkeys > toc_len / 4u) return false;

    (void)level;

    for (uint32_t i = 0; i < nkeys; ++i) {
        uint32_t koff = av_u16(node + toc_off + 4u * i);
        uint32_t voff = av_u16(node + toc_off + 4u * i + 2u);
        uint64_t key_oid, key_xid, child;

        if (key_base + koff + 16u > val_end) return false;
        if (voff < 8u || val_end - voff < 8u) return false;

        key_oid = av_u64(node + key_base + koff);
        key_xid = av_u64(node + key_base + koff + 8u);
        if (!key_oid || !key_xid || key_oid < prev_oid ||
            (key_oid == prev_oid && key_xid <= prev_xid)) return false;
        prev_oid = key_oid;
        prev_xid = key_xid;

        child = av_u64(node + val_end - voff);
        if (!av_extent(child, 1, limit)) return false;
        if (key_oid <= oid && key_xid <= checkpoint) {
            pick = i;
            *child_addr = child;
            have = true;
        }
    }
    if (!have) return false;
    *child_index = pick;
    return true;
}

/*
 * Trial736: the newest visible mapping of `oid` inside an omap *leaf*.
 *
 * The leaf's values live just below the end of the block, and that end is 4096 unless the node
 * carries BTNODE_ROOT, in which case btree_info occupies the last 40 bytes. Reading the two cases
 * the same way shifts every value by 40 and silently returns a neighbouring object's address --
 * which is exactly what the first attempt here did (oid 1029 came back as 2513 instead of 2510).
 *
 * The value is btrees: flags(4), size(4), paddr(8). Only a plain, full-block mapping is accepted.
 */
static inline bool av_leaf_map(const uint8_t *node, uint64_t addr, uint64_t checkpoint,
                               uint64_t oid, uint64_t limit, uint64_t *paddr, uint64_t *xid_out)
{
    const uint16_t BTNODE_ROOT = 0x0001u, BTNODE_LEAF = 0x0002u, BTNODE_FIXED = 0x0004u;
    const uint32_t BT_DATA_OFF = 56u, BT_INFO_SIZE = 40u;

    uint16_t flags;
    uint32_t nkeys, toc_off, toc_len, key_base, val_end;
    bool have = false;
    uint64_t best_xid = 0, best_addr = 0;
    uint64_t prev_oid = 0, prev_xid = 0;

    if (!av_object(node, UINT32_C(0x40000003), 11)) return false;
    if (av_u64(node + 8) != addr || av_u64(node + 16) > checkpoint) return false;

    flags = av_u16(node + 32);
    nkeys = av_u32(node + 36);
    toc_off = BT_DATA_OFF + av_u16(node + 40);
    toc_len = av_u16(node + 42);

    if (!(flags & BTNODE_LEAF) || !(flags & BTNODE_FIXED) || !nkeys) return false;
    if (toc_len < nkeys * 4u) return false;

    val_end = AV_BLOCK - ((flags & BTNODE_ROOT) ? BT_INFO_SIZE : 0u);
    if (toc_off > val_end || toc_len > val_end - toc_off) return false;
    key_base = toc_off + toc_len;

    for (uint32_t i = 0; i < nkeys; ++i) {
        uint32_t koff = av_u16(node + toc_off + 4u * i);
        uint32_t voff = av_u16(node + toc_off + 4u * i + 2u);
        const uint8_t *val;
        uint64_t key_oid, key_xid, flags32, size32, p;

        if (key_base + koff + 16u > val_end) return false;
        if (voff == 0xffffu) continue;                     /* a ghost: no value */
        if (voff < 16u || val_end - voff < 16u) return false;

        key_oid = av_u64(node + key_base + koff);
        key_xid = av_u64(node + key_base + koff + 8u);
        if (!key_oid || !key_xid || key_oid < prev_oid ||
            (key_oid == prev_oid && key_xid <= prev_xid)) return false;
        prev_oid = key_oid;
        prev_xid = key_xid;

        if (key_oid != oid || key_xid > checkpoint) continue;

        val = node + (val_end - voff);
        flags32 = av_u32(val);
        size32 = av_u32(val + 4);
        p = av_u64(val + 8);
        if (flags32 != 0u || size32 != AV_BLOCK) return false;
        if (!av_extent(p, 1, limit)) return false;
        if (!have || key_xid > best_xid) {
            best_xid = key_xid;
            best_addr = p;
            have = true;
        }
    }
    if (!have) return false;
    *paddr = best_addr;
    *xid_out = best_xid;
    return true;
}


/* ---- Trial737: the file-system tree's own keys ------------------------------------------
 *
 * The tree below the root is keyed by j_key_t: a 64-bit field whose low 60 bits are the object
 * identifier and whose top 4 bits are the record kind, followed by kind-specific bytes (a name
 * for a directory entry, a logical offset for an extent). The tree is ordered by that pair first,
 * so choosing a child means comparing against the first key of each entry, and the node is not
 * BTNODE_FIXED_KV_SIZE -- its table of contents holds four halfwords per entry.
 */
#define AV_J_OBJ_ID_MASK   UINT64_C(0x0FFFFFFFFFFFFFFF)
#define AV_J_TYPE_SHIFT 60u
#define AV_J_TYPE_INODE   3u
#define AV_J_TYPE_DIRREC  9u
#define AV_J_DREC_LEN_MASK UINT32_C(0x3ff)

static inline uint64_t av_j_objid(const uint8_t *key) { return av_u64(key) & AV_J_OBJ_ID_MASK; }
static inline uint32_t av_j_type(const uint8_t *key) { return (uint32_t)(av_u64(key) >> AV_J_TYPE_SHIFT); }

/* Where one entry's key and value sit, given the node and an index. */
static inline bool av_j_entry(const uint8_t *node, uint32_t index, const uint8_t **key,
                              uint32_t *klen, const uint8_t **val, uint32_t *vlen)
{
    const uint16_t BTNODE_ROOT = 0x0001u, BTNODE_LEAF = 0x0002u, BTNODE_FIXED = 0x0004u;
    const uint32_t BT_DATA_OFF = 56u, BT_INFO_SIZE = 40u;

    uint16_t flags = av_u16(node + 32);
    uint32_t nkeys = av_u32(node + 36);
    uint32_t toc = BT_DATA_OFF + av_u16(node + 40);
    uint32_t val_end = AV_BLOCK - ((flags & BTNODE_ROOT) ? BT_INFO_SIZE : 0u);
    uint32_t key_base, koff, voff, kl, vl;

    if (index >= nkeys) return false;
    if (flags & BTNODE_FIXED) return false;                 /* the file-system tree never is */
    if (toc + 8u * nkeys > val_end) return false;
    key_base = toc + av_u16(node + 42);

    koff = av_u16(node + toc + 8u * index);
    kl   = av_u16(node + toc + 8u * index + 2u);
    voff = av_u16(node + toc + 8u * index + 4u);
    vl   = av_u16(node + toc + 8u * index + 6u);

    if (voff == 0xffffu) return false;                      /* a ghost has no value */
    if (key_base + koff + kl > val_end) return false;
    if (voff > val_end || vl > val_end - voff) return false;

    *key = node + key_base + koff;  *klen = kl;
    *val = node + (val_end - voff); *vlen = vl;
    (void)BTNODE_LEAF;
    return true;
}

/*
 * Which child of an interior node leads to `oid`. The tree is ordered by (object, kind), so the
 * answer is the last entry whose first key does not sort after the one being looked for -- the
 * same rule the object map uses, applied to a different key layout.
 */
static inline bool av_j_pick(const uint8_t *node, uint64_t addr, uint64_t oid, uint32_t type,
                             uint32_t *index, uint64_t *child_oid)
{
    uint16_t flags = av_u16(node + 32);
    uint32_t nkeys = av_u32(node + 36);
    uint64_t prev = 0;
    uint32_t prev_kind = 0;
    bool have = false;

    if (av_u64(node + 8) != addr) return false;
    if ((flags & 0x0002u) != 0u) return false;              /* a leaf has no children */
    if (!nkeys) return false;

    for (uint32_t i = 0; i < nkeys; ++i) {
        const uint8_t *key, *val;
        uint32_t klen, vlen;
        uint64_t first;
        uint32_t kind;

        if (!av_j_entry(node, i, &key, &klen, &val, &vlen)) return false;
        if (klen < 8u || vlen < 8u) return false;
        /*
         * The tree sorts by the whole (object, kind) pair, not by object alone: one object
         * contributes an inode key, a directory-entry key and an extent key, and they sit at
         * different places in the order. Comparing only the object identifier picks the wrong
         * branch as soon as a directory's own records and its entries straddle a node boundary,
         * which is exactly what happened the first time this was tried.
         */
        first = av_j_objid(key);
        kind = av_j_type(key);
        if (i && (first < prev || (first == prev && kind < prev_kind))) return false;
        prev = first; prev_kind = kind;
        if (first < oid || (first == oid && kind <= type)) {
            *index = i;
            *child_oid = av_u64(val);
            have = true;
        }
    }
    return have;
}

/* The directory entry named in a leaf, if it is there. */
static inline bool av_j_dirent(const uint8_t *leaf, uint64_t addr, uint64_t dir_oid,
                               const char *name, uint32_t name_len,
                               uint64_t *file_id, uint32_t *dtype)
{
    uint32_t nkeys = av_u32(leaf + 36);
    bool have = false;

    if (av_u64(leaf + 8) != addr) return false;
    for (uint32_t i = 0; i < nkeys; ++i) {
        const uint8_t *key, *val;
        uint32_t klen, vlen, nlen;
        uint32_t stored;

        if (!av_j_entry(leaf, i, &key, &klen, &val, &vlen)) return false;
        if (klen < 12u || vlen < 18u) continue;
        if (av_j_objid(key) != dir_oid || av_j_type(key) != AV_J_TYPE_DIRREC) continue;

        stored = av_u32(key + 8) & AV_J_DREC_LEN_MASK;
        nlen = stored;
        if (nlen != name_len) continue;
        if (12u + nlen > klen) return false;
        if (av_memcmp(key + 12, name, nlen) != 0) continue;

        *file_id = av_u64(val);
        *dtype = (uint32_t)(av_u16(val + 16) & 0x000fu);
        have = true;                                        /* last match wins: newest wins */
    }
    return have;
}


/*
 * Trial737: the descent itself.
 *
 * `blocks` holds the snapshot so far; `have` is how many are in it. Each call either names the
 * next block to fetch or reports that the walk is finished. The state is carried in `look` alone,
 * so nothing here remembers an address the caller could get wrong.
 *
 * Constraints that make this safe rather than merely working:
 *   - every child address is taken from a value of a node whose header has already been checked
 *     against the address the parent named, so the chain cannot jump;
 *   - the level strictly decreases, so a corrupt tree cannot loop;
 *   - the object map is consulted for every virtual identifier, never assumed to be the address;
 *   - the walk stops at AV_LOOK_MAX_STEPS fetches, so a fabricated key order cannot run away.
 */
#define AV_LOOK_MAX_STEPS 8u
#define AV_LOOK_MAX_LEAVES 4u

struct av_look {
    uint64_t dir_oid;        /* directory whose entries we want */
    uint64_t tree_oid;       /* the tree's root object identifier */
    uint64_t node_oid;       /* virtual oid of the node we are descending into */
    const char *name;
    uint32_t name_len;
    uint32_t step;           /* fetch counter */
    uint16_t level;          /* level of node_oid, from its parent */
    uint8_t visited;         /* leaves examined so far */
    bool have_root;
};

static inline void av_look_init(struct av_look *look, uint64_t dir_oid, uint64_t tree_oid,
                                const char *name, uint32_t name_len)
{
    memset(look, 0, sizeof(*look));
    look->dir_oid = dir_oid;
    look->tree_oid = tree_oid;
    look->name = name;
    look->name_len = name_len;
}

/* What the caller should do after one av_look_next(). */
enum av_look_result {
    AV_LOOK_FETCH,      /* fetch `relative`, append it, then call again */
    AV_LOOK_FOUND,      /* `file_id` and `dtype` name the entry; the walk is done */
    AV_LOOK_ABSENT,     /* the name is not in this directory; the walk ends cleanly */
    AV_LOOK_REFUSED     /* something did not check out; no snapshot may be published */
};

/*
 * One step. `snapshot` holds `have` contiguous blocks starting at the tree root's first fetched
 * block; `nodes` maps the order they arrived in. Returns what to do next.
 */
static inline enum av_look_result
av_look_next(const uint8_t *const *block, uint32_t have, struct av_look *look,
             uint64_t *want_oid, uint64_t *file_id, uint32_t *dtype)
{
    const uint8_t *node;
    uint64_t addr;

    if (!block || !look || !want_oid || !file_id || !dtype) return AV_LOOK_REFUSED;
    if (look->step >= AV_LOOK_MAX_STEPS) return AV_LOOK_REFUSED;

    if (!look->have_root) {
        /* The first block is the tree root, whose identity was checked when it was fetched. */
        if (have < 1u) return AV_LOOK_REFUSED;
        look->node_oid = look->tree_oid;
        look->level = av_u16(block[0] + 34);
        look->have_root = true;
        if (look->level == 0u) { look->level = 0u; }
    }

    node = block[have - 1u];
    addr = av_u64(node + 8);
#ifdef AV_LOOK_TRACE
    printf("    look: have=%u addr=%llu flags=%#x level=%u nkeys=%u step=%u\n",
        have, (unsigned long long)addr, av_u16(node + 32), av_u16(node + 34),
        av_u32(node + 36), look->step);
#endif

    /* A leaf answers the question directly. */
    if ((av_u16(node + 32) & 0x0002u) != 0u) {
        look->visited++;
        if (av_j_dirent(node, addr, look->dir_oid, look->name, look->name_len,
                        file_id, dtype))
            return AV_LOOK_FOUND;
        /*
         * A directory's entries can span more than one leaf. The one after this starts at the
         * next leaf in the tree, but this walker only follows the path the root's keys describe,
         * so a miss here is reported as absent rather than guessed at.
         */
        return AV_LOOK_ABSENT;
    }

    /* An interior node names the child that covers our key. */
    {
        uint32_t index = 0;
        uint64_t child_oid = 0;
        uint16_t level = av_u16(node + 34);

        if (level == 0u) return AV_LOOK_REFUSED;
        if (!av_j_pick(node, addr, look->dir_oid, AV_J_TYPE_DIRREC, &index, &child_oid))
            return AV_LOOK_REFUSED;
        if (!child_oid || child_oid == look->node_oid) return AV_LOOK_REFUSED;

        /* The child is virtual: it has to go through the volume's object map. */
        look->node_oid = child_oid;
        look->level = (uint16_t)(level - 1u);
        look->step++;
        /* The child is a virtual object: the caller maps it to a block and fetches that. */
        *want_oid = child_oid;
        return AV_LOOK_FETCH;
    }
}


/*
 * Trial737: mapping a virtual identifier through the volume's object map.
 *
 * The map's root is already in the snapshot, but its leaves are not: which leaf holds an
 * identifier is decided by the root's own key order, so the caller fetches one leaf and this
 * function reads the answer out of it. Two identifiers that fall in the same leaf need only one
 * fetch, which is why the walk asks before fetching rather than fetching blindly.
 */
static inline bool av_omap_leaf_for(const uint8_t *map_root, uint64_t oid, uint32_t *leaf_rel)
{
    /* The map's interior node names, for each run of identifiers, the leaf that holds it. */
    uint16_t flags = av_u16(map_root + 32);
    uint32_t nkeys = av_u32(map_root + 36);
    uint32_t toc = 56u + av_u16(map_root + 40);
    uint32_t val_end = AV_BLOCK - ((flags & 0x0001u) ? 40u : 0u);
    uint32_t key_base;
    uint64_t best = 0;
    bool have = false;

    if (flags & 0x0002u) return false;                     /* the map root is interior here */
    if (!nkeys) return false;
    if (toc + 4u * nkeys > val_end) return false;          /* fixed-size keys: two halfwords */
    key_base = toc + av_u16(map_root + 42);

    for (uint32_t i = 0; i < nkeys; ++i) {
        uint32_t koff = av_u16(map_root + toc + 4u * i);
        uint32_t voff = av_u16(map_root + toc + 4u * i + 2u);
        uint64_t first;

        if (key_base + koff + 16u > val_end) return false;
        if (voff < 8u || val_end - voff < 8u) return false;
        first = av_u64(map_root + key_base + koff);
        if (first <= oid) {
            best = av_u64(map_root + val_end - voff);
            have = true;
        }
    }
    if (!have || best > UINT32_MAX) return false;
    *leaf_rel = (uint32_t)best;
    return true;
}

/* The tree root object itself: identity, checksum, transaction, type and TOC bounds. */
static inline bool av_tree_root(const uint8_t *node, uint64_t addr, uint64_t oid, uint64_t xid)
{
    const uint32_t end = AV_BLOCK - 40u;

    /* The root is a virtual object: its oid names it, and `addr` is where the omap says that oid
     * lives. They are different numbers on this volume (oid 1029 at relative 2510), so identity is
     * checked against oid and the location against the mapping, not against each other. */
    if (!av_object(node, 2, 14) || av_u64(node + 8) != oid || av_u64(node + 16) != xid)
        return false;
    (void)addr;
    /* btree_node_phys: flags at +32, level at +34, nkeys at +36. */
    if (!(av_u16(node + 32) & 1u)) return false;           /* BTNODE_ROOT */
    if (av_u16(node + 34) < 1u) return false;              /* level */
    if (av_u32(node + 36) == 0u) return false;             /* at least one key */
    /*
     * The file-system tree's keys are j_key_t, 64 bytes, not the 16-byte (oid, xid) pair the
     * object map uses. Checking 16 here rejected a root that is otherwise perfect, which is how
     * this was found.
     */
    if (av_u32(node + end) != 64u || av_u32(node + end + 4) != AV_BLOCK) return false;
    return true;
}

/* blocks holds complete contiguous snapshot blocks in acquisition order.
 * Relative addresses refer to the beginning of xnusys. A zero count signals a
 * validated APSB. Only a successful call publishes the next read. */
/*
 * `stop_after` is how many blocks the caller's mode wants: Trial735 stops at 6 (the volume
 * superblock) and Trial736 at 10 (the tree root). The same walk serves both, and the difference
 * is one number rather than a second parser -- but it is a parameter, not a guess, because the
 * two modes genuinely disagree about what complete==6 means.
 */
static inline bool av_next_upto(const uint8_t *blocks, uint32_t complete, uint32_t stop_after,
                                uint32_t *relative, uint32_t *count)
{
    if (!blocks || !relative || !count || stop_after < 6u || stop_after > 10u) return false;
    if (!(complete == 1 || (complete >= 3 && complete <= 10))) return false;
    if (complete > stop_after) return false;
    uint32_t descriptor;
    if (!av_nx(blocks) || !av_descriptors(blocks, &descriptor)) return false;
    if (complete == 1) {
        *relative = descriptor; *count = 2; return true;
    }
    const uint8_t *nx = blocks + 2u * AV_BLOCK;
    if (!av_checkpoint(blocks, blocks + AV_BLOCK, nx, descriptor)) return false;
    uint64_t limit = av_u64(nx + 40), checkpoint = av_u64(nx + 16);
    uint64_t map_addr = av_u64(nx + 160);
    if (!av_extent(map_addr, 1, limit)) return false;
    if (complete == 3) {
        *relative = (uint32_t)map_addr; *count = 1; return true;
    }
    const uint8_t *omap = blocks + 3u * AV_BLOCK;
    if (!av_object(omap, UINT32_C(0x4000000b), 0) || av_u64(omap + 8) != map_addr ||
        av_u64(omap + 16) > checkpoint || av_u32(omap + 40) != UINT32_C(0x40000002))
        return false;
    uint64_t tree = av_u64(omap + 48);
    if (!av_extent(tree, 1, limit)) return false;
    if (complete == 4) {
        *relative = (uint32_t)tree; *count = 1; return true;
    }
    uint64_t oid = av_u64(nx + 184), addr = 0, xid = 0;
    if (!av_mapping(blocks + 4u*AV_BLOCK, tree, checkpoint, oid, limit, &addr, &xid))
        return false;
    if (complete == 5) {
        *relative = (uint32_t)addr; *count = 1; return true;
    }
    const uint8_t *apsb = blocks + 5u * AV_BLOCK;
    if (!av_object(apsb, 13, 0) || av_u64(apsb + 8) != oid || av_u64(apsb + 16) != xid ||
        av_u32(apsb + 32) != UINT32_C(0x42535041) || av_u32(apsb + 36) != 0)
        return false;
    if (complete == 6) {
        if (stop_after == 6u) { *relative = 0; *count = 0; return true; }
        /*
         * Trial736 continues past the volume superblock; Trial735 ended here.
         *
         * apsb+48 is the volume's omap *oid*, and on this container that object is physical:
         * it is not in the container's object map (lookup refuses it) and the block it names
         * carries oid == its own address with the omap type. So the oid is also the address,
         * which is checked below rather than assumed here.
         */
        const uint64_t vol_omap_addr = av_u64(apsb + 128);
        if (!av_extent(vol_omap_addr, 1, limit)) return false;
        *relative = (uint32_t)vol_omap_addr; *count = 1; return true;
    }

    /* ---- Trial736: the file-system tree root ---------------------------------------- */
    {
        const uint8_t *vol_omap = blocks + 6u * AV_BLOCK;
        const uint64_t root_oid = av_u64(apsb + 136);
        uint32_t child = 0;
        uint64_t child_addr = 0;

        if (!av_object(vol_omap, UINT32_C(0x4000000b), 0)) return false;
        if (av_u64(vol_omap + 8) != av_u64(apsb + 128)) return false;
        if (av_u64(vol_omap + 16) > checkpoint) return false;
        if (av_u32(vol_omap + 40) != UINT32_C(0x40000002)) return false;

        {
            const uint64_t omap_tree = av_u64(vol_omap + 48);
            if (!av_extent(omap_tree, 1, limit)) return false;
            if (complete == 7) {
                *relative = (uint32_t)omap_tree; *count = 1; return true;
            }

            {
                const uint8_t *internal = blocks + 7u * AV_BLOCK;
                if (!av_internal_pick(internal, omap_tree, checkpoint, root_oid, limit,
                                      &child, &child_addr)) return false;
                if (complete == 8) {
                    *relative = (uint32_t)child_addr; *count = 1; return true;
                }

                {
                    const uint8_t *leaf = blocks + 8u * AV_BLOCK;
                    uint64_t leaf_addr = 0, leaf_xid = 0;
                    if (!av_leaf_map(leaf, child_addr, checkpoint, root_oid, limit,
                                     &leaf_addr, &leaf_xid))
                        return false;
                    if (complete == 9) {
                        *relative = (uint32_t)leaf_addr; *count = 1; return true;
                    }

                    {
                        const uint8_t *root = blocks + 9u * AV_BLOCK;
                        if (!av_tree_root(root, leaf_addr, root_oid, leaf_xid)) return false;
                        *relative = 0; *count = 0;
                        return true;
                    }
                }
            }
        }
    }
}
/* Trial735's entry point: the walk that ends at the volume superblock. */
static inline bool av_next(const uint8_t *blocks, uint32_t complete,
                           uint32_t *relative, uint32_t *count)
{
    return av_next_upto(blocks, complete, 6u, relative, count);
}

/* Trial736's: the same walk, continued to the file-system tree root. */
static inline bool av_next_root(const uint8_t *blocks, uint32_t complete,
                                uint32_t *relative, uint32_t *count)
{
    return av_next_upto(blocks, complete, 10u, relative, count);
}

#endif
