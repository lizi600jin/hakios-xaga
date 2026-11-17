/* Host-only shim for the 721-C provider. Never copied into a kernel tree. */
#ifndef VR_HOST_TEST_ENV_H
#define VR_HOST_TEST_ENV_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

typedef uintptr_t vm_offset_t;
typedef unsigned long vm_size_t;
typedef int kern_return_t;
typedef int boolean_t;
#define VM_WIMG_WCOMB 0x8u
#define PAGE_SIZE 4096u
#define PAGE_MASK (PAGE_SIZE-1u)

/* One contiguous mapping: registers, table, then a 15-page scratch area, so the provider's
 * own offset arithmetic (pa - UR_LEASE_PA) is exercised unchanged.
 */
#define VR_EMU_REG_BYTES 0x2300u
#define VR_EMU_SCRATCH_PAGES 15u
#define VR_EMU_PAGES (2u + VR_EMU_SCRATCH_PAGES)
#define VR_EMU_PG_REG 0u
#define VR_EMU_PG_TABLE 1u
#define VR_EMU_PG_SCRATCH 2u

void  vr_host_barrier(void);
uint64_t vr_host_now_us(void);
vm_offset_t vr_host_map(vm_offset_t pa, vm_size_t size);
boolean_t   vr_host_pmap_valid(uint64_t pa);
int         vr_host_boot_arg(const char *name, void *out, size_t size);
void       *vr_host_base(void);
#endif
