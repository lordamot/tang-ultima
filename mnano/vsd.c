/*
  vsd.c - the Evo's SD card: the Tang's own card, rooted at /evo/SD/.
  See vsd.h.

  The Evo's sector v is the card's sector v - shift (shift 0, or
  VSD_PART_START when the card has no partition table), and is one of:

    0                      the MBR: made up, one partition (0Ch) holding
                           the card's FAT32 volume
    before the volume      zeros; not writable
    the boot sector and    the card's, the root cluster replaced by
    its backup             /evo/SD's and the label by "EVO NANO"; not
                           writable
    the FSInfo             the card's, both ways (and its backup)
    the rest reserved      the card's; not writable
    a FAT                  the card's; a write is merged entry by entry
                           (fat_write)
    a cluster              the card's; writable if inside or free, with
                           the dot entries translated (vsd.h)
    after the volume       zeros; not writable

  Every write the Evo makes is a write of the card's sector, and FatFs's
  own window is dropped if it holds that sector, so the firmware's next
  file operation reads the card as the Evo left it.  The FAT sectors
  the guard reads are cached (FATC of them) until anyone else takes the
  card's lock (sdc_locks counts).

  On the board the card's sectors are read through FatFs's diskio
  (sdc.c, the FPGA's sd_card.v); on the host (make vsd-test) the test's
  in-memory card.
*/
#include <stdio.h>
#include <string.h>
#include <ff.h>
#include <diskio.h>

#include "vsd.h"
#include "sdc.h"

vsd_info_t vsd_info;

static FATFS   *fs;             // the card, as FatFs mounted it; NULL: no card for the Evo
static uint32_t sdroot;         // /evo/SD's first cluster: the Evo's root
static uint32_t shift;          // the Evo's sector - the card's
static uint32_t vol_end;        // the card's sector after the volume
static uint32_t fat_end;        // ... after the FATs
static uint32_t fsi_lba;        // ... of the FSInfo
static uint32_t bk_lba;         // ... of the backup boot sector (0: none)
static uint32_t root_lba;       // ... holding the root's "." and ".."
static uint32_t card_sectors;   // what the Evo is told

//------------------------------------------------------------------------
// little-endian access
//------------------------------------------------------------------------
static uint32_t r16(const uint8_t *p) { return p[0] | (p[1] << 8); }
static uint32_t r32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
static void w16(uint8_t *p, uint32_t v) { p[0] = v; p[1] = v >> 8; }
static void w32(uint8_t *p, uint32_t v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }

static uint32_t ent_clus(const uint8_t *e) { return (r16(e + 20) << 16) | r16(e + 26); }
static void set_clus(uint8_t *e, uint32_t c) { w16(e + 20, c >> 16); w16(e + 26, c); }

static uint32_t clus_lba(uint32_t c) { return fs->database + (c - 2) * fs->csize; }

// a directory's first sector: "." and ".." in its first two entries
static int dot_entries(const uint8_t *b) {
  return !memcmp(b, ".          ", 11) && (b[11] & AM_DIR) &&
         !memcmp(b + 32, "..         ", 11) && (b[43] & AM_DIR);
}

//------------------------------------------------------------------------
// The card, and FAT sectors cached while nobody else has used it
//------------------------------------------------------------------------
#define FATC 4
static uint32_t fc_lba[FATC];
static uint8_t  fc_ok[FATC];
static uint8_t  fc_buf[FATC][512];
static int      fc_next;
static unsigned long seen_locks;

static void lock(void) {
  sdc_lock();
  if(sdc_locks != seen_locks + 1) memset(fc_ok, 0, sizeof(fc_ok));
  seen_locks = sdc_locks;
}

static void unlock(void) { sdc_unlock(); }

static int card_read(uint32_t lba, uint8_t *b) {
  if(disk_read(fs->pdrv, b, lba, 1) == RES_OK) return 0;
  vsd_info.errors++;
  return -1;
}

