# Shared BL616 companion configuration

Build the dock firmware once in `tang-ultima` and use that same binary with
UKNC, PK8000, Korvet, ZS-256 and BK. The FPGA reports its core ID; the firmware
loads that machine's JSON profile from the SD card. No per-machine compiler
flags or companion binaries are needed. This is the M0S Dock firmware;
`onboard/` remains the separate second stage on the Tang's own BL616.

All five sibling repositories now delegate `make fw`, direct `make -C mnano`
and CMake builds to this implementation. Their shipped `bin/bl616.bin` files
are identical. Clone `tang-ultima` beside the siblings, or pass
`COMPANION_ROOT=/path/to/tang-ultima` to Make (CMake uses
`-DCOMPANION_ROOT=/path/to/tang-ultima`). The sibling `card`, `card-config`,
`menu-test`, `config-check` and `storage-test` targets use this configuration
as well. Their local C copies remain historical regression references.
Each sibling's `SHARED-COMPANION.md` explains the new entry points.
`config/sibling-builds.lock.json` records their build wrappers;
`make siblings-check` checks those wrappers and byte-compares all six shipped
firmware images. A firmware release should update the shipped binary in all
six repositories together.

```sh
make siblings-check       # compare all five sibling source snapshots
make config-check         # real firmware JSON parser and path checks on host
make storage-test         # production FatFs reader and loaders on a RAM disk
make menu-test            # all five menus, embedded and card-loaded profiles
make fw                   # build/fw/bl616.bin, the same image for all five
make card-stage           # collect existing bitstreams and sibling assets
```

`make card-stage` writes only `build/card/`. It collects the UKNC RT-11 base
and SAV programs, PK8000 sample images/programs, Korvet ExtROM package, ZS
ROMs, and the BK AZBK package. It reads siblings without changing them and
uses the JSON paths for every destination. Existing staged media/bitstreams
are kept; `python3 tools/stage_card.py --refresh` explicitly replaces them
with the current shipped copies. Config files always follow this checkout.
`make card-config` stages JSON alone. Copy the staged files to the SD card,
preserving any existing writable media and saved settings you want to keep.

The shipped layout is:

```text
/companion.json
/cores/uknc.json          /cores/uknc.bin
/cores/pk8000.json        /cores/pk8000.bin
/cores/korvet.json        /cores/korvet.bin
/cores/zs256.json         /cores/zs256.bin
/cores/bk.json            /cores/bk.bin
/uknc/                   uknc.ini, BASERT11.DSK, RT11SAV.DSK, programs/images
/pk8000/                 pk8000.ini, programs/images
/korvet/                 korvet.ini, extrom/
/zs256/                  zs256.ini, zs256.rom, gs105a.rom, images
/bk/                     bk.ini, AZ.INI, ROM/, DISKS/, eeprom.dat
```

Edit card JSON and restart the dock to apply it. Firmware rebuilding is
needed only for protocol/behavior changes, or to change the recovery defaults.
Without a card JSON file the firmware uses an embedded profile generated from
these exact files. An invalid override is rejected as a whole, logged over
the dock UART, and replaced by its embedded default. There is no live reload.

## Catalog: companion.json

`version` is the schema version, currently `1`. `cores` has these five entries
in order: IDs `5`, `7`, `8`, `9`, `10`. Each entry has:

| Field | Meaning |
| --- | --- |
| `id` | FPGA core ID; fixed by the protocol |
| `name` | Name in the common Core menu, at most 31 bytes |
| `dir` | Machine directory, relative to the SD root, at most 79 bytes |
| `settings` | Existing INI settings filename, relative to `dir`, at most 79 bytes |
| `bitstream` | Packed bitstream path, relative to the SD root, at most 119 bytes |
| `profile` | JSON profile path, relative to the SD root, at most 119 bytes |

For example, change UKNC's `dir` to `machines/uknc` and its `bitstream` to
`images/mc0511.bin`. Its browser, settings, SAV base and generated disk then
follow `/machines/uknc/`; switching reads `/images/mc0511.bin`. Staging uses
the same paths. Nested machine directories are created on boot.

Paths use `/`, have no `.` or `..` components, repeated/trailing separators,
drive letters or leading `/`. Directories, bitstream paths and profile paths
must be distinct between cores. The full firmware path limit is 255 bytes,
including the internal `/sd` mount prefix. Put resource subdirectories on
the card before using them.

## Machine profiles: cores/*.json

