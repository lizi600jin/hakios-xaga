#!/usr/bin/env python3
"""Offline arithmetic audit; reads local files only, never accesses a device.

This models the current C builder. It does NOT execute that builder or prove
that a GPU can read the resulting physical addresses.
"""
import hashlib
import json
from pathlib import Path
import re
import struct

OUT = Path(__file__).resolve().parent
DRIVER = Path('/home/lizi/IOS/hakios-mtk/mtk/mt6895/drivers/gpu/hakios.mali610.c')
HEADER = Path('/home/lizi/IOS/hakios-mtk/drivers/gpu/hakios.mali610_fw.h')
KROOT = Path('/home/lizi/IOS/work-xaga/k-xaga-16.2/drivers/gpu/mediatek/mali-valhall/gpu/arm/midgard')
REFS = [Path('/tmp/src-ref/panthor_panthor_fw.c'),
        KROOT / 'csf/mali_kbase_csf_firmware.c',
        KROOT / 'csf/mali_kbase_csf_trace_buffer.c',
        KROOT / 'mmu/mali_kbase_mmu_mode_aarch64.c',
        KROOT / 'mmu/mali_kbase_mmu_hw_direct.c',
        KROOT / 'mmu/backend/mali_kbase_mmu_csf.c',
        KROOT / 'include/uapi/gpu/arm/midgard/gpu/mali_kbase_gpu_regmap.h']
EVIDENCE = Path('/home/lizi/IOS/work-xaga/codex/out/round-6-support')


def fingerprint(path):
    data = path.read_bytes()
    return {'path': str(path), 'bytes': len(data),
            'sha256': hashlib.sha256(data).hexdigest()}


source = DRIVER.read_text()
assert hashlib.sha256(DRIVER.read_bytes()).hexdigest() == 'd37bb795b0e996bfe787e85ab5118d092f640ac0779ce00c30ee19557c0e24d6', 'Source changed; update the arithmetic model before reusing this audit.'
assert re.search(r'#define\s+PT_LEAF_GRANULE\s+0x1000ull', source)
assert 'va += PT_LEAF_GRANULE' in source
assert '(va >> (39 - 9 * level)) & 0x1ffu' in source
assert 'table[(va >> 12) & 0x1ffu] = (pa & PTE_ADDRESS) | leaf;' in source
body = HEADER.read_text().split('= {', 1)[1].split('};', 1)[0]
fw = bytes(int(x, 16) for x in re.findall(r'0x([0-9a-fA-F]{2})\b', body))
assert len(fw) == 258048
assert hashlib.sha256(fw).hexdigest() == 'de22d2fc06bc8a859968499a6e609c27fe838be062bf5c97d92ea9f904f7229a'
assert struct.unpack_from('<I', fw)[0] == 0xc3f13a6e
end = struct.unpack_from('<I', fw, 16)[0]
offset, sections, traces, entries = 20, [], [], []
while offset < end:
    head = struct.unpack_from('<I', fw, offset)[0]
    kind, size = head & 255, (head >> 8) & 255
    assert size >= 4 and size % 4 == 0 and offset + size <= end
    data = fw[offset + 4:offset + size]
    entries.append({'offset': hex(offset), 'type': kind, 'size': size,
                    'optional': bool(head & (1 << 31))})
    if kind == 0:
        flags, lo, hi, ds, de = struct.unpack_from('<5I', data)
        assert lo % 4096 == hi % 4096 == 0
        assert 0 <= de - ds <= hi - lo and de <= len(fw)
        sections.append({'flags': flags, 'lo': lo, 'hi': hi,
                         'data_start': ds, 'data_end': de,
                         'name': data[20:].split(b'\0')[0].decode()})
    elif kind == 3:
        words = struct.unpack_from('<7I', data)
        traces.append({'name': data[28:].split(b'\0')[0].decode(),
                       'type': words[0], 'size_va': words[1],
                       'insert_va': words[2], 'extract_va': words[3],
                       'data_va': words[4], 'enable_va': words[5],
                       'enable_count': words[6]})
    offset += size
assert offset == end
for i, a in enumerate(sections):
    for b in sections[i + 1:]:
        assert max(a['lo'], b['lo']) >= min(a['hi'], b['hi'])

# Reproduce low allocator ordering: host allocations are 16K, GPU leaves 4K.
host_page, granule, mask = 16384, 4096, 0x0000fffffffff000
next_pa, tables = 0x50000000, {}


def alloc(size):
    global next_pa
    pa = (next_pa + host_page - 1) // host_page * host_page
    next_pa = pa + (size + host_page - 1) // host_page * host_page
    return pa


def new_table():
    pa = alloc(host_page)
    tables[pa] = [0] * 512
    return pa


def indices(va):
    return [(va >> shift) & 511 for shift in (39, 30, 21, 12)]


root = new_table()
for sec in sections:
    sec['pa'] = alloc(sec['hi'] - sec['lo'])
    cached = (sec['flags'] & 24) == 8  # baseline fwnc=fwx=0
    attrs = 3 | (1 << 10) | (1 << 11) | (1 << 6)
    attrs |= (3 if cached else 2) << 8
    attrs |= (0 if cached else 5) << 2
    if not sec['flags'] & 2:
        attrs |= 3 << 6
    if not sec['flags'] & 4:
        attrs |= 1 << 54
    sec['attrs'] = attrs
    for va in range(sec['lo'], sec['hi'], granule):
        table = tables[root]
        idxs = indices(va)
        for idx in idxs[:3]:
            if not table[idx] & 1:
                table[idx] = new_table() | 3 | (1 << 10)
            table = tables[table[idx] & mask]
        assert table[idxs[3]] == 0  # no alias/overwritten leaf
        table[idxs[3]] = (sec['pa'] + va - sec['lo']) | attrs


