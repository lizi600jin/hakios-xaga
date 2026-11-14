#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
init-order-audit.py — `hakios_input_init()` 的初始化顺序审计器（只读）。

它回答的问题：这 21 条串行调用里，谁起线程、谁碰 PMIF/SPMI、谁碰 GPU 窗口、
谁会在串行阶段把整条 init 卡住、以及谁和谁在并发下抢同一块硬件。

四步：

  1. 从 `drivers/input/hakios.input.c` 的 `hakios_input_init()` **函数体**里按顺序抽调用；
  2. 只在 `.config` 里 `CONFIG_HAKIOS_<X>=y` 时才认为驱动真的编入内核，否则是
     `drivers/core/hakios.stubs.c` 的空体（stub）；
  3. 从该函数出发，按 `kernel_thread_start(worker)` 和普通函数调用展开**有限深度的调用图**
     （默认 4 跳），对沿途每个函数体（去注释）匹配原语：
       线程   kernel_thread_start / thread_call_allocate / timer_call_enter
       PMIF   0x1c804000 / PMIF_ / SWINF_ / hakios_spmi_lock
       GPU    0x13000000 —— 直接写，或经 `#define X 0x13000000` 的宏间接写
       阻塞   delay(n)（n 为微秒）与 `for(;;)` / `while(1)`
  4. 输出表格（默认）/ JSON（`--json`）/ 自检（`--selftest`）。

三条判定口径（报告与本脚本必须一致）：

  * `pmif_where == "init"`     —— 在串行阶段就访问 PMIF（会与已启动的 worker 抢通道）。
  * `pmif_where == "worker"`   —— 只在它自己起的线程里访问（进入时串行阶段已经过去）。
  * `blocks_ms`                —— init 体里的 delay 之和，**加上** worker 体里第一个无限循环
                                  之前的 delay 之和；即"从 init 返回前它同步花掉的时间"或
                                  "worker 开始轮询前先睡掉的时间"。单位毫秒。
  * `sync_forever`             —— init 体自身有无限循环（会把整条串行 init 卡死）。
                                  worker 里的循环是设计（轮询），记为 `worker_forever`。

用法：
    ./init-order-audit.py                          # 表格 + 汇总
    ./init-order-audit.py --json out.json          # 另写 JSON
    ./init-order-audit.py --selftest               # 断言
    ./init-order-audit.py --tree /path/to/tree     # 指定树（默认 /home/lizi/IOS/hakios-mtk）
    ./init-order-audit.py --depth 6                # 调用图深度

只读：不写树内任何文件。
"""

from __future__ import annotations

import argparse
import json
import os
import re
import sys
from dataclasses import dataclass, field, asdict

DEFAULT_TREE = "/home/lizi/IOS/hakios-mtk"

CALL_RE = re.compile(r"\b(hakios_[a-z0-9_]+_init|hakios_mtk_emmc_devfs)\s*\(")
THREAD_RE = re.compile(r"\b(?:kernel_thread_start|thread_call_allocate|timer_call_enter)\s*\(\s*([A-Za-z_][A-Za-z0-9_]*)?")
# 注意：不能用 \bPMIF_ —— 下划线属于单词字符，"PMIF_OK" 里 PMIF 后没有边界。
PMIF_RE = re.compile(r"(0x1c804000|PMIF_[A-Z0-9_]+|SWINF_[A-Z0-9_]+|hakios_spmi_lock|pmif_(?:init|read8|write8|recover|recover_besteffort|last_sta))")
GPU_RE = re.compile(r"0x13000000")
DELAY_RE = re.compile(r"\bdelay\s*\(\s*([0-9]+(?:\s*\*\s*[0-9]+)*)\s*\)")
FOREVER_RE = re.compile(
    r"(for\s*\(\s*;\s*;\s*\)"                                     # for (;;)
    r"|for\s*\(\s*(?:unsigned\s+|int\s+)?\w+\s*=\s*[0-9]+\s*;\s*;\s*"   # for (tick = 1;; tick++)
    r"|for\s*\(\s*(?:unsigned\s+|int\s+)?\w+\s*;\s*;\s*"
    r"|while\s*\(\s*1\s*\))"
)
SHARED_API_RE = re.compile(r"\bpmif_(?:init|read8|write8|recover|recover_besteffort|last_sta)\s*\(")
BOUNDED_RE = re.compile(r"(deadline|budget|polls_left|timeout|_tries|retries|attempt)", re.I)
# 一个函数体里的"被调用名字"
CALLEE_RE = re.compile(r"\b([a-z_][a-z0-9_]{2,})\s*\(")
# 需要忽略的 C 关键字/内建，免得当成函数名去图里找
NOT_CALLS = {
    "if", "for", "while", "switch", "return", "sizeof", "do", "else", "case", "goto",
    "printf", "delay", "ml_io_map", "ml_io_map_unmappable", "PE_parse_boot_argn",
    "nanoseconds_to_absolutetime", "clock_interval_to_deadline", "thread_deallocate",
    "mach_absolute_time", "strlcpy", "snprintf", "vsnprintf", "panic", "bzero",
    "memcpy", "memset", "lck_mtx_lock", "lck_mtx_unlock", "lck_spin_lock", "lck_spin_unlock",
}


# ------------------------------------------------------------------ 文本工具

def strip_comments(src: str) -> str:
    """去 // 与 /* */，用等长空白替换（保留行号/列位置）。"""
    out, i, n = [], 0, len(src)
    while i < n:
        if src.startswith("/*", i):
            j = src.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append("".join("\n" if ch == "\n" else " " for ch in src[i:j]))
            i = j
        elif src.startswith("//", i):
            j = src.find("\n", i)
            j = n if j < 0 else j
            out.append(" " * (j - i))
            i = j
        else:
            out.append(src[i])
            i += 1
    return "".join(out)