The shipped profiles were collected from the five siblings, including UKNC's
16 October 2026 `/uknc` layout. `siblings.lock.json` records source revisions,
versions and file hashes. `make siblings-check` reports upstream changes
that need review. After merging those changes, record a fresh snapshot with
`python3 tools/audit_siblings.py --record`. This does not rewrite profiles.

| Field | Meaning |
| --- | --- |
| `version`, `id`, `protocol` | Schema `1`, FPGA ID, compiled protocol name: `uknc`, `pk8000`, `korvet`, `zs256`, `bk` |
| `source` | Sibling repository used by the asset collector |
| `forms` | Menu forms, addressed by array index; main form is index 0 |
| `defaults` | Map of SYS variable letters to initial byte values |
| `keyboard` | `driver`, numeric HID-indexed `keymap`, eight `modifiers` |
| `drive_names` | Names for the five image slots |
| `resources` | File/directory names relative to the catalog's machine directory |
| `save_extra_slot` | Preserve browse slot 5 in settings; true for ZS ROM selection |
| `reset_on_change` | Variable letter to reset value, followed by `R=0` |
| `about` | Paragraphs in the About page |
| `assets` | Host staging plan: sibling `source` glob and destination directory or `resource` key |

The keyboard drivers are `uknc` for its scanned matrix, `ps2` for PK8000,
Korvet and ZS-256, and `bk` for BK's algorithmic translation. All mapped
machines take their byte tables from JSON, indexed by USB HID usage. Missing
keymap entries are zero. BK's translation, ROM upload protocols, special file
actions, RTC reads and debug decoding remain compiled protocol code. JSON
cannot introduce a new hardware protocol or change an ID into another one.

Each form has `title`, `parent` (null on main), and `entries`. Submenu parent
links must agree and reach main without cycles. Entry examples:

```json
{
  "title": "Hardware",
  "parent": 0,
  "entries": [
    {"type": "list", "label": "Volume:", "options": ["Mute", "33%", "66%", "100%"], "variable": "A"},
    {"type": "file", "label": "HDD0:", "slot": 4, "extensions": ["img"], "visible_if": "e"},
    {"type": "submenu", "label": "RTC clock", "form": 5},
    {"type": "button", "label": "Reset", "action": "R"},
    {"type": "text", "label": "About"}
  ]
}
```

`visible_if` names a variable; the entry appears when its value is positive.
Each list variable must exist in `defaults` and have a value within its option
range. The UKNC `info` entry has label `rtc`; `text` labels are `About` or
`Debug`. One form has `entries: [{"type": "cores"}]`, giving the shared Core
chooser and Save to flash entry. The main form links to it as a submenu.

Limits: 32 KB per JSON file, nesting 12, 16 forms, 64 entries per form,
64 variables, 256 keymap bytes, eight modifiers, 48 About paragraphs, 4095
bytes of compiled text per form. Labels and options cannot contain menu
separators (comma, semicolon, pipe) or control characters. Unknown metadata
is ignored by the firmware; required fields and enum values are validated.

## Paths and saved settings

The firmware accepts relative image paths beneath the machine directory,
existing `/sd/...` saved paths, and AZBK's `0:/...`, `/ROM/...` and backslash
forms. It rejects traversal and truncation. Paths stored by the OSD continue
using the existing INI format, so current Ultima cards and UKNC's
`/uknc/uknc.ini` remain compatible. A saved `/sd/...` image path can still
point outside its machine directory for compatibility with older cards.
INI values outside the JSON menu's ranges revert to JSON defaults.

Machine-specific state formats remain intact: `AZ.INI` and `eeprom.dat` are
the AZBK's formats, ExtROM keeps its `MOUNT.CFG`, and `/ultima.ini` records
what Save to flash installed. They are state, separate from the readable
firmware configuration. Existing standalone cards with root-level files
should move those files and root-level settings into the corresponding
machine folder. Their INI contents are preserved.

## Verification and board status

Host checks validate JSON overrides, fallback, path relocation, malformed
input rejection, keyboard edits, conditional menus and every form of all
five machines. The storage test formats a RAM disk with real FatFs and checks
the production card JSON reader, a SAV built in a relocated folder, disk
mount/eject, fragmented cluster maps, custom ZS ROM paths and BK's legacy
paths. They do not exercise actual SD/JTAG hardware. A firmware
build is not evidence of a board boot or switch; test the new binary on
the board before treating this refactor as hardware verified.
