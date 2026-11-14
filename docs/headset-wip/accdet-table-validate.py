#!/usr/bin/env python3
"""Machine-check the 50-row MT6368 ACCDET init table against vendor source + device dump.

Read-only inputs:
  findings/accdet-init-table.json       the table under test (50 rows)
  accdet-baseline.json                  stock-Android SPMI dump, SID 5, 130 registers
  research/mt6368-accdet-regmap.txt     ACCDET field table (addr / bit / mask / name)
  raw/mt6368.h                          vendor register header (addr + SFT + MASK defs)
  raw/mt6368-accdet.c                   vendor accdet driver (line-number checks)
  findings/accdet-init-table.c          generated C array (cross-check vs the JSON)
  mt6368.c                              codec driver (rows 1-2 only; optional)

Per-row checks
  1. width        value must fit inside mask
  2. maskshift    (mask<<shift) must be a defined field; a single-field symbol whose
                  regmap entry has a different shift/mask/address is a hard conflict
  3. dump         (dump[reg] >> shift) & mask == value
  4. line         every `file:line` must land on an op for this register+field, or on a
                  call chain that reaches it; same-register-wrong-field is flagged
  5. symbol       the symbolic name must resolve to a field at that address
  6. pulse/refill a dump conflict is *expected* for one half of a set-then-clear pulse,
                  or for the clearing half of a clear-then-refill pair

Output: human summary on stdout; JSON with --json-out.

Usage:
  accdet-table-validate.py [--json-out FILE] [--seed N] [--sample N] [--verbose]
  accdet-table-validate.py --selftest
"""
import argparse
import json
import os
import random
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
FIND = os.path.join(HERE, "findings")

TABLE_JSON = os.path.join(FIND, "accdet-init-table.json")
TABLE_C = os.path.join(FIND, "accdet-init-table.c")
BASELINE = os.path.join(HERE, "accdet-baseline.json")
REGMAP = os.path.join(HERE, "research", "mt6368-accdet-regmap.txt")
HDR = os.path.join(HERE, "raw", "mt6368.h")
ACCDET_C = os.path.join(HERE, "raw", "mt6368-accdet.c")

KERNEL = os.environ.get("XAGA_KERNEL", "/home/lizi/IOS/work-xaga/k-xaga")
MT6368C_CANDIDATES = [
    os.environ.get("MT6368_C", ""),
    os.path.join(HERE, "raw", "mt6368.c"),
    "/tmp/accdet-val/mt6368.c",
]

BLOCK_ACCDET = (0x2688, 0x26D3)
BLOCK_AUDDEC = (0x2508, 0x2537)

OPS = ("accdet_write", "accdet_update_bits", "accdet_clear_bits",
       "accdet_update_bit", "accdet_clear_bit", "accdet_read", "accdet_read_bits",
       "regmap_write", "regmap_update_bits", "regmap_read", "regmap_bulk_read")
OP_RE = re.compile(r"\b(" + "|".join(OPS) + r")\(")
MACRO_RE = re.compile(r"\b([A-Z][A-Z0-9_]*(?:_ADDR|_SFT))\b")
MT_MACRO_RE = re.compile(r"\b(MT6368_[A-Z0-9_]+)\b")
CALL_RE = re.compile(r"\b([a-zA-Z_]\w+)\s*\(")
REASON_REF = re.compile(r"(mt6368(?:-accdet)?\.c|mt6368\.h):(\d+)(?:\s*-\s*(\d+))?")
FUNC_START = re.compile(r"^(?:static\s+)?(?:inline\s+)?(?:const\s+)?[A-Za-z_][\w \*]*?\b(\w+)\s*\([^;]*\)\s*$")

NOT_A_CALL = {"if", "for", "while", "switch", "return", "sizeof", "regmap_read",
              "regmap_write", "regmap_update_bits", "BIT", "ARRAY_SIZE", "HAS_CAP",
              "REGISTER_VAL", "usleep_range", "udelay", "msleep", "container_of",
              "min", "max", "clamp", "sprintf", "strncat", "pr_info", "pr_notice",
              "of_property_read_u32", "of_property_read_u32_array", "dev_info",
              "mutex_lock", "mutex_unlock", "jiffies_to_msecs"}


# ---------------------------------------------------------------- parsing

def parse_header(path):
    """-> ({MT6368_X: addr}, {base: {addr,sft,mask,line,target}})"""
    with open(path, encoding="utf-8", errors="replace") as f:
        lines = f.read().splitlines()
    addr = {}
    for ln in lines:
        m = re.match(r"#define\s+(MT6368_\w+)\s+(0x[0-9a-fA-F]+)\s*$", ln)
        if m:
            addr[m.group(1)] = int(m.group(2), 16)

    fields = {}
    for i, ln in enumerate(lines):
        m = re.match(r"#define\s+([A-Z][A-Z0-9_]*)_ADDR\s*\\\s*$", ln)
        if not m:
            continue
        base = m.group(1)
        j = i + 1
        while j < len(lines) and not lines[j].strip():
            j += 1
        target = lines[j].strip() if j < len(lines) else ""
        rec = fields.setdefault(base, {"sft": None, "mask": None})
        rec["line"] = i + 1
        rec["target"] = target
        rec["addr"] = addr.get(target)
    for base, rec in fields.items():          # SFT/MASK may live in another block
        for ln in lines:
            if rec["sft"] is None:
                m = re.match(r"#define\s+" + re.escape(base) + r"_SFT\s+(-?\w+)\s*$", ln)
                if m:
                    try:
                        rec["sft"] = int(m.group(1), 0)
                    except ValueError:
                        pass
            if rec["mask"] is None:
                m = re.match(r"#define\s+" + re.escape(base) + r"_MASK\s+(0x[0-9a-fA-F]+)\s*$", ln)
                if m:
                    rec["mask"] = int(m.group(1), 16)
    return addr, fields


def parse_regmap(path):
    out = {}
    if not os.path.exists(path):
        return out
    with open(path, encoding="utf-8", errors="replace") as f:
        for ln in f:
            if not ln.startswith("0x"):
                continue
            p = ln.split()
            if len(p) < 5:
                continue
            out.setdefault(int(p[0], 16), []).append(
                {"bit": int(p[1], 0), "mask": int(p[2], 0), "name": p[3], "reg": p[4]})
    return out


def parse_baseline(path):
    with open(path, encoding="utf-8") as f:
        b = json.load(f)
    vals, where = {}, {}
    for region, body in b["regions"].items():
        for reg, v in body["values"].items():
            vals[int(reg, 16)] = v
            where[int(reg, 16)] = region
    return b, vals, where


