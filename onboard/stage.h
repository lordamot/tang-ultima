/*
  stage.h - the on-board BL616's flash, as stage 2 and the host agree on it.

  tools/mkstage.py writes the staged core in this form and the Makefile
  puts it at STAGE_ADDR; stage2 (main.c) reads it back and writes its log
  at LOG_ADDR.  Keep the two ends the same by hand.

    0x000000  Sipeed's FPGA Partner (FT2232 emulation, starts 0x40000)
    0x040000  stage 2 - this firmware
    0x0FE000  LOG: one 4 KB sector of (tag, value) pairs, erased at start-up,
              appended through every load of that power-up; `make onboard-log`
              reads it
    0x100000  STAGE: one 4 KB descriptor sector, then the packed bitstream
*/

#ifndef STAGE_H
#define STAGE_H

#include <stdint.h>

#define STAGE_ADDR      0x100000u
#define STAGE_DATA_OFF  0x1000u          /* the bitstream, after the descriptor */
#define STAGE_MAGIC     0x314c5554u      /* "TUL1", little-endian */
#define STAGE_MAX_LEN   (1024u * 1024u)  /* a GW2AR-18 bitstream is 907 418 */

struct stage_desc {
  uint32_t magic;      /* STAGE_MAGIC */
  uint32_t length;     /* of the bitstream at STAGE_ADDR + STAGE_DATA_OFF */
  uint32_t crc32;      /* zlib crc32 of those bytes */
  uint32_t idcode;     /* the FPGA the bitstream is for: 0x0000081b */
  char     name[16];   /* "uknc", "pk8000", "korvet", "zs256" - for the log */
};

#define LOG_ADDR        0x0FE000u
#define LOG_SIZE        0x1000u

/* the log: u32 tag, u32 value, appended; 0xffffffff is unwritten */
enum {
  LOG_START     = 0x01,  /* value: build id                        */
  LOG_FLASH     = 0x02,  /* value: jedec id                        */
  LOG_FLASHSIZE = 0x03,  /* value: bytes                           */
  LOG_DESC      = 0x04,  /* value: 0 ok, else which check failed   */
  LOG_LENGTH    = 0x05,  /* value: bitstream length                */
  LOG_CRC       = 0x06,  /* value: 0 ok, 1 mismatch                */
  LOG_IDCODE    = 0x07,  /* value: what the JTAG read              */
  LOG_STATUS0   = 0x08,  /* value: status register before anything */
  LOG_ERASE     = 0x09,  /* value: 0 ok, 1 fail                    */
  LOG_STATUS1   = 0x0a,  /* value: status after the erase          */
  LOG_WRITE     = 0x0b,  /* value: status after the load           */
  LOG_DONE      = 0x0c,  /* value: 1 DONE_FINAL up, 0 not          */
  LOG_MS        = 0x0d,  /* value: milliseconds the load took      */
  LOG_END       = 0x0e,  /* value: 0 loaded, else the step it stopped at */
  /* the link, for a board that answers nothing (13 Sep 2026 evening) */
  LOG_PINS      = 0x10,  /* value: {GPIO 13, GPIO 11} read as inputs at start */
  LOG_RX        = 0x11,  /* value: ms since start << 8 | byte, first LOG_BYTES */
  LOG_TX        = 0x12,  /* value: the same, for what stage 2 sent  */
  LOG_READY     = 0x13,  /* value: ms since start when it began to listen */
};
#define LOG_BYTES 32

#endif
