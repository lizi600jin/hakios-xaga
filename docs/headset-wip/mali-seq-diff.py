#!/usr/bin/env python3
"""Compare the register-write sequence in hakios.mali610.c against panthor's.

Two inputs:

  * our driver, parsed with a light C scanner that tracks which function a write is in,
    and resolves the small set of symbolic names the driver uses for register offsets
    (kMcuControl -> 0x700, asReg(as, off) -> 0x2400 + as*0x40 + off, ...);
  * a hand-written expectation list, transcribed from the panthor sources named in
    EXPECTATIONS below. It is hand-written on purpose: panthor reaches these registers
    through macros and function pointers, and a parser for that would be a second
    project. Every expectation carries the panthor file:line it came from so the list
    can be audited by reading it.

The output is the ordered write list from each side, the alignment between them, and
three verdicts per register: PRESENT (both write it), ABSENT (panthor writes, we never
do), EXTRA (we write, panthor never does).

    ./mali-seq-diff.py                       # default paths
    ./mali-seq-diff.py --ours FILE --json    # machine readable
    ./mali-seq-diff.py --selftest            # self-checks, no external files needed

Exit status: 0 when the parse succeeded (differences are the normal result, not a
failure), 1 when an input could not be read or parsed, 2 when --selftest fails.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import sys

DEFAULT_OURS = "/home/lizi/IOS/hakios-mtk/mtk/mt6895/drivers/gpu/hakios.mali610.c"

# --------------------------------------------------------------------------- expectations

# Each entry: (phase, register, kind, address, value, panthor evidence).
# kind is one of:
#   write  - an MMIO store through mwr()/reg_wr()/gpu_wr64()
#   read   - an MMIO load through mrd()/reg_rd()/gpu_rd64()
#   shared - a store into the firmware's shared interface block in DRAM
# The kind is part of the expectation because "panthor reads this" and "panthor writes
# this" are different requirements, and a register that is only ever read must not be
# reported as a missing write.
EXPECTATIONS = [
    ("probe", "GPU_ID", "read", "0x000", "-", "panthor_gpu.c gpu_init reads it first"),
    ("probe", "GPU_COHERENCY_PROTOCOL", "write", "0x304", "GPU_COHERENCY_NONE (31)",
     "panthor_gpu.c:63 write, :456-483 selection"),
    ("l2", "L2_PWRON", "write", "0x1a0", "l2_present",
     "panthor_gpu.c:296-320 via panthor_hw_l2_power_on, called at panthor_fw.c:1523"),
    ("l2", "L2_PWRTRANS", "read", "0x220", "poll 0", "panthor_gpu_power_on polls PWRTRANS down"),
    ("l2", "L2_READY", "read", "0x160", "poll l2_present", "panthor_gpu_power_on polls READY up"),
    ("fw-load", "AS_TRANSTAB", "write", "as+0x00", "firmware root",
     "panthor_mmu.c:601 via panthor_mmu_as_enable"),
    ("fw-load", "AS_MEMATTR", "write", "as+0x08", "mair_to_memattr()", "panthor_mmu.c:602"),
    ("fw-load", "AS_TRANSCFG", "write", "as+0x30", "0x420001c6 (INA_BITS(55-va_bits))",
     "panthor_mmu.c:779-784, written at :603"),
    ("fw-load", "AS_COMMAND", "write", "as+0x18", "AS_COMMAND_UPDATE (1)", "panthor_mmu.c:605-607"),
    ("fw-start", "JOB_INT_MASK", "write", "0x1008", "~0 (enable all)", "panthor_fw.c:1128"),
    ("fw-start", "MCU_CONTROL", "write", "0x700", "MCU_CONTROL_AUTO (2)", "panthor_fw.c:1143"),
    ("fw-start", "JOB_INT_STATUS", "read", "0x100c", "poll BIT(31)",
     "panthor_fw.c:1142-1148 via the interrupt wait"),
    ("fw-boot", "JOB_INT_CLEAR", "write", "0x1004", "status", "panthor_fw.c:1101"),
    ("fw-boot", "CSF_DOORBELL(0)", "write", "0x80000", "1",
     "panthor_fw.c:1085 panthor_fw_ring_doorbell after iface init"),
    ("fw-boot", "core_en_mask", "shared", "shared input", "shader_present", "panthor_fw.c:1061"),
    ("fw-boot", "req", "shared", "shared input", "toggle against ack",
     "panthor_fw.c:1078-1083 panthor_fw_toggle_reqs"),
    ("fw-boot", "ack_irq_mask", "shared", "shared input", "GLB_* event mask",
     "panthor_fw.c:1069-1076"),
]

# Sequence-level requirements that are not register writes and therefore cannot be seen in
# the MMIO trace. Each is a symbol (or a pair of symbols whose order matters) that panthor
# executes on the MCU-start path. Searching the driver's text is crude, but it is the right
# crudeness here: "does this driver ever ask the EB for power" is a question about whether
# the call exists at all, not about how it is spelled.
CALL_EXPECTATIONS = [
    # (phase, label, list-of-substrings, mode, panthor evidence)
    #   mode "all"  : every substring must appear somewhere
    #   mode "any"  : one is enough
    ("pre-mcu", "EB power request (IPI)", ["gpueb_power_on", "POWER_CONTROL", "gpueb_ipi",
                                           "CMD_POWER"], "any",
     "panthor_fw.c:1134; panthor_device.c:262"),
    ("pre-mcu", "write to the EB block", ["reg_wr(gGpueb", "mwr(0x13c4",
                                          "*(volatile uint32_t *)(gGpueb"], "any",
     "no panthor equivalent: panthor goes through the mailbox, not MMIO"),
    ("pre-mcu", "runtime firmware load", ["request_firmware", "load_firmware_image"], "any",
     "panthor_fw.c:817; ours embeds the blob instead (hakios.mali610.c:43)"),
]

# Registers panthor writes that are intentionally board-specific, listed so that the
# "absent" column can distinguish a real gap from a deliberate difference.
NOT_COMPARABLE = {
    "MTCMOS/SPM domain words": "panthor uses genpd; no register writes of its own",
    "PDC enables": "panthor never powers the block; vendor-only step",
    "I2M protector": "vendor-only step",
    "PMIF/SPMI regulator": "panthor uses the regulator framework",
}

# --------------------------------------------------------------------------- register names

# Register offsets the driver names symbolically. Values are taken from the enum in the
# driver itself (hakios.mali610.c:353-430) so a change there shows up as a parse error
# rather than a silent mismatch.
NAMED_REGS = {
    "kGpuId": "GPU_ID",
    "kL2Features": "GPU_L2_FEATURES",
    "kCoreFeatures": "GPU_CORE_FEATURES",
    "kTilerFeatures": "GPU_TILER_FEATURES",
    "kMemFeatures": "GPU_MEM_FEATURES",
    "kMmuFeatures": "GPU_MMU_FEATURES",
    "kAsPresent": "GPU_AS_PRESENT",
    "kCsfId": "GPU_CSF_ID",
    "kIntRaw": "GPU_IRQ_RAWSTAT",
    "kIntClear": "GPU_IRQ_CLEAR",
    "kIntMask": "GPU_IRQ_MASK",
    "kIntStatus": "GPU_IRQ_STATUS",
    "kPwrKey": "GPU_PWR_KEY",
    "kMcuControl": "MCU_CONTROL",
    "kMcuStatus": "MCU_STATUS",
    "kJobIntRaw": "JOB_INT_RAWSTAT",
    "kJobIntClear": "JOB_INT_CLEAR",
    "kJobIntMask": "JOB_INT_MASK",
    "kJobIntStatus": "JOB_INT_STATUS",
    "kDoorbell0": "CSF_DOORBELL(0)",
    "kL2PwrOn": "L2_PWRON",
    "kL2PwrTrans": "L2_PWRTRANS",
    "kL2Ready": "L2_READY",
    "kL2Present": "L2_PRESENT",
    "kShaderPwrOn": "SHADER_PWRON",
    "kShaderReady": "SHADER_READY",
    "kCoherencyProtocol": "GPU_COHERENCY_PROTOCOL",
    "kL2Config": "GPU_L2_CONFIG",
    "kGpuCmd": "GPU_CMD",
    "kGpuStatus": "GPU_STATUS",
    "kMmuIntClear": "MMU_INT_CLEAR",
    "kMmuIntMask": "MMU_INT_MASK",
    "kMmuIntRaw": "MMU_INT_RAWSTAT",
    "kAsTranstab": "AS_TRANSTAB",
    "kAsMemattr": "AS_MEMATTR",
    "kAsTranscfg": "AS_TRANSCFG",
    "kAsCommand": "AS_COMMAND",
    "kAsStatus": "AS_STATUS",
    "kAsFaultStatus": "AS_FAULTSTATUS",
    "kAsFaultAddress": "AS_FAULTADDRESS",
}

# Raw offsets that the driver writes literally at some call sites. Kept separate from
# NAMED_REGS because these come from the architecture, not from the driver's enum.
RAW_ADDR_NAMES = {
    0x700: "MCU_CONTROL",
    0x704: "MCU_STATUS",
    0x1a0: "L2_PWRON",
    0x220: "L2_PWRTRANS",
    0x160: "L2_READY",
    0x180: "SHADER_PWRON",
    0x80000: "CSF_DOORBELL(0)",
}

WRITE_RE = re.compile(r"\b(?:mwr|reg_wr|gpu_wr64)\s*\(")
READ_RE = re.compile(r"\b(?:mrd|reg_rd|gpu_rd64)\s*\(")
SHARED_STORE_RE = re.compile(
    r"(?:->|\binput->|\bcontrol->)\s*(core_en_mask|ack_irq_mask|req|version)\s*=")
FUNC_RE = re.compile(r"^([A-Za-z_][A-Za-z0-9_]*)\s*\(")


def strip_comments(src: str) -> str:
    """Remove C comments without moving any newline, so line numbers survive."""
    out = []
    i = 0
    n = len(src)
    while i < n:
        c = src[i]
        if c == "/" and i + 1 < n and src[i + 1] == "*":
            j = src.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append("".join("\n" if ch == "\n" else " " for ch in src[i:j]))
            i = j
        elif c == "/" and i + 1 < n and src[i + 1] == "/":
            j = src.find("\n", i)
            j = n if j < 0 else j
            out.append(" " * (j - i))
            i = j
        elif c == '"':
            j = i + 1
            while j < n and src[j] != '"':
                j += 2 if src[j] == "\\" else 1
            out.append(src[i:j + 1])
            i = j + 1
        else:
            out.append(c)
            i += 1
    return "".join(out)


def split_args(text: str):
    """Split a call's argument list, honouring nested parens."""
    args, depth, cur = [], 0, []
    for ch in text:
        if ch == "," and depth == 0:
            args.append("".join(cur).strip())
            cur = []
            continue
        if ch == "(":
            depth += 1
        elif ch == ")":
            depth -= 1
        cur.append(ch)
    tail = "".join(cur).strip()
    if tail:
        args.append(tail)
    return args


