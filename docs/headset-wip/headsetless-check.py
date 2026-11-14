#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""在「没有实体耳机」这个约束下，对一次 SPMI dump 做可判定的体检。

输入是驱动自己打印的行，形状就是 hakios.mt6895_headset.c 的
（/home/lizi/IOS/hakios-mtk，treemtk/mt6895/drivers/audio/hakios.mt6895_headset.c:508）：

    headset: 0x26b6 = 0xff
    headset: read timeout at 0x26b6; dump stopped
    headset: sample IRQ 0x00 AB 3 EINT0 3 FSM 0 sync 0

输出三块：逐项判定（PASS/FAIL/SKIP/INFO）、与 stock 基线的逐寄存器 diff、
以及通过/失败汇总。判定口径来自 findings/headsetless-checks.json，
本脚本是该 JSON 的可执行版本，两者必须同步改。

三件事这个脚本刻意不做：
  1. 不把 0x26b7 的 STATE 当判据。vendor 驱动全文没读过该字段，语义未验证。
  2. 不把 0x26a9 bit0 当「已插入」。它是中断标志，空插座下恒 0，插入与否只看 0x26b6[7:6]。
  3. 不把 0x26b6 == 0xff 当成「VAUD18 已上电」的证明。VAUD18 死掉时读数同样落在 AB=3。

usage:
    headsetless-check.py <dump.txt> [--baseline FILE] [--second FILE] [--post-init] [--verbose]
    headsetless-check.py --selftest

exit:
    0  全部 PASS（允许 SKIP/INFO）
    1  有 FAIL
    2  dump 本身不可用（空、全 0x00、全 0xFF 死总线）
    3  baseline 读取失败
