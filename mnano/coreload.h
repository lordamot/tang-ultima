/*
  coreload.h - tang-ultima: a core into the FPGA's SRAM, through the
  board's own BL616.

  The Tang Nano 20K's on-board BL616 owns the FPGA's JTAG, and with
  tang-ultima's stage 2 firmware in it (onboard/) it loads a core into
  the FPGA's SRAM the way openFPGALoader does - in about a second, with
  the configuration flash untouched.  It cannot see the card, so the
  core is sent to it first, over a UART every core carries
  (mister/coreload.v, SYS CMD 11: pin 69 into the chip, 70 back), and it
  keeps the bytes in its own flash until told to load them.

  This is the dock's side of that: the CMD 11 primitives, and the
  conversation coreload_proto.h defines on top of them.  A switch is
  cl_ping(), cl_send(), cl_load(), and then a reset of this MCU, because
  the FPGA - and with it the link, the card and everything the firmware
  has set up - is about to become another machine.

  Only when the board is powered from something that is not a PC: on a
  PC the Partner firmware keeps the chip as the programmer and stage 2
  never runs, so cl_ping() gets no answer.
*/

#ifndef CORELOAD_H
#define CORELOAD_H

#include "spi.h"

// progress stages, for the OSD (menu.c's install screen)
#define CL_STAGE_CONNECT 4
#define CL_STAGE_SEND    5
#define CL_STAGE_LOAD    6
typedef void (*cl_progress_t)(int stage, int done, int total);

// is stage 2 there and answering?  0 yes; CL_ERR_CMD11 when the FPGA's
// own side never offers room (the link into the FPGA is broken, or the
// core has no CMD 11); CL_ERR_LINK when it does and nothing comes back
// (on a PC, or no stage 2).  Claims the UART's TX pin for the link (the
// UKNC's serial port shares it); cl_release() gives it back when a
// switch does not happen after all.
int cl_ping(spi_t *spi);
unsigned char cl_ping_status(void);   // the status byte the ping last read
void cl_release(spi_t *spi);

// Send the bitstream at `path` to stage 2, which stages it in its flash
// under `name`.  0, or a negative CL_ERR_* below.  The file is checked
// to be a bitstream for this FPGA before a byte goes.
int cl_send(spi_t *spi, const char *path, const char *name, cl_progress_t cb);

// Tell stage 2 to load what it has.  Returns 0 when it said it is about
// to - the FPGA is gone within a second - or an error; on 0 the caller
// resets this MCU and never returns.
int cl_load(spi_t *spi);

// Tell stage 2 to reload the FPGA from its flash (address 0).  Same
// contract as cl_load().
int cl_reload(spi_t *spi);

#define CL_ERR_LINK     -20   // stage 2 does not answer: on a PC, or not installed
#define CL_ERR_OPEN     -21
#define CL_ERR_SIZE     -22
#define CL_ERR_NOTBIT   -23
#define CL_ERR_READ     -24
#define CL_ERR_REFUSED  -25   // stage 2 said no: cl_last_code() has its byte
#define CL_ERR_TIMEOUT  -26   // stage 2 stopped answering mid-way
#define CL_ERR_CMD11    -27   // the FPGA's FIFO never reports room: CMD 11 itself

unsigned char cl_last_code(void);
const char *cl_strerror(int err);

#endif // CORELOAD_H
