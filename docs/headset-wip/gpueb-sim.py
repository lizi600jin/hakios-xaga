#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
gpueb-sim.py — MT6895 GPUEB mailbox / IPI 状态机推演

目的
    在不碰真机的前提下，把 AP 驱动 ↔ GPUEB 固件的 mbox 交互跑一遍，
    验证「照抄这份协议能不能成」，并覆盖 4 个已知的失败模式。

模型
    - 寄存器：4 个 1-bit/通道 位图（TX_STS / IRQ_SET / IRQ_CLR / RX_STS）
      + 640 B 消息 SRAM（160 slots × 4 B）
    - GPUEB 侧：一个状态机，收到 IRQ 就解析 slab 里的 cmd，写应答，回触发
    - AP 侧：与 gpueb-ipi.h 里 gpueb_ipi_xfer() 逐行对应的轮询实现

    寄存器语义完全按 [ML] drivers/mailbox/mtk-gpueb-mailbox.c:22-25，见
    docs/headset-wip/findings/GPUEB-IPI-PROTOCOL.md §2。

场景
    S1  正常握手成功
    S2  GPUEB 未加载（超时）
    S3  GPUEB 已加载但拒绝 power_on
    S4  共享内存未初始化

用法
    python3 gpueb-sim.py              # 跑全部场景
    python3 gpueb-sim.py --selftest   # 同上，额外跑内部自检
    python3 gpueb-sim.py --verbose    # 打印每次寄存器访问
    python3 gpueb-sim.py --trace      # 打印状态迁移轨迹
