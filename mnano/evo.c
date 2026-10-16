/*
  evo.c - the ZX Evolution's side of the firmware.  See evo.h.
*/
#include <stdio.h>
#include <string.h>
#include <ff.h>

#include "evo.h"
#include "vsd.h"
#include "sysctrl.h"
#include "romload.h"
#include "sdc.h"
#include "companion.h"

unsigned long evo_rom_size = 0, evo_gsrom_size = 0;
static int gs_mode = 2;                  // the OSD's 'g' (sysctrl.v's system_gs)
static int booted = 0;
static int cmos_loaded = 0, cmos_saves = 0;
static char status[64];
static spi_t *evo_spi = NULL;

//------------------------------------------------------------------------
// SYS command 12: the clock's NVRAM (gluk.v, 256 bytes of NVRAM and 256
// of the AVR's EEPROM); the core answers one byte behind
//------------------------------------------------------------------------
static void gluk_begin(spi_t *spi, unsigned char sub) {
  spi_begin(spi);
  spi_tx_u08(spi, SPI_TARGET_SYS);
  spi_tx_u08(spi, SPI_SYS_GLUK);
  spi_tx_u08(spi, sub);
}

static void cmos_put(spi_t *spi, const unsigned char *buf, int len) {
  gluk_begin(spi, 2);
  spi_tx_u08(spi, 0); spi_tx_u08(spi, 0);           // from address 0
  for(int i = 0; i < len; i++) spi_tx_u08(spi, buf[i]);
  spi_end(spi);
}

static void cmos_get(spi_t *spi, unsigned char *buf, int len) {
  gluk_begin(spi, 1 | 0x80);                         // bit 7: the "changed" flag clears
  spi_tx_u08(spi, 0);
  spi_tx_u08(spi, 0);                                // the address's low byte: from 0
  for(int i = 0; i < len; i++) buf[i] = spi_tx_u08(spi, 0);
  spi_end(spi);
}

static int cmos_changed(spi_t *spi) {
  gluk_begin(spi, 0x7F);                             // a sub-command that does nothing
  unsigned char f = spi_tx_u08(spi, 0);              // the answer to it: the flag
  spi_end(spi);
  return f & 1;
}

static void cmos_load(spi_t *spi) {
  static unsigned char buf[512];
  FIL f;
  UINT rd = 0;
  sdc_lock();
  if(f_open(&f, companion_resource("cmos"), FA_OPEN_EXISTING | FA_READ) == FR_OK) {
    f_read(&f, buf, sizeof(buf), &rd);
    f_close(&f);
  }
  sdc_unlock();
  if(rd >= 256) { cmos_put(spi, buf, rd > 512 ? 512 : rd); cmos_loaded = 1; }
  printf("EVO: %s %s\r\n", EVO_CMOS, cmos_loaded ? "loaded" : "not there (the ERS makes its defaults)");
}

// written in place, never truncated: once the file is there a save
// allocates no cluster, and so cannot take one the Evo's own FAT driver
// is about to (the Evo writes the same card, vsd.c)
static void cmos_save(spi_t *spi) {
  static unsigned char buf[512];
  cmos_get(spi, buf, sizeof(buf));
  FIL f;
  UINT wr = 0;
  sdc_lock();
  if(f_open(&f, companion_resource("cmos"), FA_OPEN_ALWAYS | FA_WRITE) == FR_OK) {
    f_write(&f, buf, sizeof(buf), &wr);
    f_close(&f);
  }
  sdc_unlock();
  cmos_saves++;
  printf("EVO: %s saved (%u bytes)\r\n", EVO_CMOS, wr);
}

//------------------------------------------------------------------------
// Start
//------------------------------------------------------------------------

// General Sound's ROM for the OSD's setting: NeoGS's 1.08 (patched to
// count 3.5 MB as 4, tools/gs108patch.py) for NGS 3.5M, gs105b otherwise;
// without the first, the second
static unsigned long gs_rom_load(spi_t *spi) {
  unsigned long n = 0;
  if(gs_mode == 3) {
    n = rom_load(spi, companion_resource("ngs_rom"), EVO_ADDR_GSROM, EVO_MAX_GSROM);
    if(!n) printf("EVO: no %s - General Sound's own ROM instead\r\n", EVO_NGSROM);
  }
  if(!n) n = rom_load(spi, companion_resource("gs_rom"), EVO_ADDR_GSROM, EVO_MAX_GSROM);
  return n;
}
void evo_boot(spi_t *spi, const char *rom) {
  evo_spi = spi;
  const char *path = rom ? rom : companion_resource("rom");
  evo_rom_size = rom_load(spi, path, EVO_ADDR_ROM, EVO_MAX_ROM);
  evo_gsrom_size = gs_rom_load(spi);
  booted = 1;
  if(!evo_rom_size) printf("EVO: no %s - the machine has nothing to run\r\n", EVO_ROM);
  cmos_load(spi);
  if(!cmos_loaded) cmos_save(spi);          // made now, before the Evo uses the card
  vsd_boot(spi, companion_resource("sd"));
  snprintf(status, sizeof(status), "ROM %luK GS %luK SD %d files",
           evo_rom_size / 1024, evo_gsrom_size / 1024, vsd_info.files);
}

