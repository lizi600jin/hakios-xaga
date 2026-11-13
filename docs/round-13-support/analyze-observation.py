#!/usr/bin/env python3
"""Validate and compare two saved MCU/SPM/PDC snapshots; no device I/O."""
from pathlib import Path
import argparse
import hashlib
import json
import re


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2, ensure_ascii=False) + '\n')


def analyze(directory, trial):
    raw = (directory / f'trial-{trial}-logbook.bin').read_bytes()
    assert len(raw) == 4198400
    phases = ['before the start request', 'after AUTO (before DISABLE)']
    rows = [match[1].decode('utf-8', 'replace') for match in
            re.finditer(rb'\[\s*[0-9]+\.[0-9]+\]: (mali:[^\x00\n]{0,800})', raw)]
    tags = {int(match[1]) for line in rows
            if (match := re.search(r'\btrial (\d+)(?:\s|:)', line))}
    assert tags == {trial}
    snapshots, full_blocks, raw_blocks = [], [], []

    def one(prefix):
        matches = [line for line in rows if line.startswith(prefix)]
        assert len(matches) == 1, (prefix, len(matches))
        return matches[0]

    for phase in phases:
        marker = f'mali: MCU window {phase}:'.encode()
        assert raw.count(marker) == 1
        start = raw.index(marker)
        stop = raw.index(f'mali: spm raw trial {trial} {phase}:'.encode(), start)
        block = raw[start:stop].decode('utf-8', 'replace')
        raw_blocks.append(block)
        pairs = [(int(offset, 16), int(value, 0)) for offset, value in
                 re.findall(r'\b0x(30[0-9a-f]{2})=(0x[0-9a-f]+|0)(?![0-9a-f])', block)]
        assert len(pairs) == 64
        window = dict(pairs)
        assert len(window) == 64 and set(window) == set(range(0x3000, 0x3100, 4))
        full_blocks.append(f'MCU window {phase}:\n' + '\n'.join(
            '  ' + ' '.join(f'{offset:#x}={window[offset]:#x}' for offset in range(base, base + 16, 4))
            for base in range(0x3000, 0x3100, 16)) + '\n')
        spm_line = one(f'mali: spm raw trial {trial} {phase}:')
        spm = {name: int(value, 0) for name, value in
               re.findall(r'\b(F30|F34|F38|F3C|F40) (0x[0-9a-f]+|0)\b', spm_line)}
        assert set(spm) == {'F30', 'F34', 'F38', 'F3C', 'F40'}
        bits_line = one(f'mali: spm F3C bits trial {trial} {phase}:')
        bits = {int(bit): int(value) for bit, value in re.findall(r'\bb(\d+)=(0|1)\b', bits_line)}
        assert set(bits) == set(range(32))
        assert sum(value << bit for bit, value in bits.items()) == spm['F3C']
        sc_raw_line = one(f'mali: pdc raw trial {trial} {phase}:')
        sc_enable_line = one(f'mali: pdc readback: trial {trial} {phase}:')
        sc_raw = {int(index): int(value, 0) for index, value in
                  re.findall(r'\bsc(\d+)=(0x[0-9a-f]+|0)\b', sc_raw_line)}
        sc_enable = {int(index): int(value) for index, value in
                     re.findall(r'\bsc(\d+)=(0|1)\b', sc_enable_line)}
        assert set(sc_raw) == set(sc_enable) == set(range(10))
        assert sc_enable == {index: value & 1 for index, value in sc_raw.items()}
        group_line = one(f'mali: pdc groups trial {trial} {phase}:')
        groups = {name: int(value, 0) for name, value in
                  re.findall(r'\b(cg|st[012456]) (0x[0-9a-f]+|0)\b', group_line)}
        assert set(groups) == {'cg', 'st0', 'st1', 'st2', 'st4', 'st5', 'st6'}
        snapshots.append({'phase': phase, 'raw_window_start_offset': start,
                          'raw_window_end_offset': stop,
                          'interleaved_process_message': 'process lsd' in block,
                          'window': {hex(k): hex(v) for k, v in window.items()},
                          'spm': {k: hex(v) for k, v in spm.items()},
                          'F3C_set_bits': [bit for bit in range(32) if bits[bit]],
                          'F3C_MFG_1_12_masked': hex(spm['F3C'] & 0x3ffc),
                          'F3C_MFG_0_12_masked': hex(spm['F3C'] & 0x3ffe),
                          'F3C_domain_status': {f'mfg{domain}': bits[domain + 1] for domain in range(13)},
                          'F3C_outside_MFG_0_12_mask': hex(spm['F3C'] & ~0x3ffe),
                          'sc_raw': {f'sc{k}': hex(v) for k, v in sc_raw.items()},
                          'sc_enable': {f'sc{k}': v for k, v in sc_enable.items()},
                          'groups_raw': {k: hex(v) for k, v in groups.items()}})

    before, after = snapshots
    differences = [{'offset': hex(offset), 'before': before['window'][hex(offset)],
                    'after': after['window'][hex(offset)]}
                   for offset in range(0x3000, 0x3100, 4)
                   if before['window'][hex(offset)] != after['window'][hex(offset)]]
    result = {'trial': trial, 'logbook_sha256': hashlib.sha256(raw).hexdigest(),
              'parsed_entire_window': True, 'snapshot_count': len(snapshots),
              'words_per_snapshot': 64, 'snapshots': snapshots, 'window_differences': differences,
              'mapping_note': 'F3C bits 1..13 are MFG0..12 under vendor masks. SC enable words configure shader-core power control; no one-to-one SC index to MFG-domain mapping is established here.'}
    (directory / f'trial-{trial}-mcu.txt').write_text('\n'.join(full_blocks))
    (directory / f'trial-{trial}-mcu-rawblocks.txt').write_text('\n'.join(raw_blocks))
    write_json(directory / f'trial-{trial}-observations.json', result)
    (directory / f'trial-{trial}-mcu-window.tsv').write_text('offset\tbefore_AUTO\tafter_AUTO\tchanged\n' + ''.join(
        f'{offset:#x}\t{before["window"][hex(offset)]}\t{after["window"][hex(offset)]}\t'
        f'{int(before["window"][hex(offset)] != after["window"][hex(offset)])}\n'
        for offset in range(0x3000, 0x3100, 4)))
    (directory / f'trial-{trial}-F3C-bits.tsv').write_text('bit\tunder_vendor_MFG_mask\tbefore_AUTO\tafter_AUTO\n' + ''.join(
        f'{bit}\t{f"mfg{bit - 1}" if 1 <= bit <= 13 else "outside_mask"}\t'
        f'{(int(before["spm"]["F3C"], 0) >> bit) & 1}\t{(int(after["spm"]["F3C"], 0) >> bit) & 1}\n'
        for bit in range(32)))
    (directory / f'trial-{trial}-PDC-SC.tsv').write_text('SC\tphysical_address\traw_before\ten_before\traw_after\ten_after\n' + ''.join(
        f'sc{index}\t{0x13fbf400 + index * 0x18:#x}\t{before["sc_raw"][f"sc{index}"]}\t'
        f'{before["sc_enable"][f"sc{index}"]}\t{after["sc_raw"][f"sc{index}"]}\t'
        f'{after["sc_enable"][f"sc{index}"]}\n' for index in range(10)))
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('trial', type=int)
    parser.add_argument('--input-dir', type=Path, default=Path(__file__).resolve().parent)
    args = parser.parse_args()
    print(json.dumps(analyze(args.input_dir, args.trial), indent=2, ensure_ascii=False))
