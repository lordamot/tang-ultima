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
#include "flashwr.h"     // BIT_MAGIC_OFF, BIT_IDCODE_OFF: the same checks as the installer
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
  unsigned char hdr[21];
  int r;

  if(cb) cb(CL_STAGE_CONNECT, 0, 0);

  // the file, and its head: the same two fields the flash installer
  // insists on, so nothing that is not a bitstream leaves the card
  sdc_lock();
  FRESULT fr = f_open(&fil, path, FA_OPEN_EXISTING | FA_READ);
  if(fr == FR_OK) size = f_size(&fil);
  sdc_unlock();
  if(fr != FR_OK) { printf("coreload: %s: f_open %d\r\n", path, fr); return CL_ERR_OPEN; }
  if(size < 0x10000UL || size > 0x100000UL) { sdc_lock(); f_close(&fil); sdc_unlock(); return CL_ERR_SIZE; }
  sdc_lock(); fr = f_read(&fil, buf, 64, &got); sdc_unlock();
  if(fr != FR_OK || got != 64) { sdc_lock(); f_close(&fil); sdc_unlock(); return CL_ERR_READ; }
  if(buf[BIT_MAGIC_OFF] != 0xa5 || buf[BIT_MAGIC_OFF + 1] != 0xc3 ||
     buf[BIT_IDCODE_OFF] != 0x00 || buf[BIT_IDCODE_OFF + 1] != 0x00 ||
     buf[BIT_IDCODE_OFF + 2] != 0x08 || buf[BIT_IDCODE_OFF + 3] != 0x1b) {
    sdc_lock(); f_close(&fil); sdc_unlock();
    return CL_ERR_NOTBIT;
  }
  sdc_lock(); fr = f_lseek(&fil, 0); sdc_unlock();
  if(fr != FR_OK) { sdc_lock(); f_close(&fil); sdc_unlock(); return CL_ERR_READ; }

  printf("coreload: sending %s, %lu bytes\r\n", path, size);

  // BEGIN: stage 2 erases its slot, a second or two
  hdr[0] = CL_BEGIN;
  hdr[1] = size & 0xff; hdr[2] = (size >> 8) & 0xff; hdr[3] = (size >> 16) & 0xff; hdr[4] = (size >> 24) & 0xff;
  memset(hdr + 5, 0, 16);
  strncpy((char *)hdr + 5, name, 15);
  cl_flush(spi);
  if(cl_push(spi, hdr, sizeof(hdr)) < 0) { r = CL_ERR_CMD11; goto out; }
  r = cl_reply(spi, 10000);
  if(r < 0)      { r = CL_ERR_TIMEOUT; goto out; }
  if(r != CL_OK) { r = CL_ERR_REFUSED; goto out; }

  // the bytes, a chunk at a time, each one acknowledged once it is in
  // stage 2's flash - so nothing arrives there while it writes
  for(;;) {
    sdc_lock();
    fr = f_read(&fil, buf, CL_CHUNK, &got);
    sdc_unlock();
    if(fr != FR_OK) { r = CL_ERR_READ; goto out; }
    if(got == 0) break;
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
  r = cl_reply(spi, 3000);
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
  int r = cl_reply(spi, 3000);
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
  case CL_ERR_OPEN:     return "cannot open the core file";
  case CL_ERR_SIZE:     return "the core file is the wrong size";
  case CL_ERR_NOTBIT:   return "not a bitstream for this FPGA";
  case CL_ERR_READ:     return "cannot read the core file";
  case CL_ERR_TIMEOUT:  return "the board's BL616 stopped answering";
  case CL_ERR_REFUSED:
    switch(last_code) {
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
