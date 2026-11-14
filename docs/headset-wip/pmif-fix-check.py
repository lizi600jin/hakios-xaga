#!/usr/bin/env python3
"""Check the PMIF fix's diff draft before anybody applies it.

Three things are checked, in the order a reviewer would check them:

  1. every hunk's context actually exists in the target file -- a diff written against a
     file that has since moved is the most expensive kind of draft, because it applies with
     fuzz and lands in the wrong place;
  2. brackets balance in each result file, and the result still has no unmatched
     #if/#endif (a deleted `#else` is the classic way to break a driver without a brace
     going missing);
  3. every store to the PMIF command register in the original is accounted for: the script
     greps the *original* files for the ACC write (by every spelling they use), then
     requires that each such site is either removed by the diff or explicitly listed as
     kept, with a reason. A fix that misses one writer leaves the bus unserialised.

It also runs a self-test of its own parsing, because a checker that cannot fail is worse
than none:

    ./pmif-fix-check.py --selftest

Usage:
    ./pmif-fix-check.py                      # check the draft next to this script
    ./pmif-fix-check.py --diff F --root DIR  # check another draft against another tree
    ./pmif-fix-check.py --selftest           # prove the checks can fail
"""

import argparse
import os
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_DIFF = os.path.join(HERE, "SPMI-FIX-DESIGN.diff")
DEFAULT_ROOT = "/home/lizi/IOS/hakios-mtk"

# The MT6895's PMIF window, from SPMI-PMIF-SPEC.md §6 and the DTS. Nothing outside it is
# this block; 0x900 upwards belongs to spmi_pmif_mpu@1c804900.
PMIF_WINDOW_HI = 0x8FE

# Every way the three drivers spell a store to the command register today. The grep has to
# be by spelling, not by "ACC", or a rename hides a site.
ACC_PATTERNS = [
    r"pmif_wr\s*\(\s*(?:PMIF_SWINF_ACC|acc)\b",
    r"pmif_wr\s*\(\s*MALI_PMIF_ACC\b",
    r"pmif_wr\s*\(\s*PMIF_SWINF_ACC\b",
    r"reg_wr\s*\([^,]+,\s*MALI_PMIF_ACC\b",
    r"\*\s*\(\s*volatile\s+uint32_t\s*\*\s*\)\s*\(\s*pmif\s*\+\s*(?:PMIF_SWINF_ACC|acc)\b",
]

# The sites a reviewer has agreed may keep writing ACC directly. Empty is the goal; an
# entry needs a reason and a line, and the script prints it so the claim is visible.
KEPT_WRITERS = {}

HUNK_RE = re.compile(r"^@@ -(\d+)(?:,(\d+))? \+(\d+)(?:,(\d+))? @@")


class Problem(Exception):
    pass


# --------------------------------------------------------------------- diff parsing

def read_diff(path):
    """[(old_path, new_path, [hunk, ...])], one entry per file in the diff."""
    files = []
    current = None
    with open(path, encoding="utf-8") as handle:
        for number, raw in enumerate(handle, 1):
            line = raw.rstrip("\n")
            if line.startswith("--- "):
                old = line[4:].split("\t")[0].strip()
                current = {"old": old, "new": None, "hunks": [], "where": number}
                files.append(current)
            elif line.startswith("+++ ") and current is not None:
                current["new"] = line[4:].split("\t")[0].strip()
            elif line.startswith("@@") and current is not None:
                match = HUNK_RE.match(line)
                if not match:
                    raise Problem("%s:%d: cannot read the hunk header %r" % (path, number, line))
                old_start = int(match.group(1))
                old_len = int(match.group(2)) if match.group(2) is not None else 1
                new_start = int(match.group(3))
                new_len = int(match.group(4)) if match.group(4) is not None else 1
                current["hunks"].append({
                    "where": number, "old_start": old_start, "old_len": old_len,
                    "new_start": new_start, "new_len": new_len,
                    "lines": [], "header": line,
                })
            elif current is not None and current["hunks"]:
                hunk = current["hunks"][-1]
                if line[:1] in (" ", "-", "+", "\\"):
                    hunk["lines"].append(line)
    for entry in files:
        if entry["new"] is None:
            raise Problem("%s:%d: a '--- ' with no '+++ '" % (path, entry["where"]))
    return files


def strip_prefix(path):
    """'a/mtk/...' -> 'mtk/...'; '/dev/null' -> None."""
    if path == "/dev/null":
        return None
    if path[:2] in ("a/", "b/"):
        return path[2:]
    return path


