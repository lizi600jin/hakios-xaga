#!/usr/bin/env python3
"""Final host input/artifact audit; no device calls and no compiler replay."""
import hashlib
import json
from pathlib import Path
import subprocess

ROOT = Path('/home/lizi/IOS/hakios-mtk')
DOCS = Path('/home/lizi/IOS/repo/hakios-mt6895')
BASE = Path(__file__).resolve().parent
CPU = ROOT / 'logs/mt6895-cpu-baseline-20261116'


def identity(path):
    data = path.read_bytes()
    return {'path': str(path), 'bytes': len(data),
            'sha256': hashlib.sha256(data).hexdigest()}


def checked(path, expected, size=None):
    result = identity(path)
    assert result['sha256'] == expected, result
    assert size is None or result['bytes'] == size, result
    return result


def main():
    records = json.loads((CPU / 'production-before.json').read_text())
    production = [checked(ROOT / r['path'], r['sha256'], r['bytes']) for r in records]
    records = json.loads((BASE / 'original-identities.json').read_text())
    originals = [checked(Path(r['path']), r['sha256'], r['bytes']) for r in records]
    retained = []
    for directory, report in [(CPU, 'independent-package-review.json'),
                              (BASE, 'independent-package-review.json')]:
        old = json.loads((directory / report).read_text())
        trial = Path(old['image']).parent
        for name, key in [('boot.img', 'image'), ('payload.bin', 'payload'),
                          ('loader.bin', 'loader')]:
            retained.append(checked(trial / name, old[key + '_sha256'], old[key + '_bytes']))
    stock = checked(ROOT / 'backup/boot_a_stock_real.img',
                    'c5f8e7bde8e9f1d824d0c7915f7d701adce8142656a82c20e10ccb75e15e01fc',
                    67108864)
    wip = checked(DOCS / 'docs/headset-wip/findings/GPUEB-INTEGRATION.patch',
                  '9cdcb0da826e9bd561fce98aae81304789fee2742c63b10235942f028e53eb16')
    staged = subprocess.run(['git', '-C', str(DOCS), 'diff', '--cached', '--name-only'],
                            check=True, capture_output=True, text=True).stdout.splitlines()
    assert 'docs/headset-wip/findings/GPUEB-INTEGRATION.patch' not in staged
    report = json.loads((BASE / 'trial715/artifacts.json').read_text())
    current = [checked(BASE / 'trial715' / name, item['sha256'], item['bytes'])
               for name, item in report.items()]
    commands = json.loads((BASE / 'trial715/compile-commands.json').read_text())
    commands += [json.loads((BASE / 'trial715' / name).read_text())
                 for name in ('mkpayload-cmd.json', 'chainboot-cmd.json')]
    assert len(commands) == 5 and all(c['rc'] == 0 for c in commands)
    macros = {}
    for line in (BASE / 'trial715/loader-macros.txt').read_text().splitlines():
        if line.startswith('#define BOARD_MALI_'):
            _, key, value = line.split(maxsplit=2)
            macros[key] = value
    for key in ('POWER', 'PREPOWER', 'EARLY', 'RETURN', 'STOP'):
        assert macros['BOARD_MALI_' + key] == '0', macros
    for name in ('independent-package-review-715.json', 'independent-loader-review-715.json'):
        assert json.loads((BASE / name).read_text())['status'] == 'PASS', name
    result = dict(status='PASS', device_commands_run=False,
                  production_inputs_verified=production, original_candidate_verified=originals,
                  retained_713_714_artifacts_verified=retained, stock_source=stock,
                  gpueb_wip_unchanged_and_unstaged=wip, current_715_verified=current,
                  packaging_native_rc=[c['rc'] for c in commands], loader_mali_macros=macros,
                  compiler_warnings_retained=commands[1]['stderr'],
                  limits=['713 historical object identities remain unproven; original hashes absent',
                          'Stock source hash is a host-file check, not a new device measurement',
                          'Only enumerated production inputs and artifact identities checked here'])
    (BASE / 'POST-HOST-AUDIT-715.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({'status': 'PASS', 'production_inputs': len(production),
                      'retained_artifacts': len(retained), 'current_artifacts': len(current),
                      'device_commands_run': False}))


if __name__ == '__main__':
    main()
