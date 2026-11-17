/* Host-only shim for the 720 capacity adapter. Never copied into a kernel tree. */
#ifndef UC_CAPACITY_TEST_ENV_H
#define UC_CAPACITY_TEST_ENV_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

typedef uintptr_t vm_offset_t;
typedef unsigned long vm_size_t;
typedef int kern_return_t;
typedef int boolean_t;
#define VM_WIMG_WCOMB 0x8u
#define PAGE_SIZE 4096u
#define PAGE_MASK (PAGE_SIZE-1u)

/* ---- synthetic soft-DMA backing store ---- */
#define UC_EMU_REG_BYTES  0x2300u
#define UC_EMU_SCRATCH    (1024u*1024u)   /* >= UCD region + 4096 data */
#define UC_EMU_TABLE      2u*1024u*1024u

struct uc_emu {
    uint8_t regs[UC_EMU_REG_BYTES];
    uint8_t scratch[UC_EMU_SCRATCH];
    uint8_t table[4096];
    /* fault injection */
    unsigned fail_reg_after;      /* fail the Nth read_reg (1-based); 0 = never */
    unsigned fail_word_after;     /* fail the Nth read_word */
    unsigned fail_write_after;    /* fail the Nth write_word */
    unsigned reg_reads, mem_reads, mem_writes, rings;
    bool ring_returns_false;
    bool stop_after_ring;
    uint32_t stop_phase;          /* phase observed by stopped() */
    bool stopped_flag;
    uint32_t reg_override_offset;
    uint32_t reg_override_value;
    bool reg_override_on;
    /* controller behaviour */
    uint32_t dbr_clear_after;     /* polls before doorbell clears; 0 = never */
    uint32_t ocs_value;
    uint8_t  response[52];
    uint8_t  data_payload[8];
    bool     write_data_payload;
    bool     data_word_zero;      /* leave prefill untouched in first word */
};
extern struct uc_emu *uc_emu;

void uc_host_barrier(void);
uint64_t uc_host_now_us(void);
vm_offset_t uc_host_ml_io_map(vm_offset_t pa, vm_size_t size);
vm_offset_t uc_host_ml_io_map_unmappable(vm_offset_t pa, vm_size_t size, unsigned int flags);
boolean_t uc_host_pmap_valid_address(uint64_t pa);
int uc_host_pe_parse_boot_argn(const char *name, void *out, size_t size);
#endif
