//
// extrom.c - the Korvet-EXTROM controller's brain, on the BL616.
//
// See extrom.h.  What the machine sends arrives here a byte at a time
// (extrom_feed); what goes back is queued to the core's tx FIFO
// (xr_send), which hands it to the ВВ55 as the machine reads.  The
// state machine is Erokhin's ext_rom.c for the Etalon emulator, with
// the file system behind a small layer so that `make extrom-test` can
// run it on the host against a directory.
//
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "extrom.h"

//------------------------------------------------------------------------
// The file layer: FatFs on the card, stdio on the host
//------------------------------------------------------------------------
#ifndef EXTROM_HOST_TEST
#include <ff.h>
#include "sdc.h"
#include "sysctrl.h"
typedef FIL xfile;
static const char *xroot = EXTROM_ROOT;
static int xopen(xfile *f, const char *path, int write) {
  return f_open(f, path, write ? (FA_READ | FA_WRITE) : FA_READ) == FR_OK;
}
static int xcreate(xfile *f, const char *path) { return f_open(f, path, FA_CREATE_ALWAYS | FA_WRITE) == FR_OK; }
static void xclose(xfile *f) { f_close(f); }
static int xseek(xfile *f, unsigned long pos) { return f_lseek(f, pos) == FR_OK; }
static int xread(xfile *f, void *buf, int n) { UINT r = 0; f_read(f, buf, n, &r); return r; }
static int xwrite(xfile *f, const void *buf, int n) { UINT w = 0; f_write(f, buf, n, &w); return w; }
static unsigned long xsize(xfile *f) { return f_size(f); }
static int xisdir(const char *path) { FILINFO fi; return f_stat(path, &fi) == FR_OK && (fi.fattrib & AM_DIR); }
// the entries of a directory, one at a time: 0 at the end
static DIR xdir;
static int xdir_open(const char *path) { return f_opendir(&xdir, path) == FR_OK; }
static int xdir_next(char *name, int *isdir) {
  FILINFO fi;
  for(;;) {
    if(f_readdir(&xdir, &fi) != FR_OK || !fi.fname[0]) return 0;
    if(fi.fname[0] == '.') continue;
    strncpy(name, fi.fname, 13); name[13] = 0;
    *isdir = (fi.fattrib & AM_DIR) ? 1 : 0;
    return 1;
  }
}
static void xdir_close(void) { f_closedir(&xdir); }
#else
#include <dirent.h>
#include <sys/stat.h>
typedef FILE *xfile;
static const char *xroot = ".";
void extrom_set_root(const char *r) { xroot = r; }
static int xopen(xfile *f, const char *path, int write) { *f = fopen(path, write ? "r+b" : "rb"); return *f != NULL; }
static int xcreate(xfile *f, const char *path) { *f = fopen(path, "w+b"); return *f != NULL; }
static void xclose(xfile *f) { fclose(*f); }
static int xseek(xfile *f, unsigned long pos) { return fseek(*f, pos, SEEK_SET) == 0; }
static int xread(xfile *f, void *buf, int n) { return fread(buf, 1, n, *f); }
static int xwrite(xfile *f, const void *buf, int n) { return fwrite(buf, 1, n, *f); }
static unsigned long xsize(xfile *f) { long p = ftell(*f); fseek(*f, 0, SEEK_END); long s = ftell(*f); fseek(*f, p, SEEK_SET); return s; }
static int xisdir(const char *path) { struct stat st; return stat(path, &st) == 0 && S_ISDIR(st.st_mode); }
static DIR *xdir;
static int xdir_open(const char *path) { xdir = opendir(path); return xdir != NULL; }
static int xdir_next(char *name, int *isdir) {
  struct dirent *d;
  while((d = readdir(xdir))) {
    if(d->d_name[0] == '.') continue;
    strncpy(name, d->d_name, 13); name[13] = 0;
    *isdir = (d->d_type == DT_DIR);
    return 1;
  }
  return 0;
}
static void xdir_close(void) { closedir(xdir); }
#endif

