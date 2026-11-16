#!/usr/bin/env python3
"""Run the production accdet code with a PMIF/W1C model, without accessing hardware."""

from pathlib import Path
import argparse
import json
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]

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
static uint16_t fail_mute_reg;
static unsigned fail_mute_count, mute_writes[2];
static uint8_t gain_read_high;
struct hp_io { unsigned reg, value, us; };
static struct hp_io hp_trace[4096];
static unsigned hp_trace_count, delay_calls;
static bool trace_hp, service_worker;
void hakios_mt6895_headset_poll(uint32_t elapsed_ms);
static void trace(unsigned reg, unsigned value, unsigned us) {
    if (!trace_hp || (!us && (reg < 0x2580 || reg > 0x260c))) return;
    assert(hp_trace_count < sizeof(hp_trace) / sizeof(hp_trace[0]));
    hp_trace[hp_trace_count++] = (struct hp_io){reg, value, us};
}
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
    assert(at == 0x880 && (value >> 30) == 3);
    if (((value >> 24) & 15) == 9) {
        assert(!(value & (1u << 29)) && (value & 0xffff) == 0x079e);
        reads++; rdata = 0xb8; bus_state = 6; return;
    }
    assert(((value >> 24) & 15) == 5);
    assert(((value >> 16) & 255) == 0);
    uint16_t addr = value & 0xffff;
    assert(addr != 0x014b && addr != 0x0240);
    if (value & (1u << 29)) {
        writes++;
        trace(addr, wdata, 0);
        if (addr == fail_once) { fail_once = 0; return; }
        if ((addr == 0x260b || addr == 0x260c) && wdata == 0x9f) {
            mute_writes[addr - 0x260b]++;
            if (addr == fail_mute_reg && fail_mute_count) {
                fail_mute_count--;
                return; /* A real failure of the low gain field, not just the high bits. */
            }
        }
        if (addr == 0x2323) { /* Direct W1C must not clear other top-level interrupts. */
            assert(wdata == 2 || wdata == 4);
            regs[addr] &= (uint8_t)~wdata;
        } else {
            /* Trial 705: ZCD_CON3 0x9f reads as 0x1f. Both HP gain fields are 5 bits.
             * This models that readback, without asserting the silicon's reserved-bit type. */
            regs[addr] = (addr == 0x260b || addr == 0x260c) ? wdata & 0x1f : wdata;
            if (addr == 0x26aa && !hold_irq) regs[0x26a9] &= (uint8_t)~(wdata & 5);
        }
    } else {
        reads++;
        rdata = regs[addr];
        if (addr == 0x260b || addr == 0x260c) rdata |= gain_read_high;
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
static void delay(unsigned us) {
    assert(!lock_depth && us <= 2000);
    delay_calls++;
    trace(0, 0, us);
    if (service_worker && us == 1000) {
        service_worker = false;
        hakios_mt6895_headset_poll(0);
    }
}
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
    regs[0x0009] = 0x68; regs[0x1cb2] = 1;
    regs[0x259f] = 0xa8; regs[0x25a1] = 0x3c; regs[0x25a2] = 0x70;
    memcpy(original, regs, sizeof(regs));
    quiesced = false; boot_mode = requested; mode = 0; saved_count = 0; poll_elapsed = 0;
    active = eint_sync = button_down = first_sample = micbias_disabled = false;
    cable = CABLE_NONE; detector = PLUG_OUT; inserted_ms = 0;
    reads = writes = status_reads = 0; bus_state = 0; hold_irq = stuck_bus = false; fail_once = 0;
    fail_mute_reg = 0; fail_mute_count = 0; mute_writes[0] = mute_writes[1] = 0;
    gain_read_high = 0;
    hp_owned = hp_on = false; hp_worker_state = 0;
    trace_hp = service_worker = false; hp_trace_count = delay_calls = 0;
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
static void check_trace(const struct hp_io *expected, unsigned count, bool exact) {
    assert(hp_trace_count >= count && (!exact || hp_trace_count == count));
    for (unsigned i = 0; i < count; i++) {
        if (memcmp(&hp_trace[i], &expected[i], sizeof(hp_trace[i]))) {
            fprintf(stderr, "vendor trace differs at %u: got %x=%x delay %u; expected %x=%x delay %u\n",
                    i, hp_trace[i].reg, hp_trace[i].value, hp_trace[i].us,
                    expected[i].reg, expected[i].value, expected[i].us);
            assert(false);
        }
    }
}
static void classify_3pole(void) {
    event(0, 3, IRQ_EINT0);
    assert(eint_sync && cable == CABLE_NONE && !hp_owned);
    event(0, 0, IRQ_ACCDET);
}
static void hp_tests(void) {
    reset(3); hakios_mt6895_headset_init();
    assert(active && !hp_owned && !hp_on);
    event(0, 3, IRQ_EINT0);
    assert(eint_sync && !hp_owned);
    trace_hp = true; hp_trace_count = 0;
    event(0, 0, IRQ_ACCDET);
    assert(cable == CABLE_NO_MIC && hp_owned && hp_on);
    check_trace(vendor_enable, sizeof(vendor_enable)/sizeof(vendor_enable[0]), true);
    assert(regs[0x2588] == 0xff && regs[0x258a] == 3 && regs[0x2589] == 0x3a);
    assert(regs[0x25a2] == 0x71 && regs[0x25a1] == 0x3c && regs[0x259f] == 0xa9);
    assert(regs[0x260b] == 0x1f && regs[0x260c] == 0x1f);
    for (unsigned i = 0; i < hp_trace_count; i++) {
        unsigned a = hp_trace[i].reg;
        assert(a != 0x2595 && a != 0x2596 && a != 0x2598 && a != 0x2599 &&
               a != 0x25a0 && a != 0x2609 && a != 0x260a);
    }
    trace_hp = false;
    for (unsigned i = 0; i < 121; i++) hakios_mt6895_headset_poll(50);
    assert(micbias_disabled && hp_on);
    event(0, 3, 0); assert(hp_on && eint_sync && cable == CABLE_NO_MIC);
    trace_hp = true; hp_trace_count = 0;
    event(3, 3, IRQ_EINT0);
    check_trace(vendor_disable, sizeof(vendor_disable)/sizeof(vendor_disable[0]), false);
    assert(!hp_owned && !hp_on && active && !eint_sync);
    for (unsigned i = 0; i < sizeof(hp_saved)/sizeof(hp_saved[0]); i++)
        assert(regs[hp_saved[i].addr] == original[hp_saved[i].addr]);
    trace_hp = false;
    classify_3pole(); assert(hp_on);
    service_worker = true; hakios_mt6895_headset_stop();
    assert(!hp_owned && !hp_on && (hp_worker_state & HP_WORKER_DONE)); restored();
    unsigned before = reads + writes + status_reads;
    hakios_mt6895_headset_poll(50); hakios_mt6895_headset_stop();
    hakios_mt6895_headset_quiesce(); hakios_mt6895_headset_poll(50);
    assert(before == reads + writes + status_reads);

    /* The four-pole classification takes the same HP path, without restarting on button edges. */
    reset(3); hakios_mt6895_headset_init();
    event(0, 3, IRQ_EINT0); event(0, 1, IRQ_ACCDET);
    assert(hp_on && cable == CABLE_MIC);
    event(0, 0, IRQ_ACCDET); assert(hp_on && button_down);
    event(0, 1, IRQ_ACCDET); assert(hp_on && !button_down);
    event(3, 3, IRQ_EINT0); assert(!hp_owned && !hp_on);
    restore_configuration(); restored();

    /* Drop a late enable write. Failed readback must shut down before restoring snapshots. */
    reset(3); hakios_mt6895_headset_init(); fail_once = 0x25a2;
    classify_3pole(); assert(!hp_on && !hp_owned); restored();
    /* One dropped shutdown write is recovered; an unrecoverable bus must not claim HP off. */
    reset(3); hakios_mt6895_headset_init(); classify_3pole(); assert(hp_on);
    fail_once = 0x258a; event(3, 3, IRQ_EINT0);
    assert(!hp_owned && !hp_on); restore_configuration(); restored();
    reset(3); hakios_mt6895_headset_init(); classify_3pole();
    stuck_bus = true; service_worker = true; hakios_mt6895_headset_stop();
    assert(!active && hp_owned && hp_on && regs[0x2588] == 0xff);
    stuck_bus = false; restore_configuration(); restored(); assert(!hp_owned && !hp_on);

    /* Refuse another active output, wrong PMIC, disabled VAUD18 or NLE gain control. */
    const unsigned blocked[] = {0x0009, 0x1cb2, 0x2595, 0x2593, 0x2588, 0x2589, 0x258a, 0x23d7};
    for (unsigned i = 0; i < sizeof(blocked)/sizeof(blocked[0]); i++) {
        reset(3);
        unsigned a = blocked[i]; regs[a] = a == 0x23d7 ? 0x24 : a < 0x2500 ? 0 : 1;
        memcpy(original, regs, sizeof(regs));
        hakios_mt6895_headset_init(); classify_3pole();
        assert(!hp_owned && !hp_on); restored();
    }
    /* Stop before worker entry prevents every PMIF transaction; a stalled worker wait is bounded. */
    reset(3); hakios_mt6895_headset_stop(); hakios_mt6895_headset_init();
    assert(!active && reads == 0 && writes == 0 && status_reads == 0);
    reset(3); hakios_mt6895_headset_init(); classify_3pole();
    before = delay_calls; hakios_mt6895_headset_stop();
    assert(delay_calls - before == 2000 && hp_on && hp_owned);
    before = reads + writes + status_reads;
    hakios_mt6895_headset_quiesce(); hakios_mt6895_headset_poll(50);
    assert(before == reads + writes + status_reads);
}

static void gain_and_shared_fault_tests(void) {
    for (unsigned channel = 0; channel < 2; channel++) {
        /* A real low-field error on either channel still aborts enable and rolls back. */
        reset(3); hakios_mt6895_headset_init(); fail_once = 0x260b + channel;
        classify_3pole(); assert(!hp_on && !hp_owned); restored();

        /* A dropped final mute is retried once, and both channels are attempted each time. */
        reset(3); hakios_mt6895_headset_init(); classify_3pole();
        fail_mute_reg = 0x260b + channel; fail_mute_count = 1;
        assert(hp_shutdown());
        assert(mute_writes[0] == 2 && mute_writes[1] == 2 && !hp_on && !hp_owned);
        restore_configuration(); restored();

        /* Persistent gain failure cannot hide the independently confirmed off shape.
         * It still returns failure and retains the snapshot, with exactly two attempts. */
        reset(3); hakios_mt6895_headset_init(); classify_3pole();
        fail_mute_reg = 0x260b + channel; fail_mute_count = 100;
        assert(!hp_shutdown());
        assert(mute_writes[0] == 2 && mute_writes[1] == 2 && fail_mute_count == 98);
        assert(!hp_on && hp_owned && regs[0x2588] == 0 && regs[0x258a] == 0);
        assert((regs[0x2589] & 15) == 0 && (regs[0x25a2] & 1) == 0);
        assert(regs[0x260b + channel] == 0x1e && regs[0x260c - channel] == 0x1f);
        fail_mute_count = 0; assert(hp_shutdown()); restore_configuration(); restored();
    }

    /* Undefined high readback bits are recorded but do not alter the gain-field result. */
    reset(3); hakios_mt6895_headset_init(); classify_3pole();
    gain_read_high = 0xa0;
    assert(hp_verify(true) && hp_shutdown());
    restore_configuration(); restored();

    const unsigned shared_regs[] = {0x259f, 0x25a1, 0x25a2};
    const unsigned owned_bits[] = {0x02, 0x01, 0x01};
    const unsigned kept_bits[] = {0x08, 0x04, 0x10};
    for (unsigned i = 0; i < 3; i++) {
        reset(3); hakios_mt6895_headset_init(); classify_3pole();
        regs[shared_regs[i]] ^= owned_bits[i];
        assert(!hp_verify_shared(false));
        assert(hp_shutdown()); restore_configuration(); restored();

        /* Never claim full restoration or overwrite a changed, unowned shared field. */
        reset(3); hakios_mt6895_headset_init(); classify_3pole();
        regs[shared_regs[i]] ^= kept_bits[i];
        assert(!hp_verify_shared(false) && !hp_shutdown());
        assert(hp_owned && !hp_on);
        assert((regs[shared_regs[i]] & kept_bits[i]) != (original[shared_regs[i]] & kept_bits[i]));
        regs[shared_regs[i]] ^= kept_bits[i];
        assert(hp_shutdown()); restore_configuration(); restored();
    }
}
int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
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
    hp_tests();
    gain_and_shared_fault_tests();
    puts("production accdet: mode1 no writes, 19 stock config bytes, EINT gating, mic button,"
         " unplug, 3-pole timer, W1C; HP matches independent vendor instruction trace,"
         " 5-bit gain readback, bounded stereo retries, shared-field corruption,"
         " repeat insertion, fault rollback, worker stop and zero-access post-stop passed");
    return 0;
}
'''

TRIAL705_MAIN = r'''
int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    reset(3);
    regs[0x259f] = 0x22; regs[0x25a1] = 0x55; regs[0x25a2] = 0x10;
    regs[0x260b] = regs[0x260c] = 0x1f;
    memcpy(original, regs, sizeof(regs));
    hakios_mt6895_headset_init(); classify_3pole();
    assert(hp_on && hp_owned);
    for (unsigned i = 0; i < 121; i++) hakios_mt6895_headset_poll(50);
    event(0, 3, 0); assert(hp_on && eint_sync && cable == CABLE_NO_MIC);
    service_worker = true; hakios_mt6895_headset_stop();
    assert(!hp_owned && !hp_on); restored();
    assert(regs[0x259f] == 0x22 && regs[0x25a1] == 0x55 && regs[0x25a2] == 0x10);
    unsigned before = reads + writes + status_reads;
    hakios_mt6895_headset_quiesce(); hakios_mt6895_headset_poll(50);
    assert(before == reads + writes + status_reads);
    puts("trial705 baseline replay: on, micbias timer, normal worker stop and restore passed");
    return 0;
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path,
                        default=ROOT / 'mtk/mt6895/drivers/audio/hakios.mt6895_headset.c')
    parser.add_argument('--header', type=Path, default=ROOT / 'xnu/files/bsd/sys/hakios.pmif.h')
    parser.add_argument('--trial705', action='store_true', help='Replay 705 initial HP bytes and normal return')
    parser.add_argument('--log-file', type=Path, help='Save the complete model stdout/stderr')
    args = parser.parse_args()
    source = re.sub(r'^#include .*\n', '', args.source.read_text(), flags=re.M)
    with tempfile.TemporaryDirectory(prefix='hakios-headset-test-') as name:
        temp = Path(name)
        header = args.header.read_text()
        shared = (ROOT / 'mtk/mt6895/drivers/core/hakios.mt6895_pmif.c').read_text()
        shared = re.sub(r'^#include .*\n', '', shared, flags=re.M)
        fixture = json.loads((ROOT / 'mtk/tools/fixtures/mt6368-hp-normal.json').read_text())
        golden = ''
        for phase in ('enable', 'disable'):
            golden += f'static const struct hp_io vendor_{phase}[] = {{\n'
            for event in fixture[f'{phase}_bus']:
                if event['op'] == 'write':
                    golden += '{%d,%d,0},\n' % (event['reg'], event['value'])
                else:
                    golden += '{0,0,%d},\n' % event['us']
            golden += '};\n'
        suffix = SUFFIX.split('static void hp_tests(void) {')[0] + TRIAL705_MAIN if args.trial705 else SUFFIX
        (temp / 'test.c').write_text(PREFIX + header + shared + source + golden + suffix)
        subprocess.run(['clang', '-std=c11', '-O1', '-g', '-Wall', '-Wextra',
                        '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                        str(temp / 'test.c'), '-o', str(temp / 'test')], check=True)
        result = subprocess.run([str(temp / 'test')], text=True, capture_output=True)
        if args.log_file:
            args.log_file.write_text(result.stdout + result.stderr)
        if result.returncode:
            print(result.stdout)
            print(result.stderr)
            raise SystemExit(result.returncode)
        print(result.stdout.splitlines()[-1])


if __name__ == '__main__':
    main()