static int card_write(uint32_t lba, const uint8_t *b) {
  if(disk_write(fs->pdrv, b, lba, 1) != RES_OK) { vsd_info.errors++; return -1; }
  for(int i = 0; i < FATC; i++) if(fc_ok[i] && fc_lba[i] == lba) memcpy(fc_buf[i], b, 512);
  // FatFs's window must not keep the sector as it was (a dirty one is
  // FatFs's own unfinished business, and every firmware operation
  // finishes before it lets the card's lock go)
  if(fs->winsect == lba && !(fs->wflag & 1)) fs->winsect = (LBA_t)0 - 1;
  return 0;
}

// a FAT sector, from the cache; NULL if the card would not read
static const uint8_t *fat_sec(uint32_t lba) {
  for(int i = 0; i < FATC; i++) if(fc_ok[i] && fc_lba[i] == lba) return fc_buf[i];
  int i = fc_next; fc_next = (fc_next + 1) % FATC;
  fc_ok[i] = 0;
  if(card_read(lba, fc_buf[i])) return NULL;
  fc_lba[i] = lba; fc_ok[i] = 1;
  return fc_buf[i];
}

// a cluster's entry in FAT 1; one that cannot be read counts as in use
static uint32_t fat_get(uint32_t c) {
  const uint8_t *b = fat_sec(fs->fatbase + c / 128);
  return b ? r32(b + (c % 128) * 4) & 0x0FFFFFFF : 0x0FFFFFFF;
}

//------------------------------------------------------------------------
// The inside clusters: sorted, disjoint runs
//------------------------------------------------------------------------
typedef struct { uint32_t start, len; } ext_t;
static ext_t ext[VSD_EXTENTS];
static int   next;
static int   ext_full;          // a cluster could not be noted: said once

// the last run starting at or before c, or -1
static int ext_find(uint32_t c) {
  int lo = 0, hi = next - 1, at = -1;
  while(lo <= hi) {
    int mid = (lo + hi) / 2;
    if(ext[mid].start <= c) { at = mid; lo = mid + 1; } else hi = mid - 1;
  }
  return at;
}

static int inside(uint32_t c) {
  int i = ext_find(c);
  return i >= 0 && c < ext[i].start + ext[i].len;
}

static void full(void) {
  if(!ext_full) printf("VSD: more than %d runs of clusters in /evo/SD: the rest is not writable\r\n", VSD_EXTENTS);
  ext_full = 1;
}

static void ext_add(uint32_t c) {
  int i = ext_find(c);
  if(i >= 0 && c < ext[i].start + ext[i].len) return;
  int join_prev = i >= 0 && ext[i].start + ext[i].len == c;
  int join_next = i + 1 < next && ext[i + 1].start == c + 1;
  if(join_prev && join_next) {
    ext[i].len += 1 + ext[i + 1].len;
    memmove(&ext[i + 1], &ext[i + 2], (next - i - 2) * sizeof(ext_t));
    next--;
  } else if(join_prev) ext[i].len++;
  else if(join_next) { ext[i + 1].start--; ext[i + 1].len++; }
  else {
    if(next >= VSD_EXTENTS) { full(); return; }
    memmove(&ext[i + 2], &ext[i + 1], (next - i - 1) * sizeof(ext_t));
    ext[i + 1].start = c; ext[i + 1].len = 1;
    next++;
  }
}

static void ext_remove(uint32_t c) {
  int i = ext_find(c);
  if(i < 0 || c >= ext[i].start + ext[i].len) return;
  uint32_t end = ext[i].start + ext[i].len;
  if(c == ext[i].start) {
    ext[i].start++;
    if(--ext[i].len == 0) { memmove(&ext[i], &ext[i + 1], (next - i - 1) * sizeof(ext_t)); next--; }
  } else if(c == end - 1) ext[i].len--;
  else {
    // a run split in two; with no room the cluster stays noted, which
    // only lets the Evo write a cluster it has just freed
    if(next >= VSD_EXTENTS) { full(); return; }
    memmove(&ext[i + 2], &ext[i + 1], (next - i - 1) * sizeof(ext_t));
    ext[i].len = c - ext[i].start;
    ext[i + 1].start = c + 1; ext[i + 1].len = end - c - 1;
    next++;
  }
}

//------------------------------------------------------------------------
// The walk at start: every cluster of every file and directory under
// /evo/SD, breadth first (no recursion: the OSD task's stack is small)
//------------------------------------------------------------------------
static uint32_t dirq[VSD_DIRS];
static int      ndirq;