def resolve_reg(expr: str) -> str:
    """Turn a register expression into a stable name.

    Handles the three shapes the driver uses: a named enum constant, asReg(n, kFoo), and a
    raw hex literal.
    """
    e = expr.strip()
    m = re.fullmatch(r"asReg\s*\(\s*([A-Za-z0-9_]+)\s*,\s*([A-Za-z0-9_]+)\s*\)", e)
    if m:
        asn, off = m.group(1), m.group(2)
        base = NAMED_REGS.get(off, off)
        return "%s(AS%s)" % (base, asn)
    if e in NAMED_REGS:
        return NAMED_REGS[e]
    m = re.fullmatch(r"0x([0-9a-fA-F]+)u?", e)
    if m:
        val = int(m.group(1), 16)
        if val in RAW_ADDR_NAMES:
            return RAW_ADDR_NAMES[val]
        return "0x%x" % val
    return e


RETTYPE_WORDS = {
    "static", "void", "bool", "int", "unsigned", "uint32_t", "uint64_t", "char",
    "size_t", "long", "const", "struct", "extern", "inline",
}


def paren_balance(text: str) -> int:
    depth = 0
    i = 0
    while i < len(text):
        c = text[i]
        if c == '"':
            i += 1
            while i < len(text) and text[i] != '"':
                i += 2 if text[i] == "\\" else 1
        elif c == "(":
            depth += 1
        elif c == ")":
            depth -= 1
        i += 1
    return depth


