#!/usr/bin/env python3
"""Analyze complete saved logbooks; no device commands."""
from pathlib import Path
import hashlib, json, re

out = Path(__file__).resolve().parent
summary = []
trials = sorted(int(m[1]) for p in out.glob('trial-*-logbook.bin')
                if (m := re.fullmatch(r'trial-(\d+)-logbook.bin', p.name)))
for trial in trials:
    path = out / f'trial-{trial}-logbook.bin'
    if not path.exists():
        continue
    raw = path.read_bytes()
    rows = [(float(m[1]), m[2].decode('utf-8', 'replace'), m.start())
            for m in re.finditer(rb'\[\s*([0-9]+\.[0-9]+)\]: (mali:[^\x00\n]{0,800})', raw)]
    assert any(f'trial {trial} ' in s or f'trial {trial}:' in s for _, s, _ in rows)
    # A shorter save can leave the previous trial's suffix in the reserved slot.
    # Keep the conservative prefix before the first page containing a foreign
    # marker after the current marker. Preserve the complete raw file separately.
    seen_current, cutoff, foreign = False, None, None
    for _, s, off in rows:
        tag = re.search(r'\btrial (\d+)(?:\s|:)', s)
        if tag and int(tag[1]) == trial:
            seen_current = True
        elif tag and seen_current:
            cutoff = off // 4096 * 4096
            foreign = int(tag[1])
            break
    if cutoff is not None:
        rows = [row for row in rows if row[2] < cutoff]
    (out / f'trial-{trial}-mali.txt').write_text(''.join(f'{off:08x} {t:.6f} {s}\n' for t, s, off in rows))
    writes = []
    for _, s, _ in rows:
        m = re.match(r'mali: write\[(\d+)\] (0x[0-9a-f]+)=(0x[0-9a-f]+|0)\b', s)
        if m:
            writes.append((int(m[1]), m[2], int(m[3], 0)))
    (out / f'trial-{trial}-writes.tsv').write_text('record\toffset\tvalue\n' +
        ''.join(f'{i}\t{off}\t{value:#x}\n' for i, off, value in writes))
    # Preserve MCU-window continuation lines from the full body too.
    text = raw.decode('utf-8', 'replace')
    blocks = re.findall(r'MCU window[^\n]*\n(?: *0x[0-9a-f]+=[^\n]*\n)*', text)
    (out / f'trial-{trial}-mcu.txt').write_text('\n'.join(blocks))
    def first(needle):
        return next(({'time': t, 'line': s} for t, s, _ in rows if needle in s), None)
    item = {'trial': trial, 'logbook_bytes': len(raw),
            'logbook_sha256': hashlib.sha256(raw).hexdigest(),
            'conservative_prefix_end': cutoff, 'foreign_suffix_trial': foreign,
            'mali_rows': len(rows), 'stores': len(writes),
            'coverage': first('coverage:'), 'l2': first('L2 up:'),
            'as0_update': first('AS0 step update'),
            'as0_probe': first('round7 AS0 after UPDATE'),
            'version': first('mali: version word '),
            'glb': first('round7 GLB:'), 'csg0': first('round7 CSG0:'),
            'boot_failed': first('the global interface never raised'),
            'witness_entry': first('reset vector now'),
            'witness_reads': first('witness: read1'),
            'witness_verdict': first('witness: verdict'),
            'failed_tail_return': first('completed; returning before global_interface'),
            'vendor_leaves': [s for _, s, _ in rows if 'mali: vendor leaf ' in s],
            'parked_as': [s for _, s, _ in rows if re.match(r'mali: AS[2-7] parked', s)]}
    if item['l2'] and item['as0_update']:
        item['l2_precedes_as0_update'] = item['l2']['time'] < item['as0_update']['time']
    (out / f'trial-{trial}-parse.json').write_text(json.dumps(item, indent=2) + '\n')
    summary.append(item)
(out / 'comparison.json').write_text(json.dumps(summary, indent=2) + '\n')
print(json.dumps([{'trial': x['trial'], 'stores': x['stores'],
                   'l2_precedes_as0': x.get('l2_precedes_as0_update'),
                   'version': x['version'], 'witness': x['witness_reads']}
                  for x in summary], indent=2))
