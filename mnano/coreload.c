/*
  coreload.c - tang-ultima: a core into the FPGA's SRAM, through the
  board's own BL616.  See coreload.h.

  The FPGA side (mister/coreload.v) is a 2 KB TX FIFO into the UART and
  two single-byte answers: a status - room for 512 more bytes, and how
  many bytes have come back since the last flush - and the last byte
  that came back.  Everything stage 2 says is one byte, so that is all
  the dock ever needs to read (the module says why).
*/

#include <stdio.h>
#include <string.h>
#include <ff.h>

#include "coreload.h"
#include "coreload_proto.h"
#include "sysctrl.h"
#include "sdc.h"

#ifndef SDL
#include <FreeRTOS.h>
#include <task.h>
#endif

// coreload.v's sub-commands, the first byte of every CMD 11 stream
#define SUB_STATUS 0x00
#define SUB_WRITE  0x01
#define SUB_LAST   0x02
#define SUB_CLAIM  0x03
#define SUB_FLUSH  0x04

#define STATUS_ROOM  0x80
#define STATUS_COUNT 0x7f

#define PUSH 512     // what one SUB_WRITE carries: the FIFO's "room"

static unsigned char last_code;
unsigned char cl_last_code(void) { return last_code; }

// the status byte as the ping last saw it, for the failure screen: it
// tells the FPGA's side of the link (CMD 11 answering 0x80 = room, no
// bytes) from stage 2's (a count that never came)
static unsigned char ping_status;
unsigned char cl_ping_status(void) { return ping_status; }

// ---------------------------------------------------------------------
// the primitives
// ---------------------------------------------------------------------

static void cl_begin(spi_t *spi, unsigned char sub) {
  spi_begin(spi);
  spi_tx_u08(spi, SPI_TARGET_SYS);
  spi_tx_u08(spi, SPI_SYS_CORELOAD);
  spi_tx_u08(spi, sub);
}

static unsigned char cl_status(spi_t *spi) {
  cl_begin(spi, SUB_STATUS);
  unsigned char b = spi_tx_u08(spi, 0);
  spi_end(spi);
  return b;
}

static unsigned char cl_last(spi_t *spi) {
  cl_begin(spi, SUB_LAST);
  unsigned char b = spi_tx_u08(spi, 0);
  spi_end(spi);
  return b;
}

static void cl_write(spi_t *spi, const unsigned char *d, int n) {
  cl_begin(spi, SUB_WRITE);
  for(int i=0;i<n;i++) spi_tx_u08(spi, d[i]);
  spi_end(spi);
}

static void cl_claim(spi_t *spi, int on) {
  cl_begin(spi, SUB_CLAIM);
  spi_tx_u08(spi, on ? 1 : 0);
  spi_end(spi);
}

static void cl_flush(spi_t *spi) {
  cl_begin(spi, SUB_FLUSH);
  spi_end(spi);
}

// ---------------------------------------------------------------------
// the conversation
// ---------------------------------------------------------------------

static void cl_delay(int ms) {
#ifndef SDL
  vTaskDelay(pdMS_TO_TICKS(ms));
#else
  (void)ms;
#endif
}

// push `n` bytes, PUSH at a time, waiting for room; 0, or -1 when the
// FIFO never drains (the UART is not going anywhere)
static int cl_push(spi_t *spi, const unsigned char *d, int n) {
  while(n > 0) {
    int i;
    for(i=0;i<2000;i++) {
      if(cl_status(spi) & STATUS_ROOM) break;
      cl_delay(1);
    }
    if(i == 2000) return -1;
    int k = n < PUSH ? n : PUSH;
    cl_write(spi, d, k);
    d += k; n -= k;
  }
  return 0;
}

// wait for one answer byte: it, or -1 after `ms` without one
static int cl_reply(spi_t *spi, int ms) {
  for(int i=0;i<ms;i++) {
    if(cl_status(spi) & STATUS_COUNT) {
      last_code = cl_last(spi);
      return last_code;
    }
    cl_delay(1);
  }
  return -1;
}

void cl_release(spi_t *spi) {
  cl_claim(spi, 0);
}

