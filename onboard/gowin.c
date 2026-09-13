/*
  gowin.c - see gowin.h.  Each function follows the openFPGALoader
  routine it is named after: send_command, readReg32, eraseSRAM,
  writeSRAM (split so the bitstream can be streamed from flash).
*/

#include "bflb_mtimer.h"
#include "jtag.h"
#include "gowin.h"

void gw_command(uint8_t cmd) {
  jtag_shift_ir(cmd);
  jtag_idle_clocks(6);
}

uint32_t gw_read32(uint8_t cmd) {
  uint8_t ones[4] = { 0xff, 0xff, 0xff, 0xff }, out[4] = { 0, 0, 0, 0 };
  gw_command(cmd);
  jtag_shift_dr(ones, out, 32, 1);
  return (uint32_t)out[0] | ((uint32_t)out[1] << 8) |
         ((uint32_t)out[2] << 16) | ((uint32_t)out[3] << 24);
}

uint32_t gw_idcode(void) { return gw_read32(GW_READ_IDCODE); }
uint32_t gw_status(void) { return gw_read32(GW_STATUS); }

/* pollFlag: read the status until (status & mask) == value */
static int poll(uint32_t mask, uint32_t value, uint32_t tries) {
  while(tries--) {
    if((gw_status() & mask) == value) return 0;
  }
  return 1;
}

static int enable_cfg(void) {
  gw_command(GW_CONFIG_ENABLE);
  return poll(GW_ST_SYSTEM_EDIT_MODE, GW_ST_SYSTEM_EDIT_MODE, 1000);
}

static int disable_cfg(void) {
  gw_command(GW_CONFIG_DISABLE);
  gw_command(GW_NOOP);
  return poll(GW_ST_SYSTEM_EDIT_MODE, 0, 1000);
}

/* gw2a_force_state: an undocumented sequence openFPGALoader sends when
   the status shows a CRC error from a failed flash boot */
static void force_state(void) {
  if(!(gw_status() & GW_ST_CRC_ERROR)) return;
  gw_command(GW_CONFIG_DISABLE); gw_command(0);
  gw_idcode(); gw_status();
  gw_command(GW_CONFIG_DISABLE); gw_command(0);
  gw_status(); gw_idcode();
  gw_command(GW_CONFIG_ENABLE);
  gw_command(GW_RELOAD); gw_command(GW_NOOP);
  gw_command(GW_CONFIG_DISABLE); gw_command(GW_NOOP);
  gw_idcode(); gw_command(GW_NOOP); gw_idcode();
}

void gw_reload(void) {
  gw_command(GW_RELOAD);
  gw_command(GW_NOOP);
}

int gw_erase_sram(void) {
  force_state();
  if(enable_cfg()) return 1;
  gw_command(GW_ERASE_SRAM);
  gw_command(GW_NOOP);
  /* the MEMORY_ERASE bit drops on ERASE_SRAM and rises when it is done */
  if(poll(GW_ST_MEMORY_ERASE, GW_ST_MEMORY_ERASE, 100000)) return 2;
  gw_command(GW_XFER_DONE);
  gw_command(GW_NOOP);
  if(disable_cfg()) return 3;
  return 0;
}

void gw_write_begin(void) {
  gw_command(GW_CONFIG_ENABLE);
  gw_command(GW_INIT_ADDR);      /* UG704 3.4.3 */
  gw_command(GW_XFER_WRITE);
}

void gw_write_chunk(const uint8_t *data, uint32_t bytes, int last) {
  jtag_shift_dr_msb(data, bytes, last);
}

uint32_t gw_write_end(void) {
  /* openFPGALoader sends the .fs checksum here through 0x0a/0x08; on
     the GW2A it does not verify it ("FIXME: implement GW2 checksum")
     and the load succeeds regardless, so zero goes */
  uint8_t sum[4] = { 0, 0, 0, 0 };
  gw_command(0x0a);
  jtag_shift_dr(sum, 0, 32, 1);
  gw_command(0x08);
  gw_command(GW_CONFIG_DISABLE);
  gw_command(GW_NOOP);
  return gw_status();
}