def walk(va):
    table, path = tables[root], []
    for level, idx in enumerate(indices(va)):
        entry = table[idx]
        path.append(entry)
        assert entry & 3 == 3 and entry & (1 << 10)
        if level < 3:
            table = tables[entry & mask]
    return path


coverage, shared_leaves = [], []
for number, sec in enumerate(sections):
    count = 0
    # Count independently by division; inspect every byte-range's first/last VA.
    expected = (sec['hi'] - sec['lo']) // 4096
    for page in range(expected):
        va = sec['lo'] + page * 4096
        pte = walk(va)[-1]
        physical = pte & mask
        assert physical == sec['pa'] + page * 4096
        assert (pte & ~mask) == sec['attrs']
        assert ((physical + 4095) == sec['pa'] + page * 4096 + 4095)
        count += 1
        if sec['lo'] == 0x04000000:
            shared_leaves.append((page, va, *indices(va), physical, pte))
    assert count == expected
    coverage.append({'section': number, 'va_start': hex(sec['lo']),
                     'va_end_exclusive': hex(sec['hi']), 'leaves': count,
                     'pa_start_model': hex(sec['pa']),
                     'first_leaf': hex(walk(sec['lo'])[-1]),
                     'last_leaf': hex(walk(sec['hi'] - 4096)[-1]),
                     'file_bytes': sec['data_end'] - sec['data_start'],
                     'zero_tail_bytes': sec['hi'] - sec['lo'] -
                     (sec['data_end'] - sec['data_start'])})
assert len(shared_leaves) == 148
assert sum(s['leaves'] for s in coverage) == 357

shared = next(s for s in sections if s['lo'] == 0x04000000)
shared_data = (fw[shared['data_start']:shared['data_end']] +
               bytes(shared['hi'] - shared['lo'] -
                     (shared['data_end'] - shared['data_start'])))
glb = struct.unpack_from('<8I', shared_data)
csg = struct.unpack_from('<7I', shared_data, 4096)
assert glb[2:4] == (0x04002000, 0x04003000)
assert csg[1:3] == (0x04004000, 0x0400c000)
assert not any(shared_data[8192:])
for trace in traces:
    trace['initial_words'] = {}
    for field in ('size_va', 'insert_va', 'extract_va', 'data_va', 'enable_va'):
        va = trace[field]
        sec = next(s for s in sections if s['lo'] <= va < s['hi'])
        rel = va - sec['lo']
        data = fw[sec['data_start']:sec['data_end']]
        assert rel + 4 <= len(data)
        trace['initial_words'][field] = hex(struct.unpack_from('<I', data, rel)[0])

logs = []
select = re.compile(r'mali: (?:walk |coverage:|COVERAGE|AS0 |mmu irq |glb req |'
                    r'spin |sentinel|version word|the start loop|the global interface|'
                    r'trial |witness:|shared probe|CSF interface|the MCU is up)')
for name in ('trial-608-logbook.bin', 'trial-613-logbook.bin',
             'trial-660-logbook.bin', 'trial-662-logbook.bin',
             'trial-663-logbook.bin'):
    p = EVIDENCE / name
    text = p.read_bytes().decode('utf-8', 'replace')  # whole file, no header len
    selected = [line for line in text.splitlines() if select.search(line)]
    (OUT / name.replace('-logbook.bin', '-key.txt')).write_text('\n'.join(selected) + '\n')
    logs.append(fingerprint(p))
    if name == 'trial-663-logbook.bin':
        # Compare model descriptor paths with observed CPU walks from this run.
        checked = []
        for match in re.finditer(r'mali: walk (0x[0-9a-f]+|0): L0 (0x[0-9a-f]+) '
                                 r'L1 (0x[0-9a-f]+) L2 (0x[0-9a-f]+) '
                                 r'L3 (0x[0-9a-f]+)', text):
            va, *path = [int(x, 0) for x in match.groups()]
            assert walk(va) == path
            checked.append(hex(va))
        assert len(checked) == 4

with (OUT / 'shared-leaves.tsv').open('w') as f:
    f.write('leaf\tva\tl0\tl1\tl2\tl3\tpa_model\tpte_model\n')
    for page, va, i0, i1, i2, i3, pa, pte in shared_leaves:
        f.write(f'{page}\t{va:#010x}\t{i0}\t{i1}\t{i2}\t{i3}\t{pa:#x}\t{pte:#x}\n')

report = {'scope': 'offline model and existing logs only; no hardware access',
          'sources': [fingerprint(p) for p in [DRIVER, HEADER, *REFS]],
          'firmware_binary_sha256': hashlib.sha256(fw).hexdigest(),
          'host_page_bytes': host_page, 'gpu_leaf_bytes': granule,
          'table_count': len(tables), 'leaves_total': 357,
          'shared_leaves': len(shared_leaves), 'sections': coverage,
          'shared_glb_words': [hex(x) for x in glb],
          'shared_csg0_words': [hex(x) for x in csg],
          'shared_zero_tail_from': '0x04002000',
          'entries': entries, 'trace_entries': traces,
          'trial663_sample_walks_match_model': checked,
          'logs': logs}
(OUT / 'audit.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps({'sections': len(sections), 'tables': len(tables),
                  'leaves': 357, 'shared_leaves': len(shared_leaves),
                  'sample_walks_match': checked,
                  'output': str(OUT)}, indent=2))