static void xpath(char *out, int n, const char *folder, const char *file) {
  if(folder && folder[0]) snprintf(out, n, "%s/%s/%s", xroot, folder, file);
  else                    snprintf(out, n, "%s/%s", xroot, file);
}

//------------------------------------------------------------------------
// The output: to the core's tx FIFO, or to the test's buffer
//------------------------------------------------------------------------
#ifndef EXTROM_HOST_TEST
static spi_t *xspi;
static void xr_send(const unsigned char *buf, int n) {
  // the FIFO is 1 K; wait for room in chunks, as the machine drains it
  while(n > 0) {
    unsigned char count, room, flags;
    sys_extrom_status(xspi, &count, &room, &flags);
    if(room == 0) { vTaskDelay(pdMS_TO_TICKS(1)); continue; }
    int k = n < room ? n : room;
    if(k > 64) k = 64;
    sys_extrom_write(xspi, buf, k);
    buf += k; n -= k;
  }
}
#else
unsigned char extrom_out[70000];
int extrom_out_n;
static void xr_send(const unsigned char *buf, int n) {
  while(n-- > 0) if(extrom_out_n < (int)sizeof(extrom_out)) extrom_out[extrom_out_n++] = *buf++;
}
#endif
static void xr_byte(unsigned char b) { xr_send(&b, 1); }

//------------------------------------------------------------------------
// The controller's state
//------------------------------------------------------------------------
#define S_STAGE1     1
#define S_CMD        2
#define S_WRITE128   3
#define S_SPEEDTEST  4
#define S_GETNAME    5
#define S_CREATE     6
#define S_GETFOLDER  7

#define API_FAIL 0
#define API_OK   1

static int state = S_STAGE1;
static unsigned char in_buf[128];
static int in_n;

static char drive_file[5][14];       // the image a drive is mounted from
static char drive_folder[5][14];     // and its folder
static char drive_status[5];         // 1 mounted
static char drive_ro[5];             // 1 read-only
static char diskfolder[14];          // where new mounts are looked for
static char substitute = 1;          // the system tracks come from a file
static int  subst_number = 0;        // 0 SYSTEM.BIN, 1 MICRODOS.BIN, 2 SYSTEMn.BIN
static char subst_name[16] = "SYSTEMn.BIN";
static const char *sysfiles[3] = { "SYSTEM.BIN", "MICRODOS.BIN", subst_name };
static char control_sense = 1;

static unsigned char e_cmd, e_drv, e_trk, e_sec;
static unsigned char sector[128];

int extrom_state(void) { return state; }
const char *extrom_drive_file(int d) { return drive_file[d]; }
int extrom_drive_mounted(int d) { return drive_status[d]; }

// a 14-byte field: the name, zero padded
static void put_name(const char *s) {
  unsigned char f[14];
  memset(f, 0, 14);
  strncpy((char *)f, s, 13);
  xr_send(f, 14);
}

//------------------------------------------------------------------------
// MOUNT.CFG: four (folder, file) pairs of 14 bytes, then the folder
//------------------------------------------------------------------------
static void mount_cfg_read(void) {
  char path[64];
  xfile f;
  xpath(path, sizeof(path), NULL, "MOUNT.CFG");
  if(xopen(&f, path, 0)) {
    for(int d=0;d<4;d++) { xread(&f, drive_folder[d], 14); xread(&f, drive_file[d], 14); }
    xread(&f, diskfolder, 14);
    xclose(&f);
  } else {
    for(int d=0;d<4;d++) {
      strcpy(drive_folder[d], "DISK");
      sprintf(drive_file[d], "DISK%c.KDI", 'A' + d);
    }
    strcpy(diskfolder, "DISK");
    if(xcreate(&f, path)) {
      for(int d=0;d<4;d++) { xwrite(&f, drive_folder[d], 14); xwrite(&f, drive_file[d], 14); }
      xwrite(&f, diskfolder, 14);
      xclose(&f);
    }
  }
  for(int d=0;d<4;d++) { drive_folder[d][13] = 0; drive_file[d][13] = 0; }
  diskfolder[13] = 0;
  // drive E: the tool disk, read-only
  strcpy(drive_file[4], "EXRTOOLS.KDI");
  drive_folder[4][0] = 0;
  for(int d=0;d<5;d++) { drive_status[d] = 1; drive_ro[d] = (d == 4); }
}