def parse_table(path):
    with open(path, encoding="utf-8") as f:
        rows = json.load(f)
    for r in rows:
        r.setdefault("op", "write")
        r.setdefault("delay_us", 0)
        r["reg_i"] = int(r["reg"], 16)
        r["mask_i"] = int(r["mask"], 16)
        r["shift_i"] = int(r.get("shift") or 0)
        r["value_i"] = None if r.get("value") is None else int(r["value"], 16)
        r["byte_mask"] = (r["mask_i"] << r["shift_i"]) & 0xFF
        r["byte_val"] = None if r["value_i"] is None else (r["value_i"] << r["shift_i"]) & 0xFF
    return rows


def parse_table_c(path):
    """{seq: (reg, mask<<shift, val<<shift, delay)} from the generated C file."""
    if not os.path.exists(path):
        return {}
    txt = open(path, encoding="utf-8").read()
    out = {}
    for m in re.finditer(r"\{\s*(0x[0-9a-fA-F]+),\s*(0x[0-9a-fA-F]+),\s*(0x[0-9a-fA-F]+),"
                         r"\s*(\d+)\s*\},?\s*/\*\s*(\d+)\s*\*/", txt):
        out[int(m.group(5))] = (int(m.group(1), 16), int(m.group(2), 16),
                                int(m.group(3), 16), int(m.group(4)))
    return out


def load_mt6368c():
    for p in MT6368C_CANDIDATES:
        if p and os.path.exists(p):
            return p, open(p, encoding="utf-8", errors="replace").read()
    try:
        r = subprocess.run(["git", "-C", KERNEL, "cat-file", "-p",
                            "HEAD:sound/soc/codecs/mt6368.c"],
                           capture_output=True, text=True, timeout=90)
        if r.returncode == 0:
            dst = "/tmp/accdet-val/mt6368.c"
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            with open(dst, "w", encoding="utf-8") as f:
                f.write(r.stdout)
            return dst, r.stdout
    except Exception:
        pass
    return None, None


def macro_base(macro):
    for suf in ("_ADDR", "_SFT"):
        if macro.endswith(suf):
            return macro[:-len(suf)]
    return macro


def func_ranges(src):
    """[(name, start_line, end_line)] by brace counting from a top-level signature."""
    lines = src.splitlines()
    out = []
    i = 0
    while i < len(lines):
        m = FUNC_START.match(lines[i])
        if m and lines[i][:1] not in (" ", "\t") and not lines[i].rstrip().endswith(";"):
            depth, j, started = 0, i, False
            while j < len(lines) and j < i + 4000:
                depth += lines[j].count("{") - lines[j].count("}")
                if lines[j].count("{"):
                    started = True
                if started and depth <= 0:
                    break
                j += 1
            if started:
                out.append((m.group(1), i + 1, min(j + 1, len(lines))))
                i = j + 1
                continue
        i += 1
    return out


def ops_index(src):
    """{line: [(helper, macro)]} for ops whose target macro appears on/after the call."""
    lines = src.splitlines()
    idx = {}
    for i, ln in enumerate(lines):
        m = OP_RE.search(ln)
        if not m:
            continue
        mm = MACRO_RE.search(ln[m.end():]) or MT_MACRO_RE.search(ln[m.end():])
        if not mm:
            for k in range(i + 1, min(i + 3, len(lines))):
                mm = MACRO_RE.search(lines[k]) or MT_MACRO_RE.search(lines[k])
                if mm:
                    break
        if mm:
            idx.setdefault(i + 1, []).append((m.group(1), mm.group(1)))
    return idx, lines


class Source:
    """Vendor .c file with a function-level call graph and per-function register sets."""

    def __init__(self, path, src, fields, addr=None):
        self.path = path
        self.lines = src.splitlines()
        self.funcs = func_ranges(src)
        self.op_idx, _ = ops_index(src)
        self.reg_of_macro = dict(addr or {})     # MT6368_* -> address
        for base, rec in fields.items():
            if rec.get("addr") is not None:
                self.reg_of_macro[base + "_ADDR"] = rec["addr"]
                self.reg_of_macro[base] = rec["addr"]
        self.func_direct = {}
        self.func_calls = {}
        for name, a, b in self.funcs:
            regs, calls = set(), set()
            for ln in range(a, min(b, len(self.lines)) + 1):
                for _h, macro in self.op_idx.get(ln, []):
                    base = macro_base(macro)
                    if base in self.reg_of_macro:
                        regs.add(self.reg_of_macro[base])
                for cm in CALL_RE.finditer(self.lines[ln - 1]):
                    f = cm.group(1)
                    if f not in NOT_A_CALL:
                        calls.add(f)
            self.func_direct[name] = regs
            self.func_calls[name] = calls
        # fixed point: a function reaches everything its callees reach
        self.func_regs = {n: set(r) for n, r in self.func_direct.items()}
        for _ in range(6):
            changed = False
            for name in self.func_regs:
                for callee in self.func_calls.get(name, ()):
                    if callee in self.func_regs:
                        new = self.func_regs[callee] - self.func_regs[name]
                        if new:
                            self.func_regs[name] |= new
                            changed = True
            if not changed:
                break
        self.enclosing = {}
        for name, a, b in self.funcs:
            for ln in range(a, min(b, len(self.lines)) + 1):
                self.enclosing.setdefault(ln, name)
        self.debounce_map = self._debounce_map()

    def _debounce_map(self):
        """case label -> register set, for accdet_set_debounce's switch."""
        out = {}
        if "accdet_set_debounce" not in self.func_regs:
            return out
        a = b = None
        for name, s, e in self.funcs:
            if name == "accdet_set_debounce":
                a, b = s, e
                break
        if not a:
            return out
        cur = None
        for ln in range(a, min(b, len(self.lines)) + 1):
            txt = self.lines[ln - 1].strip()
            cm = re.match(r"case\s+(\w+)\s*:", txt)
            if cm:
                cur = cm.group(1)
                out.setdefault(cur, set())
            for _h, macro in self.op_idx.get(ln, []):
                base = macro_base(macro)
                if cur and base in self.reg_of_macro:
                    out[cur].add(self.reg_of_macro[base])
        return out

    def func_at(self, ln):
        return self.enclosing.get(ln)

    def reach(self, name):
        return self.func_regs.get(name, set())


# ---------------------------------------------------------------- helpers

def bits(mask, shift=0):
    v = (mask << shift) & 0xFF
    return {b for b in range(8) if v >> b & 1}


