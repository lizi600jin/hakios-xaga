#!/usr/bin/env python3
"""Sample stock Mali PTE return registers while allocating owned user buffers.

Only CPU $retval registers are fetched; no MMIO or pointer dereferences.
Reference: https://www.kernel.org/doc/html/v5.10/trace/kprobetrace.html
These are user mappings, not a firmware-table dump.
"""
from pathlib import Path
import collections
import datetime
import hashlib
import json
import re
import shlex
import subprocess
import time

out = Path(__file__).resolve().parent
serial = 'GAJZ4HIN99AQ69Y9'
base = '/sys/kernel/tracing'
name = f'hakios_alloc_{time.time_ns()}'
group = name
instance = f'{base}/instances/{name}'
remote_helper = '/data/local/tmp/hakios-mali-alloc'
prefix = out / 'android-alloc-real-pte'
assert not prefix.with_suffix('.json').exists(), 'Preserve previous sample'
targets = [
    'mali_kbase_mt6895:kbase_mmu_create_ate',
    'mali_kbase_mt6895:kbase_native_mgm_update_gpu_pte',
    'mali_mgm:example_mgm_update_gpu_pte',
]
meta = {'started_at': datetime.datetime.now().astimezone().isoformat(),
        'method': 'private kretprobes; CPU return registers only',
        'limitation': 'Owned user-buffer mappings; not the firmware AS0 table.',
        'probes': [], 'cleanup': []}
created = False


def adb(command, check=True, timeout=15):
    p = subprocess.run(['adb', '-s', serial, 'shell', command],
                       capture_output=True, text=True, timeout=timeout)
    result = {'exit': p.returncode, 'stdout': p.stdout, 'stderr': p.stderr}
    if check and p.returncode:
        raise RuntimeError(f'{command}: {result}')
    return result


def write(path, value, append=False):
    return adb(f"printf '%s\\n' {shlex.quote(value)} "
               f"{'>>' if append else '>'} {shlex.quote(path)}")


try:
    assert adb('getprop sys.boot_completed')['stdout'].strip() == '1'
    meta['stock_boot_md5'] = adb('md5sum /dev/block/by-name/boot_a')['stdout'].split()[0]
    assert meta['stock_boot_md5'] == 'a63b2e35a7c2522cb24a6a10332b9bc8'
    meta['helper_sha256'] = hashlib.sha256((out / 'mali-alloc-sample').read_bytes()).hexdigest()
    assert adb(f'sha256sum {remote_helper}')['stdout'].split()[0] == meta['helper_sha256']
    adb(f'mkdir {instance}')
    created = True
    write(f'{instance}/tracing_on', '0')
    for i, target in enumerate(targets):
        probe = {'target': target, 'event': f'{group}/pte{i}', 'registered': False,
                 'enabled': False}
        meta['probes'].append(probe)
        definition = f'r128:{probe["event"]} {target} pte=$retval:x64'
        probe['definition'] = definition
        try:
            write(f'{base}/kprobe_events', definition, append=True)
            probe['registered'] = True
            probe['format'] = adb(f'cat {instance}/events/{probe["event"]}/format')['stdout']
            write(f'{instance}/events/{probe["event"]}/enable', '1')
            probe['enabled'] = True
        except Exception as exc:
            probe['error'] = str(exc)
    assert any(p['enabled'] for p in meta['probes']), 'No usable return probes'
    write(f'{instance}/tracing_on', '1')
    meta['allocation'] = adb(remote_helper, check=False, timeout=15)
    write(f'{instance}/tracing_on', '0')
    trace = adb(f'cat {instance}/trace')['stdout']
    prefix.with_suffix('.trace').write_text(trace)
    meta['trace_sha256'] = hashlib.sha256(trace.encode()).hexdigest()
    for probe in meta['probes']:
        event = probe['event'].split('/')[1]
        values = [int(v, 16) for v in re.findall(
            rf'\b{event}:.*?\bpte=(0x[0-9a-fA-F]+)', trace)]
        probe['rows'] = len(values)
        probe['pbha_counts'] = dict(collections.Counter(str((v >> 59) & 15) for v in values))
        probe['low_attributes_counts'] = dict(collections.Counter(hex(v & 4095) for v in values))
        probe['sample_values'] = [hex(v) for v in values[:32]]
        probe['physical_address_min'] = hex(min(v & 0x0000fffffffff000 for v in values)) if values else None
        probe['physical_address_max'] = hex(max(v & 0x0000fffffffff000 for v in values)) if values else None
    meta['outcome'] = ('sampled' if any(p.get('rows', 0) for p in meta['probes'])
                       else 'no_events_observed')
except Exception as exc:
    meta['outcome'] = 'unavailable_or_failed'
    meta['error'] = str(exc)
finally:
    if created:
        meta['cleanup'].append(adb(f'printf 0 > {instance}/tracing_on', check=False))
    for probe in meta['probes']:
        if probe['enabled']:
            meta['cleanup'].append(adb(f'printf 0 > {instance}/events/{probe["event"]}/enable', check=False))
        if probe['registered']:
            meta['cleanup'].append(adb(
                f"printf '%s\\n' {shlex.quote('-:' + probe['event'])} >> {base}/kprobe_events", check=False))
    if created:
        meta['cleanup'].append(adb(f'rmdir {instance}', check=False))
    remaining_events = adb(f'cat {base}/kprobe_events', check=False)
    remaining_instances = adb(f'ls {base}/instances', check=False)
    meta['cleanup_verified'] = (all(p['exit'] == 0 for p in meta['cleanup'])
                                and remaining_events['exit'] == 0
                                and remaining_instances['exit'] == 0
                                and name not in remaining_events['stdout']
                                and name not in remaining_instances['stdout'])
    meta['boot_completed_after'] = adb('getprop sys.boot_completed', check=False)['stdout'].strip()
    prefix.with_suffix('.json').write_text(json.dumps(meta, indent=2) + '\n')
    print(json.dumps(meta, indent=2))