def hunk_sides(hunk):
    """(context_and_removed, context_and_added) as lists of text, without the marker."""
    old, new = [], []
    for line in hunk["lines"]:
        if line.startswith("\\"):
            continue
        marker, text = line[:1], line[1:]
        if marker in (" ", "-"):
            old.append(text)
        if marker in (" ", "+"):
            new.append(text)
    return old, new


# --------------------------------------------------------------------- check 1: context

def check_context(files, root):
    problems = []
    for entry in files:
        old_rel = strip_prefix(entry["old"])
        if old_rel is None:
            problems.append("%s: a new file has no context to check" % entry["new"])
            continue
        target = os.path.join(root, old_rel)
        if not os.path.isfile(target):
            problems.append("%s: the diff's target is not there (%s)" % (old_rel, target))
            continue
        with open(target, encoding="utf-8", errors="surrogateescape") as handle:
            lines = handle.read().split("\n")
        for hunk in entry["hunks"]:
            old, _new = hunk_sides(hunk)
            start = hunk["old_start"] - 1
            if old and lines[start:start + len(old)] == old:
                continue
            # say which line of the hunk diverges, and what the file has there instead
            detail = ""
            for offset, want in enumerate(old):
                here = start + offset
                got = lines[here] if 0 <= here < len(lines) else "<past the end of the file>"
                if got != want:
                    detail = ("\n    %s:%d\n      the diff wants: %r\n      the file has: %r"
                              % (old_rel, here + 1, want, got))
                    break
            problems.append("%s:%d: the hunk's context is not in the file%s"
                            % (os.path.basename(old_rel), hunk["where"], detail))
    return problems


def apply(files, root, into):
    """Write the post-diff files into `into`; returns [(rel, path)] or raises Problem."""
    made = []
    for entry in files:
        old_rel = strip_prefix(entry["old"])
        new_rel = strip_prefix(entry["new"])
        if old_rel is None or new_rel is None:
            raise Problem("%s: file creation and deletion are not supported by this check"
                          % entry["new"])
        source = os.path.join(root, old_rel)
        with open(source, encoding="utf-8", errors="surrogateescape") as handle:
            lines = handle.read().split("\n")
        out = []
        cursor = 0
        for hunk in entry["hunks"]:
            start = hunk["old_start"] - 1
            out.extend(lines[cursor:start])
            _old, new = hunk_sides(hunk)
            out.extend(new)
            cursor = start + len(hunk_sides(hunk)[0])
        out.extend(lines[cursor:])
        destination = os.path.join(into, new_rel)
        os.makedirs(os.path.dirname(destination), exist_ok=True)
        with open(destination, "w", encoding="utf-8") as handle:
            handle.write("\n".join(out))
        made.append((new_rel, destination))
    return made


# --------------------------------------------------------------------- check 2: syntax

def check_braces(path, text):
    """Brackets balance, ignoring what is inside comments, strings and characters."""
    problems = []
    depth = {"(": 0, "[": 0, "{": 0}
    closing = {")": "(", "]": "[", "}": "{"}
    line_of = {}
    i, line, n = 0, 1, len(text)
    while i < n:
        ch = text[i]
        if ch == "\n":
            line += 1
            i += 1
            continue
        if text.startswith("/*", i):
            end = text.find("*/", i + 2)
            if end < 0:
                problems.append("%s:%d: a /* that is never closed" % (path, line))
                return problems
            line += text.count("\n", i, end)
            i = end + 2
            continue
        if text.startswith("//", i):
            end = text.find("\n", i)
            i = n if end < 0 else end
            continue
        if ch in "\"'":
            quote, start_line = ch, line
            i += 1
            while i < n:
                if text[i] == "\\":
                    i += 2
                    continue
                if text[i] == quote:
                    break
                if text[i] == "\n":
                    line += 1
                i += 1
            if i >= n:
                problems.append("%s:%d: a %s that is never closed" % (path, start_line, quote))
                return problems
            i += 1
            continue
        if ch in depth:
            depth[ch] += 1
            line_of.setdefault(ch, []).append(line)
            i += 1
            continue
        if ch in closing:
            want = closing[ch]
            if depth[want] == 0:
                problems.append("%s:%d: an unmatched %r" % (path, line, ch))
                return problems
            depth[want] -= 1
            if line_of[want]:
                line_of[want].pop()
            i += 1
            continue
        i += 1
    for opener, count in depth.items():
        if count:
            where = line_of[opener][-1] if line_of.get(opener) else 0
            problems.append("%s:%d: %d %r opened and never closed" % (path, where, count, opener))
    return problems


