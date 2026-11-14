#!/usr/bin/env python3
"""accdet-init-gen.py — 从 accdet-init-table.json 生成裸机可直接照抄的 C 初始化表。

用法:
    ./accdet-init-gen.py                       # 生成 C 数组到 stdout 和 accdet-init-table.c
    ./accdet-init-gen.py -o path.c             # 指定输出文件
    ./accdet-init-gen.py --selftest            # 只做 JSON 合法性与地址范围自检
    ./accdet-init-gen.py --selftest -o /dev/null

JSON 格式: [{"seq":1,"reg":"0x2688","mask":"0xff","shift":0,"value":"0x00",
             "delay_us":0,"reason":"mt6368-accdet.c:1234"}]
mask/shift 为 null 表示整字节写。
op 字段（可选）: "write"(默认) / "verify"(只读校验，生成到单独的校验表) / "read"(采样读)
value 为 null 且 op=="verify"/"read" 时表示"只判读回值"，不生成写入项。

不猜值：脚本只做格式转换，任何 value 为 null 的 write 项都会让 --selftest 失败。
"""

import argparse
import json
import sys
from pathlib import Path

# 我们写入范围内的 MT6368 寄存器地址（0x1000–0x2fff 段），以及越界时的说明。
MT6368_REG_MIN = 0x0100
MT6368_REG_MAX = 0x2FFF
# 裸机 PMIF 通道一次只能搬 1 字节（EXT_REG_LONG，byte 地址），所以每个 mask 必须落在单字节内。
BYTE_MASK_LIMIT = 0xFF

VALID_OPS = ("write", "verify", "read")


class TableError(Exception):
    pass


def load_table(path):
    try:
        with open(path, "r", encoding="utf-8") as fh:
            data = json.load(fh)
    except FileNotFoundError:
        raise TableError(f"找不到输入文件: {path}")
    except json.JSONDecodeError as exc:
        raise TableError(f"JSON 解析失败 {path}:{exc.lineno}:{exc.colno}: {exc.msg}")
    if not isinstance(data, list):
        raise TableError("顶层必须是数组")
    if not data:
        raise TableError("数组为空")
    return data


def parse_int(value, field, seq):
    if isinstance(value, int):
        return value
    if isinstance(value, str):
        try:
            return int(value, 0)
        except ValueError:
            raise TableError(f"seq={seq}: {field} 不是合法整数: {value!r}")
    raise TableError(f"seq={seq}: {field} 类型非法: {type(value).__name__}")


def check_entry(entry, index, seen_seq):
    if not isinstance(entry, dict):
        raise TableError(f"第 {index} 项不是对象")
    for key in ("seq", "reg", "mask", "shift", "value", "delay_us", "reason"):
        if key not in entry:
            raise TableError(f"第 {index} 项缺字段 {key!r}")

    seq = entry["seq"]
    if not isinstance(seq, int) or seq < 1:
        raise TableError(f"第 {index} 项 seq 非法: {seq!r}")
    if seq in seen_seq:
        raise TableError(f"seq={seq} 重复")
    seen_seq.add(seq)

    op = entry.get("op", "write")
    if op not in VALID_OPS:
        raise TableError(f"seq={seq}: op 非法 {op!r}，只允许 {VALID_OPS}")

    reg = parse_int(entry["reg"], "reg", seq)
    if not (MT6368_REG_MIN <= reg <= MT6368_REG_MAX):
        raise TableError(
            f"seq={seq}: reg=0x{reg:04x} 超出 MT6368 可写窗口 "
            f"0x{MT6368_REG_MIN:04x}..0x{MT6368_REG_MAX:04x}"
        )

    mask = entry["mask"]
    shift = entry["shift"]
    if mask is None:
        if shift is not None:
            raise TableError(f"seq={seq}: mask 为 null 时 shift 必须也是 null")
        mask_i = 0xFF
        shift_i = 0
    else:
        mask_i = parse_int(mask, "mask", seq)
        if shift is None:
            raise TableError(f"seq={seq}: mask 非 null 时 shift 不能为 null")
        shift_i = parse_int(shift, "shift", seq)
        if mask_i <= 0 or mask_i > BYTE_MASK_LIMIT:
            raise TableError(f"seq={seq}: mask=0x{mask_i:x} 不在 1..0xff")
        if shift_i < 0 or shift_i > 7:
            raise TableError(f"seq={seq}: shift={shift_i} 不在 0..7")
        # mask 必须是连续的低位掩码（1..2^n-1），这样 mask<<shift 才落在单字节内
        if (mask_i & (mask_i + 1)) != 0:
            raise TableError(f"seq={seq}: mask=0x{mask_i:x} 不是连续低位掩码")
        if (mask_i << shift_i) > BYTE_MASK_LIMIT:
            raise TableError(
                f"seq={seq}: mask<<shift = 0x{mask_i << shift_i:x} 跨字节；"
                "PMIF EXT_REG_LONG 一次只写 1 字节，必须拆成两项"
            )

    value = entry["value"]
    if value is None:
        if op == "write":
            raise TableError(f"seq={seq}: write 项不允许 value=null（不许猜值）")
    else:
        value_i = parse_int(value, "value", seq)
        if not (0 <= value_i <= 0xFF):
            raise TableError(f"seq={seq}: value=0x{value_i:x} 不是字节")
        if op == "write" and (value_i & ~mask_i) != 0:
            raise TableError(
                f"seq={seq}: value=0x{value_i:02x} 超出 mask=0x{mask_i:02x}"
                "（value 是未移位的字段值，必须 <= mask）"
            )

    delay = entry["delay_us"]
    if not isinstance(delay, int) or delay < 0:
        raise TableError(f"seq={seq}: delay_us 非法: {delay!r}")
    if delay > 2_000_000:
        raise TableError(f"seq={seq}: delay_us={delay} 过大（>2s），疑似单位写错")

    reason = entry["reason"]
    if not isinstance(reason, str) or not reason.strip():
        raise TableError(f"seq={seq}: reason 为空")

    return {
        "seq": seq,
        "reg": reg,
        "mask": mask_i,
        "shift": shift_i,
        "value": None if value is None else parse_int(value, "value", seq),
        "delay_us": delay,
        "reason": reason,
        "op": op,
        "note": entry.get("note", ""),
    }


