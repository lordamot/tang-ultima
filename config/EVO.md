# ZX Evolution TS-Conf in Tang Ultima

Added 17 October 2026 from `retro-tang-evo-tsconf` v0.1.9 alpha.
The source repository has no commits; `siblings.lock.json` records file
hashes and revision `uncommitted` rather than inventing a source commit.

## Build and card

`make core-evo` builds the sibling RTL into Ultima's own build directory,
runs the sibling timing gate and packs `bin/evo.fs` into `bin/evo.bin`.
`make fw` includes the Evo backend in the shared BL616 companion.
`make card-stage` includes `/cores/evo.bin`, `/cores/evo.json` and `/evo/`:
`zxevo.rom`, `gs105b.rom`, `gs108n.rom`, and the `SD/` folder.
Copy staged files while preserving existing saved state and writable media.

The profile defines resources `rom`, `gs_rom`, `ngs_rom`, `cmos` and `sd`.
All resolve beneath the catalog's Evo directory, so a relocated catalog
and edited resource names apply to every loader. The default CMOS filename
is `cmos.bin`; it is created before virtual SD scanning. The default SD
root is `/evo/SD/`, exposed to the machine as a guarded FAT32 volume.

## Firmware behavior

FPGA ID 0x0B selects Evo. ROMs, CMOS and virtual SD initialization run
between reset values R=3 and R=0. Slot 5 selects and remembers a replacement
ROM without mounting it as a disk image. A General Sound setting crossing
into or out of NGS 3.5M reloads the corresponding ROM. IRQ 4 serves Evo's
virtual SD instead of the BK AZ controller or Korvet ExtROM channel.
Keyboard events feed both the ZX matrix and SYS command 12 PS/2 buffer;
F11 resets and Print Screen requests NMI. CMOS polling uses elapsed ticks,
including while OSD animation events arrive. The debug page decodes Evo's
own debug bytes and reports virtual SD counters.

## Validation

Prepared and checked in an isolated copy before applying:

- `make config-check`: 55 checks, including Evo profile rejection/fallback.
- `make menu-test`: 58 screens, zero errors for both embedded and card JSON;
  all six Core menus and Evo boot under reset and ROM selection checked.
- `make storage-test`: production FatFs on FAT32; relocated Evo ROMs,
  CMOS creation, virtual SD root, GS reload and keyboard commands pass,
  alongside existing core storage regressions.
- `make vsd-test`: existing guarded virtual SD regression, zero errors.
- `make fw`: shared BL616 firmware builds.
- `make core-evo`: builds, zero reported setup/hold violations; packed
  image 907418 bytes with GW2AR-18 IDCODE 0x0000081b.
- `make card-stage`: all six profiles, bitstreams and existing assets staged.
- `make siblings-check`: reviewed source snapshots and shared shipped
  firmware copies checked. Evo remains a standalone build entry point.

These are host/build checks. Evo's SDRAM pad timing is unconstrained.
The standalone core's garbled physical-board TR-DOS video remains unresolved;
this integration has not been switched to, booted or flashed on a board.

## SDRAM capture correction (17 October 2026)

The Evo FPGA image now uses ph 3 for late E capture, one clock after
normal ph 2 capture. The previous ph 4 fallback failed its SDRAM self-test
with `+SDRAM_TAC=18`. The corrected path passes the early/late regression,
DMA and NeoGS with delayed capture selected. The rebuilt FPGA passes its
timing gate. This is an FPGA-only update; the shared firmware is unchanged.
Physical-board validation and the garbled-video diagnosis remain open.
