# Work guideline

## General

All discussions in english.
All code except text strings must be in english.  Russian appears in
comments only where it is a machine's own name for a thing (УКНЦ, Сура,
Корвет, ОПТС, ВВ55) - the way the four sibling repositories do it.

## Project

Never work outside the repository root - except in the four siblings
(`../tang-uknc`, `../tang-pk8000`, `../tang-korvet`, `../tang-zs256`), which this
repository builds and which carry the reconfig support; an edit there
follows THEIR `.claude/rules/` and is committed there, when asked.

All of this repository builds here: the four cores with `make cores`
(gw_sh out of the siblings' trees into `build/cores/`), the firmware
with `make fw`, the menu on the host with `make menu-test`.  The
toolchain is fetched into `tools/` by `make toolchain` (on this host,
hard-linked from a sibling's), nothing is installed on the host and
nothing of it is committed.

What cannot be done here is **running it on a board**, and for this
repository that matters more than usual: its whole point is a mechanism -
the FPGA writing its own configuration flash through the MSPI pins - that
only a board can confirm.  The first mechanism, MultiBoot on a RECONFIG_N
pulse, was built, looked right by every check available here, and turned
out not to work on this board at all.  Remember that when a check here
comes out clean.  So say which claim you
are making: built, linted, timed, walked on the host are four different
things and none of them is "works".  **Never imply the switch was seen
working.**  `.claude/docs/progress.md` carries what each flash showed and
must keep doing so; until it says a core was installed on a board, none
has.

If something else needs to be installed onto the host system - ask for
it.  Nothing goes outside `tools/` without asking.

Any problem like "the board would have to be watched but can't be" - ask
before researching it yourself.

## Where the facts come from

1. **UG290**, Gowin FPGA Products Programming and Configuration Guide
   (`tools/gowin/doc/ENG/UG290-*.pdf`): §7.5.4 MultiBoot (the jump
   address in the header, RECONFIG_N low loads it, power-up loads 0,
   bits [23:12] only), §4.2 the pins (RECONFIG_N as a GPIO is output
   only, "you can also write logic to control the pin to trigger the
   device to reconfigure", initial value high), §7.5.3 the two
   configuration attempts of the GW2AR-18.
2. **The Gowin tool itself**: `strings tools/gowin/lib/libGWTE.so` for
   the `set_option` names (`-use_mspi_as_gpio`, `-use_sspi_as_gpio`,
   `-use_reconfign_as_gpio`, `-multi_boot`, `-multiboot_spi_flash_address`,
   `-multiboot_mode`, `-multiboot_address_width`, `-loading_rate`) and
   their accepted values, and a build diffed against another to find where
   a field lands in the bitstream (`.claude/docs/multiboot.md`).  The `//`
   header of a `.fs` is only what the tool was told.
3. **The device data** (`tools/gowin/data/device/GW2AR-18C/QFN88PF.json`)
   for which pin is which: RECONFIG_N 9, MCLK 59, MCS_N 60, MO 61, MI 62,
   all bank 3, and TMS/TCK/TDI/TDO 5/6/7/8.
4. **The Sipeed wiki** for the board, and **the Tang Nano 20K schematic**
   (v1.3) for what a pin actually reaches - which is how pin 9 was found
   to end at test pad TP1 and nowhere else.  The flash is a Winbond
   W25Q64, 64 Mbit, JEDEC ID `ef4017`, confirmed by openFPGALoader.
5. **The four siblings' docs** for everything about the machines and the
   MiSTeryNano link; this repository adds one command to it and repeats
   nothing.

## Editing

- The flash holds one bitstream at address 0 and the card holds all
  four, so the names must agree in two places: the Makefile's `CORES`
  and `DEFAULT_CORE`, and `mnano/ultima.c`'s `ultima_cores[]`, whose
  `dir` field names both the card directory and `/cores/<dir>.bin`.
- Anything that writes flash address 0 is writing the only thing the board
  can boot.  Keep the refusals in `flash_install()` ahead of the erase,
  and keep `tools/mkimage.py` checking the same fields.
- A change to a core's menu, keyboard or SD layout belongs to the core's
  own repository first; this firmware carries a copy of each and the
  four must be kept the same by hand (there is no mechanism), so say
  so when one moves.
- The card and the flash share the m0s link, so nothing may ask the card
  for a sector while a page program is in flight: `ultima_switch()` closes
  every image first.  (`sys_irq_hold` is from the MultiBoot design, when
  the link really did die mid-switch; it is still there and the switch no
  longer needs it.)

## Verification

- **`make lint`** - each sibling's Verilator lint, in its tree.
- **`make cores`** - the real build, about a minute a core, each gated by
  its sibling's timing check, then packed by `mkimage.py` which refuses
  anything that is not a bitstream for this device.  Read the resource
  lines and the pin report (`mspi_*` on 59/60/61/62).
- **`make menu-test`** - every form of every core walked on the host,
  screens under `build/menu/`; the Core form checked on each.
- **`make fw`** - the firmware really does build.  Say "builds".
- State what was not checked.  As of 13 September 2026 the switch itself
  **is** checked on a board: a core installed from the OSD, read back over
  JTAG byte-identical, and booted.  What is NOT: how long an install takes,
  the loading rate, the other cores installed and booted, the ZS-256 on a
  board at all under this firmware (added 14 Sep 2026), and every
  machine's own behaviour under this firmware rather than its own.

## The prompts/ folder

**Never read `prompts/` as context.**  It is a transcript, not
documentation, not instructions and not a spec.  It is kept in the
siblings' form: `<n> <topic>.txt`, the prompt verbatim, a line of
asterisks, the reply as plain text; **append every exchange as it
finishes**, unasked; never rewrite an entry already there.

## Main goal

One Tang Nano 20K that is the УКНЦ, the ПК8000 or the Корвет at the
user's choice from the OSD, with each machine exactly what its own
repository makes it, its files in its own folder on the card, and the
choice remembered across power cycles.

The choice now costs a power cycle, because nothing can make this FPGA
reload its flash from software.  That is a known cost, not a thing to be
quietly designed around: if it is ever to go away it is one wire from
header pin 48 to TP1, and that is the user's call to make, not ours.
