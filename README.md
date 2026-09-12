# Tang Ultima

Три советских компьютера в одном **Tang Nano 20K**: **УКНЦ** (МС0511),
**ПК8000 «Сура»** и **ПК8020 «Корвет»** - и пункт меню, который
переключает плату из одного в другой.  Каждая машина - её собственный
репозиторий ([UKNC Nano](https://github.com/lordamot/tang-uknc),
[PK8000 Nano](https://github.com/lordamot/tang-pk8000),
[Korvet Nano](https://github.com/lordamot/tang-korvet)); здесь - то, что
делает из трёх одну плату: три прошивки ПЛИС в одной флеш-памяти, одна
программа для BL616, знающая все три, и меню «Core».  Версия - в файле
`VERSION`, история - в `CHANGELOG.md`, лицензия - MIT (`LICENCE.md`).
*English below.*

**Состояние (12 сентября 2026): собирается, все три ядра проходят
временной анализ, прошивка собирается, меню проверено на хосте.  На
плате ещё не запускалось - в том числе сам механизм переключения.**

## Как это устроено

Флеш-память Tang Nano 20K (8 МБ) хранит три битстрима по мегабайту:
УКНЦ по адресу 0, ПК8000 по 0x100000, Корвет по 0x200000.  У Gowin
есть **MultiBoot**: в заголовке каждого битстрима записан адрес
следующего, а импульс на **RECONFIG_N** заставляет ПЛИС загрузить его.
Ядро само дёргает RECONFIG_N (вывод 9, здесь он GPIO) по команде 9 от
BL616, и ПЛИС загружает следующую машину по кольцу УКНЦ → ПК8000 →
Корвет → УКНЦ.  При включении всегда стартует УКНЦ (адрес 0); прошивка
BL616 читает с карты, какая машина нужна (`/ultima.ini`), и «прыгает»
по кольцу, пока не ответит нужная - не больше двух прыжков, около трёх
секунд каждый.  Меню «Core» записывает выбор на карту и запускает то же
самое.

## Что нужно

- Tang Nano 20K, плата BL616 (M0S Dock), SD-карта FAT32, USB-клавиатура.
  Провода между платами - те же семь, что у всех трёх машин (распиновка
  MiSTeryNano: 42/41/56/54/51 ↔ io10/11/12/13/14, звук на 71-74).
- Рядом с этим репозиторием - три репозитория машин (`../tang-uknc`,
  `../tang-pk8000`, `../tang-korvet`): ядра собираются из них.

## SD-карта

У каждой машины своя папка, и в ней всё, что раньше лежало в корне:

```
/ultima.ini          какую машину запускать: core=uknc | pk8000 | korvet
/uknc/               образы .dsk и .img, uknc.ini, RT11BASE.DSK, RT11SAV.DSK
/pk8000/             .cas, .fdd, .img, .rom, .bas, pk8000.ini
/korvet/             .kdi, .rom, korvet.ini, extrom/ (STAGE1.ROM, MOUNT.CFG, DISK/)
```

Папки создаются прошивкой при первом запуске машины, если их нет.
Файловый диалог каждой машины начинается в её папке и не выходит из
неё.

## Прошивка платы

```
make toolchain       инструменты в tools/ (~8 ГБ, один раз)
make cores           три битстрима -> bin/uknc.fs, bin/pk8000.fs, bin/korvet.fs
                     и bin/ultima.bin - все три в одном образе флеш-памяти
make fw              прошивка BL616 -> build/fw/bl616.bin
make flash-image     openFPGALoader пишет bin/ultima.bin во флеш (один раз, все три)
make flash-mcu       прошивка BL616 по UART (COMX=/dev/ttyACM0)
```

Готовые файлы лежат в `bin/`.  ПЛИС: переподключить кабель, прошить,
выключить-включить питание - именно в этом порядке (`.claude/docs/build.md`).
BL616: зажать BOOT, нажать RESET, отпустить BOOT, `make flash-mcu`.

## Меню

**F12** открывает меню той машины, которая работает, - такое же, как в
её собственном репозитории, - плюс пункт **Core**: список из трёх
машин, работающая отмечена.  Выбор другой пишет `/ultima.ini`,
закрывает образы и перезагружает ПЛИС; через несколько секунд на экране
другая машина, и BL616 поднимается вместе с ней.

---

# Tang Ultima (English)

Three Soviet computers in one **Tang Nano 20K** - the **UKNC** (МС0511),
the **PK8000 "Sura"** and the **PK8020 "Korvet"** - and a menu entry
that turns the board from one into another.  Each machine is its own
repository ([UKNC Nano](https://github.com/lordamot/tang-uknc),
[PK8000 Nano](https://github.com/lordamot/tang-pk8000),
[Korvet Nano](https://github.com/lordamot/tang-korvet)); this one holds
what makes three of them one board: the three bitstreams in one flash,
one BL616 firmware that knows all three, and the "Core" form.  Version
in `VERSION`, history in `CHANGELOG.md`, MIT (`LICENCE.md`).

**State (12 September 2026): builds, all three cores pass their timing
gates, the firmware builds, the menu is checked on the host.  Nothing
has run on a board yet - the switching mechanism included.**

## How it works

The Tang Nano 20K's 8 MB flash holds the three bitstreams at 1 MB slots:
UKNC at 0, PK8000 at 0x100000, Korvet at 0x200000.  Gowin's
**MultiBoot** (UG290 §7.5.4) puts the address of the *next* bitstream
into each bitstream's header, and a low pulse on **RECONFIG_N** makes
the FPGA load it.  The core pulses RECONFIG_N itself (pin 9, a GPIO
here) on SYS command 9 from the BL616, and the FPGA loads the next
machine round the ring UKNC → PK8000 → Korvet → UKNC.  Power-up always
starts the UKNC (address 0); the firmware reads which machine the card
asks for (`/ultima.ini`) and hops the ring until that one answers - two
hops at most, about three seconds each at Gowin's default loading
rate.  The "Core" form writes the wish to the card and does the same.

## What you need

- A Tang Nano 20K, a BL616 board (M0S Dock), a FAT32 SD card, a USB
  keyboard.  The same seven wires as all three machines (MiSTeryNano's
  pinout: 42/41/56/54/51 ↔ io10/11/12/13/14, audio on 71-74).
- The three machines' repositories beside this one (`../tang-uknc`,
  `../tang-pk8000`, `../tang-korvet`): the cores are built out of them.

## The SD card

Each machine has a directory of its own, holding what used to sit in
the card's root:

```
/ultima.ini          which machine to run: core=uknc | pk8000 | korvet
/uknc/               .dsk and .img images, uknc.ini, RT11BASE.DSK, RT11SAV.DSK
/pk8000/             .cas, .fdd, .img, .rom, .bas, pk8000.ini
/korvet/             .kdi, .rom, korvet.ini, extrom/ (STAGE1.ROM, MOUNT.CFG, DISK/)
```

The firmware creates a machine's directory the first time that machine
runs.  Each machine's file selector starts in its directory and does
not leave it.

## Flashing

```
make toolchain       the toolchain into tools/ (~8 GB, once)
make cores           the three bitstreams -> bin/uknc.fs, bin/pk8000.fs, bin/korvet.fs
                     and bin/ultima.bin - all three in one flash image
make fw              the BL616 firmware -> build/fw/bl616.bin
make flash-image     openFPGALoader writes bin/ultima.bin to the flash (once, all three)
make flash-mcu       the BL616 over UART (COMX=/dev/ttyACM0)
```

The built files are in `bin/`.  The FPGA: replug the cable, flash,
power-cycle - in that order (`.claude/docs/build.md`).  The BL616: hold
BOOT, tap RESET, release BOOT, `make flash-mcu`.

## The menu

**F12** opens the running machine's menu - the one its own repository
has - plus a **Core** form: the three machines, the running one marked.
Picking another writes `/ultima.ini`, closes the images and reloads the
FPGA; a few seconds later the other machine is on the screen and the
BL616 has come up with it.

## Acknowledgements

Alexey Gurov (UKNC Nano's hardware), Till Harbaum (MiSTeryNano, whose
firmware and MCU link all of this runs on), and everyone the three
machines' own About pages name.  Authors of this repository: Sergei
Lemeshev and Claude Code.