def field_bits_at(reg, regmap, fields):
    cov, names = set(), []
    for f in regmap.get(reg, []):
        cov |= bits(f["mask"], f["bit"])
        names.append(f["name"])
    if not cov:
        for base, rec in fields.items():
            if rec.get("addr") == reg and rec.get("mask") is not None and rec.get("sft") is not None:
                cov |= bits(rec["mask"], rec["sft"])
                names.append(base)
    return cov, names


def field_is_here(row, base, toks):
    """Does the op's macro name this row's field (or the register the field lives in)?"""
    reg_names = {base}
    # MT6368_X is the register; RG_X / ACCDET_X is the field. Accept both spellings:
    # a row whose field name shares the register's stem counts as the same thing.
    def norm(x):
        for pref in ("MT6368_", "RG_", "ACCDET_"):
            if x.startswith(pref):
                x = x[len(pref):]
        return x

    stem = norm(base)
    for t in toks:
        t2 = norm(t)
        if name_match(t, base) or name_match(t2, stem):
            return True
        if "*" in t2:
            continue
        # vendor spellings share a stem: LDO_VAUD18_EN vs LDO_VAUD18_CON0
        cp = 0
        for a, b in zip(t2, stem):
            if a != b:
                break
            cp += 1
        if cp >= 8 and cp >= min(len(t2), len(stem)) - 4:
            tail_a = t2[cp:cp + 1]
            tail_b = stem[cp:cp + 1]
            if tail_a.isdigit() and tail_b.isdigit() and tail_a != tail_b:
                continue                      # DEBOUNCE0 != DEBOUNCE1
            return True
    return False


def symbol_tokens(row):
    return [t.strip() for t in re.split(r"[+/]", row.get("symbol") or "") if t.strip()]


def name_match(token, name):
    """Loose match: token may be truncated (ACCDET_FOO -> ACCDET_FOO_PWM_WIDTH) or globbed
    (EINT0_*_STABLE -> EINT0_EN_STABLE)."""
    if not token:
        return False
    if "*" in token:
        pat = token.replace("*", ".*")
        for cand in (pat, "ACCDET_" + pat, ".*" + pat):
            if re.match("^" + cand + "$", name):
                return True
        return False
    if token == name:
        return True
    if name.startswith(token) or token.startswith(name):
        return True
    return token in name or name in token


# ---------------------------------------------------------------- checks

def check_width(row):
    if row["value_i"] is None:
        return None, ""
    if row["value_i"] & ~row["mask_i"]:
        return False, "value 0x%x 超出 mask 0x%x" % (row["value_i"], row["mask_i"])
    return True, ""


def check_mask_shift(row, regmap, fields):
    """-> (status, note, names_at_addr). status: ok|warn|must_fix|unknown"""
    cov, names = field_bits_at(row["reg_i"], regmap, fields)
    need = bits(row["mask_i"], row["shift_i"])
    if not cov:
        return "unknown", "该地址在 regmap/头文件中无字段定义", []

    toks = symbol_tokens(row)
    # hard conflict: an *exactly named* field that lives at a different place than the row
    exact = {}
    for a2, fl in regmap.items():
        for f in fl:
            exact.setdefault(f["name"], []).append((a2, f))
    if len(toks) == 1 and toks[0] in exact:
        for a2, f in exact[toks[0]]:
            if a2 != row["reg_i"] or f["bit"] != row["shift_i"]:
                return ("must_fix",
                        "regmap 里 %s 在 0x%04x bit%d mask 0x%x，表里是 0x%04x bit%d mask 0x%x"
                        % (f["name"], a2, f["bit"], f["mask"],
                           row["reg_i"], row["shift_i"], row["mask_i"]), names)
            if f["mask"] == row["mask_i"]:
                return "ok", "", names
            if row["mask_i"] & f["mask"] != f["mask"]:
                return ("must_fix",
                        "表里 mask 0x%x 没能盖住 regmap 的 %s mask 0x%x（偏窄或错位）"
                        % (row["mask_i"], f["name"], f["mask"]), names)
            # row writes a wider window than the field: legal only as a raw byte write
            if row["op"] == "write":
                return ("warn", "源码是整字节写（mask 0x%02x），比 regmap 的 %s（bit%d mask 0x%x）宽，"
                                "会顺带写保留位" % (row["byte_mask"], f["name"], f["bit"], f["mask"]), names)
            return ("warn", "只读校验用了整字节 mask 0x%02x，比 %s（bit%d mask 0x%x）宽："
                            "会把保留位一起比" % (row["byte_mask"], f["name"], f["bit"], f["mask"]), names)
    extra = need - cov
    if extra:
        return ("warn", "mask 0x%02x 覆盖该地址未定义的位 %s（整字节写会把保留位写成 0）"
                % (row["byte_mask"], sorted(extra)), names)
    return "ok", "", names


def check_symbol(row, regmap, fields):
    toks = symbol_tokens(row)
    if not toks:
        return "unknown", "", []
    _, names = field_bits_at(row["reg_i"], regmap, fields)
    if not names:
        return "unknown", "本地址无字段表可比（非 ACCDET 块或 regmap 未收录）", []
    hit = [t for t in toks if any(name_match(t, n) for n in names)]
    if len(hit) == len(toks):
        return "ok", "", hit
    return "warn", "符号 %s 在本地址字段中只命中 %s" % (row.get("symbol"), hit), hit


def check_dump(row, dump):
    if row["reg_i"] not in dump:
        return "no_dump", None, None
    dv = dump[row["reg_i"]]
    got = (dv >> row["shift_i"]) & row["mask_i"]
    if row["op"] == "read":                 # a sample: the dump is the observation, not a target
        return "snapshot", dv, got
    want = 0 if row["value_i"] is None else row["value_i"]
    return ("match" if got == want else "conflict"), dv, got


def zero_write_scan(reg, sources):
    """Positive proof of absence: does any write op in these files target `reg`?"""
    hits = []
    for name, src in sources:
        if src is None:
            continue
        for ln, lst in src.op_idx.items():
            for helper, macro in lst:
                base = macro_base(macro)
                if src.reg_of_macro.get(base) != reg:
                    continue
                if helper.startswith(("accdet_write", "accdet_update", "accdet_clear",
                                      "regmap_write", "regmap_update")):
                    hits.append({"file": name, "line": ln, "helper": helper, "macro": base})
    return hits


