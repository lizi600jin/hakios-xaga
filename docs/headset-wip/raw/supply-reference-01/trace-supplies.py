#!/usr/bin/env python3
"""Offline selected-codec DAPM scheduling plus actual callback instructions.

This is a software reference, never a device test. DAPM scheduling/coalescing is
modeled from the supplied core source; codec callbacks execute supplied module
instructions in Unicorn. Regmap, regulator, logging and DCXO calls are models.
"""
from collections import Counter
from itertools import groupby
import hashlib
import json
from pathlib import Path
import re
import struct

from unicorn import Uc, UC_ARCH_ARM64, UC_MODE_ARM, UC_HOOK_CODE
from unicorn.arm64_const import (
    UC_ARM64_REG_X0, UC_ARM64_REG_X1, UC_ARM64_REG_X2, UC_ARM64_REG_X3,
    UC_ARM64_REG_X18, UC_ARM64_REG_X30, UC_ARM64_REG_SP, UC_ARM64_REG_PC)

OUT = Path(__file__).resolve().parent
OLD = OUT.parent / 'headset-dapm-review-20261115'
ROOT = Path('/home/lizi/IOS/repo/hakios-mt6895/docs/headset-wip')
KO = ROOT / 'ko/snd-soc-mt6368.ko'
data = KO.read_bytes()
header = struct.unpack_from('<16sHHIQQQIHHHHHH', data)
sections = [struct.unpack_from('<IIQQQQIIQQ', data, header[6] + i * header[11])
            for i in range(header[12])]


def contents(index):
    s = sections[index]
    return data[s[4]:s[4] + s[5]]


def cstr(buf, offset):
    return buf[offset:buf.index(0, offset)].decode()


names = contents(header[13])
indices = {cstr(names, s[0]): i for i, s in enumerate(sections)}
sym_index = indices['.symtab']
strings = contents(sections[sym_index][6])
symbols = []
for at in range(0, sections[sym_index][5], 24):
    name, info, other, index, value, size = struct.unpack_from(
        '<IBBHQQ', contents(sym_index), at)
    symbols.append((cstr(strings, name), index, value, size))
functions = {s[0]: s[2] for s in symbols if s[1] == indices['.text']}

dapm_h = (OLD / 'include__sound__soc-dapm.h').read_text()
type_body = dapm_h.split('enum snd_soc_dapm_type {', 1)[1].split('};', 1)[0]
type_names = re.findall(r'^\s*(snd_soc_dapm_\w+)\s*(?:=\s*0)?\s*,', type_body, re.M)
assert type_names[0] == 'snd_soc_dapm_input'
types = {name: i for i, name in enumerate(type_names)}
core_path = OLD / 'sound__soc__soc-dapm.c'
core = core_path.read_text()


def type_order(up):
    body = core.split('static int dapm_' + ('up' if up else 'down') + '_seq[] = {', 1)[1].split('};', 1)[0]
    return {types[n]: int(v) for n, v in re.findall(r'\[(snd_soc_dapm_\w+)\]\s*=\s*(\d+)', body)}