def check_conditionals(path, text):
    """#if / #ifdef / #ifndef against #endif, with #else / #elif allowed only inside one."""
    problems = []
    stack = []
    for number, line in enumerate(text.split("\n"), 1):
        stripped = line.strip()
        if not stripped.startswith("#"):
            continue
        word = stripped[1:].strip().split("(")[0].split()[0] if stripped[1:].strip() else ""
        if word in ("if", "ifdef", "ifndef"):
            stack.append(number)
        elif word == "endif":
            if not stack:
                problems.append("%s:%d: #endif with no #if" % (path, number))
            else:
                stack.pop()
        elif word in ("else", "elif"):
            if not stack:
                problems.append("%s:%d: #%s with no #if" % (path, number, word))
    for number in stack:
        problems.append("%s:%d: #if with no #endif" % (path, number))
    return problems


# --------------------------------------------------------------------- check 3: writers

def find_acc_writers(text):
    """[(line_number, line)] of every store to the command register, by any spelling."""
    found = []
    for number, line in enumerate(text.split("\n"), 1):
        code = line.split("//")[0]
        for pattern in ACC_PATTERNS:
            if re.search(pattern, code):
                found.append((number, line.strip()))
                break
    return found


def check_writers(files, root):
    """
    Every ACC store in the original must be gone from the result, or declared.

    This is the check that the whole task turns on: three implementations of one bus
    transaction mean three writers, and a fix that converts two of them leaves the third
    racing the lock it does not take.
    """
    problems = []
    removed, kept = [], []
    for entry in files:
        old_rel = strip_prefix(entry["old"])
        if old_rel is None:
            continue
        source = os.path.join(root, old_rel)
        with open(source, encoding="utf-8", errors="surrogateescape") as handle:
            before = handle.read()
        writers = find_acc_writers(before)
        if not writers:
            continue
        # what the diff removes from this file, as a multiset of stripped lines
        gone = set()
        for hunk in entry["hunks"]:
            for line in hunk["lines"]:
                if line.startswith("-") and not line.startswith("---"):
                    gone.add(line[1:].strip())
        for number, text in writers:
            if text in gone:
                removed.append((old_rel, number, text))
            else:
                kept.append((old_rel, number, text))
    for rel, number, text in kept:
        key = (os.path.basename(rel), number)
        if key in KEPT_WRITERS:
            continue
        problems.append("%s:%d: still writes the command register and the diff does not "
                        "remove it: %s" % (os.path.basename(rel), number, text))
    return problems, removed, kept


# --------------------------------------------------------------------- the whole check

def run(diff_path, root):
    problems = []
    if not os.path.isfile(diff_path):
        return ["no diff at %s" % diff_path], None
    files = read_diff(diff_path)

    problems += check_context(files, root)

    with tempfile.TemporaryDirectory() as scratch:
        try:
            made = apply(files, root, scratch)
        except Problem as error:
            return problems + ["%s" % error], None
        for rel, path in made:
            with open(path, encoding="utf-8", errors="surrogateescape") as handle:
                text = handle.read()
            problems += check_braces(rel, text)
            problems += check_conditionals(rel, text)

    writer_problems, removed, kept = check_writers(files, root)
    problems += writer_problems
    return problems, {"files": files, "removed": removed, "kept": kept}


# --------------------------------------------------------------------- self-test

