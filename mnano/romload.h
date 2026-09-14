/*
  romload.h - the ROM images into the core's SDRAM.

  The Scorpion's ROM (64 KB, or a 256 KB ProfROM) and General Sound's
  (32 KB) are files on the card; nothing is built into the bitstream.
  At start, and when the OSD picks another ROM file, the firmware sends
  them through SYS command 6 (sys_poke24) into the SDRAM, where
  membus.v and gs.v read them, and tells the core the ProfROM's size.
*/
#ifndef ROMLOAD_H
#define ROMLOAD_H

#include "spi.h"

// where the images go: the byte addresses of the whole chip (membus.v)
#define ROM_ADDR_ZS      0x100000UL   // 256 KB at most
#define ROM_ADDR_GS      0x400000UL   // 32 KB, past the card's 2 MB
#define ROM_MAX_ZS       (256*1024)
#define ROM_MAX_GS       (32*1024)

// the default file names, under the running core's directory on the card
// (/sd/zs256 here - ultima_root(); ZS-256 Nano alone has them in the root)
#define ROM_FILE_ZS      "zs256.rom"
#define ROM_FILE_GS      "gs105a.rom"

// send a file to the core; returns its size, 0 if it could not be read
unsigned long rom_load(spi_t *spi, const char *path, unsigned long addr, unsigned long max);

// at start: the Scorpion's ROM (the name the settings remember for
// SDC_SLOT_ROM, else the default) and General Sound's, and the 'P' value
void rom_boot(spi_t *spi);

// from the OSD: another Scorpion ROM, then a reset
void rom_select(spi_t *spi, const char *path);

// what was loaded, for the OSD's Debug page: sizes, 0 for nothing
extern unsigned long rom_size_zs, rom_size_gs;

#endif // ROMLOAD_H