BASE, STUB, HEAP, STACK, SHADOW, END = 0x1000000, 0x2000000, 0x3000000, 0x4000000, 0x4100000, 0x5000000
code = bytearray(contents(indices['.text']))
externals = {}
for i, s in enumerate(sections):
    if s[1] != 4 or s[7] != indices['.text']:
        continue
    for at in range(0, s[5], 24):
        offset, info, addend = struct.unpack_from('<QQq', contents(i), at)
        kind, symbol = info & 0xffffffff, symbols[info >> 32]
        if kind not in (282, 283):
            continue
        if symbol[1] == indices['.text']:
            target = BASE + symbol[2] + addend
        else:
            externals.setdefault(symbol[0], STUB + len(externals) * 16)
            target = externals[symbol[0]] + addend
        instruction = (0x94000000 if kind == 283 else 0x14000000) | (((target - BASE - offset) // 4) & 0x3ffffff)
        struct.pack_into('<I', code, offset, instruction)
for at in range(0, len(code), 4):
    if struct.unpack_from('<I', code, at)[0] in (0xd503233f, 0xd50323bf):
        struct.pack_into('<I', code, at, 0xd503201f)

u = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
for base, size in ((BASE, 0x100000), (STUB, 0x10000), (HEAP, 0x10000),
                   (STACK, 0x100000), (SHADOW, 0x10000), (END, 0x1000)):
    u.mem_map(base, size)
u.mem_write(BASE, bytes(code))
u.mem_write(END, b'\x1f\x20\x03\xd5')
priv, widget, dapm, component, control_array = [HEAP + n for n in (0x1000, 0x4000, 0x5000, 0x6000, 0x7000)]


def wr64(at, value):
    u.mem_write(at, struct.pack('<Q', value))


def wr32(at, value):
    u.mem_write(at, struct.pack('<I', value))


def init_private():
    u.mem_write(HEAP, bytes(0x10000))
    wr64(widget + 40, dapm)
    wr64(dapm - 184, component)
    wr64(component + 152, priv)
    wr64(widget + 144, control_array)
    wr64(control_array, HEAP + 0x8000)
    wr64(priv, HEAP)
    wr64(priv + 8, HEAP + 0x2000)
    wr32(priv + 16, 48000)
    wr32(priv + 48, 31)  # Deliberately muted reference gain, not an audible profile.
    wr32(priv + 108, 0)
    wr32(priv + 132, 0)
    for offset, value in ((156, 3), (160, 7), (164, 1), (168, 2)):
        wr32(priv + offset, value)  # Synthetic trim sentinels; never silicon values.


calls = []
regs = {}
context = {}


def record(op, **kwargs):
    calls.append(dict(context, op=op, **kwargs))


def update(reg, mask, value):
    before = regs.get(reg, 0)
    after = ((before & ~mask) | (value & mask)) & 255
    if reg in (0x260b, 0x260c):
        after &= 0x1f  # Observed trial-705 behavior, not a claim about reserved-bit type.
    regs[reg] = after
    record('update', reg=reg, mask=mask, value=value, before=before, after=after)


reverse = {address: name for name, address in externals.items()}


def hook(uc, address, size, unused):
    if address == END:
        uc.emu_stop()
        return
    name = reverse.get(address)
    if name is None:
        return
    args = [uc.reg_read(r) & 0xffffffff for r in
            (UC_ARM64_REG_X0, UC_ARM64_REG_X1, UC_ARM64_REG_X2, UC_ARM64_REG_X3)]
    result = 0
    if name == 'dapm_kcontrol_get_value':
        result = 2  # HP_MUX_HP, for both HPL/HPR.
    elif name == 'regmap_write':
        reg, value = args[1:3]
        before = regs.get(reg, 0)
        after = value & 255
        if reg in (0x260b, 0x260c):
            after &= 0x1f
        regs[reg] = after
        record('write', reg=reg, value=value, before=before, after=after)
    elif name == 'regmap_update_bits_base':
        update(*args[1:4])
    elif name == 'regmap_read':
        reg = args[1]
        wr32(args[2], regs.get(reg, 0))
        record('read', reg=reg, value=regs.get(reg, 0))
    elif name == 'usleep_range':
        record('delay', min_us=args[0], max_us=args[1])
    elif name == 'mt6685_set_dcxo':
        # External call boundary only. Its independent disassembly is retained.
        record('external_dcxo', enable=bool(args[0] & 1))
    elif name == 'dapm_regulator_event':
        record('external_regulator', event=args[2])
    elif name not in ('_dev_info', '_dev_warn', '__dynamic_dev_dbg'):
        raise RuntimeError(('unexpected external', name, context))
    uc.reg_write(UC_ARM64_REG_X0, result)
    uc.reg_write(UC_ARM64_REG_PC, uc.reg_read(UC_ARM64_REG_X30))


u.hook_add(UC_HOOK_CODE, hook)


def event(w, flag):
    if not w['event'] or not w['event_flags'] & flag:
        return
    context.update(widget=w['name'], origin='module_callback', event=flag)
    for reg, value in ((UC_ARM64_REG_SP, STACK + 0x80000), (UC_ARM64_REG_X18, SHADOW + 0x100),
                       (UC_ARM64_REG_X30, END), (UC_ARM64_REG_X0, widget),
                       (UC_ARM64_REG_X1, 0), (UC_ARM64_REG_X2, flag)):
        u.reg_write(reg, value)
    u.emu_start(BASE + w['event']['offset'], END + 4, count=200000)
    assert u.reg_read(UC_ARM64_REG_PC) == END, 'callback exceeded instruction budget'


def sequence(widgets, up):
    order = type_order(up)
    context.clear()
    context['phase'] = 'up' if up else 'down'
    ordered = sorted(widgets, key=lambda w: (order[w['type']], w['subseq'] if up else -w['subseq'], w['reg'], w['module_offset']))
    key = lambda w: (order[w['type']], w['subseq'], w['reg'])
    for groupkey, group in groupby(ordered, key):
        group = list(group)
        mask = value = 0
        reg = group[0]['reg']
        for w in group:
            mask |= w['mask'] << w['shift']
            value |= (w['on'] if up else w['off']) << w['shift']
            event(w, 1 if up else 4)
        if reg >= 0:
            context.update(widget=' + '.join(w['name'] for w in group), origin='modeled_dapm_coalesced', event=None)
            update(reg, mask, value)
        for w in group:
            event(w, 2 if up else 8)
    return [w['name'] for w in ordered]


inventory = json.loads((OUT / 'codec-dapm.json').read_text())
profiles = {}
for profile in ('normal_aif1_hp', 'internal_sgen_hp'):
    init_private()
    calls.clear()
    regs.clear()
    regs.update({0x259f: 0x22, 0x25a1: 0x55, 0x25a2: 0x10, 0x25a0: 0x12,
                 0x260b: 0x1f, 0x260c: 0x1f, 0x230c: 0x66, 0x238f: 0xff,
                 0x238e: int(profile == 'internal_sgen_hp')})
    selected = inventory[profile]['widgets']
    up_order = sequence(selected, True)
    enabled = dict(regs)
    down_order = sequence(selected, False)
    assert regs[0x2588] == regs[0x258a] == 0 and regs[0x2589] & 15 == 0
    assert enabled[0x2488] & 1 and not regs[0x2488] & 1
    assert enabled[0x25a2] & 0x10 == 0 and regs[0x25a2] & 0x10
    assert enabled[0x258e] == 0x19 and enabled[0x258f] == 0x3a
    assert regs[0x258e] == regs[0x258f] == 0
    profiles[profile] = dict(
        mux_precondition=dict(reg=0x238e, mask=1, value=int(profile == 'internal_sgen_hp'),
                              note='Control put is not executed; selected source is a precondition'),
        up_order=up_order, down_order=down_order,
                             calls=list(calls), enabled=enabled, disabled=dict(regs))
    print(profile, dict(Counter(c['op'] for c in calls)), 'module callbacks and modeled scheduling completed')

result = dict(
    binary=str(KO), binary_sha256=hashlib.sha256(data).hexdigest(),
    dapm_core_source=str(core_path), dapm_core_sha256=hashlib.sha256(core_path.read_bytes()).hexdigest(),
    profile_assumptions=dict(hp_mux=2, hifi=0, rate=48000, gain=31,
                             trim_sentinels=dict(left=3, right=7, fine_left=1, fine_right=2)),
    limitations=['Synthetic regmap and trim values; no hardware execution',
                 'Callbacks are actual supplied module instructions; DAPM scheduling is a source-based model',
                 'Regulator and MT6685 DCXO calls are external boundaries, not executed drivers',
                 'Control-put writes are outside this trace; DAC source-mux selection is a precondition',
                 'Only call/jump relocations applied; logging ignored; PAC stripped',
                 'GPIO SET/CLR aliases recorded as bus calls; their side effects are not modeled',
                 'Within equal sort/subseq/reg, declaration order is assumed; actual dirty-list tie ordering unobserved',
                 'No error injection or production-driver equivalence established by this trace',
                 'Readback of ordinary bytes is a model; it cannot prove silicon writable masks'],
    profiles=profiles)
(OUT / 'supply-callback-trace.json').write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n')
