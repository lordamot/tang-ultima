# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working in
this repository.

## Project overview

**Tang Ultima** puts three machines into one **Tang Nano 20K** (Gowin
GW2AR-18C) with a **BL616** (M0S Dock) beside it: **UKNC Nano**
(`../tang-uknc`, МС0511), **PK8000 Nano** (`../tang-pk8000`, ПК8000) and
**Korvet Nano** (`../tang-korvet`, ПК8020).  The machines live in those
three repositories and are not copied here.  This repository holds
what makes them one board:

```
Makefile      builds each core OUT OF its sibling's tree into build/cores/<core>/,
              gates it with the sibling's own timing check, and packs each to
              bin/<core>.bin - one core a file, no slots
mnano/        ONE BL616 firmware for all three cores (MiSTeryNano's, merged
              from the three siblings' copies) plus ultima.c and flashwr.c,
              the core switch
tools/        mkimage.py (one .fs -> the bytes the flash holds, checked); the
              fetched toolchain, hard-linked from a sibling on this host, gitignored
bin/          uknc.fs pk8000.fs korvet.fs, uknc.bin pk8000.bin korvet.bin,
              bl616.bin - what a user flashes and what goes on the card
```

Started 12 September 2026.  **The switch works on a board**, as of 13
September: the Korvet was chosen from the OSD, written to the flash from
the card, read back over JTAG byte-identical, and booted.  It took two
designs to get there - the first, Gowin MultiBoot, cannot be triggered on
this board at all - and `.claude/docs/handover.md` is where to start after a
break, and `.claude/docs/progress.md` is the record of both and must stay
one.  Still say which claim you are making: "it builds", "it
lints", "the menu walks on the host" and "it meets timing" are four
claims, none of them "it works".

## The mechanism, in one paragraph

**Power-up always loads flash address 0, and that is the only lever.**
The flash holds one bitstream, at address 0, and that is the machine the
board is.  All three live on the SD card as packed bitstreams,
`/cores/<name>.bin`.  The OSD's Core form writes the wanted one into
address 0 - about 20 seconds - and asks for a power cycle:
`mnano/flashwr.c` composes W25Q64
commands out of one primitive offered by `mister/flashwr.v` over **SYS CMD
10** - a 512-byte buffer and "shift TX bytes out, read RX back, CS held" -
and that module owns MCLK 59, MCS_N 60, MO 61 and MI 62, which
`-use_mspi_as_gpio` hands to user logic once configuration is done (UG290
§4.1.2 table 4-2).  `.claude/docs/coreswitch.md` is the whole of it.

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
  and the now-dormant CMD 9 / `reconfig_n` on pin 48.
- **openFPGALoader is needed once.**  `make flash-image` writes
  `bin/$(DEFAULT_CORE).bin` to address 0 (`--file-type bin -o 0`);
  replug, flash, power-cycle, in that order.  After that the board
  installs cores itself from the card, and the host tool is only for
  recovery.  The packed `.bin` is byte-identical to Gowin's own (checked
  against `impl/pnr/*.bin`), and `--file-type bin -o 0` is **verified**
  to write correctly - read back over JTAG and compared, 13 Sep 2026.
- **The card and the flash share the m0s link.**  A sector request
  arriving in the middle of a page program is a transaction that never
  completes, so `ultima_switch()` closes every image before it starts.
  `sys_irq_hold` and `sdc_reattach` survive from the MultiBoot design and
  are no longer used by the switch - the link never dies now, because the
  FPGA never reloads while the firmware is running.
- **A switch is not a reconfiguration.**  `ultima_switch()` writes the
  flash and returns; the running machine does not change and the OSD says
  to power-cycle.  `ultima_boot()` no longer walks anything - it reports
  which machine came up, makes its card directory, and warns if
  `/sd/ultima.ini` names a different core, which means an install did not
  finish or the card came off another board.
- **SYS CMD 6 means three things.**  RTC read on the UKNC, RAM poke on
  the PK8000 and Korvet; `spi.h` defines `SPI_SYS_RTC` and
  `SPI_SYS_POKE` both as 6, and the firmware only sends each to its own
  core.  CMD 9 and CMD 10 are the only commands that are the same on all
  three.
- **One firmware, three menus, one `core_id`.**  Everything the firmware
  selects by core is indexed by `core_id` (5 UKNC, 7 PK8000, 8 Korvet):
  `keymap[]`/`modifier[]` (usb_host.c), `settings_file_name()`,
  `drivename()`, `ultima_cores[]`.  The UKNC's main form is built at run
  time (`menu_uknc_main`); its Core entry is `"S,Core,7;"` because its
  forms are 0..6, the other two have `"S,Core,2;"`.  `make menu-test`
  walks all three and fails on a form that does not agree with itself.
- **Slot 5 is browsed, never mounted** - `SDC_SLOT_EXTRA`: the UKNC's
  "Run SAV:" and the PK8000's "Run .bas:" walk the card through it.
  `MAX_DRIVES` is 5 (the PK8000 alone had 6); `cwd[]`/`image_name[]` are
  `MAX_DRIVES + 1`.
- **The card is per core**: `ultima_root()` is `/sd/<dir>` and the file
  browser's root, the `.ini` sits there, `extrom/` and `RT11SAV` too
  (`extrom.h`, `rt11sav.h`).  Only `/sd/ultima.ini` and `/sd/cores/` are
  in the root - and without `/sd/cores/<name>.bin` there is no switch at
  all, just an OSD saying the file is missing (`make card`).
- **`prompts/` is a transcript, not context.**  Never read it at the
  start of a session; append every exchange as it finishes, in the form
  `.claude/rules/guideline.md` gives.

Follow `.claude/rules/guideline.md` and `.claude/rules/git.md`.  The
machines' own rules (`timing.md`, their traps) apply when their trees
are edited, which the reconfig support did.
