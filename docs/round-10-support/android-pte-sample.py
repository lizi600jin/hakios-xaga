#!/usr/bin/env python3
"""Sample actual Android PTE constructor return values via a private trace instance.

Only CPU registers are fetched ($retval:x64), with no memory/MMIO dereference.
Reference: https://www.kernel.org/doc/html/v5.10/trace/kprobetrace.html
The sample may contain user mappings; it is not a complete firmware-table dump.
"""
from pathlib import Path
import collections, datetime, json, re, shlex, subprocess, time

out = Path(__file__).resolve().parent
serial = 'GAJZ4HIN99AQ69Y9'
base = '/sys/kernel/tracing'
name = f'hakios_pte_{int(time.time())}'
instance = f'{base}/instances/{name}'
event = f'hakios_gpu/{name}'
meta = {'started_at': datetime.datetime.now().astimezone().isoformat(),
        'method': 'private kretprobe, CPU return register only',
        'target': 'mali_kbase_mt6895:kbase_mmu_create_ate', 'event': event,
        'limitation': 'Mappings created during sample; not necessarily firmware mappings.'}
created = registered = enabled = False


def adb(command, check=True, timeout=15):
    p = subprocess.run(['adb', '-s', serial, 'shell', command], capture_output=True,
                       text=True, timeout=timeout)
    if check and p.returncode:
        raise RuntimeError(f'{command}: {p.stdout} {p.stderr}')
    return p.stdout


def write(path, value, append=False):
    adb(f"printf '%s\\n' {shlex.quote(value)} {'>>' if append else '>'} {shlex.quote(path)}")


try:
    assert adb('getprop sys.boot_completed').strip() == '1'
    assert adb('md5sum /dev/block/by-name/boot_a').startswith('a63b2e35a7c2522cb24a6a10332b9bc8')
    adb(f'mkdir {instance}')
    created = True
    definition = f'r128:{event} {meta["target"]} pte=$retval:x64'
    write(f'{base}/kprobe_events', definition, append=True)
    registered = True
    meta['definition'] = definition
    meta['format'] = adb(f'cat {instance}/events/{event}/format')
    write(f'{instance}/events/{event}/enable', '1')
    enabled = True
    write(f'{instance}/tracing_on', '1')
    # Capture readback requests a new render buffer; image bytes are discarded
    # on the phone. No screenshot is stored or transmitted.
    meta['render_readback_output'] = adb('screencap >/dev/null', check=False, timeout=15)
    time.sleep(2)
    write(f'{instance}/tracing_on', '0')
    trace = adb(f'cat {instance}/trace')
    (out / 'android-real-pte.trace').write_text(trace)
    values = [int(x, 16) for x in re.findall(r'pte=(0x[0-9a-fA-F]+)', trace)]
    meta['rows'] = len(values)
    meta['pbha_counts'] = dict(collections.Counter(str((v >> 59) & 15) for v in values))
    meta['low_attributes_counts'] = dict(collections.Counter(hex(v & 4095) for v in values))
    meta['sample_values'] = [hex(v) for v in values[:32]]
    meta['physical_address_min'] = hex(min((v & 0x0000fffffffff000) for v in values)) if values else None
    meta['physical_address_max'] = hex(max((v & 0x0000fffffffff000) for v in values)) if values else None
    meta['outcome'] = 'sampled' if values else 'no_events_observed'
except Exception as exc:
    meta['outcome'] = 'unavailable_or_failed'
    meta['error'] = str(exc)
finally:
    cleanup = []
    if created:
        cleanup.append(adb(f'printf 0 > {instance}/tracing_on', check=False).strip())
    if enabled:
        cleanup.append(adb(f'printf 0 > {instance}/events/{event}/enable', check=False).strip())
    if registered:
        cleanup.append(adb(f"printf '%s\\n' {shlex.quote('-:' + event)} >> {base}/kprobe_events", check=False).strip())
    if created:
        cleanup.append(adb(f'rmdir {instance}', check=False).strip())
    meta['cleanup_output'] = cleanup
    (out / 'android-real-pte.json').write_text(json.dumps(meta, indent=2) + '\n')
    print(json.dumps(meta, indent=2))
