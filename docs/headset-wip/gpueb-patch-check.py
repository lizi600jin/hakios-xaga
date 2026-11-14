#!/usr/bin/env python3
"""Check a unified diff before anyone applies it: context lines, bracket balance, line numbers.

usage: gpueb-patch-check.py PATCH [--root DIR] [--tol N] [--verbose]
       gpueb-patch-check.py --selftest

What it checks, and why each one is a way a hand-written patch goes wrong:

  context    every ' ' line and every '-' line of a hunk must be in the target file, at the place the
             hunk header says. A patch written by hand against a file that was edited meanwhile has
             the right text in the wrong place, and `git apply` says only "patch does not apply".
             This says which line and how far off it is.
  numbers    the hunk header's old-file line number must be within --tol lines of where the context
             actually is (default 20). A hunk whose numbers are off by a hundred will be applied to
             the wrong function by `patch -l` and silently rejected by `git apply -C1`.
  balance    for every file a hunk touches, the running total of ( and ) and of { and } over the
             lines the patch leaves in the file must not go negative, and the totals for a file that
             is patched in place must come back to what they were: a dropped closing brace is the
             single most common way a hand-written hunk compiles nowhere.
  stat       the counts in the hunk header (@@ -a,b +c,d @@) must equal the number of lines the
             hunk actually has. A mismatch means a line was edited after the counts were written.

What it does not do: it does not apply anything and it does not write. It reads the patch and the
target files, and prints what it found.

The target of each file is found from the diff's own path: a/ and b/ are dropped, and the rest is
looked up under --root. --root defaults to the repository this script is run from (the nearest
directory above it that has both drivers/ and mtk/), so `gpueb-patch-check.py patch` works from
anywhere inside the tree.
"""
import argparse
import os
import re
import sys

HUNK = re.compile(r"^@@ -(\d+)(?:,(\d+))? \+(\d+)(?:,(\d+))? @@ ?(.*)$")


class Hunk:
    def __init__(self, path, old_start, old_count, new_start, new_count, section, lineno):
        self.path = path
        self.old_start, self.old_count = old_start, old_count
        self.new_start, self.new_count = new_start, new_count
        self.section = section
        self.lineno = lineno            # where the @@ line is in the patch
        self.lines = []                 # (kind, text): kind in ' ', '-', '+', '\\'

    def body(self):
        return self.lines


class Patch:
    def __init__(self):
        self.files = []                 # [(old_path, new_path, is_new, is_delete)]
        self.hunks = []


def parse(text):
    p = Patch()
    lines = text.split("\n")
    if lines and lines[-1] == "":
        lines.pop()                     # the newline at the end of the patch, not a context line
    i, old, new = 0, None, None
    while i < len(lines):
        line = lines[i]
        if line.startswith("diff --git "):
            m = re.match(r"^diff --git a/(.*) b/(.*)$", line)
            old, new = (m.group(1), m.group(2)) if m else (None, None)
        elif line.startswith("--- "):
            old = line[4:].split("\t")[0]
            old = old[2:] if old.startswith("a/") else old
        elif line.startswith("+++ "):
            new = line[4:].split("\t")[0]
            new = new[2:] if new.startswith("b/") else new
            if old == "/dev/null" or old is None:
                p.files.append((None, new, True, False))
            elif new == "/dev/null":
                p.files.append((old, None, False, True))
            else:
                p.files.append((old, new, False, False))
        elif line.startswith("@@"):
            m = HUNK.match(line)
            if not m:
                i += 1
                continue
            path = new if new not in (None, "/dev/null") else old
            h = Hunk(path, int(m.group(1)), int(m.group(2) or 1),
                     int(m.group(3)), int(m.group(4) or 1), m.group(5), i + 1)
            i += 1
            while i < len(lines):
                b = lines[i]
                if b.startswith("@@") or b.startswith("diff --git ") or b.startswith("--- "):
                    break
                if b.startswith("\\"):                  # "\ No newline at end of file"
                    h.lines.append(("\\", b))
                elif b[:1] in (" ", "-", "+"):
                    h.lines.append((b[0], b[1:]))
                elif b == "":
                    # a bare empty line inside a hunk is a context line whose space was stripped
                    h.lines.append((" ", ""))
                else:
                    break
                i += 1
            p.hunks.append(h)
            continue
        i += 1
    return p


def read_target(root, path):
    full = os.path.join(root, path)
    if not os.path.isfile(full):
        return None
    with open(full, encoding="utf-8", errors="replace") as f:
        return f.read().split("\n")


def find_block(target, block, near, tol):
    """Where `block` sits in `target`, as a 1-based line number, preferring a place near `near`."""
    if not block:
        return None, 0
    n = len(block)
    hits = [i + 1 for i in range(len(target) - n + 1) if target[i:i + n] == block]
    if not hits:
        return None, 0
    best = min(hits, key=lambda h: (abs(h - near), h))
    return best, len(hits)


