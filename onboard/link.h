/*
  link.h - the UART to the FPGA: BL616 GPIO 13 is its RX, which the FPGA
  drives on pin 69; GPIO 11 its TX, into the FPGA's pin 70.  2 Mbaud 8N1,
  the same as mister/coreload.v in every core.  Received bytes go into a
  ring from the interrupt; the main loop takes them out.
*/

#ifndef LINK_H
#define LINK_H

#include <stdint.h>

#define LINK_TX_PIN  11
#define LINK_RX_PIN  13
#define LINK_BAUD    2000000

void link_init(void);
int  link_avail(void);                 /* bytes waiting */
int  link_get(void);                   /* one byte, or -1 */
int  link_get_wait(uint32_t timeout_ms);   /* one byte, or -1 on timeout */
void link_put(uint8_t b);
void link_drain(void);                 /* throw away what is waiting */

#endif
