/*
  flashwr.c - tang-ultima: install a core into the configuration flash.
  See flashwr.h for why this exists at all.

  The FPGA side (mister/flashwr.v) offers exactly one thing: a 512-byte
  buffer and "shift TX bytes of it out to the flash, then read RX bytes
  back into it, with CS held down".  Every W25Q command is composed here.
*/

#include <stdio.h>
#include <string.h>
#include <ff.h>

#include "flashwr.h"
#include "sysctrl.h"
#include "sdc.h"

#ifndef SDL
#include <FreeRTOS.h>
#include <task.h>
#endif

// flashwr.v's sub-commands, the first byte of every CMD 10 stream
#define FW_STATUS 0x00
#define FW_PTR    0x01
#define FW_WRITE  0x02
#define FW_READ   0x03
#define FW_GO     0x04

// W25Q64
#define CMD_WREN      0x06
#define CMD_RDSR      0x05
#define CMD_READ      0x03
#define CMD_PAGE_PROG 0x02
#define CMD_ERASE_64K 0xd8
#define CMD_JEDEC_ID  0x9f
#define SR_WIP        0x01

// ---------------------------------------------------------------------
// flashwr.v's four primitives.  sysctrl.c keeps its own sys_begin() to
// itself, so this opens the transaction here: target, command, then the
// sub-command byte flashwr.v parses.
// ---------------------------------------------------------------------

static void fw_begin(spi_t *spi, unsigned char sub) {
  spi_begin(spi);
  spi_tx_u08(spi, SPI_TARGET_SYS);
  spi_tx_u08(spi, SPI_SYS_FLASH);
  spi_tx_u08(spi, sub);
}

static void fw_ptr(spi_t *spi, int off) {
  fw_begin(spi, FW_PTR);
  spi_tx_u08(spi, (off >> 8) & 0x01);
  spi_tx_u08(spi, off & 0xff);
  spi_end(spi);
}

static void fw_write(spi_t *spi, const unsigned char *d, int n) {
  fw_begin(spi, FW_WRITE);
  for(int i=0;i<n;i++) spi_tx_u08(spi, d[i]);
  spi_end(spi);
}

static void fw_read(spi_t *spi, unsigned char *d, int n) {
  fw_begin(spi, FW_READ);            // the next byte is already mem[ptr]
  for(int i=0;i<n;i++) d[i] = spi_tx_u08(spi, 0);
  spi_end(spi);
}

static int fw_busy(spi_t *spi) {
  fw_begin(spi, FW_STATUS);
  unsigned char b = spi_tx_u08(spi, 0);
  spi_end(spi);
  return b & 0x01;
}

static void fw_go(spi_t *spi, int tx, int rx) {
  fw_begin(spi, FW_GO);
  spi_tx_u08(spi, (tx >> 8) & 0x03);
  spi_tx_u08(spi, tx & 0xff);
  spi_tx_u08(spi, (rx >> 8) & 0x03);
  spi_tx_u08(spi, rx & 0xff);          // the fourth byte starts it
  spi_end(spi);
}

// The engine is done in well under a millisecond for anything we ask of
// it (a 260-byte page program is 260 bytes at MCLK = clk/4), so spinning
// is cheaper than yielding.  The bound is only there so that a dead link
// cannot hang the OSD task for ever.
static int fw_wait(spi_t *spi) {
  for(int i=0;i<100000;i++)
    if(!fw_busy(spi)) return 0;
  printf("flashwr: the transaction engine never went idle\r\n");
  return -1;
}

// one flash transaction: `tx` bytes out, `rx` bytes back at buffer 0
static int fw_xfer(spi_t *spi, const unsigned char *tx, int txn,
                   unsigned char *rx, int rxn) {
  fw_ptr(spi, 0);
  fw_write(spi, tx, txn);
  fw_go(spi, txn, rxn);
  if(fw_wait(spi) < 0) return -1;
  if(rxn) {
    fw_ptr(spi, 0);
    fw_read(spi, rx, rxn);
  }
  return 0;
}

// ---------------------------------------------------------------------
// the W25Q, built out of those
// ---------------------------------------------------------------------

unsigned long flash_id(spi_t *spi) {
  unsigned char tx[1] = { CMD_JEDEC_ID };
  unsigned char rx[3] = { 0, 0, 0 };
  if(fw_xfer(spi, tx, 1, rx, 3) < 0) return 0;
  return ((unsigned long)rx[0] << 16) | ((unsigned long)rx[1] << 8) | rx[2];
}

static int flash_sr(spi_t *spi, unsigned char *sr) {
  unsigned char tx[1] = { CMD_RDSR };
  if(fw_xfer(spi, tx, 1, sr, 1) < 0) return -1;
  return 0;
}

static int flash_wren(spi_t *spi) {
  unsigned char tx[1] = { CMD_WREN };
  return fw_xfer(spi, tx, 1, NULL, 0);
}

