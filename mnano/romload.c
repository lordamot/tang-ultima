/*
  romload.c - the ROM images into the core's SDRAM.  See romload.h.

  A file is read through FatFs in 512-byte pieces and each piece goes
  out as one SYS command 6 transaction: three address bytes, then the
  bytes, the core stepping the address.  At 20 MHz on the SPI a 64 KB
  ROM takes well under a second, a 256 KB ProfROM about a second; the
  machine is held in reset by the caller meanwhile (menu.c sends R=3
  before and R=0 after).

  The ProfROM's bank switch (memmap.v) is masked to what was loaded:
  'P' is 0 for 64 KB (no switching), 1 for 128 KB, 3 for 256 KB.
*/
#include <stdio.h>
#include <string.h>
#include <ff.h>

#include "romload.h"
#include "sysctrl.h"
#include "sdc.h"
#include "ultima.h"

unsigned long rom_size_zs = 0, rom_size_gs = 0;

unsigned long rom_load(spi_t *spi, const char *path, unsigned long addr, unsigned long max) {
  FIL fil;
  unsigned long done = 0;
  static unsigned char buf[512];

  sdc_lock();
  if(f_open(&fil, path, FA_OPEN_EXISTING | FA_READ) != FR_OK) {
    printf("ROM: cannot open %s\r\n", path);
    sdc_unlock();
    return 0;
  }
  while(done < max) {
    UINT rd = 0;
    if(f_read(&fil, buf, sizeof(buf), &rd) != FR_OK || rd == 0) break;
    sys_poke24(spi, addr + done, buf, rd);
    done += rd;
  }
  f_close(&fil);
  sdc_unlock();
  printf("ROM: %s, %lu bytes to %06lx\r\n", path, done, addr);
  return done;
}

// the path of a slot's image, or of the default name in the core's
// directory on the card (ZS-256 Nano: the card's root)
static const char *rom_path(int slot, const char *dflt, char *buf, int len) {
  char *cwd = (slot >= 0) ? sdc_get_cwd(slot) : NULL;
  char *name = (slot >= 0) ? sdc_get_image_name(slot) : NULL;
  if(cwd && name) snprintf(buf, len, "%s/%s", cwd, name);
  else snprintf(buf, len, "%s/%s", ultima_root(), dflt);
  return buf;
}

static void rom_tell(spi_t *spi) {
  unsigned char p = 0;
  if(rom_size_zs > 65536)  p = 1;
  if(rom_size_zs > 131072) p = 3;
  sys_set_val(spi, 'P', p);
}

void rom_boot(spi_t *spi) {
  char path[300];
  rom_size_zs = rom_load(spi, rom_path(SDC_SLOT_ROM, ROM_FILE_ZS, path, sizeof(path)), ROM_ADDR_ZS, ROM_MAX_ZS);
  rom_size_gs = rom_load(spi, rom_path(-1, ROM_FILE_GS, path, sizeof(path)), ROM_ADDR_GS, ROM_MAX_GS);
  rom_tell(spi);
  if(!rom_size_zs) printf("ROM: no " ROM_FILE_ZS " - the machine has nothing to run\r\n");
}

void rom_select(spi_t *spi, const char *path) {
  sys_set_val(spi, 'R', 3);
  rom_size_zs = rom_load(spi, path, ROM_ADDR_ZS, ROM_MAX_ZS);
  rom_tell(spi);
  sys_set_val(spi, 'R', 0);
}
