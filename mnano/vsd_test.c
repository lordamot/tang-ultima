/*
  vsd_test.c - the Evo's SD card on the host (make vsd-test).

  vsd.c shows the Tang's own FAT32 card to the Evo with /evo/SD/ as its
  root, and lets the Evo write it.  Nothing in the FPGA simulation runs
  that code (the testbench serves an image file instead), so this does,
  on two cards built in memory with FatFs: 128 MB with a partition table
  and FatFs's own cluster size, and 4 GB without one ("superfloppy",
  which vsd.c moves to sector 2048) with clusters of 32 KB, as a card
  formatted for 32 GB has.  On each:

  1. Read: /evo/SD/ is filled (8.3 names in each case, long names, a
     subdirectory two deep, an empty file, a large file, a fragmented one,
     the Dune files from ../retro-game-zxevo-dune when they are there) and
     files are put outside it; vsd_scan() runs, and the card it answers is
     walked by a FAT32 reader written here independently of vsd.c: the
     MBR, the boot sector and its backup, the FSInfo, the root without "."
     and "..", every subdirectory's dot entries, every chain, every byte.
  2. Write: the card is mounted a second time, as FatFs volume "sd2"
     whose sectors are vsd_read()/vsd_write() - FatFs standing in for the
     Evo's own FAT driver - and directories are made, files written,
     grown, renamed, deleted, the root grown past its first cluster.
     Then the Tang's card itself: everything must be there through the
     firmware's FatFs at once, the files outside /evo/SD untouched, and
     the volume consistent (every chain as long as its file, nothing
     claimed twice, nothing allocated that nothing claims, FAT 2 = FAT 1).
  3. The firmware's own write while the Evo runs: a file made outside
     /evo/SD after the guard cached the FAT sector; the Evo's write into
     its cluster must be refused.
  4. The guard: the MBR, the boot sector, an outside file's data and a
     "format" (zeros over both FATs) written through the card; every file
     outside /evo/SD must survive.

  vsd_test --dump FILE also writes the first card as the Evo sees it
  (before the writes) out as an image, which the FPGA simulation serves
  (+SD=FILE): the Evo's own firmware then reads vsd.c's card (build.md).
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include "ff.h"
#include "diskio.h"
#include "sdc.h"
#include "vsd.h"

static int errors;
#define CHECK(cond, ...) do { if(!(cond)) { errors++; if(errors < 60) { printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } } while(0)

//------------------------------------------------------------------------
// The disks: volume 2 ("sd") the Tang's card in memory, volume 3 ("sd2")
// the Evo's card through vsd.c
//------------------------------------------------------------------------
static unsigned char *disk;
static uint32_t disk_sectors;

DSTATUS disk_initialize(BYTE p) { return (p == 2 || p == 3) ? 0 : STA_NOINIT; }
DSTATUS disk_status(BYTE p) { return (p == 2 || p == 3) ? 0 : STA_NOINIT; }
DRESULT disk_read(BYTE p, BYTE *b, LBA_t s, UINT c) {
  if(p == 3) { for(UINT i = 0; i < c; i++) if(vsd_read(s + i, b + i * 512)) return RES_ERROR; return RES_OK; }
  if(p != 2 || s + c > disk_sectors) return RES_PARERR;
  memcpy(b, disk + (size_t)s * 512, c * 512); return RES_OK;
}
DRESULT disk_write(BYTE p, const BYTE *b, LBA_t s, UINT c) {
  if(p == 3) { for(UINT i = 0; i < c; i++) if(vsd_write(s + i, b + i * 512)) return RES_ERROR; return RES_OK; }
  if(p != 2 || s + c > disk_sectors) return RES_PARERR;
  memcpy(disk + (size_t)s * 512, b, c * 512); return RES_OK;
}
DRESULT disk_ioctl(BYTE p, BYTE c, void *b) {
  if(p != 2 && p != 3) return RES_PARERR;
  switch(c) {
  case CTRL_SYNC: return RES_OK;
  case GET_SECTOR_COUNT: *(LBA_t *)b = p == 2 ? disk_sectors : vsd_sectors(); return RES_OK;
  case GET_SECTOR_SIZE: *(WORD *)b = 512; return RES_OK;
  case GET_BLOCK_SIZE: *(DWORD *)b = 1; return RES_OK;
  }
  return RES_PARERR;
}
DWORD get_fattime(void) { return ((2026 - 1980) << 25) | (10 << 21) | (2 << 16); }

// the firmware's lock: every FatFs use on the board takes it, and vsd.c
// drops its FAT cache when someone else did
unsigned long sdc_locks;
void sdc_lock(void) { sdc_locks++; }
void sdc_unlock(void) {}
void spi_begin(spi_t *spi) { (void)spi; }
unsigned char spi_tx_u08(spi_t *spi, unsigned char b) { (void)spi; (void)b; return 0; }
void spi_end(spi_t *spi) { (void)spi; }

static uint32_t r32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint16_t r16(const uint8_t *p) { return p[0] | (p[1] << 8); }

//------------------------------------------------------------------------
// Files: through FatFs, and what each should hold
//------------------------------------------------------------------------
static uint8_t *pattern(uint32_t size, uint32_t seed) {
  uint8_t *b = malloc(size ? size : 1);
  for(uint32_t i = 0; i < size; i++) b[i] = (i * 7 + (i >> 9) + seed * 13) & 0xFF;
  return b;
}

static int put(const char *path, const uint8_t *data, uint32_t size, BYTE mode) {
  FIL f;
  FRESULT r = f_open(&f, path, mode | FA_WRITE);
  CHECK(r == FR_OK, "f_open(%s): %d", path, r);
  if(r != FR_OK) return -1;
  if(mode & FA_OPEN_APPEND) f_lseek(&f, f_size(&f));
  UINT w = 0;
  if(size) { r = f_write(&f, data, size, &w); CHECK(r == FR_OK && w == size, "f_write(%s): %d, %u of %u", path, r, w, size); }
  r = f_close(&f);
  CHECK(r == FR_OK, "f_close(%s): %d", path, r);
  return 0;
}

// the content a path should have; NULL data for a file that must not be there
typedef struct { char path[96]; uint8_t *data; uint32_t size; int found; } want_t;
static want_t want[512];
static int nwant;

static want_t *want_find(const char *path) {
  for(int i = 0; i < nwant; i++) if(!strcmp(want[i].path, path)) return &want[i];
  return NULL;
}

static void want_set(const char *path, const uint8_t *data, uint32_t size) {
  want_t *w = want_find(path);
  if(!w) { w = &want[nwant++]; snprintf(w->path, sizeof(w->path), "%s", path); }
  else free(w->data);
  w->data = malloc(size ? size : 1);
  memcpy(w->data, data, size);
  w->size = size;
}

static void want_gone(const char *path) {
  want_t *w = want_find(path);
  if(w) { free(w->data); *w = want[--nwant]; }
}

// a file on the Tang's card, the way the firmware writes it (its lock taken)
static void tang_put(const char *path, const uint8_t *data, uint32_t size, BYTE mode) {
  sdc_lock(); put(path, data, size, mode); sdc_unlock();
}

// a whole file through FatFs, compared
static void fat_check(const char *path, const uint8_t *data, uint32_t size) {
  FIL f;
  FRESULT r = f_open(&f, path, FA_READ);
  CHECK(r == FR_OK, "%s: not there (%d)", path, r);
  if(r != FR_OK) return;
  CHECK(f_size(&f) == size, "%s: %lu bytes, want %u", path, (unsigned long)f_size(&f), size);
  uint8_t *b = malloc(size + 1);
  UINT got = 0;
  f_read(&f, b, size, &got);
  f_close(&f);
  CHECK(got == size && !memcmp(b, data, size), "%s: content differs", path);
  free(b);
}

//------------------------------------------------------------------------
// A FAT32 reader over a sector function, independent of vsd.c and FatFs
//------------------------------------------------------------------------
typedef struct {
  const char *name;
  int (*rd)(uint32_t lba, uint8_t *b);
  uint32_t part, spc, rsvd, nfat, fatsz, rootc, data0, clusters;
} vol_t;

static int rd_evo(uint32_t lba, uint8_t *b) { return vsd_read(lba, b); }
static int rd_tang(uint32_t lba, uint8_t *b) {
  if(lba >= disk_sectors) return -1;
  memcpy(b, disk + (size_t)lba * 512, 512); return 0;
}

static void vrd(vol_t *v, uint32_t lba, uint8_t *b) {
  CHECK(v->rd(lba, b) == 0, "%s: sector %u does not read", v->name, lba);
}

static void vol_open(vol_t *v) {
  uint8_t b[512];
  vrd(v, 0, b);
  CHECK(b[510] == 0x55 && b[511] == 0xAA, "%s: sector 0's signature", v->name);
  if((b[0] == 0xEB || b[0] == 0xE9) && r16(b + 11) == 512) v->part = 0;   // no partition table
  else {
    CHECK(b[446 + 4] == 0x0C || b[446 + 4] == 0x0B, "%s: partition type %02x", v->name, b[446 + 4]);
    v->part = r32(b + 446 + 8);
  }
  vrd(v, v->part, b);
  CHECK(b[510] == 0x55 && b[511] == 0xAA, "%s: boot sector signature", v->name);
  CHECK(r16(b + 11) == 512, "%s: bytes a sector", v->name);
  v->spc = b[13]; v->rsvd = r16(b + 14); v->nfat = b[16];
  v->fatsz = r32(b + 36); v->rootc = r32(b + 44);
  CHECK(r16(b + 17) == 0 && r16(b + 22) == 0, "%s: FAT32 has no fixed root and FATSz16 0", v->name);
  CHECK(v->nfat == 2, "%s: two FATs", v->name);
  CHECK(!memcmp(b + 82, "FAT32   ", 8), "%s: file system type", v->name);
  v->data0 = v->part + v->rsvd + v->nfat * v->fatsz;
  v->clusters = (r32(b + 32) - v->rsvd - v->nfat * v->fatsz) / v->spc;
  CHECK(v->clusters >= 65525, "%s: %u clusters: a driver that counts would take it for FAT16", v->name, v->clusters);
  uint8_t bk[512];
  vrd(v, v->part + r16(b + 50), bk);
  CHECK(!memcmp(b, bk, 512), "%s: the backup boot sector differs", v->name);
  vrd(v, v->part + r16(b + 48), b);
  CHECK(r32(b) == 0x41615252 && r32(b + 484) == 0x61417272 && r32(b + 508) == 0xAA550000, "%s: FSInfo signatures", v->name);
}

static uint32_t vfat(vol_t *v, uint32_t c, int n) {
  uint8_t b[512];
  vrd(v, v->part + v->rsvd + n * v->fatsz + (c * 4) / 512, b);
  return r32(b + (c * 4) % 512) & 0x0FFFFFFF;
}

// a chain's bytes, up to max; *nclus its length
static uint32_t vchain(vol_t *v, uint32_t c, uint8_t *out, uint32_t max, uint32_t *nclus) {
  uint32_t n = 0, k = 0;
  while(c >= 2 && c < 0x0FFFFFF8) {
    if(++k > v->clusters) { CHECK(0, "%s: a chain that does not end", v->name); break; }
    for(uint32_t s = 0; s < v->spc && n < max; s++) {
      uint8_t b[512];
      vrd(v, v->data0 + (c - 2) * v->spc + s, b);
      uint32_t m = max - n < 512 ? max - n : 512;
      if(out) memcpy(out + n, b, m);
      n += m;
    }
    c = vfat(v, c, 0);
  }
  if(nclus) *nclus = k;
  return n;
}

// a directory's entries as (name, cluster, size, attributes); calls back
typedef void (*entry_fn)(vol_t *v, const char *path, const char *name, const uint8_t *e, int index, uint32_t dirc, uint32_t parent);

static void vwalk(vol_t *v, uint32_t dirc, uint32_t parent, const char *path, entry_fn fn) {
  uint32_t max = 4 << 20;
  uint8_t *d = malloc(max);
  uint32_t n = vchain(v, dirc, d, max, NULL);
  char lfn[256] = ""; int have_lfn = 0; uint8_t lsum = 0;
  for(uint32_t i = 0; i + 32 <= n; i += 32) {
    uint8_t *e = d + i;
    if(e[0] == 0) break;
    if(e[0] == 0xE5) { have_lfn = 0; continue; }
    if(e[11] == 0x0F) {
      int seq = e[0] & 0x1F;
      if(e[0] & 0x40) { memset(lfn, 0, sizeof(lfn)); have_lfn = 1; lsum = e[13]; }
      static const int pos[13] = { 1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30 };
      for(int k = 0; k < 13; k++) {
        uint16_t ch = r16(e + pos[k]);
        if(ch == 0 || ch == 0xFFFF) break;
        int at = (seq - 1) * 13 + k;
        if(at < 255) lfn[at] = (char)ch;
      }
      continue;
    }
    char name[256];
    if(have_lfn) {
      uint8_t sum = 0;
      for(int k = 0; k < 11; k++) sum = ((sum & 1) << 7) + (sum >> 1) + e[k];
      CHECK(sum == lsum, "%s: long name checksum for %s", v->name, lfn);
      strcpy(name, lfn);
    } else {
      int k = 0;
      for(int j = 0; j < 8 && e[j] != ' '; j++) name[k++] = (e[12] & 0x08) ? tolower(e[j]) : e[j];
      if(e[8] != ' ') { name[k++] = '.'; for(int j = 8; j < 11 && e[j] != ' '; j++) name[k++] = (e[12] & 0x10) ? tolower(e[j]) : e[j]; }
      name[k] = 0;
    }
    have_lfn = 0;
    fn(v, path, name, e, i / 32, dirc, parent);
    if((e[11] & 0x10) && !(e[11] & 0x08) && strcmp(name, ".") && strcmp(name, "..")) {
      char full[512];
      snprintf(full, sizeof(full), "%s/%s", path, name);
      vwalk(v, (r16(e + 20) << 16) | r16(e + 26), dirc, full, fn);
    }
  }
  free(d);
}

static uint32_t clus(const uint8_t *e) { return (r16(e + 20) << 16) | r16(e + 26); }

//------------------------------------------------------------------------
// The Evo's card against what should be on it
//------------------------------------------------------------------------
static int nfound, ndirs;

static void evo_entry(vol_t *v, const char *path, const char *name, const uint8_t *e, int idx, uint32_t dirc, uint32_t parent) {
  int root = dirc == v->rootc;
  if(e[11] & 0x08) {                                   // a volume label: the root's first two only
    CHECK(root && idx < 2, "%s: a volume label at %s entry %d", v->name, path, idx);
    return;
  }
  CHECK(!(root && idx < 2), "%s: the root's entry %d is %s, not hidden", v->name, idx, name);
  if(!strcmp(name, ".")) { CHECK(idx == 0 && clus(e) == dirc, "%s: %s/. is %u, not %u", v->name, path, clus(e), dirc); return; }
  if(!strcmp(name, "..")) {
    uint32_t want_c = parent == v->rootc ? 0 : parent;
    CHECK(idx == 1 && clus(e) == want_c, "%s: %s/.. is %u, not %u", v->name, path, clus(e), want_c);
    return;
  }
  char full[512];
  snprintf(full, sizeof(full), "%s/%s", path, name);
  if(e[11] & 0x10) { ndirs++; return; }
  nfound++;
  want_t *w = want_find(full);
  CHECK(w != NULL, "%s: %s on the card, not in the folder", v->name, full);
  if(!w) return;
  w->found = 1;
  uint32_t size = r32(e + 28);
  CHECK(size == w->size, "%s: %s: size %u, want %u", v->name, full, size, w->size);
  uint8_t *b = malloc(size + 512);
  uint32_t got = vchain(v, clus(e), b, size, NULL);
  CHECK(got == size, "%s: %s: the chain gives %u bytes of %u", v->name, full, got, size);
  CHECK(!memcmp(b, w->data, size), "%s: %s: content differs", v->name, full);
  free(b);
}

static void evo_walk(const char *when) {
  vol_t v = { "the Evo's card", rd_evo };
  vol_open(&v);
  CHECK(v.rootc == vsd_info.root, "the boot sector's root is %u, /evo/SD is %u", v.rootc, vsd_info.root);
  for(int i = 0; i < nwant; i++) want[i].found = 0;
  nfound = ndirs = 0;
  vwalk(&v, v.rootc, v.rootc, "", evo_entry);
  for(int i = 0; i < nwant; i++) CHECK(want[i].found, "%s: %s not on the card", when, want[i].path);
}

//------------------------------------------------------------------------
// The Tang's card: consistent
//------------------------------------------------------------------------
static uint8_t *claimed;
static int bad_chains;

static void tang_entry(vol_t *v, const char *path, const char *name, const uint8_t *e, int idx, uint32_t dirc, uint32_t parent) {
  // every directory but the card's root starts with "." and ".." (the
  // Evo saw /evo/SD's as volume labels and must not have written those)
  if(dirc != v->rootc && idx < 2)
    CHECK(!strcmp(name, idx ? ".." : "."), "the card: %s's entry %d is \"%s\", not a dot entry", path, idx, name);
  if(e[11] & 0x08) return;
  if(!strcmp(name, ".")) { CHECK(clus(e) == dirc, "the card: %s/. is %u, not %u", path, clus(e), dirc); return; }
  if(!strcmp(name, "..")) {
    uint32_t want_c = parent == v->rootc ? 0 : parent;
    CHECK(clus(e) == want_c, "the card: %s/.. is %u, not %u", path, clus(e), want_c);
    return;
  }
  uint32_t c = clus(e), size = r32(e + 28), k = 0;
  uint32_t cb = v->spc * 512;
  if(!(e[11] & 0x10) && size == 0) { CHECK(c == 0, "the card: %s/%s: empty with cluster %u", path, name, c); return; }
  while(c >= 2 && c < 0x0FFFFFF8 && k <= v->clusters) {
    if(claimed[c]) { if(bad_chains++ < 10) printf("FAIL: the card: %s/%s: cluster %u claimed twice\n", path, name, c); errors++; break; }
    claimed[c] = 1; k++;
    c = vfat(v, c, 0);
  }
  if(!(e[11] & 0x10))
    CHECK(k == (size + cb - 1) / cb, "the card: %s/%s: %u clusters for %u bytes", path, name, k, size);
}

static void tang_consistent(const char *when) {
  vol_t v = { "the Tang's card", rd_tang };
  vol_open(&v);
  claimed = calloc(v.clusters + 2, 1);
  bad_chains = 0;
  // the root's own chain
  for(uint32_t c = v.rootc, k = 0; c >= 2 && c < 0x0FFFFFF8 && k++ <= v.clusters; c = vfat(&v, c, 0)) claimed[c] = 1;
  vwalk(&v, v.rootc, v.rootc, "", tang_entry);
  uint32_t lost = 0, unlinked = 0;
  uint8_t f1[512], f2[512];
  for(uint32_t s = 0; s < v.fatsz; s++) {
    vrd(&v, v.part + v.rsvd + s, f1);
    vrd(&v, v.part + v.rsvd + v.fatsz + s, f2);
    CHECK(!memcmp(f1, f2, 512), "%s: FAT sector %u: FAT 2 differs from FAT 1", when, s);
    for(int i = 0; i < 128; i++) {
      uint32_t c = s * 128 + i;
      if(c < 2 || c >= v.clusters + 2) continue;
      uint32_t e = r32(f1 + i * 4) & 0x0FFFFFFF;
      if(e && !claimed[c]) lost++;
      if(!e && claimed[c]) unlinked++;
    }
  }
  CHECK(lost == 0, "%s: %u clusters allocated that nothing claims", when, lost);
  CHECK(unlinked == 0, "%s: %u clusters claimed but free in the FAT", when, unlinked);
  free(claimed);
}

//------------------------------------------------------------------------
// The files outside /evo/SD: they must never change
//------------------------------------------------------------------------
typedef struct { const char *path; uint8_t *data; uint32_t size; } keep_t;
static keep_t keep[8];
static int nkeep;

static void outside(const char *path, uint32_t size, uint32_t seed) {
  uint8_t *b = pattern(size, seed);
  tang_put(path, b, size, FA_CREATE_ALWAYS);
  keep[nkeep].path = path; keep[nkeep].data = b; keep[nkeep].size = size; nkeep++;
}

static void outside_intact(const char *when) {
  sdc_lock();
  for(int i = 0; i < nkeep; i++) fat_check(keep[i].path, keep[i].data, keep[i].size);
  sdc_unlock();
  (void)when;
}

//------------------------------------------------------------------------
// The card as an image for the FPGA simulation's +SD= (sparse)
//------------------------------------------------------------------------
static void dump(const char *out) {
  FILE *f = fopen(out, "wb");
  if(!f) { printf("cannot write %s\n", out); errors++; return; }
  uint32_t n = vsd_sectors(), written = 0;
  static const uint8_t zero[512];
  uint8_t b[512];
  for(uint32_t lba = 0; lba < n; lba++) {
    vsd_read(lba, b);
    if(memcmp(b, zero, 512)) { fseek(f, (long)lba * 512, SEEK_SET); fwrite(b, 1, 512, f); written++; }
  }
  fseek(f, (long)n * 512 - 1, SEEK_SET); fputc(0, f);
  fclose(f);
  printf("vsd-test: the card into %s (%u sectors, %u not zero)\n", out, n, written);
}

static void host_file(const char *host, const char *rel) {
  FILE *f = fopen(host, "rb");
  if(!f) { printf("(no %s: left out)\n", host); return; }
  fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
  uint8_t *b = malloc(n);
  fread(b, 1, n, f); fclose(f);
  char p[128]; snprintf(p, sizeof(p), "/sd/evo/SD%s", rel);
  tang_put(p, b, n, FA_CREATE_ALWAYS);
  want_set(rel, b, n);
  free(b);
}

static void tang_file(const char *rel, uint32_t size, uint32_t seed) {
  uint8_t *b = pattern(size, seed);
  char p[128]; snprintf(p, sizeof(p), "/sd/evo/SD%s", rel);
  tang_put(p, b, size, FA_CREATE_ALWAYS);
  want_set(rel, b, size);
  free(b);
}

// a file the Evo writes; mode FA_CREATE_ALWAYS or FA_OPEN_APPEND
static void evo_file(const char *rel, uint32_t size, uint32_t seed, BYTE mode) {
  uint8_t *b = pattern(size, seed);
  char p[128]; snprintf(p, sizeof(p), "/sd2%s", rel);
  put(p, b, size, mode);
  if(mode & FA_OPEN_APPEND) {
    want_t *w = want_find(rel);
    if(w) {
      uint8_t *all = malloc(w->size + size);
      memcpy(all, w->data, w->size); memcpy(all + w->size, b, size);
      want_set(rel, all, w->size + size);
      free(all);
    }
  } else want_set(rel, b, size);
  free(b);
}

//------------------------------------------------------------------------
// One card
//------------------------------------------------------------------------
static void run(const char *title, uint32_t sectors, BYTE fmt, DWORD au, const char *dump_to) {
  int e0 = errors;
  disk_sectors = sectors;
  disk = calloc((size_t)sectors, 512);
  nwant = 0; nkeep = 0;
  static FATFS tang, evo;
  static BYTE work[4096];
  MKFS_PARM opt = { fmt, 2, 0, 0, au };
  FRESULT r = f_mkfs("/sd", &opt, work, sizeof(work));
  CHECK(r == FR_OK, "f_mkfs: %d", r);
  r = f_mount(&tang, "/sd", 1);
  CHECK(r == FR_OK, "f_mount: %d", r);
  uint32_t cb = tang.csize * 512;

  // 1. the folder, and files outside it
  sdc_lock();
  f_mkdir("/sd/evo"); f_mkdir("/sd/bk");
  f_mkdir("/sd/evo/SD"); f_mkdir("/sd/evo/SD/games"); f_mkdir("/sd/evo/SD/games/Sub Dir");
  sdc_unlock();
  outside("/sd/evo/zxevo.rom", 65536, 1);
  outside("/sd/bk/bk0010.rom", 32768 + 5, 2);
  tang_file("/README.TXT", 10, 3);
  tang_file("/lower.trd", 655360, 4);
  tang_file("/A long file name for the card.bin", 12345, 5);
  tang_file("/MixedCase.Scl", 4096, 6);
  tang_file("/empty.dat", 0, 7);
  tang_file("/BIG.DAT", 3 * 1024 * 1024 + 123, 8);
  tang_file("/games/GAME.TRD", 655360, 9);
  tang_file("/games/Sub Dir/deep file.$C", 513, 10);
  // a fragmented file: three clusters, something outside after them, three more
  {
    uint8_t *b = pattern(6 * cb, 11);
    tang_put("/sd/evo/SD/frag.bin", b, 3 * cb, FA_CREATE_ALWAYS);
    outside("/sd/evo/cmos.bin", cb, 12);
    tang_put("/sd/evo/SD/frag.bin", b + 3 * cb, 3 * cb, FA_OPEN_APPEND);
    want_set("/frag.bin", b, 6 * cb);
    free(b);
  }
  host_file("../retro-game-zxevo-dune/build/dune.trd", "/dune.trd");
  host_file("../retro-game-zxevo-dune/build/DUNE.DAT", "/DUNE.DAT");

  int n = vsd_scan("/sd/evo/SD");
  CHECK(n > 0, "vsd_scan: %d", n);
  CHECK(vsd_info.fragmented >= 1, "frag.bin not seen as fragmented (%d)", vsd_info.fragmented);
  CHECK(vsd_sectors() % 1024 == 0, "the card is not a whole number of 512 KB");
  evo_walk("read");
  int read_files = nfound, read_dirs = ndirs;
  tang_consistent("before the writes");
  if(dump_to) dump(dump_to);

  // 2. the Evo writes, through its own mount of the card; the firmware's
  // FatFs has /evo/SD's first sector in its window, which the Evo changes
  sdc_lock();
  FILINFO fno;
  CHECK(f_stat("/sd/evo/SD/README.TXT", &fno) == FR_OK, "README.TXT is not there");
  sdc_unlock();
  r = f_mount(&evo, "/sd2", 1);
  CHECK(r == FR_OK, "the Evo's f_mount: %d", r);
  r = f_mkdir("/sd2/NEWDIR");             CHECK(r == FR_OK, "f_mkdir NEWDIR: %d", r);
  r = f_mkdir("/sd2/games/New Sub");      CHECK(r == FR_OK, "f_mkdir games/New Sub: %d", r);
  r = f_mkdir("/sd2/NEWDIR/inner");       CHECK(r == FR_OK, "f_mkdir NEWDIR/inner: %d", r);
  evo_file("/NEWDIR/a new file with a long name.bin", 3 * cb + 100, 20, FA_CREATE_ALWAYS);
  evo_file("/NEWDIR/inner/X.TXT", 77, 21, FA_CREATE_ALWAYS);
  evo_file("/SAVE.DAT", 1000, 22, FA_CREATE_ALWAYS);
  evo_file("/games/New Sub/deep.bin", 70000, 23, FA_CREATE_ALWAYS);
  evo_file("/BIG.DAT", 5000, 24, FA_OPEN_APPEND);
  evo_file("/frag.bin", cb + 1, 25, FA_OPEN_APPEND);
  evo_file("/empty.dat", 600, 26, FA_OPEN_APPEND);
  {
    // a sector rewritten in place, as the ERS's mounter does to a TRD
    FIL f; UINT w = 0;
    uint8_t *p = pattern(512, 27);
    r = f_open(&f, "/sd2/lower.trd", FA_WRITE | FA_READ);
    CHECK(r == FR_OK, "open lower.trd: %d", r);
    f_lseek(&f, 4096); f_write(&f, p, 512, &w); f_close(&f);
    want_t *t = want_find("/lower.trd");
    memcpy(t->data + 4096, p, 512);
    free(p);
  }
  r = f_rename("/sd2/MixedCase.Scl", "/sd2/games/Moved.scl");
  CHECK(r == FR_OK, "f_rename: %d", r);
  { want_t *t = want_find("/MixedCase.Scl"); want_set("/games/Moved.scl", t->data, t->size); want_gone("/MixedCase.Scl"); }
  r = f_unlink("/sd2/README.TXT");        CHECK(r == FR_OK, "f_unlink: %d", r);
  want_gone("/README.TXT");
  // the root grown past its first cluster: four entries a file
  int many = tang.csize * 16 / 4 + 8;
  for(int i = 0; i < many; i++) {
    char p[96]; snprintf(p, sizeof(p), "/File number %03d with a long name.txt", i);
    evo_file(p, 1 + i, 100 + i, FA_CREATE_ALWAYS);
  }
  r = f_unmount("/sd2");
  CHECK(r == FR_OK, "the Evo's f_unmount: %d", r);
  CHECK(vsd_info.refused == 0 && vsd_info.fat_kept == 0, "the Evo's own writes: %lu refused, %lu FAT entries kept",
        vsd_info.refused, vsd_info.fat_kept);

  // the firmware's FatFs: a sector in its window is the card's (a path
  // walk moves the window, so a stale one would show only in a file's
  // own updates - a FAT or directory sector written back as it was)
  CHECK(tang.winsect == (LBA_t)0 - 1 || !memcmp(tang.win, disk + (size_t)tang.winsect * 512, 512),
        "the firmware's FatFs window holds sector %lu as it was", (unsigned long)tang.winsect);

  // the Tang's card at once, through the firmware's FatFs
  sdc_lock();
  CHECK(f_stat("/sd/evo/SD/README.TXT", &fno) == FR_NO_FILE, "README.TXT is still there");
  for(int i = 0; i < nwant; i++) {
    char p[128]; snprintf(p, sizeof(p), "/sd/evo/SD%s", want[i].path);
    fat_check(p, want[i].data, want[i].size);
  }
  CHECK(f_stat("/sd/evo/SD/MixedCase.Scl", &fno) == FR_NO_FILE, "MixedCase.Scl is still there");
  CHECK(f_stat("/sd/evo/SD/NEWDIR/inner", &fno) == FR_OK && (fno.fattrib & AM_DIR), "NEWDIR/inner is not a directory");
  sdc_unlock();
  outside_intact("after the Evo's writes");
  tang_consistent("after the Evo's writes");

  // the next start sees the same
  n = vsd_scan("/sd/evo/SD");
  evo_walk("after a new scan");
  int write_files = nfound, write_dirs = ndirs;

  // 3. the firmware makes a file while the guard has the FAT cached
  {
    vol_t v = { "the Tang's card", rd_tang };
    vol_open(&v);
    // the cluster the firmware's FatFs will take next (the first free
    // one after its last); the guard reads its FAT sector, and a write to
    // it passes while it is free
    uint32_t c = 0;
    for(uint32_t k = tang.last_clst + 1; k < v.clusters + 2 && !c; k++) if(vfat(&v, k, 0) == 0) c = k;
    uint32_t shift = v.part ? 0 : VSD_PART_START;
    uint8_t *p = pattern(512, 30);
    CHECK(vsd_write(v.data0 + (c - 2) * v.spc + shift, p) == 0, "a write to the free cluster %u was refused", c);
    outside("/sd/evo/later.bin", cb, 31);
    CHECK(vfat(&v, c, 0) != 0, "later.bin did not take cluster %u", c);
    unsigned long before = vsd_info.refused;
    CHECK(vsd_write(v.data0 + (c - 2) * v.spc + shift, p) != 0, "a write into later.bin's cluster %u passed", c);
    CHECK(vsd_info.refused == before + 1, "the write into later.bin was not counted as refused");
    free(p);
    outside_intact("after the firmware's write");
  }

  // 4. the guard
  {
    vol_t v = { "the Tang's card", rd_tang };
    vol_open(&v);
    uint32_t shift = v.part ? 0 : VSD_PART_START;
    static uint8_t z[512];
    uint8_t mbr0[512], bpb0[512], b[512];
    rd_tang(0, mbr0); rd_tang(v.part, bpb0);
    CHECK(vsd_write(0, z) != 0, "the MBR's write passed");
    CHECK(vsd_write(v.part + shift, z) != 0, "the boot sector's write passed");
    CHECK(vsd_write(v.part + shift + 6, z) != 0, "the backup boot sector's write passed");
    { uint8_t fsi[512]; rd_tang(v.part + 7, fsi);
      CHECK(vsd_write(v.part + shift + 7, fsi) == 0, "the backup FSInfo's write was refused"); }
    rd_tang(0, b);      CHECK(!memcmp(b, mbr0, 512), "the card's sector 0 changed");
    rd_tang(v.part, b); CHECK(!memcmp(b, bpb0, 512), "the card's boot sector changed");
    // zxevo.rom's first cluster, from /evo's directory on the card
    sdc_lock();
    FIL f; f_open(&f, "/sd/evo/zxevo.rom", FA_READ);
    uint32_t rc = f.obj.sclust;
    f_close(&f);
    sdc_unlock();
    CHECK(vsd_write(v.data0 + (rc - 2) * v.spc + shift, z) != 0, "a write into zxevo.rom passed");
    // a "format": zeros over both FATs
    unsigned long kept = vsd_info.fat_kept;
    for(uint32_t s = 0; s < 2 * v.fatsz; s++)
      CHECK(vsd_write(v.part + shift + v.rsvd + s, z) == 0, "a FAT sector's write failed");
    CHECK(vsd_info.fat_kept > kept, "no FAT entry kept against the format");
    CHECK(vfat(&v, vsd_info.root, 0) != 0, "/evo/SD's own first cluster was freed");
    outside_intact("after the format");
  }

  f_unmount("/sd");
  free(disk);
  printf("vsd-test: %s: %d files and %d directories read, %d and %d after the writes; "
         "%u clusters of %u KB; %lu reads, %lu writes, %lu refused; %d errors\n",
         title, read_files, read_dirs, write_files, write_dirs,
         vsd_info.clusters, vsd_info.spc / 2, vsd_info.reads, vsd_info.writes, vsd_info.refused, errors - e0);
}

int main(int argc, char **argv) {
  const char *out = (argc > 2 && !strcmp(argv[1], "--dump")) ? argv[2] : NULL;
  run("128 MB, a partition", 128 * 2048, FM_FAT32, 0, out);
  run("4 GB, no partition, 32 KB clusters", 4u * 1024 * 2048, FM_FAT32 | FM_SFD, 32768, NULL);
  printf("vsd-test: %d errors\n", errors);
  return errors ? 1 : 0;
}
