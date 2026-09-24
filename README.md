# Tang Ultima

Пять советских компьютеров в одном **Tang Nano 20K**: **УКНЦ**
(МС0511), **ПК8000 «Сура»**, **ПК8020 «Корвет»**, **Scorpion ZS-256
Turbo+** и **БК-0011М** с контроллером AZBK - и пункт меню, который
переключает плату из одного в другой за несколько секунд.  Каждая
машина - её собственный репозиторий
([UKNC Nano](https://github.com/lordamot/tang-uknc),
[PK8000 Nano](https://github.com/lordamot/tang-pk8000),
[Korvet Nano](https://github.com/lordamot/tang-korvet),
[ZS-256 Nano](https://github.com/lordamot/tang-zs256),
[BK Nano](https://github.com/lordamot/tang-bk-epta)); здесь - то, что
делает из пяти одну плату: одна программа для BL616, знающая все
пять, меню «Core» и прошивка для собственного BL616 платы, которая и
переключает.  Версия - в файле `VERSION`, история - в `CHANGELOG.md`,
лицензия - MIT (`LICENCE.md`).  *English below.*

**Состояние (24 сентября 2026): переключение работает на плате -
Корвет → ПК8000 → УКНЦ из меню, 13 сентября; ZS-256 добавлен и
запущен на плате 14 сентября; БК-0011М добавлен 24 сентября - собран,
проверен на хосте, на плате под этой прошивкой ещё не запускался.**

## Как это устроено

На Tang Nano 20K есть свой BL616 - USB-программатор платы, и JTAG ПЛИС
принадлежит ему.  Когда плата питается не от компьютера, прошивка
Sipeed (FPGA Partner) запускает в нём нашу вторую ступень (`onboard/`).
Меню «Core» посылает ей нужный битстрим с SD-карты (`/cores/<имя>.bin`)
через ядро - UART на выводах 69/70, - она держит его в своей флеш-памяти
и загружает в SRAM ПЛИС по JTAG за секунду; BL616 на доке перезапускается
и поднимается вместе с новой машиной.  Флеш-память ПЛИС при этом не
трогается.  При включении питания стартует то, что лежит во флеш-памяти
ПЛИС по адресу 0; последний пункт меню «Core», **Save to flash**,
записывает туда работающую машину (через выводы MSPI, `mister/flashwr.v`) -
это и переживает выключение.  **От компьютера переключения нет**: там
Partner остаётся программатором, и меню так и говорит.
`.claude/docs/onboard.md` и `coreswitch.md` - подробности и то, что было
испробовано до этого (MultiBoot на этой плате не запускается).

## Что нужно

- Tang Nano 20K, плата BL616 (M0S Dock), SD-карта FAT32, USB-клавиатура,
  питание не от компьютера (для переключения).  Провода между платами -
  те же семь, что у всех машин (распиновка MiSTeryNano: 42/41/56/54/51 ↔
  io10/11/12/13/14, звук на 71-74).
- Один раз: `make onboard-backup`, `make onboard-efuse`, вторая ступень в
  собственный BL616 платы (`make onboard-fw`, `make flash-mcu-onboard-stage2`)
  и одно ядро во флеш-память ПЛИС (`make flash-image`).  Дальше плата
  ставит ядра сама, с карты.
- Рядом с этим репозиторием - пять репозиториев машин (`../tang-uknc`,
  `../tang-pk8000`, `../tang-korvet`, `../tang-zs256`, `../tang-bk-epta`):
  ядра собираются из них.

## SD-карта

У каждой машины своя папка, и в ней всё, что раньше лежало в корне:

```
/ultima.ini          какая машина во флеш-памяти: core=uknc | pk8000 | korvet | zs256 | bk
/cores/uknc.bin      пять битстримов, из них меню и переключает
/cores/pk8000.bin
/cores/korvet.bin
/cores/zs256.bin
/cores/bk.bin
/uknc/               образы .dsk и .img, uknc.ini, RT11BASE.DSK, RT11SAV.DSK
/pk8000/             .cas, .fdd, .img, .rom, .bas, pk8000.ini
/korvet/             .kdi, .rom, korvet.ini, extrom/ (STAGE1.ROM, MOUNT.CFG, DISK/)
/zs256/              .trd, .img, zs256.ini, zs256.rom и gs105a.rom - ПЗУ машины,
                     без них она исполняет нули (в битстриме ПЗУ нет)
/bk/                 bk.ini и карточный пакет AZBK (MAXIOL): AZ.INI, ROM/, DISKS/,
                     eeprom.dat - ПЗУ машины тоже не в битстриме, без AZ.INI она
                     не стартует (../tang-bk-epta/soft/azbk/)
```

Папки создаются прошивкой при первом запуске машины, если их нет.
Файловый диалог каждой машины начинается в её папке и не выходит из
неё.

## Прошивка платы

```
make toolchain       инструменты в tools/ (~8 ГБ, один раз)
make cores           пять битстримов -> bin/<ядро>.fs и bin/<ядро>.bin
make card            что положить на карту
make fw              прошивка BL616 (док) -> build/fw/bl616.bin
make flash-image     openFPGALoader пишет одно ядро во флеш ПЛИС (один раз)
make flash-mcu       прошивка BL616 по UART (COMX=/dev/ttyACM0)
make onboard-fw      вторая ступень для BL616 платы -> build/onboard/stage2.bin
```

Готовые файлы лежат в `bin/`.  ПЛИС: переподключить кабель, прошить,
выключить-включить питание - именно в этом порядке (`.claude/docs/build.md`).
BL616: зажать BOOT, нажать RESET, отпустить BOOT, `make flash-mcu`.

## Меню

**F12** открывает меню той машины, которая работает, - такое же, как в
её собственном репозитории, - плюс пункт **Core**: список из пяти
машин, работающая отмечена, и **Save to flash**.  Выбор другой машины
закрывает образы, посылает её битстрим BL616 платы и перезапускает док;
через несколько секунд на экране другая машина.  «Save to flash» пишет
работающую машину во флеш ПЛИС - «Do not switch off!» на экране не
украшение.

---

# Tang Ultima (English)

Five Soviet computers in one **Tang Nano 20K** - the **UKNC** (МС0511),
the **PK8000 "Sura"**, the **PK8020 "Korvet"**, the **Scorpion ZS-256
Turbo+** and the **BK-0011M** with an AZBK controller - and a menu entry
that turns the board from one into another in seconds.  Each machine is
its own repository
([UKNC Nano](https://github.com/lordamot/tang-uknc),
[PK8000 Nano](https://github.com/lordamot/tang-pk8000),
[Korvet Nano](https://github.com/lordamot/tang-korvet),
[ZS-256 Nano](https://github.com/lordamot/tang-zs256),
[BK Nano](https://github.com/lordamot/tang-bk-epta)); this one holds
what makes five of them one board: one BL616 firmware that knows all
five, the "Core" form, and a firmware for the board's own BL616, which
does the switching.  Version in `VERSION`, history in `CHANGELOG.md`,
MIT (`LICENCE.md`).

**State (24 September 2026): the switch works on the board - Korvet →
PK8000 → UKNC from the OSD, 13 September; the ZS-256 was added and
seen running on the board on 14 September; the BK-0011M was added on
24 September - built and walked on the host, not yet run on a board
under this firmware.**

## How it works

The Tang Nano 20K has a BL616 of its own - the board's USB programmer -
and it owns the FPGA's JTAG.  When the board is not powered from a PC,
Sipeed's firmware on it (the FPGA Partner) starts our stage 2
(`onboard/`).  The "Core" form sends it the wanted bitstream from the
card (`/cores/<name>.bin`) through the core - a UART on pins 69/70 - it
keeps it in its own flash and loads it into the FPGA's SRAM over JTAG in
about a second; the dock's BL616 restarts and comes up with the new
machine.  The FPGA's flash is not touched.  Power-up loads whatever is
at the FPGA's flash address 0; the Core form's last entry, **Save to
flash**, writes the running machine there (through the MSPI pins,
`mister/flashwr.v`), and that is what survives a power cycle.  **There
is no switch on PC power**: there the Partner stays the programmer, and
the OSD says so.  `.claude/docs/onboard.md` and `coreswitch.md` have the
details and what was tried before (MultiBoot cannot be triggered on this
board).

## What you need

- A Tang Nano 20K, a BL616 board (M0S Dock), a FAT32 SD card, a USB
  keyboard, and power that is not a PC (for the switch).  The same seven
  wires as all the machines (MiSTeryNano's pinout: 42/41/56/54/51 ↔
  io10/11/12/13/14, audio on 71-74).
- Once: `make onboard-backup`, `make onboard-efuse`, stage 2 into the
  board's own BL616 (`make onboard-fw`, `make flash-mcu-onboard-stage2`),
  and one core into the FPGA's flash (`make flash-image`).  After that
  the board installs cores itself, from the card.
- The five machines' repositories beside this one (`../tang-uknc`,
  `../tang-pk8000`, `../tang-korvet`, `../tang-zs256`, `../tang-bk-epta`):
  the cores are built out of them.

## The SD card

Each machine has a directory of its own, holding what used to sit in
the card's root:

```
/ultima.ini          which machine the flash holds: core=uknc | pk8000 | korvet | zs256 | bk
/cores/uknc.bin      the five bitstreams, which is what the OSD switches between
/cores/pk8000.bin
/cores/korvet.bin
/cores/zs256.bin
/cores/bk.bin
/uknc/               .dsk and .img images, uknc.ini, RT11BASE.DSK, RT11SAV.DSK
/pk8000/             .cas, .fdd, .img, .rom, .bas, pk8000.ini
/korvet/             .kdi, .rom, korvet.ini, extrom/ (STAGE1.ROM, MOUNT.CFG, DISK/)
/zs256/              .trd, .img, zs256.ini, and zs256.rom and gs105a.rom - the
                     machine's ROMs; without them it executes zeros (none is in
                     the bitstream)
/bk/                 bk.ini and MAXIOL's AZBK card package: AZ.INI, ROM/, DISKS/,
                     eeprom.dat - the machine's ROMs are not in the bitstream
                     either, and without AZ.INI it does not start
                     (../tang-bk-epta/soft/azbk/)
```

The firmware creates a machine's directory the first time that machine
runs.  Each machine's file selector starts in its directory and does
not leave it.

## Flashing

```
make toolchain       the toolchain into tools/ (~8 GB, once)
make cores           the five bitstreams -> bin/<core>.fs and bin/<core>.bin
make card            what goes onto the card
make fw              the dock's BL616 firmware -> build/fw/bl616.bin
make flash-image     openFPGALoader writes one core into the FPGA's flash (once)
make flash-mcu       the BL616 over UART (COMX=/dev/ttyACM0)
make onboard-fw      stage 2 for the board's own BL616 -> build/onboard/stage2.bin
```

The built files are in `bin/`.  The FPGA: replug the cable, flash,
power-cycle - in that order (`.claude/docs/build.md`).  The BL616: hold
BOOT, tap RESET, release BOOT, `make flash-mcu`.

## The menu

**F12** opens the running machine's menu - the one its own repository
has - plus a **Core** form: the five machines, the running one marked,
and **Save to flash**.  Picking another machine closes the images, sends
its bitstream to the board's BL616 and restarts the dock; a few seconds
later the other machine is on the screen.  "Save to flash" writes the
running machine into the FPGA's flash - "Do not switch off!" on the
screen is not decoration.

## Acknowledgements

Alexey Gurov (UKNC Nano's hardware), Till Harbaum (MiSTeryNano, whose
firmware and MCU link all of this runs on), and everyone the five
machines' own About pages name.  Authors of this repository: Sergei
Lemeshev and Claude Code.
