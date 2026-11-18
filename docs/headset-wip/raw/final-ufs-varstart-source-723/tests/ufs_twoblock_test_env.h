#ifndef UFS_TWOBLOCK_TEST_ENV_H
#define UFS_TWOBLOCK_TEST_ENV_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef uintptr_t vm_offset_t;
typedef size_t vm_size_t;

#define PAGE_SIZE 4096u
#define PAGE_MASK (PAGE_SIZE - 1u)
#define VM_WIMG_WCOMB 8u

extern unsigned long gPhysBase, gPhysSize;
bool PE_parse_boot_argn(const char *, void *, size_t);
bool pmap_valid_address(uint64_t);
vm_offset_t ml_io_map(vm_offset_t, vm_size_t);
vm_offset_t ml_io_map_unmappable(vm_offset_t, vm_size_t, unsigned int);
bool vr_host_reg_hook(uint16_t);
bool vr_host_ring_hook(void);
bool vr_host_read_word_hook(int, uint32_t);
bool vr_host_write_hook(int, uint32_t);
void vr_host_barrier(void);
uint64_t vr_host_now_us(void);

#endif
