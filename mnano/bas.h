//
// bas.h - "Run .bas": a BASIC program from a text file into the machine
//
#ifndef BAS_H
#define BAS_H

// the drive number the menu's file selector uses for .bas files; not a
// slot of sd_card.v (those are 0..4), the file is read here and sent
// through sysctrl.v's CMD 6
#define BAS_DRIVE 5

int bas_tokenise(const char *text, unsigned char *out, int max, unsigned *number);

#ifndef BAS_HOST_TEST
#include "spi.h"
int bas_run(spi_t *spi, const char *path);
#endif

#endif