int cl_ping(spi_t *spi) {
  unsigned char b = CL_PING;
  cl_claim(spi, 1);
  cl_flush(spi);
  ping_status = cl_status(spi);
  if(cl_push(spi, &b, 1) < 0) return CL_ERR_CMD11;
  if(cl_reply(spi, 500) != CL_OK) { ping_status = cl_status(spi); return CL_ERR_LINK; }
  b = CL_CAPS;
  cl_flush(spi);
  if(cl_push(spi, &b, 1) < 0) return CL_ERR_CMD11;
  if(cl_reply(spi, 500) != CL_RAM_VERSION) return CL_ERR_VERSION;
  return 0;
}

static uint32_t crc32_update(uint32_t crc, const unsigned char *p, uint32_t n) {
  crc = ~crc;
  while(n--) {
    crc ^= *p++;
    for(int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1)));
  }
  return ~crc;
}

int cl_send(spi_t *spi, const char *path, const char *name, cl_progress_t cb) {
  FIL fil;
  static unsigned char buf[CL_CHUNK];
  UINT got;
  unsigned long size, sent = 0;
  uint32_t crc = 0;
  unsigned char hdr[29];
  unsigned char container[16];
  char ram_path[300];
  uint32_t raw_size, raw_crc;
  int r;

  if(cb) cb(CL_STAGE_CONNECT, 0, 0);

  // The .bin remains available for Save to flash. Switching requires its
  // RAM-sized .ram sidecar and a RAM-capable stage 2; never fall back to B.
  if(snprintf(ram_path, sizeof(ram_path), "%s.ram", path) >= (int)sizeof(ram_path))
    return CL_ERR_OPEN;
  sdc_lock();
  FRESULT fr = f_open(&fil, ram_path, FA_OPEN_EXISTING | FA_READ);
  if(fr == FR_OK) size = f_size(&fil);
  sdc_unlock();
  if(fr != FR_OK) { printf("coreload: %s: f_open %d\r\n", ram_path, fr); return CL_ERR_RAMFILE; }
  if(size <= 16 || size > 360UL * 1024 + 16) { r = CL_ERR_SIZE; goto out; }
  sdc_lock(); fr = f_read(&fil, container, sizeof(container), &got); sdc_unlock();
  if(fr != FR_OK || got != sizeof(container)) { r = CL_ERR_READ; goto out; }
  if(memcmp(container, "URM1", 4)) { r = CL_ERR_NOTBIT; goto out; }
  raw_size = (uint32_t)container[4] | ((uint32_t)container[5] << 8) |
             ((uint32_t)container[6] << 16) | ((uint32_t)container[7] << 24);
  raw_crc = (uint32_t)container[8] | ((uint32_t)container[9] << 8) |
            ((uint32_t)container[10] << 16) | ((uint32_t)container[11] << 24);
  uint32_t packed_size = (uint32_t)container[12] | ((uint32_t)container[13] << 8) |
                        ((uint32_t)container[14] << 16) | ((uint32_t)container[15] << 24);
  if(raw_size < 0x10000 || raw_size > 0x100000 || packed_size != size - 16) {
    r = CL_ERR_SIZE; goto out;
  }
  size = packed_size;

  printf("coreload: sending %s, %lu bytes\r\n", path, size);

  // BEGIN_RAM: reserve the bounded SRAM buffer; no flash access.
  hdr[0] = CL_BEGIN_RAM;
  hdr[1] = size & 0xff; hdr[2] = (size >> 8) & 0xff; hdr[3] = (size >> 16) & 0xff; hdr[4] = (size >> 24) & 0xff;
  memset(hdr + 5, 0, 16);
  strncpy((char *)hdr + 5, name, 15);
  for(int i = 0; i < 4; i++) {
    hdr[21 + i] = raw_size >> (8 * i);
    hdr[25 + i] = raw_crc >> (8 * i);
  }
  cl_flush(spi);
  if(cl_push(spi, hdr, sizeof(hdr)) < 0) { r = CL_ERR_CMD11; goto out; }
  r = cl_reply(spi, 10000);
  if(r < 0)      { r = CL_ERR_TIMEOUT; goto out; }
  if(r != CL_OK) { r = CL_ERR_REFUSED; goto out; }

  // the bytes, a chunk at a time, each one acknowledged once it is in
  // stage 2's RAM; the full core must arrive before the FPGA is erased
  while(sent < size) {
    UINT want = size - sent < CL_CHUNK ? size - sent : CL_CHUNK;
    sdc_lock();
    fr = f_read(&fil, buf, want, &got);
    sdc_unlock();
    if(fr != FR_OK || got != want) { r = CL_ERR_READ; goto out; }
    crc = crc32_update(crc, buf, got);
    cl_flush(spi);
    if(cl_push(spi, buf, got) < 0) { r = CL_ERR_CMD11; goto out; }
    r = cl_reply(spi, 3000);
    if(r < 0)      { r = CL_ERR_TIMEOUT; goto out; }
    if(r != CL_OK) { r = CL_ERR_REFUSED; goto out; }
    sent += got;
    if(cb) cb(CL_STAGE_SEND, (int)(sent >> 10), (int)(size >> 10));
  }

  // the crc, and the answer that says the core is staged
  hdr[0] = crc & 0xff; hdr[1] = (crc >> 8) & 0xff; hdr[2] = (crc >> 16) & 0xff; hdr[3] = (crc >> 24) & 0xff;
  cl_flush(spi);
  if(cl_push(spi, hdr, 4) < 0) { r = CL_ERR_CMD11; goto out; }
  r = cl_reply(spi, 30000);
  if(r < 0)      { r = CL_ERR_TIMEOUT; goto out; }
  if(r != CL_OK) { r = CL_ERR_REFUSED; goto out; }
  printf("coreload: %lu bytes staged, crc %08lx\r\n", sent, (unsigned long)crc);
  r = 0;

out:
  sdc_lock(); f_close(&fil); sdc_unlock();
  return r;
}

