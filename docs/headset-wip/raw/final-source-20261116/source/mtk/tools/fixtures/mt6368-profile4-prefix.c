#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
typedef uintptr_t vm_offset_t;
typedef size_t vm_size_t;
#define NSEC_PER_USEC 1000u
#define round_page(n) (((n) + 16383u) & ~(vm_size_t)16383u)
#define PMIF_MMIO_TEST 1
static unsigned lock_depth, boot_mode, boot_arg_mask;
static uint32_t model_trim[4];
static uint32_t model_tone;
static bool model_tone_present;
static uint32_t model_tone_gain;
static bool model_tone_gain_present;
static uint8_t model_regs[2][65536], model_original[2][65536], rdata, wdata;
static unsigned bus_state, error_samples, status_reads, pmic_reads, profile_writes, all_writes;
static unsigned fail_write_no, fail_sid, fail_reg, fail_phase;
static bool stuck_bus, trace_bus, service_worker, corrupt_off, corrupt_gain;
static unsigned stop_write_no, hp_down_writes;
static uint64_t model_now;
static unsigned model_phase(void);
static bool model_off_read(void);
static void model_request_stop(void);
static uint8_t model_defined(uint32_t sid, uint16_t reg);
static bool model_profile_reg(uint32_t sid, uint16_t reg);
static void model_seed(void);
void hakios_mt6895_headset_poll(uint32_t elapsed_ms);

static int quiet_printf(const char *format, ...) {
    va_list args; va_start(args, format);
    int result = vfprintf(stderr, format, args);
    va_end(args); return result;
}
#define printf quiet_printf
void hakios_spmi_lock(void) { assert(!lock_depth); lock_depth = 1; }
void hakios_spmi_unlock(void) { assert(lock_depth == 1); lock_depth = 0; }
static uint64_t mach_absolute_time(void) { return model_now += 1000; }
static void nanoseconds_to_absolutetime(uint64_t ns, uint64_t *ticks) { *ticks = ns; }
static void clock_interval_to_deadline(uint32_t n, uint32_t scale, uint64_t *out) {
    *out = mach_absolute_time() + (uint64_t)n * scale;
}
static uint32_t model_read(uint32_t at) {
    assert(lock_depth == 1 && at >= 0x880 && at <= 0x8a8);
    if (at == 0x8a8) {
        status_reads++;
        if (error_samples) { error_samples--; return (1u << 18) | (bus_state << 1); }
        return (stuck_bus ? 1u : bus_state) << 1;
    }
    assert(at == 0x894 && bus_state == 6);
    return rdata;
}
static void model_write(uint32_t at, uint32_t value) {
    assert(lock_depth == 1 && at >= 0x880 && at <= 0x8a8);
    if (at == 0x8a4) { assert(value == 1 && bus_state == 6); bus_state = 0; return; }
    assert(bus_state == 0);
    if (at == 0x884) { wdata = (uint8_t)value; return; }
    assert(at == 0x880 && (value >> 30) == 3 && ((value >> 16) & 255) == 0);
    unsigned sid = (value >> 24) & 15;
    assert(sid == 5 || sid == 9);
    uint16_t reg = value & 0xffff;
    assert(reg != 0x014b && reg != 0x0240);
    unsigned bank = sid == 9;
    uint8_t defined = model_defined(sid, reg);
    if (value & (1u << 29)) {
        all_writes++;
        if (sid == 9) {
            assert(reg == 0x079e);
            assert((wdata & 0xfb) == (model_regs[1][reg] & 0xfb));
        }
        if (model_profile_reg(sid, reg)) {
            profile_writes++;
            if (model_phase() == 2 && (reg == 0x2588 || reg == 0x258a)) hp_down_writes++;
            if (profile_writes == fail_write_no ||
                (sid == fail_sid && reg == fail_reg && (!fail_phase || model_phase() == fail_phase))) {
                error_samples = 1;
                return; /* A PMIF NAK, including writes whose data would be unchanged. */
            }
            /* Removing any upstream rail/source while HP-off is unconfirmed is forbidden. */
            bool removing = (reg == 0x230c && (wdata & 0x66)) ||
                (reg == 0x2537 && !(wdata & 1)) ||
                (sid == 9 && !(wdata & 4)) || (reg == 0x25a3 && !(wdata & 0x15));
            if (model_phase() >= 2 && removing) {
                assert(model_regs[0][0x2588] == 0 && model_regs[0][0x258a] == 0);
                assert(!(model_regs[0][0x2589] & 15) && !(model_regs[0][0x25a2] & 1));
            }
            if (trace_bus) fprintf(stdout, "E %u W %u %04x %02x\n", model_phase(), sid, reg, wdata & defined);
        }
        if (reg == 0x2323) {
            model_regs[bank][reg] &= (uint8_t)~wdata;
        } else {
            model_regs[bank][reg] = wdata & defined;
            if (reg == 0x26aa) model_regs[0][0x26a9] &= (uint8_t)~(wdata & 5);
        }
        if (stop_write_no && profile_writes == stop_write_no) model_request_stop();
    } else {
        pmic_reads++;
        rdata = model_regs[bank][reg] | (uint8_t)~defined;
        if (corrupt_off && reg == 0x2588 && model_off_read()) rdata |= 1;
        if (corrupt_gain && (reg == 0x260b || reg == 0x260c)) rdata ^= 1;
        bus_state = 6;
    }
}
vm_offset_t ml_io_map(vm_offset_t address, vm_size_t size) {
    assert(!lock_depth && address == 0x1c804000 && size == 0x8ff); return 1;
}
void ml_io_unmap(vm_offset_t address, vm_size_t size) { (void)address; (void)size; assert(!lock_depth); }
uint32_t pmif_test_rd(uint32_t at) { return model_read(at); }
void pmif_test_wr(uint32_t at, uint32_t value) { model_write(at, value); }
static bool PE_parse_boot_argn(const char *name, void *out, size_t size) {
    assert(size == 4);
    if (strcmp(name, "headset") == 0) { *(uint32_t *)out = boot_mode; return true; }
    if (strcmp(name, "hp_tone") == 0 && model_tone_present) {
        *(uint32_t *)out = model_tone; return true;
    }
    if (strcmp(name, "hp_tone_gain") == 0 && model_tone_gain_present) {
        *(uint32_t *)out = model_tone_gain; return true;
    }
    const char *names[] = {"hp_trim_l", "hp_trim_r", "hp_fine_l", "hp_fine_r"};
    for (unsigned i = 0; i < 4; i++) {
        if (strcmp(name, names[i]) == 0 && (boot_arg_mask & (1u << i))) {
            *(uint32_t *)out = model_trim[i]; return true;
        }
    }
    return false;
}
static void delay(unsigned us) {
    assert(!lock_depth && us <= 2000);
    if (trace_bus) fprintf(stdout, "E %u D %u\n", model_phase(), us);
    if (service_worker && us == 1000) {
        service_worker = false;
        hakios_mt6895_headset_poll(0);
    }
}
