#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""PMIF (MT6895) register-level simulator for the HakiOS SPMI work.

It models exactly one software-interface channel -- SWINF_2 of the mt8195 register
table, the only one inside this board's declared window -- and the five registers a
byte transaction touches:

    0x880 ACC   0x884 WDATA   0x894 RDATA   0x8a4 VLDCLR   0x8a8 STA

Every hardware claim below is tagged with its evidence:

  [U] upstream  drivers/spmi/spmi-mtk-pmif.c, MT6895-Mainline/linux @
                7.2-mt6895-xiaomi-xaga
  [V] vendor    drivers/spmi/spmi-mtk-pmif-core.c (XagaForge/android_kernel_xiaomi_mt6895
                @ 17.0), plus the mt6xxx register table recovered from the .rodata of
                spmi-mtk-pmif.ko shipped in this phone's TWRP image
  [O] observed on this board, recorded in hakios-mtk (see SPEC section 7)
  [M] MODELED -- a plausible mechanism, not read out of a manual; listed as
                unverified in SPEC section 6

Usage:  python3 spmi-sim.py --selftest [-v]
"""

from __future__ import annotations

import argparse
import sys

# ---------------------------------------------------------------------------
# Registers, mt8195 table, SWINF_2.  [U]:213-217  [V] mt6xxx table: 880/884/894/8a4/8a8
# ---------------------------------------------------------------------------
ACC = 0x880
WDATA = 0x884
RDATA = 0x894
VLDCLR = 0x8A4
STA = 0x8A8
INIT_DONE = 0x000
CMDISSUE_EN = 0x3B8

#: The window this board's device tree declares: reg = <0x1c804000 0x8ff>.  [O] xaga_live.dts:1119
WINDOW_LO = 0x000
WINDOW_HI = 0x8FE

#: Last offset any real register table mentions: SWINF_3_VLD_CLR of the mt8195 table.  [U]:221
LAST_REAL_OFFSET = 0x8E8

# ---------------------------------------------------------------------------
# STA / FSM
# ---------------------------------------------------------------------------
STA_FSM_MASK = 0x0000000E  # bits 3:1
STA_FSM_SHIFT = 1
STA_FSM_IDLE = 0x0
STA_FSM_REQ = 0x2  # mtk-pmic-wrap.c:42 PWRAP_WACS_FSM_REQ
STA_FSM_WFDLE = 0x4  # mtk-pmic-wrap.c:43 PWRAP_WACS_FSM_WFDLE
STA_FSM_WFVLDCLR = 0x6  # [U]:21

#: STA as this board reads it with the channel idle: 0x00008441.          [O]
#: FSM field is 0 (IDLE); bits 0, 6, 10 and 15 read 1.
STA_IDLE_BASE = 0x00008441

# [M] Field names inside STA, decoded from the argument order of the vendor's own SWINF
# dump format string "[err:%d, sbusy:%d, done:%d, qfillcnt:%d qfreecnt:%d, qempty:%d,
# qfull:%d, req:%d" (spmi-mtk-pmif.ko, get_pmif_swinf_reg_dump): the compiler emits
# ubfx from the same register at #18, #17, #15, #11(w4), #7(w4), #6, #5, #4.  The
# result is self-consistent with 0x8441: done=1, qempty=1, qfreecnt=8, req=0, err=0.
STA_ERR = 1 << 18
STA_SBUSY = 1 << 17
STA_DONE = 1 << 15
STA_QFULL = 1 << 5
STA_QEMPTY = 1 << 6
STA_REQ = 1 << 4

# ---------------------------------------------------------------------------
# Command word.  [U]:389 (read), :467 (write)  [V]:422, :501
# ---------------------------------------------------------------------------
CMD_OPC_SHIFT = 30
CMD_OPC_MASK = 0x3
CMD_WRITE = 1 << 29
CMD_SID_SHIFT = 24
CMD_SID_MASK = 0xF
CMD_BC_SHIFT = 16
CMD_BC_MASK = 0xFF
CMD_ADDR_MASK = 0xFFFF

OPC_REG_0 = 0
OPC_REG = 1
OPC_EXT_REG = 2
OPC_EXT_REG_LONG = 3

SID_MT6363 = 4  # [O] xaga_live.dts:1134  pmic@4  compatible mediatek,mt6363
SID_MT6368 = 5  # [O] second_pmic@5, the accdet/headset PMIC

#: The only slaves this board has on the bus.  [O] xaga_live.dts
PRESENT_SIDS = {SID_MT6363, SID_MT6368}

#: PMIC registers that must never be written, by any path.          [O] SPEC section 5
PMIC_FORBIDDEN_WRITE = {
    0x014B: "PMIC global shutdown",
    0x0240: "VBUCK2 status: read-only, and RMW on it is forbidden; use the 0x241 set alias",
}


class BusAbort(Exception):
    """An offset outside the declared window. On hardware this is a reboot, not an error."""


class PmicShutdown(Exception):
    """A write that powers the PMIC off: the SoC falls back to preloader."""


# ---------------------------------------------------------------------------
# The model
# ---------------------------------------------------------------------------
class Channel:
    """SWINF_2 plus the PMIC side of the bus."""

    def __init__(self, window=(WINDOW_LO, WINDOW_HI), latency=3, verbose=False):
        self.window = window
        self.latency = latency
        self.verbose = verbose
        self.log = []
        self.regs = {
            ACC: 0,
            WDATA: 0,
            RDATA: 0,
            VLDCLR: 0,
            STA: STA_IDLE_BASE,
            INIT_DONE: 0x1,
            CMDISSUE_EN: 0x1,  # bit 2 = this channel's issue gate  [U]:133
        }
        self.fsm = STA_FSM_IDLE
        self.pending = None
        self.pmic = {}
        self.dead = False
        self.dead_reason = ""
        self.shutdown = False
        self.cycles = 0
        self.n_abort = 0
        self.n_nak = 0

    # -- internals ---------------------------------------------------------
    def _say(self, msg):
        self.log.append(f"[{self.cycles:03d}] {msg}")
        if self.verbose:
            print(f"[{self.cycles:03d}] {msg}", file=sys.stderr)

    def _alive(self):
        if self.dead:
            raise BusAbort(f"machine is dead: {self.dead_reason}")
        if self.shutdown:
            raise PmicShutdown("PMIC is in shutdown; the board has fallen to preloader")

    def _check_window(self, off):
        lo, hi = self.window
        if not lo <= off <= hi:
            self.dead = True
            self.dead_reason = f"offset 0x{off:03x} is outside the pmif window 0x{lo:03x}..0x{hi:03x}"
            self.n_abort += 1
            self._say(f"BUS ABORT: {self.dead_reason}")
            raise BusAbort(self.dead_reason)

    def _sta_value(self):
        return (STA_IDLE_BASE & ~STA_FSM_MASK) | ((self.fsm << STA_FSM_SHIFT) & STA_FSM_MASK)

    def _tick(self):
        if self.pending is not None and self.cycles >= self.pending["ready_at"]:
            self._complete()

    def _submit(self, cmd):
        opc = (cmd >> CMD_OPC_SHIFT) & CMD_OPC_MASK
        wr = bool(cmd & CMD_WRITE)
        sid = (cmd >> CMD_SID_SHIFT) & CMD_SID_MASK
        addr = cmd & CMD_ADDR_MASK
        bc = ((cmd >> CMD_BC_SHIFT) & CMD_BC_MASK) + 1
        self._say(
            f"ACC <- 0x{cmd:08x}: opc={opc} {'WRITE' if wr else 'READ'} "
            f"sid={sid} len={bc} addr=0x{addr:04x}"
        )
        # These PMICs are addressed only in the 16-bit extended space, which is opc 3.
        # opc 1 (PMIF_CMD_REG, eight-bit space) is what left the FSM at 4 on this board. [O]
        legal_shape = opc == OPC_EXT_REG_LONG and bc == 1
        present = sid in PRESENT_SIDS
        if wr and legal_shape and present and addr in PMIC_FORBIDDEN_WRITE:
            self._say(f"PMIC write to 0x{addr:04x}: {PMIC_FORBIDDEN_WRITE[addr]}")
            if addr == 0x014B:
                self.shutdown = True
                raise PmicShutdown("write to 0x014b: the PMIC shuts the board down")
        self.fsm = STA_FSM_REQ
        self.pending = {
            "kind": "wr" if wr else "rd",
            "ready_at": self.cycles + max(1, self.latency),
            "sid": sid,
            "addr": addr,
            "nak": not (legal_shape and present),
            "data": self.regs[WDATA] & 0xFF,
        }

    def _complete(self):
        p, self.pending = self.pending, None
        if p["nak"]:
            # [O] "left the state machine at 4 instead of WFVLDCLR": parked, never valid.
            self.fsm = STA_FSM_WFDLE
            self.n_nak += 1
            self._say(f"NAK sid={p['sid']} addr=0x{p['addr']:04x}: FSM parks at 4, no data")
            return
        if p["kind"] == "wr":
            self.pmic[(p["sid"], p["addr"])] = p["data"]
            self.fsm = STA_FSM_IDLE
            self._say(f"write ok sid={p['sid']} 0x{p['addr']:04x} <- 0x{p['data']:02x}: FSM IDLE")
        else:
            self.regs[RDATA] = self.pmic.get((p["sid"], p["addr"]), 0x00)
            self.fsm = STA_FSM_WFVLDCLR
            self._say(f"read ok sid={p['sid']} 0x{p['addr']:04x} -> 0x{self.regs[RDATA]:02x}: WFVLDCLR")

    # -- MMIO --------------------------------------------------------------
    def read(self, off):
        self._alive()
        self._check_window(off)
        self.cycles += 1
        self._tick()
        if off == STA:
            return self._sta_value()
        return self.regs.get(off, 0)

    def write(self, off, value):
        self._alive()
        self._check_window(off)
        self.cycles += 1
        self._tick()
        value &= 0xFFFFFFFF
        if off == ACC:
            self.regs[ACC] = value
            self._submit(value)
        elif off == VLDCLR:
            self.regs[VLDCLR] = value
            if value & 1:
                # [V][U] this is the W1C transaction ack: it retires WFVLDCLR. It is not
                # data, so reading it back and comparing is meaningless.
                if self.fsm == STA_FSM_WFVLDCLR:
                    self.fsm = STA_FSM_IDLE
                    self._say("VLDCLR 1: read retired, FSM IDLE")
                else:
                    self._say(f"VLDCLR 1 while FSM={self.fsm}: nothing to retire")
        else:
            self.regs[off] = value


def build_cmd(write, sid, addr, length=1, opc=OPC_EXT_REG_LONG):
    """The command word exactly as both drivers build it.  [U]:389,467  [V]:422,501"""
    if not 0 <= sid <= 0xF:
        raise ValueError("SID is four bits, 27:24")
    if not 1 <= length <= 256:
        raise ValueError("byte count is eight bits, length-1 in 23:16")
    return (
        ((opc & CMD_OPC_MASK) << CMD_OPC_SHIFT)
        | (CMD_WRITE if write else 0)
        | ((sid & CMD_SID_MASK) << CMD_SID_SHIFT)
        | (((length - 1) & CMD_BC_MASK) << CMD_BC_SHIFT)
        | (addr & CMD_ADDR_MASK)
    )


def decode_cmd(cmd):
    return {
        "opc": (cmd >> CMD_OPC_SHIFT) & CMD_OPC_MASK,
        "write": bool(cmd & CMD_WRITE),
        "sid": (cmd >> CMD_SID_SHIFT) & CMD_SID_MASK,
        "len": ((cmd >> CMD_BC_SHIFT) & CMD_BC_MASK) + 1,
        "addr": cmd & CMD_ADDR_MASK,
    }


# ---------------------------------------------------------------------------
# Reference transactions, in the shape SPEC says to write them
# ---------------------------------------------------------------------------
IDLE_BUDGET = 200  # model cycles, not nanoseconds; every loop here is bounded


def wait_fsm(ch, want, budget=IDLE_BUDGET):
    for _ in range(budget):
        if ((ch.read(STA) >> STA_FSM_SHIFT) & 0x7) == want:
            return True
    return False


def pmic_read(ch, sid, addr, budget=IDLE_BUDGET, recover=True):
    """Return (ok, value, why). `recover=False` models a driver with no recovery at all."""
    if sid not in PRESENT_SIDS:
        return False, 0, f"refused: sid {sid} not in {sorted(PRESENT_SIDS)}"
    if addr > CMD_ADDR_MASK:
        return False, 0, "refused: address wider than 16 bits"
    if not wait_fsm(ch, STA_FSM_IDLE, budget):
        if recover and ch.fsm == STA_FSM_WFVLDCLR:
            ch.write(VLDCLR, 1)  # [U]:379-386 the only recovery upstream performs
            if not wait_fsm(ch, STA_FSM_IDLE, budget):
                return False, 0, "stuck in WFVLDCLR even after the ack"
        else:
            return False, 0, f"channel busy, fsm={ch.fsm}"
    ch.write(ACC, build_cmd(False, sid, addr))
    if not wait_fsm(ch, STA_FSM_WFVLDCLR, budget):
        return False, 0, f"no WFVLDCLR, fsm={ch.fsm} (NAK or abort)"
    value = ch.read(RDATA) & 0xFF
    ch.write(VLDCLR, 1)
    if not wait_fsm(ch, STA_FSM_IDLE, budget):
        return False, value, "not idle after the ack"
    return True, value, "ok"


def pmic_write(ch, sid, addr, value, budget=IDLE_BUDGET, recover=True):
    if sid not in PRESENT_SIDS:
        return False, f"refused: sid {sid} not in {sorted(PRESENT_SIDS)}"
    if addr in PMIC_FORBIDDEN_WRITE:
        return False, f"refused: 0x{addr:04x} is {PMIC_FORBIDDEN_WRITE[addr]}"
    if not wait_fsm(ch, STA_FSM_IDLE, budget):
        if recover and ch.fsm == STA_FSM_WFVLDCLR:
            ch.write(VLDCLR, 1)
            if not wait_fsm(ch, STA_FSM_IDLE, budget):
                return False, "stuck in WFVLDCLR"
        else:
            return False, f"channel busy, fsm={ch.fsm}"
    ch.write(WDATA, value)
    ch.write(ACC, build_cmd(True, sid, addr))
    # [V] the vendor driver returns here without any wait; a write that never retires is
    # invisible to it. This model does wait, because the SPEC requires knowing.
    if not wait_fsm(ch, STA_FSM_IDLE, budget):
        return False, f"write not accepted, fsm={ch.fsm}"
    return True, "ok"


# ---------------------------------------------------------------------------
# Selftest
# ---------------------------------------------------------------------------
CASES = []


def case(fn):
    CASES.append(fn)
    return fn


@case
def test_idle_sta_matches_this_board():
    """Idle STA on this board is 0x8441, FSM field clear, done and qempty set."""
    ch = Channel()
    ch.pmic[(SID_MT6368, 0x26B6)] = 0x00
    sta = ch.read(STA)
    assert sta == 0x00008441, hex(sta)
    assert ((sta & STA_FSM_MASK) >> STA_FSM_SHIFT) == STA_FSM_IDLE
    assert sta & STA_DONE
    assert sta & STA_QEMPTY
    assert not sta & STA_ERR
    assert not sta & STA_SBUSY


@case
def test_read_transaction_succeeds():
    """The SPEC read sequence: bounded IDLE wait, ACC, WFVLDCLR, RDATA, W1C ack, IDLE."""
    ch = Channel()
    ch.pmic[(SID_MT6368, 0x26B6)] = 0xC1
    ok, val, why = pmic_read(ch, SID_MT6368, 0x26B6)
    assert ok, why
    assert val == 0xC1, hex(val)
    assert ch.fsm == STA_FSM_IDLE
    assert decode_cmd(build_cmd(False, 5, 0x26B6)) == {
        "opc": 3, "write": False, "sid": 5, "len": 1, "addr": 0x26B6,
    }
    assert build_cmd(False, 5, 0x26B6) == 0xC50026B6, hex(build_cmd(False, 5, 0x26B6))


@case
def test_write_transaction_succeeds():
    """A write is WDATA then ACC with bit 29, and finishes without WFVLDCLR."""
    ch = Channel()
    ok, why = pmic_write(ch, SID_MT6368, 0x0241, 0x04)
    assert ok, why
    assert ch.pmic[(SID_MT6368, 0x0241)] == 0x04
    assert ch.fsm == STA_FSM_IDLE
    assert build_cmd(True, 5, 0x0241) == 0xE5000241, hex(build_cmd(True, 5, 0x0241))


@case
def test_missing_vldclr_blocks_the_channel():
    """No W1C ack: the channel stays in WFVLDCLR and the next IDLE wait can never succeed."""
    ch = Channel()
    ch.pmic[(SID_MT6368, 0x26B6)] = 0x11
    ch.write(ACC, build_cmd(False, SID_MT6368, 0x26B6))
    assert wait_fsm(ch, STA_FSM_WFVLDCLR), "the read should reach WFVLDCLR"
    assert not wait_fsm(ch, STA_FSM_IDLE, budget=20), "it must not go idle on its own"
    assert ch.fsm == STA_FSM_WFVLDCLR
    # A driver with no recovery now times out on every single transaction, forever.
    ok, _, why = pmic_read(ch, SID_MT6368, 0x26B7, recover=False)
    assert not ok and "busy" in why, why
    ok, _, why = pmic_read(ch, SID_MT6368, 0x26B7, recover=False)
    assert not ok and "busy" in why, why
    # The ack alone is the documented recovery; after it the same read works.
    assert ch.fsm == STA_FSM_WFVLDCLR
    ch.write(VLDCLR, 1)
    assert wait_fsm(ch, STA_FSM_IDLE, budget=20)
    ok, _, why = pmic_read(ch, SID_MT6368, 0x26B7)
    assert ok, why


@case
def test_nak_parks_the_fsm_at_4():
    """A command the slave does not answer parks the FSM at 4; bounded waits return."""
    ch = Channel()
    # Bypass the wrapper to show what the hardware does with an absent slave.
    ch.write(ACC, build_cmd(False, 0x1, 0x001E))
    assert not wait_fsm(ch, STA_FSM_WFVLDCLR, budget=20)
    assert ch.fsm == STA_FSM_WFDLE, ch.fsm
    # And the wrapper refuses the same request before it reaches the wire.
    ok, _, why = pmic_read(ch, 0x1, 0x001E)
    assert not ok and "refused" in why, why
    assert ch.n_nak == 1


@case
def test_out_of_window_is_a_bus_abort():
    """0x8e8 is the last real register; 0x900 is outside the window and kills the board."""
    ch = Channel()
    for off in (0x000, 0x880, 0x8A8, 0x8E8, 0x8FE):
        ch.read(off)
    assert LAST_REAL_OFFSET == 0x8E8
    ch2 = Channel()
    try:
        ch2.read(0x900)
    except BusAbort as e:
        assert "outside the pmif window" in str(e), str(e)
    else:
        raise AssertionError("0x900 must abort")
    assert ch2.dead and ch2.n_abort == 1
    try:
        ch2.read(0x8A8)  # even a legal offset
    except BusAbort as e:
        assert "machine is dead" in str(e), str(e)
    else:
        raise AssertionError("a dead bus must stay dead")


@case
def test_sid_not_shifted_into_bits_27_24():
    """The recorded accident shape: the SID written as 5 instead of 5 << 24.

    An unshifted SID leaves bits 27:24 zero, so an extended access meant for MT6368
    becomes an access to slave 0, and the stray bit lands in the address field. With
    bit 29 also set -- the shape that turns a read into a write -- the same slip sends
    a write to whatever register the address happens to name.
    """
    good = build_cmd(False, 5, 0x001E)
    bad = (OPC_EXT_REG_LONG << CMD_OPC_SHIFT) | 0x5 | 0x001E
    assert decode_cmd(good) == {"opc": 3, "write": False, "sid": 5, "len": 1, "addr": 0x001E}
    assert decode_cmd(bad) == {"opc": 3, "write": False, "sid": 0, "len": 1, "addr": 0x001F}
    assert bad == 0xC000001F, hex(bad)
    ch = Channel()
    ok, _, why = pmic_read(ch, decode_cmd(bad)["sid"], decode_cmd(bad)["addr"])
    assert not ok and "refused" in why, why
    # Same slip on the write path: a read becomes a write aimed at slave 0.
    assert decode_cmd(bad | CMD_WRITE)["write"] is True
    # The damage class worth modelling explicitly: a write to 0x014b. The wrapper refuses
    # it, so drive the model below the wrapper -- this is what a driver without the
    # whitelist does on the wire.
    ch2 = Channel()
    ok, why = pmic_write(ch2, SID_MT6368, 0x014B, 0x00)
    assert not ok and "refused" in why, why
    try:
        ch2.write(WDATA, 0x00)
        ch2.write(ACC, build_cmd(True, SID_MT6368, 0x014B))
    except PmicShutdown as e:
        assert "0x014b" in str(e), str(e)
    else:
        raise AssertionError("0x014b must shut the PMIC down")
    assert ch2.shutdown
    try:
        ch2.read(STA)
    except PmicShutdown as e:
        assert "preloader" in str(e), str(e)
    else:
        raise AssertionError("a shut-down PMIC answers nothing")


@case
def test_short_opcode_parks_at_4():
    """opc=1 (eight-bit space) is what the old code sent, and it never reaches WFVLDCLR."""
    ch = Channel()
    ch.write(ACC, (OPC_REG << CMD_OPC_SHIFT) | (SID_MT6363 << CMD_SID_SHIFT) | 0x1E)
    assert not wait_fsm(ch, STA_FSM_WFVLDCLR, budget=20)
    assert ch.fsm == STA_FSM_WFDLE
    # A multi-byte transfer is a legal SPMI shape, but not one a byte accessor may send.
    assert decode_cmd(build_cmd(False, 5, 0x0100, length=2))["len"] == 2


@case
def test_forbidden_registers_and_sid_whitelist():
    ch = Channel()
    for addr in sorted(PMIC_FORBIDDEN_WRITE):
        ok, why = pmic_write(ch, SID_MT6368, addr, 0x00)
        assert not ok and "refused" in why, why
    assert not ch.shutdown
    for sid in (0, 1, 2, 3, 6, 7, 0xF):
        ok, _, why = pmic_read(ch, sid, 0x001E)
        assert not ok and "refused" in why, (sid, why)
    for sid in sorted(PRESENT_SIDS):
        ok, _, why = pmic_read(ch, sid, 0x001E)
        assert ok, (sid, why)


def main():
    ap = argparse.ArgumentParser(description="PMIF register-level simulator")
    ap.add_argument("--selftest", action="store_true", help="run every assertion")
    ap.add_argument("-v", "--verbose", action="store_true", help="trace transactions")
    args = ap.parse_args()
    if not args.selftest:
        ap.print_help()
        return 0
    failed = 0
    for fn in CASES:
        try:
            fn()
        except AssertionError as e:
            failed += 1
            print(f"FAIL {fn.__name__}: {e}")
        except Exception as e:  # noqa: BLE001
            failed += 1
            print(f"ERROR {fn.__name__}: {type(e).__name__}: {e}")
        else:
            print(f"ok   {fn.__name__}")
    print(f"\n{len(CASES) - failed}/{len(CASES)} passed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
