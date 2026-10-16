#!/usr/bin/env python3
"""Prepare/review or apply the shared companion entry points in all five siblings.

Preparation writes build/sibling-integration/ and a unified diff only. Applying
requires explicit --apply and checks every destination against its prepared
hash before writing. It never changes FPGA sources, Git state, or toolchains.
"""
import argparse
import difflib
import hashlib
import json
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
STAGE = ROOT / 'build/sibling-integration'
REPOS = ['tang-uknc', 'tang-pk8000', 'tang-korvet', 'tang-zs256', 'tang-bk-epta']

MNANO_MAKE = '''# The companion implementation and JSON profiles live in tang-ultima.
SIBLING_ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/..)
COMPANION_ROOT ?= $(abspath $(SIBLING_ROOT)/../tang-ultima)
.DEFAULT_GOAL := build
.PHONY: build fw ninja flash test sdl_menu_test clean term size
build fw ninja:
	$(MAKE) -C "$(SIBLING_ROOT)" fw COMPANION_ROOT="$(COMPANION_ROOT)"
	@mkdir -p "$(SIBLING_ROOT)/mnano/build/build_out"
	cp "$(SIBLING_ROOT)/build/fw/bl616.bin" "$(SIBLING_ROOT)/mnano/build/build_out/misterynano_fw_bl616.bin"
flash:
	$(MAKE) -C "$(SIBLING_ROOT)" flash-mcu COMPANION_ROOT="$(COMPANION_ROOT)"
test sdl_menu_test:
	$(MAKE) -C "$(SIBLING_ROOT)" menu-test COMPANION_ROOT="$(COMPANION_ROOT)"
size:
	$(MAKE) -C "$(COMPANION_ROOT)/mnano" size
term:
	term.sh $(COMX) $(BAUDRATE)
clean:
	rm -rf "$(SIBLING_ROOT)/mnano/build"
'''

MNANO_CMAKE = '''cmake_minimum_required(VERSION 3.15)
project(shared_companion NONE)
get_filename_component(COMPANION_DEFAULT "${CMAKE_CURRENT_LIST_DIR}/../../tang-ultima" ABSOLUTE)
set(COMPANION_ROOT "${COMPANION_DEFAULT}" CACHE PATH "Shared Tang Ultima companion repository")
if(NOT EXISTS "${COMPANION_ROOT}/mnano/companion.c")
  message(FATAL_ERROR "Shared firmware missing: clone tang-ultima beside this repository or set COMPANION_ROOT")
endif()
find_program(COMPANION_MAKE NAMES gmake make)
if(NOT COMPANION_MAKE)
  message(FATAL_ERROR "GNU Make is required to build the shared companion")
endif()
add_custom_target(shared_companion ALL
  COMMAND "${COMPANION_MAKE}" -C "${COMPANION_ROOT}" fw
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/build_out"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
    "${COMPANION_ROOT}/build/fw/bl616.bin"
    "${CMAKE_CURRENT_BINARY_DIR}/build_out/misterynano_fw_bl616.bin"
  COMMENT "Build the common JSON-configured BL616 firmware" VERBATIM)
'''

README_NOTE = '''## Shared BL616 companion

The firmware for UKNC, PK8000, Korvet, ZS-256 and BK now has one implementation
in the sibling `../tang-ultima` repository. `make fw`, direct `make -C mnano`
and CMake all use it. `COMPANION_ROOT=/path/to/tang-ultima` selects another
checkout. No per-core compiler switches are used.

`bin/bl616.bin` is the same shipped image in all five repositories;
`make fw` writes the new image to `build/fw/bl616.bin`. `make flash-mcu`
uses the shipped image by default and refuses stale binaries after a newer
build. To flash a fresh build, pass `FW_BIN=build/fw/bl616.bin`.

`make card` collects the common JSON profiles, bitstreams and all five
machines' assets into this repository's `build/card/`. It preserves existing
staged media. Copy the staged contents to the card root, preserving your
saved settings and writable images. `make card-config` stages JSON only.
JSON edits take effect after restarting the dock; firmware rebuilding is
needed for protocol changes. See `../tang-ultima/config/README.md`.

`make menu-test`, `make config-check` and `make storage-test` exercise the
actual shared companion. Local C files remain as historical references for
machine-specific regression tests; they are not separate firmware builds.
The FPGA build and machine code are unchanged. The new shared firmware is
host-tested, not yet verified on a board.

'''