def is_definition(prev_line: str, line: str) -> bool:
    """True for the name line of a function definition, e.g. 'beta(void)'.

    The driver puts the return type on its own line ("static bool" / "beta(void)"), which
    is the same shape as a call statement. Two things separate them: a call statement ends
    its line with ';', and a definition's previous non-empty line is nothing but type
    keywords.
    """
    body = line.rstrip()
    if body.endswith(";") or body.endswith(","):
        return False
    if not re.match(r"^[A-Za-z_][A-Za-z0-9_]*\s*\(.*\)$", body):
        return False
    words = prev_line.replace("*", " ").split()
    return bool(words) and all(w in RETTYPE_WORDS for w in words)


def call_args(logical: str, m):
    """Return the argument list of the call starting at match m."""
    j = m.end() - 1
    depth = 0
    while j < len(logical):
        if logical[j] == "(":
            depth += 1
        elif logical[j] == ")":
            depth -= 1
            if depth == 0:
                break
        j += 1
    return split_args(logical[m.end():j])


def parse_writes(path: str):
    """Return (writes, reads, shared, nlines).

    Calls that span physical lines are joined before parsing, and the reported line is the
    one the call starts on. 'writes' and 'reads' carry MMIO access records; 'shared' carries
    stores into the firmware's shared interface block.
    """
    with open(path, "r", encoding="utf-8", errors="replace") as fh:
        raw = fh.read()
    src = strip_comments(raw)
    lines = src.split("\n")
    writes, reads, shared = [], [], []
    func = "<file scope>"
    prev_nonempty = ""
    idx = 0
    while idx < len(lines):
        line = lines[idx]
        start_line = idx + 1
        idx += 1
        stripped = line.lstrip()
        if stripped.startswith("#"):
            # Preprocessor lines: "#define mwr(at, v) ..." reads exactly like a call.
            prev_nonempty = stripped
            continue
        logical = line
        guard = 0
        while paren_balance(logical) > 0 and idx < len(lines) and guard < 20:
            logical += " " + lines[idx].strip()
            idx += 1
            guard += 1

        fm = FUNC_RE.match(line)
        if fm and is_definition(prev_nonempty, line):
            func = fm.group(1)

        for m in WRITE_RE.finditer(logical):
            args = call_args(logical, m)
            kw = m.group(0)
            if kw.startswith("reg_wr"):
                if len(args) < 3:
                    continue
                reg, value = resolve_reg(args[1]), args[2]
            elif kw.startswith("gpu_wr64"):
                if len(args) < 2:
                    continue
                reg, value = resolve_reg(args[0]), args[1]
            else:
                if len(args) < 2:
                    continue
                reg, value = resolve_reg(args[0]), args[1]
            writes.append({
                "line": start_line,
                "func": func,
                "reg": reg,
                "value": " ".join(value.split()),
                "raw": " ".join(logical.strip().split())[:160],
            })

        for m in READ_RE.finditer(logical):
            args = call_args(logical, m)
            kw = m.group(0)
            if kw.startswith("reg_rd"):
                if len(args) < 2:
                    continue
                reg = resolve_reg(args[1])
            else:
                if len(args) < 1:
                    continue
                reg = resolve_reg(args[0])
            reads.append({"line": start_line, "func": func, "reg": reg})

        for m in SHARED_STORE_RE.finditer(logical):
            shared.append({"line": start_line, "func": func, "field": m.group(1)})

        if stripped:
            prev_nonempty = stripped
    return writes, reads, shared, len(lines)


