static unsigned model_phase(void) { return (unsigned)p4.phase; }
static bool model_off_read(void) { return p4.phase == P4_HP_CLOSING && p4.down_pc >= P4_HP_DOWN_END; }
static void model_request_stop(void) { __atomic_fetch_or(&hp_worker_state, HP_STOP_REQUESTED, __ATOMIC_ACQ_REL); }
static void reset_model(void) {
    memset(model_regs, 0, sizeof(model_regs));
    model_seed();
    memcpy(model_original, model_regs, sizeof(model_regs));
    boot_mode = 4; boot_arg_mask = 15;
    model_trim[0] = 3; model_trim[1] = 7; model_trim[2] = 1; model_trim[3] = 2;
    model_tone = 0; model_tone_present = false;
    model_tone_gain = 30; model_tone_gain_present = false;
    mode = 0; quiesced = active = eint_sync = button_down = first_sample = micbias_disabled = false;
    cable = CABLE_NONE; detector = PLUG_OUT; inserted_ms = poll_elapsed = saved_count = 0;
    hp_owned = hp_on = false; hp_worker_state = 0;
    p4 = (__typeof__(p4)){0};
    bus_state = error_samples = pmic_reads = profile_writes = all_writes = status_reads = 0;
    fail_write_no = fail_sid = fail_reg = fail_phase = stop_write_no = hp_down_writes = 0;
    stuck_bus = trace_bus = service_worker = corrupt_off = corrupt_gain = false;
}
static void ready_direct(void) {
    reset_model(); mode = 4;
    assert(p4_configure_arguments());
    assert(pmif_init() == PMIF_OK);
}
static void check_profile_original(void) {
    assert(!p4.owned && !p4.on && p4.phase == P4_NONE);
    for (unsigned i = 0; i < P4_REG_COUNT; i++) {
        struct p4_register *r = &p4_registers[i]; unsigned bank = r->sid == 9;
        assert((model_regs[bank][r->reg] & r->defined) == (model_original[bank][r->reg] & r->defined));
    }
}
static void event(uint8_t eint, uint8_t ab, uint8_t irq) {
    model_regs[0][ACCDET_EINT0_AB] = eint << 6; model_regs[0][ACCDET_AB] = ab << 6;
    model_regs[0][ACCDET_IRQ] |= irq; hakios_mt6895_headset_poll(50);
}
static void begin_worker(void) {
    hakios_mt6895_headset_init(); assert(active);
    event(0, 3, IRQ_EINT0);
}
static unsigned cases;
int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    ready_direct(); trace_bus = true;
    assert(p4_enable() && p4.on && p4.owned);
    unsigned up_writes = profile_writes;
    assert(p4_shutdown()); trace_bus = false;
    unsigned close_writes = profile_writes - up_writes;
    check_profile_original(); cases++;

    /* Every actual up bus write can report NAK, including repeated/pulse writes. */
    for (unsigned n = 1; n <= up_writes; n++) {
        ready_direct(); fail_write_no = n;
        assert(!p4_enable());
        bool closed = p4_shutdown();
        if (!closed) fprintf(stderr, "UP FAULT n=%u phase=%u pc=%u restore=%u\n", n, p4.phase, p4.down_pc, p4.restore_pc);
        assert(closed); check_profile_original(); cases++;
    }
    /* Every close/restore bus write can report one transient NAK and resume. */
    for (unsigned n = 1; n <= close_writes; n++) {
        ready_direct(); assert(p4_enable()); fail_write_no = profile_writes + n;
        assert(p4_shutdown()); check_profile_original(); cases++;
    }
    /* Both final gain writes are attempted even when the left channel fails. */
    ready_direct(); assert(p4_enable()); fail_sid = 5; fail_reg = 0x260b; fail_phase = P4_SUPPLY_CLOSING;
    assert(!p4_shutdown() && p4.owned && !p4.on);
    assert(model_regs[0][0x260c] == 0x1f);
    fail_sid = 0; assert(p4_shutdown()); check_profile_original(); cases++;

    /* A retained HP-off diagnostic never authorizes removal of upstream supplies. */
    ready_direct(); assert(p4_enable()); corrupt_off = true;
    assert(!p4_shutdown() && p4.owned && p4.phase == P4_HP_CLOSING);
    assert((model_regs[1][0x079e] & 4) && (model_regs[0][0x25a3] & 0x15) == 0x15);
    unsigned previous_hp_writes = hp_down_writes;
    corrupt_off = false; assert(p4_shutdown());
    assert(hp_down_writes == previous_hp_writes); check_profile_original(); cases++;

    /* Supply-close failures resume after HP close, rather than rerunning an analog ramp. */
    ready_direct(); assert(p4_enable()); fail_sid = 5; fail_reg = 0x23a6; fail_phase = P4_SUPPLY_CLOSING;
    assert(!p4_shutdown() && p4.phase == P4_SUPPLY_CLOSING && p4.owned && !p4.on);
    previous_hp_writes = hp_down_writes; fail_sid = 0;
    assert(p4_shutdown() && hp_down_writes == previous_hp_writes); check_profile_original(); cases++;

    /* Snapshot restoration retains progress and cannot claim success on a lost field. */
    ready_direct(); assert(p4_enable()); fail_sid = 5; fail_reg = 0x259f; fail_phase = P4_RESTORING;
    assert(!p4_shutdown() && p4.phase == P4_RESTORING && p4.owned);
    assert(!(model_regs[1][0x079e] & 4)); fail_sid = 0;
    assert(p4_shutdown()); check_profile_original(); cases++;

    ready_direct(); assert(p4_enable()); model_regs[0][0x25a1] ^= 4;
    assert(!p4_shutdown() && p4.owned && !(model_regs[0][0x25a1] & 4));
    model_regs[0][0x25a1] ^= 4;
    assert(p4_shutdown()); check_profile_original(); cases++;

    /* Validate caller parameters before any PMIF access, not merely before the first write. */
    for (unsigned i = 0; i < 4; i++) {
        reset_model(); boot_arg_mask &= ~(1u << i); hakios_mt6895_headset_init();
        assert(pmic_reads == 0 && all_writes == 0 && status_reads == 0); cases++;
        reset_model(); model_trim[i] = i < 2 ? 32 : 8; hakios_mt6895_headset_init();
        assert(pmic_reads == 0 && all_writes == 0 && status_reads == 0); cases++;
    }
    /* An existing stream, another analog output, or unexpected clock mode is never taken over. */
    const uint16_t refused[] = {0x0009,0x1cb2,0x2595,0x2593,0x23d7,0x2588,0x258a,
        0x2589,0x2388,0x2389,0x238e,0x23c8,0x2488,0x2537,0x25a2,0x25a0,0x25a3,0x079e};
    for (unsigned i = 0; i < sizeof(refused)/sizeof(refused[0]); i++) {
        ready_direct(); unsigned reg = refused[i], bank = reg == 0x079e;
        model_regs[bank][reg] = reg == 0x0009 || reg == 0x1cb2 || reg == 0x25a0 ? 0 :
            reg == 0x23d7 ? 0x24 : reg == 0x23c8 ? 0x80 : reg == 0x25a2 ? 0x30 : 1;
        assert(!p4_enable() && !p4.owned && profile_writes == 0); cases++;
    }
    /* No synthesis of a default trim: a different set of explicit arguments reaches both channels. */
    ready_direct(); p4.trim_l = 31; p4.trim_r = 0; p4.fine_l = 7; p4.fine_r = 6;
    assert(p4_enable()); assert(model_regs[0][0x258e] == 255 && model_regs[0][0x258f] == 6);
    assert(p4_shutdown()); check_profile_original(); cases++;

    /* Full classification and normal-stop handshake exercise the production integration. */
    reset_model(); begin_worker(); event(0, 0, IRQ_ACCDET);
    assert(p4.on && p4.owned); service_worker = true; hakios_mt6895_headset_stop();
    check_profile_original(); assert(!active && saved_count == 0 && (hp_worker_state & HP_WORKER_DONE));
    unsigned io = all_writes + pmic_reads + status_reads;
    hakios_mt6895_headset_quiesce(); hakios_mt6895_headset_poll(50); assert(p4_shutdown());
    assert(io == all_writes + pmic_reads + status_reads); cases++;

    /* A failed close retains ACCDET configuration until the profile has restored. */
    reset_model();
    begin_worker(); event(0, 0, IRQ_ACCDET); assert(p4.on);
    fail_sid = 5; fail_reg = 0x23a6; fail_phase = P4_SUPPLY_CLOSING;
    event(3, 3, IRQ_EINT0);
    assert(p4.owned && !active && saved_count > 0 && (model_regs[0][0x1cb2] & 1));
    fail_sid = 0; restore_configuration();
    assert(!p4.owned && saved_count == 0 && model_regs[0][0x1cb2] == 1); cases++;

    /* Valid low gain bits must read back correctly even when reserved bits read as ones. */
    ready_direct(); assert(p4_enable()); corrupt_gain = true;
    assert(!p4_shutdown() && p4.owned);
    corrupt_gain = false; assert(p4_shutdown()); check_profile_original(); cases++;

    /* A dead bus has a bounded failure path and retains the snapshot for recovery. */
    ready_direct(); assert(p4_enable()); stuck_bus = true;
    unsigned before_status = status_reads, before_writes = all_writes;
    assert(!p4_shutdown() && p4.owned && p4.on);
    assert(status_reads - before_status < 5000 && all_writes == before_writes);
    stuck_bus = false; assert(p4_shutdown()); check_profile_original(); cases++;

    /* Four-pole classification does not enter the profile that uses explicit three-pole trim. */
    reset_model(); begin_worker(); event(0, 1, IRQ_ACCDET);
    assert(cable == CABLE_MIC && !p4.owned && !p4.on);
    service_worker = true; hakios_mt6895_headset_stop(); assert(saved_count == 0); cases++;

    /* Reclassification also closes an already started three-pole profile. */
    reset_model(); begin_worker(); event(0, 0, IRQ_ACCDET); assert(p4.on);
    event(0, 1, IRQ_ACCDET);
    assert(!p4.on && !p4.owned && !active && saved_count == 0); cases++;

    ready_direct(); model_request_stop();
    io = all_writes + pmic_reads + status_reads;
    assert(!p4_enable() && !p4.owned && io == all_writes + pmic_reads + status_reads); cases++;

    reset_model(); hakios_mt6895_headset_stop(); hakios_mt6895_headset_init();
    assert(!pmic_reads && !all_writes && !status_reads); cases++;
    ready_direct(); assert(p4_enable()); hakios_mt6895_headset_quiesce();
    io = all_writes + pmic_reads + status_reads; assert(!p4_shutdown());
    assert(io == all_writes + pmic_reads + status_reads); cases++;

    reset_model(); begin_worker(); stop_write_no = profile_writes + 10; event(0, 0, IRQ_ACCDET);
    assert(!p4.owned && !p4.on && !active); cases++;

    /* Explicitly disabling the experiment preserves the original profile's writes. */
    ready_direct(); model_tone_present = true; model_tone = 0;
    assert(p4_configure_arguments() && p4_enable());
    assert(!p4.tone && profile_writes == up_writes);
    assert(p4_shutdown()); check_profile_original(); cases++;

    /* Unknown experiment selection and missing trim fail before any PMIF operation. */
    reset_model(); model_tone_present = true; model_tone = 2;
    hakios_mt6895_headset_init();
    assert(!pmic_reads && !all_writes && !status_reads); cases++;
    for (unsigned i = 0; i < 4; i++) {
        reset_model(); model_tone_present = true; model_tone = 1;
        boot_arg_mask &= ~(1u << i); hakios_mt6895_headset_init();
        assert(!pmic_reads && !all_writes && !status_reads); cases++;
    }

    /* The optional experiment waits muted, emits exactly three gated slots, then restores. */
    reset_model(); model_tone_present = true; model_tone = 1;
    begin_worker(); event(0, 0, IRQ_ACCDET);
    assert(p4.on && p4.tone_phase == P4_TONE_WAIT && p4.tone_pulses == 0);
    assert(model_regs[0][0x23c8] == 0xc0 && model_regs[0][0x260b] == 30 && model_regs[0][0x260c] == 30);
    model_now = p4.tone_deadline - 1000000;
    event(0, 0, 0); assert(p4.tone_phase == P4_TONE_WAIT);
    for (unsigned pulse = 1; pulse <= 3; pulse++) {
        model_now = p4.tone_deadline; event(0, 0, 0);
        assert(p4.on && p4.tone_phase == P4_TONE_ON && p4.tone_pulses == pulse);
        assert(model_regs[0][0x23c8] == 0x80 && model_regs[0][0x260b] == 30 && model_regs[0][0x260c] == 30);
        model_now = p4.tone_deadline; event(0, 0, 0);
        if (pulse < 3) {
            assert(p4.on && p4.tone_phase == P4_TONE_GAP && model_regs[0][0x23c8] == 0xc0);
        }
    }
    assert(p4.tone_pulses == 3 && p4.tone_phase == P4_TONE_DONE && !active && !saved_count);
    check_profile_original();
    io = all_writes + pmic_reads + status_reads;
    for (unsigned i = 0; i < 10; i++) hakios_mt6895_headset_poll(50);
    assert(io == all_writes + pmic_reads + status_reads); cases++;

    /* NAK on every new tone write must close the path and retain correct ownership ordering. */
    for (unsigned n = 1; n <= 11; n++) {
        reset_model(); model_tone_present = true; model_tone = 1;
        fail_write_no = up_writes + n;
        begin_worker(); event(0, 0, IRQ_ACCDET);
        for (unsigned edge = 0; edge < 6 && active && p4.on; edge++) {
            model_now = p4.tone_deadline; event(0, 0, 0);
        }
        assert(!active && !saved_count); check_profile_original(); cases++;
    }

    /* STOP during an audible slot closes immediately; no later slot is replayed. */
    reset_model(); model_tone_present = true; model_tone = 1;
    begin_worker(); event(0, 0, IRQ_ACCDET);
    model_now = p4.tone_deadline; event(0, 0, 0); assert(p4.tone_phase == P4_TONE_ON);
    service_worker = true; hakios_mt6895_headset_stop();
    assert(!active && !saved_count && p4.tone_pulses == 1); check_profile_original(); cases++;

    /* A false gain readback aborts a tone transition instead of accepting a named setting. */
    reset_model(); model_tone_present = true; model_tone = 1;
    begin_worker(); event(0, 0, IRQ_ACCDET);
    model_regs[0][0x260b] ^= 1; model_now = p4.tone_deadline; event(0, 0, 0);
    assert(!active && !saved_count); check_profile_original(); cases++;

    /* Quiesce after arming permits neither a gate write nor a cleanup bus transaction. */
    reset_model(); model_tone_present = true; model_tone = 1;
    begin_worker(); event(0, 0, IRQ_ACCDET); hakios_mt6895_headset_quiesce();
    io = all_writes + pmic_reads + status_reads; model_now = p4.tone_deadline;
    hakios_mt6895_headset_poll(50); assert(p4_tone_poll());
    assert(io == all_writes + pmic_reads + status_reads); cases++;

    /* Only the two reviewed experiment gain codes are accepted, before PMIF access. */
    const uint32_t bad_tone_gain[] = {0, 29, 32, UINT32_MAX};
    for (unsigned i = 0; i < sizeof(bad_tone_gain)/sizeof(bad_tone_gain[0]); i++) {
        reset_model(); model_tone_present = true; model_tone = 1;
        model_tone_gain_present = true; model_tone_gain = bad_tone_gain[i];
        hakios_mt6895_headset_init();
        assert(!pmic_reads && !all_writes && !status_reads); cases++;
    }

    /* The lower-output selection retains the same gate timing and closes after slot3. */
    reset_model(); model_tone_present = true; model_tone = 1;
    model_tone_gain_present = true; model_tone_gain = 31;
    begin_worker(); event(0, 0, IRQ_ACCDET);
    assert(p4.on && p4.tone_gain == 31 && p4.tone_phase == P4_TONE_WAIT);
    for (unsigned edge = 0; edge < 6; edge++) {
        model_now = p4.tone_deadline; event(0, 0, 0);
        if (edge < 5) {
            assert(p4.on && model_regs[0][0x260b] == 31 && model_regs[0][0x260c] == 31);
            assert(model_regs[0][0x23c8] == (edge % 2 == 0 ? 0x80 : 0xc0));
        }
    }
    assert(!active && !saved_count && p4.tone_pulses == 3); check_profile_original(); cases++;

    /* Seven new gate writes remain when both gain changes are idempotent at code31. */
    for (unsigned n = 1; n <= 7; n++) {
        reset_model(); model_tone_present = true; model_tone = 1;
        model_tone_gain_present = true; model_tone_gain = 31;
        fail_write_no = up_writes + n;
        begin_worker(); event(0, 0, IRQ_ACCDET);
        for (unsigned edge = 0; edge < 6 && active && p4.on; edge++) {
            model_now = p4.tone_deadline; event(0, 0, 0);
        }
        assert(!active && !saved_count); check_profile_original(); cases++;
    }

    /* Header rejects every other SID9 write, wide address/value, and unrelated SID. */
    ready_direct(); io = all_writes + pmic_reads + status_reads;
    assert(pmif_write8(9, 0x079d, 0) != PMIF_OK && pmif_write8(9, 0x079f, 0) != PMIF_OK);
    assert(pmif_write8(9, 0x1079e, 0) != PMIF_OK && pmif_write8(9, 0x079e, 256) != PMIF_OK);
    assert(pmif_write8(8, 0x079e, 0) != PMIF_OK && !p4_apply(9, 0x079f, 4, 4, false));
    assert(io == all_writes + pmic_reads + status_reads); cases++;
    fprintf(stdout, "PASS %u cases; up bus writes=%u; close+restore bus writes=%u\n", cases, up_writes, close_writes);
    return 0;
}
