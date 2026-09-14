/*
  sysctrl.h

  MiSTeryNano system control interface
*/

#ifndef SYS_CTRL_H
#define SYS_CTRL_H

#include "spi.h"

// the MCU can adopt to the core running to e.g.
// change the menu and the keyboard mapping
#define CORE_ID_UNKNOWN  0x00
#define CORE_ID_ATARI_ST 0x01
#define CORE_ID_C64      0x02
#define CORE_ID_UNEON    0x03
#define CORE_ID_AMIGA    0x04
#define CORE_ID_UKNC     0x05
#define CORE_ID_AGAT9    0x06
#define CORE_ID_PK8000   0x07
#define CORE_ID_KORVET   0x08
#define CORE_ID_ZS256    0x09
#define CORE_ID_VIC20    0x10

extern unsigned char core_id;

int  sys_status_is_valid(spi_t *);
void sys_set_leds(spi_t *, char);
void sys_set_rgb(spi_t *, unsigned long);
unsigned char sys_get_buttons(spi_t *);
void sys_set_val(spi_t *, char, uint8_t);
void sys_get_debug(spi_t *, unsigned char *, int);

// the UKNC core's clock (kakave.v), as sysctrl.v's CMD 6 reports it:
// year as is, month 1..12, date 1..31, 24-hour, weekday 1 = Sunday .. 7
typedef struct {
  unsigned short year;
  unsigned char month, date, hour, min, sec, dow;
} sys_rtc_t;
void sys_get_rtc(spi_t *, sys_rtc_t *);

// ZS-256: bytes into the SDRAM at a 24-bit byte address (sysctrl.v's CMD 6,
// three address bytes where the PK8000's poke has two) - romload.c
void sys_poke24(spi_t *, unsigned long addr, const unsigned char *buf, int len);

// tang-ultima: reload the FPGA from the next image in the flash (CMD 9)
void sys_reconfig(spi_t *);
// while set, the SPI task leaves the FPGA's interrupts alone and a
// coldboot notice does not reset the MCU - ultima.c holds it across a
// reconfiguration, when the link is dead and then a new core's
extern volatile int sys_irq_hold;
// the reset the coldboot notice ends in, for ultima.c to use itself
void sys_reset_mcu(void);
void sys_extrom_status(spi_t *, unsigned char *count, unsigned char *room, unsigned char *flags);
void sys_extrom_read(spi_t *, unsigned char *, int);
void sys_extrom_write(spi_t *, const unsigned char *, int);
void sys_extrom_flush(spi_t *);
void sys_extrom_load_rom(spi_t *, const unsigned char *);
unsigned char sys_irq_ctrl(spi_t *, unsigned char);
void sys_handle_interrupts(unsigned char);

#endif // SYS_CTRL_H
