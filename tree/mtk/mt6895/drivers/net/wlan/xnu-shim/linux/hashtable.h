#ifndef _XAGA_SHIM_HASHTABLE_H
#define _XAGA_SHIM_HASHTABLE_H

/*
 * XNU 侧的 linux/hashtable.h 垫片。
 *
 * os/linux/hif/common/include/hif_pdma.h:14 无条件 include 这个头，而 _HIF_AXI
 * 路径会把 hif_pdma.h 拉进来，于是 soc7_0 的每个单元都要看到它。里头用到的
 * 只有 hlist + 六个宏（hash_add / hash_del(_rcu) / hash_init /
 * hash_for_each(_safe/_possible(_rcu))），所以这里只给这部分，不给 rhashtable。
 *
 * 语义与 Linux 一致：桶内是单链表头插，hash_del 只摘链不 poison。
 * XNU 内核里这些表都是驱动自己持有锁（hif_pdma 的 arRcbHTbl 由 rHifInfo 的
 * 锁保护），垫片不引入新的锁。
 */

#include "hakios_mt6895_wlan_xnu_compat.h"

#ifndef _XAGA_SHIM_HLIST_H
#define _XAGA_SHIM_HLIST_H

struct hlist_node {
	struct hlist_node *next;
	struct hlist_node **pprev;
};

struct hlist_head {
	struct hlist_node *first;
};

#define HLIST_HEAD_INIT { .first = 0 }

static inline void hlist_add_head(struct hlist_node *n, struct hlist_head *h)
{
	struct hlist_node *first = h->first;

	n->next = first;
	if (first)
		first->pprev = &n->next;
	h->first = n;
	n->pprev = &h->first;
}

static inline void hlist_del(struct hlist_node *n)
{
	struct hlist_node *next = n->next;
	struct hlist_node **pprev = n->pprev;

	*pprev = next;
	if (next)
		next->pprev = pprev;
}

static inline int hlist_empty(const struct hlist_head *h)
{
	return !h->first;
}

#define hlist_entry(ptr, type, member) \
	((type *)((char *)(ptr) - __builtin_offsetof(type, member)))

#define hlist_entry_safe(ptr, type, member) \
	((ptr) ? hlist_entry(ptr, type, member) : 0)

#endif /* _XAGA_SHIM_HLIST_H */

#define DECLARE_HASHTABLE(name, bits) \
	struct hlist_head name[1 << (bits)]

#define HASH_SIZE(name) (sizeof(name) / sizeof(name[0]))
#define HASH_BITS(name) __builtin_ctz((unsigned long)HASH_SIZE(name))

#define HASH_MASK(bits) ((1UL << (bits)) - 1)
#define hash_min(val, bits) ((unsigned long)(val) & HASH_MASK(bits))

#define hash_init(hashtable)						\
	do {								\
		unsigned long __i;					\
		for (__i = 0; __i < HASH_SIZE(hashtable); __i++)	\
			(hashtable)[__i].first = 0;			\
	} while (0)

#define hash_add(hashtable, node, key) \
	hlist_add_head(node, &(hashtable)[hash_min(key, HASH_BITS(hashtable))])

#define hash_add_rcu(hashtable, node, key) hash_add(hashtable, node, key)

#define hash_del(node) hlist_del(node)
#define hash_del_rcu(node) hlist_del(node)

#define hash_empty(hashtable)						\
	({								\
		int __empty = 1;					\
		unsigned long __i;					\
		for (__i = 0; __i < HASH_SIZE(hashtable); __i++)	\
			if ((hashtable)[__i].first) {			\
				__empty = 0;			\
				break;				\
			}					\
		__empty;						\
	})

#define hash_for_each(hashtable, bkt, obj, member)			\
	for (bkt = 0; bkt < HASH_SIZE(hashtable); bkt++)		\
		for (obj = hlist_entry_safe((hashtable)[bkt].first,	\
					    __typeof__(*(obj)), member); \
		     obj;						\
		     obj = hlist_entry_safe((obj)->member.next,		\
					    __typeof__(*(obj)), member))

#define hash_for_each_safe(hashtable, bkt, tmp, obj, member)		\
	for (bkt = 0; bkt < HASH_SIZE(hashtable); bkt++)		\
		for (obj = hlist_entry_safe((hashtable)[bkt].first,	\
					    __typeof__(*(obj)), member), \
		     tmp = obj ? hlist_entry_safe((obj)->member.next,	\
						  __typeof__(*(obj)), member) : 0; \
		     obj;						\
		     obj = tmp,						\
		     tmp = obj ? hlist_entry_safe((obj)->member.next,	\
						  __typeof__(*(obj)), member) : 0)

#define hash_for_each_possible(hashtable, obj, member, key)		\
	for (obj = hlist_entry_safe(					\
		     (hashtable)[hash_min(key, HASH_BITS(hashtable))].first, \
		     __typeof__(*(obj)), member);			\
	     obj;							\
	     obj = hlist_entry_safe((obj)->member.next,			\
				    __typeof__(*(obj)), member))

#define hash_for_each_possible_rcu(hashtable, obj, member, key) \
	hash_for_each_possible(hashtable, obj, member, key)

#endif /* _XAGA_SHIM_HASHTABLE_H */
