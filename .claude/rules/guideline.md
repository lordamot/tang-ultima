# Work guideline

## General

All discussions in english.
All code except text strings must be in english.  Russian appears in
comments only where it is a machine's own name for a thing (УКНЦ, Сура,
Корвет, ОПТС, ВВ55) - the way the three sibling repositories do it.

## Project

Never work outside the repository root - except in the three siblings
(`../tang-uknc`, `../tang-pk8000`, `../tang-korvet`), which this
repository builds and which carry the reconfig support; an edit there
follows THEIR `.claude/rules/` and is committed there, when asked.

All of this repository builds here: the three cores with `make cores`
(gw_sh out of the siblings' trees into `build/cores/`), the firmware
with `make fw`, the menu on the host with `make menu-test`.  The
toolchain is fetched into `tools/` by `make toolchain` (on this host,
hard-linked from a sibling's), nothing is installed on the host and
nothing of it is committed.

What cannot be done here is **running it on a board**, and for this
repository that matters more than usual: its whole point is a mechanism
- RECONFIG_N pulsed from user logic, MultiBoot jumping the flash - that
only a board can confirm.  So say which claim you are making: built,
linted, timed, walked on the host are four different things and none of
them is "works".  **Never imply the switch was seen working.**
`.claude/docs/progress.md` carries what each flash showed and must keep
doing so; until it says a switch happened on a board, none has.

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
   the `set_option` names (`-multi_boot`, `-multiboot_spi_flash_address`,
   `-use_reconfign_as_gpio`, `-loading_rate`), and a build with two
   addresses diffed to find where the address lands in the bitstream
   (`.claude/docs/multiboot.md`).  The `//` header of a `.fs` is only
   what the tool was told.
3. **The device data** (`tools/gowin/data/device/GW2AR-18C/QFN88PF.json`)
   for which pin is RECONFIG_N (9, bank 3) and MCLK/MCS_N (59/60).
4. **The Sipeed wiki** for the board: a 64 Mbit flash.
5. **The three siblings' docs** for everything about the machines and the
   MiSTeryNano link; this repository adds one command to it and repeats
   nothing.

## Editing

- The flash layout is in the Makefile (`ADDR_*`, `NEXT_*`, `SLOT_SIZE`)
  and the ring's order in `mnano/ultima.c`; change both, and
  `tools/mkimage.py` will tell you if the built headers disagree.
- A change to a core's menu, keyboard or SD layout belongs to the core's
  own repository first; this firmware carries a copy of each and the
  three must be kept the same by hand (there is no mechanism), so say
  so when one moves.
- Anything that touches the SPI link while the FPGA may be reloading goes
  behind `sys_irq_hold`.

## Verification

- **`make lint`** - each sibling's Verilator lint, in its tree.
- **`make cores`** - the real build, about half a minute a core, each
  gated by its sibling's timing check; `mkimage.py` then checks the ring
  in the binaries.  Read the `//MultiBootSPIAddr` line it prints and the
  resource lines.
- **`make menu-test`** - every form of every core walked on the host,
  screens under `build/menu/`; the Core form checked on each.
- **`make fw`** - the firmware really does build.  Say "builds".
- State what was not checked: the switch on a board, the loading rate,
  whether `openFPGALoader --file-type bin` writes a raw image on this
  board (it has the option and the code path; nobody here has run it).

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
