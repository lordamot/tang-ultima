# The BL616 firmware: `mnano/`

MiSTeryNano's firmware (Till Harbaum) as the four siblings carry it,
merged back into one: Korvet Nano's copy (the latest lineage - the
UTF-8 font, the Debug page, the ExtROM) with UKNC Nano's parts and
PK8000 Nano's parts put back in, then ZS-256 Nano's (14 Sep 2026: its
forms, keymap, `romload.c`, its Debug page, and its set-wise key-report
compare in `usb_host.c`, which now serves every core), plus `ultima.c`.  The merge is by hand
and stays by hand: a change to a core's menu, keys or card layout in its
own repository has to be carried here.

```
menu.c       every core's forms, variables and About text; the Core form; the
             UKNC's run-time main form, info lines and "Run SAV:"; the
             PK8000's "Run .bas:"; the ZS-256's "ROM:"; the Debug page
             (PK8000/Korvet's byte map, and the ZS-256's own)
usb_host.c   keymap[]/modifier[] for all four; the UKNC's matrix-tracking
             kbd_tx_uknc(); the UKNC's mouse-present notice; the six key
             slots compared as a set (ZS-256 Nano's fix)
sdc.c        drivename() per core; the UKNC's IDE geometry; SDC_SLOT_EXTRA; the
             browser rooted at the core's directory; sdc_reattach()
sysctrl.c    CMD 6 as RTC (UKNC), POKE (PK8000/Korvet) and the 24-bit POKE
             (ZS-256, sys_poke24), CMD 7 debug, CMD 8 ExtROM, CMD 9 reconfig
             (dormant); sys_irq_hold; sys_reset_mcu()
spi.c        the interrupt task honours sys_irq_hold
ultima.c/h   the core table, /sd/ultima.ini, the install (coreswitch.md)
flashwr.c/h  the W25Q64 over SYS CMD 10: what puts a core at flash address 0
main.c       ultima_boot() before menu_init(); ten seconds for the FPGA
uknc.h pk8000.h korvet.h zs256.h   the keymaps, verbatim from the siblings
rt11sav.c bas.c extrom.c romload.c  the UKNC's, PK8000's, Korvet's and ZS-256's
             own, paths moved under the core's directory
```

## The core id

`sysctrl.v` answers CMD 0 with `5c 42 <id>`: 5 UKNC, 7 PK8000, 8 Korvet,
9 ZS-256.
Everything indexed by it: `core_names[]`, `keymap[]`, `modifier[]`,
`settings_file_name()`, `drivename()`, `ultima_cores[]`, the Debug
page, and the dispatch in `menu_init()`.  The other MiSTeryNano cores' tables are still
there, untouched.

## The SD card

```
/ultima.ini                 core=uknc | pk8000 | korvet | zs256   (ultima.c)
/cores/uknc.bin             the four machines as packed bitstreams; the OSD
/cores/pk8000.bin           sends one to the board's BL616 to load into the
/cores/korvet.bin           FPGA's SRAM (coreload.c), or "Save to flash" writes
/cores/zs256.bin            the running one to flash address 0 (flashwr.c)
/uknc/uknc.ini              the UKNC's settings              (settings_file_name)
/uknc/                      its file browser's root; RT11BASE.DSK, RT11SAV.DSK (rt11sav.h)
/pk8000/pk8000.ini          the PK8000's
/pk8000/                    its root; .bas files browsed from here
/korvet/korvet.ini          the Korvet's
/korvet/                    its root
/korvet/extrom/             STAGE1.ROM, MOUNT.CFG, DISK/     (extrom.h EXTROM_ROOT)
/zs256/zs256.ini            the ZS-256's; drive5= names its ROM file
/zs256/                     its root; .trd and .img images
/zs256/zs256.rom            the machine's ROM, 64 KB or a 256 KB ProfROM, and
/zs256/gs105a.rom           General Sound's - sent into the SDRAM at start
                            (romload.c); the bitstream carries no ROM
```

`ultima_root()` is `/sd/<dir>` of the running core; `sdc_readdir` starts
there and treats it as the root (no `..` above it, the "No Disk" entry
there).  A `drive<n>=/sd/...` line in an `.ini` may name any path on the
card.  `ultima_boot()` creates the running core's directory when it is
missing.

## The menu

Each core's forms are its own repository's (their `mcu.md` has the
letters) plus one entry on the main form, `S,Core,<n>` - 7 on the UKNC
(forms 0..6), 2 on the other three - opening the shared Core form:

```
Core
  UKNC      ●      'C' entries: the option field is the core id; the
  PK8000           running one carries the mark; selecting it does
  Korvet           nothing, selecting another calls ultima_switch()
  ZS-256
  Save to flash    id 0: ultima_install(), the running core -> address 0
```

`menu_select` case 'C' draws "Switching to <name> ..." before calling,
since nothing after draws anything: the FPGA reloads and the MCU
restarts.  The version at the right of every main form's caption is
`VERSION`'s first line.

`make menu-test` walks every form of every core on the host (four)
(`menu_test.c`): each 'S' entered and left by its title back to its
entry, each 'L' stepped a full circle both ways with the core told each
time, each 'F' opened on its slot with its extensions and closed, each
'T' opened and closed, Reset pulsed; then the Core form on each core.
Screens as text and PNG under `build/menu/`, `<n>-<core>-<form>`.

## The SPI link

Mode 1, 20 MHz, four targets by the first byte (0 SYS, 1 HID, 2 OSD, 3
SDC); UKNC Nano's `.claude/docs/mcu.md` has the byte-level protocol.
SYS commands, by core:

```
0 status   1 leds   2 rgb   3 buttons   4 set value   5 irq     all
6 RTC read (UKNC)   6 RAM poke (PK8000, Korvet)   6 SDRAM poke, 24-bit (ZS-256)
7 debug window (PK8000, Korvet, ZS-256)   8 ExtROM channel (Korvet)
9 A5h: reconfigure (dormant)   10 the flash   11 the coreload UART   all four
```

## The switch, from the firmware's side

`ultima.c`, and `.claude/docs/coreswitch.md` for the whole of it.  Three
rules the rest of the firmware keeps for it:

- `sys_irq_hold` up means: do not read the interrupt status (spi.c), do
  not reset on a coldboot notice (sysctrl.c).
- No image is open while the link may be dead (`ultima_switch` closes
  them; at start none is open yet).
- After the new core answers: `R=3`, acknowledge coldboot, `sdc_reattach`,
  then lift the hold - in that order.