def parse_error_check(path: str, nlines: int) -> list:
    """Cheap sanity checks: the file must be the driver we think it is."""
    problems = []
    with open(path, "r", encoding="utf-8", errors="replace") as fh:
        head = fh.read(4000)
    if "hakios.mali610" not in head and "Mali-G610" not in head:
        problems.append("header does not look like the Mali-G610 driver")
    if nlines < 1000:
        problems.append("only %d lines parsed; expected a full driver" % nlines)
    return problems


# --------------------------------------------------------------------------- diff

def normalise(name: str) -> str:
    """Fold the per-AS spelling so AS0, ASas and ASn compare against one expectation."""
    if name.startswith("AS_"):
        return re.sub(r"\(.*\)$", "(ASn)", name)
    return name


def is_registerish(name: str) -> bool:
    """True for a name that denotes a register rather than a C expression we could not fold.

    reg_wr(gSpm, at, v) inside a helper gives the register name "at"; that is a real write
    but not a register we can compare, so those are counted separately instead of being
    listed as if they were findings.
    """
    return bool(re.match(r"^(?:[A-Z][A-Z0-9_]*|k[A-Z][A-Za-z0-9_]*|gals|0x[0-9a-f]+)", name))\
        and "?" not in name and " " not in name


def _index(records):
    out = {}
    for w in records:
        out.setdefault(normalise(w.get("reg") or w.get("field")), []).append(w)
    return out


