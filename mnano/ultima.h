/*
  ultima.h - Tang Ultima: three machines in one Tang Nano 20K.

  The flash holds ONE bitstream, at address 0, and that is the machine the
  board is.  The three machines live on the card as packed bitstreams,
  /sd/cores/<name>.bin, and switching means writing one of them to address
  0 (flashwr.c, through flashwr.v and the MSPI pins) and power-cycling the
  board - power-up always loads address 0.

  It used to be MultiBoot: three slots, each header naming the next, SYS
  command 9 pulsing RECONFIG_N to jump.  That does not work on this board.
  The pulse is generated and the FPGA ignores it, because reusing the pad
  as a GPIO cuts it from the configuration controller, and pin 9 reaches
  nothing but a test pad (.claude/docs/progress.md, 13 September 2026).

  Each core keeps its files under its own directory on the card -
  /sd/uknc, /sd/pk8000, /sd/korvet - and sdc.c asks here which one it is.
*/
#ifndef ULTIMA_H
#define ULTIMA_H

#include "spi.h"
#include "flashwr.h"

typedef struct {
  unsigned char id;      // sysctrl.h's CORE_ID_*, what the core answers CMD 0 with
  const char   *name;    // as the OSD shows it
  const char   *dir;     // its directory on the card, under CARD_MOUNTPOINT
  const char   *ini;     // its settings file, under that directory
} ultima_core_t;

// the three, in the Makefile's CORES order
extern const ultima_core_t ultima_cores[];
#define ULTIMA_CORES 3

// where the installed core is recorded, on the card's root
#define ULTIMA_INI   CARD_MOUNTPOINT "/ultima.ini"
// and where the bitstreams live: /sd/cores/uknc.bin and its two siblings
#define ULTIMA_COREDIR CARD_MOUNTPOINT "/cores"

// the table entry of a core id, or NULL for a core not in the ring
const ultima_core_t *ultima_core(unsigned char id);
// the running core's directory on the card ("/sd/korvet"), or the card's
// root for a core that is not one of the three
const char *ultima_root(void);

// At start, once the card is readable: say which machine came up, make
// its directory if the card has not got one, and warn if the card says a
// different core was installed - which means the last install did not
// finish, or the card has been moved between boards.  Nothing is
// reconfigured here; there is no way to.
void ultima_boot(spi_t *spi);

// where a core's bitstream is expected on the card
const char *ultima_core_path(const ultima_core_t *c);

// From the OSD: write that core's bitstream from the card into flash
// address 0, verify it, and record it in ULTIMA_INI.  0 on success, one
// of flashwr.h's FLASH_ERR_* otherwise - and on failure nothing has been
// written unless it got as far as the erase, which the caller must say
// out loud.  The machine does not change until the board is power-cycled.
int ultima_switch(spi_t *spi, unsigned char id, flash_progress_t cb);

// the core ULTIMA_INI records as installed, or 0 when it says none
unsigned char ultima_wanted(void);

#endif // ULTIMA_H
