/*
  protocol.h - what the dock says to stage 2 over the UART, and what
  stage 2 answers.  The same file is mnano/coreload_proto.h, by hand.

  Every answer is ONE byte: CL_OK or an error - because the FPGA hands
  the dock only "how many bytes arrived" and "the last one", never a
  stream (mister/coreload.v says why).  Commands:

    CL_PING                              -> CL_OK
    CL_BEGIN len[4] name[16]             -> CL_OK once the slot is erased
                                            (a second or two), or an error
      then `len` bytes of the bitstream, and CL_OK after every 4096 of
      them and after the last few; the dock waits for each before it
      sends the next 4096, so nothing arrives while stage 2 writes flash;
      then crc32[4] of the whole                -> CL_OK: the core is staged
    CL_LOAD                              -> CL_OK when the crc checks and
                                            the load is about to start - the
                                            FPGA is gone a second later -
                                            or an error, and nothing changes
    CL_RELOAD                            -> CL_OK; then the FPGA reloads
                                            from its own flash, address 0
    CL_STATUS                            -> CL_OK if a whole core is staged
                                            and its crc checks, CL_ENOSTAGE
                                            otherwise

  Anything unexpected in the middle of a transfer, or 3 s of silence,
  drops stage 2 back to waiting for a command; the dock's own timeouts
  are longer than any of stage 2's steps.
*/

#ifndef CL_PROTOCOL_H
#define CL_PROTOCOL_H

#define CL_PING     'P'
#define CL_BEGIN    'B'
#define CL_LOAD     'L'
#define CL_RELOAD   'R'
#define CL_STATUS   'S'

#define CL_CHUNK    4096

#define CL_OK        0x00
#define CL_ELENGTH   0x01   /* BEGIN: not a size a bitstream for this FPGA has */
#define CL_ECRC      0x02   /* LOAD: what is in the flash does not match the crc */
#define CL_ENOSTAGE  0x03   /* LOAD: no complete transfer to load */
#define CL_EFLASH    0x04   /* stage 2's own flash refused an erase or a write */
#define CL_EIDCODE   0x05   /* the JTAG does not find a GW2AR-18 */
#define CL_EERASE    0x06   /* the FPGA's SRAM would not erase */
#define CL_ELOAD     0x07   /* loaded, and DONE did not come up */
#define CL_EABORT    0x08   /* the transfer stopped early */

#endif
