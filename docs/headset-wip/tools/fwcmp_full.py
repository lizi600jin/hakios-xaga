#!/usr/bin/env python3
"""Offline CSF firmware comparison: full entry parse, boundary checks, VA-aligned diff.

Reads the three firmware images, emits JSON + a human report. No device, no tree writes.
Bitfields taken from panthor_fw.c:96-97 — type = ehdr & 0xff, size = (ehdr >> 8) & 0xff.
"""
import hashlib, json, struct, sys

MAGIC = 0xc3f13a6e
HDR = struct.Struct('<5I')           # magic, minor/major/pad1, vhash, pad2, size

FLAG_NAMES = [(0x1,'RD'),(0x2,'WR'),(0x4,'EX'),(0x20,'PROT'),
              (0x40000000,'SHARED'),(0x80000000,'ZERO')]
CACHE_NAMES = {0:'CACHE_0', 1:'CACHE_1_UNCACHED_COH', 2:'CACHE_2_UNCACHED_COH',
               3:'CACHE_3_CACHED_COH'}

def flag_words(flags):
    names = [n for b, n in FLAG_NAMES if flags & b]
    cm = (flags >> 3) & 0x3
    names.append(CACHE_NAMES[cm])
    return names

def parse(path, label):
    d = open(path, 'rb').read()
    rec = {'label': label, 'path': path, 'bytes': len(d),
           'sha256': hashlib.sha256(d).hexdigest(), 'problems': [], 'entries': []}
    if len(d) < 20:
        rec['problems'].append('file shorter than the 20-byte header')
        return rec, d
    magic, word1, vhash, pad2, size = HDR.unpack_from(d, 0)
    rec['header'] = {'magic': '%#010x' % magic, 'magic_ok': magic == MAGIC,
                     'minor': d[4], 'major': d[5], 'padding1': word1 >> 16,
                     'version': '%d.%d' % (d[5], d[4]),
                     'version_hash': '%#010x' % vhash, 'padding2': pad2,
                     'entries_end': size}
    if magic != MAGIC:
        rec['problems'].append('bad magic %#x' % magic)
    if size > len(d):
        rec['problems'].append('entries_end %d beyond file (%d)' % (size, len(d)))

    off = 20
    idx = 0
    while off + 4 <= min(size, len(d)):
        ehdr = struct.unpack_from('<I', d, off)[0]
        typ = ehdr & 0xff
        esize = (ehdr >> 8) & 0xff
        if esize < 4:
            rec['problems'].append('entry %d at %#x: esize %d < 4, stop' % (idx, off, esize))
            break
        if off + esize > len(d):
            rec['problems'].append('entry %d at %#x: esize %d runs past file end' % (idx, off, esize))
            break
        ent = {'index': idx, 'file_offset': off, 'type': typ, 'esize': esize}
        if typ == 0:
            e = off + 4
            if esize < 24:
                rec['problems'].append('entry %d: IFACE esize %d < 24' % (idx, esize))
            else:
                flags, va_s, va_e, dat_s, dat_e = struct.unpack_from('<5I', d, e)
                name = d[e+20:off+esize].split(b'\0')[0].decode('ascii', 'replace')
                ent.update({'kind': 'IFACE', 'name': name, 'flags': '%#010x' % flags,
                            'flag_names': flag_words(flags),
                            'va_start': va_s, 'va_end': va_e,
                            'va_bytes': va_e - va_s,
                            'data_start': dat_s, 'data_end': dat_e,
                            'data_bytes': dat_e - dat_s})
                # boundary checks
                if va_e < va_s:
                    rec['problems'].append('entry %d: va_end %#x < va_start %#x' % (idx, va_e, va_s))
                if dat_e < dat_s:
                    rec['problems'].append('entry %d: data_end %#x < data_start %#x' % (idx, dat_e, dat_s))
                if dat_e > len(d):
                    rec['problems'].append('entry %d: data_end %#x beyond file (%d)' % (idx, dat_e, len(d)))
                if dat_e - dat_s > (va_e - va_s):
                    rec['problems'].append('entry %d: data %d B larger than va span %d B'
                                           % (idx, dat_e - dat_s, va_e - va_s))
                ent['first8'] = d[dat_s:dat_s+32].hex() if dat_e > dat_s else ''
        else:
            ent['kind'] = 'OTHER'
        rec['entries'].append(ent)
        off += esize
        idx += 1
    rec['entry_count'] = len(rec['entries'])
    rec['iface_count'] = sum(1 for e in rec['entries'] if e.get('kind') == 'IFACE')
    rec['other_count'] = rec['entry_count'] - rec['iface_count']
    if off != size:
        rec['problems'].append('parsed to %#x but entries_end says %#x' % (off, size))
    return rec, d

def vec_table(d, rec):
    """Read MSP / Reset / first 6 exception vectors from the IFACE that covers VA 0."""
    for e in rec['entries']:
        if e.get('kind') == 'IFACE' and e['va_start'] == 0:
            raw = d[e['data_start']:e['data_start']+32]
            if len(raw) < 32:
                return {'error': 'VA0 section has %d bytes of data, need 32' % len(raw)}
            vals = struct.unpack('<8I', raw)
            return {'msp': '%#010x' % vals[0], 'reset_pc': '%#010x' % vals[1],
                    'reset_pc_odd': bool(vals[1] & 1),
                    'reset_true': '%#010x' % (vals[1] & ~1),
                    'vectors': ['%#010x' % v for v in vals[2:7]],
                    'first_32_hex': raw.hex(),
                    'va0_section_bytes': e['data_bytes'],
                    'va0_span': '%#x-%#x' % (e['va_start'], e['va_end'])}
    return {'error': 'no IFACE section covering VA 0'}

