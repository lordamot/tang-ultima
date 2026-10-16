#!/usr/bin/env python3
"""Record or check which sibling firmware sources the shared profiles came from.

This is provenance, not a firmware generator. A drift report means review
the sibling change and update the JSON or shared protocol implementation.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
LOCK = ROOT / 'config/siblings.lock.json'
BACKENDS = {'uknc': ['rt11sav.c', 'rt11sav.h', 'uknc.h'],
            'pk8000': ['bas.c', 'bas.h', 'pk8000.h'],
            'korvet': ['extrom.c', 'extrom.h', 'korvet.h'],
            'zs256': ['romload.c', 'romload.h', 'zs256.h'],
            'bk': ['azbk.c', 'azbk.h', 'bk.c', 'bk.h'],
            'evo': ['evo.c', 'evo.h', 'vsd.c', 'vsd.h']}


def snapshot():
    records = {}
    catalog = json.loads((ROOT / 'config/companion.json').read_text())
    for core in catalog['cores']:
        profile = json.loads((ROOT / 'config' / core['profile']).read_text())
        repo = ROOT.parent / profile['source']
        if not repo.is_dir():
            raise FileNotFoundError(repo)
        files = ['menu.c', 'usb_host.c', 'sdc.c', 'sysctrl.c', 'sysctrl.h'] + BACKENDS[profile['protocol']]
        records[profile['source']] = {
            'revision': subprocess.run(['git', '-C', str(repo), 'rev-parse', '--verify', 'HEAD'], capture_output=True, text=True).stdout.strip() or 'uncommitted',
            'version': (repo / 'VERSION').read_text().strip(),
            'files': {f'mnano/{name}': hashlib.sha256((repo / 'mnano' / name).read_bytes()).hexdigest()
                      for name in files},
        }
    return {'version': 1, 'siblings': records}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--record', action='store_true', help='record the current source snapshot after review')
    args = parser.parse_args()
    current = snapshot()
    if args.record:
        LOCK.write_text(json.dumps(current, indent=2) + '\n')
        print(f'sibling source snapshot: {LOCK}')
    else:
        saved = json.loads(LOCK.read_text())
        changed = []
        for repo, data in current['siblings'].items():
            old = saved['siblings'].get(repo, {})
            for file, sha in data['files'].items():
                if old.get('files', {}).get(file) != sha:
                    changed.append(f'{repo}/{file}')
            print(f'{repo}: {data["version"]}, {data["revision"][:12]}')
        if changed:
            raise SystemExit('Companion sources changed; review before updating the shared firmware:\n' + '\n'.join(changed))
        print('siblings-check: all source snapshots match')
        builds = ROOT / 'config/sibling-builds.lock.json'
        if builds.exists():
            entrypoints = json.loads(builds.read_text())['siblings']
            common = (ROOT / 'bin/bl616.bin').read_bytes()
            for repo, files in entrypoints.items():
                for file, sha in files.items():
                    if hashlib.sha256((ROOT.parent / repo / file).read_bytes()).hexdigest() != sha:
                        raise SystemExit(f'{repo}/{file}: shared build entry point changed; review the integration')
                if (ROOT.parent / repo / 'bin/bl616.bin').read_bytes() != common:
                    raise SystemExit(f'{repo}/bin/bl616.bin differs from the shared shipped firmware')
            print('siblings-check: all five build entry points and all six shipped images agree')
