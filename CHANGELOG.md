# Changelog

## 0.1.0 alpha - 12 September 2026

The first cut.  Authors: Sergei Lemeshev and Claude Code.  Not yet run
on a board - the switching mechanism included.

- Three cores, one flash: UKNC Nano, PK8000 Nano and Korvet Nano built
  out of their own repositories into 1 MB slots at 0, 0x100000 and
  0x200000, each bitstream's MultiBoot header naming the next slot;
  `bin/ultima.bin` is the three in one image, checked for a closed ring.
- Reconfig support in each core (in the sibling repositories): SYS
  command 9 + A5h pulses RECONFIG_N (pin 9, a GPIO) and the FPGA loads
  the next image; `gowin_tcl.py --abs --multiboot-addr`,
  `timing_check.py <pnr dir>`.
- One BL616 firmware for all three, MiSTeryNano's merged from the
  siblings' copies, with a "Core" form on every main menu and
  `/sd/ultima.ini` remembering the choice; the firmware hops the ring at
  start until the wanted core answers.
- Each machine's files under its own directory on the card: `/uknc`,
  `/pk8000`, `/korvet` (settings, images, RT11SAV, extrom/).
- `make menu-test` walks every form of every core on the host.
