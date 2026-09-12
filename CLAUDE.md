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
Makefile      builds each core OUT OF its sibling's tree into build/cores/<core>/
              with the next slot's flash address in its header, gates it with
              the sibling's own timing check, packs the three into bin/ultima.bin
mnano/        ONE BL616 firmware for all three cores (MiSTeryNano's, merged
              from the three siblings' copies) plus ultima.c, the core switch
tools/        mkimage.py (the flash image, checks the ring); the fetched
              toolchain, hard-linked from a sibling on this host, gitignored
bin/          uknc.fs pk8000.fs korvet.fs ultima.bin bl616.bin - what a user flashes
```

Started 12 September 2026.  **Nothing has run on a board**, and that
includes the switching mechanism itself; `.claude/docs/progress.md`
says what has been verified and how.  "It builds", "it lints", "the
menu walks on the host" and "it meets timing" are four claims, none of
them "it works".

## The mechanism, in one paragraph

Gowin MultiBoot (UG290 §7.5.4, `tools/gowin/doc/ENG/UG290*.pdf`): each
bitstream's header names the flash address of the NEXT bitstream, and a
low pulse on RECONFIG_N makes the FPGA load it; power-up always loads
address 0.  The three images sit at 0x000000 (UKNC), 0x100000 (PK8000),
0x200000 (Korvet), a ring.  Each core's `sysctrl.v` takes **CMD 9 + A5h**
and pulses `reconfig`, `top.v` drives **RECONFIG_N = pin 9** low for 256
clocks (`-use_reconfign_as_gpio`, UG290 §4.2 allows exactly this), and
the FPGA reloads.  The firmware cannot pick a slot, only say "next": it
reads the wanted core from `/sd/ultima.ini`, compares with the core id
the FPGA answers (CMD 0), and hops until they agree (`mnano/ultima.c`).
`.claude/docs/multiboot.md` has the whole account and the evidence.

## Traps worth remembering

- **The ring is written in three places and they must agree**: the
  Makefile's `CORES`/`ADDR_*`/`NEXT_*`, `mnano/ultima.c`'s
  `ultima_cores[]` (same order), and the header of each built `.fs`.
  `tools/mkimage.py` refuses an image whose headers do not close the
  ring - and it checks the *binary* field (the D2 command's operand at
  0x38), not the `//MultiBootSPIAddr` comment, because the comment is
  only what the tool was told.  Gowin's `-multiboot_spi_flash_address`
  wants BARE hex digits (`00100000`): given `0x100000` the header reads
  `0x0x00100000` and the value is still right, but do not rely on it.
- **The siblings are read, never written, by a build here.**  gw_sh
  runs in `build/cores/<core>/` with absolute source paths
  (`gowin_tcl.py --abs`), so their `tang/impl/pnr/`, `tang/build.tcl`
  and `bin/tang.fs` stay theirs.  The reconfig support (CMD 9,
  `reconfig_n`, pin 9, `RECONFIG_N: true` in the process config, the
  `--multiboot-addr`/`--abs` options of `gowin_tcl.py`, the PnR-dir
  argument of `timing_check.py`) IS in the siblings' trees, as a generic
  feature: a standalone build names address 0 and reloads itself.
- **Flashing three slots is ONE openFPGALoader run**, `make flash-image`
  with `bin/ultima.bin` (`--file-type bin -o 0`).  The Tang's USB
  bridge takes one USB reset per replug, so three `-o` runs would be
  three replugs; and the `.bin` is byte-identical to what openFPGALoader
  writes for a `.fs` (checked: the packed `.fs` equals Gowin's own
  `.bin`).  Replug, flash, power-cycle, in that order.
- **A dead link reads as requests.**  While the FPGA reloads, MISO
  floats; `sdc_handle_event` would take that for a sector request - a
  WRITE into a mounted image among the possibilities.  So the switch
  closes every image first, and `sys_irq_hold` (sysctrl.c, honoured in
  spi.c's task) stops interrupt processing until the new core is up and
  the card is remounted (`sdc_reattach`).  Keep every path that touches
  the link during a switch behind that flag.
- **A fresh core raises coldboot, and coldboot resets the MCU.**  That
  is MiSTeryNano's design (`sys_handle_event`) and it is what makes the
  OSD's switch clean: one hop, the MCU restarts, `ultima_boot()` finishes
  the walk with the card's wish.  Inside the walk the notice is
  acknowledged (`sys_irq_ctrl(spi, 1)`) before the hold is lifted, or
  the MCU would reset in the middle of it.  `main()` waits ten seconds
  for the FPGA, not MiSTeryNano's five: after the switch the MCU is up
  before the FPGA is.
- **SYS CMD 6 means three things.**  RTC read on the UKNC, RAM poke on
  the PK8000 and Korvet; `spi.h` defines `SPI_SYS_RTC` and
  `SPI_SYS_POKE` both as 6, and the firmware only sends each to its own
  core.  CMD 9 is the only command that is the same on all three.
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
  (`extrom.h`, `rt11sav.h`).  `/sd/ultima.ini` alone is in the root.
- **`prompts/` is a transcript, not context.**  Never read it at the
  start of a session; append every exchange as it finishes, in the form
  `.claude/rules/guideline.md` gives.

Follow `.claude/rules/guideline.md` and `.claude/rules/git.md`.  The
machines' own rules (`timing.md`, their traps) apply when their trees
are edited, which the reconfig support did.