static void mount_cfg_write_drive(int d) {
  char path[64];
  xfile f;
  xpath(path, sizeof(path), NULL, "MOUNT.CFG");
  if(xopen(&f, path, 1)) {
    xseek(&f, 28 * d);
    xwrite(&f, diskfolder, 14);
    xwrite(&f, drive_file[d], 14);
    xclose(&f);
  }
}

static void mount_cfg_write_folder(void) {
  char path[64];
  xfile f;
  xpath(path, sizeof(path), NULL, "MOUNT.CFG");
  if(xopen(&f, path, 1)) {
    xseek(&f, 112);
    xwrite(&f, diskfolder, 14);
    xclose(&f);
  }
}

//------------------------------------------------------------------------
// Sectors: 128 bytes at (track * sectors-a-track + sector), the count
// from the image's information sector (offset 16); the system tracks of
// drive A from the substitute file when that mode is on
//------------------------------------------------------------------------
static int open_sector_file(xfile *f, int d, int trk, int write) {
  char path[64];
  int subst_tracks = (subst_number == 1) ? 3 : 2;
  if(!write && substitute && d == 0 && trk < subst_tracks)
    xpath(path, sizeof(path), NULL, sysfiles[subst_number]);
  else
    xpath(path, sizeof(path), drive_folder[d], drive_file[d]);
  return xopen(f, path, write);
}

static void read_sector(void) {
  xfile f;
  unsigned char hdr[18];
  if(e_drv > 4 || !drive_status[e_drv]) { xr_byte(API_FAIL); return; }
  if(!open_sector_file(&f, e_drv, e_trk, 0)) { drive_status[e_drv] = 0; xr_byte(API_FAIL); return; }
  xseek(&f, 0); xread(&f, hdr, 18);
  unsigned lspt = hdr[16] | (hdr[17] << 8);
  if(lspt == 0) lspt = 40;
  memset(sector, 0, 128);
  xseek(&f, ((unsigned long)e_trk * lspt + e_sec) * 128);
  xread(&f, sector, 128);
  xclose(&f);
  xr_byte(API_OK);
  xr_send(sector, 128);
}

static void write_sector(void) {
  xfile f;
  unsigned char hdr[18];
  if(!open_sector_file(&f, e_drv, e_trk, 1)) { drive_status[e_drv] = 0; return; }
  xseek(&f, 0); xread(&f, hdr, 18);
  unsigned lspt = hdr[16] | (hdr[17] << 8);
  if(lspt == 0) lspt = 40;
  xseek(&f, ((unsigned long)e_trk * lspt + e_sec) * 128);
  xwrite(&f, in_buf, 128);
  xclose(&f);
}

//------------------------------------------------------------------------
// The mount commands
//------------------------------------------------------------------------
static void do_getname(void) {
  char path[64];
  xfile f;
  strncpy(drive_file[e_drv], (char *)in_buf, 13); drive_file[e_drv][13] = 0;
  strncpy(drive_folder[e_drv], diskfolder, 13); drive_folder[e_drv][13] = 0;
  drive_status[e_drv] = 1;
  drive_ro[e_drv] = e_trk ? 1 : 0;
  if(e_sec) mount_cfg_write_drive(e_drv);
  xpath(path, sizeof(path), diskfolder, drive_file[e_drv]);
  if(!xopen(&f, path, 0)) drive_status[e_drv] = 0;
  else xclose(&f);
}

