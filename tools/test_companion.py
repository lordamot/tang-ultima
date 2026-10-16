#!/usr/bin/env python3
"""Behavioral checks for readable profiles and legacy SD path compatibility."""
import copy
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
RUNNER = ROOT / 'build/config/companion_test'
count = 0


def run(args, ok=True, contains=None, env=None):
    global count
    result = subprocess.run([str(RUNNER), *map(str, args)], capture_output=True, text=True, env=env)
    assert (result.returncode == 0) == ok, (args, result.stdout, result.stderr)
    if contains:
        assert contains in result.stdout, (args, result.stdout, result.stderr)
    count += 1


with tempfile.TemporaryDirectory(dir=ROOT / 'build/config') as tmp:
    temp = Path(tmp) / 'test.json'

    def document(mode, obj, ok=True, contains=None):
        temp.write_text(json.dumps(obj))
        run([*mode, temp], ok, contains)

    catalog = json.loads((ROOT / 'config/companion.json').read_text())
    run(['catalog', ROOT / 'config/companion.json'])
    for core in catalog['cores']:
        run(['profile', core['id'], ROOT / 'config' / core['profile']])

    moved = copy.deepcopy(catalog)
    moved['cores'][0].update(dir='machines/uknc', bitstream='images/mc0511.bin')
    document(['catalog'], moved, contains='5 /sd/machines/uknc /sd/images/mc0511.bin')
    for change in [lambda c: c.update(version=2),
                   lambda c: c['cores'][0].update(id=10),
                   lambda c: c['cores'][0].update(dir='../uknc'),
                   lambda c: c['cores'][0].update(settings='../../state.ini'),
                   lambda c: c['cores'][1].update(bitstream='cores/uknc.bin'),
                   lambda c: c['cores'][0].update(name='Bad;C,Erase,0')]:
        bad = copy.deepcopy(catalog)
        change(bad)
        document(['catalog'], bad, False)

    profile = json.loads((ROOT / 'config/cores/uknc.json').read_text())
    edited = copy.deepcopy(profile)
    edited['forms'][0]['title'] = 'My UKNC'
    edited['keyboard']['keymap'][4] = 77
    edited['resources']['sav_disk'] = 'generated/BOOT.DSK'
    document(['profile', 5], edited, contains='key4=77')
    document(['profile', 5], edited, contains='sav=/sd/uknc/generated/BOOT.DSK')
    changes = [lambda p: p.update(id=7), lambda p: p.update(protocol='bk'),
               lambda p: p['forms'][0]['entries'][0].update(slot=6),
               lambda p: p['forms'][0]['entries'][4].update(form=15),
               lambda p: p['forms'][1].update(parent=1),
               lambda p: p['defaults'].update(A=255),
               lambda p: p['keyboard']['keymap'].__setitem__(4, 256),
               lambda p: p['keyboard'].update(modifiers=[1, 2]),
               lambda p: p['resources'].update(sav_disk='../BOOT.DSK'),
               lambda p: p['forms'][0]['entries'][1].update(visible_if='!'),
               lambda p: p['forms'][0]['entries'][0].update(label='Bad;F,Disk,4'),
               lambda p: p['forms'][0]['entries'][0].update(type='execute'),
               lambda p: p['forms'].__setitem__(7, p['forms'][6]),
               lambda p: p.update(save_extra_slot=True)]
    for change in changes:
        bad = copy.deepcopy(profile)
        change(bad)
        document(['profile', 5], bad, False)
    for raw in ['{', json.dumps(profile) + 'garbage',
                '{"version":1,"version":1}', '{"x":' + '[' * 15 + '0' + ']' * 15 + '}',
                '{"name":"\\u0000"}', '{"x":"' + 'a' * 33000 + '"}']:
        temp.write_text(raw)
        run(['profile', 5, temp], False)

    for root, path, expected in [('/sd/uknc', 'disk.dsk', '/sd/uknc/disk.dsk'),
                                 ('/sd/uknc', '/sd/uknc/DISKS/a.dsk', '/sd/uknc/DISKS/a.dsk'),
                                 ('/sd/uknc', '/SD/UKNC/a.dsk', '/sd/UKNC/a.dsk'),
                                 ('/sd/bk', '0:/ROM/BOS.ROM', '/sd/bk/ROM/BOS.ROM'),
                                 ('/sd/bk', r'0:\DISKS\DAVE.IMG', '/sd/bk/DISKS/DAVE.IMG'),
                                 ('/sd/bk', '/ROM/BOS.ROM', '/sd/bk/ROM/BOS.ROM')]:
        run(['path', root, path, 256], contains=expected)
    for path in ['../a', 'ROM/../../a', 'ROM/./a', '1:/a', '', 'a' * 256]:
        run(['path', '/sd/bk', path, 256], False)
    run(['path', '/sd/uknc', 'disk.dsk', 8], False)

    evo = json.loads((ROOT / 'config/cores/evo.json').read_text())
    missing = copy.deepcopy(evo)
    del missing['resources']['sd']
    document(['profile', 11], missing, False)
    run(['boot', 11], contains='Evo Nano,;')

    # The same executable loads a customized card file and falls back when
    # that file is malformed. No recompile separates these two runs.
    card = Path(tmp) / 'card'
    (card / 'cores').mkdir(parents=True)
    (card / 'cores/uknc.json').write_text('{')
    env = dict(os.environ, COMPANION_CARD=str(card))
    run(['boot', 5], contains='using embedded profile', env=env)
    (card / 'companion.json').write_text(json.dumps(moved))
    (card / 'cores/uknc.json').write_text(json.dumps(edited))
    run(['boot', 5], contains='My UKNC,;', env=env)
    run(['boot', 5], contains='key4=77', env=env)
    run(['boot', 5], contains='sav=/sd/machines/uknc/generated/BOOT.DSK', env=env)

print(f'config-check: {count} checks passed')
