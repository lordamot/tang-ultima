/*
  main.c - stage 2 for the Tang Nano 20K's on-board BL616.

  Started by Sipeed's FPGA Partner (flash address 0) at 0x40000 when the
  board is powered from something that is not a PC.  It waits on the
  UART from the FPGA (link.c: the FPGA's pin 69 into GPIO 13, GPIO 11
  back into pin 70) for the dock's firmware, relayed through whatever
  core is running (mister/coreload.v, SYS CMD 11), to send it a core:
  the bytes go into this chip's own flash (stage.h), and on CL_LOAD it
  checks the crc and loads them into the FPGA's SRAM over the JTAG it
  owns - the way `openFPGALoader -b tangnano20k <core>.fs` does, minus
  the cable.  The FPGA then runs that core until power-off or the next
  load; at power-up it boots its own flash, as always, and this waits
  again.  protocol.h is the whole of what is said.

  Nothing here is visible from a PC - the Partner does not start this
  when one is attached - so every step, and the first bytes each way on the link,
  go as (tag, value) pairs into the LOG sector - one power-up, one log -
  which `make onboard-log` reads back in boot mode.  .claude/docs/onboard.md is the account.
*/

#include <stdio.h>
#include <string.h>

#include "board.h"
#include "bflb_flash.h"
#include "bflb_gpio.h"
#include "bflb_mtimer.h"

#include "stage.h"
#include "protocol.h"
#include "link.h"
#include "jtag.h"
#include "gowin.h"

#ifndef STAGE2_BUILD
#define STAGE2_BUILD 0
#endif

#define BYTE_TIMEOUT_MS 3000   /* silence in a transfer that ends it */

static uint8_t chunk[CL_CHUNK];
static uint32_t log_off;

// ---------------------------------------------------------------------
// the log
// ---------------------------------------------------------------------

static void log_put(uint32_t tag, uint32_t value) {
  uint32_t rec[2] = { tag, value };
  if(log_off + sizeof(rec) > LOG_SIZE) return;
  bflb_flash_write(LOG_ADDR + log_off, (uint8_t *)rec, sizeof(rec));
  log_off += sizeof(rec);
  printf("stage2: %02lx %08lx\r\n", (unsigned long)tag, (unsigned long)value);
}

static void log_reset(void) {
  bflb_flash_erase(LOG_ADDR, LOG_SIZE);
  log_off = 0;
  log_put(LOG_START, STAGE2_BUILD);
}

// ---------------------------------------------------------------------
// the link, logged: the first LOG_BYTES bytes each way go into the LOG
// with the time they passed, so a board that "does not answer" can be
// read afterwards - was anything heard, was anything said
// ---------------------------------------------------------------------

static int rx_logged, tx_logged;

static int rx_wait(uint32_t timeout_ms) {
  int b = link_get_wait(timeout_ms);
  if(b >= 0 && rx_logged < LOG_BYTES) {
    rx_logged++;
    log_put(LOG_RX, ((uint32_t)bflb_mtimer_get_time_ms() << 8) | (uint32_t)b);
  }
  return b;
}

static void tx_put(uint8_t b) {
  if(tx_logged < LOG_BYTES) {
    tx_logged++;
    log_put(LOG_TX, ((uint32_t)bflb_mtimer_get_time_ms() << 8) | b);
  }
  link_put(b);
}

// ---------------------------------------------------------------------
// the staged core
// ---------------------------------------------------------------------

static uint32_t crc32_update(uint32_t crc, const uint8_t *p, uint32_t n) {
  crc = ~crc;
  while(n--) {
    crc ^= *p++;
    for(int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1)));
  }
  return ~crc;
}

// the descriptor in flash, if it is one: 0 ok, else a CL_E* code
static int stage_desc(struct stage_desc *d) {
  bflb_flash_read(STAGE_ADDR, (uint8_t *)d, sizeof(*d));
  if(d->magic != STAGE_MAGIC) return CL_ENOSTAGE;
  if(d->length == 0 || d->length > STAGE_MAX_LEN) return CL_ENOSTAGE;
  if(d->idcode != GW2AR18_IDCODE) return CL_ENOSTAGE;
  return CL_OK;
}