def digest(data):
    return hashlib.sha256(data).hexdigest()


def makefile(text):
    assert 'COMPANION_ROOT' not in text, 'already integrated; review instead of overwriting'
    text = text.replace('FW_BIN   ?= bin/bl616.bin', 'COMPANION_ROOT ?= $(abspath $(ROOT)/../tang-ultima)\nFW_BIN   ?= bin/bl616.bin')
    a = text.index('# The BL616 firmware')
    b = text.index('.PHONY:', a)
    text = text[:a] + '# One companion implementation and JSON configuration in tang-ultima.\n\n' + text[b:]
    a = text.index('fw:\n')
    b = text.index('\n#', a)
    text = text[:a] + '''fw:
	@test -f "$(COMPANION_ROOT)/mnano/companion.c" || { echo "shared firmware missing: clone tang-ultima beside this repository, or set COMPANION_ROOT" >&2; exit 1; }
	$(MAKE) -C "$(COMPANION_ROOT)" fw
	@mkdir -p $(BUILD)/fw
	cp "$(COMPANION_ROOT)/build/fw/bl616.bin" $(BUILD)/fw/bl616.bin
	@echo "shared companion: build/fw/bl616.bin (JSON profiles select the machine)"
''' + text[b:]
    # Keep machine-specific backend tests but make menu checks use the actual
    # shipped firmware, rather than the historical sibling menu source.
    a = text.index('menu-test:')
    b = text.find('\n#', a)
    if b < 0:
        b = text.index('\nclean:', a)
    text = text[:a] + '''menu-test config-check storage-test:
	$(MAKE) -C "$(COMPANION_ROOT)" $@

''' + text[b:]
    # Replace the old (possibly informational) card target, and add it to
    # siblings that did not yet have one. Resolve every destination via JSON.
    card = '''.PHONY: card card-config menu-test config-check storage-test
card: config-check
	$(PYTHON) "$(COMPANION_ROOT)/tools/stage_card.py" --output "$(BUILD)/card"
card-config: config-check
	$(PYTHON) "$(COMPANION_ROOT)/tools/stage_card.py" --config-only --output "$(BUILD)/card"
'''
    if re.search(r'^card:\n', text, re.M):
        a = text.index('card:\n')
        b = text.find('\n\n', a)
        text = text[:a] + card + text[b:]
    else:
        text += '\n' + card
    a = text.index('flash-mcu:\n')
    at = text.index('\t@test -w $(COMX)', a)
    text = text[:at] + '''	@test ! $(BUILD)/fw/bl616.bin -nt $(FW_BIN) || { echo "build/fw/bl616.bin is newer; pass FW_BIN=build/fw/bl616.bin or update bin/bl616.bin" >&2; exit 1; }
	@test ! "$(COMPANION_ROOT)/build/fw/bl616.bin" -nt $(FW_BIN) || { echo "the shared firmware build is newer; run make fw and pass FW_BIN=build/fw/bl616.bin" >&2; exit 1; }
''' + text[at:]
    return text