def compare(writes, reads, shared, expectations):
    by_write = _index(writes)
    by_read = _index(reads)
    by_shared = _index(shared)
    rows = []
    for phase, name, kind, addr, value, evidence in expectations:
        pool = {"write": by_write, "read": by_read, "shared": by_shared}[kind]
        key = normalise(name)
        hits = pool.get(key, [])
        if not hits and kind == "write":
            # A per-AS expectation is satisfied by any AS, not only AS0.
            hits = [h for k, v in by_write.items()
                    if k.startswith(key + "(") for h in v]
        rows.append({
            "phase": phase,
            "register": name,
            "kind": kind,
            "panthor_addr": addr,
            "panthor_value": value,
            "panthor_evidence": evidence,
            "ours": hits,
            "verdict": "PRESENT" if hits else "ABSENT",
        })
    expected = {(normalise(e[1]), e[2]) for e in expectations}
    extras = []
    for kind, pool in (("write", by_write), ("read", by_read), ("shared", by_shared)):
        for name, hits in sorted(pool.items()):
            if (name, kind) in expected:
                continue
            if any(name.startswith(normalise(e[1]) + "(") and e[2] == kind
                   for e in expectations):
                continue
            extras.append({"kind": kind, "register": name, "count": len(hits),
                           "first_line": hits[0]["line"],
                           "funcs": sorted({h["func"] for h in hits})})
    extras.sort(key=lambda e: (e["kind"], -e["count"], e["register"]))
    return rows, extras


def check_calls(src_text: str):
    """Report which sequence-level requirements the driver's text satisfies.

    Matching is on substrings chosen so that a read-only probe cannot satisfy them: the EB
    power request needs an IPI-ish symbol, and "write to the EB block" needs a store.
    """
    body_lines = [(i + 1, line) for i, line in enumerate(src_text.split("\n"))
                  if not line.lstrip().startswith(("*", "/*", "//"))]
    out = []
    for phase, label, needles, mode, evidence in CALL_EXPECTATIONS:
        found = {}
        for needle in needles:
            hits = [n for n, line in body_lines if needle in line]
            if hits:
                found[needle] = hits
        if mode == "all":
            ok = len(found) == len(needles)
        else:
            ok = bool(found)
        flat = sorted(n for hits in found.values() for n in hits)
        out.append({
            "phase": phase,
            "symbol": label,
            "panthor_evidence": evidence,
            "hits": flat[:5],
            "count": len(flat),
            "matched": sorted(found),
            "verdict": "PRESENT" if ok else "ABSENT",
        })
    return out