// the crc of what the flash holds against the descriptor
static int stage_check(const struct stage_desc *d) {
  uint32_t crc = 0;
  for(uint32_t off = 0; off < d->length; off += sizeof(chunk)) {
    uint32_t n = d->length - off;
    if(n > sizeof(chunk)) n = sizeof(chunk);
    bflb_flash_read(STAGE_ADDR + STAGE_DATA_OFF + off, chunk, n);
    crc = crc32_update(crc, chunk, n);
  }
  return crc == d->crc32 ? CL_OK : CL_ECRC;
}

// ---------------------------------------------------------------------
// the FPGA
// ---------------------------------------------------------------------

// the SRAM load of the staged core; logs every step; 0 or a CL_E* code
static int fpga_load(const struct stage_desc *d) {
  jtag_init();
  jtag_reset();
  uint32_t id = gw_idcode();
  log_put(LOG_IDCODE, id);
  if(id != GW2AR18_IDCODE) return CL_EIDCODE;
  log_put(LOG_STATUS0, gw_status());

  int e = gw_erase_sram();
  log_put(LOG_ERASE, e);
  log_put(LOG_STATUS1, gw_status());
  if(e) return CL_EERASE;

  uint64_t t0 = bflb_mtimer_get_time_ms();
  gw_write_begin();
  for(uint32_t off = 0; off < d->length; off += sizeof(chunk)) {
    uint32_t n = d->length - off;
    if(n > sizeof(chunk)) n = sizeof(chunk);
    bflb_flash_read(STAGE_ADDR + STAGE_DATA_OFF + off, chunk, n);
    gw_write_chunk(chunk, n, off + n >= d->length);
  }
  uint32_t st = gw_write_end();
  log_put(LOG_WRITE, st);
  log_put(LOG_DONE, (st & GW_ST_DONE_FINAL) ? 1 : 0);
  log_put(LOG_MS, (uint32_t)(bflb_mtimer_get_time_ms() - t0));
  return (st & GW_ST_DONE_FINAL) ? CL_OK : CL_ELOAD;
}

// ---------------------------------------------------------------------
// the commands
// ---------------------------------------------------------------------

// CL_BEGIN: the header, the erase, then the bytes in acknowledged
// chunks; the descriptor is written only once every byte is in, so a
// transfer that stops early leaves nothing that looks complete
static void cmd_begin(void) {
  uint8_t hdr[20];
  struct stage_desc d;

  for(int i = 0; i < (int)sizeof(hdr); i++) {
    int b = rx_wait(BYTE_TIMEOUT_MS);
    if(b < 0) return;
    hdr[i] = (uint8_t)b;
  }
  memset(&d, 0, sizeof(d));
  d.magic  = STAGE_MAGIC;
  d.length = (uint32_t)hdr[0] | ((uint32_t)hdr[1] << 8) | ((uint32_t)hdr[2] << 16) | ((uint32_t)hdr[3] << 24);
  d.idcode = GW2AR18_IDCODE;
  memcpy(d.name, hdr + 4, 15);

  if(d.length < 0x10000 || d.length > STAGE_MAX_LEN) { tx_put(CL_ELENGTH); return; }

  // the descriptor sector and the data: erased in one go, sector-rounded
  uint32_t span = (STAGE_DATA_OFF + d.length + 0xfff) & ~0xfffu;
  if(bflb_flash_erase(STAGE_ADDR, span) != 0) { tx_put(CL_EFLASH); return; }
  tx_put(CL_OK);

  for(uint32_t off = 0; off < d.length; off += CL_CHUNK) {
    uint32_t n = d.length - off;
    if(n > CL_CHUNK) n = CL_CHUNK;
    for(uint32_t i = 0; i < n; i++) {
      int b = rx_wait(BYTE_TIMEOUT_MS);
      if(b < 0) { tx_put(CL_EABORT); return; }
      chunk[i] = (uint8_t)b;
    }
    if(bflb_flash_write(STAGE_ADDR + STAGE_DATA_OFF + off, chunk, n) != 0) {
      tx_put(CL_EFLASH); return;
    }
    tx_put(CL_OK);
  }

  // the crc, computed by the dock as it read the file; then every byte
  // is in and the descriptor makes it a staged core
  for(int i = 0; i < 4; i++) {
    int b = rx_wait(BYTE_TIMEOUT_MS);
    if(b < 0) { tx_put(CL_EABORT); return; }
    d.crc32 |= (uint32_t)b << (8 * i);
  }
  if(bflb_flash_write(STAGE_ADDR, (uint8_t *)&d, sizeof(d)) != 0) { tx_put(CL_EFLASH); return; }
  tx_put(CL_OK);
  printf("stage2: staged %s, %lu bytes, crc %08lx\r\n", d.name, (unsigned long)d.length, (unsigned long)d.crc32);
}

