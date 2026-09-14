# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working in
this repository.

## Project overview

**Tang Ultima** puts four machines into one **Tang Nano 20K** (Gowin
GW2AR-18C) with a **BL616** (M0S Dock) beside it: **UKNC Nano**
(`../tang-uknc`, МС0511), **PK8000 Nano** (`../tang-pk8000`, ПК8000),
**Korvet Nano** (`../tang-korvet`, ПК8020) and **ZS-256 Nano**
(`../tang-zs256`, Scorpion ZS-256 Turbo+, added 14 September 2026).  The
machines live in those four repositories and are not copied here.  This repository holds
what makes them one board:

```
Makefile      builds each core OUT OF its sibling's tree into build/cores/<core>/,
              gates it with the sibling's own timing check, and packs each to
              bin/<core>.bin - one core a file, no slots
mnano/        ONE BL616 firmware for all four cores (MiSTeryNano's, merged
              from the siblings' copies) plus ultima.c, coreload.c (the
              switch) and flashwr.c ("Save to flash")
onboard/      stage 2 for the board's OWN BL616: the other end of the switch -
              takes a core over a UART, keeps it in its flash, loads it into
              the FPGA's SRAM over JTAG
tools/        mkimage.py (one .fs -> the bytes the flash holds, checked);
              mkstage.py (a core in stage 2's form, and the log decoder);
              onboard.sh and efuse_bl616.py for the board's own BL616; the
              fetched toolchain, hard-linked from a sibling on this host, gitignored
bin/          <core>.fs and <core>.bin for uknc, pk8000, korvet, zs256, and
              bl616.bin - what a user flashes and what goes on the card;
              onboard/ - the factory and FPGA Partner images for the
              on-board BL616, fetched from upstream, and its backups
```

Started 12 September 2026.  **The switch works on a board**, as of the
evening of 13 September: Korvet -> PK8000 -> UKNC, each chosen from the
OSD and running seconds later, no flash written, no power cycle.  It took
three designs to get there - Gowin MultiBoot, which cannot be triggered
on this board at all; writing the FPGA's flash and power-cycling, which
works and is kept as "Save to flash"; and the board's own BL616 loading
the FPGA's SRAM, which is the switch - and `.claude/docs/handover.md` is
where to start after a break, and `.claude/docs/progress.md` is the
record of all three and must stay one.  Still say which claim you are
making: "it builds", "it lints", "the menu walks on the host" and "it
meets timing" are four claims, none of them "it works".

## The mechanism, in one paragraph

**The board's own BL616 owns the FPGA's JTAG, and JTAG can load the
FPGA's SRAM.**  Sipeed's FPGA Partner sits at that chip's flash address
0 and, when the board is not on a PC, starts **our stage 2** (`onboard/`)
at 0x40000.  The dock's firmware sends the wanted core from the card to
it - `mnano/coreload.c` over **SYS CMD 11**, `mister/coreload.v` in every
core: a 2 KB TX FIFO into a 2 Mbaud UART on pin 69, one-byte answers
back on pin 70 - stage 2 keeps it in its own 4 MB flash at 0x100000
(`onboard/protocol.h`: B, 4 KB acknowledged chunks, crc; then L), erases
the FPGA's SRAM and writes the bitstream through the JTAG in 1.1 s, and
the dock resets itself onto the new machine.  The FPGA's own flash is
never touched by a switch.  Power-up still loads flash address 0, so the
Core form's last entry, **"Save to flash"**, writes the running machine
there - `mnano/flashwr.c` over **SYS CMD 10**, `mister/flashwr.v` on the
MSPI pins 59-62 that `-use_mspi_as_gpio` hands to user logic - and that
is what survives a power cycle.  `.claude/docs/onboard.md` is the
account of the chip and the switch, `coreswitch.md` of the flash writer
and the routes that were weighed.  **The switch only exists on non-PC
power**: on a PC the Partner stays the programmer and stage 2 never
runs, and the OSD says "Not on PC power?".

**Why not MultiBoot:** the jump address can be put in every header, but
the trigger does not exist.  A low pulse on RECONFIG_N should reload the
FPGA and on this board it does nothing - the pulse is provably generated
(an LED latched off `sys_reconfig` keeps blinking through CMD 9) and pin 9
reaches nothing but test pad TP1, so nothing external holds it up.
Reusing the pad as a GPIO cuts it from the configuration controller.
`.claude/docs/multiboot.md` keeps that account; **one wire from header pin
48 to TP1 would bring the instant switch back**, and everything needed for
it is still in the three cores.

## Traps worth remembering

- **Writing flash address 0 is writing the only thing the board can
  boot.**  `flash_install()` refuses before it erases - a Winbond JEDEC
  ID on the MSPI pins, a file between 64 KB and 1 MB, `a5 c3` at 0x16 and
  IDCODE `0x0000081b` at 0x1c - and verifies after.  `tools/mkimage.py`
  checks the same two bitstream fields when it makes the file, so the two
  ends agree by construction.  A power cut between the erase and the end
  of the write is a board that needs openFPGALoader; the OSD says "Do not
  switch off!" throughout, and that is not decoration.
- **The siblings are read, never written, by a build here.**  gw_sh
  runs in `build/cores/<core>/` with absolute source paths
  (`gowin_tcl.py --abs`), so their `tang/impl/pnr/`, `tang/build.tcl`
  and `bin/tang.fs` stay theirs.  What IS in the siblings' trees, as a
  generic feature: `mister/flashwr.v` and sysctrl's **CMD 10**, the four
  MSPI pins with `"MSPI" : true` in the process config, the `--abs`
  option of `gowin_tcl.py`, the PnR-dir argument of `timing_check.py`,
  `mister/coreload.v` and **CMD 11** on pins 69/70, and the now-dormant
  CMD 9 / `reconfig_n` on pin 48.  ZS-256 Nano was written with all of
  it from the start.
