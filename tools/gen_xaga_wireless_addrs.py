#!/usr/bin/env python3
"""
gen_xaga_wireless_addrs.py — 从 Cyber-Mobile/xaga-qemu 提取 MT6895 的
CONSYS / BTIF / DPMAIF / SCPSYS / MD1 真实地址事实，生成可直接 include 的头文件。

与离线包自带的 xaga_qemu_wireless_kit.py 的差别（那版有两个缺陷）：
  1. 那版只抓 #define，而 CONSYS 窗口是 mt6895.c 里的**数组字面量**，一个都没抓到。
  2. 那版的 KEYWORDS 白名单漏了 SCPSYS，且 DPMAIF 的寄存器名不带 "DPMAIF"
     字样（叫 AO_UL_INT_MASK / PD_DL_BAT_INIT_DONE），因此整段被过滤掉。

本脚本按"窗口表 + 整文件寄存器"两条路分别处理，结果用 X-macro 导出，
不产生 unused-variable 警告。

Usage:
    python3 gen_xaga_wireless_addrs.py /path/to/xaga-qemu -o out.h
"""

import argparse
import re
from pathlib import Path

# mt6895.c 里的窗口表：{ "mt6895.dev-consys-wf-top",    0x18400000ULL, 0x1000 },
WINDOW_RE = re.compile(
    r'\{\s*"mt6895\.dev-([a-z0-9_-]+)"\s*,\s*(0x[0-9A-Fa-f]+)ULL\s*,\s*(0x[0-9A-Fa-f]+)\s*\}'
)

DEFINE_RE = re.compile(r"^\s*#define\s+([A-Z][A-Z0-9_]*)\s+((?:0x[0-9a-fA-F]+|[0-9]+)[uUlL]*)")

# 只在 mt6895.c 的窗口表里出现过、属于本次攻关闭环的子系统
WANTED_GROUPS = ("consys", "bt", "gps", "md1_seq", "dpmaif")
GROUP_TAG = {
    "consys-wf-top": "WF_TOP",
    "consys-wf-cfg": "WF_CFG",
    "consys-wf-mcu": "WF_MCU",
    "consys-wf-misc": "WF_MISC",
    "consys-bgf-cfg": "BGF_CFG",
    "consys-bgf-sram": "BGF_SRAM",
    "consys-bgf-top": "BGF_TOP",
    "consys": "CONSYS",
    "bt": "BT",
    "gps": "GPS",
    "md1_seq": "MD1_SEQ",
    "dpmaif": "DPMAIF",
}

# 整文件提取寄存器定义：这两个文件里的宏名都不带模块前缀，必须全收
REGISTER_FILES = (
    ("hw/misc/mtk_dpmaif.c", "DPMAIF"),
    ("hw/misc/mtk_scpsys.c", "SCPSYS"),
)


def extract_windows(mt6895_c: Path):
    """返回 [(name, base, size)]，保持源文件顺序。"""
    if not mt6895_c.exists():
        return []
    out = []
    text = mt6895_c.read_text(encoding="utf-8", errors="ignore")
    for m in WINDOW_RE.finditer(text):
        name, base, size = m.group(1), int(m.group(2), 16), int(m.group(3), 16)
        if not any(g in name for g in WANTED_GROUPS):
            continue
        out.append((name, base, size))
    return out


def extract_defines(path: Path):
    if not path.exists():
        return []
    found = []
    for line in path.read_text(encoding="utf-8", errors="ignore").splitlines():
        m = DEFINE_RE.match(line)
        if m:
            found.append((m.group(1), m.group(2)))
    return found


def gen_windows(windows):
    """同名窗口出现多次时加 _N 后缀，保证宏名唯一。"""
    lines, seen = [], {}
    for name, base, size in windows:
        tag = GROUP_TAG.get(name, re.sub(r"[^A-Za-z0-9]", "_", name).upper())
        seen[tag] = seen.get(tag, 0) + 1
        suffix = "" if seen[tag] == 1 else f"_{seen[tag]}"
        lines.append((f"XAGA_WIN_{tag}{suffix}", f"0x{base:08X}ULL", f"0x{size:X}", name))
    return lines


def build_header(qemu: Path):
    windows = extract_windows(qemu / "hw/arm/mt6895.c")
    win_rows = gen_windows(windows)

    L = []
    L.append("/*")
    L.append(" * hakios_mt6895_wireless_addrs.h — 自动生成，勿手改。")
    L.append(" * 来源：Cyber-Mobile/xaga-qemu（hw/arm/mt6895.c、hw/misc/mtk_dpmaif.c、")
    L.append(" *       hw/misc/mtk_scpsys.c）")
    L.append(" * 生成器：tools/gen_xaga_wireless_addrs.py")
    L.append(" *")
    L.append(" * 这些地址是 MT6895 的板级事实，不是从别的机型抄的：qemu 里对它们做过")
    L.append(" * External Abort 与 probe 校验。写进 XNU 侧驱动前请先用真机 Android 的")
    L.append(" * /proc/device-tree/reserved-memory 与 /sys/kernel/debug/regmap 复核一遍。")
    L.append(" */")
    L.append("#ifndef HAKIOS_MT6895_WIRELESS_ADDRS_H")
    L.append("#define HAKIOS_MT6895_WIRELESS_ADDRS_H")
    L.append("")
    L.append("/* ---- MMIO 窗口表（X-macro，展开成任意形状都不产生未用告警） ---- */")
    L.append("#define XAGA_MMIO_WINDOWS(X) \\")
    width = max((len(r[0]) for r in win_rows), default=1)
    for i, (macro, base, size, src) in enumerate(win_rows):
        tag = macro[len("XAGA_WIN_"):]
        tail = " \\" if i < len(win_rows) - 1 else ""
        L.append(f'    X({tag:<{width - 6}s}, {base}, {size:>7s}) /* {src} */{tail}')
    L.append("")
    L.append(f"#define XAGA_MMIO_WINDOW_COUNT {len(win_rows)}")
    L.append("")
    L.append("/* ---- 同一批窗口的单值宏（X-macro 之外直取用） ---- */")
    for macro, base, size, _src in win_rows:
        tag = macro[len("XAGA_WIN_"):]
        L.append(f"#define XAGA_WIN_{tag}_BASE {base}")
        L.append(f"#define XAGA_WIN_{tag}_SIZE {size}")
    L.append("")

    for rel, tag in REGISTER_FILES:
        defs = extract_defines(qemu / rel)
        L.append(f"/* ---- {tag} 寄存器（{rel}） ---- */")
        if not defs:
            L.append(f"/* 未找到：{rel} */")
        for name, val in defs:
            if not val:
                continue
            L.append(f"#define XAGA_{tag}_{name:<34s} ({val}U)")
        L.append("")

    L.append("#endif /* HAKIOS_MT6895_WIRELESS_ADDRS_H */")
    return "\n".join(L) + "\n", len(win_rows)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("qemu_dir", type=Path)
    ap.add_argument("-o", "--output", type=Path,
                    default=Path("hakios_mt6895_wireless_addrs.h"))
    a = ap.parse_args()
    if not a.qemu_dir.is_dir():
        raise SystemExit(f"error: {a.qemu_dir} is not a directory")
    hdr, n = build_header(a.qemu_dir)
    a.output.parent.mkdir(parents=True, exist_ok=True)
    a.output.write_text(hdr, encoding="utf-8")
    print(f"Wrote {a.output} — {n} MMIO windows, {len(hdr.splitlines())} lines")


if __name__ == "__main__":
    main()