def cleared_by_row(row, src, dump):
    """Vendor has a writer that clears exactly this field, and the dump shows it cleared.

    Typical case: 0x268a bit0 SW_EN. Init sets it, the plug-out path clears it
    (accdet.c:1894 / 1925), and an unplugged dump therefore reads 0. The table value is
    right for "after init", the dump value is right for "idle, nothing plugged".
    Returns the clearing line or None.
    """
    if row["op"] != "write" or row["value_i"] in (None, 0):
        return None
    if row["reg_i"] not in dump:
        return None
    if (dump[row["reg_i"]] >> row["shift_i"]) & row["mask_i"] != 0:
        return None
    toks = symbol_tokens(row)
    for ln in range(1, len(src.lines) + 1):
        for helper, macro in src.op_idx.get(ln, []):
            base = macro_base(macro)
            if src.reg_of_macro.get(base) != row["reg_i"]:
                continue
            if not any(name_match(t, base) for t in toks):
                continue
            if helper in ("accdet_clear_bit", "accdet_clear_bits"):
                return {"line": ln, "helper": helper, "macro": macro}
            if helper == "accdet_update_bits":
                m = re.search(r"update_bits\s*\([^;]*?,\s*0\s*\)", src.lines[ln - 1])
                if m:
                    return {"line": ln, "helper": helper, "macro": macro}
    return None


def pulse_pairs(rows):
    """-> (pulse {seq: partner}, refill {clearing_seq: refilling_seq})."""
    groups = {}
    for r in rows:
        if r["op"] != "write" or r["value_i"] is None:
            continue
        groups.setdefault((r["reg_i"], r["mask_i"], r["shift_i"]), []).append(r)
    pulse, refill = {}, {}
    for _k, g in groups.items():
        g = sorted(g, key=lambda r: r["seq"])
        for i, a in enumerate(g):
            for b in g[i + 1:]:
                if a["value_i"] == b["value_i"] or 0 not in (a["value_i"], b["value_i"]):
                    continue
                gap = b["seq"] - a["seq"]
                if a["value_i"] != 0 and b["value_i"] == 0 and gap <= 2:
                    pulse[a["seq"]] = b["seq"]       # set then clear: a transient
                    pulse[b["seq"]] = a["seq"]
                elif a["value_i"] == 0 and b["value_i"] != 0:
                    refill[a["seq"]] = b["seq"]      # cleared early, refilled later
    return pulse, refill


def resolve_ref(row, ref, ctx):
    """Classify one file:line reference into a status + detail."""
    fname, lo, hi = ref
    win_lo, win_hi = max(1, lo - 8), hi + 8
    rec = {"ref": "%s:%d%s" % (fname, lo, ("-%d" % hi) if hi != lo else ""),
           "file": fname, "cited": [lo, hi], "status": "miss", "detail": "",
           "excerpt": "", "delta": None, "op_line": None}

    if fname == "mt6368.h":
        lines = ctx["hdr_lines"]
        toks = symbol_tokens(row)
        val = row["value_i"]
        found, const = [], None
        for ln in range(win_lo, min(win_hi, len(lines)) + 1):
            txt = lines[ln - 1]
            idents = re.findall(r"[A-Z][A-Z0-9_]{3,}", txt)
            for t in toks:
                if t in txt or any(name_match(t, i) or name_match(i, t) for i in idents):
                    found.append((ln, t))
            if val is not None:
                for h in re.findall(r"0x[0-9a-fA-F]+", txt):
                    if int(h, 16) == val:
                        const = (ln, h)
        rec["excerpt"] = lines[min(max(1, lo), len(lines)) - 1].strip()[:110]
        if found:
            rec["status"] = "ok"
            rec["detail"] = "定义命中 %s" % (found[:2],)
        elif const:
            rec["status"] = "ok_const"
            rec["detail"] = "常量 0x%x 就定义在这行（值为 %s）" % (val, const[1])
        else:
            rec["detail"] = "引用窗口内既无该符号定义，也无等于写值的常量"
        return rec

    src = ctx["mtc"] if fname == "mt6368.c" else ctx["acc"]
    if src is None:
        rec["status"] = "skip"
        rec["detail"] = "源文件不可得"
        return rec
    lines = src.lines
    rec["excerpt"] = lines[min(max(1, lo), len(lines)) - 1].strip()[:110]

    toks = symbol_tokens(row)

    def scan(a_lo, a_hi):
        found = []
        for ln in range(max(1, a_lo), min(a_hi, len(lines)) + 1):
            for helper, macro in src.op_idx.get(ln, []):
                base = macro_base(macro)
                if src.reg_of_macro.get(base) != row["reg_i"]:
                    continue
                found.append((ln, helper, base, field_is_here(row, base, toks)))
        return found

    # pass 1: strictly inside the cited range; pass 2: a small drift window.
    inside = scan(lo, hi)
    hits = inside or scan(win_lo, win_hi)
    strict = bool(inside)

    best = None
    for ln, helper, base, same_field in hits:
        dist = 0 if strict else (ln - hi if ln > hi else lo - ln)
        key = (0 if same_field else 1, dist, ln)
        if best is None or key < best[0]:
            best = (key, ln, helper, base, same_field)

    if best:
        key, ln, helper, base, same_field = best
        if same_field:
            rec.update(status="ok", op_line=ln, delta=key[1],
                       detail="%s(%s) @%d" % (helper, base, ln))
            return rec
        # same register, other field: for a verify row that is the proof of absence
        if row["op"] == "verify" and (row.get("value") in (None, "0x00")):
            rec.update(status="ok_absence", op_line=ln, delta=key[1],
                       detail="%s(%s) @%d 写的是同寄存器的其它位 → 佐证本字段无人写"
                              % (helper, base, ln))
            return rec
        rec.update(status="field_conflict", op_line=ln, delta=key[1],
                   detail="%s(%s) @%d ← 同寄存器的另一个字段，不支持本行" % (helper, base, ln))
        return rec

    # indirect: cited line sits inside a function that reaches this register
    fn = src.func_at(lo)
    if fn and row["reg_i"] in src.reach(fn):
        chain = "%s() 间接到达" % fn
        rec.update(status="ok_indirect", detail=chain)
        # refine debounce dispatcher callsites by their enum argument
        for ln in range(lo, min(hi, len(lines)) + 1):
            m = re.search(r"accdet_set_debounce\(\s*(\w+)", lines[ln - 1])
            if m and m.group(1) in src.debounce_map:
                rec["detail"] = "%s(accdet_set_debounce(%s))" % (chain, m.group(1))
                rec["delta"] = 0
                break
        return rec

    # cross-file chain: this file's function calls something that reaches the register
    fn2 = src.func_at(lo)
    if fn2:
        other = ctx["acc"] if src is ctx["mtc"] else ctx["mtc"]
        if other is not None:
            for callee in src.func_calls.get(fn2, ()):
                if callee in other.func_regs and row["reg_i"] in other.reach(callee):
                    rec.update(status="ok_chain", detail="%s() → %s() 到达该寄存器（跨文件调用链）"
                                                         % (fn2, callee))
                    return rec

    # context: dts parse / default value / dump scope, cited for provenance not an op
    txt = lines[min(max(1, lo), len(lines)) - 1]
    if (re.search(r"of_property|accdet_dts\.\w+\s*=|dump_register|cat_register|pr_info\(\"accdet",
                  txt) or (fn and re.search(r"parse|dump|probe|dt_|dts", fn))):
        rec.update(status="context", detail="上下文行（默认值/解析/打印），非寄存器操作")
        return rec

    rec["detail"] = "窗口内无该寄存器的操作，也不在能到达它的函数里"
    return rec


