/* Run production stage-2 command handlers and decoder without hardware.
   No flash APIs are provided: introducing a flash call fails the host link. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#define main stage2_main
#define printf(...) ((void)0)
#include "../onboard/main.c"
#undef printf
#undef main

static uint8_t input[1024 * 1024], replies[1024], output[1024 * 1024];
static uint32_t in_size, in_pos, reply_size, out_size;
static int erases, reloads, last_chunks, erase_error, load_error;
void board_init(void) {}
struct bflb_device_s *bflb_device_get_by_name(const char *s) { (void)s; return NULL; }
void bflb_gpio_init(struct bflb_device_s *d, int p, int f) { (void)d; (void)p; (void)f; }
int bflb_gpio_read(struct bflb_device_s *d, int p) { (void)d; (void)p; return 1; }
uint64_t bflb_mtimer_get_time_ms(void) { return 0; }
void bflb_mtimer_delay_ms(uint32_t ms) { (void)ms; }
void link_init(void) {}
int link_get_wait(uint32_t ms) { (void)ms; return in_pos < in_size ? input[in_pos++] : -1; }
void link_put(uint8_t b) { if(reply_size < sizeof(replies)) replies[reply_size++] = b; }
void jtag_init(void) {}
void jtag_reset(void) {}
uint32_t gw_idcode(void) { return GW2AR18_IDCODE; }
uint32_t gw_status(void) { return 0; }
int gw_erase_sram(void) { erases++; return erase_error; }
void gw_write_begin(void) {}
void gw_write_chunk(const uint8_t *data, uint32_t n, int last) {
  if(out_size + n <= sizeof(output)) memcpy(output + out_size, data, n);
  out_size += n;
  if(last) last_chunks++;
}
uint32_t gw_write_end(void) { return load_error ? 0 : GW_ST_DONE_FINAL; }
void gw_reload(void) { reloads++; }

void test_reset(void) {
  staged_valid = 0; ramstage_invalidate();
  in_size = in_pos = reply_size = out_size = 0;
  erases = reloads = last_chunks = erase_error = load_error = 0;
  rx_logged = tx_logged = 0; log_reset();
}
void test_begin(const uint8_t *p, uint32_t n) {
  if(n > sizeof(input)) return;
  memcpy(input, p, n); in_size = n; in_pos = 0; reply_size = 0;
  cmd_begin();
}
int test_reply(void) { return reply_size ? replies[reply_size - 1] : -1; }
uint32_t test_reply_count(void) { return reply_size; }
void test_load(void) { reply_size = 0; cmd_load(); }
int test_erases(void) { return erases; }
int test_reloads(void) { return reloads; }
int test_last_chunks(void) { return last_chunks; }
uint32_t test_output_size(void) { return out_size; }
const uint8_t *test_output(void) { return output; }
void test_corrupt(void) { ramstage_data()[ramstage_packed() / 2] ^= 0x40; }
void test_fail(int erase, int load) { erase_error = erase; load_error = load; }