def find_function(src: str, name: str) -> tuple[int, int, str] | None:
    """
    找 `^name(` 的定义。返回 (起行, 止行, **函数体**) —— 函数体从第一个 `{` 之后开始，
    不含签名行，否则 `hakios_input_init(` 会把自己当成一次调用。
    """
    lines = src.split("\n")
    for idx, line in enumerate(lines):
        if not re.match(rf"^{re.escape(name)}\s*\(", line):
            continue
        depth, started, end, brace = 0, False, None, None
        for j in range(idx, len(lines)):
            for col, ch in enumerate(lines[j]):
                if ch == "{":
                    depth += 1
                    if not started:
                        started, brace = True, (j, col)
                elif ch == "}":
                    depth -= 1
                    if started and depth == 0:
                        end = j
                        break
            if end is not None:
                break
        if not started or end is None:
            return None
        bj, bc = brace
        body = lines[bj][bc + 1:] + "\n" + "\n".join(lines[bj + 1:end])
        return idx + 1, end + 1, body
    return None


def delay_us(text: str) -> int:
    """`delay(n)` 的 n 是微秒；支持 `2000 * 1000` 这种写法。"""
    total = 0
    for m in DELAY_RE.finditer(text):
        prod = 1
        for part in m.group(1).split("*"):
            part = part.strip()
            if part.isdigit():
                prod *= int(part)
        total += prod
    return total


def before_first_loop(body: str) -> str:
    m = FOREVER_RE.search(body)
    return body[: m.start()] if m else body


def macro_constants(src: str) -> dict[str, str]:
    """`#define NAME 0x13...` → {NAME: 值}，用来把 GPU 地址宏认出来。"""
    out = {}
    for m in re.finditer(r"^#define\s+([A-Z0-9_]+)\s+([0-9][0-9a-fA-FxXuUlL]*)", src, re.M):
        out[m.group(1)] = m.group(2)
    return out


def expand_macro_refs(body: str, consts: dict[str, str]) -> str:
    """把体里出现的、值含 0x13000000 的宏名替换成它的值，让 GPU 检测不再依赖字面量。"""
    gpu_macros = {k for k, v in consts.items() if "0x13000000" in v.lower()}
    for name in gpu_macros:
        body = re.sub(rf"\b{re.escape(name)}\b", "0x13000000", body)
    return body


# ------------------------------------------------------------------ 配置与定位

def config_enabled(tree: str) -> set[str]:
    enabled = set()
    with open(os.path.join(tree, ".config"), encoding="utf-8", errors="replace") as fh:
        for line in fh:
            m = re.match(r"^CONFIG_HAKIOS_([A-Z0-9_]+)=y\s*$", line.strip())
            if m:
                enabled.add(m.group(1))
    return enabled