"""

import argparse
import sys
import struct
from dataclasses import dataclass, field
from typing import List, Optional, Dict, Tuple

# ═══════════════════════════════════════════════════════════════════════════
# 常量 —— 全部来自 GPUEB-IPI-PROTOCOL.md，与 gpueb-ipi.h 一一对应
# ═══════════════════════════════════════════════════════════════════════════

# 物理地址
MBOX_SRAM_PHYS = 0x13C5FD80
MBOX_SRAM_SIZE = 0x280            # 640 B = 160 slots
MBOX_SEND_PHYS = 0x13C62000       # RO: TX busy
MBOX_SET_PHYS  = 0x13C62004       # WO: raise IRQ
MBOX_CLR_PHYS  = 0x13C62074       # WO: clear IRQ
MBOX_RECV_PHYS = 0x13C62078       # RO: RX pending

SHARED_PHYS        = 0x7C600000
GPUFREQ_MEM_SIZE   = 0x1000
GPUFREQ_STATUS_SZ  = 0x400
GPUFREQ_DEBUG_SZ   = GPUFREQ_MEM_SIZE - GPUFREQ_STATUS_SZ   # 0xc00

# 通道
CHAN_GPUFREQ = 1
BIT = lambda n: 1 << n

# slot（§4.4）
TX_SLOT_GPUFREQ = 4
RX_SLOT_GPUFREQ = 47
GPUFREQ_TX_OFF  = TX_SLOT_GPUFREQ * 4     # 0x010
GPUFREQ_RX_OFF  = RX_SLOT_GPUFREQ * 4     # 0x0bc

MSG_WORDS = 8
MSG_BYTES = 32

# 命令
CMD_INIT_SHARED_MEM = 0
CMD_POWER_CONTROL   = 6
CMD_COMMIT          = 7

POWER_OFF = 0
POWER_ON  = 1

# 超时
POLL_INTERVAL_US   = 10
TMO_INIT_MS        = 1000
TMO_POWER_MS       = 1000

# 返回码
GPUEB_OK        = 0
GPUEB_ETXSTUCK  = -1
GPUEB_ETIMEOUT  = -2
GPUEB_EREFUSED  = -3

# 共享状态字段偏移（§6）
OFF_POWER_CONTROL  = 0x60
OFF_DVFS_STATE     = 0x64
OFF_SHADER_PRESENT = 0x68
OFF_POWER_COUNT    = 0x6C

DVFS_POWEROFF = 1 << 1


# ═══════════════════════════════════════════════════════════════════════════
# 寄存器模型
# ═══════════════════════════════════════════════════════════════════════════

class MboxRegs:
    """4 个 1-bit/通道 位图 + 640 B SRAM。

    严格按 [ML] mtk-gpueb-mailbox.c 的语义：
        TX_STS  RO  1 = 我方上一条还没被对方取走
        IRQ_SET WO  写 1 -> 对方 RX_STS 对应位置 1（"我发消息了"）
        IRQ_CLR WO  写 1 -> 我方 RX_STS 对应位清 0（"我收完了"）
        RX_STS  RO  1 = 对方给我发了消息
    """

    def __init__(self, verbose: bool = False):
        self.tx_sts = 0        # 我方 TX 忙位
        self.rx_sts = 0        # 我方 RX 待收位
        self.sram = bytearray(MBOX_SRAM_SIZE)
        self.verbose = verbose
        self.access_log: List[str] = []

    # --- 读 ---
    def rd_tx_sts(self) -> int:
        self._log(f"RD  0x{MBOX_SEND_PHYS:08x} (TX_STS)  -> 0x{self.tx_sts:08x}")
        return self.tx_sts

    def rd_rx_sts(self) -> int:
        self._log(f"RD  0x{MBOX_RECV_PHYS:08x} (RX_STS)  -> 0x{self.rx_sts:08x}")
        return self.rx_sts

    def rd_sram(self, off: int, n: int) -> bytes:
        self._log(f"RD  SRAM+0x{off:03x} ({n} B)")
        return bytes(self.sram[off:off + n])

    # --- 写 ---
    def wr_irq_set(self, bits: int):
        """AP 侧：触发。硬件上会让 *对端* 的 RX_STS 置位 —— 这里由 Sim 桥接。"""
        self._log(f"WR  0x{MBOX_SET_PHYS:08x} (IRQ_SET) = 0x{bits:08x}")
        return bits

    def wr_irq_clr(self, bits: int):
        """AP 侧：清自己的 RX_STS。"""
        self._log(f"WR  0x{MBOX_CLR_PHYS:08x} (IRQ_CLR) = 0x{bits:08x}")
        before = self.rx_sts
        self.rx_sts &= ~bits
        self._log(f"    RX_STS 0x{before:08x} -> 0x{self.rx_sts:08x}")

    def wr_sram(self, off: int, data: bytes):
        self._log(f"WR  SRAM+0x{off:03x} ({len(data)} B) {data.hex()}")
        self.sram[off:off + len(data)] = data

    def _log(self, s: str):
        self.access_log.append(s)
        if self.verbose:
            print("        " + s)


# ═══════════════════════════════════════════════════════════════════════════
# 共享内存模型
# ═══════════════════════════════════════════════════════════════════════════

class SharedMem:
    """GPUFREQ 的 4 KiB 保留区。

    0x000..0x3ff  status
    0x400..0xfff  debug
    """

    def __init__(self, initialized: bool = False, fill: int = 0x00):
        self.buf = bytearray(GPUFREQ_MEM_SIZE)
        # 未初始化时填 0xff（模拟 DRAM 上电初值/脏数据），
        # 已初始化则全 0（GPUEB 启动时会清零）
        for i in range(len(self.buf)):
            self.buf[i] = fill
        self.initialized = initialized
        self.status_base: Optional[int] = None
        self.debug_base: Optional[int] = None

    def wr32(self, off: int, v: int):
        struct.pack_into("<I", self.buf, off, v & 0xFFFFFFFF)

    def rd32(self, off: int) -> int:
        return struct.unpack_from("<I", self.buf, off)[0]

    @property
    def power_control(self) -> int:
        return self.rd32(OFF_POWER_CONTROL)

    @power_control.setter
    def power_control(self, v: int):
        self.wr32(OFF_POWER_CONTROL, v)

    @property
    def dvfs_state(self) -> int:
        return self.rd32(OFF_DVFS_STATE)

    @dvfs_state.setter
    def dvfs_state(self, v: int):
        self.wr32(OFF_DVFS_STATE, v)

    @property
    def shader_present(self) -> int:
        return self.rd32(OFF_SHADER_PRESENT)

    @shader_present.setter
    def shader_present(self, v: int):
        self.wr32(OFF_SHADER_PRESENT, v)

    @property
    def power_count(self) -> int:
        return struct.unpack_from("<i", self.buf, OFF_POWER_COUNT)[0]

    @power_count.setter
    def power_count(self, v: int):
        struct.pack_into("<i", self.buf, OFF_POWER_COUNT, v)


# ═══════════════════════════════════════════════════════════════════════════
# GPUEB 固件状态机
# ═══════════════════════════════════════════════════════════════════════════

@dataclass
class GpuebConfig:
    """每个场景通过它调整 GPUEB 的行为。"""
    present: bool = True          # 固件是否被 LK 加载并运行
    clock_on: bool = True         # mbox bank 时钟是否开
    accept_power_on: bool = True  # 是否同意 POWER_ON
    power_on_err: int = -22       # 拒绝时返回的 return_value
    auto_init_shared: bool = False  # 固件是否自己初始化共享内存（正常情况下不）


class Gpueb:
    """GPUEB 侧状态机。等价于固件的 mbox_port.c + gpufreq_ipi_handler。"""

    # 状态
    ST_RESET       = "RESET"
    ST_IDLE        = "IDLE"
    ST_HANDLING    = "HANDLING"
    ST_POWERED     = "POWERED"
    ST_REJECT      = "REJECT"

    def __init__(self, cfg: GpuebConfig, shm: SharedMem,
                 own_ep: "EndPoint", ap_ep: "EndPoint",
                 shared_phys: int = SHARED_PHYS):
        self.cfg = cfg
        self.shm = shm
        self.own = own_ep          # GPUEB 自己的端点：AP 的 IRQ_SET 打到 own.rx_sts
        self.ap = ap_ep            # 对端（AP）的端点：应答要打到 ap.rx_sts
        self.shared_phys = shared_phys
        self.state = self.ST_RESET if cfg.present else "ABSENT"
        self.ctx: Dict[str, int] = {}
        self.trace: List[str] = []
        self.power_refcount = 0

        if cfg.present:
            self._goto(self.ST_IDLE, "fw booted by LK")

        # 正常情况下，固件初始化自己那半边的共享内存？
        # 不。按协议，共享内存地址是 AP 通过 CMD_INIT_SHARED_MEM 给的。
        # 固件在收到之前不知道 status_base，所以不会写。
        if cfg.auto_init_shared:
            self.shm.power_control = 1
            self.shm.shader_present = 0x20003
            self.shm.power_count = 0

    # --- 内部 ---
    def _goto(self, new: str, why: str):
        self.trace.append(f"[GPUEB] {self.state:9s} -> {new:9s}  ({why})")
        self.state = new

    def _reply(self, cmd: int, retval: int):
        """写应答到 GPUFREQ 的 RX slot，然后触发 IRQ_SET（GPUEB→AP 方向）。"""
        if not self.cfg.present:
            return
        msg = [0] * MSG_WORDS
        msg[0] = cmd
        msg[1] = 0
        msg[2] = retval & 0xFFFFFFFF
        data = b"".join(struct.pack("<I", w) for w in msg)
        # GPUEB 的 TX slot == AP 的 RX slot（同一块 SRAM）
        self.own.sram[GPUFREQ_RX_OFF:GPUFREQ_RX_OFF + MSG_BYTES] = data
        # 触发：让 **AP** 的 RX_STS bit1 置位（关键：不是自己的）
        self.ap.rx_sts |= BIT(CHAN_GPUFREQ)
        self.trace.append(f"[GPUEB] reply cmd={cmd} ret={retval} -> "
                          f"SRAM+0x{GPUFREQ_RX_OFF:03x}, AP RX_STS=0x{self.ap.rx_sts:08x}")

    # --- 对外：被 AP 的 IRQ_SET 调用 ---
    def on_irq(self):
        """模拟中断/轮询发现 RX pending 后的处理。"""
        if not self.cfg.present or not self.cfg.clock_on:
            return

        self._goto(self.ST_HANDLING, "IRQ received")

        raw = bytes(self.own.sram[GPUFREQ_TX_OFF:GPUFREQ_TX_OFF + MSG_BYTES])
        words = list(struct.unpack("<8I", raw))
        cmd = words[0]
        target = words[1]
        w2 = words[2]
        u64_lo = words[2] | (words[3] << 32)
        dbg_lo = words[4] | (words[5] << 32)
        st_sz = words[6]
        db_sz = words[7]

        self.trace.append(f"[GPUEB] recv cmd={cmd} target={target} w2=0x{w2:x}")

        if cmd == CMD_INIT_SHARED_MEM:
            self._handle_init(u64_lo, dbg_lo, st_sz, db_sz)
        elif cmd == CMD_POWER_CONTROL:
            self._handle_power(w2)
        else:
            self.trace.append(f"[GPUEB] invalid IPI cmd: {cmd}")
            self._reply(cmd, -22)
            self._goto(self.ST_IDLE, "invalid cmd")

    def _handle_init(self, status_base: int, debug_base: int,
                     status_size: int, debug_size: int):
        # 固件侧校验（[FW] 有 "status mem size: 0x%x < status data size: 0x%x"）
        min_need = 0x80      # sizeof(struct gpufreq_shared_status) 的保守下界
        if status_base != self.shared_phys:
            self.trace.append(f"[GPUEB] invalid mem status: 0x{status_base:x}")
            self._reply(CMD_INIT_SHARED_MEM, -22)
            self._goto(self.ST_IDLE, "bad status_base")
            return
        if status_size < min_need:
            self.trace.append(f"[GPUEB] status mem size: 0x{status_size:x} "
                              f"< status data size: 0x{min_need:x}")
            self._reply(CMD_INIT_SHARED_MEM, -22)
            self._goto(self.ST_IDLE, "status_size too small")
            return

        # 接受：记下地址，初始化 status 区
        self.shm.status_base = status_base
        self.shm.debug_base = debug_base
        self.shm.initialized = True
        # 固件写自己的运行状态
        self.shm.power_control = 1          # "GPU 电源归我管"
        self.shm.dvfs_state = DVFS_POWEROFF  # 还没上电
        self.shm.shader_present = 0x00020003 # T0C0|T1C0|T5C0（部分值，见报告 §6.3）
        self.shm.power_count = 0
        self.trace.append(f"[GPUEB] INIT ok: status=0x{status_base:x} "
                          f"sz=0x{status_size:x} debug=0x{debug_base:x} sz=0x{debug_size:x}")
        self._reply(CMD_INIT_SHARED_MEM, 0)
        self._goto(self.ST_IDLE, "shared mem registered")

    def _handle_power(self, power_state: int):
        if not self.shm.initialized:
            # 场景 S4：共享内存没初始化就发 POWER_CONTROL —— 固件拒绝
            self.trace.append("[GPUEB] reject POWER_CONTROL: shared mem not initialized")
            self._reply(CMD_POWER_CONTROL, -22)
            self._goto(self.ST_REJECT, "no shared mem")
            return

        if power_state == POWER_ON:
            if not self.cfg.accept_power_on:
                self.trace.append(f"[GPUEB] reject POWER_ON, ret={self.cfg.power_on_err}")
                self._reply(CMD_POWER_CONTROL, self.cfg.power_on_err)
                self._goto(self.ST_REJECT, "policy refused")
                return
            self.power_refcount += 1
            self.shm.power_count = self.power_refcount
            self.shm.dvfs_state &= ~DVFS_POWEROFF
            self._reply(CMD_POWER_CONTROL, self.power_refcount)
            self._goto(self.ST_POWERED, f"powered on, refcount={self.power_refcount}")
        else:
            self.power_refcount = max(0, self.power_refcount - 1)
            self.shm.power_count = self.power_refcount
            if self.power_refcount == 0:
                self.shm.dvfs_state |= DVFS_POWEROFF
            self._reply(CMD_POWER_CONTROL, self.power_refcount)
            self._goto(self.ST_IDLE, f"power off, refcount={self.power_refcount}")


# ═══════════════════════════════════════════════════════════════════════════
# 端点：把「AP 的寄存器视图」和「GPUEB 的寄存器视图」桥接起来
# ═══════════════════════════════════════════════════════════════════════════

class EndPoint:
    """一个端点的 mbox 视图。SRAM 是两侧共享的（同一块物理内存）。"""

    def __init__(self, shared_sram: bytearray):
        self.sram = shared_sram
        self.rx_sts = 0    # 本端点的 RX 待收位
        self.tx_sts = 0    # 本端点的 TX 忙位（本模拟简化：不建模对端取走延迟）


class Sim:
    """AP ↔ GPUEB 总线桥。"""

    def __init__(self, cfg: GpuebConfig, shm: SharedMem, verbose=False):
        self.cfg = cfg
        self.shm = shm
        self.verbose = verbose

        self.sram = bytearray(MBOX_SRAM_SIZE)
        self.ap = EndPoint(self.sram)
        self.gpueb_ep = EndPoint(self.sram)
        self.gpueb = Gpueb(cfg, shm, self.gpueb_ep, self.ap)

        # 虚拟时钟（毫秒）
        self.t = 0
        self.access_log: List[str] = []

        # GPUEB 应答延迟（模拟固件处理时间）。0 表示立即。
        self.gpueb_latency_ms = 0

    def now_ms(self) -> int:
        return self.t

    def _log(self, s: str):
        self.access_log.append(s)
        if self.verbose:
            print("    " + s)

    # --- AP 侧寄存器访问 ---
    def ap_rd_tx_sts(self) -> int:
        self._log(f"RD  TX_STS -> 0x{self.ap.tx_sts:08x}")
        return self.ap.tx_sts

    def ap_rd_rx_sts(self) -> int:
        self._log(f"RD  RX_STS -> 0x{self.ap.rx_sts:08x}")
        return self.ap.rx_sts

    def ap_wr_irq_set(self, bits: int):
        self._log(f"WR  IRQ_SET = 0x{bits:08x}")
        # 打到 GPUEB 的 RX_STS
        self.gpueb_ep.rx_sts |= bits
        # 固件在中断里处理（同步模型：立即）
        if self.cfg.present and self.cfg.clock_on:
            if self.gpueb_latency_ms:
                self.t += self.gpueb_latency_ms
            self.gpueb.on_irq()
            # 固件处理完会清自己的 RX_STS
            self.gpueb_ep.rx_sts &= ~bits

    def ap_wr_irq_clr(self, bits: int):
        self._log(f"WR  IRQ_CLR = 0x{bits:08x}")
        self.ap.rx_sts &= ~bits

    def ap_wr_sram(self, off: int, data: bytes):
        self._log(f"WR  SRAM+0x{off:03x} ({len(data)}B) {data.hex()}")
        self.sram[off:off + len(data)] = data

    def ap_rd_sram(self, off: int, n: int) -> bytes:
        d = bytes(self.sram[off:off + n])
        self._log(f"RD  SRAM+0x{off:03x} ({n}B) {d.hex()}")
        return d


# ═══════════════════════════════════════════════════════════════════════════
# AP 侧：与 gpueb-ipi.h 的 gpueb_ipi_xfer() 逐行对应
# ═══════════════════════════════════════════════════════════════════════════

def build_init_shared_mem(status_pa: int, debug_pa: int,
                          status_sz: int, debug_sz: int) -> bytes:
    words = [0] * MSG_WORDS
    words[0] = CMD_INIT_SHARED_MEM
    words[1] = 0
    words[2] = status_pa & 0xFFFFFFFF
    words[3] = (status_pa >> 32) & 0xFFFFFFFF
    words[4] = debug_pa & 0xFFFFFFFF
    words[5] = (debug_pa >> 32) & 0xFFFFFFFF
    words[6] = status_sz & 0xFFFFFFFF
    words[7] = debug_sz & 0xFFFFFFFF
    return b"".join(struct.pack("<I", w) for w in words)


def build_power_control(power_on: int) -> bytes:
    words = [0] * MSG_WORDS
    words[0] = CMD_POWER_CONTROL
    words[1] = 0
    words[2] = POWER_ON if power_on else POWER_OFF
    return b"".join(struct.pack("<I", w) for w in words)


def gpueb_ipi_xfer(sim: Sim, msg: bytes, timeout_ms: int,
                   dead_ms: Optional[int] = None):
    """与 gpueb-ipi.h 的 gpueb_ipi_xfer() 一一对应。

    dead_ms: 模拟「对端不在」——过了这个时刻就永远等不到应答。
             为 None 表示对端正常工作（不注入故障）。
    返回 (status, ack or None)
    """
    assert len(msg) == MSG_BYTES

    # 1. 等 TX 空闲
    deadline = sim.now_ms() + timeout_ms
    while sim.ap_rd_tx_sts() & BIT(CHAN_GPUFREQ):
        if sim.now_ms() >= deadline:
            return GPUEB_ETXSTUCK, None
        sim.t += max(1, POLL_INTERVAL_US // 1000)

    # 2. 写 SRAM
    sim.ap_wr_sram(GPUFREQ_TX_OFF, msg)

    # 3. 屏障（模型中无操作）
    # GPUEB_BARRIER()

    # 4. 触发
    sim.ap_wr_irq_set(BIT(CHAN_GPUFREQ))

    # 5. 轮询 RX
    deadline = sim.now_ms() + timeout_ms
    while True:
        if sim.ap_rd_rx_sts() & BIT(CHAN_GPUFREQ):
            break
        if dead_ms is not None and sim.now_ms() >= dead_ms:
            # 对端已死：剩下的轮询全部白等
            sim.t = deadline
        if sim.now_ms() >= deadline:
            sim.ap_wr_irq_clr(BIT(CHAN_GPUFREQ))
            return GPUEB_ETIMEOUT, None
        sim.t += max(1, POLL_INTERVAL_US // 1000)

    # 6. 读应答
    raw = sim.ap_rd_sram(GPUFREQ_RX_OFF, MSG_BYTES)
    words = list(struct.unpack("<8I", raw))
    ack = struct.unpack("<i", struct.pack("<I", words[2]))[0]

    # 7. 清位
    sim.ap_wr_irq_clr(BIT(CHAN_GPUFREQ))

    return GPUEB_OK, ack


# ═══════════════════════════════════════════════════════════════════════════
# 场景
# ═══════════════════════════════════════════════════════════════════════════

@dataclass
class ScenarioResult:
    name: str
    passed: bool
    detail: str
    assertions: List[Tuple[str, bool, str]] = field(default_factory=list)
    trace: List[str] = field(default_factory=list)


class Checker:
    def __init__(self):
        self.items: List[Tuple[str, bool, str]] = []
        self.fails = 0

    def check(self, cond: bool, msg: str, got: str = ""):
        self.items.append((msg, bool(cond), got))
        if not cond:
            self.fails += 1

    def eq(self, a, b, msg: str):
        self.check(a == b, msg, f"got {a!r}, want {b!r}")


def run_s1(verbose=False) -> ScenarioResult:
    """S1 正常握手成功"""
    cfg = GpuebConfig(present=True, clock_on=True, accept_power_on=True)
    shm = SharedMem(initialized=False, fill=0xFF)   # 脏 DRAM
    sim = Sim(cfg, shm, verbose=verbose)

    # 注意：**不** 用 shm.power_control 做门控（报告 §6.5 的教训）。
    # 初始时 power_control 是 0xFFFFFFFF（脏），如果按 vendor 逻辑
    # 判 != 0 就会走进「EB 模式」，恰好也是对的；但若脏值是 0 就会跳过。
    # 上游 gpueb_power.c 无条件发，这里照抄上游。

    r = gpueb_ipi_xfer(
        sim,
        build_init_shared_mem(SHARED_PHYS, SHARED_PHYS + GPUFREQ_STATUS_SZ,
                              GPUFREQ_STATUS_SZ, GPUFREQ_DEBUG_SZ),
        TMO_INIT_MS)
    init_status, init_ack = r

    power_status, power_ack = (None, None)
    if init_status == GPUEB_OK:
        power_status, power_ack = gpueb_ipi_xfer(
            sim, build_power_control(POWER_ON), TMO_POWER_MS)

    c = Checker()
    c.eq(init_status, GPUEB_OK, "S1.1 INIT_SHARED_MEM 传输成功")
    c.eq(init_ack, 0, "S1.2 INIT_SHARED_MEM 应答 return_value==0")
    c.eq(power_status, GPUEB_OK, "S1.3 POWER_CONTROL 传输成功")
    c.eq(power_ack, 1, "S1.4 POWER_CONTROL 应答 return_value==power_count==1")
    c.eq(shm.initialized, True, "S1.5 共享内存已登记")
    c.eq(shm.status_base, SHARED_PHYS, "S1.6 status_base 正确")
    c.eq(shm.debug_base, SHARED_PHYS + GPUFREQ_STATUS_SZ, "S1.7 debug_base = status + 0x400")
    c.eq(shm.power_control, 1, "S1.8 power_control==1（固件写）")
    c.check((shm.dvfs_state & DVFS_POWEROFF) == 0, "S1.9 dvfs_state 的 POWEROFF 位已清",
            f"dvfs_state=0x{shm.dvfs_state:x}")
    c.eq(shm.power_count, 1, "S1.10 power_count==1")
    c.check((shm.shader_present & 0x00020003) != 0, "S1.11 shader_present 非 0",
            f"0x{shm.shader_present:08x}")
    c.eq(sim.gpueb.state, Gpueb.ST_POWERED, "S1.12 GPUEB 处于 POWERED")
    c.eq(sim.ap.rx_sts, 0, "S1.13 AP 的 RX_STS 已清干净（无残留位）")

    trace = [f"[AP] INIT_SHARED_MEM -> status={init_status} ack={init_ack}"] + \
            sim.gpueb.trace + \
            [f"[AP] POWER_CONTROL -> status={power_status} ack={power_ack}"]
    passed = c.fails == 0
    return ScenarioResult("S1 正常握手成功", passed,
                          "两条命令往返 + 共享内存状态正确" if passed else
                          f"{c.fails} 项断言失败", c.items, trace)


def run_s2(verbose=False) -> ScenarioResult:
    """S2 GPUEB 未加载（超时）

    场景：LK 没加载 gpueb 固件（或 MFG0 没上电）。AP 发出去，没人应答。
    期望：INIT_SHARED_MEM 超时（GPUEB_ETIMEOUT），不是 TXSTUCK。
          因为 SRAM 是可写的（内存存在），只是没人取走消息。
    """
    cfg = GpuebConfig(present=False, clock_on=False)
    shm = SharedMem(initialized=False, fill=0xFF)
    sim = Sim(cfg, shm, verbose=verbose)

    t0 = sim.now_ms()
    init_status, init_ack = gpueb_ipi_xfer(
        sim,
        build_init_shared_mem(SHARED_PHYS, SHARED_PHYS + GPUFREQ_STATUS_SZ,
                              GPUFREQ_STATUS_SZ, GPUFREQ_DEBUG_SZ),
        TMO_INIT_MS)
    elapsed = sim.now_ms() - t0

    # 正确的驱动在超时后必须停止，不再发 POWER_CONTROL
    sent_power = False
    power_status = None
    if init_status == GPUEB_OK:
        sent_power = True
        power_status, _ = gpueb_ipi_xfer(sim, build_power_control(POWER_ON), TMO_POWER_MS)

    c = Checker()
    c.eq(init_status, GPUEB_ETIMEOUT, "S2.1 INIT 超时返回 GPUEB_ETIMEOUT")
    c.eq(init_ack, None, "S2.2 无应答（ack is None）")
    c.eq(init_status != GPUEB_ETXSTUCK, True, "S2.3 不是 TXSTUCK（SRAM 可写，只是没人取）")
    c.check(elapsed >= TMO_INIT_MS, "S2.4 等满了超时窗口",
            f"elapsed={elapsed}ms, timeout={TMO_INIT_MS}ms")
    c.eq(sent_power, False, "S2.5 失败后不再发 POWER_CONTROL（快速失败）")
    c.eq(sim.ap.rx_sts, 0, "S2.6 超时后 RX_STS 已清（回滚到位）")
    c.eq(shm.initialized, False, "S2.7 共享内存仍未被登记")
    c.check(shm.power_control == 0xFFFFFFFF, "S2.8 power_control 仍是脏值（固件没碰过）",
            f"0x{shm.power_control:08x}")
    c.eq(sim.gpueb.state, "ABSENT", "S2.9 GPUEB 状态为 ABSENT")
    c.eq(sim.sram[GPUFREQ_TX_OFF:GPUFREQ_TX_OFF + 4],
         struct.pack("<I", CMD_INIT_SHARED_MEM),
         "S2.10 消息确实写进了 SRAM（证明不是根本没写）")

    trace = [f"[AP] INIT_SHARED_MEM -> status={init_status} elapsed={elapsed}ms"] + \
            sim.gpueb.trace
    passed = c.fails == 0
    return ScenarioResult("S2 GPUEB 未加载（超时）", passed,
                          f"1 次超时后快速失败，耗时 {elapsed}ms" if passed else
                          f"{c.fails} 项断言失败", c.items, trace)


def run_s3(verbose=False) -> ScenarioResult:
    """S3 GPUEB 已加载但拒绝 power_on

    场景：固件在跑，INIT 成功，但 POWER_ON 被策略拒绝（返回 -22）。
    期望：传输成功（GPUEB_OK），ack < 0 → 驱动映射成 GPUEB_EREFUSED。
          **不能**把「收到应答」当成成功。
    """
    cfg = GpuebConfig(present=True, clock_on=True,
                      accept_power_on=False, power_on_err=-22)
    shm = SharedMem(initialized=False, fill=0xFF)
    sim = Sim(cfg, shm, verbose=verbose)

    init_status, init_ack = gpueb_ipi_xfer(
        sim,
        build_init_shared_mem(SHARED_PHYS, SHARED_PHYS + GPUFREQ_STATUS_SZ,
                              GPUFREQ_STATUS_SZ, GPUFREQ_DEBUG_SZ),
        TMO_INIT_MS)
    power_status, power_ack = gpueb_ipi_xfer(
        sim, build_power_control(POWER_ON), TMO_POWER_MS)

    # 驱动侧的错误映射（报告 §10 的 if ((s32)ack < 0) 分支）
    if power_status != GPUEB_OK:
        final = power_status
    elif power_ack is not None and power_ack < 0:
        final = GPUEB_EREFUSED
    else:
        final = GPUEB_OK

    c = Checker()
    c.eq(init_status, GPUEB_OK, "S3.1 INIT 成功")
    c.eq(init_ack, 0, "S3.2 INIT ack==0")
    c.eq(power_status, GPUEB_OK, "S3.3 POWER_CONTROL 传输层成功（收到应答）")
    c.eq(power_ack, -22, "S3.4 应答 return_value == -22")
    c.eq(final, GPUEB_EREFUSED, "S3.5 驱动正确映射为 GPUEB_EREFUSED")
    c.eq(sim.gpueb.state, Gpueb.ST_REJECT, "S3.6 GPUEB 状态 REJECT")
    c.eq(shm.power_count, 0, "S3.7 power_count 仍为 0（没上电）")
    c.check((shm.dvfs_state & DVFS_POWEROFF) != 0, "S3.8 dvfs_state 的 POWEROFF 位仍在",
            f"dvfs_state=0x{shm.dvfs_state:x}")
    c.check(shm.initialized, "S3.9 共享内存已登记（INIT 不能被回滚）",
            f"initialized={shm.initialized}")
    c.eq(sim.ap.rx_sts, 0, "S3.10 RX_STS 无残留")

    trace = [f"[AP] INIT -> {init_status}/{init_ack}",
             f"[AP] POWER -> {power_status}/{power_ack}",
             f"[AP] final = {final} (GPUEB_EREFUSED={GPUEB_EREFUSED})"] + sim.gpueb.trace
    passed = c.fails == 0
    return ScenarioResult("S3 GPUEB 拒绝 power_on", passed,
                          "收到应答但 ack<0，正确判为拒绝" if passed else
                          f"{c.fails} 项断言失败", c.items, trace)


def run_s4(verbose=False) -> ScenarioResult:
    """S4 共享内存未初始化

    场景：跳过 CMD_INIT_SHARED_MEM 直接发 POWER_CONTROL。
    期望：固件拒绝（-22）。而**错误的驱动**会以为成功——因为传输层 OK。
          这个场景专门验证「必须按顺序发」。
    """
    cfg = GpuebConfig(present=True, clock_on=True, accept_power_on=True)
    shm = SharedMem(initialized=False, fill=0xFF)
    sim = Sim(cfg, shm, verbose=verbose)

    # 故意跳过 INIT
    power_status, power_ack = gpueb_ipi_xfer(
        sim, build_power_control(POWER_ON), TMO_POWER_MS)

    if power_status != GPUEB_OK:
        final = power_status
    elif power_ack is not None and power_ack < 0:
        final = GPUEB_EREFUSED
    else:
        final = GPUEB_OK

    c = Checker()
    c.eq(power_status, GPUEB_OK, "S4.1 传输层仍成功（消息送到了）")
    c.eq(power_ack, -22, "S4.2 固件拒绝：return_value==-22")
    c.eq(final, GPUEB_EREFUSED, "S4.3 驱动判为失败（不会被传输成功骗过）")
    c.eq(shm.initialized, False, "S4.4 共享内存仍未初始化")
    c.check(shm.power_count == 0xFFFFFFFF - (1 << 32) or shm.power_count == -1,
            "S4.5 power_count 仍是脏值 -1（没上过电）",
            f"power_count={shm.power_count}")
    c.eq(sim.gpueb.state, Gpueb.ST_REJECT, "S4.6 GPUEB 状态 REJECT")
    c.eq(sim.ap.rx_sts, 0, "S4.7 RX_STS 无残留")

    # 反证：如果驱动只检查传输层（错误实现），会误判成功
    naive_ok = (power_status == GPUEB_OK)
    c.check(naive_ok, "S4.8 反证：只查传输层会误判为成功（说明必须查 ack）",
            f"naive_ok={naive_ok}")

    # 补救：补发 INIT 后重试应成功
    init_status, _ = gpueb_ipi_xfer(
        sim,
        build_init_shared_mem(SHARED_PHYS, SHARED_PHYS + GPUFREQ_STATUS_SZ,
                              GPUFREQ_STATUS_SZ, GPUFREQ_DEBUG_SZ),
        TMO_INIT_MS)
    retry_status, retry_ack = gpueb_ipi_xfer(
        sim, build_power_control(POWER_ON), TMO_POWER_MS)
    c.eq(init_status, GPUEB_OK, "S4.9 补发 INIT 成功")
    c.eq(retry_status, GPUEB_OK, "S4.10 重试 POWER_CONTROL 传输成功")
    c.eq(retry_ack, 1, "S4.11 重试 ack==1（补救有效）")
    c.eq(sim.gpueb.state, Gpueb.ST_POWERED, "S4.12 最终 POWERED")

    trace = [f"[AP] POWER(no init) -> {power_status}/{power_ack}",
             f"[AP] INIT(retry) -> {init_status}",
             f"[AP] POWER(retry) -> {retry_status}/{retry_ack}"] + sim.gpueb.trace
    passed = c.fails == 0
    return ScenarioResult("S4 共享内存未初始化", passed,
                          "被拒绝，补发 INIT 后补救成功" if passed else
                          f"{c.fails} 项断言失败", c.items, trace)


# ═══════════════════════════════════════════════════════════════════════════
# 内部自检（--selftest）
# ═══════════════════════════════════════════════════════════════════════════

def selftest() -> int:
    fails = 0

    def chk(cond, msg):
        nonlocal fails
        if not cond:
            print(f"  FAIL  {msg}")
            fails += 1

    print("── selftest: 常量与布局 ──")
    chk(MBOX_SRAM_SIZE == 0x280, "SRAM 640 B")
    chk(MBOX_SRAM_PHYS + MBOX_SRAM_SIZE == 0x13C60000, "SRAM 终点 0x13c60000")
    chk(MBOX_SEND_PHYS == 0x13C62000, "TX_STS 绝对地址")
    chk(MBOX_SET_PHYS == 0x13C62004, "IRQ_SET 绝对地址")
    chk(MBOX_CLR_PHYS == 0x13C62074, "IRQ_CLR 绝对地址")
    chk(MBOX_RECV_PHYS == 0x13C62078, "RX_STS 绝对地址")
    chk(MBOX_SRAM_PHYS + GPUFREQ_TX_OFF == 0x13C5FD90, "GPUFREQ TX 绝对地址")
    chk(MBOX_SRAM_PHYS + GPUFREQ_RX_OFF == 0x13C5FE3C, "GPUFREQ RX 绝对地址")
    chk(GPUFREQ_TX_OFF == 0x10, "TX 偏移 0x10")
    chk(GPUFREQ_RX_OFF == 0xBC, "RX 偏移 0xbc")
    chk(TX_SLOT_GPUFREQ == 4, "TX slot 4")
    chk(RX_SLOT_GPUFREQ == 47, "RX slot 47")
    chk(RX_SLOT_GPUFREQ != TX_SLOT_GPUFREQ, "RX slot != TX slot（关键陷阱）")
    chk(RX_SLOT_GPUFREQ * 4 + MSG_BYTES <= MBOX_SRAM_SIZE, "RX 消息不越界")
    chk(43 + 26 == 69 <= 160, "slot 总数 69 <= 160")
    chk(GPUFREQ_DEBUG_SZ == 0xC00, "debug 0xc00")
    chk(OFF_POWER_CONTROL == 0x60 and OFF_DVFS_STATE == 0x64
        and OFF_SHADER_PRESENT == 0x68 and OFF_POWER_COUNT == 0x6c,
        "status 字段偏移 0x60/0x64/0x68/0x6c")

    print("── selftest: 消息编码 ──")
    m = build_init_shared_mem(SHARED_PHYS, SHARED_PHYS + GPUFREQ_STATUS_SZ,
                              GPUFREQ_STATUS_SZ, GPUFREQ_DEBUG_SZ)
    chk(len(m) == 32, "消息 32 字节")
    w = struct.unpack("<8I", m)
    chk(w[0] == CMD_INIT_SHARED_MEM == 0, "w0 = CMD_INIT_SHARED_MEM = 0")
    chk(w[1] == 0, "w1 = target = 0")
    chk(w[2] == SHARED_PHYS, "w2 = status_base 低 32")
    chk(w[3] == 0, "w3 = status_base 高 32")
    chk(w[4] == SHARED_PHYS + 0x400, "w4 = debug_base 低 32")
    chk(w[5] == 0, "w5 = debug_base 高 32")
    chk(w[6] == 0x400, "w6 = status_size")
    chk(w[7] == 0xC00, "w7 = debug_size")

    p = build_power_control(POWER_ON)
    pw = struct.unpack("<8I", p)
    chk(pw[0] == 6, "power w0 = 6")
    chk(pw[2] == 1, "power w2 = POWER_ON = 1")
    chk(pw[1] == 0 and pw[3] == 0 and pw[7] == 0, "power 其余为 0")

    po = struct.unpack("<8I", build_power_control(POWER_OFF))
    chk(po[2] == 0, "POWER_OFF = 0")

    print("── selftest: 寄存器模型 ──")
    ep = EndPoint(bytearray(MBOX_SRAM_SIZE))
    ep.rx_sts |= BIT(CHAN_GPUFREQ)
    chk(ep.rx_sts == 0x2, "bit1 = 通道 1")
    ep.rx_sts &= ~BIT(CHAN_GPUFREQ)
    chk(ep.rx_sts == 0, "清位生效")
    chk(BIT(0) == 1 and BIT(7) == 128, "BIT() 正确")

    print("── selftest: 共享内存模型 ──")
    s = SharedMem(initialized=False, fill=0xFF)
    chk(s.power_control == 0xFFFFFFFF, "未初始化时 power_control 是 0xFF..")
    s.power_control = 1
    chk(s.power_control == 1, "power_control 可写")
    s.power_count = -1
    chk(s.power_count == -1, "power_count 是有符号")
    chk(len(s.buf) == 0x1000, "共享内存 4 KiB")

    print("── selftest: GPUEB 状态机 ──")
    cfg = GpuebConfig()
    shm = SharedMem(fill=0xFF)
    sim = Sim(cfg, shm)
    chk(sim.gpueb.state == Gpueb.ST_IDLE, "初始 IDLE（present=True）")
    cfg2 = GpuebConfig(present=False)
    sim2 = Sim(cfg2, SharedMem())
    chk(sim2.gpueb.state == "ABSENT", "present=False -> ABSENT")

    # 走一遍正常流程
    st, ack = gpueb_ipi_xfer(
        sim,
        build_init_shared_mem(SHARED_PHYS, SHARED_PHYS + 0x400, 0x400, 0xC00),
        TMO_INIT_MS)
    chk(st == GPUEB_OK and ack == 0, "selftest INIT 往返")
    st, ack = gpueb_ipi_xfer(sim, build_power_control(POWER_ON), TMO_POWER_MS)
    chk(st == GPUEB_OK and ack == 1, "selftest POWER 往返")
    chk(sim.gpueb.state == Gpueb.ST_POWERED, "selftest 到 POWERED")

    # 幂等性：再上电一次，refcount 变 2
    st, ack = gpueb_ipi_xfer(sim, build_power_control(POWER_ON), TMO_POWER_MS)
    chk(ack == 2, "第二次 POWER_ON -> refcount 2")
    st, ack = gpueb_ipi_xfer(sim, build_power_control(POWER_OFF), TMO_POWER_MS)
    chk(ack == 1, "POWER_OFF -> refcount 1")
    st, ack = gpueb_ipi_xfer(sim, build_power_control(POWER_OFF), TMO_POWER_MS)
    chk(ack == 0, "POWER_OFF -> refcount 0")
    chk((shm.dvfs_state & DVFS_POWEROFF) != 0, "refcount 归零后 DVFS_POWEROFF 置位")

    # 非法 cmd
    bad = bytearray(32)
    struct.pack_into("<I", bad, 0, 99)
    st, ack = gpueb_ipi_xfer(sim, bytes(bad), TMO_POWER_MS)
    chk(ack == -22, "非法 cmd -> -22")

    # 验证「RX slot != TX slot」这个陷阱真的会读出不同内容
    sim3 = Sim(GpuebConfig(), SharedMem(fill=0xFF))
    gpueb_ipi_xfer(sim3,
                   build_init_shared_mem(SHARED_PHYS, SHARED_PHYS + 0x400, 0x400, 0xC00),
                   TMO_INIT_MS)
    tx_words = struct.unpack("<8I", bytes(sim3.sram[GPUFREQ_TX_OFF:GPUFREQ_TX_OFF + MSG_BYTES]))
    rx_words = struct.unpack("<8I", bytes(sim3.sram[GPUFREQ_RX_OFF:GPUFREQ_RX_OFF + MSG_BYTES]))
    chk(tx_words[0] == CMD_INIT_SHARED_MEM, "TX slot w0 是命令号 0")
    chk(rx_words[0] == CMD_INIT_SHARED_MEM, "RX slot w0 是应答的命令号 0")
    chk(tx_words[2] == SHARED_PHYS, "TX w2 = status_base")
    chk(rx_words[2] == 0, "RX w2 = return_value = 0")
    chk(tx_words != rx_words, "TX slot 与 RX slot 整体内容不同（证明不能混用）")
    chk(GPUFREQ_TX_OFF != GPUFREQ_RX_OFF, "TX/RX 偏移不同")

    print()
    if fails == 0:
        print(f"selftest: ALL PASS")
    else:
        print(f"selftest: {fails} FAIL")
    return fails


# ═══════════════════════════════════════════════════════════════════════════
# 主程序
# ═══════════════════════════════════════════════════════════════════════════

def print_result(r: ScenarioResult, show_trace: bool):
    mark = "PASS" if r.passed else "FAIL"
    print(f"\n{'=' * 74}")
    print(f"[{mark}] {r.name}")
    print(f"       {r.detail}")
    print(f"{'-' * 74}")
    for msg, ok, got in r.assertions:
        sym = "✓" if ok else "✗"
        line = f"  {sym} {msg}"
        if not ok and got:
            line += f"   ({got})"
        print(line)
    if show_trace:
        print(f"{'-' * 74}")
        for t in r.trace:
            print("  " + t)


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(
        description="MT6895 GPUEB mailbox/IPI 状态机推演",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__)
    ap.add_argument("--selftest", action="store_true",
                    help="跑内部自检 + 全部场景")
    ap.add_argument("--verbose", action="store_true",
                    help="打印每次寄存器访问")
    ap.add_argument("--trace", action="store_true",
                    help="打印状态迁移轨迹")
    ap.add_argument("--only", choices=["S1", "S2", "S3", "S4"],
                    help="只跑一个场景")
    args = ap.parse_args(argv)

    rc = 0
    if args.selftest:
        print("╔" + "═" * 72 + "╗")
        print("║ gpueb-sim.py  内部自检" + " " * 49 + "║")
        print("╚" + "═" * 72 + "╝")
        rc |= selftest()
        print()

    print("╔" + "═" * 72 + "╗")
    print("║ MT6895 GPUEB mailbox/IPI 状态机推演" + " " * 36 + "║")
    print("║ 纯轮询 / 零中断模型  ·  slot: TX=4  RX=47" + " " * 27 + "║")
    print("╚" + "═" * 72 + "╝")

    runners = [("S1", run_s1), ("S2", run_s2), ("S3", run_s3), ("S4", run_s4)]
    if args.only:
        runners = [(k, f) for k, f in runners if k == args.only]

    results = []
    for key, fn in runners:
        r = fn(verbose=args.verbose)
        results.append(r)
        print_result(r, show_trace=args.trace)
        if not r.passed:
            rc = 1

    print(f"\n{'=' * 74}")
    npass = sum(1 for r in results if r.passed)
    print(f"汇总: {npass}/{len(results)} 场景通过")
    for r in results:
        print(f"  {'PASS' if r.passed else 'FAIL'}  {r.name}")
    print(f"{'=' * 74}")

    return rc


if __name__ == "__main__":
    sys.exit(main())