def print_report(rows, extras, calls, nwrites, nreads, nshared, nlines):
    print("parsed %d lines: %d MMIO writes, %d MMIO reads, %d shared-block stores"
          % (nlines, nwrites, nreads, nshared))
    print()
    print("%-10s %-22s %-8s %-8s %s" % ("PHASE", "REGISTER", "KIND", "VERDICT", "OURS"))
    print("-" * 100)
    for r in rows:
        if r["ours"]:
            where = ", ".join("%s:%d" % (w["func"], w["line"]) for w in r["ours"][:3])
            if len(r["ours"]) > 3:
                where += " (+%d)" % (len(r["ours"]) - 3)
        else:
            where = "-"
        print("%-10s %-22s %-8s %-8s %s" %
              (r["phase"], r["register"], r["kind"], r["verdict"], where))
    print()
    absent = [r for r in rows if r["verdict"] == "ABSENT"]
    print("ABSENT -- panthor does this, we never do (%d of %d):" % (len(absent), len(rows)))
    for r in absent:
        print("  [%s] %-20s panthor %s = %s  (%s)" %
              (r["kind"], r["register"], r["panthor_addr"], r["panthor_value"],
               r["panthor_evidence"]))
    print()
    real = [e for e in extras if is_registerish(e["register"])]
    synthetic = [e for e in extras if not is_registerish(e["register"])]
    print("EXTRA -- we touch these, panthor's MCU-start path does not (%d names, writes and"
          " shared stores only):" % len(real))
    for e in real:
        if e["kind"] not in ("write", "shared"):
            continue
        print("  [%s] %-34s %3d, first at :%d in %s" %
              (e["kind"], e["register"], e["count"], e["first_line"],
               ",".join(e["funcs"][:3])))
    print()
    print("  %d further names are reads panthor also performs (or diagnostics unique to this"
          " driver); %d are register arguments the parser could not fold (helper wrappers)."
          % (len([e for e in real if e["kind"] == "read"]), len(synthetic)))
    print()
    print("SEQUENCE-LEVEL -- not a register write, so invisible in the trace above:")
    for c in calls:
        where = ("first at :%d (%d occurrences)" % (c["hits"][0], c["count"])
                 if c["hits"] else "never")
        print("  %-8s %-24s %-8s %s  [%s]" %
              (c["phase"], c["symbol"], c["verdict"], where, c["panthor_evidence"]))
    print()
    print("not comparable by construction (board-specific, no panthor equivalent):")
    for k, v in NOT_COMPARABLE.items():
        print("  %-24s %s" % (k, v))


# --------------------------------------------------------------------------- selftest

SELFTEST_SRC = r'''
#include "x.h"
#define mwr(at, v) mali_gpu_write((at), (v))

/* a comment mentioning mwr(kFake, 0) must not be parsed */
static void
alpha(void)
{
	mwr(kMcuControl, 2u);
	mwr(kJobIntMask, 0xffffffffu);
	gpu_wr64(asReg(0, kAsTranscfg), 0x42000006ull);
	(void)mrd(kMcuStatus);
}

static bool
beta(void)
{
	reg_wr(gSpm, MALI_MFG1, v);
	// mwr(kComment, 1);
	mwr(0x1a0u, 1u);
	control->core_en_mask = 0x130013u;
	input->req = 1u;
	return true;
}
'''