// note a chain; returns 0 if its first cluster was noted already (a
// directory seen twice, or two entries sharing a chain), else 1
static int note_chain(uint32_t c, int *frag) {
  if(c < 2 || c >= fs->n_fatent || inside(c)) return 0;
  uint32_t prev = 0, guard = fs->n_fatent;
  while(c >= 2 && c < fs->n_fatent && !inside(c) && guard--) {
    if(prev && c != prev + 1 && frag) *frag = 1;
    ext_add(c);
    prev = c;
    c = fat_get(c);
  }
  return 1;
}

static void walk_dir(uint32_t dc) {
  static uint8_t b[512];
  uint32_t guard = fs->n_fatent;
  for(uint32_t c = dc; c >= 2 && c < fs->n_fatent && guard--; c = fat_get(c))
    for(uint32_t k = 0; k < fs->csize; k++) {
      if(card_read(clus_lba(c) + k, b)) return;
      for(int i = 0; i < 512; i += 32) {
        const uint8_t *e = b + i;
        if(e[0] == 0) return;                           // the end of the directory
        if(e[0] == 0xE5 || e[0] == '.' || e[11] == 0x0F || (e[11] & 0x08)) continue;
        uint32_t ec = ent_clus(e);
        if(e[11] & AM_DIR) {
          vsd_info.dirs++;
          if(note_chain(ec, NULL)) {
            if(ndirq < VSD_DIRS) dirq[ndirq++] = ec;
            else printf("VSD: more than %d directories: the rest is not writable\r\n", VSD_DIRS);
          }
        } else {
          int frag = 0;
          vsd_info.files++;
          note_chain(ec, &frag);
          vsd_info.fragmented += frag;
        }
      }
    }
}

int vsd_scan(const char *root) {
  memset(&vsd_info, 0, sizeof(vsd_info));
  fs = NULL; next = 0; ext_full = 0; ndirq = 0;

  DIR dp;
  lock();
  FRESULT r = f_opendir(&dp, root);
  if(r == FR_NO_PATH || r == FR_NO_FILE) {
    // an empty card is still a card the Evo can fill
    if(f_mkdir(root) == FR_OK) printf("VSD: %s made\r\n", root);
    r = f_opendir(&dp, root);
  }
  if(r != FR_OK) { unlock(); printf("VSD: no %s (%d): no card for the Evo\r\n", root, r); return -1; }
  FATFS *f = dp.obj.fs;
  sdroot = dp.obj.sclust;
  f_closedir(&dp);
  if(f->fs_type != FS_FAT32) {
    unlock();
    printf("VSD: the card is not FAT32 (type %d): no card for the Evo\r\n", f->fs_type);
    return -1;
  }
  fs = f;
  memset(fc_ok, 0, sizeof(fc_ok));

  static uint8_t b[512];
  if(card_read(fs->volbase, b)) { fs = NULL; unlock(); return -1; }
  vol_end = fs->volbase + r32(b + 32);
  fsi_lba = fs->volbase + r16(b + 48);
  bk_lba  = r16(b + 50) ? fs->volbase + r16(b + 50) : 0;
  fat_end = fs->fatbase + fs->n_fats * fs->fsize;
  root_lba = clus_lba(sdroot);
  shift = fs->volbase ? 0 : VSD_PART_START;
  card_sectors = (shift + vol_end + 1023) & ~1023UL;   // the CSD counts in 512 KB

  note_chain(sdroot, NULL);
  dirq[ndirq++] = sdroot;
  for(int i = 0; i < ndirq; i++) walk_dir(dirq[i]);
  unlock();

  vsd_info.extents = next;
  vsd_info.clusters = fs->n_fatent - 2;
  vsd_info.spc = fs->csize;
  vsd_info.root = sdroot;
  printf("VSD: %s: %d files (%d fragmented), %d directories, %d runs of clusters; "
         "the card's FAT32: %lu clusters of %lu KB, %lu MB\r\n",
         root, vsd_info.files, vsd_info.fragmented, vsd_info.dirs, next,
         (unsigned long)vsd_info.clusters, (unsigned long)(fs->csize / 2),
         (unsigned long)(card_sectors / 2048));
  return vsd_info.files + vsd_info.dirs;
}

