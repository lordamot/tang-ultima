/*
  azbk.c - the AZ controller's STM32, played by the BL616.  See azbk.h.

  The command set is MAXIOL's ("Команды контролера AZ", "Новые команды
  контроллеров AZ*") and the model followed is GID's AZBK_ctrl.cpp, the
  same as azctrl.v's.  Which commands come here and which the FPGA does
  itself is azctrl.v's header.
*/
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ff.h>
#ifndef SDL
#include <FreeRTOS.h>
#include <task.h>
#endif

#include "azbk.h"
#include "sysctrl.h"
#include "sdc.h"

// ---- the units -------------------------------------------------------
typedef struct {
  char path[AZ_PATH_MAX];      // "" = nothing mounted
  unsigned long blocks;        // size in 512-byte blocks
  unsigned char ro;
} az_unit_t;

static az_unit_t units[AZ_UNITS];
static int cur_unit = -1;
static FIL cur_fil;
static int cur_open = 0;
static DWORD cur_clmt[64];

static spi_t *az_spi = NULL;
static unsigned short buf[256];  // one block's worth of words for the transfers

// "0:/rom/x" or "/rom/x" or "rom/x" -> "/sd/bk/rom/x"; a path that is
// already the card's ("/sd/...", the OSD's file selector, menu_bk_mount)
// is left as it is - prefixing it too made the first board's unit 0
// "/sd/bk/sd/dave.img" (22 Sep 2026)
static void az_path(char *out, int len, const char *in) {
  if(!strncasecmp(in, CARD_MOUNTPOINT "/", strlen(CARD_MOUNTPOINT) + 1)) {
    snprintf(out, len, "%s", in);
    return;
  }
  if(in[0] && in[1] == ':') in += 2;
  while(*in == '/' || *in == '\\') in++;
  snprintf(out, len, "%s/%s", AZ_ROOT, in);
  for(char *p = out; *p; p++) if(*p == '\\') *p = '/';
}

static unsigned long file_blocks(const char *path) {
  FILINFO fi;
  char full[AZ_PATH_MAX + 16];
  az_path(full, sizeof(full), path);
  if(f_stat(full, &fi) != FR_OK) return 0;
  return (fi.fsize + 511) / 512;
}

static void unit_close(void) {
  if(cur_open) { f_close(&cur_fil); cur_open = 0; }
  cur_unit = -1;
}

static int unit_open(int u) {
  char full[AZ_PATH_MAX + 16];
  if(u < 0 || u >= AZ_UNITS || !units[u].path[0]) return -1;
  if(cur_unit == u && cur_open) return 0;
  unit_close();
  az_path(full, sizeof(full), units[u].path);
  if(f_open(&cur_fil, full, FA_OPEN_EXISTING | FA_READ | FA_WRITE) != FR_OK) {
    units[u].ro = 1;
    if(f_open(&cur_fil, full, FA_OPEN_EXISTING | FA_READ) != FR_OK) return -1;
  }
  // a cluster link map, so that a seek is a lookup and not a chain walk
  cur_fil.cltbl = cur_clmt;
  cur_clmt[0] = sizeof(cur_clmt) / sizeof(cur_clmt[0]);
  if(f_lseek(&cur_fil, CREATE_LINKMAP) != FR_OK) cur_fil.cltbl = NULL;
  cur_open = 1;
  cur_unit = u;
  return 0;
}

static void az_bwrite(unsigned adr, const unsigned short *w, int n);

// every unit's size in blocks into the FPGA's buffer: the select (001)
// is the FPGA's own and answers from this table, so it must be there
// before the machine runs and follow every mount and unmount
static void az_push_sizes(void) {
  unsigned short w[2 * AZ_UNITS];
  if(!az_spi) return;
  for(int u = 0; u < AZ_UNITS; u++) { w[2*u] = units[u].blocks & 0xffff; w[2*u+1] = units[u].blocks >> 16; }
  az_bwrite(AZ_R_USIZE, w, 2 * AZ_UNITS);
}

