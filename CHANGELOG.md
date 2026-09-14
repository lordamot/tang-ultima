# Changelog

## Unreleased

- **A fourth core: the ZS-256** (`../tang-zs256`, ZS-256 Nano - the
  Scorpion ZS-256 Turbo+).  `make core-zs256`, `bin/zs256.fs` and
  `bin/zs256.bin`, `/sd/cores/zs256.bin`, `/sd/zs256/` on the card with
  `zs256.ini`, `zs256.rom` and `gs105a.rom` (the ROMs are not in the
  bitstream: `mnano/romload.c` sends them into the SDRAM at start over
  SYS CMD 6 with three address bytes, `sys_poke24`).  Core id 9;
  `keymap_zs256`, its forms, About, Debug page and "ROM:" selector in
  the firmware; the Core form has four machines and "Save to flash".
  Built, timed, linted, walked on the host (`make menu-test`, 40
  screens, 0 errors), and seen working on the board the same evening.
- The USB keyboard report is compared as a set of six keys, not slot by
  slot (from ZS-256 Nano, 14 Sep 2026): a keyboard packs its slots, so
  releasing the first of two held keys moved the second and the old
  compare sent the core a release and a press for a key that never
  moved.  A release now reaches the core with the OSD open too.  All
  cores; seen on the ZS-256, not reported on the other three.
- README brought to the current mechanism (it still described
  MultiBoot).
- USB keyboard lost until a power cycle - the likely cause removed, not
  yet seen fixed on a board.  A keyboard with a power-saving mode drops
  off the bus and re-attaches as it wakes, often within the 100 ms the
  firmware polled `/dev/inputN` at, so the poll saw "still there" while
  the reader thread stayed blocked for ever on a URB the stack had
  killed without a callback.  `mnano/usb_host.c` now takes the stack's
  own attach/detach hooks (`usbh_hid_run`/`usbh_hid_stop`), the thread
  exits on a flag, its URB has a timeout, and a stalled endpoint is
  cleared.  The same change is in all three siblings' `mnano/usb_host.c`.
- **The switch is now a load into the FPGA's SRAM by the board's own
  BL616** - seconds, no flash written, no power cycle.  Seen on the board
  13 September 2026: Korvet -> PK8000 -> UKNC from the OSD.
  - `onboard/`: stage 2 for the on-board BL616, started by Sipeed's FPGA
    Partner at 0x40000 when the board is not on a PC; a command server on
    a 2 Mbaud UART (`protocol.h`) that takes a core into its own flash and
    loads it over the JTAG it owns, openFPGALoader's SRAM sequence routine
    for routine; every step and the first bytes on the link logged to a
    flash sector, `make onboard-log` reads it back.
  - Every core (in the sibling repositories): `mister/coreload.v` and SYS
    CMD 11, a TX FIFO and the UART on pins 69/70.
  - The dock: `mnano/coreload.c`, `ultima_switch()` = ping, send, load,
    reset.  The Core form's last entry, "Save to flash", is the previous
    switch - the running machine written into flash address 0, which is
    what power-up loads.
  - Make: `onboard-fw`, `flash-mcu-onboard-stage2`,
    `flash-mcu-onboard-core CORE=`, `onboard-log`; `tools/mkstage.py`.
    `flash-mcu` refuses when `build/fw/bl616.bin` is newer than
    `bin/bl616.bin`.
- Make targets for the board's own BL616, the USB programmer:
  `onboard-status`, `onboard-fetch`, `onboard-backup`, `onboard-efuse`,
  `flash-mcu-onboard-{orig,orig-encrypted,ftdi,stage2,restore}`, with
  the factory and FPGA Partner images fetched from MiSTle-Dev/
  FPGA-Companion into `bin/onboard/`.  Run on the board: backup,
  efuse (fused), the Partner written and running.
  `.claude/docs/onboard.md` is the account and the recovery table.

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