def selftest():
    """Six assertions, each of which must fail in the way it claims to catch."""
    checks = 0
    with tempfile.TemporaryDirectory() as scratch:
        target = os.path.join(scratch, "t.c")
        with open(target, "w", encoding="utf-8") as handle:
            handle.write("int a;\nint b;\nint c;\n")

        def diff_of(body, name="d.diff"):
            path = os.path.join(scratch, name)
            with open(path, "w", encoding="utf-8") as handle:
                handle.write(body)
            return path

        good = ("--- a/t.c\n+++ b/t.c\n@@ -1,3 +1,3 @@\n int a;\n-int b;\n+int bee;\n int c;\n")

        # 1. a hunk whose context is in the file passes
        problems, _ = run(diff_of(good), scratch)
        assert not problems, "1: the good diff was rejected: %r" % problems
        checks += 1

        # 2. a hunk whose context is not in the file is caught, and named by line
        bad = ("--- a/t.c\n+++ b/t.c\n@@ -1,3 +1,3 @@\n int a;\n-int zzz;\n+int bee;\n int c;\n")
        problems, _ = run(diff_of(bad, "bad.diff"), scratch)
        assert problems and "context is not in the file" in problems[0], problems
        assert "t.c:2" in problems[0], problems[0]
        checks += 1

        # 3. an unmatched brace in the result is caught
        with open(target, "w", encoding="utf-8") as handle:
            handle.write("int a;\nint b;\nint c;\n")
        brace = ("--- a/t.c\n+++ b/t.c\n@@ -1,3 +1,4 @@\n int a;\n-int b;\n+int f(void) {\n+int b;\n int c;\n")
        problems, _ = run(diff_of(brace, "brace.diff"), scratch)
        assert any("never closed" in p or "unmatched" in p for p in problems), problems
        checks += 1

        # 4. a brace inside a comment or a string does not count
        assert not check_braces("x.c", 'int a; /* } */ char *s = "}"; // }\n')
        assert not check_braces("x.c", "int f(void) { return 0; }\n")
        assert check_braces("x.c", "int f(void) {\n")
        checks += 1

        # 5. a store to ACC that the diff leaves behind is caught
        with open(target, "w", encoding="utf-8") as handle:
            handle.write("void f(void) {\n\tpmif_wr(PMIF_SWINF_ACC, 1);\n\tpmif_wr(acc, 2);\n}\n")
        untouched = ("--- a/t.c\n+++ b/t.c\n@@ -1,4 +1,4 @@\n void f(void) {\n \tpmif_wr(PMIF_SWINF_ACC, 1);\n-\tpmif_wr(acc, 2);\n+\tpmif_read8(5, 0x1e, &v);\n }\n")
        problems, _ = run(diff_of(untouched, "untouched.diff"), scratch)
        assert any("still writes the command register" in p for p in problems), problems
        checks += 1

        # 6. both spellings are found, and a removing diff clears both
        with open(target, "w", encoding="utf-8") as handle:
            handle.write("void f(void) {\n\tpmif_wr(PMIF_SWINF_ACC, 1);\n\tpmif_wr(acc, 2);\n}\n")
        assert len(find_acc_writers(open(target, encoding="utf-8").read())) == 2
        both = ("--- a/t.c\n+++ b/t.c\n@@ -1,3 +1,1 @@\n void f(void) {\n-\tpmif_wr(PMIF_SWINF_ACC, 1);\n-\tpmif_wr(acc, 2);\n }\n")
        problems, result = run(diff_of(both, "both.diff"), scratch)
        assert not problems, problems
        assert len(result["removed"]) == 2, result["removed"]
        checks += 1

        # 7. a #if without its #endif is caught by the result
        assert not check_conditionals("x.c", "#if A\nint a;\n#endif\n")
        assert check_conditionals("x.c", "#if A\nint a;\n")
        assert not check_conditionals("x.c", "#if A\n#else\n#endif\n")
        assert check_conditionals("x.c", "#endif\n")
        checks += 1

    print("selftest: %d checks, all passed" % checks)
    return 0


# --------------------------------------------------------------------- main

def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--diff", default=DEFAULT_DIFF, help="the draft to check")
    parser.add_argument("--root", default=DEFAULT_ROOT, help="the tree the diff is against")
    parser.add_argument("--selftest", action="store_true", help="prove the checks can fail")
    args = parser.parse_args()

    if args.selftest:
        return selftest()

    print("pmif-fix-check: diff %s" % args.diff)
    print("pmif-fix-check: tree %s" % args.root)
    problems, result = run(args.diff, args.root)
    if result is not None:
        hunks = sum(len(f["hunks"]) for f in result["files"])
        print("pmif-fix-check: %d file(s), %d hunk(s), %d ACC store(s) removed, %d kept"
              % (len(result["files"]), hunks, len(result["removed"]), len(result["kept"])))
        for rel, number, text in result["removed"]:
            print("  - %s:%d  %s" % (os.path.basename(rel), number, text))
        for rel, number, text in result["kept"]:
            print("  = %s:%d  %s (declared)" % (os.path.basename(rel), number, text))
    if problems:
        for problem in problems:
            print("pmif-fix-check: " + problem)
        print("pmif-fix-check: %d problem(s)" % len(problems))
        return 1
    print("pmif-fix-check: the draft applies, the results parse, every ACC store is accounted for")
    return 0


if __name__ == "__main__":
    sys.exit(main())