def check(patch_text, root, tol, verbose):
    p = parse(patch_text)
    problems, notes = [], []

    if not p.hunks:
        return ["no hunks found in the patch"], notes

    # every file: does it exist, and is it a new file or an in-place edit
    new_files = {new for old, new, is_new, _ in p.files if is_new}
    deleted = {old for old, new, _, is_del in p.files if is_del}
    targets = {}
    for path in {h.path for h in p.hunks}:
        if path in new_files:
            targets[path] = None
            continue
        t = read_target(root, path)
        if t is None:
            problems.append("%s: not found under %s (the patch expects it there; --root names the "
                            "tree the a/ and b/ paths are relative to)" % (path, root))
        targets[path] = t

    # 1. hunk header counts against the lines actually present
    for h in p.hunks:
        got_old = sum(1 for k, _ in h.lines if k in (" ", "-"))
        got_new = sum(1 for k, _ in h.lines if k in (" ", "+"))
        if (got_old, got_new) != (h.old_count, h.new_count):
            problems.append(
                "%s:%d: header says -%d,%d +%d,%d but the hunk has %d old and %d new lines"
                % (h.path, h.lineno, h.old_start, h.old_count, h.new_start, h.new_count,
                   got_old, got_new))

    # 2. context lines are in the target, near where the header says
    for h in p.hunks:
        t = targets.get(h.path)
        if t is None:
            continue                        # a new file: nothing to look up
        block = [txt for k, txt in h.lines if k in (" ", "-")]
        at, nhits = find_block(t, block, h.old_start, tol)
        if at is None:
            # narrow it down: which of the block's lines is missing
            first = next((i for i, txt in enumerate(block) if txt not in t), None)
            if first is not None:
                problems.append(
                    "%s:%d: context/addressed line %r (hunk line %d) is nowhere in the file"
                    % (h.path, h.lineno, block[first], first + 1))
            else:
                problems.append(
                    "%s:%d: every line of the hunk is in the file, but not in this order"
                    % (h.path, h.lineno))
            continue
        off = at - h.old_start
        if abs(off) > tol:
            problems.append(
                "%s:%d: header says the hunk starts at line %d, the context is at %d (off by %+d, "
                "tolerance %d)" % (h.path, h.lineno, h.old_start, at, off, tol))
        elif off:
            notes.append("%s:%d: context found at %d, header says %d (%+d)"
                         % (h.path, h.lineno, at, h.old_start, off))
        if verbose:
            notes.append("%s:%d: %d context lines match at %d (%d place%s in the file)"
                         % (h.path, h.lineno, len(block), at, nhits,
                            "" if nhits == 1 else "s"))

    # 3. brackets: per file, add the '+' lines and drop the '-' lines, then compare totals
    touched = {}
    for h in p.hunks:
        if h.path in new_files:
            continue
        touched.setdefault(h.path, []).append(h)
    for path, hunks in touched.items():
        t = targets.get(path)
        if t is None:
            continue
        # the file's own totals, and the totals of the hunks' old and new sides
        whole = {"(": 0, ")": 0, "{": 0, "}": 0}
        for line in t:
            for c in whole:
                whole[c] += line.count(c)
        old_side = {"(": 0, ")": 0, "{": 0, "}": 0}
        new_side = {"(": 0, ")": 0, "{": 0, "}": 0}
        for h in hunks:
            for kind, txt in h.lines:
                if kind in (" ", "-"):
                    for c in old_side:
                        old_side[c] += txt.count(c)
                if kind in (" ", "+"):
                    for c in new_side:
                        new_side[c] += txt.count(c)
        if old_side != new_side:
            # the file's totals after the patch = whole - old_side + new_side
            for a, b in (("(", ")"), ("{", "}")):
                want = whole[a] - whole[b]
                got = want - (old_side[a] - old_side[b]) + (new_side[a] - new_side[b])
                if got != 0:
                    problems.append(
                        "%s: after the patch %r minus %r is %+d, not 0 (the hunks add %+d and remove "
                        "%+d)" % (path, a, b, got, new_side[a] - new_side[b],
                                  old_side[a] - old_side[b]))
        else:
            notes.append("%s: the hunks are bracket-neutral (%d hunks)" % (path, len(hunks)))

    # 4. a hunk header that names the enclosing line helps a reviewer; git leaves it empty for a hunk
    #    at the top of a file, so this is a note for --verbose only, never a problem
    if verbose:
        for h in p.hunks:
            if not h.section.strip() and h.path not in new_files:
                notes.append("%s:%d: the hunk header names no enclosing line"
                             % (h.path, h.lineno))

    return problems, notes


# ----------------------------------------------------------------------------- the checks' own tests

SELFTEST_FILES = {
    "f.c": (
        "int a(void)\n"
        "{\n"
        "\treturn 1;\n"
        "}\n"
        "\n"
        "int b(void)\n"
        "{\n"
        "\treturn 2;\n"
        "}\n"
    ),
}


