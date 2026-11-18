#!/usr/bin/env python3
"""Verify the unpacked Trial725 host candidate against its build outputs and stock boot."""
import argparse
import gzip
import hashlib
import json
import struct
from pathlib import Path

HERE = Path(__file__).resolve().parent
STOCK_SHA = "c5f8e7bde8e9f1d824d0c7915f7d701adce8142656a82c20e10ccb75e15e01fc"
RAMDISK_SHA = "78985f67583b4d4666e4612681b879720259d83b309530e2b82c1246d5ed5bce"


def sha(data):
    return hashlib.sha256(data).hexdigest()


def check(condition, description):
    if not condition:
        raise ValueError(description)


def aligned(size, multiple):
    return (size + multiple - 1) // multiple * multiple


def boot_parts(data):
    check(data[:8] == b"ANDROID!" and len(data) >= 4096, "Android boot header")
    ksize, rsize = struct.unpack_from("<II", data, 8)
    check(struct.unpack_from("<I", data, 20)[0] == 1584 and
          struct.unpack_from("<I", data, 40)[0] == 4, "expected Android v4")
    sig_size = struct.unpack_from("<I", data, 1580)[0]
    k_end = 4096 + ksize
    rest = aligned(k_end, 4096)
    end = rest + aligned(rsize, 4096) + aligned(sig_size, 4096)
    check(0 < ksize and end <= len(data), "section geometry")
    return data[4096:k_end], data[rest:end], end


def payload_entries(payload):
    magic, length = struct.unpack_from("<II", payload)
    check(magic == 0x50554e58 and length == len(payload) - 8, "XNUP header")
    offset = 8
    entries = {}
    for name in ("kernel", "ramdisk", "kernel.ver"):
        field, size, reserved = struct.unpack_from("<16sII", payload, offset)
        check(field == name.encode().ljust(16, b"\0") and reserved == 0, f"entry {name}")
        start = offset + 24
        end = start + size
        offset = start + aligned(size, 16)
        check(size > 0 and offset <= len(payload) and not any(payload[end:offset]), f"size of {name}")
        entries[name] = payload[start:end]
    check(payload[offset:] == bytes(24), "XNUP end marker")
    return entries


def verify(root, stock_path):
    stock = stock_path.read_bytes()
    check(sha(stock) == STOCK_SHA and len(stock) == 64 * 1024 ** 2, "stock boot identity")
    stock_k, stock_tail, _ = boot_parts(stock)
    android = gzip.decompress(stock_k) if stock_k.startswith(b"\x1f\x8b") else stock_k
    folder = root / "trial725"
    boot = (folder / "boot.img").read_bytes()
    loader = (root / "build/r2/loader.bin").read_bytes()
    payload = (folder / "payload.bin").read_bytes()
    kernel = (root / "kernel-src/BUILD/obj/DEVELOPMENT_ARM64_BCM2837/mach.development.bcm2837").read_bytes()
    check(loader == (folder / "loader.bin").read_bytes(), "loader copy")
    k, tail, end = boot_parts(boot)
    check(len(boot) == end <= len(stock), "final partition size")
    check(boot[:8] == stock[:8] and boot[12:4096] == stock[12:4096] and tail == stock_tail,
          "stock header, ramdisk and signature sections")
    image = gzip.decompress(k)
    chain = 0x40000
    off = chain + aligned(len(android), 16)
    linux_size = struct.unpack_from("<Q", android, 0x10)[0]
    reserve = aligned(max(chain + linux_size, off + len(payload)), 1 << 20)
    check(image[0x38:0x3c] == android[0x38:0x3c] == b"ARM\x64", "ARM64 image header")
    check(0 < len(loader) <= chain, "loader size")
    expected_head = bytearray(loader)
    for pos, value in ((0x10, reserve), (0x28, len(android)), (0x30, off)):
        struct.pack_into("<Q", expected_head, pos, value)
    check(image[:chain] == expected_head.ljust(chain, b"\0"), "patched loader")
    check(image[chain:off] == android.ljust(off-chain, b"\0"), "embedded stock kernel")
    check(image[off:] == payload, "embedded XNUP payload")
    entries = payload_entries(payload)
    check(entries["kernel"] == kernel and b"mtk_ufs_blocks\0" in kernel and b"mtk_ufs_mixed\0" in kernel, "embedded Trial725 kernel")
    check(sha(entries["ramdisk"]) == RAMDISK_SHA, "XNU ramdisk")
    argfile = (HERE / "arguments725.json").read_bytes()
    args = json.loads(argfile)
    requests = [{"lba": 3, "blocks": 1}, {"lba": 2, "blocks": 2}, {"lba": 3, "blocks": 1}]
    check(args["trial"] == 725 and args["expected_requests"] == requests, "frozen request sequence")
    base = args["base_arguments"].encode()
    version = entries["kernel.ver"]
    check(loader.count(base + b"\0") == 1 and b"fresh trial725 lease" in loader, "loader arguments and trial")
    check(version.endswith(b"\0") and b"\0" not in version[:-1] and
          sha(version) == args["kernel_ver_sha256"], "kernel version")
    full = base + b" hakios_kver=" + version[:-1]
    check(full.decode() == args["full_arguments"] and len(full)+1 == args["nul_inclusive_bytes"] <= 608,
          "reconstructed boot arguments")
    for name, value in {"mtk_ufs_blocks": "1", "mtk_ufs_start_lba": "3", "mali_trial": "725",
                        "mtk_ufs_twoblock": "1", "mtk_ufs_mixed": "1", "loader_return_after": "150"}.items():
        check([t for t in full.decode().split() if t.startswith(name + "=")] == [name + "=" + value],
              f"boot argument {name}")
    files = {"boot.img": boot, "payload.bin": payload, "loader.bin": loader, **entries, "arguments725.json": argfile}
    return {"trial": 725, "verification": "offline package PASS", "hardware_tested": False,
            "requests": requests,
            "stock_sha256": STOCK_SHA, "boot_argument_bytes_with_nul": len(full)+1,
            "files": {name: {"bytes": len(data), "sha256": sha(data)} for name, data in files.items()}}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--work-dir", type=Path, required=True)
    parser.add_argument("--stock", type=Path, required=True)
    options = parser.parse_args()
    print(json.dumps(verify(options.work_dir, options.stock), indent=2))