uint32_t vsd_sectors(void) { return fs ? card_sectors : 0; }

//------------------------------------------------------------------------
// What the Evo reads
//------------------------------------------------------------------------
static void mbr(uint8_t *b) {
  uint8_t *p = b + 446;
  p[0] = 0x00;                         // not bootable
  p[1] = 0xFE; p[2] = 0xFF; p[3] = 0xFF; // CHS: beyond, as for any large partition
  p[4] = 0x0C;                         // FAT32, LBA
  p[5] = 0xFE; p[6] = 0xFF; p[7] = 0xFF;
  w32(p + 8, fs->volbase + shift);
  w32(p + 12, vol_end - fs->volbase);
  b[510] = 0x55; b[511] = 0xAA;
}

// the root a FAT32 root: no "." and "..", shown as two volume labels so
// that no driver takes the entries for free ones
static void root_hide(uint8_t *b) {
  if(!dot_entries(b)) return;
  for(int i = 0; i < 64; i += 32) {
    memset(b + i, 0, 32);
    memcpy(b + i, "EVO NANO   ", 11);
    b[i + 11] = 0x08;
  }
}

int vsd_read(uint32_t v, uint8_t *b) {
  vsd_info.reads++;
  memset(b, 0, 512);
  if(!fs) return -1;
  if(v == 0) { mbr(b); return 0; }
  if(v < shift) return 0;
  uint32_t r = v - shift;
  if(r < fs->volbase || r >= vol_end) return 0;
  lock();
  int e = card_read(r, b);
  unlock();
  if(e) return -1;
  if(r == fs->volbase || r == bk_lba) {
    w32(b + 28, fs->volbase + shift);  // the hidden sectors: where the MBR puts it
    w32(b + 44, sdroot);               // the root directory
    memcpy(b + 71, "EVO NANO   ", 11);
  } else if(r == root_lba) root_hide(b);
  else if(r >= fs->database && dot_entries(b) && ent_clus(b + 32) == sdroot)
    set_clus(b + 32, 0);               // ".." of a directory in the root
  return 0;
}

//------------------------------------------------------------------------
// What the Evo writes
//------------------------------------------------------------------------
static int refuse(uint32_t v, const char *what) {
  vsd_info.refused++;
  if(vsd_info.refused <= 16) printf("VSD: write of sector %lu refused: %s\r\n", (unsigned long)v, what);
  return -1;
}

// a FAT sector, entry by entry: an entry may change if its cluster is
// free or inside (FAT 1 keeps the inside set), or if it becomes what FAT
// 1 says (a FAT 2 following FAT 1); /evo/SD's own first cluster is never
// freed; the top four bits stay the card's
static int fat_write(uint32_t v, uint32_t r, const uint8_t *b) {
  static uint8_t old[512], one[512];
  uint32_t k = (r - fs->fatbase) / fs->fsize;
  uint32_t s = (r - fs->fatbase) % fs->fsize;
  uint32_t c0 = s * 128;
  lock();
  const uint8_t *p = fat_sec(r);
  if(p) memcpy(old, p, 512);
  if(p && k > 0) { p = fat_sec(fs->fatbase + s); if(p) memcpy(one, p, 512); }
  if(!p) { unlock(); return -1; }
  int changed = 0, kept = 0;
  for(int i = 0; i < 128; i++) {
    uint32_t c = c0 + i;
    uint32_t o = r32(old + i * 4) & 0x0FFFFFFF, n = r32(b + i * 4) & 0x0FFFFFFF;
    if(o == n || c < 2 || c >= fs->n_fatent) continue;
    int ok = (o == 0 || inside(c)) && !(c == sdroot && n == 0);
    if(k > 0 && n == (r32(one + i * 4) & 0x0FFFFFFF)) ok = 1;
    if(!ok) { kept++; continue; }
    w32(old + i * 4, (r32(old + i * 4) & 0xF0000000) | n);
    changed = 1;
    if(k == 0) { if(n == 0) ext_remove(c); else if(o == 0) ext_add(c); }
  }
  int e = changed ? card_write(r, old) : 0;
  if(changed && k == 0) fs->free_clst = 0xFFFFFFFF;    // FatFs's count: unknown now
  unlock();
  if(kept) {
    vsd_info.fat_kept += kept;
    printf("VSD: FAT sector %lu: %d entries outside /evo/SD kept\r\n", (unsigned long)v, kept);
  }
  return e;
}

