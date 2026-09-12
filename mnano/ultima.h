/*
  ultima.h - Tang Ultima: three machines in one Tang Nano 20K.

  The flash holds one bitstream a machine, each one's header naming the
  next (Gowin MultiBoot, ../Makefile); SYS command 9 makes the running
  core pulse RECONFIG_N and the FPGA loads that next image.  This module
  knows the three cores, which one the card asks for (/sd/ultima.ini),
  and how to get there: hop the ring until the core id the FPGA answers
  is the wanted one.  Each core keeps its files under its own directory
  on the card - /sd/uknc, /sd/pk8000, /sd/korvet - and sdc.c asks here
  which one that is.
*/
#ifndef ULTIMA_H
#define ULTIMA_H

#include "spi.h"

typedef struct {
  unsigned char id;      // sysctrl.h's CORE_ID_*, what the core answers CMD 0 with
  const char   *name;    // as the OSD shows it
  const char   *dir;     // its directory on the card, under CARD_MOUNTPOINT
  const char   *ini;     // its settings file, under that directory
} ultima_core_t;

// the ring, in flash order (the Makefile's CORES): slot 0 boots at power-up
extern const ultima_core_t ultima_cores[];
#define ULTIMA_CORES 3

// where the wanted core is remembered, on the card's root
#define ULTIMA_INI  CARD_MOUNTPOINT "/ultima.ini"

// the table entry of a core id, or NULL for a core not in the ring
const ultima_core_t *ultima_core(unsigned char id);
// the running core's directory on the card ("/sd/korvet"), or the card's
// root for a core that is not one of the three
const char *ultima_root(void);

// At start, once the card is readable: the core the card asks for, and
// if it is not the one running, hop the ring until it is (or the ring
// has been walked once).  Holds the interrupt processing meanwhile,
// acknowledges the new core's coldboot notice, and leaves the FPGA
// answering with the machine held in reset - as main() leaves it.
void ultima_boot(spi_t *spi);

// From the OSD: remember `id` on the card, close the images, move the
// FPGA to that core and restart the MCU.  Does not return.
void ultima_switch(spi_t *spi, unsigned char id);

// the core the card asks for, or 0 when it asks for none
unsigned char ultima_wanted(void);

#endif // ULTIMA_H