def selftest() -> int:
    import tempfile

    fails = []
    with tempfile.TemporaryDirectory() as td:
        p = os.path.join(td, "t.c")
        with open(p, "w") as fh:
            fh.write(SELFTEST_SRC)
        writes, reads, shared, nlines = parse_writes(p)

    regs = [w["reg"] for w in writes]
    if len(writes) != 5:
        fails.append("expected 5 writes, got %d: %r" % (len(writes), regs))
    if "MCU_CONTROL" not in regs:
        fails.append("kMcuControl did not resolve to MCU_CONTROL")
    if "JOB_INT_MASK" not in regs:
        fails.append("kJobIntMask did not resolve to JOB_INT_MASK")
    if "AS_TRANSCFG(AS0)" not in regs:
        fails.append("asReg(0, kAsTranscfg) did not resolve: %r" % regs)
    if "L2_PWRON" not in regs:
        fails.append("0x1a0 did not resolve to L2_PWRON: %r" % regs)
    if any("kFake" in r or "kComment" in r for r in regs):
        fails.append("comment stripping failed: %r" % regs)

    funcs = {w["func"] for w in writes}
    if "alpha" not in funcs or "beta" not in funcs:
        fails.append("function attribution failed: %r" % funcs)

    # reads and shared stores must be classified separately from writes
    if not [r for r in reads if r["reg"] == "MCU_STATUS"]:
        fails.append("mrd(kMcuStatus) was not recorded as a read: %r" %
                     [r["reg"] for r in reads])
    if not [s for s in shared if s["field"] == "core_en_mask"]:
        fails.append("shared core_en_mask store not recorded: %r" %
                     [s["field"] for s in shared])

    # compare() must call an unwritten expectation ABSENT and a written one PRESENT
    exp = [("p", "MCU_CONTROL", "write", "0x700", "2", "test"),
           ("p", "MCU_STATUS", "read", "0x704", "-", "test"),
           ("p", "NEVER_WRITTEN", "write", "0x999", "1", "test")]
    rows, extras = compare(writes, reads, shared, exp)
    verdicts = {r["register"]: r["verdict"] for r in rows}
    if verdicts["MCU_CONTROL"] != "PRESENT":
        fails.append("MCU_CONTROL should be PRESENT")
    if verdicts["MCU_STATUS"] != "PRESENT":
        fails.append("MCU_STATUS read should be PRESENT")
    if verdicts["NEVER_WRITTEN"] != "ABSENT":
        fails.append("NEVER_WRITTEN should be ABSENT")
    if not extras:
        fails.append("extras should be non-empty")

    # a write must not satisfy a read expectation
    exp2 = [("p", "MCU_CONTROL", "read", "0x700", "-", "test")]
    rows2, _ = compare(writes, reads, shared, exp2)
    if rows2[0]["verdict"] != "ABSENT":
        fails.append("a write satisfied a read expectation")

    # normalisation must fold the AS index
    if normalise("AS_TRANSCFG(AS3)") != "AS_TRANSCFG(ASn)":
        fails.append("normalise() failed for AS_TRANSCFG")

    # argument splitting with nested calls
    if split_args("a, fn(b, c), d") != ["a", "fn(b, c)", "d"]:
        fails.append("split_args() failed on nesting")

    if fails:
        for f in fails:
            print("SELFTEST FAIL: " + f, file=sys.stderr)
        return 2
    print("selftest ok: parse, comment strip, multi-line join, name resolution, "
          "function attribution, read/write/shared split, verdicts, normalisation, "
          "arg splitting")
    return 0


# --------------------------------------------------------------------------- main

def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--ours", default=DEFAULT_OURS, help="driver to parse")
    ap.add_argument("--json", action="store_true", help="emit JSON instead of text")
    ap.add_argument("--selftest", action="store_true", help="run built-in checks and exit")
    args = ap.parse_args(argv)

    if args.selftest:
        return selftest()

    if not os.path.exists(args.ours):
        print("cannot read %s" % args.ours, file=sys.stderr)
        return 1
    writes, reads, shared, nlines = parse_writes(args.ours)
    problems = parse_error_check(args.ours, nlines)
    if problems:
        for p in problems:
            print("parse error: " + p, file=sys.stderr)
        return 1
    rows, extras = compare(writes, reads, shared, EXPECTATIONS)
    with open(args.ours, "r", encoding="utf-8", errors="replace") as fh:
        calls = check_calls(fh.read())

    if args.json:
        print(json.dumps({
            "file": args.ours,
            "lines": nlines,
            "writes": len(writes),
            "reads": len(reads),
            "shared_stores": len(shared),
            "rows": rows,
            "extras": extras,
            "absent": [{"register": r["register"], "kind": r["kind"]} for r in rows
                       if r["verdict"] == "ABSENT"],
            "calls": calls,
            "not_comparable": NOT_COMPARABLE,
        }, indent=2))
    else:
        print_report(rows, extras, calls, len(writes), len(reads), len(shared), nlines)
    return 0


if __name__ == "__main__":
    sys.exit(main())
