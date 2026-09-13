/*
  flashwr.h - tang-ultima: install a core into the configuration flash.

  The board cannot be told to reload its FPGA from software: pulsing
  RECONFIG_N from user logic does not reconfigure this device, because
  reusing the pad as a GPIO cuts it from the configuration controller
  (seen on the board, 13 September 2026 - .claude/docs/progress.md).
  What power-up DOES always do is load the bitstream at flash address 0.

  So a core switch writes the wanted machine to address 0 and the board is
  power-cycled into it.  The three machines live on the card as packed
  bitstreams, /sd/cores/<name>.bin - byte for byte what Gowin's own .bin
  holds - and this is the code that puts one of them in the flash, through
  flashwr.v on the FPGA (SYS CMD 10) and the MSPI pins it owns once
  configuration is over.

  Everything about the flash itself is here rather than in the FPGA: the
  Verilog knows only "shift these bytes out, read those back".  The chip
  is a Winbond W25Q64 (8 MB, 256-byte pages, 64 KB blocks).

  Writing address 0 destroys what the board boots until it finishes, so
  flash_install() refuses anything that is not a bitstream for this
  device, and verifies what it wrote.  A power cut in the middle leaves a
  board that needs openFPGALoader before it comes up again.
*/

#ifndef FLASHWR_H
#define FLASHWR_H

#include "spi.h"

#define FLASH_PAGE      256          // W25Q64 page program
#define FLASH_BLOCK     65536UL      // 64 KB block erase (0xD8)
#define FLASH_SLOT      0x100000UL   // the most one core may occupy at 0
#define FLASH_ID_W25Q64 0x00ef4017UL // what --detect prints for this board
#define FLASH_MFG_WINBOND 0xefU

// what a GW2AR-18C bitstream must look like (checked before anything is
// erased): the Gowin preamble and this device's IDCODE
#define BIT_MAGIC_OFF   0x16
#define BIT_IDCODE_OFF  0x1c
#define BIT_IDCODE      0x0000081bUL

// stages, for the OSD to draw something during the ten-odd seconds
#define FLASH_STAGE_CHECK  0
#define FLASH_STAGE_ERASE  1
#define FLASH_STAGE_WRITE  2
#define FLASH_STAGE_VERIFY 3
typedef void (*flash_progress_t)(int stage, int done, int total);

// 0x9F: {manufacturer, type, capacity}, 0 if the bus answers nothing
unsigned long flash_id(spi_t *spi);

// Read-only, and the first thing worth doing on a board: the JEDEC ID, the
// status register, and the head of address 0 with a verdict on whether it
// looks like a bitstream.  Proves CMD 10, flashwr.v, the MSPI pins and the
// read path without writing a byte, so it runs at every boot.
void flash_probe(spi_t *spi);

// Put the bitstream at `path` into flash address 0, verify it, and report.
// 0 on success, negative on refusal or failure - nothing is erased unless
// every check passed first.
#define FLASH_ERR_BUS     -1   // no flash answered on the MSPI pins
#define FLASH_ERR_OPEN    -2
#define FLASH_ERR_SIZE    -3
#define FLASH_ERR_NOTBIT  -4   // not a bitstream for this device
#define FLASH_ERR_READ    -5
#define FLASH_ERR_ERASE   -6
#define FLASH_ERR_WRITE   -7
#define FLASH_ERR_VERIFY  -8
int flash_install(spi_t *spi, const char *path, flash_progress_t cb);

const char *flash_strerror(int err);

#endif // FLASHWR_H