static int cl_command(spi_t *spi, unsigned char c) {
  cl_flush(spi);
  if(cl_push(spi, &c, 1) < 0) return CL_ERR_CMD11;
  int r = cl_reply(spi, 30000);
  if(r < 0)      return CL_ERR_TIMEOUT;
  if(r != CL_OK) return CL_ERR_REFUSED;
  return 0;
}

int cl_load(spi_t *spi)   { return cl_command(spi, CL_LOAD); }
int cl_reload(spi_t *spi) { return cl_command(spi, CL_RELOAD); }

const char *cl_strerror(int err) {
  switch(err) {
  case 0:               return "ok";
  case CL_ERR_LINK: {
    // the status after the wait: 0x80 is "room, nothing came back"
    static char s[40];
    snprintf(s, sizeof(s), "no answer from stage 2 (st %02x)", ping_status);
    return s;
  }
  case CL_ERR_CMD11: {
    static char s[40];
    snprintf(s, sizeof(s), "CMD 11: FIFO never has room (st %02x)", ping_status);
    return s;
  }
  case CL_ERR_VERSION:  return "Update on-board firmware";
  case CL_ERR_RAMFILE:  return "SD core .bin.ram missing";
  case CL_ERR_OPEN:     return "cannot open the core file";
  case CL_ERR_SIZE:     return "the core file is the wrong size";
  case CL_ERR_NOTBIT:   return "not a bitstream for this FPGA";
  case CL_ERR_READ:     return "cannot read the core file";
  case CL_ERR_TIMEOUT:  return "the board's BL616 stopped answering";
  case CL_ERR_REFUSED:
    switch(last_code) {
    case CL_EVERSION: return "refused: RAM protocol needed";
    case CL_ELENGTH:  return "refused: the size";
    case CL_ECRC:     return "refused: the crc does not match";
    case CL_ENOSTAGE: return "refused: nothing staged";
    case CL_EFLASH:   return "refused: its flash failed";
    case CL_EIDCODE:  return "refused: no FPGA on its JTAG";
    case CL_EERASE:   return "refused: SRAM erase failed";
    case CL_ELOAD:    return "refused: the load failed";
    case CL_EABORT:   return "refused: the transfer broke";
    default:          return "refused";
    }
  default:              return "unknown error";
  }
}