def entry_bytes(d, rec, vec):
    """First bytes at reset_true, if we can map it back into an IFACE with data."""
    if 'reset_true' not in vec:
        return None
    pc = int(vec['reset_true'], 16)
    for e in rec['entries']:
        if e.get('kind') != 'IFACE' or e['data_bytes'] == 0:
            continue
        if e['va_start'] <= pc < e['va_end']:
            delta = pc - e['va_start']
            if delta + 32 > e['data_bytes']:
                return {'at': '%#x' % pc, 'error': 'reset pc is %d B into a %d B section'
                        % (delta, e['data_bytes'])}
            raw = d[e['data_start']+delta:e['data_start']+delta+32]
            return {'at': '%#x' % pc, 'section_va': '%#x-%#x' % (e['va_start'], e['va_end']),
                    'section_flags': e['flags'], 'ex_bit': bool(int(e['flags'], 16) & 0x4),
                    'first_32_hex': raw.hex(),
                    'as_u32': ['%#010x' % v for v in struct.unpack('<8I', raw)]}
    return {'at': '%#x' % pc, 'error': 'reset pc falls in no IFACE section with data'}

def main():
    specs = [('ours_0.1', '/tmp/our_fw.bin'),
             ('upstream_0.3', '/tmp/cmpfw/upstream/mali_csffw_arch10.8.bin'),
             ('xaga_vendor_0.3', '/tmp/repo-search/clones/mali_r54/firmware/mali_csffw.bin')]
    out = {'generated_from': 'panthor_fw.c:96-97 bitfields',
           'images': [], 'va_diff': {}, 'summary': {}}
    imgs = {}
    for label, path in specs:
        rec, d = parse(path, label)
        rec['vectors'] = vec_table(d, rec)
        rec['entry_point'] = entry_bytes(d, rec, rec['vectors'])
        imgs[label] = rec
        out['images'].append(rec)

    # VA-aligned diff: union of every IFACE va_start across images
    vas = sorted({e['va_start'] for r in imgs.values() for e in r['entries']
                  if e.get('kind') == 'IFACE'})
    for va in vas:
        row = {}
        for label, r in imgs.items():
            hit = next((e for e in r['entries']
                        if e.get('kind') == 'IFACE' and e['va_start'] == va), None)
            if hit:
                row[label] = {'present': True, 'va_end': hit['va_end'],
                              'va_bytes': hit['va_bytes'], 'flags': hit['flags'],
                              'flag_names': hit['flag_names'],
                              'data_bytes': hit['data_bytes'], 'name': hit['name']}
            else:
                row[label] = {'present': False}
        out['va_diff']['%#x' % va] = row

    out['summary'] = {
        'our_version': imgs['ours_0.1']['header']['version'],
        'upstream_version': imgs['upstream_0.3']['header']['version'],
        'our_iface': imgs['ours_0.1']['iface_count'],
        'upstream_iface': imgs['upstream_0.3']['iface_count'],
        'our_reset_pc': imgs['ours_0.1']['vectors'].get('reset_pc'),
        'upstream_reset_pc': imgs['upstream_0.3']['vectors'].get('reset_pc'),
        'problem_total': sum(len(r['problems']) for r in imgs.values()),
        'va_only_in_ours': [k for k, v in out['va_diff'].items()
                            if v['ours_0.1']['present'] and not v['upstream_0.3']['present']],
        'va_only_in_upstream': [k for k, v in out['va_diff'].items()
                                if not v['ours_0.1']['present'] and v['upstream_0.3']['present']],
    }
    json.dump(out, open('/tmp/fwcmp_out/fw-compare.json', 'w'), indent=2, ensure_ascii=False)

    # human summary
    print('='*76)
    for r in out['images']:
        h = r['header']
        v = r['vectors']
        print('%-18s %7d B  sha256 %s' % (r['label'], r['bytes'], r['sha256'][:16]+'...'))
        print('  header %s  vhash %s  entries_end %d  iface %d  other %d  problems %d'
              % (h['version'], h['version_hash'], h['entries_end'],
                 r['iface_count'], r['other_count'], len(r['problems'])))
        print('  MSP %s  Reset %s (odd=%s)  VA0 span %s data %d B'
              % (v.get('msp'), v.get('reset_pc'), v.get('reset_pc_odd'),
                 v.get('va0_span'), v.get('va0_section_bytes', 0)))
        ep = r['entry_point'] or {}
        print('  entry @ %s  section %s  EX=%s' % (ep.get('at'), ep.get('section_va'), ep.get('ex_bit')))
        print('    bytes: %s' % (ep.get('first_32_hex') or ep.get('error')))
        for p in r['problems']:
            print('    PROBLEM: %s' % p)
        print()
    print('VA only in ours   :', out['summary']['va_only_in_ours'])
    print('VA only in upstream:', out['summary']['va_only_in_upstream'])
    print('total problems    :', out['summary']['problem_total'])
    print('JSON -> /tmp/fwcmp_out/fw-compare.json')

main()
