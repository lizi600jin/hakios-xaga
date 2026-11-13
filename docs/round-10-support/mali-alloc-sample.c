/* SPDX-License-Identifier: GPL-2.0 */
/* Ordinary Mali CSF allocation through the supplied vendor UAPI. No GPU jobs. */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include "mali_base_kernel.h"
#include "mali_kbase_ioctl.h"

int main(void)
{
    int fd = open("/dev/mali0", O_RDWR | O_CLOEXEC);
    if (fd < 0) { perror("open"); return 1; }
    struct kbase_ioctl_version_check version = {
        .major = BASE_UK_VERSION_MAJOR, .minor = BASE_UK_VERSION_MINOR
    };
    if (ioctl(fd, KBASE_IOCTL_VERSION_CHECK, &version)) {
        perror("version"); close(fd); return 1;
    }
    printf("CSF API %u.%u\n", version.major, version.minor);
    struct kbase_ioctl_set_flags flags = { .create_flags = 0 };
    if (ioctl(fd, KBASE_IOCTL_SET_FLAGS, &flags)) {
        perror("set_flags"); close(fd); return 1;
    }
    size_t page = (size_t)sysconf(_SC_PAGESIZE);
    void *tracking = mmap(NULL, page, PROT_NONE, MAP_SHARED, fd,
                          BASE_MEM_MAP_TRACKING_HANDLE);
    if (tracking == MAP_FAILED) perror("tracking mmap");
    unsigned successful = 0;
    for (unsigned i = 0; i < 4; ++i) {
        union kbase_ioctl_mem_alloc allocation = { .in = {
            .va_pages = 16, .commit_pages = 16, .extension = 0,
            .flags = BASE_MEM_PROT_CPU_RD | BASE_MEM_PROT_CPU_WR |
                     BASE_MEM_PROT_GPU_RD | BASE_MEM_PROT_GPU_WR |
                     BASE_MEM_CACHED_CPU | BASE_MEM_SAME_VA
        }};
        if (ioctl(fd, KBASE_IOCTL_MEM_ALLOC, &allocation)) {
            perror("mem_alloc"); break;
        }
        printf("allocation %u: gpu_va=%#llx flags=%#llx\n", i,
               (unsigned long long)allocation.out.gpu_va,
               (unsigned long long)allocation.out.flags);
        void *mapping = mmap(NULL, 16 * page, PROT_READ | PROT_WRITE,
                             MAP_SHARED, fd, (off_t)allocation.out.gpu_va);
        if (mapping == MAP_FAILED) { perror("allocation mmap"); break; }
        ((volatile unsigned char *)mapping)[0] = 0x5a;
        printf("owned buffer mapped at %p, byte=%#x\n", mapping,
               ((volatile unsigned char *)mapping)[0]);
        munmap(mapping, 16 * page);
        ++successful;
    }
    if (tracking != MAP_FAILED) munmap(tracking, page);
    close(fd);
    printf("completed %u allocations; context closed\n", successful);
    return successful == 4 ? 0 : 1;
}