# ---------------------------------------------------------------- prereq audit

def _call_args(text, start):
    """Split the argument list of a call whose '(' is at index `start`."""
    depth, args, cur, i = 0, [], [], start
    while i < len(text):
        c = text[i]
        if c == "(":
            depth += 1
            if depth == 1:
                i += 1
                continue
        elif c == ")":
            depth -= 1
            if depth == 0:
                args.append("".join(cur))
                return [a.strip() for a in args]
        elif c == "," and depth == 1:
            args.append("".join(cur))
            cur = []
            i += 1
            continue
        if depth >= 1:
            cur.append(c)
        i += 1
    return [a.strip() for a in args]


def prereq_audit(mt6368c, fields):
    """Who writes 0x230c bit0 (RG_ACCDET_CK_PDN)?

    Bits are taken from (a) the mask argument of regmap_update_bits (3rd arg),
    (b) any RG_*_SFT symbol on the site (DAPM supplies name one bit that way).
    The 4th argument is the *value*, never the mask, and is ignored.
    """
    res = {"reg": "0x230c", "symbol": "RG_ACCDET_CK_PDN", "sites": [],
           "bit0_written": None, "bits_touched": [], "verdict": "unverified",
           "note": "", "reads": 0, "writes": 0}
    if not mt6368c:
        res["note"] = "mt6368.c 不可得"
        return res
    lines = mt6368c.splitlines()
    touched, bit0_reasons = set(), []
    for i, ln in enumerate(lines):
        if "MT6368_AUD_TOP_CKPDN_CON0" not in ln:
            continue
        stmt = "\n".join(lines[i:min(i + 5, len(lines))])
        site = {"line": i + 1, "mask": None, "sft": None, "kind": None,
                "text": ln.strip()[:110]}
        m = re.search(r"(regmap_update_bits|regmap_write|regmap_read)\s*\(", stmt)
        if m:
            args = _call_args(stmt, m.end() - 1)
            site["kind"] = m.group(1)
            if m.group(1) == "regmap_update_bits" and len(args) >= 3:
                site["mask"] = args[2]
                try:
                    v = int(args[2], 0)
                    touched |= {b for b in range(8) if v >> b & 1}
                    if v & 1:
                        bit0_reasons.append((i + 1, "mask=" + args[2]))
                except ValueError:
                    pass
            elif m.group(1) == "regmap_write" and len(args) >= 2:
                site["mask"] = args[1]
                try:
                    v = int(args[1], 0)
                    touched |= {b for b in range(8) if v >> b & 1}
                    if v & 1:
                        bit0_reasons.append((i + 1, "write=" + args[1]))
                except ValueError:
                    pass
        for sym in re.findall(r"(RG_\w+?)_SFT", stmt):
            f = fields.get(sym)
            if f and f.get("sft") is not None:
                site["sft"] = f["sft"]
                touched.add(f["sft"])
                if f["sft"] == 0:
                    bit0_reasons.append((i + 1, sym + "_SFT=0"))
        if site["kind"] == "regmap_read":
            res["reads"] += 1
        else:
            res["writes"] += 1
        res["sites"].append(site)
    res["bit0_written"] = bool(bit0_reasons)
    res["bits_touched"] = sorted(touched)
    res["bit0_reasons"] = bit0_reasons
    res["verdict"] = ("bit0 无写入者" if not bit0_reasons else "bit0 有写入者")
    return res


# ---------------------------------------------------------------- driver

