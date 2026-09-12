//
// extrom.h - the Korvet-EXTROM controller's brain, on the BL616.
//
// The core's extrom.v is the connector side of forth32's controller
// (the sibling repository korvet-extrom-forth32): the phase-1 ROM, the
// ВВ55 #3's mode-2 handshake, two FIFOs.  This is the ATmega's program
// (avr/extrom.c there, and the emulator patch emu_patch/ext_rom.c,
// which this follows line for line): the phase-1 loader's file request,
// then the API v2 commands - sector reads and writes of .kdi images,
// mounting, the folder, the system-track substitution - on the card's
// `extrom` folder through FatFs.  .claude/docs/extrom.md has the
// protocol and the folder's layout.
//
#ifndef EXTROM_H
#define EXTROM_H

// the card's folder the controller sees as its root
#define EXTROM_ROOT   "/sd/korvet/extrom"   // under the Korvet's directory (ultima.h)

#ifndef EXTROM_HOST_TEST
#include "spi.h"
void extrom_init(spi_t *spi);        // the phase-1 ROM into the core, the mount table
void extrom_handle_event(void);      // the core's interrupt: bytes from the machine
#else
// the host test drives the state machine directly
void extrom_reset(void);
void extrom_feed(const unsigned char *bytes, int n);
#endif

// the state machine, for the tests
int extrom_state(void);
const char *extrom_drive_file(int drive);
int extrom_drive_mounted(int drive);

#endif
