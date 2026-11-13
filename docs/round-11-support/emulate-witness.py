#!/usr/bin/env python3
"""Validate the source witness bytes with an offline Cortex-M7 interpreter.

No device access. This checks instruction encoding, immediates and stores only;
it does not model the Mali MMU, clocks, cache, NVIC or reset/startup protocol.
"""
from pathlib import Path
import hashlib
import json
import re
import struct
import sys

import capstone
import unicorn
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS, UC_HOOK_MEM_WRITE
from unicorn.arm_const import UC_CPU_ARM_CORTEX_M7, UC_ARM_REG_SP, UC_ARM_REG_XPSR

out = Path(__file__).resolve().parent
driver = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(
    '/home/lizi/IOS/hakios-mtk/mtk/mt6895/drivers/gpu/hakios.mali610.c')
source = driver.read_text()
body = re.search(r'kMaliWitness\[\d*\]\s*=\s*\{([^}]+)\}', source).group(1)
code = bytes(int(x, 16) for x in re.findall(r'0x([0-9a-fA-F]{2})\b', body))
assert len(code) == 32
base, shared = 0x01000000, 0x04000000
md = capstone.Cs(capstone.CS_ARCH_ARM, capstone.CS_MODE_THUMB | capstone.CS_MODE_MCLASS)
instructions = [{'pc': hex(i.address), 'bytes': i.bytes.hex(),
                 'instruction': f'{i.mnemonic} {i.op_str}'.strip()}
                for i in md.disasm(code, base)]
assert sum(len(bytes.fromhex(i['bytes'])) for i in instructions) == len(code)

uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
uc.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_M7)
uc.mem_map(base, 4096)
uc.mem_map(shared, 4096)
uc.mem_write(base, code)
uc.mem_write(shared, struct.pack('<II', 0xff5aa5f0, 0))
uc.reg_write(UC_ARM_REG_SP, 0x02040000)
uc.reg_write(UC_ARM_REG_XPSR, 1 << 24)
writes = []


def store(_uc, _access, address, size, value, _data):
    writes.append({'address': hex(address), 'bytes': size, 'value': hex(value)})


uc.hook_add(UC_HOOK_MEM_WRITE, store)
uc.emu_start(base | 1, base + len(code), count=40)
final = struct.unpack('<II', uc.mem_read(shared, 8))
assert writes == [
    {'address': hex(shared), 'bytes': 4, 'value': '0x11'},
    {'address': hex(shared + 4), 'bytes': 4, 'value': '0x22'},
    {'address': hex(shared), 'bytes': 4, 'value': '0xdeadbeef'},
]
assert final == (0xdeadbeef, 0x22)
meta = {'driver_sha256': hashlib.sha256(driver.read_bytes()).hexdigest(),
        'witness_sha256': hashlib.sha256(code).hexdigest(), 'witness_hex': code.hex(),
        'unicorn': unicorn.__version__, 'capstone': capstone.__version__,
        'cpu': 'Cortex-M7, Thumb M-class', 'instruction_budget': 40,
        'instructions': instructions, 'stores': writes,
        'final_words': [hex(x) for x in final], 'outcome': 'expected_stores_completed',
        'limitation': 'Encoding test only; does not model Mali MMU, cache, clocks, NVIC or reset.'}
(out / 'witness-emulation.json').write_text(json.dumps(meta, indent=2) + '\n')
print(json.dumps(meta, indent=2))
