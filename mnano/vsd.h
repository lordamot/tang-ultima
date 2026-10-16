/*
  vsd.h - the Evo's SD card: the Tang's own card, rooted at /evo/SD/.

  The ZX Evolution has an SD card behind its Z-controller ports, and
  everything on it - the EVO Reset Service's file browser, the Dune
  loader, Wild Commander, NedoOS - expects a FAT card there.  Here that
  card is the Tang's own FAT32 card, sector for sector, with two changes:
  the boot sector names the folder /evo/SD/ as the root directory, and
  the MBR is made up (one partition, the card's volume).  Everything the
  Evo reads is the card's - its FATs, its clusters, its directories - and
  everything it writes goes onto the card at once: a file or a directory
  made, renamed, grown or deleted on the Evo side is there in /evo/SD/
  the moment the Evo's driver writes its sectors.

  What keeps the rest of the card safe: at start the folder's tree is
  walked and its clusters noted (the "inside" clusters).  The Evo may
  write a data sector only into an inside cluster or a free one; may
  change a FAT entry only of an inside cluster or a free one (allocating
  it makes it inside, freeing it takes it out); and may not write the
  MBR, the boot sectors or the reserved area (the FSInfo and its backup
  excepted).  A write refused is answered with the data error token; a
  FAT sector is written with the refused entries left as they were.

  The folder's own ".." entries: the root's "." and ".." are shown as two
  volume-label entries (a root has neither) and put back on a write; a
  subdirectory's ".." that names /evo/SD reads as cluster 0 (the root),
  and 0 is written back as /evo/SD.

  The card must be FAT32 (as the siblings' cards are); on any other the
  Evo sees no card.  `.claude/docs/mcu.md` has the rest.
*/
#ifndef VSD_H
#define VSD_H

#include <stdint.h>
#include "spi.h"

#define VSD_EXTENTS     4096        // runs of inside clusters
#define VSD_DIRS        2048        // directories the start-up walk can queue
#define VSD_PART_START  2048        // where the volume is shown on a card without partitions

// find /evo/SD ("/sd/evo/SD", made if missing) on the card and walk its
// tree; returns the number of files and directories under it, or -1 if
// there is no card for the Evo (no folder, or not FAT32)
int vsd_scan(const char *root);

// the card's size in sectors, for the CSD
uint32_t vsd_sectors(void);

// a sector of the card; 0 if it was served, -1 if the card's own read or
// write failed or the write was refused (the core then sends the data
// error token)
int vsd_read(uint32_t lba, uint8_t *buf);
int vsd_write(uint32_t lba, const uint8_t *buf);

// statistics, for the OSD and the test
typedef struct {
  int files, dirs, fragmented, extents;
  uint32_t clusters, spc, root;
  unsigned long reads, writes, refused, fat_kept, errors;
} vsd_info_t;
extern vsd_info_t vsd_info;

// the core: at start (after the scan), tell sdcard.v the size and that a
// card is in; on its interrupt (sysctrl's irq 4), serve the request
void vsd_boot(spi_t *spi, const char *root);
void vsd_handle_event(spi_t *spi);

#endif // VSD_H
