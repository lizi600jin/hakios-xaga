#!/usr/bin/env python3
"""Read both reference kernels; classify every differing byte without editing them."""
import hashlib
import json
from pathlib import Path
import plistlib
import struct

ROOT = Path('/home/lizi/IOS/hakios-mtk/logs/build-reference-710-20261116-135551')
OUT = Path(__file__).resolve().parent
paths = [ROOT / n for n in ('709-old-kernel', '710-new-kernel')]
old, new = [p.read_bytes() for p in paths]
assert len(old) == len(new)


def parse(blob):
    magic, cpu, sub, kind, ncmds, cmdbytes, flags, reserved = struct.unpack_from('<8I', blob)
    assert magic == 0xfeedfacf
    at = 32
    segments, sections, uuid = [], [], None
    for _ in range(ncmds):
        cmd, size = struct.unpack_from('<II', blob, at)
        assert size >= 8 and at + size <= 32 + cmdbytes
        if cmd == 0x19:
            name, va, vs, fo, fs, mp, ip, ns, fl = struct.unpack_from('<16sQQQQiiII', blob, at + 8)
            segment = name.rstrip(b'\0').decode()
            segments.append(dict(name=segment, file_offset=fo, file_size=fs, address=va))
            for i in range(ns):
                sect, seg, addr, n, off, align, reloff, nreloc, f, r1, r2, r3 = struct.unpack_from(
                    '<16s16sQQ8I', blob, at + 72 + i * 80)
                sections.append(dict(segment=segment, name=sect.rstrip(b'\0').decode(),
                                     address=addr, file_offset=off, size=n, flags=f))
        elif cmd == 0x1b:
            assert size == 24
            uuid = (at + 8, blob[at + 8:at + 24].hex())
        at += size
    assert at == 32 + cmdbytes
    return segments, sections, uuid


segments, sections, old_uuid = parse(old)
new_segments, new_sections, new_uuid = parse(new)
assert segments == new_segments and sections == new_sections
different = {i for i, (a, b) in enumerate(zip(old, new)) if a != b}
remaining = set(different)
classified = []
for kind, start, end in [('LC_UUID', old_uuid[0], old_uuid[0] + 16)]:
    changed = remaining.intersection(range(start, end))
    remaining.difference_update(changed)
    classified.append(dict(kind=kind, different_bytes=len(changed), file_offset=start))
for s in sections:
    if s['flags'] & 255 in (1, 12) or not s['size']:
        continue
    start, end = s['file_offset'], s['file_offset'] + s['size']
    changed = {i for i in remaining if start <= i < end}
    if not changed:
        continue
    remaining.difference_update(changed)
    classified.append(dict(kind=s['segment'] + '.' + s['name'], different_bytes=len(changed),
                           file_offset=start, size=s['size'], first_difference=min(changed)))
assert not remaining, 'unclassified byte differences'
symbol_section = next(s for s in sections if (s['segment'], s['name']) == ('__LINKINFO', '__symbolsets'))
at, size = symbol_section['file_offset'], symbol_section['size']
plists = [plistlib.loads(b[at:at + size]) for b in (old, new)]


def normalize_symbols(plist):
    result = {k: v for k, v in plist.items() if k != 'SymbolsSets'}
    sets = {}
    for entry in plist['SymbolsSets']:
        item = dict(entry)
        symbols = item.pop('Symbols')
        identifier = item['CFBundleIdentifier']
        assert identifier not in sets
        # Compare complete symbol records as a multiset, preserving duplicates.
        item['Symbol_records_as_multiset'] = sorted(json.dumps(s, sort_keys=True) for s in symbols)
        sets[identifier] = item
    result['SymbolsSets_by_identifier'] = sets
    return result


normalized = [normalize_symbols(p) for p in plists]
assert normalized[0] == normalized[1]
canonical = json.dumps(normalized[0], sort_keys=True, separators=(',', ':')).encode()
text = next(x for x in classified if x['kind'] == '__TEXT.__const')
at = text['first_difference']
start = old.rfind(b'\0', 0, at) + 1
end = old.find(b'\0', at)
assert end - start < 1024
report = dict(
    inputs=[dict(path=str(p), sha256=hashlib.sha256(b).hexdigest(), bytes=len(b))
            for p, b in zip(paths, (old, new))],
    different_bytes=len(different), classifications=classified,
    unclassified_differences=len(remaining), segments=segments,
    uuid=dict(old=old_uuid[1], new=new_uuid[1], generation_algorithm_not_inferred=True),
    changed_const_string=dict(file_offset=start, old=old[start:end].decode(), new=new[start:end].decode()),
    symbolsets=dict(section=symbol_section, normalized_records_equal=True,
                    normalized_sha256=hashlib.sha256(canonical).hexdigest(),
                    counts={p['CFBundleIdentifier']: len(p['Symbols']) for p in plists[0]['SymbolsSets']}),
    limitations=['No binaries altered or UUID/timestamps normalized in the input artifacts',
                 'Record multiset comparison ignores symbol array order, retaining all record fields and duplicates',
                 'Same executable and initialized-data bytes are not proof of historical source identity',
                 'No runtime equivalence or physical-device test is established here'])
(OUT / 'reference-kernel-diff.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n')
print(json.dumps({k: report[k] for k in ('different_bytes', 'classifications', 'unclassified_differences', 'changed_const_string', 'symbolsets')}, ensure_ascii=False, indent=2))
