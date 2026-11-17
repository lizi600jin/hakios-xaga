/* Host callback harness only, never part of a kernel build. Real-XNU ARM64 checked separately. */
#ifndef UFS_SERVICE_TEST_ENV_H
#define UFS_SERVICE_TEST_ENV_H
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include "ufs_service_engine.h"
#define __unused __attribute__((unused))
typedef uintptr_t vm_offset_t;
typedef size_t vm_size_t;
typedef int boolean_t;
typedef int kern_return_t;
typedef uintptr_t thread_t;
typedef int wait_result_t;
struct proc { int unused; };
struct uio { int64_t offset, residual; uint8_t *base; size_t moved; };
struct vnode { dev_t device; };
typedef struct vnode *vnode_t;
typedef void *vfs_context_t;
struct cdevsw {
    int (*d_open)(dev_t,int,int,struct proc *);
    int (*d_close)(dev_t,int,int,struct proc *);
    int (*d_read)(dev_t,struct uio *,int);
    int (*d_write)(dev_t,struct uio *,int);
    int (*d_ioctl)(void),(*d_stop)(void),(*d_reset)(void);
    void *d_ttys;
    int (*d_select)(void),(*d_mmap)(void),(*d_strategy)(void),(*d_reserved_1)(void),(*d_reserved_2)(void);
    int d_type;
};
extern struct cdevsw cdevsw[256];
int us_eno(void);
#define eno_ioctl us_eno
#define eno_stop us_eno
#define eno_reset us_eno
#define eno_select us_eno
#define eno_mmap us_eno
#define eno_strat us_eno
#define eno_getc us_eno
#define eno_putc us_eno
#define FREAD 1
#define FWRITE 2
#define Z_WAITOK 1
#define NSEC_PER_MSEC 1000000u
#define KERN_SUCCESS 0
#define PAGE_SIZE 4096u
#define PAGE_MASK (PAGE_SIZE-1)
#define VM_WIMG_WCOMB 7u
#define DEVFS_CHAR 1
#define UID_ROOT 0
#define GID_WHEEL 0
#define VNODE_LOOKUP_NOFOLLOW 1
#define VCHR 2
#define UIO_READ 0
#define UIO_SYSSPACE 1
#define IO_NOCACHE 1
#define minor(d) ((int)((d)&255))
#define makedev(m,n) (((dev_t)(m)<<8)|(n))
#define LCK_GRP_DECLARE(n,s) int n
#define LCK_MTX_DECLARE(n,g) int n
void lck_mtx_lock(int *);
void lck_mtx_unlock(int *);
void *kalloc_data(size_t,int);
void kfree_data(void *,size_t);
int64_t uio_offset(struct uio *);
int64_t uio_resid(struct uio *);
int uiomove(caddr_t,int,struct uio *);
int cdevsw_add(int,const struct cdevsw *);
int cdevsw_remove(int,const struct cdevsw *);
void *devfs_make_node(dev_t,int,int,int,int,const char *,...);
void delay_for_interval(uint32_t,uint32_t);
uint64_t mach_absolute_time(void);
void absolutetime_to_nanoseconds(uint64_t,uint64_t *);
bool PE_parse_boot_argn(const char *,void *,size_t);
kern_return_t kernel_thread_start(void (*)(void *,wait_result_t),void *,thread_t *);
void thread_deallocate(thread_t);
thread_t current_thread(void);
vfs_context_t vfs_context_kernel(void);
void *vfs_context_ucred(vfs_context_t);
struct proc *vfs_context_proc(vfs_context_t);
int vnode_lookup(const char *,int,vnode_t *,vfs_context_t);
int vnode_vtype(vnode_t);
dev_t vnode_specrdev(vnode_t);
int VNOP_OPEN(vnode_t,int,vfs_context_t);
int VNOP_CLOSE(vnode_t,int,vfs_context_t);
void vnode_put(vnode_t);
int vn_rdwr(int,vnode_t,caddr_t,int,off_t,int,int,void *,int *,struct proc *);
void us_host_barrier(void);
#endif