DRIVER_OF = {
    "cpufreq": "CPUFREQ", "virtio_input": "VIRTIO_INPUT", "goodix": "GOODIX", "tcm": "TCM",
    "nvt": "NVT", "buttons": "BUTTONS", "mtk_keys": "MTK_KEYS", "mtk6895_keys": "MTK6895_KEYS",
    "mt6895_keys": "MTK6895_KEYS", "mt6895_usb": "MT6895_USB", "battery": "BATTERY",
    "ufs": "UFS", "adreno": "ADRENO", "mali": "MALI", "mali610": "MALI610",
    "mtk_connsys": "MTK_CONNSYS", "mtk_scp": "MTK_SCP", "wifi": "WIFI", "adsp": "ADSP",
    "mpss": "MODEM", "mtk_emmc": "MTK_EMMC",
}
STANDARD = {"net"}          # drivers/net/hakios.net.c 是 standard 文件，永不被 stub


def driver_of(fn: str) -> tuple[str | None, bool]:
    core = fn[len("hakios_"):] if fn.startswith("hakios_") else fn
    for suffix in ("_devfs", "_init"):
        if core.endswith(suffix):
            core = core[: -len(suffix)]
    if core in STANDARD:
        return None, True
    return DRIVER_OF.get(core), False


def locate(tree: str, fn: str) -> tuple[str | None, int]:
    pat = re.compile(rf"^{re.escape(fn)}\s*\(", re.M)
    for root in ("drivers", "mtk", "xnu/files"):
        base = os.path.join(tree, root)
        if not os.path.isdir(base):
            continue
        for dirpath, _dn, filenames in os.walk(base):
            for name in sorted(filenames):
                if not name.endswith((".c", ".cpp")):
                    continue
                path = os.path.join(dirpath, name)
                try:
                    with open(path, encoding="utf-8", errors="replace") as fh:
                        text = fh.read()
                except OSError:
                    continue
                m = pat.search(text)
                if m:
                    return os.path.relpath(path, tree), text[: m.start()].count("\n") + 1
    return None, 0


@dataclass
class InitRecord:
    order: int
    fn: str
    threads: bool = False
    worker: str = ""
    pmif: bool = False
    pmif_where: str = ""            # "init" | "worker" | "init+worker"
    pmif_depth: int = 0             # 几跳调用才碰到 PMIF（0 = 直接）
    pmif_shared: bool = False       # 是否经 mtk/mt6895/drivers/core/hakios.mt6895_pmif.c 的共享 API
    gpu: bool = False
    blocks_ms: float = 0.0
    sync_forever: bool = False
    worker_forever: bool = False
    unbounded_poll: bool = False
    built: bool = True
    config: str = ""
    src: str = ""
    line: int = 0
    maps: list[str] = field(default_factory=list)
    note: str = ""


class BodyIndex:
    """按需读文件、取函数体，带缓存。"""

    def __init__(self, tree: str):
        self.tree = tree
        self._files: dict[str, str] = {}

    def body_of(self, fn: str, prefer: str | None = None) -> tuple[str, str, int] | None:
        """返回 (相对路径, 函数体, 起行)；prefer 给一个文件优先试。"""
        cands = []
        if prefer:
            cands.append(prefer)
        path, line = locate(self.tree, fn)
        if path and path not in cands:
            cands.append(path)
        for rel in cands:
            if rel not in self._files:
                try:
                    with open(os.path.join(self.tree, rel), encoding="utf-8", errors="replace") as fh:
                        self._files[rel] = strip_comments(fh.read())
                except OSError:
                    continue
            got = find_function(self._files[rel], fn)
            if got:
                return rel, got[2], got[0]
        return None


