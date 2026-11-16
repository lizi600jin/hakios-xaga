#!/usr/bin/env python3
"""Read the supplied AArch64 ET_REL codec; do not load it or contact a device.

The layout is specific to this module, checked against the complete source
widget-name and route inventories. Results are software dependencies, not
measurements of power, clock waveforms, or audio.
"""
import hashlib
import json
from pathlib import Path
import re
import struct

ROOT = Path('/home/lizi/IOS/repo/hakios-mt6895/docs/headset-wip')
KO = ROOT / 'ko/snd-soc-mt6368.ko'
SOURCE = ROOT / 'raw/mt6368.c'
OUT = Path(__file__).resolve().parent
blob = KO.read_bytes()
h = struct.unpack_from('<16sHHIQQQIHHHHHH', blob)
assert h[0][:6] == b'\x7fELF\x02\x01' and h[1:3] == (1, 183)
sections = [struct.unpack_from('<IIQQQQIIQQ', blob, h[6] + i * h[11])
            for i in range(h[12])]


def section(index):
    s = sections[index]
    return blob[s[4]:s[4] + s[5]]


def cstr(buf, offset):
    assert 0 <= offset < len(buf)
    return buf[offset:buf.index(0, offset)].decode()


section_names = section(h[13])
indices = {cstr(section_names, s[0]): i for i, s in enumerate(sections)}
symindex = indices['.symtab']
symstrings = section(sections[symindex][6])
symbols = []
for at in range(0, len(section(symindex)), 24):
    name, info, other, index, value, size = struct.unpack_from(
        '<IBBHQQ', section(symindex), at)
    symbols.append(dict(name=cstr(symstrings, name), section=index,
                        value=value, size=size))
byname = {s['name']: s for s in symbols if s['name']}
roindex = indices['.rodata']
rodata = section(roindex)
relocations = {}
for i, s in enumerate(sections):
    if s[1] != 4 or s[7] != roindex:
        continue
    for at in range(0, len(section(i)), 24):
        offset, info, addend = struct.unpack_from('<QQq', section(i), at)
        relocations[offset] = dict(kind=info & 0xffffffff,
                                   symbol=symbols[info >> 32], addend=addend)


def pointer(at, string=False):
    r = relocations.get(at)
    if r is None:
        assert struct.unpack_from('<Q', rodata, at)[0] == 0, hex(at)
        return None
    assert r['kind'] == 257, r  # R_AARCH64_ABS64
    s = r['symbol']
    target = s['value'] + r['addend']
    if string:
        assert s['section'] == roindex
        return cstr(rodata, target)
    matches = [x['name'] for x in symbols
               if x['section'] == s['section'] and x['value'] == target
               and x['name'] and not x['name'].startswith('$')]
    return dict(section=s['section'], offset=target, symbols=matches)


widget_symbol = byname['mt6368_dapm_widgets']
route_symbol = byname['mt6368_dapm_routes']
assert widget_symbol['section'] == route_symbol['section'] == roindex
assert widget_symbol['size'] % 320 == route_symbol['size'] % 96 == 0
widgets = []
for offset in range(0, widget_symbol['size'], 320):
    at = widget_symbol['value'] + offset
    widgets.append(dict(
        name=pointer(at + 8, string=True), stream=pointer(at + 16, string=True),
        module_offset=at, type=struct.unpack_from('<I', rodata, at)[0],
        reg=struct.unpack_from('<i', rodata, at + 72)[0], shift=rodata[at + 76],
        mask=struct.unpack_from('<I', rodata, at + 80)[0],
        on=struct.unpack_from('<I', rodata, at + 84)[0],
        off=struct.unpack_from('<I', rodata, at + 88)[0],
        subseq=struct.unpack_from('<i', rodata, at + 96)[0],
        event_flags=struct.unpack_from('<H', rodata, at + 112)[0],
        event=pointer(at + 120)))
routes = []
for offset in range(0, route_symbol['size'], 96):
    at = route_symbol['value'] + offset
    routes.append(dict(sink=pointer(at, string=True),
                       control=pointer(at + 8, string=True),
                       source=pointer(at + 16, string=True),
                       connected=pointer(at + 24), module_offset=at))

source = SOURCE.read_text()
widget_source = source.split('mt6368_dapm_widgets[] = {', 1)[1].split('\n};', 1)[0]
route_source = source.split('mt6368_dapm_routes[] = {', 1)[1].split('\n};', 1)[0]
source_names = re.findall(r'SND_SOC_DAPM_[A-Z0-9_]+\s*\(\s*"([^"]+)"', widget_source)
source_routes = [(a, None if b == 'NULL' else b[1:-1], c) for a, b, c in
                 re.findall(r'\{\s*"([^"]+)",\s*(NULL|"[^"]+"),\s*"([^"]+)"', route_source)]
assert source_names == [w['name'] for w in widgets], 'source/module widget mismatch'
assert source_routes == [(r['sink'], r['control'], r['source']) for r in routes], 'source/module route mismatch'
bywidget = {w['name']: w for w in widgets}


def closure(sgen=False):
    """Selected codec subgraph; stream activity and external machine links are assumed."""
    muxes = {'DAC In Mux': 'Sgen' if sgen else 'Normal Path',
             'HPL Mux': 'Audio Playback', 'HPR Mux': 'Audio Playback'}
    seen = set()
    pending = ['Headphone L', 'Headphone R']
    selected = []
    while pending:
        sink = pending.pop()
        if sink in seen:
            continue
        seen.add(sink)
        for r in routes:
            if r['sink'] != sink:
                continue
            if r['control'] and muxes.get(sink) != r['control']:
                continue
            # Normal Path has both AIF inputs in the static graph. This profile
            # selects AIF1, with AIF2 playback inactive; this is not runtime proof.
            if not sgen and sink == 'DAC In Mux' and r['source'] == 'AIF2_RX':
                continue
            assert r['connected'] is None, 'conditional supply requires extra evaluation'
            selected.append(r)
            pending.append(r['source'])
    return dict(assumptions=muxes,
                widgets=[bywidget[n] for n in sorted(seen)], routes=selected,
                has_clk_buf='CLK_BUF' in seen)


result = dict(
    source=dict(binary=str(KO), sha256=hashlib.sha256(blob).hexdigest(),
                c=str(SOURCE), c_sha256=hashlib.sha256(SOURCE.read_bytes()).hexdigest()),
    layout=dict(widget_stride=320, route_stride=96, widget_count=len(widgets),
                route_count=len(routes), complete_source_inventory_matches=True),
    limitations=['No hardware access or target code execution',
                 'Codec static graph only; machine graph and actual stream state are unobserved',
                 'Dependency edges do not prove electrical necessity',
                 'Supply subseq is sorted within widget-type ordering, not a global recipe'],
    widgets=widgets, routes=routes, normal_aif1_hp=closure(), internal_sgen_hp=closure(True))
(OUT / 'codec-dapm.json').write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n')
for profile in ('normal_aif1_hp', 'internal_sgen_hp'):
    selected = result[profile]
    print(profile, 'widgets', len(selected['widgets']), 'CLK_BUF', selected['has_clk_buf'])
    print(' -> '.join(sorted(w['name'] for w in selected['widgets'])))
print('inventory', result['layout'])