int az_set_unit(int unit, const char *path) {
  if(unit < 0 || unit >= AZ_UNITS) return -1;
  if(cur_unit == unit) unit_close();
  if(path && path[0]) {
    strncpy(units[unit].path, path, AZ_PATH_MAX - 1);
    units[unit].path[AZ_PATH_MAX - 1] = 0;
    units[unit].blocks = file_blocks(path);
    units[unit].ro = 0;
  } else {
    units[unit].path[0] = 0;
    units[unit].blocks = 0;
  }
  az_push_sizes();
  return 0;
}

const char *az_unit_path(int unit) {
  if(unit < 0 || unit >= AZ_UNITS || !units[unit].path[0]) return NULL;
  return units[unit].path;
}

unsigned long az_unit_blocks(int unit) {
  if(unit < 0 || unit >= AZ_UNITS) return 0;
  return units[unit].blocks;
}

// ---- the software clock ----------------------------------------------
static long clock_base = 0;    // seconds since 2000-01-01 at tick 0
static unsigned long clock_tick0 = 0;

static unsigned long ticks_now(void) {
#ifndef SDL
  return xTaskGetTickCount() / configTICK_RATE_HZ;
#else
  return 0;
#endif
}

static int days_in_month(int y, int m) {
  static const int d[] = { 31,28,31,30,31,30,31,31,30,31,30,31 };
  if(m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) return 29;
  return d[m-1];
}

static long to_seconds(int y, int mo, int d, int h, int mi, int s) {
  long days = 0;
  for(int yy = 2000; yy < y; yy++) days += ((yy % 4 == 0 && yy % 100 != 0) || yy % 400 == 0) ? 366 : 365;
  for(int mm = 1; mm < mo; mm++) days += days_in_month(y, mm);
  days += d - 1;
  return ((days * 24 + h) * 60 + mi) * 60 + s;
}

static void from_seconds(long t, int *y, int *mo, int *d, int *wd, int *h, int *mi, int *s) {
  long days = t / 86400, rem = t % 86400;
  *h = rem / 3600; *mi = (rem / 60) % 60; *s = rem % 60;
  *wd = (days + 6) % 7;               // 2000-01-01 was a Saturday: 0 Sunday
  *y = 2000;
  for(;;) {
    long yd = ((*y % 4 == 0 && *y % 100 != 0) || *y % 400 == 0) ? 366 : 365;
    if(days < yd) break;
    days -= yd; (*y)++;
  }
  *mo = 1;
  while(days >= days_in_month(*y, *mo)) { days -= days_in_month(*y, *mo); (*mo)++; }
  *d = days + 1;
}

void az_set_time(int year, int month, int day, int hour, int min, int sec) {
  if(year < 100) year += 2000;
  clock_base = to_seconds(year, month, day, hour, min, sec);
  clock_tick0 = ticks_now();
}

// the fourteen words of the timestamp buffer
static void az_timestamp(unsigned short *w) {
  int y, mo, d, wd, h, mi, s;
  from_seconds(clock_base + (long)(ticks_now() - clock_tick0), &y, &mo, &d, &wd, &h, &mi, &s);
  int ry = y - 1972;
  unsigned long t50 = (h * 3600UL + mi * 60UL + s) * 50, t60 = (h * 3600UL + mi * 60UL + s) * 60;
  w[0] = ((ry / 32) << 14) | (mo << 10) | (d << 5) | (ry % 32);
  w[1] = t50 >> 16; w[2] = t50 & 0xffff;
  w[3] = t60 >> 16; w[4] = t60 & 0xffff;
  w[5] = d | (mo << 5) | ((y - 1980) << 9);
  w[6] = (s / 2) | (mi << 5) | (h << 11);
  w[7] = y; w[8] = mo; w[9] = d; w[10] = wd; w[11] = h; w[12] = mi; w[13] = s;
}

// ---- the SPI target ----------------------------------------------------
static void az_begin(unsigned char cmd) {
  spi_begin(az_spi);
  spi_tx_u08(az_spi, SPI_TARGET_AZ);
  spi_tx_u08(az_spi, cmd);
}

typedef struct {
  int pending, cmd, ie;
  unsigned char unit;              // {ok, 0, 0, number}: what the FPGA's select chose
  unsigned long blkn;
  unsigned written;
  unsigned char seq;
} az_status_t;