// A 64 KB erase is up to a second on this part and a page program up to
// 3 ms, so this is where the time goes.  Ten seconds is far past either.
static int flash_wait_wip(spi_t *spi) {
  unsigned char sr = SR_WIP;
  for(int i=0;i<10000;i++) {
    if(flash_sr(spi, &sr) < 0) return -1;
    if(!(sr & SR_WIP)) return 0;
#ifndef SDL
    if((i & 0x0f) == 0x0f) vTaskDelay(pdMS_TO_TICKS(1));
#endif
  }
  printf("flashwr: the flash stayed busy (SR %02x)\r\n", sr);
  return -1;
}

static int flash_erase_block(spi_t *spi, unsigned long addr) {
  unsigned char tx[4] = { CMD_ERASE_64K,
                          (addr >> 16) & 0xff, (addr >> 8) & 0xff, addr & 0xff };
  if(flash_wren(spi) < 0) return -1;
  if(fw_xfer(spi, tx, 4, NULL, 0) < 0) return -1;
  return flash_wait_wip(spi);
}

static int flash_program_page(spi_t *spi, unsigned long addr,
                              const unsigned char *d, int n) {
  unsigned char hdr[4] = { CMD_PAGE_PROG,
                           (addr >> 16) & 0xff, (addr >> 8) & 0xff, addr & 0xff };
  if(flash_wren(spi) < 0) return -1;
  fw_ptr(spi, 0);
  fw_write(spi, hdr, 4);
  fw_write(spi, d, n);                 // the pointer carried on from 4
  fw_go(spi, 4 + n, 0);
  if(fw_wait(spi) < 0) return -1;
  return flash_wait_wip(spi);
}

static int flash_read_at(spi_t *spi, unsigned long addr,
                         unsigned char *d, int n) {
  unsigned char tx[4] = { CMD_READ,
                          (addr >> 16) & 0xff, (addr >> 8) & 0xff, addr & 0xff };
  return fw_xfer(spi, tx, 4, d, n);
}

// Read-only.  Everything here is 9F, 05 and 03 - no WREN, so the chip
// cannot be changed even if this code is wrong.
void flash_probe(spi_t *spi) {
  unsigned char sr = 0, head[32];
  unsigned long id = flash_id(spi);

  if(id == 0 || id == 0xffffffUL) {
    printf("flash: nothing answers on the MSPI pins (ID %06lx) -"
           " no core can be installed\r\n", id);
    return;
  }
  flash_sr(spi, &sr);
  printf("flash: JEDEC ID %06lx%s, status %02x\r\n", id,
         id == FLASH_ID_W25Q64 ? " (W25Q64, 8 MB)" : " - not the expected ef4017",
         sr);

  if(flash_read_at(spi, 0, head, sizeof(head)) < 0) {
    printf("flash: cannot read address 0\r\n");
    return;
  }
  // the same two fields flash_install() insists on, read back off the
  // chip: if these are right, what is in the flash is a bitstream for
  // this FPGA, which is also a check that reads work at a real address
  unsigned long idcode =
      ((unsigned long)head[BIT_IDCODE_OFF]     << 24) |
      ((unsigned long)head[BIT_IDCODE_OFF + 1] << 16) |
      ((unsigned long)head[BIT_IDCODE_OFF + 2] <<  8) |
       (unsigned long)head[BIT_IDCODE_OFF + 3];
  if(head[BIT_MAGIC_OFF] == 0xa5 && head[BIT_MAGIC_OFF + 1] == 0xc3 &&
     idcode == BIT_IDCODE)
    printf("flash: address 0 holds a GW2AR-18C bitstream\r\n");
  else
    printf("flash: address 0 does not look like one (%02x %02x, IDCODE %08lx)"
           " - reads may be wrong\r\n",
           head[BIT_MAGIC_OFF], head[BIT_MAGIC_OFF + 1], idcode);
}

// ---------------------------------------------------------------------
// the install
// ---------------------------------------------------------------------

// flash_strerror() is in ultima.c: the host menu test links that and
// not this file, and menu.c needs the strings on both sides.

// Is this really a bitstream for the GW2AR-18C?  Nothing is erased before
// this passes: the whole risk of the design is that address 0 is what the
// board boots from, and writing anything else there is a board that needs
// openFPGALoader to come back.
static int looks_like_our_bitstream(const unsigned char *h) {
  unsigned long idcode =
      ((unsigned long)h[BIT_IDCODE_OFF]     << 24) |
      ((unsigned long)h[BIT_IDCODE_OFF + 1] << 16) |
      ((unsigned long)h[BIT_IDCODE_OFF + 2] <<  8) |
       (unsigned long)h[BIT_IDCODE_OFF + 3];
  if(h[BIT_MAGIC_OFF] != 0xa5 || h[BIT_MAGIC_OFF + 1] != 0xc3) {
    printf("flashwr: no Gowin preamble (%02x %02x, want a5 c3)\r\n",
           h[BIT_MAGIC_OFF], h[BIT_MAGIC_OFF + 1]);
    return 0;
  }
  if(idcode != BIT_IDCODE) {
    printf("flashwr: IDCODE %08lx, want %08lx\r\n", idcode, BIT_IDCODE);
    return 0;
  }
  return 1;
}