def scan_graph(index: BodyIndex, root: str, root_file: str, depth: int) -> dict:
    """
    从 root 展开调用图（不含 root 自身），累积原语。返回一个字典：
      pmif / pmif_depth / gpu / worker / worker_forever / blocks_ms / unbounded_poll
    `worker` 只认 root 体里的 kernel_thread_start；worker 体单独再展开一层。
    """
    res = {"pmif": False, "pmif_depth": 0, "pmif_shared": False, "gpu": False, "worker": "",
           "worker_forever": False, "head_delay_us": 0, "unbounded_poll": False,
           "loop_text_bounded": False}

    def walk(fn: str, prev_file: str, level: int, seen: set[str], in_worker: bool) -> None:
        got = index.body_of(fn, prev_file)
        if not got:
            return
        rel, body, _ln = got
        consts = macro_constants(open(os.path.join(index.tree, rel), encoding="utf-8",
                                      errors="replace").read())
        b = expand_macro_refs(body, consts)
        if PMIF_RE.search(b) and not res["pmif"]:
            res["pmif"] = True
            res["pmif_depth"] = level
        if SHARED_API_RE.search(b):
            res["pmif_shared"] = True
        if GPU_RE.search(b):
            res["gpu"] = True
        if in_worker and BOUNDED_RE.search(b):
            # worker 链上任何一处出现 deadline/budget/timeout，就说明这个轮询是有界的
            res["unbounded_poll"] = False
            res["worker_bounded"] = True
        if FOREVER_RE.search(b) and not in_worker:
            res.setdefault("inner_forever", True)
        if level >= depth:
            return
        for m in CALLEE_RE.finditer(b):
            name = m.group(1)
            if name in NOT_CALLS or name in seen or name == fn:
                continue
            walk(name, rel, level + 1, seen | {name}, in_worker)

    got = index.body_of(root, root_file)
    if not got:
        return res
    rel, body, _ln = got
    consts = macro_constants(open(os.path.join(index.tree, rel), encoding="utf-8",
                                  errors="replace").read())
    b = expand_macro_refs(body, consts)

    if PMIF_RE.search(b):
        res["pmif"], res["pmif_depth"] = True, 0
    if GPU_RE.search(b):
        res["gpu"] = True

    # 体里直接的 kernel_thread_start(worker)
    m = re.search(r"kernel_thread_start\s*\(\s*([A-Za-z_][A-Za-z0-9_]*)", b)
    if m:
        res["worker"] = m.group(1)
        wgot = index.body_of(m.group(1), rel)
        if wgot:
            wrel, wbody, _wln = wgot
            wconsts = macro_constants(open(os.path.join(index.tree, wrel), encoding="utf-8",
                                          errors="replace").read())
            wb = expand_macro_refs(wbody, wconsts)
            res["head_delay_us"] = delay_us(before_first_loop(wb))
            res["worker_forever"] = bool(FOREVER_RE.search(wb))
            res["worker_bounded"] = bool(BOUNDED_RE.search(wb))
            if res["worker_forever"] and not res["worker_bounded"]:
                # 还没完：要看 worker 的被调函数是否有界，由下面的 walk 结果补上
                res["unbounded_poll"] = True
            if PMIF_RE.search(wb) and not res["pmif"]:
                res["pmif"], res["pmif_depth"] = True, 1
            if GPU_RE.search(wb):
                res["gpu"] = True
            walked = set()
            for mm in CALLEE_RE.finditer(wb):
                n2 = mm.group(1)
                if n2 in NOT_CALLS or n2 in walked:
                    continue
                walked.add(n2)
                # worker 体自身是 1 跳，它调用的函数是 2 跳，依此类推
                walk(n2, wrel, 2, {n2, m.group(1)}, True)

    # init 体里的普通调用（同步链）
    for mm in CALLEE_RE.finditer(b):
        n2 = mm.group(1)
        if n2 in NOT_CALLS or n2 == res["worker"]:
            continue
        walk(n2, rel, 1, {n2}, False)
    return res


def analyse(tree: str, depth: int = 4) -> list[InitRecord]:
    with open(os.path.join(tree, "drivers/input/hakios.input.c"), encoding="utf-8", errors="replace") as fh:
        raw = fh.read()
    found = find_function(strip_comments(raw), "hakios_input_init")
    if found is None:
        raise SystemExit("hakios_input_init() not found")
    calls = [m.group(1) for m in CALL_RE.finditer(found[2])]

    enabled = config_enabled(tree)
    index = BodyIndex(tree)
    out: list[InitRecord] = []

    for i, fn in enumerate(calls, start=1):
        rec = InitRecord(order=i, fn=fn)
        drv, is_standard = driver_of(fn)
        rec.config = "standard" if is_standard else (f"CONFIG_HAKIOS_{drv}" if drv else "?")
        rec.built = True if is_standard else (drv in enabled if drv else False)

        path, line = locate(tree, fn)
        if path is None:
            rec.built, rec.src, rec.note = False, "(no definition)", "no definition found"
            out.append(rec)
            continue
        rec.src, rec.line = path, line

        if not rec.built:
            rec.note = "not built: hakios.stubs.c supplies an empty body"
            out.append(rec)
            continue

        got = index.body_of(fn, path)
        body = got[1] if got else ""
        rec.threads = bool(THREAD_RE.search(body))
        rec.sync_forever = bool(FOREVER_RE.search(body))
        rec.maps = sorted(set(re.findall(r"ml_io_map(?:_unmappable)?\(\s*([A-Z0-9_]+)", body)))

        g = scan_graph(index, fn, path, depth)
        rec.pmif, rec.pmif_depth = g["pmif"], g["pmif_depth"]
        rec.pmif_shared = g["pmif_shared"]
        rec.worker = g["worker"]
        rec.worker_forever = g["worker_forever"]
        rec.unbounded_poll = g["unbounded_poll"]
        rec.gpu = g["gpu"]
        rec.blocks_ms = round((delay_us(body) + g["head_delay_us"]) / 1000.0, 1)

        if rec.pmif:
            if rec.pmif_depth == 0:
                rec.pmif_where = "init"
            elif g["worker"] and rec.pmif_depth <= 1:
                rec.pmif_where = "worker"
            else:
                rec.pmif_where = "worker" if not rec.sync_forever else "init"
        if rec.sync_forever:
            rec.note = "init body loops forever -> blocks the serial init"
        elif rec.unbounded_poll:
            rec.note = "worker polls without a deadline"
        out.append(rec)
    return out