static void az_status(az_status_t *st) {
  az_begin(SPI_AZ_STATUS);
  unsigned char b = spi_tx_u08(az_spi, 0);
  st->pending = b >> 7; st->cmd = b & 0x3f;
  st->unit = spi_tx_u08(az_spi, 0);
  st->blkn = 0;
  for(int i = 0; i < 4; i++) st->blkn = (st->blkn << 8) | spi_tx_u08(az_spi, 0);
  st->ie = spi_tx_u08(az_spi, 0) & 1;
  st->written = spi_tx_u08(az_spi, 0) << 8;
  st->written |= spi_tx_u08(az_spi, 0);
  st->seq = spi_tx_u08(az_spi, 0);
  spi_end(az_spi);
}

// n words of the FPGA's buffer at adr, out of / into w
static void az_bread(unsigned adr, unsigned short *w, int n) {
  az_begin(SPI_AZ_READ);
  spi_tx_u08(az_spi, adr >> 8); spi_tx_u08(az_spi, adr & 0xff);
  spi_tx_u08(az_spi, 0);                 // the first word is set up on the address byte
  for(int i = 0; i < n; i++) {
    unsigned char lo = spi_tx_u08(az_spi, 0);
    unsigned char hi = spi_tx_u08(az_spi, 0);
    w[i] = lo | (hi << 8);
  }
  spi_end(az_spi);
}

static void az_bwrite(unsigned adr, const unsigned short *w, int n) {
  az_begin(SPI_AZ_WRITE);
  spi_tx_u08(az_spi, adr >> 8); spi_tx_u08(az_spi, adr & 0xff);
  for(int i = 0; i < n; i++) { spi_tx_u08(az_spi, w[i] & 0xff); spi_tx_u08(az_spi, w[i] >> 8); }
  spi_end(az_spi);
}

static void az_done(int err, int big, unsigned rd_base, unsigned rd_cnt, unsigned wr_base, unsigned wr_cnt,
                    unsigned rd_buf, unsigned init) {
  unsigned long usize = (cur_unit >= 0) ? units[cur_unit].blocks : 0;
  az_begin(SPI_AZ_DONE);
  spi_tx_u08(az_spi, (big ? 2 : 0) | (err ? 1 : 0));
  spi_tx_u08(az_spi, rd_base >> 8); spi_tx_u08(az_spi, rd_base & 0xff);
  spi_tx_u08(az_spi, rd_cnt >> 8);  spi_tx_u08(az_spi, rd_cnt & 0xff);
  spi_tx_u08(az_spi, wr_base >> 8); spi_tx_u08(az_spi, wr_base & 0xff);
  spi_tx_u08(az_spi, wr_cnt >> 8);  spi_tx_u08(az_spi, wr_cnt & 0xff);
  spi_tx_u08(az_spi, rd_buf >> 8);  spi_tx_u08(az_spi, rd_buf & 0xff);
  spi_tx_u08(az_spi, init >> 8);    spi_tx_u08(az_spi, init & 0xff);
  spi_tx_u08(az_spi, usize >> 24); spi_tx_u08(az_spi, usize >> 16); spi_tx_u08(az_spi, usize >> 8); spi_tx_u08(az_spi, usize);
  spi_tx_u08(az_spi, (cur_unit >= 0 ? 0x80 : 0) | (cur_unit & 0x1f));
  spi_end(az_spi);
}

// a string out of the buffer's words (NUL-terminated, at most len-1 chars)
static void words_to_str(const unsigned short *w, int nw, char *s, int len) {
  int n = 0;
  for(int i = 0; i < nw && n < len - 1; i++) {
    s[n++] = w[i] & 0xff; if(!(w[i] & 0xff)) break;
    if(n < len - 1) { s[n++] = w[i] >> 8; if(!(w[i] >> 8)) break; }
  }
  s[n] = 0;
  s[len - 1] = 0;
}

// ---- the file commands (050-055, 047) -----------------------------------
static FIL io_rd, io_wr;
static int io_rd_open = 0, io_wr_open = 0;
static unsigned long io_rd_left = 0, io_wr_left = 0;
static char io_rd_name[AZ_PATH_MAX], io_wr_name[AZ_PATH_MAX];
static int io_status = 0;   // 1 read, -1 write