- **openFPGALoader is needed once.**  `make flash-image` writes
  `bin/$(DEFAULT_CORE).bin` to address 0 (`--file-type bin -o 0`);
  replug, flash, power-cycle, in that order.  After that the board
  installs cores itself from the card, and the host tool is only for
  recovery.  The packed `.bin` is byte-identical to Gowin's own (checked
  against `impl/pnr/*.bin`), and `--file-type bin -o 0` is **verified**
  to write correctly - read back over JTAG and compared, 13 Sep 2026.
- **`make flash-mcu` writes `bin/bl616.bin`, not `build/fw/bl616.bin`.**
  `make fw` leaves the firmware in `build/fw/` and it is copied to
  `bin/` by hand.  On 13 Sep 2026 the stale `bin/` copy went onto the
  dock and the board "regressed" to writing its flash; the target now
  refuses when `build/fw/` is newer.  And it writes whatever BL616 is
  on `/dev/ttyACM0` - with the Tang in boot mode that is the board's
  own chip, so the Tang is unplugged first.
- **A switch ends in `sys_reset_mcu()`.**  `ultima_switch()` pings,
  sends, says load, and the dock restarts on the new machine; the card,
  every image and the link are gone with the old one, so it closes every
  image first and holds the interrupt task (`sys_irq_hold`) before the
  load.  It never writes the FPGA's flash; `ultima_install()` ("Save to
  flash") does, and that is the one that must not be interrupted.
  `ultima_boot()` reports which machine came up, makes its card
  directory, and warns if `/sd/ultima.ini` names a different core.
- **Stage 2 is blind and its log is the only witness.**  Nothing it does
  is visible from a PC, so it writes (tag, value) pairs into its flash
  at 0x0FE000 - every step of a load, the first 32 bytes each way on
  the link, when it started listening - and `make onboard-log` (UPDATE
  held) reads them back.  One power-up is one log.
- **SYS CMD 6 means three things.**  RTC read on the UKNC, RAM poke on
  the PK8000 and Korvet (two address bytes), SDRAM poke on the ZS-256
  (three address bytes, `sys_poke24`, the ROM loader's); `spi.h` defines
  `SPI_SYS_RTC` and `SPI_SYS_POKE` both as 6, and the firmware only
  sends each to its own core.  CMD 9, 10 and 11 are the only commands
  that are the same on all four.
- **One firmware, four menus, one `core_id`.**  Everything the firmware
  selects by core is indexed by `core_id` (5 UKNC, 7 PK8000, 8 Korvet, 9
  ZS-256): `keymap[]`/`modifier[]` (usb_host.c), `settings_file_name()`,
  `drivename()`, `ultima_cores[]`, the Debug page's byte map.  The UKNC's
  main form is built at run time (`menu_uknc_main`); its Core entry is
  `"S,Core,7;"` because its forms are 0..6, the other three have
  `"S,Core,2;"`.  `make menu-test` walks all four and fails on a form
  that does not agree with itself.
- **Slot 5 is browsed, never mounted** - `SDC_SLOT_EXTRA`: the UKNC's
  "Run SAV:", the PK8000's "Run .bas:" and the ZS-256's "ROM:" walk the
  card through it.  `MAX_DRIVES` is 5 (the PK8000 alone had 6);
  `cwd[]`/`image_name[]` are `MAX_DRIVES + 1`.  Only the ZS-256 saves
  slot 5 in its `.ini` (`drive5=`), because `romload.c` loads that file
  at every start.
- **The card is per core**: `ultima_root()` is `/sd/<dir>` and the file
  browser's root, the `.ini` sits there, `extrom/` and `RT11SAV` too
  (`extrom.h`, `rt11sav.h`), and the ZS-256's `zs256.rom` and
  `gs105a.rom` (`romload.c` - the sibling alone reads them from the
  card's root).  **The ZS-256 has no ROM in its bitstream**: without
  `/sd/zs256/zs256.rom` it executes zeros.  Only `/sd/ultima.ini` and `/sd/cores/` are
  in the root - and without `/sd/cores/<name>.bin` there is no switch at
  all, just an OSD saying the file is missing (`make card`).  A switch
  does not touch `ultima.ini`; "Save to flash" does.
- **The on-board BL616 is the board's only PC-side programmer.**  The
  `flash-mcu-onboard-*` targets overwrite its firmware; `make
  onboard-backup` and `make onboard-efuse` come first, always, and
  `.claude/docs/onboard.md` is the account and the recovery plan.  Its
  ISP is in mask ROM, so the chip cannot be bricked from its flash, but
  an unfused chip (this board, probably - early 2023) cannot run
  Sipeed's FPGA Partner.  This board IS fused: the Partner is at 0 and
  our stage 2 (`onboard/`) at 0x40000, and the two coexist -
  openFPGALoader works on a PC, the switch works off one.
  `onboard/protocol.h` and `mnano/coreload_proto.h` are the same file
  by hand, as are the four `mister/coreload.v`.
- **`prompts/` is a transcript, not context.**  Never read it at the
  start of a session; append every exchange as it finishes, in the form
  `.claude/rules/guideline.md` gives.

Follow `.claude/rules/guideline.md` and `.claude/rules/git.md`.  The
machines' own rules (`timing.md`, their traps) apply when their trees
are edited, which the reconfig support did.
