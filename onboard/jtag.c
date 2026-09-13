/*
  jtag.c - see jtag.h.  Straight register writes: GLB_GPIO_CFG138 sets a
  pin, CFG140 clears it, bit 28 of a pin's CFG word is its input - what
  the SDK's bflb_gpio_set/reset/read do, without the calls.
*/

#include "bflb_gpio.h"
#include "bflb_core.h"
#include "hardware/glb_reg.h"
#include "jtag.h"

#define GLB   ((uint32_t)GLB_BASE)
#define SET(p)   putreg32(1u << (p), GLB + GLB_GPIO_CFG138_OFFSET)
#define CLR(p)   putreg32(1u << (p), GLB + GLB_GPIO_CFG140_OFFSET)
#define GET(p)   ((getreg32(GLB + GLB_GPIO_CFG0_OFFSET + ((p) << 2)) >> 28) & 1u)

static int in_shift;   /* 1 while parked in SHIFT-DR between calls */

/* TCK high and low each last JTAG_NOPS nops plus a register write - a
   few MHz; openFPGALoader drives this device at 6 MHz by default */
#ifndef JTAG_NOPS
#define JTAG_NOPS 8
#endif
static inline void tick(void) {
  for(int i = 0; i < JTAG_NOPS; i++) __asm__ volatile("nop");
}

static inline void clock(void) {
  tick();
  SET(JTAG_TCK);
  tick();
  CLR(JTAG_TCK);
}

/* one TMS step: TMS set, TCK pulsed */
static inline void tms(int v) {
  if(v) SET(JTAG_TMS); else CLR(JTAG_TMS);
  clock();
}

void jtag_init(void) {
  struct bflb_device_s *gpio = bflb_device_get_by_name("gpio");
  bflb_gpio_init(gpio, JTAG_TCK, GPIO_OUTPUT | GPIO_FLOAT | GPIO_SMT_EN | GPIO_DRV_1);
  bflb_gpio_init(gpio, JTAG_TDI, GPIO_OUTPUT | GPIO_FLOAT | GPIO_SMT_EN | GPIO_DRV_1);
  bflb_gpio_init(gpio, JTAG_TMS, GPIO_OUTPUT | GPIO_FLOAT | GPIO_SMT_EN | GPIO_DRV_1);
  bflb_gpio_init(gpio, JTAG_TDO, GPIO_INPUT  | GPIO_FLOAT | GPIO_SMT_EN);
  CLR(JTAG_TCK);
  SET(JTAG_TMS);
  CLR(JTAG_TDI);
  in_shift = 0;
}

void jtag_reset(void) {
  for(int i = 0; i < 6; i++) tms(1);   /* TEST-LOGIC-RESET from anywhere */
  tms(0);                              /* RUN-TEST/IDLE */
  in_shift = 0;
}

void jtag_idle_clocks(int n) {
  CLR(JTAG_TMS);
  while(n-- > 0) clock();
}

/* RUN-TEST/IDLE -> SHIFT-IR: 1 1 0 0 ; -> SHIFT-DR: 1 0 0 */
void jtag_shift_ir(uint8_t ir) {
  tms(1); tms(1); tms(0); tms(0);
  for(int i = 0; i < 8; i++) {
    if(ir & 1) SET(JTAG_TDI); else CLR(JTAG_TDI);
    ir >>= 1;
    if(i == 7) SET(JTAG_TMS);          /* last bit: EXIT1-IR */
    clock();
  }
  tms(1);                              /* UPDATE-IR */
  tms(0);                              /* RUN-TEST/IDLE */
}

void jtag_shift_dr(const uint8_t *tdi, uint8_t *tdo, uint32_t bits, int last) {
  if(!in_shift) { tms(1); tms(0); tms(0); in_shift = 1; }
  for(uint32_t i = 0; i < bits; i++) {
    uint8_t b = tdi ? (tdi[i >> 3] >> (i & 7)) & 1 : 0;
    if(b) SET(JTAG_TDI); else CLR(JTAG_TDI);
    if(last && i == bits - 1) SET(JTAG_TMS);
    uint32_t o = GET(JTAG_TDO);        /* TDO is valid before the rising edge */
    if(tdo) {
      if(o) tdo[i >> 3] |=  (1u << (i & 7));
      else  tdo[i >> 3] &= ~(1u << (i & 7));
    }
    clock();
  }
  if(last) { tms(1); tms(0); in_shift = 0; }   /* UPDATE-DR, RUN-TEST/IDLE */
}

void jtag_shift_dr_msb(const uint8_t *data, uint32_t bytes, int last) {
  if(!in_shift) { tms(1); tms(0); tms(0); in_shift = 1; }
  for(uint32_t i = 0; i < bytes; i++) {
    uint8_t d = data[i];
    for(int k = 7; k >= 0; k--) {
      if(d & 0x80) SET(JTAG_TDI); else CLR(JTAG_TDI);
      d <<= 1;
      if(last && i == bytes - 1 && k == 0) SET(JTAG_TMS);
      clock();
    }
  }
  if(last) { tms(1); tms(0); in_shift = 0; }
}