# ------------------------------------------------------------------ 输出

def print_table(records: list[InitRecord]) -> None:
    hdr = (f"{'#':>2}  {'function':30} {'thr':4} {'PMIF':6} {'GPU':4} {'block(ms)':>9} "
           f"{'loop':7} {'built':5} source")
    print(hdr)
    print("-" * (len(hdr) + 18))
    for r in records:
        pm = "-" if not r.pmif else r.pmif_where
        loop = "SYNC" if r.sync_forever else ("worker" if r.worker_forever else "-")
        print(f"{r.order:>2}  {r.fn:30} {'yes' if r.threads else '-':4} {pm:6} "
              f"{'yes' if r.gpu else '-':4} {r.blocks_ms:>9} {loop:7} "
              f"{'yes' if r.built else 'stub':5} {r.src}:{r.line}")


def print_summary(records: list[InitRecord]) -> None:
    built = [r for r in records if r.built]
    print()
    print(f"init calls                  : {len(records)}")
    print(f"built (not a stub)          : {len(built)}")
    print(f"stub (empty body)           : {len(records) - len(built)}")
    print(f"start a thread              : {sum(r.threads for r in built)}   "
          + ", ".join(r.fn for r in built if r.threads))
    print(f"access PMIF/SPMI            : {sum(r.pmif for r in built)}   "
          + ", ".join(f"{r.fn}({r.pmif_where}, depth {r.pmif_depth})" for r in built if r.pmif))
    print(f"access GPU 0x13000000       : {sum(r.gpu for r in built)}   "
          + ", ".join(r.fn for r in built if r.gpu))
    print(f"init body loops forever     : {sum(r.sync_forever for r in built)}   "
          + ", ".join(r.fn for r in built if r.sync_forever))
    print(f"worker loops forever        : {sum(r.worker_forever for r in built)}   "
          + ", ".join(r.fn for r in built if r.worker_forever))
    print(f"worker polls w/o timeout    : {sum(r.unbounded_poll for r in built)}   "
          + ", ".join(r.fn for r in built if r.unbounded_poll))
    print(f"delays in the init path     : {sum(r.blocks_ms for r in built)} ms")


# ------------------------------------------------------------------ selftest