def prepare():
    STAGE.mkdir(parents=True, exist_ok=True)
    plan = []
    diffs = []
    image = (ROOT / 'bin/bl616.bin').read_bytes()
    assert image == (ROOT / 'build/fw/bl616.bin').read_bytes(), 'shared shipped firmware is stale'
    for repo in REPOS:
        target_root = ROOT.parent / repo
        replacement = {
            'Makefile': makefile((target_root / 'Makefile').read_text()),
            'mnano/Makefile': MNANO_MAKE,
            'mnano/CMakeLists.txt': MNANO_CMAKE,
            'mnano/run_make': '#!/bin/sh\nset -eu\nexec make -C "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)" "$@"\n',
            'SHARED-COMPANION.md': README_NOTE.rstrip() + '\n',
        }
        for name in ['mnano/flash_prog_cfg.ini', 'mnano/misterynano_fw_bl616_cfg.ini']:
            old_ini = (target_root / name).read_text()
            replacement[name] = re.sub(r'^filedir\s*=.*$', 'filedir = ../bin/bl616.bin', old_ini, flags=re.M)
        readme = (target_root / 'README.md').read_text()
        # Preserve the existing document and introduce the new source/layout
        # before the old standalone-build history and reference text.
        first, sep, rest = readme.partition('\n')
        replacement['README.md'] = first + '\n\n' + README_NOTE + rest.lstrip('\n')
        mreadme = target_root / 'mnano/README.md'
        replacement['mnano/README.md'] = README_NOTE + '\n---\n\n' + mreadme.read_text()
        progress = target_root / '.claude/docs/progress.md'
        replacement['.claude/docs/progress.md'] = progress.read_text() + '''

## 16 October 2026: use the shared JSON companion

Top-level make fw, direct mnano Make and CMake now build the same companion
from ../tang-ultima. The shipped bin/bl616.bin is common to all five machines.
make card and card-config use the shared JSON paths. Raw flasher INIs point
to the shared shipped image rather than a possibly stale local build glob.
The local C sources remain historical references; firmware and menu tests
use the common implementation. FPGA sources and bitstreams are unchanged.
All three entry points were built in a scratch copy before applying this
integration and produced the identical common image. JSON card staging and
flash dry runs passed. No hardware was flashed or tested, no commits/pushes.
See SHARED-COMPANION.md and ../tang-ultima/config/README.md.
'''
        for name, new in replacement.items():
            dest = target_root / name
            old = dest.read_bytes() if dest.exists() else None
            staged = STAGE / repo / name
            staged.parent.mkdir(parents=True, exist_ok=True)
            staged.write_text(new)
            plan.append({'repo': repo, 'path': name, 'old_sha256': digest(old) if old is not None else None,
                         'new_sha256': digest(new.encode())})
            diffs += difflib.unified_diff(old.decode().splitlines(True) if old is not None else [],
                                         new.splitlines(True), fromfile=f'{repo}/{name}', tofile=f'{repo}/{name}')
        dest = target_root / 'bin/bl616.bin'
        staged = STAGE / repo / 'bin/bl616.bin'
        staged.parent.mkdir(parents=True, exist_ok=True)
        staged.write_bytes(image)
        plan.append({'repo': repo, 'path': 'bin/bl616.bin', 'old_sha256': digest(dest.read_bytes()), 'new_sha256': digest(image)})
    (STAGE / 'plan.json').write_text(json.dumps(plan, indent=2) + '\n')
    (ROOT / 'build/sibling-integration.patch').write_text(''.join(diffs))
    print(f'Prepared {len(plan)} files across five siblings; review build/sibling-integration.patch')


def apply():
    plan = json.loads((STAGE / 'plan.json').read_text())
    expected = set(REPOS)
    assert {p['repo'] for p in plan} == expected
    for item in plan:
        assert item['repo'] in expected and '..' not in Path(item['path']).parts and not Path(item['path']).is_absolute()
        dest = ROOT.parent / item['repo'] / item['path']
        staged = STAGE / item['repo'] / item['path']
        old = digest(dest.read_bytes()) if dest.exists() else None
        assert old == item['old_sha256'], f'{dest} changed after preparation; refusing to overwrite'
        assert digest(staged.read_bytes()) == item['new_sha256'], f'{staged} changed after preparation'
    for item in plan:
        dest = ROOT.parent / item['repo'] / item['path']
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_bytes((STAGE / item['repo'] / item['path']).read_bytes())
    print(f'Applied {len(plan)} reviewed files to all five siblings')