def validate(data):
    seen = set()
    rows = [check_entry(e, i, seen) for i, e in enumerate(data)]
    rows.sort(key=lambda r: r["seq"])
    seqs = [r["seq"] for r in rows]
    if seqs != list(range(1, len(rows) + 1)):
        raise TableError(f"seq 必须从 1 开始连续，实际: {seqs}")
    return rows


def render_c(rows, source_name):
    writes = [r for r in rows if r["op"] == "write"]
    verifies = [r for r in rows if r["op"] == "verify"]
    reads = [r for r in rows if r["op"] == "read"]

    out = []
    out.append("/* 由 accdet-init-gen.py 从 %s 生成，勿手改。 */" % source_name)
    out.append("/* MT6368 ACCDET 冷启动初始化序列（xaga / MT6895，SPMI SID 5）。 */")
    out.append("/* 每一项展开后都是『单字节 read-modify-write』：*/")
    out.append("/* 表中 mask/val 均已是『字节内就位』的形式（JSON 里的 mask<<shift 与 value<<shift）。 */")
    out.append("/*   v = pmif_read8(reg); v = (v & ~mask) | val; pmif_write8(reg, v); */")
    out.append("/* mask=0xff 表示整字节写（此时无需读回）。 */")
    out.append("")
    out.append("#ifndef _ACCDET_INIT_TABLE_H_")
    out.append("#define _ACCDET_INIT_TABLE_H_")
    out.append("")
    out.append("#include <stdint.h>")
    out.append("")
    out.append("struct accdet_wr {")
    out.append("\tuint16_t reg;")
    out.append("\tuint8_t  mask;")
    out.append("\tuint8_t  val;")
    out.append("\tuint16_t delay_us;")
    out.append("};")
    out.append("")
    out.append("/* 阶段 A：只读校验。读回值必须与 val 在 mask 内一致，否则停机。 */")
    out.append("struct accdet_chk {")
    out.append("\tuint16_t reg;")
    out.append("\tuint8_t  mask;")
    out.append("\tuint8_t  val;")
    out.append("};")
    out.append("")

    out.append("static const struct accdet_wr accdet_init_seq[] = {")
    for r in writes:
        out.append(
            "\t{ 0x%04x, 0x%02x, 0x%02x, %5d }, /* %2d */"
            % (r["reg"], r["mask"] << r["shift"], r["value"] << r["shift"],
               r["delay_us"], r["seq"])
        )
    out.append("};")
    out.append("")
    out.append("#define ACCDET_INIT_SEQ_LEN %d" % len(writes))
    out.append("")

    if verifies:
        out.append("/* 只读校验项：不写，只确认这些位已是期望值。 */")
        out.append("static const struct accdet_chk accdet_init_check[] = {")
        for r in verifies:
            val = 0 if r["value"] is None else r["value"]
            out.append(
                "\t{ 0x%04x, 0x%02x, 0x%02x }, /* %2d */"
                % (r["reg"], r["mask"] << r["shift"], val << r["shift"], r["seq"])
            )
        out.append("};")
        out.append("")
        out.append("#define ACCDET_INIT_CHECK_LEN %d" % len(verifies))
        out.append("")

    if reads:
        out.append("/* 采样项：cur_AB = (read8(0x26b6) >> 6) & 3；STATE = read8(0x26b7) & 7。 */")
        out.append("static const struct accdet_chk accdet_sample[] = {")
        for r in reads:
            out.append("\t{ 0x%04x, 0x%02x, 0x00 }, /* %2d */"
                       % (r["reg"], r["mask"] << r["shift"], r["seq"]))
        out.append("};")
        out.append("")
        out.append("#define ACCDET_SAMPLE_LEN %d" % len(reads))
        out.append("")

    out.append("#endif /* _ACCDET_INIT_TABLE_H_ */")
    return "\n".join(out) + "\n"


