# The BL616 firmware: `mnano/`

MiSTeryNano's firmware (Till Harbaum) as the five siblings carry it,
merged back into one: Korvet Nano's copy (the latest lineage - the
UTF-8 font, the Debug page, the ExtROM) with UKNC Nano's parts and
PK8000 Nano's parts put back in, then ZS-256 Nano's (14 Sep 2026: its
forms, keymap, `romload.c`, its Debug page, and its set-wise key-report
compare in `usb_host.c`, which now serves every core), then BK Nano's
(24 Sep 2026: `azbk.c`/`bk.c` verbatim, its forms and Debug page, the
`kbd_tx_bk()` branches, `sys_peek24`, irq 4 to the AZ, and two generic
changes - the SPI task's 2048-word stack and the bounded card waits),
plus `ultima.c`.  The merge is by hand
and stays by hand: a change to a core's menu, keys or card layout in its
own repository has to be carried here.

```
menu.c       every core's forms, variables and About text; the Core form; the
             UKNC's run-time main form, info lines and "Run SAV:"; the
             PK8000's "Run .bas:"; the ZS-256's "ROM:"; the BK's AZ units
             (menu_bk_mount/menu_bk_boot -> azbk.c); the Debug page
             (PK8000/Korvet's byte map, the ZS-256's own, the BK's own)
usb_host.c   keymap[]/modifier[] for all five (NULL for the BK); the UKNC's
             matrix-tracking kbd_tx_uknc(); the BK's kbd_tx_bk() on all three
             key paths; the UKNC's mouse-present notice; the six key
             slots compared as a set (ZS-256 Nano's fix)
sdc.c        drivename() per core; the UKNC's IDE geometry; SDC_SLOT_EXTRA; the
             browser rooted at the core's directory; sdc_reattach(); the
             bounded sector waits and sdc_timeouts() (BK Nano's)
sysctrl.c    CMD 6 as RTC (UKNC), POKE (PK8000/Korvet) and the 24-bit POKE
             (ZS-256/BK, sys_poke24), CMD 7 debug, CMD 8 ExtROM (Korvet) or
             PEEK (BK, sys_peek24), CMD 9 reconfig (dormant); irq 4 to the
             ExtROM or the AZ by core_id; sys_irq_hold; sys_reset_mcu()
spi.c        the interrupt task honours sys_irq_hold; its stack is 2048 words
             (the BK's AZ service runs FatFs from it)
ultima.c/h   the core table, /sd/ultima.ini, the install (coreswitch.md)
flashwr.c/h  the W25Q64 over SYS CMD 10: what puts a core at flash address 0
main.c       ultima_boot() before menu_init(); ten seconds for the FPGA
uknc.h pk8000.h korvet.h zs256.h   the keymaps, verbatim from the siblings
bk.h bk.c    the BK's keyboard: USB HID -> КОИ-7 codes and flags, with the
             РУС/ЛАТ/СТР state on the MCU - verbatim from BK Nano
azbk.h azbk.c  the AZ controller's STM32 side: AZ.INI, the ROMs into the
             SDRAM (sys_poke24, verified back with sys_peek24), the units,
             the commands the FPGA hands over on irq 4 through SPI target 4 -
             verbatim from BK Nano; its AZ_ROOT "/sd/bk" is ultima_cores[]'s
             "bk" by hand
rt11sav.c bas.c extrom.c romload.c  the UKNC's, PK8000's, Korvet's and ZS-256's
             own, paths moved under the core's directory
```

## The core id

`sysctrl.v` answers CMD 0 with `5c 42 <id>`: 5 UKNC, 7 PK8000, 8 Korvet,
9 ZS-256, 10 BK.
Everything indexed by it: `core_names[]`, `keymap[]`, `modifier[]`,
`settings_file_name()`, `drivename()`, `ultima_cores[]`, the Debug
page, and the dispatch in `menu_init()`.  The other MiSTeryNano cores' tables are still
there, untouched.

## The SD card

```
/ultima.ini                 core=uknc | pk8000 | korvet | zs256 | bk   (ultima.c)
/cores/uknc.bin             the five machines as packed bitstreams; the OSD
/cores/pk8000.bin           sends one to the board's BL616 to load into the
/cores/korvet.bin           FPGA's SRAM (coreload.c), or "Save to flash" writes
/cores/zs256.bin            the running one to flash address 0 (flashwr.c)
/cores/bk.bin
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
/bk/bk.ini                  the BK's; drive0..3= name the AZ units chosen in the OSD
/bk/                        its root, and the AZ controller's (azbk.h AZ_ROOT):
/bk/AZ.INI                  MAXIOL's card package - [ROM] Rnn= files sent into the
/bk/ROM/ /bk/DISKS/         SDRAM at start over sys_poke24 and read back over
/bk/eeprom.dat              sys_peek24, [DISKS] Dn= the units, the EEPROM image;
                            the bitstream carries no ROM (../tang-bk-epta/soft/azbk/)
```

`ultima_root()` is `/sd/<dir>` of the running core; `sdc_readdir` starts
there and treats it as the root (no `..` above it, the "No Disk" entry
there).  A `drive<n>=/sd/...` line in an `.ini` may name any path on the
card.  `ultima_boot()` creates the running core's directory when it is
missing.

## The menu

Each core's forms are its own repository's (their `mcu.md` has the
letters) plus one entry on the main form, `S,Core,<n>` - 7 on the UKNC
(forms 0..6), 2 on the other four - opening the shared Core form:

```
Core
  UKNC      ●      'C' entries: the option field is the core id; the
  PK8000           running one carries the mark; selecting it does
  Korvet           nothing, selecting another calls ultima_switch()
  ZS-256
  BK-0011M
  Save to flash    id 0: ultima_install(), the running core -> address 0
```

The BK's main form is its four AZ units (`F` entries on slots 0..3):
a file picked there is not an `sd_card.v` image - `menu_bk_mount()`
remembers the name in the slot, as the settings need, and gives the
card path to `az_set_unit()`; "No Disk" unmounts the same way.  At
start `menu_bk_boot()` runs between `R=3` and `R=0`: `az_boot()` reads
`AZ.INI` and loads the ROMs with the machine held, then the saved
`drive<n>=` names override units 0..3.

`menu_select` case 'C' draws "Switching to <name> ..." before calling,
since nothing after draws anything: the FPGA reloads and the MCU
restarts.  The version at the right of every main form's caption is
`VERSION`'s first line.

`make menu-test` walks every form of every core on the host (five)
(`menu_test.c`): each 'S' entered and left by its title back to its
entry, each 'L' stepped a full circle both ways with the core told each
time, each 'F' opened on its slot with its extensions and closed, each
'T' opened and closed, Reset pulsed; then the Core form on each core.
Screens as text and PNG under `build/menu/`, `<n>-<core>-<form>`.

## The SPI link

Mode 1, 20 MHz, four targets by the first byte (0 SYS, 1 HID, 2 OSD, 3
SDC) and a fifth on the BK alone (4 AZ, `azbk.h`, azctrl.v's status,
buffer reads and writes, done, reset); UKNC Nano's `.claude/docs/mcu.md`
has the byte-level protocol of the first four.  SYS commands, by core:

```
0 status   1 leds   2 rgb   3 buttons   4 set value   5 irq     all
6 RTC read (UKNC)   6 RAM poke (PK8000, Korvet)   6 SDRAM poke, 24-bit (ZS-256, BK)
7 debug window (PK8000, Korvet, ZS-256, BK)   8 ExtROM channel (Korvet)   8 SDRAM peek (BK)
9 A5h: reconfigure (dormant)   10 the flash   11 the coreload UART   all five
```

Interrupt 4 is the ExtROM's on the Korvet and the AZ controller's on the
BK; `sys_handle_interrupts()` sends it to `extrom_handle_event()` or
`az_handle_event()` by `core_id`.  The BK's keyboard event is two bytes
on HID target 1 (the КОИ-7 code and the flags, `bk.c`), where every
other core's is one.

## The switch, from the firmware's side

`ultima.c`, and `.claude/docs/coreswitch.md` for the whole of it.  Three
rules the rest of the firmware keeps for it:

- `sys_irq_hold` up means: do not read the interrupt status (spi.c), do
  not reset on a coldboot notice (sysctrl.c).
- No image is open while the link may be dead (`ultima_switch` closes
  them; at start none is open yet).
- After the new core answers: `R=3`, acknowledge coldboot, `sdc_reattach`,
  then lift the hold - in that order.
