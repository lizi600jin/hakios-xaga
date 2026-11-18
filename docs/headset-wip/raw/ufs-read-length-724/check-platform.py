#!/usr/bin/env python3
"""Replay the Trial724 platform tests with a synthetic or archived reference window."""

import argparse
import hashlib
import random
import re
import shutil
import subprocess
import tempfile
import zlib
from pathlib import Path

HERE = Path(__file__).resolve().parent
BASE = HERE.parent / "final-ufs-varstart-source-723"
REFERENCE = Path("source/ufs_varread/metadata/ufs_varread_reference.h")
FROZEN_SHA = "03628d70602e4f21e36ace76f5c4e27ed1b10e50ae690704035799ea34e4e82b"
KNOWN = (1, 2, 3, 14, 15)


def prepare_window(work, private):
    if private:
        if hashlib.sha256(private.read_bytes()).hexdigest() != FROZEN_SHA:
            raise ValueError("The supplied window differs from the frozen reference")
        return private.resolve()

    window = random.Random(724).randbytes(16 * 4096)
    path = work / "synthetic.bin"
    path.write_bytes(window)
    header = work / REFERENCE
    text = header.read_text()
    for lba in KNOWN:
        crc = zlib.crc32(window[lba * 4096:(lba + 1) * 4096])
        pattern = rf"(#define VR_REF_CRC_LBA{lba}\s+)UINT32_C\(0x[0-9a-f]+\)"
        text, matches = re.subn(pattern, rf"\g<1>UINT32_C(0x{crc:08x})", text)
        if matches != 1:
            raise ValueError(f"Expected one frozen reference for LBA {lba}")
    header.write_text(text)
    return path


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--reference-window", type=Path)
    args = ap.parse_args()
    with tempfile.TemporaryDirectory(prefix="platform-724-") as tmp:
        work = Path(tmp)
        shutil.copytree(BASE / "source", work / "source")
        shutil.copytree(BASE / "tests", work / "tests")
        subprocess.run(["git", "init", "-q"], cwd=work, check=True)
        for patch in ("723-to-724.patch", "724-platform.patch"):
            subprocess.run(["git", "apply", str(HERE / patch)], cwd=work, check=True)
        window = prepare_window(work, args.reference_window)
        for name, flags in [
            ("strict", ["-O2"]),
            ("ASan/UBSan", ["-O1", "-g", "-fsanitize=address,undefined", "-fno-sanitize-recover=all"]),
        ]:
            subprocess.run(
                ["clang", "-std=c11", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function",
                 "-Wconversion", "-Wshadow", "-pedantic", *flags, "-I", "tests", "-I", "source/ufs_twoblock",
                 "tests/test-ufs-twoblock-platform.c", "-o", "platform"],
                cwd=work, check=True,
            )
            subprocess.run([work / "platform", window], cwd=work, check=True)
            print(f"{name} passed", flush=True)


if __name__ == "__main__":
    main()