// ---- the directory (003, 013) ---------------------------------------------
static DIR hfs_dir;
static int hfs_open = 0;

// ---- boot: AZ.INI ----------------------------------------------------------
// what the boot did, for the Debug page and the serial log
static struct {
  int ini;                          // AZ.INI was read
  int files, missing;               // ROM files sent, and named but not opened
  unsigned long bytes;              // bytes sent
  unsigned long checked, wrong;     // words read back through SYS command 8, and those wrong
  int noanswer;                     // reads the core never answered
  unsigned long bad_addr, bad_want, bad_got;   // the first wrong word
} binfo;

// what the MCU has served, for the Debug page: the count, the last
// four commands with their error flags, the reads and writes
static struct {
  unsigned long cmds, reads, writes, errs;
  unsigned char last[4];            // the last four (cmd | err << 7), newest first
  unsigned long last_blkn;
  int last_unit;
  unsigned long rd_ms;              // the last block read's service time
  int phase;                        // where a block read is: 1 opening, 2 seeking, 3 reading, 4 sending, 0 done
} ainfo;

static unsigned long ms_now(void) {
#ifndef SDL
  return xTaskGetTickCount() * portTICK_PERIOD_MS;
#else
  return 0;
#endif
}

// the file read back out of the SDRAM through SYS command 8 against the
// file, every stride-th word: the whole of AZBOOT (the one that must
// run, 16 KB), one in sixteen of the rest - about a fifth of a second
// in all.  A board whose memory does not hold what was written, or
// whose card did not give it, shows here and nowhere else.
static void az_verify(spi_t *spi, FIL *fil, unsigned long addr, unsigned long len, int stride) {
  static unsigned char fbuf[512];
  unsigned long off = 0;
  if(f_lseek(fil, 0) != FR_OK) return;
  while(off < len) {
    UINT rd = 0;
    if(f_read(fil, fbuf, sizeof(fbuf), &rd) != FR_OK || rd == 0) break;
    for(unsigned long o = 0; o < rd; o += 4) {
      if((((off + o) >> 2) % stride) != 0) continue;
      unsigned char w[4];
      unsigned long want = 0, got = 0;
      int n = (rd - o < 4) ? (int)(rd - o) : 4;
      if(sys_peek24(spi, addr + off + o, w) < 0) { binfo.noanswer++; continue; }
      binfo.checked++;
      for(int i = 0; i < n; i++) { want |= (unsigned long)fbuf[o + i] << (8 * i); got |= (unsigned long)w[i] << (8 * i); }
      if(want != got) {
        if(!binfo.wrong) { binfo.bad_addr = addr + off + o; binfo.bad_want = want; binfo.bad_got = got; }
        binfo.wrong++;
      }
    }
    off += rd;
  }
}

static unsigned long az_load(spi_t *spi, const char *path, unsigned long addr, unsigned long max, int stride) {
  FIL fil;
  unsigned long done = 0;
  static unsigned char fbuf[512];
  char full[AZ_PATH_MAX + 16];
  az_path(full, sizeof(full), path);
  if(f_open(&fil, full, FA_OPEN_EXISTING | FA_READ) != FR_OK) {
    printf("AZ: cannot open %s\r\n", full);
    binfo.missing++;
    return 0;
  }
  while(done < max) {
    UINT rd = 0;
    if(f_read(&fil, fbuf, sizeof(fbuf), &rd) != FR_OK || rd == 0) break;
    sys_poke24(spi, addr + done, fbuf, rd);
    done += rd;
  }
  binfo.files++;
  binfo.bytes += done;
  unsigned long wrong0 = binfo.wrong, checked0 = binfo.checked;
  az_verify(spi, &fil, addr, done, stride);
  f_close(&fil);
  printf("AZ: %s, %lu bytes at %06lx, %lu words read back, %lu wrong\r\n", full, done, addr,
         binfo.checked - checked0, binfo.wrong - wrong0);
  return done;
}