int flash_install(spi_t *spi, const char *path, flash_progress_t cb) {
  FIL fil;
  unsigned char page[FLASH_PAGE], back[FLASH_PAGE];
  UINT got;
  unsigned long size, addr;
  int blocks, pages, done;

  if(cb) cb(FLASH_STAGE_CHECK, 0, 1);

  // 1. is there a flash on those pins at all, and is it the one we know?
  unsigned long id = flash_id(spi);
  printf("flashwr: JEDEC ID %06lx\r\n", id);
  if(id == 0 || id == 0xffffffUL || ((id >> 16) & 0xff) != FLASH_MFG_WINBOND) {
    printf("flashwr: refusing - no Winbond flash answered\r\n");
    return FLASH_ERR_BUS;
  }

  // 2. the file
  sdc_lock();
  FRESULT r = f_open(&fil, path, FA_OPEN_EXISTING | FA_READ);
  if(r == FR_OK) size = f_size(&fil);
  sdc_unlock();
  if(r != FR_OK) {
    printf("flashwr: %s: f_open %d\r\n", path, r);
    return FLASH_ERR_OPEN;
  }
  printf("flashwr: %s, %lu bytes\r\n", path, size);
  if(size < 0x10000UL || size > FLASH_SLOT) {
    sdc_lock(); f_close(&fil); sdc_unlock();
    return FLASH_ERR_SIZE;
  }

  // 3. its head, before anything is destroyed
  sdc_lock();
  r = f_read(&fil, page, FLASH_PAGE, &got);
  sdc_unlock();
  if(r != FR_OK || got != FLASH_PAGE) {
    sdc_lock(); f_close(&fil); sdc_unlock();
    return FLASH_ERR_READ;
  }
  if(!looks_like_our_bitstream(page)) {
    sdc_lock(); f_close(&fil); sdc_unlock();
    return FLASH_ERR_NOTBIT;
  }

  blocks = (int)((size + FLASH_BLOCK - 1) / FLASH_BLOCK);
  pages  = (int)((size + FLASH_PAGE  - 1) / FLASH_PAGE);

  // 4. erase.  From here the board has no bitstream to boot until the
  //    write finishes - the one genuinely dangerous stretch.
  printf("flashwr: erasing %d blocks\r\n", blocks);
  for(int b=0;b<blocks;b++) {
    if(cb) cb(FLASH_STAGE_ERASE, b, blocks);
    if(flash_erase_block(spi, (unsigned long)b * FLASH_BLOCK) < 0) {
      sdc_lock(); f_close(&fil); sdc_unlock();
      return FLASH_ERR_ERASE;
    }
  }

  // 5. write, from the start of the file again
  sdc_lock(); r = f_lseek(&fil, 0); sdc_unlock();
  if(r != FR_OK) { sdc_lock(); f_close(&fil); sdc_unlock(); return FLASH_ERR_READ; }

  printf("flashwr: writing %d pages\r\n", pages);
  addr = 0; done = 0;
  for(;;) {
    sdc_lock();
    r = f_read(&fil, page, FLASH_PAGE, &got);
    sdc_unlock();
    if(r != FR_OK) { sdc_lock(); f_close(&fil); sdc_unlock(); return FLASH_ERR_READ; }
    if(got == 0) break;
    if(got < FLASH_PAGE) memset(page + got, 0xff, FLASH_PAGE - got);
    if(flash_program_page(spi, addr, page, FLASH_PAGE) < 0) {
      sdc_lock(); f_close(&fil); sdc_unlock();
      return FLASH_ERR_WRITE;
    }
    addr += FLASH_PAGE;
    if(cb && ((++done & 0x1f) == 0)) cb(FLASH_STAGE_WRITE, done, pages);
  }

  // 6. verify - the point of which is to find out NOW, while the user is
  //    still in front of the board and the card still holds the file
  sdc_lock(); r = f_lseek(&fil, 0); sdc_unlock();
  if(r != FR_OK) { sdc_lock(); f_close(&fil); sdc_unlock(); return FLASH_ERR_READ; }

  printf("flashwr: verifying\r\n");
  addr = 0; done = 0;
  for(;;) {
    sdc_lock();
    r = f_read(&fil, page, FLASH_PAGE, &got);
    sdc_unlock();
    if(r != FR_OK) { sdc_lock(); f_close(&fil); sdc_unlock(); return FLASH_ERR_READ; }
    if(got == 0) break;
    if(flash_read_at(spi, addr, back, (int)got) < 0) {
      sdc_lock(); f_close(&fil); sdc_unlock();
      return FLASH_ERR_VERIFY;
    }
    if(memcmp(page, back, got)) {
      printf("flashwr: mismatch at %06lx\r\n", addr);
      sdc_lock(); f_close(&fil); sdc_unlock();
      return FLASH_ERR_VERIFY;
    }
    addr += got;
    if(cb && ((++done & 0x1f) == 0)) cb(FLASH_STAGE_VERIFY, done, pages);
  }

  sdc_lock(); f_close(&fil); sdc_unlock();
  printf("flashwr: %lu bytes installed at address 0 and verified\r\n", addr);
  return 0;
}
