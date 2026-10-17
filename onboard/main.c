/* Stage 2: receive an XZ core into internal RAM, validate it, then decode
   into FPGA SRAM over JTAG. Diagnostic records are RAM-only too.
   Runs behind FPGA Partner at 0x40000 on non-PC power. No flash writes. */

#include <stdio.h>
#include <string.h>

#include "board.h"
#include "bflb_gpio.h"
#include "bflb_mtimer.h"

#include "stage.h"
#include "protocol.h"
#include "link.h"
#include "jtag.h"
#include "gowin.h"
#include "ramstage.h"

#ifndef STAGE2_BUILD
#define STAGE2_BUILD 0
#endif

#define BYTE_TIMEOUT_MS 3000   /* silence in a transfer that ends it */

static uint32_t log_records[LOG_SIZE / sizeof(uint32_t)];
static int staged_valid;
static uint32_t log_off;

// ---------------------------------------------------------------------
// the log
// ---------------------------------------------------------------------

static void log_put(uint32_t tag, uint32_t value) {
  uint32_t rec[2] = { tag, value };
  if(log_off + sizeof(rec) > LOG_SIZE) return;
  memcpy((uint8_t *)log_records + log_off, rec, sizeof(rec));
  log_off += sizeof(rec);
  printf("stage2: %02lx %08lx\r\n", (unsigned long)tag, (unsigned long)value);
}

static void log_reset(void) {
  memset(log_records, 0xff, sizeof(log_records));
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

// ---------------------------------------------------------------------
// the FPGA
// ---------------------------------------------------------------------

// the SRAM load of the staged core; logs every step; 0 or a CL_E* code
static int fpga_load(void) {
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
  int r = ramstage_decode(gw_write_chunk);
  if(r != CL_OK) return r;
  uint32_t st = gw_write_end();
  log_put(LOG_WRITE, st);
  log_put(LOG_DONE, (st & GW_ST_DONE_FINAL) ? 1 : 0);
  log_put(LOG_MS, (uint32_t)(bflb_mtimer_get_time_ms() - t0));
  return (st & GW_ST_DONE_FINAL) ? CL_OK : CL_ELOAD;
}

// ---------------------------------------------------------------------
// the commands
// ---------------------------------------------------------------------

static uint32_t le32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
         ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void cmd_begin(void) {
  uint8_t hdr[28], tail[4];
  staged_valid = 0;
  ramstage_invalidate();
  for(unsigned i = 0; i < sizeof(hdr); i++) {
    int b = rx_wait(BYTE_TIMEOUT_MS);
    if(b < 0) { tx_put(CL_EABORT); return; }
    hdr[i] = b;
  }
  uint32_t length = le32(hdr);
  int r = ramstage_begin(length, le32(hdr + 20), le32(hdr + 24));
  if(r != CL_OK) { tx_put(r); return; }
  tx_put(CL_OK);
  uint8_t *data = ramstage_data();
  for(uint32_t off = 0; off < length;) {
    uint32_t n = length - off;
    if(n > CL_CHUNK) n = CL_CHUNK;
    for(uint32_t i = 0; i < n; i++) {
      int b = rx_wait(BYTE_TIMEOUT_MS);
      if(b < 0) { ramstage_invalidate(); tx_put(CL_EABORT); return; }
      data[off + i] = b;
    }
    off += n;
    tx_put(CL_OK);
  }
  for(unsigned i = 0; i < sizeof(tail); i++) {
    int b = rx_wait(BYTE_TIMEOUT_MS);
    if(b < 0) { ramstage_invalidate(); tx_put(CL_EABORT); return; }
    tail[i] = b;
  }
  r = crc32_update(0, data, length) == le32(tail) ? ramstage_decode(NULL) : CL_ECRC;
  staged_valid = r == CL_OK;
  if(!staged_valid) ramstage_invalidate();
  tx_put(r);
}

static void cmd_load(void) {
  int r = staged_valid ? ramstage_decode(NULL) : CL_ENOSTAGE;
  log_put(LOG_DESC, r);
  if(r != CL_OK) { staged_valid = 0; tx_put(r); return; }
  log_put(LOG_LENGTH, ramstage_length());
  log_put(LOG_CRC, 0);

  // the dock is told now: from here the FPGA, and the link through it,
  // are gone until the new core is up
  tx_put(CL_OK);
  bflb_mtimer_delay_ms(100);     // let the byte leave, and the dock read it

  r = fpga_load();
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
  tx_put(staged_valid ? ramstage_decode(NULL) : CL_ENOSTAGE);
}

int main(void) {
  board_init();
  printf("\r\nTang Ultima stage 2, build %08lx\r\n", (unsigned long)STAGE2_BUILD);

  log_reset();

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
    case CL_CAPS:   tx_put(CL_RAM_VERSION); break;
    case CL_BEGIN:  tx_put(CL_EVERSION); break;
    case CL_BEGIN_RAM: cmd_begin(); break;
    case CL_LOAD:   cmd_load();      break;
    case CL_RELOAD: cmd_reload();    break;
    case CL_STATUS: cmd_status();    break;
    default: break;                  // silence, or a byte that is not ours
    }
  }
  return 0;
}