const char *az_boot_line(int n) {
  // a buffer a line: menu.c keeps the pointers and formats the page
  // afterwards (the first flash of these showed the last line five times)
  static char lines[7][40];
  if(n < 0 || n >= 7) return NULL;
  char *line = lines[n];
  switch(n) {
  case 0:
    snprintf(line, 40, "ROM: %s, %d files %luK, %d missing",
             binfo.ini ? "ini ok" : "NO AZ.INI", binfo.files, binfo.bytes >> 10, binfo.missing);
    return line;
  case 1:
    snprintf(line, 40, "verify %lu words, %lu bad, %d unanswered", binfo.checked, binfo.wrong, binfo.noanswer);
    return line;
  case 2:
    snprintf(line, 40, "served %lu cmds, %lu rd %lu wr, %lu err, rd %lums", ainfo.cmds, ainfo.reads, ainfo.writes, ainfo.errs, ainfo.rd_ms);
    return line;
  case 3:
    // the last four, newest first, an error marked with !; the last block and unit
    snprintf(line, 40, "last %03o%s %03o%s %03o%s %03o%s blk %lu u%d",
             ainfo.last[0] & 0x3f, ainfo.last[0] & 0x80 ? "!" : "", ainfo.last[1] & 0x3f, ainfo.last[1] & 0x80 ? "!" : "",
             ainfo.last[2] & 0x3f, ainfo.last[2] & 0x80 ? "!" : "", ainfo.last[3] & 0x3f, ainfo.last[3] & 0x80 ? "!" : "",
             ainfo.last_blkn, ainfo.last_unit);
    return line;
  case 4:
    snprintf(line, 40, "unit0 %lu blocks%s", az_unit_blocks(0), az_unit_blocks(0) ? "" : " - NOT FOUND");
    return line;
  case 5:
    snprintf(line, 40, "sdc timeouts %d, read phase %d", sdc_timeouts(), ainfo.phase);
    return line;
  case 6:
    if(!binfo.wrong) return NULL;
    snprintf(line, 40, "at %06lX want %08lX got %08lX", binfo.bad_addr, binfo.bad_want, binfo.bad_got);
    return line;
  default:
    return NULL;
  }
}

static char *trim(char *s) {
  while(*s == ' ' || *s == '\t') s++;
  char *e = s + strlen(s);
  while(e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) *--e = 0;
  return s;
}

static void az_read_ini(spi_t *spi, int load_roms) {
  FIL fil;
  static char line[200];
  char section = 0;
  for(int i = 0; i < AZ_UNITS; i++) { units[i].path[0] = 0; units[i].blocks = 0; }
  memset(&binfo, 0, sizeof(binfo));
  if(f_open(&fil, AZ_INI, FA_OPEN_EXISTING | FA_READ) != FR_OK) {
    printf("AZ: no %s - the machine has no ROMs to run\r\n", AZ_INI);
    return;
  }
  binfo.ini = 1;
  while(f_gets(line, sizeof(line), &fil)) {
    char *s = trim(line);
    if(!*s || *s == ';' || *s == '#') continue;
    if(*s == '[') {
      if(!strncasecmp(s, "[ROM]", 5)) section = 'R';
      else if(!strncasecmp(s, "[LOGO]", 6)) section = 'L';
      else if(!strncasecmp(s, "[DISKS]", 7)) section = 'D';
      else section = 0;
      continue;
    }
    char *eq = strchr(s, '=');
    if(!eq) continue;
    *eq = 0;
    char *key = trim(s), *val = trim(eq + 1);
    if(section == 'R' && (key[0] == 'R' || key[0] == 'r')) {
      int slot = atoi(key + 1);
      if(slot >= 0 && slot < 64 && load_roms) az_load(spi, val, 0x40000UL + slot * 4096UL, 64 * 1024, slot == 0 ? 1 : 16);
    } else if(section == 'L' && (key[0] == 'L' || key[0] == 'l')) {
      if(load_roms) az_load(spi, val, 0x20000UL, 48 * 1024, 16);
    } else if(section == 'D' && (key[0] == 'D' || key[0] == 'd')) {
      int u = atoi(key + 1);
      if(u >= 0 && u < AZ_UNITS) az_set_unit(u, val);
    }
  }
  f_close(&fil);
  for(int i = 0; i < AZ_UNITS; i++)
    if(units[i].path[0]) printf("AZ: unit %d = %s (%lu blocks)\r\n", i, units[i].path, units[i].blocks);
  az_push_sizes();
}

