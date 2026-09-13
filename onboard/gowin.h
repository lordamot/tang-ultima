/*
  gowin.h - the GW2AR-18's JTAG configuration commands, as openFPGALoader
  (src/gowin.cpp) issues them for an SRAM load: the opcodes, the status
  bits, and the erase / write sequence.  Nothing here is from a datasheet
  Gowin publishes; it is what is known to work on this device.
*/

#ifndef GOWIN_H
#define GOWIN_H

#include <stdint.h>

#define GW_NOOP            0x02
#define GW_ERASE_SRAM      0x05
#define GW_XFER_DONE       0x09
#define GW_READ_IDCODE     0x11
#define GW_INIT_ADDR       0x12
#define GW_READ_USERCODE   0x13
#define GW_CONFIG_ENABLE   0x15
#define GW_XFER_WRITE      0x17
#define GW_CONFIG_DISABLE  0x3A
#define GW_RELOAD          0x3C
#define GW_STATUS          0x41

#define GW_ST_CRC_ERROR        (1u << 0)
#define GW_ST_BAD_COMMAND      (1u << 1)
#define GW_ST_ID_VERIFY_FAILED (1u << 2)
#define GW_ST_TIMEOUT          (1u << 3)
#define GW_ST_MEMORY_ERASE     (1u << 5)
#define GW_ST_PREAMBLE         (1u << 6)
#define GW_ST_SYSTEM_EDIT_MODE (1u << 7)
#define GW_ST_GOWIN_VLD        (1u << 12)
#define GW_ST_DONE_FINAL       (1u << 13)
#define GW_ST_READY            (1u << 15)
#define GW_ST_POR              (1u << 16)

#define GW2AR18_IDCODE     0x0000081bu

void     gw_command(uint8_t cmd);          /* IR, then 6 idle clocks */
uint32_t gw_read32(uint8_t cmd);           /* IR, then a 32-bit DR read */
uint32_t gw_idcode(void);
uint32_t gw_status(void);

int gw_erase_sram(void);                   /* 0 ok, else the step that failed */

/* openFPGALoader's reset(): the device reconfigures from its flash,
   address 0 - the same RELOAD that brings a board up after a JTAG flash
   write.  Whatever was loaded into the SRAM is gone. */
void gw_reload(void);

/* the SRAM load, streamed: begin, then the chunks (bytes, MSB first;
   `last` set on the final one, which leaves SHIFT-DR), then end - which
   finishes the transfer and returns the status */
void     gw_write_begin(void);
void     gw_write_chunk(const uint8_t *data, uint32_t bytes, int last);
uint32_t gw_write_end(void);

#endif
