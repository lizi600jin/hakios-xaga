#!/usr/bin/env python3
"""Exercise the production shared PMIF C code, including concurrency and failures."""
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
HEADER = ROOT / 'xnu/files/bsd/sys/hakios.pmif.h'
DRIVER = ROOT / 'mtk/mt6895/drivers/core/hakios.mt6895_pmif.c'

PREFIX = r'''
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <sched.h>
typedef uintptr_t vm_offset_t;
typedef size_t vm_size_t;
#define NSEC_PER_USEC 1000u
#define round_page(n) (((n) + 16383u) & ~(vm_size_t)16383u)
#define PMIF_MMIO_TEST 1
static pthread_mutex_t bus_lock = PTHREAD_MUTEX_INITIALIZER;
static _Thread_local unsigned lock_depth;
static unsigned lock_calls, unlock_calls;
static unsigned mmio_calls, gReads, gWrites, gAcks, sta_reads;
static unsigned map_calls, unmap_calls;
static bool map_fail, map_race;
static pthread_barrier_t map_barrier;
static uint32_t gSta, gRdata, gWdata, gAcc;
static uint8_t gMem[65536];
static bool gFailNextAcc, gIgnoreAck, gNeverComplete, late_idle;
static unsigned drop_acks;
static uint64_t model_now, clock_step = 1000, clock_calls, idle_until;
static uint64_t sta_times[10016];
static uint64_t late_at;
static uint64_t mach_absolute_time(void) {
    clock_calls++;
    uint64_t now = model_now; model_now += clock_step; return now;
}
static void nanoseconds_to_absolutetime(uint64_t ns, uint64_t *out) { *out = ns; }
static void clock_interval_to_deadline(uint32_t n, uint32_t scale, uint64_t *out) {
    *out = mach_absolute_time() + (uint64_t)n * scale;
}
void hakios_spmi_lock(void) {
    assert(lock_depth == 0); assert(pthread_mutex_lock(&bus_lock) == 0);
    lock_depth = 1; lock_calls++;
}
void hakios_spmi_unlock(void) {
    assert(lock_depth == 1); lock_depth = 0; unlock_calls++;
    assert(pthread_mutex_unlock(&bus_lock) == 0);
}
vm_offset_t ml_io_map(vm_offset_t address, vm_size_t size) {
    assert(lock_depth == 0 && address == 0x1c804000 && size == 0x8ff);
    unsigned n = __atomic_add_fetch(&map_calls, 1u, __ATOMIC_RELAXED);
    if (map_race) {
        int rc = pthread_barrier_wait(&map_barrier);
        assert(rc == 0 || rc == PTHREAD_BARRIER_SERIAL_THREAD);
    }
    return map_fail ? 0 : (vm_offset_t)(n * 0x4000u);
}
void ml_io_unmap(vm_offset_t address, vm_size_t size) {
    assert(lock_depth == 0 && address && size == 0x4000);
    __atomic_add_fetch(&unmap_calls, 1u, __ATOMIC_RELAXED);
}
static void set_fsm(uint32_t fsm) { gSta = (gSta & ~14u) | (fsm << 1); }
uint32_t pmif_test_rd(uint32_t at) {
    assert(lock_depth == 1); mmio_calls++;
    if (at == 0x8a8) {
        if (sta_reads < sizeof(sta_times)/sizeof(sta_times[0])) sta_times[sta_reads] = model_now;
        sta_reads++;
        if (late_idle && model_now >= late_at) set_fsm(6);
        return model_now < idle_until ? 4u : gSta;
    }
    assert(at == 0x894 && ((gSta >> 1) & 7) == 6); return gRdata;
}
void pmif_test_wr(uint32_t at, uint32_t value) {
    assert(lock_depth == 1); mmio_calls++;
    if (at == 0x8a4) {
        assert(value == 1 && ((gSta >> 1) & 7) == 6); gAcks++;
        if (gIgnoreAck) return;
        if (drop_acks) { drop_acks--; return; }
        late_idle = false; set_fsm(0); return;
    }
    assert(((gSta >> 1) & 7) == 0);
    if (at == 0x884) { gWdata = value; return; }
    assert(at == 0x880 && (value >> 30) == 3 && ((value >> 16) & 255) == 0);
    assert(((value >> 24) & 15) == 4 || ((value >> 24) & 15) == 5);
    gAcc = value;
    if (value & (1u << 29)) {
        gWrites++;
        if (gFailNextAcc) { gSta |= 1u << 18; set_fsm(4); gFailNextAcc = false; return; }
        assert((value & 0xffff) != 0x014b && (value & 0xffff) != 0x0240);
        gMem[value & 0xffff] = (uint8_t)gWdata;
    } else {
        gReads++; gRdata = gMem[value & 0xffff]; set_fsm(gNeverComplete ? 2 : 6);
    }
    sched_yield(); /* Encourage interleaving while a transfer is live. */
}
'''

