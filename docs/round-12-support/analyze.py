#!/usr/bin/env python3
"""Parse complete saved logbooks and compare GPU stores. No device access."""
from pathlib import Path
import argparse
import csv
import difflib
import hashlib
import json
import re


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2, ensure_ascii=False) + '\n')


def analyze(out, trial):
    raw = (out / f'trial-{trial}-logbook.bin').read_bytes()
    assert len(raw) == 4198400
    rows = [(float(m[1]), m[2].decode('utf-8', 'replace'), m.start())
            for m in re.finditer(rb'\[\s*([0-9]+\.[0-9]+)\]: (mali:[^\x00\n]{0,800})', raw)]
    tags = sorted({int(tag[1]) for _, line, _ in rows
                   if (tag := re.search(r'\btrial (\d+)(?:\s|:)', line))})
    assert tags == [trial], f'Foreign or missing trial marker: {tags}'
    (out / f'trial-{trial}-mali.txt').write_text(''.join(
        f'{off:08x} {time:.6f} {line}\n' for time, line, off in rows))
    stores = []
    for _, line, _ in rows:
        match = re.fullmatch(r'mali: write\[(\d+)\] (0x[0-9a-f]+)=(0x[0-9a-f]+|0)', line)
        if match:
            stores.append((int(match[1]), int(match[2], 0), int(match[3], 0)))
    assert [index for index, _, _ in stores] == list(range(len(stores)))
    declared = [int(match[1]) for _, line, _ in rows
                if (match := re.search(r'write trace: (\d+) completed GPU stores', line))]
    assert declared == [len(stores)]
    (out / f'trial-{trial}-writes.tsv').write_text('record\toffset\tvalue\n' + ''.join(
        f'{index}\t{offset:#x}\t{value:#x}\n' for index, offset, value in stores))
    body = raw.decode('utf-8', 'replace')
    blocks = re.findall(r'MCU window[^\n]*\n(?: *0x[0-9a-f]+=[^\n]*\n)*', body)
    (out / f'trial-{trial}-mcu.txt').write_text('\n'.join(blocks))

    def first(needle):
        return next(({'time': time, 'line': line} for time, line, _ in rows if needle in line), None)

    result = {'trial': trial, 'logbook_bytes': len(raw),
              'logbook_sha256': hashlib.sha256(raw).hexdigest(),
              'parsed_entire_window': True, 'trial_markers': tags,
              'mali_rows': len(rows), 'stores': len(stores)}
    for field, needle in {
        'coverage': 'coverage:', 'l2': 'L2 up:',
        'hwissues': 'hwissues trial', 'protocol': 'protocol trial',
        'as0_update': 'AS0 step update', 'as0_probe': 'round7 AS0 after UPDATE',
        'before_auto': 'views before AUTO:', 'after_auto': 'after the start request:',
        'version': 'mali: version word ', 'glb': 'round7 GLB:',
        'witness_entry': 'reset vector now', 'witness_reads': 'witness: read1',
        'witness_verdict': 'witness: verdict',
        'failed_tail_return': 'completed; returning before global_interface'
    }.items():
        result[field] = first(needle)
    result['debug_top'] = [{'time': time, 'line': line} for time, line, _ in rows if 'debug_top' in line]
    result['spin_samples'] = [{'time': time, 'line': line} for time, line, _ in rows if 'mali: spin ' in line]
    result['l2_precedes_as0_update'] = (result['l2']['time'] < result['as0_update']['time'])
    write_json(out / f'trial-{trial}-parse.json', result)
    return result


def compare(out, baseline, trial):
    def load(number, normalize):
        with (out / f'trial-{number}-writes.tsv').open() as stream:
            rows = [(int(row['offset'], 0), int(row['value'], 0))
                    for row in csv.DictReader(stream, delimiter='\t')]
        if normalize:
            rows = [(offset, 0 if offset in (0x2400, 0x2404) else value) for offset, value in rows]
        return rows

    def delta(a, b):
        return [{'operation': op, 'a_index': [i, j], 'b_index': [k, l],
                 'a': [[hex(offset), hex(value)] for offset, value in a[i:j]],
                 'b': [[hex(offset), hex(value)] for offset, value in b[k:l]]}
                for op, i, j, k, l in difflib.SequenceMatcher(None, a, b, autojunk=False).get_opcodes()
                if op != 'equal']

    a, b = load(baseline, True), load(trial, True)
    result = {'a': baseline, 'b': trial,
              'normalization': 'Only AS0 TRANSTAB LO/HI (0x2400/0x2404) values set to 0 for allocator PAs',
              'a_stores': len(a), 'b_stores': len(b), 'differences': delta(a, b),
              'unnormalized_differences': delta(load(baseline, False), load(trial, False))}
    write_json(out / f'write-diff-{baseline}-{trial}.json', result)
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('trial', type=int)
    parser.add_argument('--input-dir', type=Path, default=Path(__file__).resolve().parent)
    parser.add_argument('--baseline', type=int, default=681)
    args = parser.parse_args()
    print(json.dumps({'result': analyze(args.input_dir, args.trial),
                      'write_diff': compare(args.input_dir, args.baseline, args.trial)},
                     indent=2, ensure_ascii=False))
