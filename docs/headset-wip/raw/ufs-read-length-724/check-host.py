#!/usr/bin/env python3
"""Build and exercise the Trial724 host engine against the frozen Trial723 source."""

import shutil
import subprocess
import tempfile
from pathlib import Path


HERE = Path(__file__).resolve().parent
BASE = HERE.parent / "final-ufs-varstart-source-723"
ENGINE = Path("source/ufs_twoblock/engine/ufs_two_block_engine.h")
TEST = Path("tests/test-ufs-two-block-engine.c")


def build(work, flags):
    subprocess.run(
        ["clang", "-std=c11", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function",
         "-Wconversion", "-Wshadow", "-pedantic", *flags, "-I", str(ENGINE.parent),
         str(TEST), "-o", "check"],
        cwd=work, check=True,
    )


def check_regressions(work):
    # The visible test failure must identify the regression under test.
    variants = [
        ("oversized prefill", "vrq_prepare(ctr, io, &cmd, data_words)",
         "vrq_prepare(ctr, io, &cmd, VRQ_DATA_WORDS)", "data callback outside the test window: offset 4096"),
        ("oversized restore", "vrq_restore(ctr, io, saved, data_words)",
         "vrq_restore(ctr, io, saved, VRQ_DATA_WORDS)", "data callback outside the test window: offset 4096"),
        ("CRC includes the unused page", "const uint32_t transfer_bytes = request.blocks * VR_BLOCK_BYTES;",
         "const uint32_t transfer_bytes = VR2_TRANSFER_BYTES;",
         "OUT.data_crc32 == tail_crc[blocks - 1]"),
    ]
    path = work / ENGINE
    original = path.read_text()
    for label, before, after, failure in variants:
        if original.count(before) != 1:
            raise RuntimeError(f"Expected one substitution site for {label}")
        path.write_text(original.replace(before, after))
        build(work, ["-O2"])
        p = subprocess.run([work / "check"], cwd=work, capture_output=True, text=True)
        if p.returncode != 1 or failure not in p.stderr:
            raise RuntimeError(f"{label}: rc={p.returncode}, stderr={p.stderr}")
        print(f"Detected regression: {label}", flush=True)


def main():
    with tempfile.TemporaryDirectory(prefix="ufs-724-") as tmp:
        work = Path(tmp)
        shutil.copytree(BASE / "source", work / "source")
        (work / "tests").mkdir()
        shutil.copyfile(BASE / TEST, work / TEST)
        subprocess.run(["git", "apply", "--no-index", str(HERE / "723-to-724.patch")],
                       cwd=work, check=True)
        for label, flags in [
            ("strict", ["-O2"]),
            ("ASan/UBSan", ["-O1", "-g", "-fsanitize=address,undefined", "-fno-sanitize-recover=all"]),
        ]:
            print(label, flush=True)
            build(work, flags)
            subprocess.run([work / "check"], cwd=work, check=True)
        check_regressions(work)


if __name__ == "__main__":
    main()