def selftest():
    """Five assertions, each one a failure this script exists to catch."""
    import tempfile
    ok = 0
    fails = []

    def expect(name, patch, want_problem, want_note=None, tol=20):
        nonlocal ok
        with tempfile.TemporaryDirectory() as d:
            for path, text in SELFTEST_FILES.items():
                with open(os.path.join(d, path), "w") as f:
                    f.write(text)
            problems, notes = check(patch, d, tol, False)
        blob = "\n".join(problems)
        if want_problem is None:
            good = not problems
        else:
            good = want_problem in blob
        if want_note is not None:
            good = good and want_note in "\n".join(notes)
        print("%-46s %s" % (name, "PASS" if good else "FAIL"))
        if good:
            ok += 1
        else:
            fails.append(name)
            for line in problems:
                print("      problem: " + line)
            for line in notes:
                print("      note:    " + line)

    # 1. a clean hunk: context in the right place, one line added
    expect("1 clean hunk, context where it says",
           "--- a/f.c\n"
           "+++ b/f.c\n"
           "@@ -6,4 +6,5 @@ int b(void)\n"
           " int b(void)\n"
           " {\n"
           "+\tint unused;\n"
           " \treturn 2;\n"
           " }\n", None)

    # 2. context that is nowhere in the file
    expect("2 context line that does not exist",
           "--- a/f.c\n"
           "+++ b/f.c\n"
           "@@ -6,4 +6,4 @@\n"
           " int b(void)\n"
           " {\n"
           "-\treturn 3;\n"
           "+\treturn 4;\n"
           " }\n", "is nowhere in the file")

    # 3. context that is in the file but the header points a hundred lines off
    expect("3 header line number off by more than tolerance",
           "--- a/f.c\n"
           "+++ b/f.c\n"
           "@@ -120,3 +120,4 @@\n"
           " {\n"
           "+\tint x;\n"
           " \treturn 2;\n"
           " }\n", "off by", tol=20)

    # 4. a dropped closing brace
    expect("4 a hunk that removes a closing brace",
           "--- a/f.c\n"
           "+++ b/f.c\n"
           "@@ -6,4 +6,2 @@\n"
           " int b(void)\n"
           " {\n"
           "-\treturn 2;\n"
           "-}\n", "not 0")

    # 5. header counts that do not match the lines present
    expect("5 hunk header counts that lie",
           "--- a/f.c\n"
           "+++ b/f.c\n"
           "@@ -6,4 +6,9 @@\n"
           " int b(void)\n"
           " {\n"
           "+\tint x;\n"
           " \treturn 2;\n"
           " }\n", "header says")

    # 6. a new file: no context to look up, and it must not be reported as missing
    expect("6 new file, no target on disk",
           "--- /dev/null\n"
           "+++ b/g.h\n"
           "@@ -0,0 +1,2 @@\n"
           "+#ifndef G_H\n"
           "+#endif\n", None)

    print("\n%d of %d assertions passed" % (ok, ok + len(fails)))
    return 0 if not fails else 1


# --------------------------------------------------------------------------------------- entry

def default_root(paths):
    """The directory a/ and b/ are relative to: the current one if the patch's files are there, else
    the nearest ancestor of this script that has drivers/ and mtk/, else the current directory."""
    cwd = os.getcwd()
    for cand in (cwd,) + tuple(_ancestors(os.path.dirname(os.path.abspath(__file__)))):
        if all(os.path.isfile(os.path.join(cand, p)) for p in paths if p):
            return cand
        if os.path.isdir(os.path.join(cand, "drivers")) and os.path.isdir(os.path.join(cand, "mtk")):
            return cand
    return cwd


def _ancestors(d):
    for _ in range(12):
        yield d
        parent = os.path.dirname(d)
        if parent == d:
            return
        d = parent


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("patch", nargs="?", help="the unified diff to check")
    ap.add_argument("--root", default=None, help="where a/ and b/ paths are looked up")
    ap.add_argument("--tol", type=int, default=20,
                    help="how far the hunk header may be from where the context actually is (20)")
    ap.add_argument("--verbose", action="store_true")
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args(argv[1:])

    if a.selftest:
        return selftest()
    if not a.patch:
        ap.error("a patch is required (or --selftest)")

    with open(a.patch, encoding="utf-8", errors="replace") as f:
        text = f.read()
    existing = [h.path for h in parse(text).hunks]
    root = os.path.abspath(a.root or default_root(existing))

    problems, notes = check(text, root, a.tol, a.verbose)
    print("gpueb-patch-check: %s" % a.patch)
    print("gpueb-patch-check: root %s, tolerance %d lines" % (root, a.tol))
    for line in notes:
        print("  note:    " + line)
    for line in problems:
        print("  PROBLEM: " + line)
    if problems:
        print("gpueb-patch-check: %d problem(s)" % len(problems))
        return 1
    print("gpueb-patch-check: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
