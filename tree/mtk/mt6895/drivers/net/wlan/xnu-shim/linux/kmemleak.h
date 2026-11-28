/* SPDX-License-Identifier: GPL-2.0 */
/*
 * linux/kmemleak.h 的 XNU 侧桩。
 *
 * 驱动里只有两处 kmemleak_ignore()（common/wlan_oid.c:1442/1521），
 * 作用是把一块已分配内存标注给内核泄漏检测器。XNU 没有 kmemleak，
 * 这里保留调用点语义为空操作，而不是删掉驱动的收尾逻辑。
 */
#ifndef _XAGA_SHIM_KMEMLEAK_H
#define _XAGA_SHIM_KMEMLEAK_H

#define kmemleak_ignore(ptr)		do { (void)(ptr); } while (0)
#define kmemleak_not_leak(ptr)		do { (void)(ptr); } while (0)
#define kmemleak_no_scan(ptr)		do { (void)(ptr); } while (0)
#define kmemleak_alloc(ptr, size, min, gfp)	do { } while (0)
#define kmemleak_free(ptr)		do { (void)(ptr); } while (0)

#endif /* _XAGA_SHIM_KMEMLEAK_H */
