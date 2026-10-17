#ifndef TEST_BOARD_H
#define TEST_BOARD_H
#include <stdint.h>
struct bflb_device_s { int unused; };
void board_init(void);
struct bflb_device_s *bflb_device_get_by_name(const char *name);
#endif
