/*
  azbk.h - the AZ controller's STM32, played by the BL616.

  On the real AZBK an STM32F407 owns the MicroSD card and answers the
  controller's commands (177220/177222) with the FPGA as its bus
  interface.  Here the FPGA keeps the registers and the 8192-word buffer
  (azctrl.v) and hands every command that needs the card, the clock or
  the configuration to this file over its own SPI target: az_handle_event()
  runs on the controller's interrupt, reads the command out, serves it
  from FatFs on the card's /bk/ folder, moves words in and out of the
  buffer, and reports "done".

  At start az_boot() reads /bk/AZ.INI - MAXIOL's own format: [ROM] with
  Rnn=path lines (slot nn of 4 KB from page 100), [LOGO] L=path, [DISKS]
  with Dn=path, [BOOT] Dn - loads the ROMs and the logo into the SDRAM
  through SYS command 6 while the machine is in reset, and keeps the
  unit table.  A path "0:/rom/x" means /bk/rom/x on the card.

  There is no network: the HOF and the IP commands answer as a
  controller without a cable does, and the time comes from a software
  clock set by the machine (command 034) or the OSD.
*/
#ifndef AZBK_H
#define AZBK_H

#include "spi.h"

#define AZ_ROOT          CARD_MOUNTPOINT "/bk"    // the AZ's card root
#define AZ_INI           AZ_ROOT "/AZ.INI"
#define AZ_EEPROM        AZ_ROOT "/eeprom.dat"
#define AZ_UNITS         32
#define AZ_PATH_MAX      160

// SPI target 4 (azctrl.v)
#define SPI_TARGET_AZ    4
#define SPI_AZ_STATUS    1
#define SPI_AZ_READ      2
#define SPI_AZ_WRITE     3
#define SPI_AZ_DONE      4
#define SPI_AZ_RESET     5

// the buffer's regions, in words (azctrl.v)
#define AZ_R_IOBUF       0
#define AZ_R_CMOS        256
#define AZ_R_IP          512
#define AZ_R_SIZE        528
#define AZ_R_FSZ         530
#define AZ_R_TS          544
#define AZ_R_USIZE       576   // 32 units x (blocks low, high): the FPGA's select reads it
#define AZ_R_TABLE       1024

// at start: AZ.INI, the ROMs and the logo into the core, the unit table;
// afterwards az_boot_line(0..) says what happened, for the OSD's Debug
// page (NULL past the last line): whether AZ.INI was read, the files
// loaded and missing, and the read-back verify through SYS command 8
void az_boot(spi_t *spi);
const char *az_boot_line(int n);

// on the controller's interrupt: one command served
void az_handle_event(void);

// the OSD: a unit's image (0..3), or none; the software clock
int  az_set_unit(int unit, const char *path);
const char *az_unit_path(int unit);
unsigned long az_unit_blocks(int unit);
void az_set_time(int year, int month, int day, int hour, int min, int sec);

#endif // AZBK_H
