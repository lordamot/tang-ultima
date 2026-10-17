#include <stddef.h>
#include <string.h>
#include "xz.h"
#include "ramstage.h"
#include "protocol.h"

static uint8_t packed_data[RAMSTAGE_CAPACITY];
static uint8_t decoder_arena[40 * 1024] __attribute__((aligned(8)));
static uint8_t decoded[CL_CHUNK];
static size_t arena_used;
static uint32_t packed_length, raw_length, raw_crc;

/* XZ Embedded's SDK configuration uses these allocators. Reset the bounded
   arena for each pass; no dynamic heap or external PSRAM is needed. */
void *simple_malloc(uint32_t size) {
  size_t off = (arena_used + 7u) & ~(size_t)7u;
  if(size > sizeof(decoder_arena) - off) return NULL;
  arena_used = off + size;
  return decoder_arena + off;
}
void simple_free(void *p) { (void)p; }

uint8_t *ramstage_data(void) { return packed_data; }
uint32_t ramstage_length(void) { return raw_length; }
uint32_t ramstage_packed(void) { return packed_length; }
void ramstage_invalidate(void) { packed_length = raw_length = raw_crc = 0; }

int ramstage_begin(uint32_t packed, uint32_t raw, uint32_t crc) {
  ramstage_invalidate();
  if(!packed || packed > RAMSTAGE_CAPACITY || raw < 0x10000 || raw > 0x100000)
    return CL_ELENGTH;
  packed_length = packed;
  raw_length = raw;
  raw_crc = crc;
  return CL_OK;
}

int ramstage_decode(ramstage_output output) {
  if(!packed_length) return CL_ENOSTAGE;
  arena_used = 0;
  xz_crc32_init();
  struct xz_dec *dec = xz_dec_init(XZ_PREALLOC, RAMSTAGE_DICT);
  if(!dec) return CL_ELENGTH;
  struct xz_buf b = { .in = packed_data, .in_size = packed_length,
                      .out = decoded, .out_size = sizeof(decoded) };
  uint32_t total = 0, crc = 0;
  uint8_t head[32];
  uint32_t head_used = 0;
  int result = CL_ECRC;
  enum xz_ret ret;
  do {
    b.out_pos = 0;
    ret = xz_dec_run(dec, &b);
    if(ret != XZ_OK && ret != XZ_STREAM_END) break;
    if(b.out_pos > raw_length - total) break;
    if(head_used < sizeof(head)) {
      size_t n = sizeof(head) - head_used;
      if(n > b.out_pos) n = b.out_pos;
      memcpy(head + head_used, decoded, n);
      head_used += n;
    }
    total += b.out_pos;
    crc = xz_crc32(decoded, b.out_pos, crc);
    if(output && b.out_pos) output(decoded, b.out_pos, total == raw_length);
    if(ret == XZ_STREAM_END && total == raw_length && b.in_pos == packed_length &&
       crc == raw_crc && head_used == sizeof(head) &&
       head[0x16] == 0xa5 && head[0x17] == 0xc3 &&
       head[0x1c] == 0 && head[0x1d] == 0 && head[0x1e] == 8 && head[0x1f] == 0x1b)
      result = CL_OK;
  } while(ret == XZ_OK);
  xz_dec_end(dec);
  return result;
}