def run(table_path=TABLE_JSON, dump_path=BASELINE, regmap_path=REGMAP,
        hdr_path=HDR, c_path=ACCDET_C, sample_n=15, seed=20261115):
    addr, fields = parse_header(hdr_path)
    hdr_lines = open(hdr_path, encoding="utf-8", errors="replace").read().splitlines()
    regmap = parse_regmap(regmap_path)
    base, dump, where = parse_baseline(dump_path)
    rows = parse_table(table_path)
    ctab = parse_table_c(TABLE_C)
    acc_src = open(c_path, encoding="utf-8", errors="replace").read()
    acc = Source(c_path, acc_src, fields, addr)
    mcpath, mc_src = load_mt6368c()
    mtc = Source(mcpath, mc_src, fields, addr) if mc_src else None
    ctx = {"hdr_lines": hdr_lines, "acc": acc, "mtc": mtc}

    pulse, refill = pulse_pairs(rows)

    items = []
    for r in rows:
        wr_ok, wr_msg = check_width(r)
        ms_status, ms_msg, ms_names = check_mask_shift(r, regmap, fields)
        sym_status, sym_msg, sym_hits = check_symbol(r, regmap, fields)
        d_status, d_val, d_got = check_dump(r, dump)

        if d_status == "conflict":
            if r["seq"] in pulse:
                d_class = "conflict_pulse"
            elif r["seq"] in refill:
                d_class = "conflict_refill"
            elif cleared_by_row(r, acc, dump):
                d_class = "conflict_cleared_at_runtime"
            else:
                d_class = "conflict_unexplained"
        else:
            d_class = d_status

        zw = zero_write_scan(r["reg_i"], [("mt6368-accdet.c", acc), ("mt6368.c", mtc)])

        refs = []
        for m in REASON_REF.finditer(r.get("reason") or ""):
            lo = int(m.group(2))
            hi = int(m.group(3)) if m.group(3) else lo
            refs.append(resolve_ref(r, (m.group(1), lo, hi), ctx))

        hard_ref = [x for x in refs if x["status"] in ("miss", "field_conflict")]
        src_proven = (bool(refs) and not hard_ref
                      and wr_ok is not False and ms_status != "must_fix")
        if r["op"] == "verify" and not zw:
            src_proven = True
            refs.append({"ref": "全文件扫描", "file": "-", "cited": [0, 0],
                         "status": "zero_write", "detail":
                         "accdet.c 与 mt6368.c 中没有任何写该寄存器的操作（只读校验成立）",
                         "excerpt": "", "delta": None, "op_line": None})

        if d_class == "match":
            dump_ok = True
        elif d_class in ("conflict_pulse", "conflict_refill", "conflict_cleared_at_runtime"):
            dump_ok = None
        elif d_class in ("no_dump", "snapshot"):
            dump_ok = None
        else:
            dump_ok = False

        if src_proven and dump_ok is True:
            verdict = "A 双证"
        elif src_proven and d_class == "no_dump":
            verdict = "C 源码可证（dump 无覆盖）"
        elif src_proven and d_class == "snapshot":
            verdict = "C 源码可证（采样行，dump 只做参考）"
        elif src_proven and dump_ok is None:
            verdict = "B 源码可证（瞬态/状态相关，dump 不适用）"
        elif src_proven:
            verdict = "D 源码可证但 dump 冲突"
        elif dump_ok is True:
            verdict = "E 仅 dump 佐证"
        else:
            verdict = "F 仅推导/未验证"

        c_row = ctab.get(r["seq"])
        c_ok = None
        if c_row:
            c_ok = (c_row[0], c_row[1], c_row[2], c_row[3]) == \
                   (r["reg_i"], r["byte_mask"], r["byte_val"], r["delay_us"])

        block = ("accdet" if BLOCK_ACCDET[0] <= r["reg_i"] <= BLOCK_ACCDET[1]
                 else "auddec_ana" if BLOCK_AUDDEC[0] <= r["reg_i"] <= BLOCK_AUDDEC[1]
                 else "outside")

        items.append({
            "seq": r["seq"], "reg": r["reg"], "op": r["op"], "block": block,
            "mask": r["mask"], "shift": r["shift_i"], "value": r.get("value"),
            "symbol": r.get("symbol"), "delay_us": r["delay_us"],
            "byte_mask": "0x%02x" % r["byte_mask"],
            "byte_val": None if r["byte_val"] is None else "0x%02x" % r["byte_val"],
            "width": "ok" if wr_ok else ("n/a" if wr_ok is None else "fail"),
            "width_note": wr_msg,
            "mask_shift": ms_status, "mask_shift_note": ms_msg,
            "fields_here": ms_names,
            "symbol_check": sym_status, "symbol_note": sym_msg,
            "dump": d_class, "dump_value": None if d_val is None else "0x%02x" % d_val,
            "dump_field": None if d_got is None else "0x%x" % d_got,
            "dump_region": where.get(r["reg_i"]),
            "cleared_at": cleared_by_row(r, acc, dump),
            "other_write_sites": zw,
            "reason_refs": refs, "src_proven": src_proven, "verdict": verdict,
            "c_array_consistent": c_ok,
        })

    hard, soft = [], []
    for it in items:
        if it["width"] == "fail":
            hard.append([it["seq"], "value 超出字段位宽：" + it["width_note"]])
        for x in it["reason_refs"]:
            if x["status"] == "miss":
                hard.append([it["seq"], "行号 %s 不在操作该寄存器：%s" % (x["ref"], x["detail"])])
            elif x["status"] == "field_conflict":
                hard.append([it["seq"], "行号 %s 落在同寄存器另一字段：%s" % (x["ref"], x["detail"])])
        if it["mask_shift"] == "must_fix":
            hard.append([it["seq"], "mask/shift 与 regmap 冲突：" + it["mask_shift_note"]])
        if it["c_array_consistent"] is False:
            hard.append([it["seq"], "生成的 C 数组与 JSON 不一致"])
        if it["dump"] == "conflict_unexplained":
            soft.append([it["seq"], "dump 与写值不一致且无瞬态/回填解释"])
        if it["mask_shift"] == "warn":
            soft.append([it["seq"], it["mask_shift_note"]])
        if it["symbol_check"] == "warn":
            soft.append([it["seq"], it["symbol_note"]])
        for x in it["reason_refs"]:
            if x["status"] == "ok" and x["delta"] not in (None, 0) and abs(x["delta"]) > 1:
                soft.append([it["seq"], "%s 行号偏移 %+d（引用 %d，实际 %d）"
                             % (x["ref"], x["delta"], x["cited"][0], x["op_line"])])
            if x["status"] in ("ok_indirect", "ok_chain", "ok_absence", "context", "zero_write", "ok_const"):
                soft.append([it["seq"], "%s %s：%s" % (x["ref"], x["status"], x["detail"])])

    order = []
    sw = [i for i in items if i["reg"] == "0x268a" and i["shift"] == 0
          and i["op"] == "write" and i["value"] == "0x01"]
    if sw:
        last_w = max(i["seq"] for i in items if i["op"] == "write")
        order.append({"rule": "SW_EN(0x268a bit0) 必须是最后一次写", "seq": sw[0]["seq"],
                      "ok": sw[0]["seq"] == last_w, "advisory": False})
    vaud = [i for i in items if i["reg"] == "0x1cb2"]
    if vaud:
        first_ana = min((i["seq"] for i in items
                         if i["block"] in ("accdet", "auddec_ana") and i["op"] == "write"),
                        default=10 ** 6)
        order.append({"rule": "VAUD18(0x1cb2) 必须早于所有 ACCDET/AUDDEC 写",
                      "seq": vaud[0]["seq"], "ok": vaud[0]["seq"] < first_ana, "advisory": False})
    rst = [i for i in items if i["reg"] == "0x2319" and i["value"] == "0x01"]
    deb = [i for i in items if i["reg"] in ("0x2697", "0x2698", "0x2699", "0x269a")]
    if rst and deb:
        order.append({"rule": "ACCDET_RST 脉冲(#22) 早于 debounce 写(#10-13)，避免复位冲掉配置",
                      "seq": rst[0]["seq"], "ok": rst[0]["seq"] < min(i["seq"] for i in deb),
                      "advisory": True})
    mbst = [i for i in items if i["reg"] == "0x252a" and i["value"] == "0x03"]
    if mbst and rst:
        order.append({"rule": "MICBIAS 2.8V 配置(#33) 早于 SW_EN(#48)，否则无偏置就开检测",
                      "seq": mbst[0]["seq"], "ok": mbst[0]["seq"] < (sw[0]["seq"] if sw else 10 ** 6),
                      "advisory": False})

    regs = sorted({i["reg"] for i in items}, key=lambda s: int(s, 16))
    cov = {"distinct_regs": len(regs),
           "in_dump": [r for r in regs if int(r, 16) in dump],
           "not_in_dump": [r for r in regs if int(r, 16) not in dump],
           "dump_total_regs": len(dump),
           "misc_strays": sorted("0x%04x" % a for a, v in where.items() if v == "misc")}
    cov["not_in_dump_detail"] = [
        {"reg": r, "block": ("accdet" if BLOCK_ACCDET[0] <= int(r, 16) <= BLOCK_ACCDET[1]
                             else "auddec_ana" if BLOCK_AUDDEC[0] <= int(r, 16) <= BLOCK_AUDDEC[1]
                             else "outside")}
        for r in cov["not_in_dump"]]

    summary = {"rows": len(items),
               "ops": {k: sum(1 for i in items if i["op"] == k) for k in ("write", "verify", "read")},
               "verdicts": {}, "dump": {},
               "hard_errors": len(hard), "soft_findings": len(soft),
               "total_delay_us": sum(i["delay_us"] for i in items),
               "ref_status": {}}
    for it in items:
        summary["verdicts"][it["verdict"]] = summary["verdicts"].get(it["verdict"], 0) + 1
        summary["dump"][it["dump"]] = summary["dump"].get(it["dump"], 0) + 1
        for x in it["reason_refs"]:
            summary["ref_status"][x["status"]] = summary["ref_status"].get(x["status"], 0) + 1

    rng = random.Random(seed)
    sample = sorted(rng.sample([i["seq"] for i in items], sample_n))

    return {"meta": {"table": table_path, "baseline": dump_path, "regmap": regmap_path,
                     "header": hdr_path, "accdet_c": c_path, "mt6368_c": mcpath,
                     "baseline_source_field": base.get("source"),
                     "baseline_sid": base.get("sid"),
                     "seed": seed, "sample_n": sample_n},
            "summary": summary, "coverage": cov,
            "pulse_pairs": {str(k): v for k, v in sorted(pulse.items())},
            "refill_pairs": {str(k): v for k, v in sorted(refill.items())},
            "order_rules": order,
            "prereq_audit_0x230c": prereq_audit(mc_src, fields),
            "hard_errors": hard, "soft_findings": soft,
            "sample": sample, "items": items}


