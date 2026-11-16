#!/usr/bin/env python3
"""Reproduce714 loader in private temporary output; never replay recorded -o blindly."""
from pathlib import Path
import hashlib, json, shutil, subprocess, tempfile
ROOT = Path("/home/lizi/IOS/hakios-mtk")
BASE = Path(__file__).resolve().parent
SOURCE = BASE / "trial714"
def sha(b): return hashlib.sha256(b).hexdigest()
def tree(d): return {str(f.relative_to(d)):sha(f.read_bytes()) for f in d.rglob("*") if f.is_file()}
def resolve(path):
    p = Path(path)
    return p.resolve() if p.is_absolute() else (ROOT / p).resolve()
def remap_source(path, temporary):
    rel = resolve(path).relative_to((SOURCE / "loader-src").resolve())
    return str(temporary / "loader-src" / rel)
def plan(record, number, temporary):
    a = list(record["argv"])
    assert a[0] == ("ld.lld" if number == 2 else "clang")
    assert a.count("-o") == 1
    a[a.index("-o") + 1] = str(temporary / ["start.o","mtkloader.o","loader.bin"][number])
    for i, arg in enumerate(list(a)):
        if arg.startswith('-DBOARD_HEADER='):
            value = arg.split("=",1)[1]
            assert value.startswith('"') and value.endswith('"')
            a[i] = '-DBOARD_HEADER="' + remap_source(value[1:-1], temporary) + '"'
        if arg in ("-c", "-T"):
            a[i+1] = remap_source(a[i+1], temporary)
        if arg.endswith(".o") and i != a.index("-o") + 1:
            name = Path(arg).name
            assert name in ("start.o", "mtkloader.o")
            assert resolve(arg).parent == SOURCE.resolve()
            a[i] = str(temporary / name)
    out = Path(a[a.index("-o") + 1]).resolve()
    assert out.parent == temporary.resolve()
    assert not out.is_relative_to(SOURCE.resolve())
    forbidden = [str(SOURCE), str(SOURCE.relative_to(ROOT))]
    assert not any(old in arg for old in forbidden for arg in a)
    return a

def main():
    records = json.loads((SOURCE / "compile-commands.json").read_text())
    assert len(records) == 3
    expected = json.loads((BASE / "arguments714.json").read_text())["extra_arguments"]
    assert all('-DXNU_EXTRA_ARGS="' + expected + '"' in row["argv"] for row in records[:2])
    previous = ROOT / "logs/mt6895-cpu-baseline-20261116/trial713/loader-src"
    assert tree(SOURCE / "loader-src") == tree(previous)
    results = []
    with tempfile.TemporaryDirectory(prefix="ufs714-loader-review-") as name:
        work = Path(name)
        shutil.copytree(SOURCE / "loader-src", work / "loader-src")
        plans = [plan(row, n, work) for n, row in enumerate(records)]
        # Resolve and audit every output before starting any compiler/linker.
        outputs = [Path(a[a.index("-o")+1]) for a in plans]
        assert [f.name for f in outputs] == ["start.o","mtkloader.o","loader.bin"]
        for a in plans:
            c = subprocess.run(a, cwd=work, capture_output=True, text=True)
            results.append(dict(argv=a,rc=c.returncode,stdout=c.stdout,stderr=c.stderr))
            assert c.returncode == 0, c.stderr
        rebuilt = (work / "loader.bin").read_bytes()
        assert rebuilt == (SOURCE / "loader.bin").read_bytes()
        objects = {f.name:sha(f.read_bytes()) for f in outputs}
    report = dict(status="PASS",frozen_loader_mirror_matches_713=True,
        all_outputs_audited_before_execution=True,private_temporary_output=True,
        loader_reproduction_sha256=sha(rebuilt),reproduced_objects=objects,commands=results)
    (BASE / "independent-loader-review.json").write_text(json.dumps(report,indent=2)+"\n")
    print(json.dumps({k:v for k,v in report.items() if k!="commands"},indent=2))
if __name__ == "__main__": main()
