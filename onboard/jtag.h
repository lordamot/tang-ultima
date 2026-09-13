/*
  jtag.h - JTAG bit-banged on the on-board BL616's four dedicated pins
  (FPGA-Companion's table for the Tang Nano 20K: TCK GPIO 10, TDI 12,
  TDO 14, TMS 16).  One TAP, IR length 8 - the GW2AR-18 alone.
*/

#ifndef JTAG_H
#define JTAG_H

#include <stdint.h>

#define JTAG_TCK  10
#define JTAG_TDI  12
#define JTAG_TDO  14
#define JTAG_TMS  16

void jtag_init(void);
void jtag_reset(void);            /* TEST-LOGIC-RESET, then RUN-TEST/IDLE */
void jtag_idle_clocks(int n);     /* n TCK pulses in RUN-TEST/IDLE */

/* IR: 8 bits LSB first, back to RUN-TEST/IDLE */
void jtag_shift_ir(uint8_t ir);

/* DR: `bits` bits LSB first from tdi (may be NULL: zeros), TDO into tdo
   (may be NULL).  `last` = leave SHIFT-DR on the final bit and go to
   RUN-TEST/IDLE; otherwise stay in SHIFT-DR for the next call. */
void jtag_shift_dr(const uint8_t *tdi, uint8_t *tdo, uint32_t bits, int last);

/* DR: bytes MSB first - the order a bitstream is loaded in */
void jtag_shift_dr_msb(const uint8_t *data, uint32_t bytes, int last);

#endif
