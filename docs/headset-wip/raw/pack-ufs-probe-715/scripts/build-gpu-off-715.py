#!/usr/bin/env python3
"""Build one new host-only715 package with GPU paths disabled; audit outputs first."""
from pathlib import Path
import hashlib, importlib.util, json, shutil, subprocess
ROOT = Path("/home/lizi/IOS/hakios-mtk")
BASE = Path(__file__).resolve().parent
OLD = BASE / "trial714"
OUT = BASE / "trial715"
def sha(b): return hashlib.sha256(b).hexdigest()
def run(a):
    c = subprocess.run(a,cwd=ROOT,capture_output=True,text=True)
    row = dict(argv=a,rc=c.returncode,stdout=c.stdout,stderr=c.stderr)
    assert c.returncode==0,c.stderr
    return row

def main():
    assert not OUT.exists(), "Never overwrite an existing artifact directory"
    params=json.loads((BASE/"arguments715.json").read_text())
    assert params["device_execution_authorized"] is False
    assert params["nul_inclusive_bytes"]<=608
    OUT.mkdir()
    shutil.copytree(OLD/"loader-src",OUT/"loader-src")
    spec=importlib.util.spec_from_file_location("safe_loader",BASE/"reproduce-loader-safe.py")
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
    records=json.loads((OLD/"compile-commands.json").read_text())
    plans=[module.plan(row,n,OUT) for n,row in enumerate(records)]
    overrides=params["loader_macro_overrides"]
    for a in plans[:2]:
        for i,arg in enumerate(a):
            if arg.startswith("-DXNU_EXTRA_ARGS="):
                a[i]='-DXNU_EXTRA_ARGS="'+params["extra_arguments"]+'"'
        a[:]=[v for v in a if not any(v=="-D"+k or v.startswith("-D"+k+"=") for k in overrides)]
        at=a.index("-c");a[at:at]=["-D"+k+"=0" for k in overrides]
        for k in overrides:
            assert [v for v in a if v.startswith("-D"+k+"=")]==["-D"+k+"=0"]
        assert "-DXNU_WATCH_SECONDS=0" in a and "-DXNU_CORES=4" in a
    for n,a in enumerate(plans):
        assert Path(a[a.index("-o")+1]).resolve()==OUT/["start.o","mtkloader.o","loader.bin"][n]
        assert not any(str(OLD) in v or str(OLD.relative_to(ROOT)) in v for v in a)
    (OUT/"planned-commands.json").write_text(json.dumps(plans,indent=2)+"\n")
    # All compiler/linker output destinations were checked before the first command.
    built=[run(a) for a in plans]
    (OUT/"compile-commands.json").write_text(json.dumps(built,indent=2)+"\n")
    source=plans[1][plans[1].index("-c")+1]
    cpp=plans[1][:plans[1].index("-c")]+["-dM","-E",source]
    macros=run(cpp);(OUT/"loader-macros.txt").write_text(macros["stdout"])
    cpp_source=run(plans[1][:plans[1].index("-c")]+["-E","-P",source])["stdout"]
    for key in overrides:
        assert "#define "+key+" 0" in macros["stdout"]
    assert "mali_power_down(" not in cpp_source and "mali_leave_powered(" not in cpp_source
    macro_report=dict(rc=macros["rc"],argv=cpp,overrides=overrides,
        gpu_entry_and_cleanup_references_absent=True)
    (OUT/"macro-review.json").write_text(json.dumps(macro_report,indent=2)+"\n")
    kernel=BASE/"kernel-src/BUILD/obj/DEVELOPMENT_ARM64_BCM2837/mach.development.bcm2837"
    assert sha(kernel.read_bytes())=="ecdc9194fbcc635a63d8da6be7c5f4ef6a1c6bc7f3c1b715c9373cca9fe7da38"
    ref=ROOT/"logs/profile4-device-20261116/trial710/revision2"
    payload=run(["python3","xnuloader/mkpayload.py","-o",str(OUT/"payload.bin"),"--no-version",
        "kernel="+str(kernel),"ramdisk="+str(ref/"ref-ramdisk.bin"),"kernel.ver="+str(ref/"ref-kernel.ver.bin")])
    (OUT/"mkpayload-cmd.json").write_text(json.dumps(payload,indent=2)+"\n")
    assert (OUT/"payload.bin").read_bytes()==(OLD/"payload.bin").read_bytes()
    chain=run(["python3","mtk/tools/chainboot.py",str(ROOT/"backup/boot_a_stock_real.img"),
        str(OUT/"loader.bin"),str(OUT/"boot.img"),str(OUT/"payload.bin"),"--partition-size","64M"])
    (OUT/"chainboot-cmd.json").write_text(json.dumps(chain,indent=2)+"\n")
    artifacts={name:dict(bytes=(OUT/name).stat().st_size,sha256=sha((OUT/name).read_bytes())) for name in
        ["start.o","mtkloader.o","loader.bin","payload.bin","boot.img"]}
    (OUT/"artifacts.json").write_text(json.dumps(artifacts,indent=2)+"\n")
    print(json.dumps(dict(status="HOST_BUILD_COMPLETE",device_executed=False,artifacts=artifacts),indent=2))
if __name__=="__main__":main()