TEST = r'''
static void reset_device(void) {
    assert(lock_depth == 0 && lock_calls == unlock_calls);
    gSta = gAcc = gRdata = gWdata = 0;
    gFailNextAcc = gIgnoreAck = gNeverComplete = late_idle = false;
    gReads = gWrites = gAcks = mmio_calls = sta_reads = drop_acks = 0;
    model_now = clock_calls = idle_until = late_at = 0; clock_step = 1000;
    memset(gMem, 0x5a, sizeof(gMem)); gMem[0x26b6] = 0xa7; gMem[0x240] = 4;
}
static void *init_worker(void *unused) {
    (void)unused; assert(pmif_init() == PMIF_OK); return NULL;
}
static void *transfer_worker(void *arg) {
    uintptr_t id = (uintptr_t)arg;
    uint32_t sid = id == 0 ? 4 : 5;
    uint32_t addr = id == 0 ? 0x1e : id == 1 ? 0x26b6 : 0x24e;
    for (unsigned n = 0; n < 400; n++) {
        uint8_t value = 0;
        if (id == 2) assert(pmif_write8(sid, addr, n & 0xff) == PMIF_OK);
        assert(pmif_read8(sid, addr, &value) == PMIF_OK);
        assert(value == (id == 0 ? 0x5a : id == 1 ? 0xa7 : (n & 0xff)));
    }
    assert(lock_depth == 0); return NULL;
}
int main(void) {
    uint8_t v = 0;
    pmif_status_t status;
    reset_device();
    assert(pmif_read8(5, 0x26b6, &v) == PMIF_E_DEAD);
    map_fail = true; assert(pmif_init() == PMIF_E_DEAD); map_fail = false;
    assert(pmif_init() == PMIF_OK);
    unsigned before = mmio_calls;
    assert(pmif_read8(3, 0x1e, &v) == PMIF_E_SID);
    assert(pmif_read8(261, 0x1e, &v) == PMIF_E_SID);
    assert(pmif_read8(5, 0x10000, &v) == PMIF_E_ARG);
    assert(pmif_read8(5, 0x26b6, NULL) == PMIF_E_ARG);
    assert(pmif_write8(5, 0x014b, 1) == PMIF_E_REG);
    assert(pmif_write8(5, 0x0240, 1) == PMIF_E_REG);
    assert(pmif_write8(5, 0x241, 256) == PMIF_E_ARG);
    assert(mmio_calls == before);
    puts("1 admit: rejected SID/address/value/null/forbidden writes perform no MMIO");
    assert(pmif_read8(5, 0x26b6, &v) == PMIF_OK && v == 0xa7);
    assert(gAcc == 0xc50026b6u && gAcks == 1 && PMIF_STA_FSM(gSta) == 0);
    puts("2 read: correct command, RDATA before ACK, post-ACK IDLE");
    reset_device(); v = 0x11; gNeverComplete = true;
    assert(pmif_read8(5, 0x26b6, &v) == PMIF_E_TIMEOUT && v == 0x11);
    puts("3 failed read: output is unchanged");
    reset_device(); set_fsm(6);
    assert(pmif_read8(5, 0x26b6, &v) == PMIF_OK && gAcks == 2 && PMIF_STA_FSM(gSta) == 0);
    puts("4 B3: stale FSM=6 ACK then successful transfer");
    reset_device();
    assert(pmif_write8(5, 0x241, 4) == PMIF_OK);
    assert(gAcc == 0xe5000241u && gWrites == 1 && gAcks == 0 && gMem[0x241] == 4);
    puts("5 B4: bit29 set; write completes at IDLE, without read/ACK");
    reset_device(); gFailNextAcc = true;
    assert(pmif_write8(5, 0x241, 4) == PMIF_E_NAK && model_now < 100000);
    puts("6 NAK: error observed promptly");
    reset_device(); v = 0x5c; gIgnoreAck = true;
    status = pmif_read8(5, 0x26b6, &v);
    assert((status == PMIF_E_ACK || status == PMIF_E_STUCK) && v == 0x5c && gAcks == 2);
    puts("7 ignored ACK: finite retry, failed status and unchanged output");
    reset_device();
    assert(!pmif_pmic_reg_writable(0x240));
    assert(pmif_read8(5, 0x240, &v) == PMIF_OK && v == 4);
    before = mmio_calls;
    assert(pmif_rd(0x900) == 0xffffffffu && pmif_rd(0x8a4) == 0xffffffffu);
    pmif_wr(0x940, 1); pmif_wr(0x894, 1); assert(mmio_calls == before);
    puts("8 whitelist: 0x240 readable only; exact directional SWINF_2 offset guard");
    uint32_t sta_before, sta_after;
    pmif_last_sta(&sta_before, &sta_after);
    assert(PMIF_STA_FSM(sta_before) == 0 && PMIF_STA_FSM(sta_after) == 0);
    puts("9 last_sta: locked transaction snapshot");
    assert(lock_depth == 0 && lock_calls == unlock_calls);
    puts("10 lock: every success/failure path releases the shared lock");

    /* IDLE consumes 60ms, RX is stuck: no second 100ms allowance. */
    reset_device(); idle_until = 60000000; gNeverComplete = true; v = 0x6d;
    assert(pmif_read8(5, 0x26b6, &v) == PMIF_E_TIMEOUT && v == 0x6d && gReads == 1);
    assert(model_now >= 100000000 && model_now <= 100010000);
    /* Failed samples after the first preflight use at least the 10us cadence. */
    for (unsigned n = 2; n < 100 && n < sta_reads; n++) assert(sta_times[n]-sta_times[n-1] >= 10000);
    puts("11 budget: IDLE+RX+cleanup share 100ms; failed polls use 10us cadence");

    /* Initial IDLE timeout can become FSM=6 only at cleanup, on either path. */
    for (unsigned write = 0; write < 2; write++) {
        reset_device(); set_fsm(2); late_idle = true; late_at = 100000000; v = 0xa5;
        status = write ? pmif_write8(5, 0x241, 4) : pmif_read8(5, 0x26b6, &v);
        assert(status != PMIF_OK && gAcks == 1 && PMIF_STA_FSM(gSta) == 0);
        assert(gReads == 0 && gWrites == 0 && v == 0xa5 && model_now <= 100010000);
    }
    puts("12 late initial-IDLE FSM=6: read and write failure both ACK, no new budget");
    reset_device(); drop_acks = 1; v = 0x7a;
    assert(pmif_read8(5, 0x26b6, &v) != PMIF_OK && v == 0x7a && gAcks == 2 && PMIF_STA_FSM(gSta) == 0);
    assert(model_now <= 100010000);
    puts("13 dropped first ACK: cleanup retires it; failed read still does not publish");
    reset_device(); clock_step = 0; set_fsm(2); v = 0x19;
    assert(pmif_read8(5, 0x26b6, &v) == PMIF_E_TIMEOUT && v == 0x19);
    assert(clock_calls <= PMIF_SPIN_HARD_CAP + 20 && sta_reads <= 4);
    /* Also check the shared sample ceiling independent of the clock deadline. */
    reset_device(); clock_step = 0; set_fsm(2);
    hakios_spmi_lock();
    struct pmif_budget budget = pmif_budget_begin(); budget.polls = PMIF_SPIN_HARD_CAP - 1;
    assert(!pmif_wait(&budget, 0) && budget.polls == PMIF_SPIN_HARD_CAP);
    assert(sta_reads == 1); hakios_spmi_unlock();
    puts("14 frozen clock and poll ceiling: both pause and transaction terminate");

    reset_device(); pthread_t workers[3];
    for (uintptr_t n = 0; n < 3; n++) assert(pthread_create(&workers[n], NULL, transfer_worker, (void *)n) == 0);
    for (unsigned n = 0; n < 3; n++) assert(pthread_join(workers[n], NULL) == 0);
    assert(gReads == 1200 && gWrites == 400 && gAcks == 1200 && PMIF_STA_FSM(gSta) == 0);
    assert(lock_calls == unlock_calls && lock_depth == 0);
    puts("15 concurrent SID4 keys/SID5 headset/GPU: 1600 transfers, no interleaved transaction");

    /* Both callers map outside the lock; one winner and one discarded mapping. */
    reset_device(); pmif_base = 0; before = unmap_calls; map_race = true;
    assert(pthread_barrier_init(&map_barrier, NULL, 2) == 0);
    for (unsigned n = 0; n < 2; n++) assert(pthread_create(&workers[n], NULL, init_worker, NULL) == 0);
    for (unsigned n = 0; n < 2; n++) assert(pthread_join(workers[n], NULL) == 0);
    map_race = false; assert(pthread_barrier_destroy(&map_barrier) == 0);
    assert(pmif_base != 0 && unmap_calls == before + 1 && lock_calls == unlock_calls);
    puts("16 first-map race: no allocation/unmap under spin lock; loser mapping freed");
    puts("all 10 baseline PMIF state-machine checks and 6 integration checks passed");
    return 0;
}
'''


def main():
    source = re.sub(r'^#include .*\n', '', DRIVER.read_text(), flags=re.M)
    with tempfile.TemporaryDirectory(prefix='hakios-shared-pmif-test-') as name:
        temp = Path(name)
        (temp / 'test.c').write_text(PREFIX + '\n' + HEADER.read_text() + '\n' + source + TEST)
        subprocess.run(['clang', '-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Wpedantic',
                        '-Wshadow', '-Wconversion', '-Werror', '-pthread',
                        '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                        str(temp / 'test.c'), '-o', str(temp / 'test')], check=True)
        subprocess.run([str(temp / 'test')], check=True, timeout=20)
    subprocess.run(['python3', str(ROOT / 'mtk/tools/test-headset.py')], check=True, timeout=30)


if __name__ == '__main__':
    main()