void az_boot(spi_t *spi) {
  az_spi = spi;
  sdc_lock();
  f_mkdir(AZ_ROOT);
  az_read_ini(spi, 1);
  sdc_unlock();
}

// ---- the service -------------------------------------------------------------
void az_handle_event(void) {
  az_status_t st;
  static unsigned short big[400];   // one table row, or a directory record
  char path[AZ_PATH_MAX], full[AZ_PATH_MAX + 16];
  int err = 0, bigf = 0;
  unsigned rd_base = 0, rd_cnt = 0, wr_base = 0, wr_cnt = 0, rd_buf = AZ_R_IOBUF, init = 256;

  if(!az_spi) return;
  az_status(&st);
  if(!st.pending) return;

  ainfo.cmds++;
  if(st.cmd == 005) ainfo.reads++;
  if(st.cmd == 006) ainfo.writes++;
  ainfo.last_blkn = st.blkn;
  sdc_lock();
  switch(st.cmd) {
  case 001: {                      // select the unit (the FPGA does this itself now; kept in case)
    int u = st.unit & 0x1f;
    if(unit_open(u) != 0) err = 1;
    break;
  }
  case 005: {                      // read a block into IOBUF, from the unit the FPGA's select chose
    UINT rd = 0;
    unsigned long t0 = ms_now();
    ainfo.phase = 1;
    if(!(st.unit & 0x80) || unit_open(st.unit & 0x1f) != 0) { err = 1; break; }
    if(st.blkn >= units[cur_unit].blocks) { err = 1; break; }
    ainfo.phase = 2;
    if(f_lseek(&cur_fil, st.blkn * 512UL) != FR_OK) { err = 1; break; }
    ainfo.phase = 3;
    if(f_read(&cur_fil, buf, 512, &rd) != FR_OK) { err = 1; break; }
    if(rd < 512) memset((unsigned char*)buf + rd, 0, 512 - rd);
    ainfo.phase = 4;
    az_bwrite(AZ_R_IOBUF, buf, 256);
    ainfo.rd_ms = ms_now() - t0;
    ainfo.phase = 0;
    break;
  }
  case 006: {                      // write a block out of IOBUF
    UINT wr = 0;
    if(!(st.unit & 0x80) || unit_open(st.unit & 0x1f) != 0) { err = 1; break; }
    if(units[cur_unit].ro || st.blkn >= units[cur_unit].blocks) { err = 1; break; }
    az_bread(AZ_R_IOBUF, buf, 256);
    if(f_lseek(&cur_fil, st.blkn * 512UL) != FR_OK || f_write(&cur_fil, buf, 512, &wr) != FR_OK || wr != 512) err = 1;
    else f_sync(&cur_fil);
    break;
  }
  case 011: {                      // the table of units: 32 rows of 198 words
    for(int u = 0; u < AZ_UNITS; u++) {
      memset(big, 0, 198 * 2);
      if(units[u].path[0] && units[u].blocks) {
        big[0] = units[u].blocks & 0xffff; big[1] = units[u].blocks >> 16;
        big[4] = 0x05;             // flags CFG|MTD, attr 0
        for(int i = 0; i < 384 && units[u].path[i]; i++) {
          if(i & 1) big[5 + i/2] |= units[u].path[i] << 8; else big[5 + i/2] = units[u].path[i];
        }
      }
      az_bwrite(AZ_R_TABLE + u * 198, big, 198);
    }
    rd_buf = AZ_R_TABLE; init = 32 * 198;
    break;
  }
  case 003: {                      // open a directory: the path is in IOBUF
    az_bread(AZ_R_IOBUF, buf, 64);
    words_to_str(buf, 64, path, sizeof(path));
    az_path(full, sizeof(full), path);
    if(hfs_open) f_closedir(&hfs_dir);
    hfs_open = (f_opendir(&hfs_dir, full) == FR_OK);
    if(!hfs_open) err = 1;
    break;
  }
  case 013: {                      // the next directory record into IOBUF
    FILINFO fi;
    if(!hfs_open || f_readdir(&hfs_dir, &fi) != FR_OK || !fi.fname[0]) { err = 1; break; }
    memset(big, 0, 22);
    big[0] = fi.fsize & 0xffff; big[1] = fi.fsize >> 16;
    big[2] = fi.fdate; big[3] = fi.ftime;
    big[4] = fi.fattrib;
    for(int i = 0; i < 12 && fi.fname[i]; i++) {
      if(i & 1) big[4 + (i+1)/2] |= fi.fname[i] << 8; else big[5 + i/2] = fi.fname[i];
    }
    // the record is 11 words: {size lo, hi, date, time, attr|name0<<8, name...}
    az_bwrite(AZ_R_IOBUF, big, 11);
    rd_buf = AZ_R_IOBUF; init = 11;
    break;
  }
  case 004: {                      // mount: "Dn=path" in IOBUF
    az_bread(AZ_R_IOBUF, buf, 200);
    words_to_str(buf, 200, path, sizeof(path));
    char *eq = strchr(path, '=');
    if(!eq || (path[0] != 'D' && path[0] != 'd')) { err = 1; break; }
    *eq = 0;
    if(az_set_unit(atoi(path + 1), trim(eq + 1)) != 0) err = 1;
    break;
  }
  case 014: {                      // unmount: the unit's number as text in IOBUF
    az_bread(AZ_R_IOBUF, buf, 2);
    words_to_str(buf, 2, path, sizeof(path));
    int u = atoi(path + ((path[0] == 'D' || path[0] == 'd') ? 1 : 0));
    if(u < 0 || u >= AZ_UNITS || !units[u].path[0]) err = 1;
    else az_set_unit(u, NULL);
    break;
  }
  case 021: {                      // the EEPROM block into CMOS: a status word, then 255
    FIL fil; UINT rd = 0;
    memset(buf, 0, 512);
    buf[0] = 1;
    if(f_open(&fil, AZ_EEPROM, FA_OPEN_EXISTING | FA_READ) == FR_OK) {
      if(f_read(&fil, &buf[1], 510, &rd) == FR_OK && rd == 510) buf[0] = 0;
      f_close(&fil);
    }
    az_bwrite(AZ_R_CMOS, buf, 256);
    break;
  }
  case 024: {                      // CMOS out to the EEPROM file: 255 words
    FIL fil; UINT wr = 0;
    az_bread(AZ_R_CMOS, buf, 256);
    if(f_open(&fil, AZ_EEPROM, FA_CREATE_ALWAYS | FA_WRITE) == FR_OK) {
      f_write(&fil, buf, 510, &wr);
      f_close(&fil);
      if(wr != 510) err = 1;
    } else err = 1;
    break;
  }
  case 031: case 042:              // the time into the timestamp buffer
    az_timestamp(buf);
    az_bwrite(AZ_R_TS, buf, 14);
    break;
  case 034:                        // the clock set from the buffer's SimpleIN words
    az_bread(AZ_R_TS + 7, buf, 7);
    az_set_time(buf[0], buf[1], buf[2], buf[4], buf[5], buf[6]);
    break;
  case 025: case 026: {            // HOF: no network here
    static const char js[] = "{\"RESULT\":\"ERROR\",\"DESCRIPTION\":\"CONNECTION_ERROR\"}";
    memset(buf, 0, 512);
    memcpy(buf, js, sizeof(js));
    az_bwrite(AZ_R_CMOS, buf, 256);
    break;
  }
  case 050: {                      // the file to read: its name in CMOS
    az_bread(AZ_R_CMOS, buf, 128);
    words_to_str(buf, 128, io_rd_name, sizeof(io_rd_name));
    io_status = 1;
    break;
  }
  case 053: {                      // the file to write
    az_bread(AZ_R_CMOS, buf, 128);
    words_to_str(buf, 128, io_wr_name, sizeof(io_wr_name));
    io_status = -1;
    break;
  }
  case 051: {                      // open it, answer the size or the error, two words into CMOS
    unsigned long v;
    if(io_status == 1) {
      if(io_rd_open) { f_close(&io_rd); io_rd_open = 0; }
      az_path(full, sizeof(full), io_rd_name);
      if(f_open(&io_rd, full, FA_OPEN_EXISTING | FA_READ) == FR_OK) { io_rd_open = 1; io_rd_left = f_size(&io_rd); v = io_rd_left; }
      else { io_rd_left = 0; v = 0x80000000UL | 4; }
    } else if(io_status == -1) {
      if(io_wr_open) { f_close(&io_wr); io_wr_open = 0; }
      az_path(full, sizeof(full), io_wr_name);
      if(f_open(&io_wr, full, FA_CREATE_ALWAYS | FA_WRITE) == FR_OK) { io_wr_open = 1; v = 0; }
      else v = 0x80000000UL | 7;
    } else { err = 1; break; }
    buf[0] = v & 0xffff; buf[1] = v >> 16;
    az_bwrite(AZ_R_CMOS, buf, 2);
    rd_base = AZ_R_CMOS; rd_cnt = 2;
    break;
  }
  case 052: {                      // a block of the file into CMOS
    UINT rd = 0;
    if(!io_rd_open || !io_rd_left) { err = 1; break; }
    unsigned long want = io_rd_left > 512 ? 512 : io_rd_left;
    memset(buf, 0, 512);
    if(f_read(&io_rd, buf, want, &rd) != FR_OK || rd != want) err = 1;
    io_rd_left -= want;
    if(!io_rd_left) { f_close(&io_rd); io_rd_open = 0; }
    az_bwrite(AZ_R_CMOS, buf, 256);
    break;
  }
  case 054:                        // the length of the file to write: two words the processor will write
    wr_base = AZ_R_FSZ; wr_cnt = 2;
    break;
  case 055: {                      // a block out of CMOS into the file
    UINT wr = 0;
    az_bread(AZ_R_FSZ, buf, 2);
    if(!io_wr_left && (buf[0] || buf[1])) io_wr_left = buf[0] | ((unsigned long)buf[1] << 16);
    if(!io_wr_open || !io_wr_left) { err = 1; break; }
    unsigned long want = io_wr_left > 512 ? 512 : io_wr_left;
    az_bread(AZ_R_CMOS, buf, 256);
    if(f_write(&io_wr, buf, want, &wr) != FR_OK || wr != want) err = 1;
    io_wr_left -= want;
    if(!io_wr_left) { f_close(&io_wr); io_wr_open = 0; buf[0] = buf[1] = 0; az_bwrite(AZ_R_FSZ, buf, 2); }
    break;
  }
  case 047: {                      // the file straight into memory: the word address in CMOS
    UINT rd = 0;
    static unsigned char fbuf[512];
    az_bread(AZ_R_CMOS, buf, 2);
    unsigned long addr = (buf[0] | ((unsigned long)buf[1] << 16)) * 2;
    if(!io_rd_open) { err = 1; break; }
    while(io_rd_left) {
      unsigned long want = io_rd_left > 512 ? 512 : io_rd_left;
      if(f_read(&io_rd, fbuf, want, &rd) != FR_OK || rd != want) { err = 1; break; }
      sys_poke24(az_spi, addr, fbuf, want);
      addr += want; io_rd_left -= want;
    }
    f_close(&io_rd); io_rd_open = 0;
    break;
  }
  case 056: {                      // the card's size, in MB, two words
    FATFS *fs; DWORD fre = 0;
    unsigned long total = 0, avail = 0;
    if(f_getfree(CARD_MOUNTPOINT, &fre, &fs) == FR_OK) {
      total = ((unsigned long long)(fs->n_fatent - 2) * fs->csize) / 2048;
      avail = ((unsigned long long)fre * fs->csize) / 2048;
    }
    buf[0] = total > 65535 ? 65535 : total; buf[1] = avail > 65535 ? 65535 : avail;
    az_bwrite(AZ_R_SIZE, buf, 2);
    break;
  }
  case 044:                        // the screenshot: not here yet
  default:
    err = 1;
    break;
  }
  sdc_unlock();
  if(err) ainfo.errs++;
  ainfo.last_unit = cur_unit;
  for(int i = 3; i > 0; i--) ainfo.last[i] = ainfo.last[i-1];
  ainfo.last[0] = (st.cmd & 0x3f) | (err ? 0x80 : 0);
  az_done(err, bigf, rd_base, rd_cnt, wr_base, wr_cnt, rd_buf, init);
}