void evo_value(spi_t *spi, char id, int val) {
  if(id == 'g') {
    int was_ngs = (gs_mode == 3);
    gs_mode = val;
    if(!booted || was_ngs == (val == 3)) return;
    // the other ROM, with the card off the bus and held in reset ('g' = 0,
    // top.v) while it goes in; then the setting itself again
    sys_set_val(spi, 'g', 0);
    evo_gsrom_size = gs_rom_load(spi);
    sys_set_val(spi, 'g', val);
  }
}

void evo_rom_select(spi_t *spi, const char *path) {
  sys_set_val(spi, 'R', 3);
  evo_rom_size = rom_load(spi, path, EVO_ADDR_ROM, EVO_MAX_ROM);
  sys_set_val(spi, 'R', 0);
}

void evo_handle_event(void) {
  if(evo_spi) vsd_handle_event(evo_spi);
}

void evo_poll(void) {
  if(evo_spi && cmos_changed(evo_spi)) cmos_save(evo_spi);
}

//------------------------------------------------------------------------
// The AVR's PS/2 buffer: the board's AVR put every PC key's scan codes
// (set 2) into a buffer the Z80 reads through the clock's extension cells
// (mode 2, gluk.v); here each USB key event becomes those codes and goes
// in through SYS command 12, sub-command 4.  E0 for the extended keys,
// F0 before a release's code.  Print Screen and Pause send their short
// forms (E0 7C; nothing for Pause), which is what a buffer reader needs.
//------------------------------------------------------------------------
#define X 0x100                                      // E0-prefixed
static const unsigned short ps2_set2[0x66] = {
  /* 00 */ 0, 0, 0, 0,
  /* 04 A-Z */ 0x1C, 0x32, 0x21, 0x23, 0x24, 0x2B, 0x34, 0x33, 0x43, 0x3B, 0x42, 0x4B, 0x3A,
               0x31, 0x44, 0x4D, 0x15, 0x2D, 0x1B, 0x2C, 0x3C, 0x2A, 0x1D, 0x22, 0x35, 0x1A,
  /* 1E 1-0 */ 0x16, 0x1E, 0x26, 0x25, 0x2E, 0x36, 0x3D, 0x3E, 0x46, 0x45,
  /* 28 Enter Esc BS Tab Space - = [ ] \ # ; ' ` , . / Caps */
               0x5A, 0x76, 0x66, 0x0D, 0x29, 0x4E, 0x55, 0x54, 0x5B, 0x5D, 0x5D, 0x4C,
               0x52, 0x0E, 0x41, 0x49, 0x4A, 0x58,
  /* 3A F1-F12 */ 0x05, 0x06, 0x04, 0x0C, 0x03, 0x0B, 0x83, 0x0A, 0x01, 0x09, 0x78, 0x07,
  /* 46 PrtSc ScrLk Pause Ins Home PgUp Del End PgDn Right Left Down Up */
               X|0x7C, 0x7E, 0, X|0x70, X|0x6C, X|0x7D, X|0x71, X|0x69, X|0x7A,
               X|0x74, X|0x6B, X|0x72, X|0x75,
  /* 53 NumLk KP/ KP* KP- KP+ KPEnter KP1-9 KP0 KP. */
               0x77, X|0x4A, 0x7C, 0x7B, 0x79, X|0x5A, 0x69, 0x72, 0x7A, 0x6B, 0x73,
               0x74, 0x6C, 0x75, 0x7D, 0x70, 0x71,
  /* 64 the non-US \, Application */ 0x61, X|0x2F
};
// the modifier byte's bits: LCtrl LShift LAlt LGUI RCtrl RShift RAlt RGUI
static const unsigned short ps2_mods[8] = { 0x14, 0x12, 0x11, X|0x1F, X|0x14, 0x59, X|0x11, X|0x27 };
#undef X

static void ps2_byte(spi_t *spi, unsigned char b) {
  gluk_begin(spi, 4);
  spi_tx_u08(spi, b);
  spi_end(spi);
}

static void ps2_send(spi_t *spi, unsigned short code, int pressed) {
  if(!code) return;
  if(code & 0x100) ps2_byte(spi, 0xE0);
  if(!pressed) ps2_byte(spi, 0xF0);
  ps2_byte(spi, code & 0xFF);
}

void evo_mods(spi_t *spi, unsigned char was, unsigned char now) {
  for(int i = 0; i < 8; i++)
    if((was ^ now) & (1 << i)) ps2_send(spi, ps2_mods[i], (now >> i) & 1);
}

//------------------------------------------------------------------------
// The keys the AVR kept: F11 resets the machine (F12, the board's, is the
// OSD's here), Print Screen is the NMI button.  Every key's PS/2 codes go
// into the buffer first.
//------------------------------------------------------------------------
int evo_key(spi_t *spi, unsigned char code, int pressed) {
  if(code < sizeof(ps2_set2) / sizeof(ps2_set2[0])) ps2_send(spi, ps2_set2[code], pressed);
  if(code == 0x44) {                                 // F11
    if(pressed) { sys_set_val(spi, 'R', 1); sys_set_val(spi, 'R', 0); }
    return 1;
  }
  if(code == 0x46) {                                 // Print Screen
    if(pressed) sys_set_val(spi, 'n', 1);
    return 1;
  }
  return 0;
}

const char *evo_status(void) { return status; }
