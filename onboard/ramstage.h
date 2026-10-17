/* Compressed core staging in internal SRAM. No flash access. */
#ifndef RAMSTAGE_H
#define RAMSTAGE_H
#include <stdint.h>
#define RAMSTAGE_CAPACITY (360u * 1024u)
#define RAMSTAGE_DICT 4096u
uint8_t *ramstage_data(void);
/* Invalidate the previous transfer, including when the new header is rejected. */
int ramstage_begin(uint32_t packed, uint32_t raw, uint32_t crc);
void ramstage_invalidate(void);
/* Decode fully before touching the FPGA. Checks XZ, size, CRC and device header.
   A second decode can then emit chunks into JTAG. Returns a CL_E* code. */
typedef void (*ramstage_output)(const uint8_t *, uint32_t, int);
int ramstage_decode(ramstage_output output);
uint32_t ramstage_length(void);
uint32_t ramstage_packed(void);
#endif
