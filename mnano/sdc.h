#ifndef SDC_H
#define SDC_H

#include "spi.h"

// sd_card.v's image slots, five on every core here (what each is,
// drivename() in sdc.c): the UKNC's four floppies and the IDE image, the
// PK8000's tape, two floppies, hard disk and ROM disk, the Korvet's four
// floppies and the ОПТС ROM, the ZS-256's four floppies and the SMUC's disk.
#define MAX_DRIVES  5

// One more slot that is browsed but never mounted: the UKNC's "Run SAV:"
// (rt11sav.c), the PK8000's "Run .bas:" (bas.c) and the ZS-256's ROM
// file (romload.c) walk the card through it, so it has a working directory and a remembered name like a drive,
// but no open image, no core-side drive and no line in the settings file.
#define SDC_SLOT_EXTRA  MAX_DRIVES
#define SDC_SLOT_SAV    SDC_SLOT_EXTRA
#define SDC_SLOT_ROM    SDC_SLOT_EXTRA

// fatfs mounts the card under /sd; each core browses its own directory
// below it (ultima.h: /sd/uknc, /sd/pk8000, /sd/korvet, /sd/zs256)
#define CARD_MOUNTPOINT "/sd"

typedef struct {
  char *name;
  unsigned long len;
  int is_dir;
} sdc_dir_entry_t;

typedef struct {
  int len;
  sdc_dir_entry_t *files;
} sdc_dir_t;

int sdc_init(spi_t *spi);
int sdc_image_open(int drive, char *name);
sdc_dir_t *sdc_readdir(int drive, char *name, const char *exts);
int sdc_handle_event(void);
int sdc_is_ready(void);
void sdc_lock(void);
void sdc_unlock(void);
char *sdc_get_image_name(int drive);
char *sdc_get_cwd(int drive);
void sdc_set_default(int drive, const char *name);
void sdc_set_image_name(int drive, const char *name);
// after the FPGA has been reloaded (ultima.c): wait for the new core's
// sd_card.v to have the card up again, and mount it afresh
int  sdc_reattach(void);

#endif // SDC_H