# ---------------------------------------------------------------- report

def human(rep, verbose=False):
    s, c = rep["summary"], rep["coverage"]
    out = []
    A = out.append
    A("=" * 78)
    A("MT6368 ACCDET 初始化表 — 机器校验")
    A("=" * 78)
    A("表   : %s" % os.path.basename(rep["meta"]["table"]))
    A("dump : %s  source=%r  sid=%s  寄存器 %d"
      % (os.path.basename(rep["meta"]["baseline"]),
         rep["meta"]["baseline_source_field"], rep["meta"]["baseline_sid"],
         c["dump_total_regs"]))
    A("源   : %s / %s" % (os.path.basename(rep["meta"]["accdet_c"]),
                          rep["meta"]["mt6368_c"] or "（不可得）"))
    A("行数 %d：写 %d / 只读校验 %d / 采样 %d；串行延时合计 %.1f ms"
      % (s["rows"], s["ops"]["write"], s["ops"]["verify"], s["ops"]["read"],
         s["total_delay_us"] / 1000.0))
    A("")
    A("-- 判定分布 " + "-" * 64)
    for k in sorted(s["verdicts"]):
        A("   %-36s %3d" % (k, s["verdicts"][k]))
    A("-- dump 比对 " + "-" * 64)
    for k in sorted(s["dump"]):
        A("   %-36s %3d" % (k, s["dump"][k]))
    A("-- 行号解析 " + "-" * 64)
    for k in sorted(s["ref_status"]):
        A("   %-36s %3d" % (k, s["ref_status"][k]))
    A("")
    A("-- 覆盖 " + "-" * 68)
    A("   distinct 寄存器 %d；dump 覆盖 %d；未覆盖 %d"
      % (c["distinct_regs"], len(c["in_dump"]), len(c["not_in_dump"])))
    for d in c["not_in_dump_detail"]:
        A("      未覆盖 %s (%s)" % (d["reg"], d["block"]))
    A("   dump 杂项单点：%s" % c["misc_strays"])
    A("")
    A("-- 脉冲 / 回填 " + "-" * 62)
    A("   脉冲 set→clear：%s" % rep["pulse_pairs"])
    A("   回填 clear→set：%s" % rep["refill_pairs"])
    A("")
    A("-- 顺序规则 " + "-" * 66)
    for o in rep["order_rules"]:
        A("   %s seq=%s %s %s" % ("PASS" if o["ok"] else "FAIL", o["seq"], o["rule"],
                                  "(建议)" if o["advisory"] else ""))
    A("")
    A("-- 0x230c 前置审计 " + "-" * 58)
    a = rep["prereq_audit_0x230c"]
    A("   %s；bit0 被写=%s；被触碰位=%s；写点 %d / 读点 %d"
      % (a["verdict"], a["bit0_written"], a["bits_touched"], a.get("writes", -1),
         a.get("reads", -1)))
    for st in a["sites"]:
        A("     :%-5d %-20s mask=%-6s sft=%s  %s"
          % (st["line"], st["kind"], st["mask"], st["sft"], st["text"]))
    A("")
    A("-- 一定错 %d 条 " % len(rep["hard_errors"]) + "-" * 60)
    for seq, why in rep["hard_errors"]:
        A("   #%-2d %s" % (seq, why))
    A("")
    A("-- 次级发现 %d 条 " % len(rep["soft_findings"]) + "-" * 56)
    for seq, why in rep["soft_findings"]:
        A("   #%-2d %s" % (seq, why))
    A("")
    A("-- 随机抽样 seed=%s n=%d " % (rep["meta"]["seed"], rep["meta"]["sample_n"]) + "-" * 46)
    by = {i["seq"]: i for i in rep["items"]}
    for seq in rep["sample"]:
        it = by[seq]
        A("   #%-2d %-7s %-6s m=%s<<%d v=%-5s dump=%s(%s)  %s"
          % (seq, it["reg"], it["op"], it["mask"], it["shift"], it["value"],
             it["dump"], it["dump_field"], it["verdict"]))
        for r in it["reason_refs"]:
            A("        %-22s %-13s %s" % (r["ref"], r["status"], r["detail"]))
            if verbose:
                A("        %-22s %s" % ("", ("| " + r["excerpt"]) if r["excerpt"] else ""))
    A("")
    if verbose:
        A("-- 全部 50 行 " + "-" * 64)
        for it in rep["items"]:
            A("   #%-2d %-7s %-6s m=%-4s<<%d v=%-5s dump=%-19s %s"
              % (it["seq"], it["reg"], it["op"], it["mask"], it["shift"],
                 it["value"], it["dump"], it["verdict"]))
    return "\n".join(out)


# ---------------------------------------------------------------- selftest

