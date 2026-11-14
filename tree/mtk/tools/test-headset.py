#!/usr/bin/env python3
"""Run the production accdet code with a PMIF/W1C model, without accessing hardware."""

from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
source = (ROOT / 'mtk/mt6895/drivers/audio/hakios.mt6895_headset.c').read_text()
source = re.sub(r'^#include .*\n', '', source, flags=re.M)

PREFIX = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uintptr_t vm_offset_t;
typedef size_t vm_size_t;
#define NSEC_PER_USEC 1000u
#define round_page(n) (((n) + 16383u) & ~(vm_size_t)16383u)
#define PMIF_MMIO_TEST 1
static unsigned lock_depth;
void hakios_spmi_lock(void) { assert(!lock_depth); lock_depth = 1; }
void hakios_spmi_unlock(void) { assert(lock_depth == 1); lock_depth = 0; }

static uint8_t regs[65536], original[65536], rdata, wdata;
static uint32_t bus_state, boot_mode;
static unsigned reads, writes, status_reads;
static bool hold_irq, stuck_bus;
static uint16_t fail_once;
static uint64_t model_now;
static uint64_t mach_absolute_time(void) { return model_now += 1000; }
static void nanoseconds_to_absolutetime(uint64_t ns, uint64_t *ticks) { *ticks = ns; }
static void clock_interval_to_deadline(uint32_t n, uint32_t scale, uint64_t *out) {
    *out = mach_absolute_time() + (uint64_t)n * scale;
}
static uint32_t model_read(uint32_t at) {
    assert(lock_depth == 1 && at >= 0x880 && at <= 0x8a8);
    if (at == 0x8a8) { status_reads++; return (stuck_bus ? 1u : bus_state) << 1; }
    assert(at == 0x894 && bus_state == 6);
    return rdata;
}
static void model_write(uint32_t at, uint32_t value) {
    assert(lock_depth == 1 && at >= 0x880 && at <= 0x8a8);
    if (at == 0x8a4) { assert(value == 1 && bus_state == 6); bus_state = 0; return; }
    assert(bus_state == 0);
    if (at == 0x884) { wdata = value; return; }
    assert(at == 0x880 && (value >> 30) == 3 && ((value >> 24) & 15) == 5);
    assert(((value >> 16) & 255) == 0);
    uint16_t addr = value & 0xffff;
    assert(addr != 0x014b && addr != 0x0240);
    if (value & (1u << 29)) {
        writes++;
        if (addr == fail_once) { fail_once = 0; return; }
        if (addr == 0x2323) { /* Direct W1C must not clear other top-level interrupts. */
            assert(wdata == 2 || wdata == 4);
            regs[addr] &= (uint8_t)~wdata;
        } else {
            regs[addr] = wdata;
            if (addr == 0x26aa && !hold_irq) regs[0x26a9] &= (uint8_t)~(wdata & 5);
        }
    } else {
        reads++;
        rdata = regs[addr];
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
    assert(strcmp(name, "headset") == 0 && size == 4);
    *(uint32_t *)out = boot_mode; return true;
}
static void delay(unsigned us) { assert(!lock_depth && us <= 2000); }
'''

SUFFIX = r'''
static void reset(unsigned requested) {
    memset(regs, 0, sizeof(regs));
    regs[MICBIAS1] = 0x82; regs[MICBIAS1_HIGH] = 0x8f;
    regs[MIC_MODE] = 0x40; regs[ACCDET_CTRL] = 0x20;
    regs[EINT_CONFIG] = 0x67; regs[NCP_PULLDOWN] = 0xc0;
    regs[0x26a4] = 0x0e; regs[AUD_TOP_RESET] = 0x08; regs[VUSB_HW_OP] = 0x85;
    regs[ACCDET_AB] = regs[ACCDET_EINT0_AB] = 0xff;
    regs[AUD_IRQ_STATUS] = 0x0e;
    memcpy(original, regs, sizeof(regs));
    quiesced = false; boot_mode = requested; mode = 0; saved_count = 0; poll_elapsed = 0;
    active = eint_sync = button_down = first_sample = micbias_disabled = false;
    cable = CABLE_NONE; detector = PLUG_OUT; inserted_ms = 0;
    reads = writes = status_reads = 0; bus_state = 0; hold_irq = stuck_bus = false; fail_once = 0;
}
static void restored(void) {
    assert(!active && saved_count == 0 && bus_state == 0);
    for (unsigned i = 0; i < sizeof(regs); i++) {
        if (i == AUD_IRQ_STATUS || i == ACCDET_IRQ || i == ACCDET_AB ||
            i == ACCDET_EINT0_AB || i == ACCDET_FSM) continue;
        assert(regs[i] == original[i]);
    }
}
static void event(uint8_t eint, uint8_t ab, uint8_t irq) {
    regs[ACCDET_EINT0_AB] = eint << 6; regs[ACCDET_AB] = ab << 6;
    regs[ACCDET_IRQ] |= irq; hakios_mt6895_headset_poll(50);
}
int main(void) {
    reset(0); hakios_mt6895_headset_init();
    assert(reads == 0 && writes == 0);
    reset(1); hakios_mt6895_headset_init();
    assert(reads == 129 && writes == 0 && !active);
    hakios_mt6895_headset_poll(1000); hakios_mt6895_headset_quiesce();
    assert(reads == 129 && writes == 0);
    /* Stale ACK recovery belongs to the shared transport; mode1 stays write-free at PMIC. */
    uint8_t unchanged = 0xa5;
    quiesced = false; bus_state = 6;
    assert(mt6368_read(ACCDET_AB, &unchanged) && unchanged == 0xff && bus_state == 0);
    reset(2); hakios_mt6895_headset_init();
    assert(active && !eint_sync && cable == CABLE_NONE);
    const struct { uint16_t reg; uint8_t value; } stock[] = {
        {0x268e,0xff},{0x268f,4},{0x2690,0xff},{0x2691,4},{0x2692,0xf0},{0x2693,0x81},
        {0x2694,0x41},{0x2697,0},{0x2698,8},{0x2699,0},{0x269a,8},{0x269d,0x20},
        {0x269e,0},{0x269f,0x44},{0x26a0,0},{0x26a3,0x35},{0x26a4,0x5e},
        {0x26a5,0x0e},{0x268c,0x67}
    };
    for (unsigned i = 0; i < sizeof(stock)/sizeof(stock[0]); i++)
        assert(regs[stock[i].reg] == stock[i].value);
    assert((regs[MICBIAS1_HIGH] & 0x30) == 0x30);
    unsigned before = reads;
    hakios_mt6895_headset_poll(20); assert(reads == before);
    assert(!configure(0x014b, 1, 1) && !configure(0x0240, 1, 1) && reads == before);
    event(3, 0, IRQ_ACCDET); assert(!eint_sync && cable == CABLE_NONE);
    event(0, 1, IRQ_EINT0 | IRQ_ACCDET); assert(eint_sync && cable == CABLE_NONE);
    hakios_mt6895_headset_poll(50); assert(cable == CABLE_MIC && detector == MIC_BIAS);
    event(0, 0, IRQ_ACCDET); assert(cable == CABLE_MIC && detector == HOOK_SWITCH && button_down);
    event(0, 1, IRQ_ACCDET); assert(cable == CABLE_MIC && detector == MIC_BIAS && !button_down);
    event(0, 3, IRQ_ACCDET); assert(cable == CABLE_MIC && detector == PLUG_OUT);
    event(0, 3, IRQ_EINT0); assert(!eint_sync && cable == CABLE_NONE);
    assert((regs[AUD_IRQ_STATUS] & 0x08) == 0x08);
    event(0, 0, IRQ_EINT0 | IRQ_ACCDET); hakios_mt6895_headset_poll(50);
    assert(cable == CABLE_NO_MIC && detector == HOOK_SWITCH && !button_down);
    event(0, 0, IRQ_ACCDET); assert(cable == CABLE_NO_MIC && !button_down);
    for (unsigned i = 0; i < 121; i++) hakios_mt6895_headset_poll(50);
    assert(micbias_disabled && !(regs[ACCDET_CTRL] & 1));
    event(3, 3, IRQ_EINT0); assert(!eint_sync && cable == CABLE_NONE);
    bus_state = 6; /* A keys read parked after RDATA, before VLDCLR. */
    unsigned before_quiesce = reads + writes + status_reads;
    unsigned saved_before_quiesce = saved_count;
    hakios_mt6895_headset_quiesce();
    assert(quiesced && saved_count == saved_before_quiesce);
    assert(before_quiesce == reads + writes + status_reads && bus_state == 6);
    hakios_mt6895_headset_poll(50);
    assert(before_quiesce == reads + writes + status_reads);
    quiesced = false; restore_configuration(); restored();
    reset(2); fail_once = 0x2690; hakios_mt6895_headset_init(); restored();
    reset(2); hakios_mt6895_headset_init(); hold_irq = true;
    event(0, 1, IRQ_EINT0); restored();
    assert((regs[ACCDET_IRQ_CLEAR] & 5) == 0);
    reset(2); hakios_mt6895_headset_init(); stuck_bus = true; before = status_reads;
    hakios_mt6895_headset_poll(50); assert(!active && saved_count > 0);
    assert(status_reads - before <= 2 * PMIF_SPIN_HARD_CAP + 4);
    stuck_bus = false; restore_configuration(); restored();
    puts("production accdet: mode1 no writes, 19 stock config bytes, EINT gating, mic button,"
         " unplug, 3-pole timer, W1C, worker rollback, zero-access post-stop and bounded failures passed");
    return 0;
}
'''


def main():
    with tempfile.TemporaryDirectory(prefix='hakios-headset-test-') as name:
        temp = Path(name)
        header = (ROOT / 'xnu/files/bsd/sys/hakios.pmif.h').read_text()
        shared = (ROOT / 'mtk/mt6895/drivers/core/hakios.mt6895_pmif.c').read_text()
        shared = re.sub(r'^#include .*\n', '', shared, flags=re.M)
        (temp / 'test.c').write_text(PREFIX + header + shared + source + SUFFIX)
        subprocess.run(['clang', '-std=c11', '-O1', '-g', '-Wall', '-Wextra',
                        '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                        str(temp / 'test.c'), '-o', str(temp / 'test')], check=True)
        result = subprocess.run([str(temp / 'test')], text=True, capture_output=True)
        if result.returncode:
            print(result.stdout)
            print(result.stderr)
            raise SystemExit(result.returncode)
        print(result.stdout.splitlines()[-1])


if __name__ == '__main__':
    main()