def selftest(path):
    ok = True
    print("== accdet-init-gen.py --selftest ==")
    print("input : %s" % path)
    try:
        data = load_table(path)
        print("PASS  JSON 解析：顶层数组 %d 项" % len(data))
    except TableError as exc:
        print("FAIL  %s" % exc)
        return 1

    try:
        rows = validate(data)
        print("PASS  逐项校验：seq 连续 1..%d，字段齐全" % len(rows))
    except TableError as exc:
        print("FAIL  %s" % exc)
        return 1

    writes = [r for r in rows if r["op"] == "write"]
    verifies = [r for r in rows if r["op"] == "verify"]
    reads = [r for r in rows if r["op"] == "read"]
    print("PASS  分类：write=%d verify=%d read=%d" % (len(writes), len(verifies), len(reads)))

    regs = sorted({r["reg"] for r in writes})
    print(
        "PASS  地址范围：%d 个 distinct 寄存器，0x%04x..0x%04x（全部落在 0x%04x..0x%04x）"
        % (len(regs), regs[0], regs[-1], MT6368_REG_MIN, MT6368_REG_MAX)
    )

    bad = [r for r in writes if r["value"] is None]
    if bad:
        print("FAIL  有 write 项没有值：%s" % [r["seq"] for r in bad])
        ok = False
    else:
        print("PASS  无空值 write 项（%d 项全部可追到源码行号）" % len(writes))

    noreason = [r for r in rows if ":" not in r["reason"]]
    if noreason:
        print("FAIL  有项 reason 不含源码行号：%s" % [r["seq"] for r in noreason])
        ok = False
    else:
        print("PASS  全部 %d 项 reason 含 file:line" % len(rows))

    total_us = sum(r["delay_us"] for r in writes)
    print("INFO  串行延时合计 %d us（%.1f ms）" % (total_us, total_us / 1000.0))

    # 单字节约束已经逐项查过，这里再确认一次整字节写的数量
    whole = [r for r in writes if (r["mask"] << r["shift"]) == 0xFF]
    partial = len(writes) - len(whole)
    print("INFO  整字节写 %d 项，位域读改写 %d 项" % (len(whole), partial))

    print("RESULT: %s" % ("PASS" if ok else "FAIL"))
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser(description="Generate the MT6368 ACCDET init C table from JSON.")
    ap.add_argument("json", nargs="?", default=None, help="输入 JSON（缺省取脚本同目录 findings/accdet-init-table.json）")
    ap.add_argument("-o", "--output", default=None, help="输出 C 文件（缺省 findings/accdet-init-table.c）")
    ap.add_argument("--selftest", action="store_true", help="只做自检，不生成 C")
    ap.add_argument("--quiet", action="store_true", help="不把 C 内容打到 stdout")
    args = ap.parse_args()

    here = Path(__file__).resolve().parent
    json_path = Path(args.json) if args.json else here / "findings" / "accdet-init-table.json"

    if args.selftest:
        return selftest(json_path)

    try:
        rows = validate(load_table(json_path))
    except TableError as exc:
        print("error: %s" % exc, file=sys.stderr)
        return 1

    text = render_c(rows, json_path.name)
    out_path = Path(args.output) if args.output else here / "findings" / "accdet-init-table.c"
    out_path.write_text(text, encoding="utf-8")
    if not args.quiet:
        sys.stdout.write(text)
    print(
        "wrote %s (%d write / %d verify / %d read)"
        % (out_path, sum(r["op"] == "write" for r in rows),
           sum(r["op"] == "verify" for r in rows), sum(r["op"] == "read" for r in rows)),
        file=sys.stderr,
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
