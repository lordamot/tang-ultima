/* Production companion transport with a byte-level CMD 11 peer and SD stub. */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#define SDL 1
#include "../mnano/coreload.c"
static uint8_t file_data[400000], wire_data[400000];
static unsigned file_size, file_pos, wire_size, stage_bytes, expected, chunk_bytes;
static unsigned frame_pos, sub, queued;
static int answer, ram_peer, claimed, closed, missing, short_read;
void sdc_lock(void) {}
void sdc_unlock(void) {}
FRESULT f_open(FIL *f, const TCHAR *path, BYTE mode) {
  if(missing) return FR_NO_FILE;
  if(strcmp(path, "/sd/cores/test.bin.ram") || mode != FA_READ) return FR_INVALID_NAME;
  memset(f, 0, sizeof(*f)); f->obj.objsize = file_size; file_pos = 0;
  return FR_OK;
}
FRESULT f_close(FIL *f) { (void)f; closed++; return FR_OK; }
FRESULT f_read(FIL *f, void *buf, UINT want, UINT *got) {
  (void)f;
  *got = file_size - file_pos < want ? file_size - file_pos : want;
  if(short_read && *got) --*got;
  memcpy(buf, file_data + file_pos, *got); file_pos += *got;
  return FR_OK;
}
void spi_begin(spi_t *spi) { (void)spi; frame_pos = 0; sub = 255; }
static void peer(uint8_t b) {
  wire_data[wire_size++] = b;
  if(!stage_bytes) {
    if(b == CL_PING) { answer = CL_OK; queued = 1; }
    else if(b == CL_CAPS) { if(ram_peer) { answer = CL_RAM_VERSION; queued = 1; } }
    else if(b == CL_BEGIN_RAM) { stage_bytes = 1; expected = chunk_bytes = 0; }
    else if(b == CL_LOAD) { answer = CL_OK; queued = 1; }
  } else {
    unsigned pos = stage_bytes++;
    if(pos <= 4) expected |= (uint32_t)b << (8 * (pos - 1));
    if(stage_bytes == 29) { answer = CL_OK; queued = 1; }
    else if(stage_bytes > 29 && stage_bytes <= 29 + expected) {
      chunk_bytes++;
      if(chunk_bytes == CL_CHUNK || stage_bytes == 29 + expected) {
        chunk_bytes = 0; answer = CL_OK; queued = 1;
      }
    } else if(stage_bytes == 29 + expected + 4) {
      stage_bytes = 0; answer = CL_OK; queued = 1;
    }
  }
}
unsigned char spi_tx_u08(spi_t *spi, unsigned char b) {
  (void)spi;
  unsigned pos = frame_pos++;
  if(pos < 2) return 0;
  if(pos == 2) { sub = b; if(sub == SUB_FLUSH) queued = 0; return 0; }
  if(sub == SUB_STATUS) return STATUS_ROOM | queued;
  if(sub == SUB_LAST) return answer;
  if(sub == SUB_CLAIM) { claimed = b; return 0; }
  if(sub == SUB_WRITE) peer(b);
  return 0;
}
void spi_end(spi_t *spi) { (void)spi; }
void dock_reset(const uint8_t *data, unsigned n, int ram) {
  memcpy(file_data, data, n); file_size = n;
  file_pos = wire_size = stage_bytes = expected = chunk_bytes = 0;
  ram_peer = ram; queued = claimed = closed = missing = short_read = 0;
}
void dock_fail(int absent, int shortread) { missing = absent; short_read = shortread; }
int dock_ping(void) { return cl_ping(NULL); }
int dock_send(void) { return cl_send(NULL, "/sd/cores/test.bin", "test", NULL); }
int dock_load(void) { return cl_load(NULL); }
void dock_release(void) { cl_release(NULL); }
unsigned dock_wire_size(void) { return wire_size; }
const uint8_t *dock_wire(void) { return wire_data; }
int dock_closed(void) { return closed; }
int dock_claimed(void) { return claimed; }
