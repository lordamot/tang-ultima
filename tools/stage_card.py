#!/usr/bin/env python3
"""Collect the five siblings' assets in build/card using the JSON paths.

Never writes a mounted SD card or a sibling. Existing staged user data and
saved INIs are left alone; --refresh explicitly replaces shipped assets.
"""
import argparse
import json
from pathlib import Path
import shutil

ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / 'build/card'
SIBLINGS = {'tang-uknc', 'tang-pk8000', 'tang-korvet', 'tang-zs256', 'tang-bk-epta'}


def below(root, relative):
    path = root / relative
    if not path.resolve().is_relative_to(root.resolve()):
        raise ValueError(f'path escapes {root}: {relative}')
    return path


def collect(config_only=False):
    catalog = json.loads((ROOT / 'config/companion.json').read_text())
    copies = [(ROOT / 'config/companion.json', OUTPUT / 'companion.json')]
    directories = {OUTPUT}
    for core in catalog['cores']:
        profile_file = below(ROOT / 'config', core['profile'])
        profile = json.loads(profile_file.read_text())
        copies.append((profile_file, below(OUTPUT, core['profile'])))
        if config_only:
            continue
        copies.append((ROOT / 'bin' / (profile['protocol'] + '.bin'), below(OUTPUT, core['bitstream'])))
        if profile['source'] not in SIBLINGS:
            raise ValueError(f'unknown sibling: {profile["source"]}')
        sibling = ROOT.parent / profile['source']
        machine = below(OUTPUT, core['dir'])
        directories.add(machine)
        directories.add(below(machine, core['settings']).parent)
        for key, name in profile['resources'].items():
            resource = below(machine, name)
            directories.add(resource if key == 'extrom' else resource.parent)
        for asset in profile.get('assets', []):
            # glob accepts relative patterns only. Resolve every match again,
            # including symlinks, before it can become a copy source.
            pattern = asset['source']
            if Path(pattern).is_absolute() or '..' in Path(pattern).parts:
                raise ValueError(f'invalid source pattern: {pattern}')
            matches = sorted(sibling.glob(pattern))
            if not matches:
                raise FileNotFoundError(f'{sibling}/{pattern}')
            destination = profile['resources'][asset['resource']] if 'resource' in asset else asset['destination']
            target = below(machine, destination)
            for source in matches:
                below(sibling, source.relative_to(sibling))
                if source.is_dir():
                    for file in sorted(source.rglob('*')):
                        if file.is_file():
                            below(sibling, file.relative_to(sibling))
                            copies.append((file, below(target, file.relative_to(source))))
                else:
                    copies.append((source, target if 'resource' in asset else below(target, source.name)))
    for source, target in copies:
        if not source.is_file():
            raise FileNotFoundError(f'{source} (build the missing core first)')
        below(OUTPUT, target.relative_to(OUTPUT))
    return copies, directories


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--refresh', action='store_true', help='replace shipped files already staged in build/card')
    parser.add_argument('--config-only', action='store_true', help='stage the catalog and JSON profiles only')
    parser.add_argument('--output', type=Path, default=OUTPUT, help='stage into this repository or a known sibling build/card directory')
    args = parser.parse_args()
    allowed = {OUTPUT.resolve()} | {(ROOT.parent / name / 'build/card').resolve() for name in SIBLINGS}
    OUTPUT = args.output.resolve()
    # Scratch sibling builds are used to verify the integration before touching
    # neighboring repositories. All destinations remain inside build/.
    scratch = ROOT / 'build/sibling-integration'
    allowed |= {(scratch / name / 'build/card').resolve() for name in SIBLINGS}
    if OUTPUT not in allowed:
        parser.error('--output must be a known repository build/card directory')
    copies, directories = collect(args.config_only)  # Validate every input before copying anything.
    for directory in sorted(directories):
        directory.mkdir(parents=True, exist_ok=True)
    copied = 0
    for source, target in copies:
        # Config always reflects this checkout. Assets and bitstreams are
        # replaced only explicitly, so repeated staging keeps local media.
        if target.exists() and not args.refresh and not source.is_relative_to(ROOT / 'config'):
            continue
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, target)
        copied += 1
    print(f'card-stage: {copied} files copied; {len(copies)} shipped files in {OUTPUT}')
    print('Copy build/card contents to the card root; keep your existing saved settings and writable images.')
