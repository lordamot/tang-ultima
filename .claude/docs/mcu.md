# The BL616 firmware: `mnano/`

MiSTeryNano's firmware (Till Harbaum) as the three siblings carry it,
merged back into one: Korvet Nano's copy (the latest lineage - the
UTF-8 font, the Debug page, the ExtROM) with UKNC Nano's parts and
PK8000 Nano's parts put back in, plus `ultima.c`.  The merge is by hand
and stays by hand: a change to a core's menu, keys or card layout in its
own repository has to be carried here.

```
menu.c       every core's forms, variables and About text; the Core form; the
             UKNC's run-time main form, info lines and "Run SAV:"; the
             PK8000's "Run .bas:"; the Debug page (PK8000, Korvet)
usb_host.c   keymap[]/modifier[] for all three; the UKNC's matrix-tracking
             kbd_tx_uknc(); the UKNC's mouse-present notice
sdc.c        drivename() per core; the UKNC's IDE geometry; SDC_SLOT_EXTRA; the
             browser rooted at the core's directory; sdc_reattach()
sysctrl.c    CMD 6 as RTC (UKNC) and POKE (PK8000/Korvet), CMD 7 debug, CMD 8
             ExtROM, CMD 9 reconfig; sys_irq_hold; sys_reset_mcu()
spi.c        the interrupt task honours sys_irq_hold
ultima.c/h   the core table, /sd/ultima.ini, the walk (multiboot.md)
main.c       ultima_boot() before menu_init(); ten seconds for the FPGA
uknc.h pk8000.h korvet.h   the keymaps, verbatim from the siblings
rt11sav.c bas.c extrom.c   the UKNC's, PK8000's and Korvet's own, paths moved
```

## The core id

`sysctrl.v` answers CMD 0 with `5c 42 <id>`: 5 UKNC, 7 PK8000, 8 Korvet.
Everything indexed by it: `core_names[]`, `keymap[]`, `modifier[]`,
`settings_file_name()`, `drivename()`, `ultima_cores[]`, and the
dispatch in `menu_init()`.  The other MiSTeryNano cores' tables are still
there, untouched.

## The SD card

```
/ultima.ini                 core=uknc | pk8000 | korvet     (ultima.c)
/uknc/uknc.ini              the UKNC's settings              (settings_file_name)
/uknc/                      its file browser's root; RT11BASE.DSK, RT11SAV.DSK (rt11sav.h)
/pk8000/pk8000.ini          the PK8000's
/pk8000/                    its root; .bas files browsed from here
/korvet/korvet.ini          the Korvet's
/korvet/                    its root
/korvet/extrom/             STAGE1.ROM, MOUNT.CFG, DISK/     (extrom.h EXTROM_ROOT)
```

`ultima_root()` is `/sd/<dir>` of the running core; `sdc_readdir` starts
there and treats it as the root (no `..` above it, the "No Disk" entry
there).  A `drive<n>=/sd/...` line in an `.ini` may name any path on the
card.  `ultima_boot()` creates the running core's directory when it is
missing.

## The menu

Each core's forms are its own repository's (their `mcu.md` has the
letters) plus one entry on the main form, `S,Core,<n>` - 7 on the UKNC
(forms 0..6), 2 on the other two - opening the shared Core form:

```
Core
  UKNC      ●      'C' entries: the option field is the core id; the
  PK8000           running one carries the mark; selecting it does
  Korvet           nothing, selecting another calls ultima_switch()
```

`menu_select` case 'C' draws "Switching to <name> ..." before calling,
since nothing after draws anything: the FPGA reloads and the MCU
restarts.  The version at the right of every main form's caption is
`VERSION`'s first line.

`make menu-test` walks every form of every core on the host
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
6 RTC read (UKNC)   6 RAM poke (PK8000, Korvet)
7 debug window (PK8000, Korvet)   8 ExtROM channel (Korvet)
9 A5h: reconfigure                                                all three
```

## The switch, from the firmware's side

`ultima.c`, and `.claude/docs/multiboot.md` for the whole of it.  Three
rules the rest of the firmware keeps for it:

- `sys_irq_hold` up means: do not read the interrupt status (spi.c), do
  not reset on a coldboot notice (sysctrl.c).
- No image is open while the link may be dead (`ultima_switch` closes
  them; at start none is open yet).
- After the new core answers: `R=3`, acknowledge coldboot, `sdc_reattach`,
  then lift the hold - in that order.
