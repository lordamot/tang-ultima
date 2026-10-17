/* RAM-only switch protocol. Shared verbatim with mnano/coreload_proto.h.
   P -> 0; V -> CL_RAM_VERSION. Both are required before sending a core.
   C packed_len[4] name[16] raw_len[4] raw_crc32[4] -> 0 or error.
     Then packed_len XZ bytes, acknowledged every 4096 bytes (and final
     partial chunk); then packed CRC32[4] -> 0 after full validation.
     XZ uses CRC32 and a dictionary of at most 4096 bytes.
   L -> 0 after validation, then FPGA SRAM is replaced from RAM.
   S -> 0 if a validated core is in RAM; R -> 0 then boot FPGA flash.
   B (legacy flash staging) is refused. There is no flash-writing fallback.
   All fields are little endian. A timeout invalidates the incomplete core.
*/

#ifndef CL_PROTOCOL_H
#define CL_PROTOCOL_H

#define CL_PING     'P'
#define CL_BEGIN    'B'  /* legacy: always refused */
#define CL_BEGIN_RAM 'C'
#define CL_CAPS     'V'
#define CL_RAM_VERSION 0x51
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
#define CL_EVERSION  0x09   /* incompatible stage-2 protocol */
#define CL_EABORT    0x08   /* the transfer stopped early */

#endif