def selftest():
    checks = []

    def ok(name, cond, extra=""):
        checks.append((name, bool(cond), str(extra)))

    addr, fields = parse_header(HDR)
    ok("header: RG_ACCDET_CK_PDN = 0x230c bit0 mask 1",
       fields.get("RG_ACCDET_CK_PDN", {}).get("addr") == 0x230C
       and fields["RG_ACCDET_CK_PDN"]["sft"] == 0
       and fields["RG_ACCDET_CK_PDN"]["mask"] == 1)
    ok("header: RG_ACCDET_RST = 0x2319 bit1",
       fields.get("RG_ACCDET_RST", {}).get("addr") == 0x2319
       and fields["RG_ACCDET_RST"]["sft"] == 1)
    ok("header: MT6368_AUD_TOP_CKPDN_CON0 = 0x230c",
       addr.get("MT6368_AUD_TOP_CKPDN_CON0") == 0x230C)

    rm = parse_regmap(REGMAP)
    mem = [f for f in rm.get(0x26B6, []) if f["name"] == "ACCDET_MEM_IN"]
    ok("regmap: 0x26b6 ACCDET_MEM_IN @bit6 mask 3",
       mem and mem[0]["bit"] == 6 and mem[0]["mask"] == 3, mem)

    b, dump, where = parse_baseline(BASELINE)
    ok("baseline: 130 寄存器且 0x26b6=0xff",
       len(dump) == 130 and dump.get(0x26B6) == 0xFF, len(dump))

    rows = parse_table(TABLE_JSON)
    counts = {k: sum(1 for r in rows if r["op"] == k) for k in ("write", "verify", "read")}
    ok("table: 50 行 = 44 写 / 4 校验 / 2 采样",
       len(rows) == 50 and counts == {"write": 44, "verify": 4, "read": 2}, counts)

    bad = dict(rows[0])
    bad["value_i"], bad["mask_i"] = 0x4, 0x1
    ok("width: 注入 0x4/0x1 越界被抓到", check_width(bad)[0] is False)

    r43 = [r for r in rows if r["seq"] == 43][0]
    shifted = dict(r43)
    shifted["shift_i"], shifted["byte_mask"] = 0, 0x07
    ok("maskshift: #43 原位 ok，错位到 bit0 报 must_fix",
       check_mask_shift(r43, rm, fields)[0] == "ok"
       and check_mask_shift(shifted, rm, fields)[0] == "must_fix",
       (check_mask_shift(r43, rm, fields)[0], check_mask_shift(shifted, rm, fields)[0]))

    r23 = [r for r in rows if r["seq"] == 23][0]
    flipped = dict(r23)
    flipped["value_i"] = 1
    ok("dump: 一致判 match，翻转判 conflict",
       check_dump(r23, dump)[0] == "match" and check_dump(flipped, dump)[0] == "conflict")

    pulse, refill = pulse_pairs(rows)
    ok("pulse: 3 组脉冲（#6/#7, #8/#9, #22/#23）",
       len(pulse) == 6 and set(pulse) == {6, 7, 8, 9, 22, 23}, pulse)
    ok("refill: #24 (0x252a) 由 #33 回填", refill.get(24) == 33, refill)

    acc_src = open(ACCDET_C, encoding="utf-8", errors="replace").read()
    acc = Source(ACCDET_C, acc_src, fields, addr)
    mcpath, mc_src = load_mt6368c()
    mtc = Source(mcpath, mc_src, fields, addr) if mc_src else None
    ctx = {"hdr_lines": open(HDR, encoding="utf-8", errors="replace").read().splitlines(),
           "acc": acc, "mtc": mtc}
    r48 = [r for r in rows if r["seq"] == 48][0]
    ref = resolve_ref(r48, ("mt6368-accdet.c", 1814, 1817), ctx)
    ok("line: #48 命中 0x268a 的 update_bit", ref["status"] == "ok"
       and ref["op_line"] == 1817, ref)

    r20 = [r for r in rows if r["seq"] == 20][0]
    ref20 = resolve_ref(r20, ("mt6368-accdet.c", 1974, 1978), ctx)
    ok("line: #20 引用 1974-1978 落在同寄存器的 DEBOUNCE1（应报 field_conflict）",
       ref20["status"] == "field_conflict", ref20)

    r10 = [r for r in rows if r["seq"] == 10][0]
    ref10 = resolve_ref(r10, ("mt6368-accdet.c", 3117, 3117), ctx)
    ok("line: #10 的 3117 是 accdet_set_debounce 调用点 → ok_indirect",
       ref10["status"] == "ok_indirect", ref10)

    aud = prereq_audit(mc_src, fields)
    ok("audit: 0x230c bit0 无人写（mask 参数 0x66，SFT 无 bit0）",
       aud["bit0_written"] is False, aud.get("bits_touched"))

    a1 = random.Random(7).sample(range(1, 51), 15)
    a2 = random.Random(7).sample(range(1, 51), 15)
    ok("sample: 固定种子可复现", a1 == a2)

    rep = run()
    ok("现表: 0 条一定错", rep["summary"]["hard_errors"] == 0, rep["hard_errors"][:4])
    ok("现表: C 数组与 JSON 全一致",
       all(i["c_array_consistent"] in (True, None) for i in rep["items"]))
    outside = [i["seq"] for i in rep["items"] if i["block"] == "outside"]
    ok("现表: 块外行 = #1 #2 #22 #23 #38（0x230c/0x1cb2/0x2319/0x248c）",
       outside == [1, 2, 22, 23, 38], outside)

    width = max(len(n) for n, _, _ in checks)
    bad_n = 0
    print("=" * 78)
    print("SELFTEST  (%d checks)" % len(checks))
    print("=" * 78)
    for name, passed, extra in checks:
        print("  %-*s %s%s" % (width, name, "PASS" if passed else "FAIL",
                               ("  [" + extra[:120] + "]") if not passed else ""))
        bad_n += 0 if passed else 1
    print("-" * 78)
    print("  %d/%d PASS" % (len(checks) - bad_n, len(checks)))
    return 0 if bad_n == 0 else 1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--json-out")
    ap.add_argument("--seed", type=int, default=20261115)
    ap.add_argument("--sample", type=int, default=15)
    ap.add_argument("--verbose", action="store_true")
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args()
    if a.selftest:
        return selftest()
    rep = run(sample_n=a.sample, seed=a.seed)
    print(human(rep, a.verbose))
    if a.json_out:
        with open(a.json_out, "w", encoding="utf-8") as f:
            json.dump(rep, f, ensure_ascii=False, indent=1)
        print("\n[json] %s" % a.json_out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