static void do_setfolder(void) {
  char path[64];
  char name[14];
  strncpy(name, (char *)in_buf, 13); name[13] = 0;
  xpath(path, sizeof(path), NULL, name);
  if(!xisdir(path)) return;
  strcpy(diskfolder, name);
  if(e_sec) mount_cfg_write_folder();
}

// a new 800 K image: the information sector, the rest E5h (the
// emulator's infosector bytes)
static const unsigned char infosector[32] = {
  0x80, 0xc3, 0x00, 0xda, 0x0a, 0x00, 0x00, 0x01, 0x01, 0x01, 0x03, 0x01, 0x05, 0x00, 0x50, 0x00,
  0x28, 0x00, 0x04, 0x0f, 0x00, 0x8a, 0x01, 0x7f, 0x00, 0xc0, 0x00, 0x20, 0x00, 0x02, 0x00, 0x10 };

static void do_create(void) {
  char path[64];
  xfile f;
  unsigned char fb[256];
  strncpy(drive_file[e_drv], (char *)in_buf, 13); drive_file[e_drv][13] = 0;
  strncpy(drive_folder[e_drv], diskfolder, 13); drive_folder[e_drv][13] = 0;
  xpath(path, sizeof(path), diskfolder, drive_file[e_drv]);
  if(!xcreate(&f, path)) { drive_status[e_drv] = 0; return; }
  memcpy(fb, infosector, 32);
  memset(fb + 32, 0xe5, 256 - 32);
  xwrite(&f, fb, 256);
  memset(fb, 0xe5, 32);
  for(int i=0;i<0xc7f;i++) xwrite(&f, fb, 256);
  xclose(&f);
  drive_status[e_drv] = 1;
  drive_ro[e_drv] = 0;
}

static void send_list(int dirs) {
  char path[64];
  char name[14];
  int isdir;
  if(dirs) xpath(path, sizeof(path), NULL, "");
  else     xpath(path, sizeof(path), NULL, diskfolder);
  if(path[strlen(path)-1] == '/') path[strlen(path)-1] = 0;
  if(xdir_open(path)) {
    while(xdir_next(name, &isdir))
      if(isdir == dirs) put_name(name);
    xdir_close();
  }
  xr_byte(0);
}

//------------------------------------------------------------------------
// The commands (api_v2.pdf)
//------------------------------------------------------------------------
static void do_command(void) {
  switch(e_cmd) {
  case 0x00: xr_byte(API_OK); break;
  case 0x01: read_sector(); break;
  case 0x02:
    if(e_drv > 4 || !drive_status[e_drv] || drive_ro[e_drv]) xr_byte(API_FAIL);
    else { xr_byte(API_OK); state = S_WRITE128; }
    break;
  case 0x80:
    xr_byte(API_OK);
    xr_byte(e_drv > 4 ? 1 : drive_ro[e_drv]);
    put_name(e_drv > 4 ? "" : drive_folder[e_drv]);
    put_name(e_drv > 4 ? "" : drive_file[e_drv]);
    break;
  case 0x81: xr_byte(API_OK); if(e_drv < 4) state = S_GETNAME; break;
  case 0x82: xr_byte(e_drv > 4 ? API_FAIL : drive_status[e_drv]); break;
  case 0x83: xr_byte(API_OK); if(e_drv < 4) state = S_CREATE; break;
  case 0x84: xr_byte(API_OK); send_list(0); break;
  case 0x85: xr_byte(API_OK); put_name(diskfolder); break;
  case 0x86: xr_byte(API_OK); state = S_GETFOLDER; break;
  case 0x87: xr_byte(API_OK); send_list(1); break;
  case 0x88: xr_byte(API_OK); drive_ro[4] = 0; break;
  case 0xA0:
    if(e_drv == 0) {
      xr_byte(API_OK);
      if(e_trk == 0) substitute = 0;
      else {
        substitute = 1;
        subst_name[6] = '0' + e_trk;
        subst_number = e_trk - 1;
        if(subst_number > 2) subst_number = 2;
      }
    } else xr_byte(API_FAIL);
    break;
  case 0xA1: xr_byte(API_OK); control_sense = e_trk; break;
  case 0xF0: {
    static const unsigned char zero[64];
    xr_byte(API_OK);
    for(int i=0;i<0x8000;i+=64) xr_send(zero, 64);
    break;
  }
  case 0xF1: xr_byte(API_OK); state = S_SPEEDTEST; in_n = 0; break;
  default:   xr_byte(API_FAIL); break;
  }
}

