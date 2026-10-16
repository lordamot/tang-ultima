/*
  evo.h - the ZX Evolution's side of the firmware (Evo Nano).

  What the ZX Evolution's AVR did, and what the core needs at start:

  - the ROM images into the core's SDRAM through SYS command 6: the Evo's
     512 KB ROM (zxevo.rom, at byte 600000h) and
    General Sound's 32 KB (gs105b.rom, at 700000h - gs.v snoops it into its
    BSRAM) - the machine is held in reset meanwhile (menu.c's R=3 ... R=0);
  - the clock's NVRAM (gluk.v, SYS command 12): loaded from cmos.bin at
    start, saved back when the Z80 has written it (the EVO Reset Service
    keeps its settings there);
  - the SD card: the folder SD/ made into a FAT32 card (vsd.c), its
    sectors served on the core's interrupt (irq 4);
  - the keys the AVR took for itself: F11 the reset, Print Screen the NMI.

  Paths come from companion_resource() and the active JSON profile.
  The shipped profile uses /evo/; relocating the catalog directory or
  resource names also relocates ROMs, CMOS and the virtual SD root.
*/
#ifndef EVO_H
#define EVO_H

#include "spi.h"

#define EVO_ROM         "zxevo.rom"
#define EVO_GSROM       "gs105b.rom"
#define EVO_NGSROM      "gs108n.rom"    // NeoGS's ROM 1.08, patched (tools/gs108patch.py)
#define EVO_CMOS        "cmos.bin"
#define EVO_SDDIR       "SD"

#define EVO_ADDR_ROM    0x600000UL      // the Evo's ROM in the SDRAM (sdram.v)
#define EVO_ADDR_GSROM  0x700000UL      // General Sound's
#define EVO_MAX_ROM     (512UL * 1024)
#define EVO_MAX_GSROM   (32UL * 1024)

// SYS command 12 (sysctrl.v): the clock and CMOS
#define SPI_SYS_GLUK    12

// at start, between menu.c's R=3 and R=0: the ROMs (rom: the OSD's
// choice, NULL for the configured rom resource), the CMOS, the SD card
void evo_boot(spi_t *spi, const char *rom);

// from the OSD: another ROM file, then a reset
void evo_rom_select(spi_t *spi, const char *path);

// a value menu.c sends the core (at start, before evo_boot, and when it
// changes): General Sound's 'g' chooses its ROM - EVO_NGSROM for NGS 3.5M
// (3), EVO_GSROM else - loaded again when it changes after the start
void evo_value(spi_t *spi, char id, int val);

// the core's interrupt 4: the SD card wants a sector (the SPI link is
// the one evo_boot() was given)
void evo_handle_event(void);

// every second or so: save the CMOS if the Z80 changed it
void evo_poll(void);

// a key the AVR kept for itself: returns 1 if it was one (not for the core)
int evo_key(spi_t *spi, unsigned char usb_code, int pressed);
void evo_mods(spi_t *spi, unsigned char was, unsigned char now);   // the modifiers' PS/2 codes

// for the OSD
extern unsigned long evo_rom_size, evo_gsrom_size;
const char *evo_status(void);

#endif // EVO_H