int vsd_write(uint32_t v, const uint8_t *b) {
  static uint8_t w[512], cur[512];
  vsd_info.writes++;
  if(!fs) return -1;
  if(v < shift) return refuse(v, "the MBR");
  uint32_t r = v - shift;
  if(r < fs->volbase) return refuse(v, "the MBR");
  if(r >= vol_end) return refuse(v, "past the volume");
  if(r < fs->fatbase) {
    if(r != fsi_lba && !(bk_lba && r == bk_lba + fsi_lba - fs->volbase)) return refuse(v, "the boot sectors");
    lock();
    int e = card_write(r, b);
    unlock();
    return e;
  }
  if(r < fat_end) return fat_write(v, r, b);

  uint32_t c = (r - fs->database) / fs->csize + 2;
  if(c >= fs->n_fatent) return refuse(v, "past the clusters");
  lock();
  if(!inside(c) && fat_get(c) != 0) { unlock(); return refuse(v, "a cluster outside /evo/SD"); }
  memcpy(w, b, 512);
  int e = 0;
  if(r == root_lba) {
    // the root's "." and ".." stay the card's
    e = card_read(r, cur);
    if(!e && dot_entries(cur)) memcpy(w, cur, 64);
  } else if(dot_entries(w) && ent_clus(w + 32) == 0)
    set_clus(w + 32, sdroot);          // ".." of a directory in the root
  if(!e) e = card_write(r, w);
  unlock();
  return e;
}

//------------------------------------------------------------------------
// The core: sdcard.v behind SPI target 4 (its header has the commands;
// every answer is one byte behind)
//------------------------------------------------------------------------
#define VSD_TARGET 4

static void vsd_cmd(spi_t *spi, uint8_t cmd) { spi_begin(spi); spi_tx_u08(spi, VSD_TARGET); spi_tx_u08(spi, cmd); }

void vsd_boot(spi_t *spi, const char *root) {
  int n = vsd_scan(root);
  uint32_t sectors = vsd_sectors();
  vsd_cmd(spi, 0x04);
  spi_tx_u08(spi, sectors >> 24); spi_tx_u08(spi, sectors >> 16);
  spi_tx_u08(spi, sectors >> 8);  spi_tx_u08(spi, sectors);
  spi_end(spi);
  vsd_cmd(spi, 0x05);
  spi_tx_u08(spi, n >= 0 ? 1 : 0);          // an empty folder is a card too
  spi_end(spi);
}

void vsd_handle_event(spi_t *spi) {
  static uint8_t sec[512];
  vsd_cmd(spi, 0x00);
  uint8_t st = spi_tx_u08(spi, 0);
  uint32_t lba = (uint32_t)spi_tx_u08(spi, 0) << 24;
  lba |= (uint32_t)spi_tx_u08(spi, 0) << 16;
  lba |= (uint32_t)spi_tx_u08(spi, 0) << 8;
  lba |= spi_tx_u08(spi, 0);
  spi_end(spi);
  if(!(st & 0x80)) return;                  // nothing pending (any more)

  int r;
  if(!(st & 0x40)) {
    r = vsd_read(lba, sec);
    vsd_cmd(spi, 0x01);
    for(int i = 0; i < 512; i++) spi_tx_u08(spi, sec[i]);
    spi_end(spi);
  } else {
    vsd_cmd(spi, 0x02);
    spi_tx_u08(spi, 0);                     // the answer to the command byte
    for(int i = 0; i < 512; i++) sec[i] = spi_tx_u08(spi, 0);
    spi_end(spi);
    r = vsd_write(lba, sec);
  }
  vsd_cmd(spi, 0x03);
  spi_tx_u08(spi, r ? 1 : 0);
  spi_end(spi);
}