def selftest(tree: str) -> int:
    recs = analyse(tree)
    by = {r.fn: r for r in recs}
    checks: list[tuple[str, bool, str]] = []

    def add(name: str, ok: bool, detail: str = "") -> None:
        checks.append((name, ok, detail))

    add("A1  第一条调用是 hakios_cpufreq_init", recs[0].fn == "hakios_cpufreq_init", recs[0].fn)
    add("A2  最后一条调用是 hakios_mtk_emmc_devfs",
        recs[-1].fn == "hakios_mtk_emmc_devfs", recs[-1].fn)
    add("A3  hakios_mt6895_headset_init 不在顶层调用列表里",
        "hakios_mt6895_headset_init" not in by)
    for fn in ("hakios_mt6895_keys_init", "hakios_mali610_init",
               "hakios_nvt_init", "hakios_mt6895_usb_init"):
        add(f"A4  {fn} 调 kernel_thread_start", by[fn].threads is True, str(by[fn].threads))
    add("A5  PMIF 使用者恰为 keys 与 mali610",
        sorted(r.fn for r in recs if r.pmif) == ["hakios_mali610_init", "hakios_mt6895_keys_init"],
        ",".join(f"{r.fn}:{r.pmif_where}" for r in recs if r.pmif))
    add("A6  两者都是在自己的 worker 里访问 PMIF（串行阶段不碰）",
        all(r.pmif_where == "worker" for r in recs if r.pmif),
        ",".join(f"{r.fn}:{r.pmif_where}" for r in recs if r.pmif))
    add("A7  mali610 到 PMIF 是三跳（init→thread→bring_up→vstack_rail）",
        by["hakios_mali610_init"].pmif_depth == 3,
        str(by["hakios_mali610_init"].pmif_depth))
    add("A8  未编入的 cpufreq 判为 stub 且不声明任何原语",
        (by["hakios_cpufreq_init"].built is False
         and by["hakios_cpufreq_init"].threads is False
         and by["hakios_cpufreq_init"].pmif is False
         and by["hakios_cpufreq_init"].gpu is False))
    add("A9  stub 数量为 16（21 条调用里 5 条编入）",
        sum(1 for r in recs if not r.built) == 16, str(sum(1 for r in recs if not r.built)))
    add("A10 keys 的 PMIF 轮询带 deadline（不算无超时）",
        by["hakios_mt6895_keys_init"].unbounded_poll is False)
    add("A11 mali610 的 blocks_ms 覆盖 worker 里 delay(2000*1000) = 2000 ms",
        by["hakios_mali610_init"].blocks_ms == 2000.0,
        str(by["hakios_mali610_init"].blocks_ms))
    add("A12 每个 stub 都不声明线程/PMIF/GPU（空体不该有任何原语）",
        all(not (r.threads or r.pmif or r.gpu) for r in recs if not r.built),
        ",".join(r.fn for r in recs if not r.built and (r.threads or r.pmif or r.gpu)))
    add("A13 没有任何 init 体自己在死循环（串行阶段不会卡死）",
        all(not r.sync_forever for r in recs if r.built))
    add("A14 GPU 窗口只被 mali610 用宏 MALI_GPU_PHYS 间接引用",
        [r.fn for r in recs if r.gpu] == ["hakios_mali610_init"],
        ",".join(r.fn for r in recs if r.gpu))
    add("A15 nvt 的 worker 在轮询前先睡 200 ms（blocks_ms == 202.0）",
        by["hakios_nvt_init"].blocks_ms == 202.0, str(by["hakios_nvt_init"].blocks_ms))
    add("A16 keys 与 mali610 都经共享 pmif_* API 访问（同一把锁、同一份映射）",
        all(r.pmif_shared for r in recs if r.pmif),
        ",".join(f"{r.fn}:{r.pmif_shared}" for r in recs if r.pmif))
    add("A17 没有 init 在串行阶段直接访问 PMIF",
        all(r.pmif_where != "init" for r in recs),
        ",".join(f"{r.fn}:{r.pmif_where}" for r in recs if r.pmif_where == "init"))

    bad = 0
    for name, ok, detail in checks:
        print(f"{'PASS' if ok else 'FAIL'}  {name}" + (f"   [{detail}]" if detail and not ok else ""))
        bad += 0 if ok else 1
    print(f"\n{len(checks) - bad}/{len(checks)} passed")
    return 0 if bad == 0 else 1


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description="audit the order of hakios_input_init()")
    ap.add_argument("--tree", default=DEFAULT_TREE)
    ap.add_argument("--depth", type=int, default=4)
    ap.add_argument("--json", metavar="PATH")
    ap.add_argument("--selftest", action="store_true")
    ap.add_argument("--quiet", action="store_true")
    args = ap.parse_args(argv)

    if args.selftest:
        return selftest(args.tree)

    recs = analyse(args.tree, args.depth)
    if not args.quiet:
        print_table(recs)
        print_summary(recs)
    if args.json:
        payload = []
        for r in recs:
            d = asdict(r)
            d["src"] = f"{r.src}:{r.line}" if r.line else r.src
            d.pop("line", None)
            payload.append(d)
        with open(args.json, "w", encoding="utf-8") as fh:
            json.dump(payload, fh, ensure_ascii=False, indent=1)
            fh.write("\n")
        if not args.quiet:
            print(f"\nwrote {args.json}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