//------------------------------------------------------------------------
// Phase 1: the loader asks for a file by number - 8 the phase-2 loader,
// 0..7 ROMn.BIN - and gets its load address, its length in blocks and
// the bytes (stage1.asm)
//------------------------------------------------------------------------
static void stage1(unsigned char n) {
  char path[64];
  char name[16];
  xfile f;
  unsigned char buf[64];
  if(n <= 7) sprintf(name, "ROM%d.BIN", n);
  else       strcpy(name, "STAGE2.ROM");
  xpath(path, sizeof(path), NULL, name);
  if(!xopen(&f, path, 0)) {
    // the loader file is missing: nothing to say; the machine shows BOOT:
    printf("extrom: no %s\r\n", path);
    return;
  }
  unsigned long size = xsize(&f);
  unsigned blocks = size / 256;
  unsigned char base;
  xseek(&f, 6); xread(&f, &base, 1);
  xr_byte(base);                // the load address, high byte
  xr_byte(blocks);              // 256-byte blocks
  xseek(&f, 0);
  for(unsigned i=0;i<blocks*256;i+=64) {
    int k = xread(&f, buf, 64);
    if(k <= 0) break;
    xr_send(buf, k);
  }
  xclose(&f);
  printf("extrom: sent %s, %u blocks to %02X00\r\n", name, blocks, base);
  state = S_CMD;
  in_n = 0;
}

//------------------------------------------------------------------------
// A byte from the machine
//------------------------------------------------------------------------
static void feed_byte(unsigned char b) {
  if(in_n < (int)sizeof(in_buf)) in_buf[in_n++] = b;
  switch(state) {
  case S_STAGE1:
    in_n = 0;
    stage1(b);
    break;
  case S_CMD:
    if(in_n == 5) {
      e_cmd = in_buf[0]; e_drv = in_buf[1]; e_trk = in_buf[2]; e_sec = in_buf[3];
      unsigned char crc = (unsigned char)(e_cmd + e_drv + e_trk + e_sec - 1);
      in_n = 0;
      if(crc == in_buf[4]) do_command();
      else xr_byte(API_FAIL);
    }
    break;
  case S_WRITE128:
    if(in_n == 128) { write_sector(); in_n = 0; state = S_CMD; }
    break;
  case S_GETNAME:
    if(in_n == 14) { do_getname(); in_n = 0; state = S_CMD; }
    break;
  case S_CREATE:
    if(in_n == 14) { do_create(); in_n = 0; state = S_CMD; }
    break;
  case S_GETFOLDER:
    if(in_n == 14) { do_setfolder(); in_n = 0; state = S_CMD; }
    break;
  case S_SPEEDTEST:
    in_n = 0;   // 8000h bytes, dropped: counted on the machine's side
    break;
  default:
    state = S_CMD; in_n = 0;
  }
}

static void controller_reset(void) {
  state = S_STAGE1;
  in_n = 0;
  substitute = 1; subst_number = 0;
  mount_cfg_read();
}

#ifdef EXTROM_HOST_TEST
void extrom_reset(void) { controller_reset(); }
void extrom_feed(const unsigned char *bytes, int n) { while(n-- > 0) feed_byte(*bytes++); }
#else