static void cmd_load(void) {
  struct stage_desc d;
  int r = stage_desc(&d);
  if(r == CL_OK) r = stage_check(&d);
  // the log is not reset here: one power-up is one log, so what the
  // link carried before the first load survives it
  log_put(LOG_DESC, r);
  if(r != CL_OK) { tx_put(r); return; }
  log_put(LOG_LENGTH, d.length);
  log_put(LOG_CRC, 0);

  // the dock is told now: from here the FPGA, and the link through it,
  // are gone until the new core is up
  tx_put(CL_OK);
  bflb_mtimer_delay_ms(100);     // let the byte leave, and the dock read it

  r = fpga_load(&d);
  log_put(LOG_END, r);
  if(r != CL_OK) {
    // a blank FPGA is a board with no link at all; bring the flash's
    // core back so the dock finds something
    printf("stage2: load failed (%d), reloading from flash\r\n", r);
    gw_reload();
  }
}

static void cmd_reload(void) {
  tx_put(CL_OK);
  bflb_mtimer_delay_ms(100);
  jtag_init();
  jtag_reset();
  gw_reload();
}

static void cmd_status(void) {
  struct stage_desc d;
  int r = stage_desc(&d);
  if(r == CL_OK) r = stage_check(&d);
  tx_put(r);
}

int main(void) {
  board_init();
  printf("\r\nTang Ultima stage 2, build %08lx\r\n", (unsigned long)STAGE2_BUILD);

  log_reset();
  log_put(LOG_FLASH, bflb_flash_get_jedec_id());
  log_put(LOG_FLASHSIZE, bflb_flash_get_size());

  // the two link pads read as plain inputs first: the FPGA's TX into
  // GPIO 13 idles high if the pin is the one we think it is
  struct bflb_device_s *gpio = bflb_device_get_by_name("gpio");
  bflb_gpio_init(gpio, LINK_RX_PIN, GPIO_INPUT | GPIO_FLOAT | GPIO_SMT_EN);
  bflb_gpio_init(gpio, LINK_TX_PIN, GPIO_INPUT | GPIO_FLOAT | GPIO_SMT_EN);
  bflb_mtimer_delay_ms(1);
  log_put(LOG_PINS, (bflb_gpio_read(gpio, LINK_RX_PIN) ? 2u : 0u) |
                    (bflb_gpio_read(gpio, LINK_TX_PIN) ? 1u : 0u));

  link_init();
  log_put(LOG_READY, (uint32_t)bflb_mtimer_get_time_ms());
  printf("stage2: waiting on the link\r\n");

  for(;;) {
    int c = rx_wait(1000);
    switch(c) {
    case CL_PING:   tx_put(CL_OK); break;
    case CL_BEGIN:  cmd_begin();     break;
    case CL_LOAD:   cmd_load();      break;
    case CL_RELOAD: cmd_reload();    break;
    case CL_STATUS: cmd_status();    break;
    default: break;                  // silence, or a byte that is not ours
    }
  }
  return 0;
}
