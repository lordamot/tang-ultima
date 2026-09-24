/*
  ultima.h - Tang Ultima: five machines in one Tang Nano 20K.

  The machines live on the card as packed bitstreams,
  /sd/cores/<name>.bin.  A SWITCH sends one of them to the board's own
  BL616, which loads it into the FPGA's SRAM over the JTAG it owns
  (coreload.c; onboard/ is that chip's firmware) - a few seconds, no
  power cycle, and this MCU resets itself to come up on the new machine.
  The FPGA's flash holds ONE bitstream, at address 0, which is what
  power-up loads; "Save to flash" writes the running machine there
  (flashwr.c, through flashwr.v and the MSPI pins), so the choice
  survives a power cycle.

  It used to be MultiBoot: three slots, each header naming the next, SYS
  command 9 pulsing RECONFIG_N to jump.  That does not work on this board.
  The pulse is generated and the FPGA ignores it, because reusing the pad
  as a GPIO cuts it from the configuration controller, and pin 9 reaches
  nothing but a test pad (.claude/docs/progress.md, 13 September 2026).
  Then it was the flash write alone, with a power cycle after it.

  Each core keeps its files under its own directory on the card -
  /sd/uknc, /sd/pk8000, /sd/korvet, /sd/zs256, /sd/bk - and sdc.c asks
  here which one it is.
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

// the five, in the Makefile's CORES order
extern const ultima_core_t ultima_cores[];
#define ULTIMA_CORES 5

// where the installed core is recorded, on the card's root
#define ULTIMA_INI   CARD_MOUNTPOINT "/ultima.ini"
// and where the bitstreams live: /sd/cores/uknc.bin and its four siblings
#define ULTIMA_COREDIR CARD_MOUNTPOINT "/cores"

// the table entry of a core id, or NULL for a core not in the ring
const ultima_core_t *ultima_core(unsigned char id);
// the running core's directory on the card ("/sd/korvet"), or the card's
// root for a core that is not one of the five
const char *ultima_root(void);

// At start, once the card is readable: say which machine came up and
// make its directory if the card has not got one.
void ultima_boot(spi_t *spi);

// where a core's bitstream is expected on the card
const char *ultima_core_path(const ultima_core_t *c);

// From the OSD: send that core's bitstream from the card to the board's
// BL616 and have it loaded into the FPGA's SRAM.  Returns only on
// failure - a negative CL_ERR_* (coreload.h) - because on success this
// MCU resets itself while the FPGA becomes the other machine.  The
// running core is a no-op, 0.
int ultima_switch(spi_t *spi, unsigned char id, flash_progress_t cb);

// From the OSD: write the RUNNING core's bitstream from the card into
// flash address 0, verify it, and record it in ULTIMA_INI - so that it
// is what power-up loads.  0 on success, one of flashwr.h's FLASH_ERR_*
// otherwise; past the erase, a failure is a board that needs
// openFPGALoader, which the caller must say out loud.
int ultima_install(spi_t *spi, flash_progress_t cb);

// the core ULTIMA_INI records as saved in the flash, or 0 when it says none
unsigned char ultima_wanted(void);

// the string for either function's result
const char *ultima_strerror(int err);

#endif // ULTIMA_H