// the phase-1 loader, for a card without STAGE1.ROM (loader/stage1/
// stage1.asm in the sibling repository, assembled; GPL as the project is)
static const unsigned char stage1_builtin[256] = {
  0xc3,0x08,0xf5,0x00,0x08,0xf5,0xf5,0x01,0xf3,0x21,0xa4,0xf5,0x11,0x00,0xfc,0xcd,
  0xc0,0x00,0x21,0x0b,0xfb,0x36,0xc0,0x21,0x10,0xf8,0x7e,0x0e,0x08,0x0f,0xda,0x25,
  0xf5,0x0d,0xc2,0x1d,0xf5,0x3e,0x08,0x91,0xcd,0x76,0xf5,0xcd,0x67,0xf5,0x67,0x2e,
  0x00,0x4d,0xcd,0x67,0xf5,0x47,0xe5,0x7c,0xcd,0x95,0xf5,0x7d,0xcd,0x95,0xf5,0x3e,
  0x2d,0x12,0x13,0x78,0xcd,0x95,0xf5,0xcd,0x67,0xf5,0x77,0x23,0x0b,0x78,0xb1,0xca,
  0x5e,0xf5,0x79,0xb7,0xc2,0x47,0xf5,0x3e,0x2a,0x12,0x13,0xc3,0x47,0xf5,0xc1,0x0e,
  0x04,0x0a,0x6f,0x0c,0x0a,0x67,0xe9,0xe5,0x21,0x0a,0xfb,0x7e,0xe6,0x20,0xca,0x6b,
  0xf5,0x2d,0x2d,0x7e,0xe1,0xc9,0xe5,0xf5,0x21,0x0a,0xfb,0x7e,0xe6,0x80,0xca,0x7b,
  0xf5,0x2d,0x2d,0xf1,0x77,0xe1,0xc9,0xe6,0x0f,0xc6,0x30,0xfe,0x3a,0xda,0x92,0xf5,
  0xc6,0x07,0x12,0x13,0xc9,0xf5,0x0f,0x0f,0x0f,0x0f,0xcd,0x87,0xf5,0xf1,0xe6,0x0f,
  0xcd,0x87,0xf5,0xc9,0x42,0x4f,0x4f,0x54,0x3a,0x46,0x35,0x30,0x30,0x3a,0x00,0xff,
  0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
  0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
  0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
  0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
  0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xa9 };

void extrom_init(spi_t *spi) {
  unsigned char rom[256];
  char path[64];
  xfile f;
  xspi = spi;
  memcpy(rom, stage1_builtin, 256);
  sdc_lock();
  xpath(path, sizeof(path), NULL, "STAGE1.ROM");
  if(xopen(&f, path, 0)) {
    if(xread(&f, rom, 256) == 256) printf("extrom: phase-1 ROM from %s\r\n", path);
    xclose(&f);
  } else
    printf("extrom: built-in phase-1 ROM (%s not found)\r\n", path);
  controller_reset();
  sdc_unlock();
  sys_extrom_flush(spi);
  sys_extrom_load_rom(spi, rom);
}

void extrom_handle_event(void) {
  unsigned char count, room, flags;
  unsigned char buf[64];
  sys_extrom_status(xspi, &count, &room, &flags);
  sdc_lock();
  if(flags & 0x08) {
    // Control fell: the machine was reset - the controller restarts,
    // as the real one reboots (hardware.pdf 8.3), unless told not to
    printf("extrom: Control fell%s\r\n", control_sense ? "" : " (ignored)");
    sys_extrom_flush(xspi);
    if(control_sense) controller_reset();
    else { state = S_CMD; in_n = 0; }
  } else {
    while(count) {
      int k = count > 64 ? 64 : count;
      sys_extrom_read(xspi, buf, k);
      for(int i=0;i<k;i++) feed_byte(buf[i]);
      sys_extrom_status(xspi, &count, &room, &flags);
    }
  }
  sdc_unlock();
}
#endif