def verify():
    cmake = ROOT / 'tools/cmake/bin/cmake'
    results = []
    for name in REPOS:
        repo = ROOT.parent / name
        commands = [(['make', '-C', str(repo), 'fw'], 'fw'),
                    (['make', '-C', str(repo / 'mnano')], 'mnano'),
                    (['make', '-C', str(repo), 'card'], 'card'),
                    (['make', '-C', str(repo), 'card-config'], 'card-config'),
                    ([str(cmake), '-S', str(repo / 'mnano'), '-B', str(STAGE / name / 'actual-cmake')], 'cmake-configure'),
                    ([str(cmake), '--build', str(STAGE / name / 'actual-cmake')], 'cmake-build')]
        for command, label in commands:
            result = subprocess.run(command, capture_output=True, text=True)
            log = STAGE / f'{name}-actual-{label}.log'
            log.write_text(result.stdout + result.stderr)
            if result.returncode:
                raise SystemExit(f'{command} failed; see {log}\n' + result.stdout[-2500:] + result.stderr[-2500:])
        image = (ROOT / 'build/fw/bl616.bin').read_bytes()
        for path in [repo / 'build/fw/bl616.bin', repo / 'mnano/build/build_out/misterynano_fw_bl616.bin',
                     STAGE / name / 'actual-cmake/build_out/misterynano_fw_bl616.bin']:
            assert path.read_bytes() == image, f'{path} differs from the common build'
        assert (repo / 'build/card/companion.json').read_bytes() == (ROOT / 'config/companion.json').read_bytes()
        for core in ['uknc', 'pk8000', 'korvet', 'zs256', 'bk']:
            assert (repo / f'build/card/cores/{core}.json').read_bytes() == (ROOT / f'config/cores/{core}.json').read_bytes()
        dry = subprocess.run(['make','-n','-C',str(repo),'flash-mcu','COMX=/dev/not-a-device'], capture_output=True, text=True)
        assert dry.returncode == 0 and 'build/fw/bl616.bin is newer' in dry.stdout
        (STAGE / f'{name}-actual-flash-dry-run.log').write_text(dry.stdout + dry.stderr)
        results.append(f'{name}: Make, direct mnano, CMake, JSON card layout and flash dry run passed')
        print(results[-1], flush=True)
    # All builds must finish before publishing their identical result, so the
    # timestamp guards do not flag the earlier copies as older than a rebuild.
    image = (ROOT / 'build/fw/bl616.bin').read_bytes()
    assert image == (STAGE / REPOS[0] / 'bin/bl616.bin').read_bytes(), 'firmware changed after preparation'
    for repo in [ROOT, *[ROOT.parent / name for name in REPOS]]:
        (repo / 'bin/bl616.bin').write_bytes(image)
    (STAGE / 'actual-results.txt').write_text('\n'.join(results) + '\nSHA256 ' + digest(image) + '\n')
    entrypoints = ['Makefile', 'mnano/Makefile', 'mnano/CMakeLists.txt', 'mnano/run_make',
                   'mnano/flash_prog_cfg.ini', 'mnano/misterynano_fw_bl616_cfg.ini']
    record = {'version': 1, 'siblings': {
        name: {path: digest((ROOT.parent / name / path).read_bytes()) for path in entrypoints}
        for name in REPOS}}
    (ROOT / 'config/sibling-builds.lock.json').write_text(json.dumps(record, indent=2) + '\n')
    reply = ('All five siblings now use the shared JSON firmware through their Make, mnano and CMake entry points. '
             'Their shipped binaries are identical. Build and card checks passed. No flashing or commits; board validation remains.')
    for name in REPOS:
        prompts = ROOT.parent / name / 'prompts'
        numbers = [int(m.group(1)) for p in prompts.iterdir() if (m := re.match(r'(\d+)\s', p.name))]
        transcript = prompts / f'{max(numbers, default=0)+1} shared companion integration.txt'
        transcript.write_text('all siblings?\n\n' + '*'*72 + '\n\n' + reply + '\n')
    print('All six shipped images have SHA256 ' + digest(image), flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--apply', action='store_true', help='apply the previously prepared and reviewed files to the sibling repositories')
    parser.add_argument('--verify', action='store_true', help='build and stage cards in all five real siblings after applying')
    args = parser.parse_args()
    apply() if args.apply else (None if args.verify else prepare())
    if args.verify:
        verify()