"""
import argparse
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_BASELINE = os.path.join(HERE, "accdet-baseline.json")

ACCDET_LO, ACCDET_HI = 0x2688, 0x26d3
AUDDEC_LO, AUDDEC_HI = 0x2508, 0x2537

PASS, FAIL, SKIP, INFO = "PASS", "FAIL", "SKIP", "INFO"
# VOID 与 SKIP 的区别：SKIP = 这次 dump 没带这个地址；
# VOID = 带了，但读通道已死，数值对上也没有信息量。两者都不算通过。
VOID = "VOID"

# 驱动的一次读数。允许 "headset: 0x2688 = 0x00"、"headset: 0x2688=0x00"，
# 也允许行首有内核时间戳（XNU 的 printf 会带）。值必须恰好 1..2 位十六进制。
VALUE_LINE = re.compile(
    r"0x([0-9a-fA-F]{4})\s*=\s*0x([0-9a-fA-F]{1,2})(?![0-9a-fA-F])")
# 一条带 marker 却没有合法 (addr)=value 的行，是不是超时声明。
TIMEOUT_LINE = re.compile(r"read timeout at\s+0x([0-9a-fA-F]{4})")
SAMPLE_LINE = re.compile(
    r"sample\s+IRQ\s+0x([0-9a-fA-F]{1,2})\s+AB\s+(\d)\s+EINT0\s+(\d)"
    r"\s+FSM\s+(\d)\s+sync\s+(\d)")
MARKER = re.compile(r"headset\s*:", re.IGNORECASE)
# 一行里有没有寄存器地址（用来区分「坏行」与「无值的合法提示行」）。
HEX_ADDR = re.compile(r"0x[0-9a-fA-F]{4}")

# 死总线判据里的两个常数，来自 accdet-baseline.json 的实际统计：
#   124 个块寄存器里 0x00 有 74 个、0xFF 有 7 个、不同值 27 种。
# 全 0x00 的假 dump 对 130 项基线能拿到 78 项相等（60.0%），
# 所以「占比」绝对不能单独用作判据，否则死总线会以「看起来不错」通过。
BLOCK_TOTAL = 124
DEAD_FF_MAX = 12
DEAD_ZERO_MAX = 96
DEAD_DISTINCT_MIN = 12
MIN_BLOCK_PRESENT = 100


def load_baseline(path):
    with open(path, encoding="utf-8") as f:
        base = json.load(f)
    blocks, misc = {}, {}
    for name, region in base["regions"].items():
        target = misc if name == "misc" else blocks
        for reg, value in region["values"].items():
            target[int(reg, 16)] = value
    return base, blocks, misc


def parse_dump(path):
    """-> dict reg->value, 以及统计。坏行不静默丢弃，计数并留样。"""
    parsed, samples = {}, []
    stats = {"lines": 0, "marked": 0, "malformed": [], "timeouts": [],
             "duplicates": [], "notes": []}
    with open(path, encoding="utf-8", errors="replace") as f:
        for raw in f:
            stats["lines"] += 1
            line = raw.rstrip("\n")
            if not MARKER.search(line):
                continue
            stats["marked"] += 1
            m = SAMPLE_LINE.search(line)
            if m:
                samples.append({"irq": int(m.group(1), 16), "ab": int(m.group(2)),
                                "eint": int(m.group(3)), "fsm": int(m.group(4)),
                                "sync": int(m.group(5))})
                continue
            t = TIMEOUT_LINE.search(line)
            if t:
                stats["timeouts"].append(int(t.group(1), 16))
                continue
            v = VALUE_LINE.search(line)
            if not v:
                # 驱动还有两种合法但无值的行：横幅（"headset: mode 1 on SID 5..."）与
                # 待实现提示（"headset: headset=2 is not implemented"）。它们不含 0x 地址，
                # 不当坏行；含地址却没有值的才算坏行，因为那多半是值被截断。
                if not HEX_ADDR.search(line):
                    stats["notes"].append(line.strip())
                elif len(stats["malformed"]) < 20:
                    stats["malformed"].append(line.strip())
                continue
            reg, value = int(v.group(1), 16), int(v.group(2), 16)
            if reg in parsed and parsed[reg] != value and len(stats["duplicates"]) < 20:
                stats["duplicates"].append((reg, parsed[reg], value))
            parsed[reg] = value
    return parsed, samples, stats


def in_blocks(reg):
    return ACCDET_LO <= reg <= ACCDET_HI or AUDDEC_LO <= reg <= AUDDEC_HI


def blocks_of(regs):
    return {r: v for r, v in regs.items() if in_blocks(r)}


class Report(object):
    def __init__(self):
        self.rows = []

    def add(self, cid, status, name, detail):
        self.rows.append((cid, status, name, detail, PHASE.get(cid, "?")))

    def count(self, status):
        return sum(1 for r in self.rows if r[1] == status)

    def phase_count(self, phase, status):
        return sum(1 for r in self.rows if r[4] == phase and r[1] == status)

    def worst(self):
        if self.count(FAIL):
            return FAIL
        return PASS


def judge(regs, samples, stats, base, blocks_base, misc_base, post_init,
          second_regs=None):
    """按 headsetless-checks.json 的口径逐项判定。返回 Report。"""
    rep = Report()
    block = blocks_of(regs)
    n_block = len(block)

    # ---------- A 组：PMIF 通道健康 ----------
    if 0x231d in regs:
        v = regs[0x231d]
        ok = v == 0x06
        detail = "got 0x%02x" % v
        if v == 0x00:
            detail += "  (芯片被复位过：基线作废，通道未必坏)"
        elif v == 0xff:
            detail += "  (从设备无应答：SID 错 / 仲裁没起 / MPU 拒绝)"
        rep.add("A1", PASS if ok else FAIL, "0x231d == 0x06 (SID 5 自检值)", detail)
    else:
        rep.add("A1", SKIP, "0x231d == 0x06 (SID 5 自检值)", "dump 里没有 0x231d")

    distinct = len(set(block.values())) if block else 0
    if n_block >= MIN_BLOCK_PRESENT:
        rep.add("A2", PASS if distinct >= DEAD_DISTINCT_MIN else FAIL,
                "块内不同值 >= %d" % DEAD_DISTINCT_MIN,
                "%d 个值 / %d 个寄存器（基线 124 个里 27 种）" % (distinct, n_block))
    else:
        rep.add("A2", SKIP, "块内不同值 >= %d" % DEAD_DISTINCT_MIN,
                "只有 %d 个块寄存器，不足 %d，不做统计判定" % (n_block, MIN_BLOCK_PRESENT))

    if n_block >= MIN_BLOCK_PRESENT:
        n_ff = sum(1 for v in block.values() if v == 0xff)
        rep.add("A3", PASS if n_ff <= DEAD_FF_MAX else FAIL,
                "块内 0xFF <= %d" % DEAD_FF_MAX,
                "%d 个 0xFF（基线 7 个；>= 120 即全 FF 死总线）" % n_ff)
        n_zero = sum(1 for v in block.values() if v == 0x00)
        rep.add("A4", PASS if n_zero <= DEAD_ZERO_MAX else FAIL,
                "块内 0x00 <= %d" % DEAD_ZERO_MAX,
                "%d 个 0x00（基线 74 个；>= 120 即全 00 死总线）" % n_zero)
    else:
        rep.add("A3", SKIP, "块内 0xFF <= %d" % DEAD_FF_MAX, "块寄存器不足")
        rep.add("A4", SKIP, "块内 0x00 <= %d" % DEAD_ZERO_MAX, "块寄存器不足")

    # A5 芯片身份。0x0008/0x0009 不在驱动 dump 范围里（dump 只读两块），
    # 所以缺了是 SKIP 不是 FAIL —— 这一点很容易被误当成「没通过」。
    want_a5 = {0x0008: 0x30, 0x0009: 0x68}
    have_a5 = [r for r in want_a5 if r in regs]
    if len(have_a5) == len(want_a5):
        bad = ["0x%04x 0x%02x≠0x%02x" % (r, regs[r], want_a5[r])
               for r in sorted(want_a5) if regs[r] != want_a5[r]]
        rep.add("A5", PASS if not bad else FAIL, "0x0008=0x30 且 0x0009=0x68",
                "ok" if not bad else "; ".join(bad))
    else:
        rep.add("A5", SKIP, "0x0008=0x30 且 0x0009=0x68",
                "dump 未覆盖 0x0008/0x0009（当前驱动只读两块，需先加地址）")

    # A6 镜像对。stock 里这 7 对逐对相等，相等 ⇒ 没有丢字节、没有错位。
    pairs = [(0x268e, 0x2690), (0x268f, 0x2691), (0x2698, 0x269a),
             (0x26c4, 0x26c6), (0x26c5, 0x26c7), (0x26c8, 0x26ca),
             (0x26c9, 0x26cb)]
    checked = [(a, b) for a, b in pairs if a in regs and b in regs]
    if checked:
        bad = ["0x%04x=0x%02x≠0x%04x=0x%02x" % (a, regs[a], b, regs[b])
               for a, b in checked if regs[a] != regs[b]]
        rep.add("A6", PASS if not bad else FAIL,
                "7 对镜像寄存器相等（地址对齐）",
                "%d 对中 %d 对不等%s" % (len(checked), len(bad),
                                     ("：" + "; ".join(bad)) if bad else ""))
    else:
        rep.add("A6", SKIP, "7 对镜像寄存器相等（地址对齐）", "镜像对未读到")

    # A7 完整性
    if n_block == BLOCK_TOTAL:
        rep.add("A7", PASS, "124 个块寄存器全部读到", "124/124")
    else:
        d = "%d/%d" % (n_block, BLOCK_TOTAL)
        if stats["timeouts"]:
            d += "；超时于 " + " ".join("0x%04x" % a for a in stats["timeouts"][:5])
        rep.add("A7", FAIL, "124 个块寄存器全部读到", d)

    # A8 两次 dump 一致性。放在 judge 里而不是 main 里，自检才能覆盖它。
    if second_regs is None:
        rep.add("A8", SKIP, "两次 dump 逐字节相同", "需要 --second 指向第二次 dump")
    else:
        d = compare_dumps(regs, second_regs)
        if not d:
            rep.add("A8", PASS, "两次 dump 逐字节相同", "0 差异")
        else:
            value_diff = [x for x in d if x[1] is not None and x[2] is not None]
            cover_diff = len(d) - len(value_diff)
            rep.add("A8", FAIL, "两次 dump 逐字节相同",
                    "%d 个地址不同（值不同 %d，只在一侧出现 %d）"
                    % (len(d), len(value_diff), cover_diff))

    # ---------- B 组：空插座基线 ----------
    def byte_check(cid, reg, want, name):
        if reg not in regs:
            rep.add(cid, SKIP, name, "dump 里没有 0x%04x" % reg)
            return
        v = regs[reg]
        rep.add(cid, PASS if v == want else FAIL, name,
                "got 0x%02x" % v if v == want else "got 0x%02x，期望 0x%02x" % (v, want))

    if 0x26b6 in regs:
        ab = (regs[0x26b6] >> 6) & 3
        detail = "0x26b6=0x%02x → cur_AB=%d" % (regs[0x26b6], ab)
        if ab == 0:
            detail += "  (三段耳机形状：MICBIAS 可能没供电，或基线不是空插座)"
        elif ab == 1:
            detail += "  (四段耳麦形状)"
        elif ab == 2:
            detail += "  (本平台驱动无此分支，出现即判错，先查位序)"
        # B1 的 PASS 有前提：若 VAUD18 没开，0x26b6==0xFF 与「真空插座」不可区分
        # （VAUD18 死掉时读 0x3 有两种读法，见 PLAN §4.5）。D3 没读到就明说没前提。
        if ab == 3:
            if 0x1cb2 not in regs:
                detail += "；未读到 0x1cb2，无法排除「VAUD18 未开」这一同形解释"
            elif not (regs[0x1cb2] & 1):
                detail += "；但 0x1cb2 bit0=0（VAUD18 未开），此 PASS 不构成空插座证据"
        rep.add("B1", PASS if ab == 3 else FAIL,
                "0x26b6 bit[7:6] == 3 (空插座 cur_AB=拔出)", detail)
    else:
        rep.add("B1", SKIP, "0x26b6 bit[7:6] == 3 (空插座 cur_AB=拔出)", "dump 里没有 0x26b6")

    byte_check("B2", 0x26b6, 0xff, "0x26b6 整字节 == 0xFF")
    byte_check("B3", 0x2688, 0x00, "0x2688 == 0x00 (AUXADC 开关关)")
    byte_check("B4", 0x26a9, 0x00, "0x26a9 == 0x00 (无 pending IRQ)")
    byte_check("B5", 0x26b8, 0xff, "0x26b8 == 0xFF (EINT0 侧 AB 也是拔出)")
    if 0x26b7 in regs:
        rep.add("B6", INFO, "0x26b7 STATE 只记录不判定",
                "0x26b7=0x%02x，STATE=%d（语义未验证，永不 FAIL）"
                % (regs[0x26b7], regs[0x26b7] & 7))
    else:
        rep.add("B6", SKIP, "0x26b7 STATE 只记录不判定", "dump 里没有 0x26b7")

    # B7 逐项比对。分母是基线全量 130，不是「读到的个数」——
    # 用读到的个数做分母，只读 3 个对 3 个就能 100%。
    want_all = dict(blocks_base)
    want_all.update(misc_base)
    total = len(want_all)
    present = [r for r in want_all if r in regs]
    agreed = [r for r in present if regs[r] == want_all[r]]
    differed = [r for r in present if regs[r] != want_all[r]]
    missing = [r for r in want_all if r not in regs]
    ratio = 100.0 * len(agreed) / total if total else 0.0
    thr = 110
    if len(present) >= MIN_BLOCK_PRESENT:
        rep.add("B7", PASS if len(agreed) >= thr else FAIL,
                "与 130 项基线一致 >= %d" % thr,
                "%d/%d 一致（%.1f%%），%d 不一致，%d 未读到"
                % (len(agreed), total, ratio, len(differed), len(missing)))
    else:
        rep.add("B7", SKIP, "与 130 项基线一致 >= %d" % thr,
                "只读到 %d 个基线寄存器，不足 %d" % (len(present), MIN_BLOCK_PRESENT))

    want_b8 = {0x2319: 0x00, 0x231d: 0x06, 0x2320: 0x00,
               0x2323: 0x00, 0x1108: 0x00, 0x11c0: 0xfc}
    bad = []
    seen = 0
    for reg, want in sorted(want_b8.items()):
        if reg not in regs:
            continue
        seen += 1
        if regs[reg] != want:
            bad.append("0x%04x 0x%02x≠0x%02x" % (reg, regs[reg], want))
    if seen:
        rep.add("B8", PASS if not bad else FAIL, "6 个块外寄存器逐个匹配",
                ("%d/%d 匹配" % (seen - len(bad), seen)) if not bad else "; ".join(bad))
    else:
        rep.add("B8", SKIP, "6 个块外寄存器逐个匹配", "6 个块外寄存器都没读到")

    want_b9 = {0x2515: 0x08, 0x2518: 0x15, 0x2519: 0x15, 0x251a: 0x15}
    bad = []
    seen = 0
    for reg, want in sorted(want_b9.items()):
        if reg not in regs:
            continue
        seen += 1
        if regs[reg] != want:
            bad.append("0x%04x 0x%02x≠0x%02x" % (reg, regs[reg], want))
    if seen == len(want_b9):
        rep.add("B9", PASS if not bad else FAIL, "AUDDEC 四项非零锚点",
                "ok" if not bad else "; ".join(bad))
    else:
        rep.add("B9", SKIP, "AUDDEC 四项非零锚点", "只读到 %d/4 项锚点" % seen)

    if 0x26b6 in regs and 0x26b8 in regs:
        a = (regs[0x26b6] >> 6) & 3
        b = (regs[0x26b8] >> 6) & 3
        rep.add("B10", PASS if a == b else FAIL,
                "0x26b6 与 0x26b8 的 bit[7:6] 一致",
                "0x26b6→%d, 0x26b8→%d" % (a, b))
    else:
        rep.add("B10", SKIP, "0x26b6 与 0x26b8 的 bit[7:6] 一致", "两个寄存器没都读到")

    if post_init:
        diff_block = [r for r in block if r in blocks_base and regs[r] != blocks_base[r]]
        # 离线算过：50 项表按序套在 stock 值上，块内净变化只有 0x268a 0x40→0x41
        # （第 48 步写 SW_EN=1）；现有驱动不写 SW_EN，净变化为 0。
        # 所以「只差 0x268a 且等于 0x41」是表版本差异，不是错；其余任何差异都是错。
        sw_en_only = (diff_block == [0x268a] and regs.get(0x268a) == 0x41)
        if not diff_block:
            detail = "0 差异（与不写 SW_EN 的实现一致）"
        elif sw_en_only:
            detail = "仅 0x268a 0x40→0x41：表第 48 步写了 SW_EN，属预期，非错误"
        else:
            detail = "%d 个差异：%s" % (len(diff_block),
                                      " ".join("0x%04x(0x%02x→0x%02x)"
                                               % (r, blocks_base[r], regs[r])
                                               for r in diff_block[:8]))
        rep.add("B11", PASS if (not diff_block or sw_en_only) else FAIL,
                "跑完初始化后 124 个块寄存器与基线 0 差异", detail)
    else:
        rep.add("B11", SKIP, "跑完初始化后 124 个块寄存器与基线 0 差异",
                "需要 --post-init（dump 取自初始化之后）")

    # ---------- D 组：前置项 ----------
    if 0x230c in regs:
        v = regs[0x230c]
        detail = "0x230c=0x%02x → bit0=%d" % (v, v & 1)
        if v & 1:
            detail += "  (ACCDET 时钟被 gate，整段会读回 0；不要盲写 0x230d/0x230e)"
        rep.add("D1", PASS if not (v & 1) else FAIL,
                "0x230c bit0 == 0 (ACCDET 时钟未关)", detail)
        rep.add("D2", INFO, "0x230c 整字节记录", "0x%02x" % v)
    else:
        rep.add("D1", SKIP, "0x230c bit0 == 0 (ACCDET 时钟未关)",
                "dump 未覆盖 0x230c（stock 也没有，需先加该地址到 dump）")
        rep.add("D2", SKIP, "0x230c 整字节记录", "未读到")

    if 0x1cb2 in regs:
        v = regs[0x1cb2]
        rep.add("D3", INFO, "0x1cb2 bit0 记录 (VAUD18)",
                "0x1cb2=0x%02x → bit0=%d%s" % (v, v & 1,
                                              "" if v & 1 else
                                              "（裸机必须自己开；开着也不能由 0x26b6=0xff 反推）"))
    else:
        rep.add("D3", SKIP, "0x1cb2 bit0 记录 (VAUD18)", "dump 未覆盖 0x1cb2")

    rep.add("D4", SKIP, "若 0x1cb2 bit0==0，初始化必须先写 1",
            "需要阶段 2 的写日志（当前 headset=1 只读，无写路径）")
    byte_check("D5", 0x2319, 0x00, "0x2319 == 0x00 (ACCDET 不在复位)")

    n_aud = sum(1 for r in regs if AUDDEC_LO <= r <= AUDDEC_HI)
    want_aud = AUDDEC_HI - AUDDEC_LO + 1
    rep.add("D6", PASS if n_aud == want_aud else FAIL,
            "AUDDEC 块 48 个寄存器全部读到", "%d/%d" % (n_aud, want_aud))

    # ---------- E 组：自身回环 ----------
    if samples:
        ab_ok = all(s["ab"] == 3 for s in samples[:10])
        rep.add("E1", PASS if ab_ok else FAIL, "连续 >=10 次采样 AB 恒为 3（幂等）",
                "%d 个样本，AB 集合 %s" % (len(samples),
                                          sorted({s["ab"] for s in samples})))
        eint_ok = all(s["eint"] == 3 for s in samples[:10])
        rep.add("E2", PASS if eint_ok else FAIL, "连续采样中 EINT0 恒为 3",
                "%d 个样本，EINT0 集合 %s" % (len(samples),
                                             sorted({s["eint"] for s in samples})))
    else:
        rep.add("E1", SKIP, "连续 >=10 次采样 AB 恒为 3（幂等）", "dump 里没有 sample 行")
        rep.add("E2", SKIP, "连续采样中 EINT0 恒为 3", "dump 里没有 sample 行")

    rep.add("E3", SKIP, "写 IRQ_CLR 不引入新 IRQ", "需要阶段 1 的写路径")
    rep.add("E4", SKIP, "0x26aa bit0 电平位可读回", "需要阶段 1 的写路径")

    # E5：空插座下不应有任何 cable/button 事件。只在有「整段日志」时才有意义，
    # 所以这里按「出现了就是 FAIL，没出现也不给 PASS」的口径：有就是硬错。
    rep.add("E5", INFO, "空插座下不得出现 cable/button 变更打印",
            "该断言检查的是原始日志全文，本脚本只读寄存器行；请在日志里 grep "
            "'headset: cable ' 与 'headset: microphone button'，应为 0 次")
    if 0x26b7 in regs:
        rep.add("E6", INFO, "0x26b7 STATE 与 AB 的一致性",
                "(STATE=%d, AB=%s)，仅记录"
                % (regs[0x26b7] & 7,
                   ((regs[0x26b6] >> 6) & 3) if 0x26b6 in regs else "?"))
    else:
        rep.add("E6", SKIP, "0x26b7 STATE 与 AB 的一致性", "未读到 0x26b7")

    # ---------- C 组：写通道（阶段 1）----------
    # 这些检查需要的是「写一条、读回、恢复」，而 headset=1 是纯读路径。
    # 一份 dump 里没有任何信息能证明写通道通不通，所以这里一律 SKIP，
    # 由阶段 1 的驱动日志单独判定（判据写在 headsetless-checks.json 的 C1..C7）。
    write_checks = {
        "C1": "最小写探针：0x26aa bit[6:4] 1→2→1（需写路径）",
        "C2": "写后读回在 mask 内等于写入值（需写路径）",
        "C3": "探针后 0x26aa 必须回到 0x10（需写路径）",
        "C4": "禁写门：0x014b / 0x0240 被拒（需写路径）",
        "C5": "写探针不得引起中断（需写路径）",
        "C6": "备用写探针：0x26d0 bit[6:4] 0→1→0（需写路径）",
        "C7": "探针结束后 PMIF STA 回到 IDLE（需写路径）",
    }
    for cid in sorted(write_checks):
        rep.add(cid, SKIP, write_checks[cid],
                "headset=1 只读，dump 无法判定；需阶段 1")

    # 死总线的覆盖判定。全 0x00 / 全 0xFF 时，下面这些项会「真空通过」：
    #   A3「0xFF 不多」在全 0 时当然成立；A6「镜像对相等」在全常数时当然相等；
    #   A7「124 个都读到」只说明事务完成了 124 次，不说明读到了不同内容；
    #   B3/B4/D5 的期望值本来就是 0x00，全 0 读回自然相等。
    # 这些 PASS 不能计入「通过」，否则死总线会拿一半 PASS。改判 VOID：
    # 不是失败，是这次 dump 对该项没有信息量。
    dead, reason = bus_dead(block)
    if dead:
        for i, (cid, status, name, detail, phase) in enumerate(rep.rows):
            if cid in VOID_IF_DEAD and status == PASS:
                rep.rows[i] = (cid, VOID, name,
                               "%s；但读通道已死（%s），此项无信息量" % (detail, reason),
                               phase)

    return rep, dict(total=total, agreed=agreed, differed=differed,
                     missing=missing, ratio=ratio, present=present)


# 读通道已死时，这些项即使数值上「对」也不能算通过。
VOID_IF_DEAD = {"A3", "A6", "A7", "B3", "B4", "B10", "D5", "D6"}


def bus_dead(block):
    """一个块的读回是不是死总线。返回 (bool, 原因)。

    只有「很多寄存器」才有资格这样判：单个寄存器读 0x00 是个诚实答案。
    """
    if len(block) >= 8 and len(set(block.values())) == 1:
        return True, "%d 个寄存器全是 0x%02x" % (len(block), list(block.values())[0])
    if len(block) >= 16 and len(set(block.values())) == 2:
        return True, "%d 个寄存器只有两个值 %s" % (
            len(block), "/".join("0x%02x" % v for v in sorted(set(block.values()))))
    return False, ""


# 每项属于哪个阶段。必须与 findings/headsetless-checks.json 的 "phase" 一致。
# 阶段 0 = 只读、零风险；阶段 1 = 最小写验证；阶段 2 = 跑初始化表后回读。
PHASE = {
    "A1": 0, "A2": 0, "A3": 0, "A4": 0, "A5": 0, "A6": 0, "A7": 0, "A8": 0,
    "B1": 0, "B2": 0, "B3": 0, "B4": 0, "B5": 0, "B6": 0, "B7": 0, "B8": 0,
    "B9": 0, "B10": 0, "B11": 2,
    "C1": 1, "C2": 1, "C3": 1, "C4": 1, "C5": 1, "C6": 1, "C7": 1,
    "D1": 0, "D2": 0, "D3": 0, "D4": 2, "D5": 0, "D6": 0,
    "E1": 0, "E2": 0, "E3": 1, "E4": 1, "E5": 0, "E6": 0,
}


def format_report(rep, diffinfo, base, second_diff=None):
    out = []
    out.append("== 判定（阶段 0 = 只读零风险 / 1 = 最小写 / 2 = 初始化表后）==")
    out.append("%-5s %-3s %-4s %-44s %s" % ("状态", "阶段", "项", "检查项", "实测"))
    out.append("-" * 118)
    for cid, status, name, detail, phase in rep.rows:
        out.append("%-5s %-3s %-4s %-44s %s"
                   % (status, phase, cid, name, detail))
    out.append("")

    if diffinfo:
        differed, missing = diffinfo["differed"], diffinfo["missing"]
        out.append("== 与 stock 基线 diff（source: %s）==" % base.get("source", "?"))
        out.append("基线 %d 项：一致 %d，不一致 %d，未读到 %d"
                   % (diffinfo["total"], len(diffinfo["agreed"]),
                      len(differed), len(missing)))
        if differed:
            out.append("")
            out.append("不一致（同地址、不同值）：")
            for reg in differed:
                out.append("  0x%04x  stock 0x%02x  xnu 0x%02x   (XOR 0x%02x)"
                           % (reg, diffinfo_lookup(base, reg), diffinfo["got"][reg],
                              diffinfo_lookup(base, reg) ^ diffinfo["got"][reg]))
        if missing:
            out.append("")
            out.append("未读到（在基线里、日志里没有）：%d 个" % len(missing))
            out.append("  " + " ".join("0x%04x" % r for r in missing[:24])
                       + (" ..." if len(missing) > 24 else ""))
        out.append("")

    if second_diff is not None:
        out.append("== 两次 dump 对比（A8）==")
        if not second_diff:
            out.append("两次 dump 逐字节相同")
        else:
            out.append("差异 %d 个：" % len(second_diff))
            for reg, a, b in second_diff[:20]:
                # 两次 dump 覆盖的地址可以不同（改过 dump 范围），缺失侧写 --。
                out.append("  0x%04x  %s vs %s"
                           % (reg,
                              ("0x%02x" % a) if a is not None else " -- ",
                              ("0x%02x" % b) if b is not None else " -- "))
        out.append("")

    out.append("== 汇总 ==")
    out.append("PASS %d   FAIL %d   SKIP %d   VOID %d   INFO %d"
               % (rep.count(PASS), rep.count(FAIL), rep.count(SKIP),
                  rep.count(VOID), rep.count(INFO)))
    out.append("")
    out.append("按阶段（可判定 = PASS+FAIL；SKIP/INFO 不计入分母）：")
    for phase, label in ((0, "只读零风险"), (1, "最小写验证"), (2, "初始化表后回读")):
        judged = rep.phase_count(phase, PASS) + rep.phase_count(phase, FAIL)
        out.append("  阶段 %d %-14s 可判定 %2d 项：PASS %2d  FAIL %2d  其余 %2d"
                   % (phase, label, judged, rep.phase_count(phase, PASS),
                      rep.phase_count(phase, FAIL),
                      rep.phase_count(phase, SKIP) + rep.phase_count(phase, VOID)
                      + rep.phase_count(phase, INFO)))
    return out


def diffinfo_lookup(base, reg):
    for region in base["regions"].values():
        v = region["values"].get("0x%04x" % reg)
        if v is not None:
            return v
    return None


def bus_verdict(regs, stats):
    """先判 dump 本身可不可以用。返回 (code, 说明列表)。"""
    lines = []
    if not regs:
        lines.append("VERDICT: dump 里一个寄存器都没有。")
        if not stats["marked"]:
            lines.append("         日志里连 'headset:' 这个 marker 都没有 —— "
                         "要么 headset=1 没传，要么这行不是驱动的输出。")
        else:
            lines.append("         有 %d 行带 marker，但都没有 <addr> = <value> 形状。"
                         % stats["marked"])
        return 2, lines
    block = blocks_of(regs)
    if len(block) >= 8 and len(set(block.values())) == 1:
        only = list(set(block.values()))[0]
        lines.append("VERDICT: %d 个块寄存器全部读回 0x%02x。" % (len(block), only))
        lines.append("         这是死总线，不是芯片状态：正常读回会逐寄存器不同。")
        if only == 0x00:
            lines.append("         注意它对本基线能拿到 78/130 相等（60.0%），"
                         "占比看起来像进展，其实不是。")
        return 2, lines
    if len(block) >= 16 and len(set(block.values())) == 2:
        vals = " ".join("0x%02x" % v for v in sorted(set(block.values())))
        lines.append("NOTE: %d 个块寄存器只出现两个值（%s），"
                     "在相信任何百分比之前先看这个。" % (len(block), vals))
    return 0, lines


def compare_dumps(a, b):
    out = []
    for reg in sorted(set(a) | set(b)):
        if a.get(reg) != b.get(reg):
            out.append((reg, a.get(reg), b.get(reg)))
    return out


def selftest():
    """用构造的假 dump 把每一条边界都压一遍。不碰真机、不碰真 dump。"""
    import tempfile
    base, blocks_base, misc_base = load_baseline(DEFAULT_BASELINE)
    want_all = dict(blocks_base)
    want_all.update(misc_base)

    def clean_lines(**override):
        """一份完全正确的 dump。override 用 reg 的十六进制串做键。"""
        out = []
        for reg in sorted(want_all):
            v = override.get("0x%04x" % reg, want_all[reg])
            out.append("headset: 0x%04x = 0x%02x" % (reg, v))
        return out

    def run(lines, second=None, post_init=False):
        with tempfile.NamedTemporaryFile("w", suffix=".txt", delete=False,
                                         encoding="utf-8") as f:
            f.write("\n".join(lines) + ("\n" if lines else ""))
            path = f.name
        try:
            regs, samples, stats = parse_dump(path)
            code, notes = bus_verdict(regs, stats)
            other = None
            if second is not None:
                with tempfile.NamedTemporaryFile("w", suffix=".txt", delete=False,
                                                encoding="utf-8") as g:
                    g.write("\n".join(second) + ("\n" if second else ""))
                    p2 = g.name
                try:
                    other = parse_dump(p2)[0]
                finally:
                    os.unlink(p2)
            rep, diffinfo = judge(regs, samples, stats, base, blocks_base,
                                  misc_base, post_init, other)
            second_diff = compare_dumps(regs, other) if other is not None else None
            return regs, samples, stats, rep, diffinfo, code, notes, second_diff
        finally:
            os.unlink(path)

    results = []

    def ok(name, cond, extra=""):
        results.append((name, bool(cond), extra))

    # 1. 干净 dump：只要 B11/A5/A8/E3/E4 是 SKIP/INFO，其余全 PASS。
    regs, samples, stats, rep, diff, code, notes, _ = run(clean_lines())
    ok("1 干净 dump 的 124 个块寄存器全被解析", len(blocks_of(regs)) == 124,
       "%d" % len(blocks_of(regs)))
    ok("1 干净 dump 无 FAIL", rep.count(FAIL) == 0,
       "FAIL=%d %s" % (rep.count(FAIL),
                       [r[0] for r in rep.rows if r[1] == FAIL]))
    ok("1 干净 dump 的 A1 为 PASS",
       [r[1] for r in rep.rows if r[0] == "A1"] == [PASS])
    ok("1 干净 dump 的 B1/B2/B3/B5 为 PASS",
       [r[1] for r in rep.rows if r[0] in ("B1", "B2", "B3", "B5")] == [PASS] * 4,
       [(r[0], r[1]) for r in rep.rows if r[0] in ("B1", "B2", "B3", "B5")])
    ok("1 干净 dump 的 A5 为 SKIP（0x0008 不在驱动 dump 范围）",
       [r[1] for r in rep.rows if r[0] == "A5"] == [SKIP])
    ok("1 干净 dump 的 bus verdict 为 0", code == 0, "%d" % code)

    # 2. 空文件：不是 FAIL，是「dump 不可用」，exit 2。
    regs, samples, stats, rep, diff, code, notes, _ = run([])
    ok("2 空文件 → exit 2", code == 2, "%d" % code)
    ok("2 空文件 → 0 个寄存器且 0 行 marker", len(regs) == 0 and stats["marked"] == 0)

    # 3. 全 0xFF：死总线。
    ff = ["headset: 0x%04x = 0xff" % r for r in sorted(blocks_base)]
    regs, samples, stats, rep, diff, code, notes, _ = run(ff)
    ok("3 全 0xFF → exit 2", code == 2, "%d" % code)
    ok("3 全 0xFF → A3 为 FAIL",
       [r[1] for r in rep.rows if r[0] == "A3"] == [FAIL])
    ok("3 全 0xFF → 判词提到死总线", any("死总线" in n for n in notes))

    # 4. 全 0x00：也要 exit 2，且 A4/B1 必须 FAIL。
    #    这是最关键的一条：全 0 假 dump 对基线能拿到 60% 相等。
    zero = ["headset: 0x%04x = 0x00" % r for r in sorted(blocks_base)]
    regs, samples, stats, rep, diff, code, notes, _ = run(zero)
    ok("4 全 0x00 → exit 2", code == 2, "%d" % code)
    ok("4 全 0x00 → A4/B1 都 FAIL",
       [r[1] for r in rep.rows if r[0] in ("A4", "B1")] == [FAIL, FAIL],
       [(r[0], r[1]) for r in rep.rows if r[0] in ("A4", "B1")])
    ok("4 全 0x00 的 B7 得分必须低于阈值",
       [r[1] for r in rep.rows if r[0] == "B7"] == [FAIL],
       [(r[0], r[1], r[3]) for r in rep.rows if r[0] == "B7"])
    # 这一条是整套脚本里最重要的一条：死总线上「0xFF 不多」「镜像对相等」
    # 「124 个都读到」「期望值是 0x00 的三项」在数值上全对，必须被判 VOID 而不是 PASS。
    # 这份合成 dump 只含 124 个块寄存器，块外的 D5 会是 SKIP 而不是 VOID，两者都可接受。
    ok("4 全 0x00 时 VOID_IF_DEAD 里没有一项是 PASS",
       all(r[1] in (VOID, SKIP) for r in rep.rows if r[0] in VOID_IF_DEAD),
       [(r[0], r[1]) for r in rep.rows if r[0] in VOID_IF_DEAD])
    ok("4 全 0x00 时 PASS 数为 0", rep.count(PASS) == 0,
       "PASS=%d" % rep.count(PASS))

    # 5. 只有部分寄存器：A7 FAIL，A2/A3/A4 SKIP（不足 100），B7 SKIP。
    partial = clean_lines()[:3]
    regs, samples, stats, rep, diff, code, notes, _ = run(partial)
    ok("5 只有 3 个寄存器 → A7 FAIL", [r[1] for r in rep.rows if r[0] == "A7"] == [FAIL])
    ok("5 只有 3 个寄存器 → A2/A3/A4/B7 都 SKIP",
       all(r[1] == SKIP for r in rep.rows if r[0] in ("A2", "A3", "A4", "B7")))

    # 6. 格式错误行：不被当成有效值，计数留样，程序不崩。
    bad = [
        "headset: 0x2688 = 0xGG",          # 非十六进制
        "headset: 0x2688 = 0x1234",        # 值超一字节
        "headset: nonsense 0x2688",        # 没有 =
        "headset: 0x26b6 = 0x",            # 值缺
        "headset: 0x2688 = 0xff",          # 唯一合法的一条
    ]
    regs, samples, stats, rep, diff, code, notes, _ = run(bad)
    ok("6 4 条坏行被计数、只剩 1 条生效",
       len(regs) == 1 and len(stats["malformed"]) == 4,
       "regs=%d malformed=%d" % (len(regs), len(stats["malformed"])))

    # 7. 单点差异：正好只列出那一个寄存器。
    one = clean_lines(**{"0x26c4": 0x00})
    regs, samples, stats, rep, diff, code, notes, _ = run(one)
    ok("7 单点 0x26c4 差异被精确列出",
       diff["differed"] == [0x26c4] and len(diff["agreed"]) == 129,
       "differed=%s agreed=%d" % (diff["differed"], len(diff["agreed"])))
    ok("7 单点差异仍判 B7 PASS",
       [r[1] for r in rep.rows if r[0] == "B7"] == [PASS])

    # 8. 超时行：A7 FAIL 且详情带地址。
    to = clean_lines()[:10] + ["headset: read timeout at 0x26c4; dump stopped"]
    regs, samples, stats, rep, diff, code, notes, _ = run(to)
    a7 = [r for r in rep.rows if r[0] == "A7"][0]
    ok("8 超时行 → A7 FAIL 且报出地址",
       a7[1] == FAIL and "0x26c4" in a7[3], a7[3])

    # 9. AB=0（三段形状）：B1 FAIL，且判词点出 MICBIAS。
    ab0 = clean_lines(**{"0x26b6": 0x3f})
    regs, samples, stats, rep, diff, code, notes, _ = run(ab0)
    b1 = [r for r in rep.rows if r[0] == "B1"][0]
    ok("9 AB=0 → B1 FAIL 且提到 MICBIAS", b1[1] == FAIL and "MICBIAS" in b1[3], b1[3])

    # 9b. B1 的 PASS 有前提：0x1cb2 必须在场且 bit0=1。
    #     这是本轮最容易误判的一处（VAUD18 死掉时 0x26b6 可能与真空插座同形）。
    regs, samples, stats, rep, diff, code, notes, _ = run(clean_lines())
    b1 = [r for r in rep.rows if r[0] == "B1"][0]
    ok("9b 无 0x1cb2 时 B1 的 PASS 附带「无法排除 VAUD18 未开」",
       b1[1] == PASS and "无法排除" in b1[3], b1[3])
    regs, samples, stats, rep, diff, code, notes, _ = run(
        clean_lines() + ["headset: 0x1cb2 = 0x00"])
    b1 = [r for r in rep.rows if r[0] == "B1"][0]
    ok("9b 0x1cb2 bit0=0 时 B1 明说「不构成空插座证据」",
       b1[1] == PASS and "不构成空插座证据" in b1[3], b1[3])
    regs, samples, stats, rep, diff, code, notes, _ = run(
        clean_lines() + ["headset: 0x1cb2 = 0x01"])
    b1 = [r for r in rep.rows if r[0] == "B1"][0]
    ok("9b 0x1cb2 bit0=1 时 B1 不再附带保留",
       b1[1] == PASS and "无法排除" not in b1[3] and "不构成" not in b1[3], b1[3])

    # 10. 0x26b6 与 0x26b8 不一致：B10 FAIL。
    mis = clean_lines(**{"0x26b8": 0x40})
    regs, samples, stats, rep, diff, code, notes, _ = run(mis)
    ok("10 两侧 AB 不一致 → B10 FAIL",
       [r[1] for r in rep.rows if r[0] == "B10"] == [FAIL])

    # 11. sample 行：10 次恒 AB=3 → E1/E2 PASS；混入 AB=0 → E1 FAIL。
    good_s = ["headset: sample IRQ 0x00 AB 3 EINT0 3 FSM 0 sync 0"] * 10
    regs, samples, stats, rep, diff, code, notes, _ = run(clean_lines() + good_s)
    ok("11 10 次 AB=3 → E1/E2 PASS",
       all(r[1] == PASS for r in rep.rows if r[0] in ("E1", "E2")),
       [(r[0], r[1]) for r in rep.rows if r[0] in ("E1", "E2")])
    bad_s = ["headset: sample IRQ 0x00 AB 3 EINT0 3 FSM 0 sync 0"] * 9 + \
            ["headset: sample IRQ 0x00 AB 0 EINT0 3 FSM 0 sync 0"]
    regs, samples, stats, rep, diff, code, notes, _ = run(clean_lines() + bad_s)
    ok("11 混入一次 AB=0 → E1 FAIL",
       [r[1] for r in rep.rows if r[0] == "E1"] == [FAIL])

    # 12. --post-init：干净 dump 的 B11 PASS；改一个字节的 B11 FAIL。
    regs, samples, stats, rep, diff, code, notes, _ = run(clean_lines(), post_init=True)
    ok("12 post-init 且无差异 → B11 PASS",
       [r[1] for r in rep.rows if r[0] == "B11"] == [PASS])
    # 只差 0x268a=0x41 是「表第 48 步写了 SW_EN」，属预期内的版本差异 → PASS。
    regs, samples, stats, rep, diff, code, notes, _ = run(
        clean_lines(**{"0x268a": 0x41}), post_init=True)
    b11 = [r for r in rep.rows if r[0] == "B11"][0]
    ok("12 post-init 且只差 0x268a=0x41 → B11 PASS 但注明 SW_EN",
       b11[1] == PASS and "SW_EN" in b11[3], b11[3])
    # 其它任何块内差异都必须 FAIL。
    regs, samples, stats, rep, diff, code, notes, _ = run(
        clean_lines(**{"0x26aa": 0x11}), post_init=True)
    b11 = [r for r in rep.rows if r[0] == "B11"][0]
    ok("12 post-init 且 0x26aa 多一个 bit → B11 FAIL 并指出该字节",
       b11[1] == FAIL and "0x26aa" in b11[3], b11[3])

    # 13. 两次 dump 相同 → 差异为空；差一个字节 → 恰好一条。
    regs, samples, stats, rep, diff, code, notes, sd = run(clean_lines(), second=clean_lines())
    ok("13 两次相同的 dump → 0 条差异", sd == [])
    regs, samples, stats, rep, diff, code, notes, sd = run(
        clean_lines(), second=clean_lines(**{"0x26b6": 0x00}))
    ok("13 差一个字节 → 恰好 1 条差异且是 0x26b6",
       len(sd) == 1 and sd[0][0] == 0x26b6, sd)
    # 覆盖范围不同（第二次少读了两个地址）不能当成值不同，也不能崩。
    # 覆盖范围不同（第二次少读 0x231d 与 0x2320）不能当成值不同，也不能崩。
    # 这两个地址都在真实基线的 misc 段里，所以「少读」是确定性的 2 个。
    regs, samples, stats, rep, diff, code, notes, sd = run(
        clean_lines(), second=[l for l in clean_lines()
                               if not l.startswith("headset: 0x231d")
                               and not l.startswith("headset: 0x2320")])
    a8 = [r for r in rep.rows if r[0] == "A8"][0]
    ok("13 第二次少读两个地址 → A8 FAIL 但报「值不同 0，只在一侧出现 2」",
       a8[1] == FAIL and "值不同 0" in a8[3] and "只在一侧出现 2" in a8[3], a8[3])

    # 14. 只有 marker 没有值（headset=2 未实现时的打印）：exit 2 但不崩。
    regs, samples, stats, rep, diff, code, notes, _ = run(
        ["headset: headset=2 is not implemented"])
    # 提示行不含 0x 地址 → 不算坏行，记为 note。这样「headset=2 未实现」不会被误报成格式错。
    ok("14 只有提示行 → exit 2，且不计坏行只记 note",
       code == 2 and len(stats["malformed"]) == 0 and len(stats["notes"]) == 1,
       "code=%d malformed=%d notes=%d"
       % (code, len(stats["malformed"]), len(stats["notes"])))
    # 一条坏行 + 一条提示行，必须只有坏行被计数。
    regs, samples, stats, rep, diff, code, notes, _ = run(
        ["headset: mode 1 on SID 5, serialised in keys worker",
         "headset: 0x2688 = 0xYY"])
    ok("14 横幅行不计坏行、坏值行计坏行",
       len(stats["notes"]) == 1 and len(stats["malformed"]) == 1,
       "notes=%r malformed=%r" % (stats["notes"], stats["malformed"]))

    failed = [r for r in results if not r[1]]
    for name, cond, extra in results:
        print("%s %s%s" % ("ok  " if cond else "FAIL", name,
                           ("   [" + str(extra) + "]") if (extra and not cond) else ""))
    print()
    if failed:
        print("%d/%d 条断言失败" % (len(failed), len(results)))
        return 1
    print("全部 %d 条断言通过" % len(results))
    return 0


def main():
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("dump", nargs="?", help="驱动打印的寄存器行所在文件")
    ap.add_argument("--baseline", default=DEFAULT_BASELINE,
                    help="stock 基线 JSON（默认 %(default)s）")
    ap.add_argument("--second", help="第二次 dump，用于 A8 一致性检查")
    ap.add_argument("--post-init", action="store_true",
                    help="dump 取自初始化之后，启用 B11（要求 0 差异）")
    ap.add_argument("--verbose", action="store_true", help="也列出未读到的寄存器全表")
    ap.add_argument("--selftest", action="store_true", help="用构造的假 dump 自检")
    args = ap.parse_args()

    if args.selftest:
        return selftest()
    if not args.dump:
        ap.error("需要一份 dump（或 --selftest）")

    try:
        base, blocks_base, misc_base = load_baseline(args.baseline)
    except (OSError, ValueError) as exc:
        print("baseline 读不了：%s" % exc)
        return 3

    regs, samples, stats = parse_dump(args.dump)
    second_regs = parse_dump(args.second)[0] if args.second else None
    second_diff = compare_dumps(regs, second_regs) if second_regs is not None else None
    rep, diffinfo = judge(regs, samples, stats, base, blocks_base, misc_base,
                          args.post_init, second_regs)
    diffinfo["got"] = regs

    print("== dump ==")
    print("文件     %s" % args.dump)
    print("行数     %d（带 headset: marker %d 行）"
          % (stats["lines"], stats["marked"]))
    print("寄存器   %d 个（块内 %d，distinct 值 %d）"
          % (len(regs), len(blocks_of(regs)),
             len(set(blocks_of(regs).values()))))
    if stats["notes"]:
        print("提示行   %d 行（无寄存器值，不算坏行）：" % len(stats["notes"]))
        for line in stats["notes"][:3]:
            print("           %s" % line)
    if stats["malformed"]:
        print("坏行     %d 行，前几条：" % len(stats["malformed"]))
        for line in stats["malformed"][:3]:
            print("           %s" % line)
    if stats["timeouts"]:
        print("超时     %s" % " ".join("0x%04x" % a for a in stats["timeouts"]))
    if stats["duplicates"]:
        print("重复     同一地址读了不同值：%s"
              % "; ".join("0x%04x %02x≠%02x" % d for d in stats["duplicates"][:4]))
    if samples:
        print("采样     %d 条 sample 行" % len(samples))
    print()

    code, notes = bus_verdict(regs, stats)
    for line in format_report(rep, diffinfo, base, second_diff):
        print(line)
    if args.verbose and diffinfo["missing"]:
        print("未读到的基线寄存器全表：")
        print("  " + " ".join("0x%04x" % r for r in diffinfo["missing"]))
        print()

    for line in notes:
        print(line)
    if code == 2:
        print()
        print("结论：这次 dump 不能用来判断任何寄存器语义，先解决读通道。")
        return 2

    print()
    if rep.count(FAIL):
        print("结论：%d 项失败，见上面 FAIL 行。" % rep.count(FAIL))
        return 1
    print("结论：无失败项。空插座基线成立，可以进入下一阶段。")
    return 0


if __name__ == "__main__":
    sys.exit(main())
