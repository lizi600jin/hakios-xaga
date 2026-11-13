#!/usr/bin/env python3
"""Run an experiment; wait for required log markers after adbd returns, then restore stock.

User authorized device control/flashing on 2026-11-13. Only boot_a is flashed;
the dedicated xnusys logbook slot is backed up and cleared before each run.
No arbitrary MMIO accesses or writes to the filesystem area of xnusys.
"""
from pathlib import Path
import datetime, hashlib, json, re, subprocess, sys, time

ROOT = Path('/home/lizi/IOS/hakios-mtk')
OUT = Path(__file__).resolve().parent
SERIAL = 'GAJZ4HIN99AQ69Y9'
STOCK = ROOT / 'backup/boot_a_stock_real.img'
trial = int(sys.argv[1])
build = json.loads((OUT / f'trial-{trial}-build.json').read_text())
meta = {'trial': trial, 'args': build['verified_args'], 'build': build,
        'started_at': datetime.datetime.now().astimezone().isoformat(), 'steps': []}
meta['attempt'] = build.get('attempt', 1)
assert not (OUT / f'trial-{trial}.json').exists(), 'Trial metadata already exists'


def save():
    (OUT / f'trial-{trial}.json').write_text(json.dumps(meta, indent=2) + '\n')


def run(args, timeout=15, check=True):
    try:
        p = subprocess.run(args, capture_output=True, text=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        if check:
            raise
        return ''
    if check and p.returncode:
        raise RuntimeError(f'{args}: {p.stdout} {p.stderr}')
    return p.stdout.strip()


def adb(*args, **kwargs):
    return run(['adb', '-s', SERIAL, *args], **kwargs)


def fastboot(*args):
    cmd = ['fastboot', '-s', SERIAL, *args]
    p = subprocess.run(cmd, capture_output=True, text=True, timeout=150)
    log = p.stdout + p.stderr
    print(log, flush=True)
    meta['steps'].append({'command': cmd, 'exit': p.returncode, 'output': log})
    save()
    assert p.returncode == 0


def bootloader():
    adb('reboot', 'bootloader', timeout=15, check=False)
    deadline = time.monotonic() + 120
    while time.monotonic() < deadline:
        if SERIAL in run(['fastboot', 'devices'], timeout=5, check=False):
            return
        time.sleep(2)
    raise RuntimeError('No fastboot after reboot request; physical recovery required')


def wait_adb(seconds=420):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        if 'uid=0' in adb('shell', 'id', timeout=4, check=False):
            return
        time.sleep(3)
    raise RuntimeError('No root adbd; physical recovery required')


try:
    assert hashlib.md5(STOCK.read_bytes()).hexdigest() == 'a63b2e35a7c2522cb24a6a10332b9bc8'
    for item in build['files']:
        assert hashlib.sha256((ROOT / item['path']).read_bytes()).hexdigest() == item['sha256']
    assert adb('shell', 'getprop sys.boot_completed') == '1'
    # The source reserves 8 MiB at the end of xnusys. Clear only its existing
    # 4 MiB + 4 KiB logbook window, after saving the previous bytes on the host.
    slot_block, slot_blocks, block_bytes = 653560, 1025, 4096
    partition_bytes = int(adb('shell', 'blockdev --getsize64 /dev/block/by-name/xnusys'))
    assert partition_bytes - (8 << 20) == slot_block * block_bytes
    assert slot_blocks * block_bytes <= (8 << 20)
    remote = f'/data/local/tmp/round8-before-{trial}.bin'
    adb('shell', f'dd if=/dev/block/by-name/xnusys of={remote} bs=4096 skip={slot_block} count={slot_blocks}')
    previous = OUT / f'pre-trial-{trial}-logbook.bin'
    adb('pull', remote, str(previous), timeout=30)
    assert previous.stat().st_size == slot_blocks * block_bytes
    adb('shell', f'dd if=/dev/zero of=/dev/block/by-name/xnusys bs=4096 seek={slot_block} count={slot_blocks} conv=fsync', timeout=30)
    zero_md5 = hashlib.md5(bytes(slot_blocks * block_bytes)).hexdigest()
    observed = adb('shell', f'dd if=/dev/block/by-name/xnusys bs=4096 skip={slot_block} count={slot_blocks} 2>/dev/null | md5sum')
    assert observed.startswith(zero_md5)
    meta['logbook_preclear'] = {'partition_bytes': partition_bytes,
        'offset_bytes': slot_block * block_bytes, 'bytes': slot_blocks * block_bytes,
        'previous_sha256': hashlib.sha256(previous.read_bytes()).hexdigest(),
        'zero_md5_verified': zero_md5}
    save()
    print(f'trial {trial}: previous log backed up; reserved logbook slot zeroed and verified', flush=True)
    print(f'trial {trial}: prepared hashes verified; entering fastboot', flush=True)
    bootloader()
    fastboot('flash', 'boot_a', str(ROOT / 'xnu-flashed-slot_a.img'))
    fastboot('reboot')
    meta['experiment_reboot_at'] = datetime.datetime.now().astimezone().isoformat()
    save()
    print(f'trial {trial}: waiting for adbd; log pull does not require boot_completed', flush=True)
    wait_adb()
    print(f'trial {trial}: root adbd returned; waiting up to 150s for required log markers', flush=True)
    # Trial 690 came back truncated: adbd appeared while the MCU start loop was still
    # spinning, so the pull caught the logbook at "spin 100000" and missed
    # "the start loop ended", every status-semantics line and the witness readback.
    # Wait for a completion marker inside the reserved window before pulling, with a
    # bounded budget; if it never arrives, pull anyway and record that it is short.
    def _has_completion_marker(seconds=150):
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            probe = adb('shell',
                f'dd if=/dev/block/by-name/xnusys bs=4096 skip=653560 count=1025 2>/dev/null | strings',
                timeout=30, check=False)
            start_complete = ('the start loop ended' in probe) or ('witness: read1' in probe)
            status_required = 'mali_statussem=1' in build['verified_args'].split()
            status_markers = [f'status semantics trial {trial}: {step}' for step in
                              ('after DISABLE', 'second DISABLE', 'bounded re-read')]
            status_complete = all(marker in probe for marker in status_markers)
            meta['completion_markers_seen'] = {'start_complete': start_complete,
                                                'status_probe_required': status_required,
                                                'status_probe_complete': status_complete}
            meta['completion_checked_at'] = datetime.datetime.now().astimezone().isoformat()
            save()
            if start_complete and (not status_required or status_complete):
                return True
            time.sleep(5)
        return False

    meta['log_complete'] = _has_completion_marker()
    if not meta['log_complete']:
        print(f'trial {trial}: completion marker never appeared within budget; '
              f'the pull below is likely short and must not be read as a result', flush=True)
    save()
    adb('shell', f'dd if=/dev/block/by-name/xnusys of=/data/local/tmp/round8-{trial}.bin bs=4096 skip=653560 count=1025')
    logbook = OUT / f'trial-{trial}-logbook.bin'
    adb('pull', f'/data/local/tmp/round8-{trial}.bin', str(logbook), timeout=30)
    raw = logbook.read_bytes()
    assert len(raw) == 4198400
    meta['current_trial_marker_present'] = f'trial {trial}'.encode() in raw
    if not meta['current_trial_marker_present']:
        print(f'trial {trial}: no current marker; retain raw evidence and restore stock; result inconclusive', flush=True)
    meta['logbook_sha256'] = hashlib.sha256(raw).hexdigest()
    rows = [(m[1].decode(), m[2].decode('utf-8', 'replace'), m.start())
            for m in re.finditer(rb'\[\s*([0-9]+\.[0-9]+)\]: (mali:[^\x00\n]{0,800})', raw)]
    (OUT / f'trial-{trial}-mali.txt').write_text(''.join(f'{off:08x} {t} {s}\n' for t, s, off in rows))
    for t, s, _ in rows:
        if re.search('round7|coverage:|AS0 step update|version word|global interface|witness:|shared probe trial', s):
            print(t, s, flush=True)
    deadline = time.monotonic() + 45
    completed = False
    while time.monotonic() < deadline:
        if adb('shell', 'getprop sys.boot_completed', timeout=5, check=False) == '1':
            completed = True
            break
        time.sleep(3)
    meta['android_completed_before_stock_restore'] = completed
    save()
    print(f'trial {trial}: captured current log; boot_completed={completed}; restoring stock boot_a', flush=True)
    # Trial 671 showed Android's normal reboot service can stall during return.
    # Root adbd still works: restore the unmounted boot partition, verify it,
    # sync storage, then request the kernel's emergency restart directly.
    adb('push', str(STOCK), '/data/local/tmp/hakios-stock-boot-a.img', timeout=30)
    assert adb('shell', 'md5sum /data/local/tmp/hakios-stock-boot-a.img').startswith('a63b2e35a7c2522cb24a6a10332b9bc8')
    adb('shell', 'dd if=/data/local/tmp/hakios-stock-boot-a.img of=/dev/block/by-name/boot_a bs=4194304 conv=fsync', timeout=30)
    assert adb('shell', 'sync; md5sum /dev/block/by-name/boot_a').startswith('a63b2e35a7c2522cb24a6a10332b9bc8')
    meta['stock_restore_method'] = 'root_dd_fsync_verify_sync_then_sysrq_restart'
    save()
    adb('shell', 'sync; echo b > /proc/sysrq-trigger', timeout=5, check=False)
    wait_adb()
    deadline = time.monotonic() + 120
    while adb('shell', 'getprop sys.boot_completed', timeout=5, check=False) != '1':
        assert time.monotonic() < deadline, 'Stock Android did not finish booting'
        time.sleep(3)
    digest = adb('shell', 'md5sum /dev/block/by-name/boot_a')
    assert digest.startswith('a63b2e35a7c2522cb24a6a10332b9bc8')
    meta['stock_android_boot_completed'] = True
    meta['stock_boot_a_md5'] = digest.split()[0]
    meta['outcome'] = 'experiment_captured_and_stock_restored'
    print(f'trial {trial}: stock Android boot_completed=1, stock MD5 verified', flush=True)
except Exception as exc:
    meta['outcome'] = 'interrupted_or_failed'
    meta['error'] = str(exc)
    raise
finally:
    save()
