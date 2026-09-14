#ifndef SPI_H
#define SPI_H

#ifndef SDL
#include <FreeRTOS.h>
#include <semphr.h>
#endif

#define SPI_TARGET_SYS    0   // system control target
#define SPI_SYS_STATUS    0
#define SPI_SYS_LEDS      1
#define SPI_SYS_RGB       2
#define SPI_SYS_BUTTONS   3
#define SPI_SYS_SETVAL    4
#define SPI_SYS_IRQ_CTRL  5
// CMD 6 means something different to each core; the firmware only ever
// sends it to the core it is meant for (sysctrl.c, by core_id)
#define SPI_SYS_RTC       6   // UKNC: read the Kakave+ clock (sysctrl.v CMD 6)
#define SPI_SYS_POKE      6   // PK8000: an address and bytes into the machine's RAM (bas.c);
                              // ZS-256: the same with THREE address bytes, into the SDRAM (romload.c)
#define SPI_SYS_DEBUG     7   // PK8000/Korvet/ZS-256: the debug window - an offset, then bytes
#define SPI_SYS_EXTROM    8   // Korvet: the ExtROM channel (extrom.v) - a sub-command, then bytes
#define SPI_SYS_RECONFIG  9   // all four: A5h after it pulses RECONFIG_N.  Dormant - the pulse
                              // does not reload this FPGA (docs/progress.md); kept because a
                              // wire from pin 48 to TP1 would make it work
#define SPI_SYS_FLASH    10   // all four: the configuration flash, through flashwr.v - a
                              // sub-command, then its bytes (flashwr.c).  "Save to flash":
                              // the running machine is written to flash address 0, which
                              // is what power-up loads.
#define SPI_SYS_CORELOAD 11   // all four: the UART to the board's own BL616, through
                              // coreload.v - a sub-command, then its bytes (coreload.c).
                              // This is how a core switch happens: the wanted machine is
                              // sent to that chip and it loads the FPGA's SRAM over JTAG.

#define SPI_TARGET_HID    1   // human interface devices
#define SPI_HID_STATUS    0
#define SPI_HID_KEYBOARD  1
#define SPI_HID_MOUSE     2
#define SPI_HID_JOYSTICK  3
#define SPI_HID_GET_DB9   4

#define SPI_TARGET_OSD    2   // on-screen-display
#define SPI_OSD_ENABLE    1
#define SPI_OSD_WRITE     2

#define SPI_TARGET_SDC    3   // sd card
#define SPI_SDC_STATUS    1   // get sd card status
#define SPI_SDC_CORE_RW   2   // trigger core read/write
#define SPI_SDC_MCU_READ  3   // read sector into MCU (e.g. for dir listing)
#define SPI_SDC_INSERTED  4   // inform core that some disk image has been insered
#define SPI_SDC_MCU_WRITE 5   // write sector from MCU

typedef struct {
#ifndef SDL
  struct bflb_device_s *dev;
  SemaphoreHandle_t sem;
#endif
} spi_t;
  
spi_t *spi_init(void);
void spi_begin(spi_t *spi);
unsigned char spi_tx_u08(spi_t *spi, unsigned char b);
void spi_end(spi_t *spi);

// this is still on usb_host.c but should eventially go
// into a separate hid.c
extern void hid_handle_event(void);

#endif // SPI_H
