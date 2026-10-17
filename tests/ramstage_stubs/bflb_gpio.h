#include "board.h"
#define GPIO_INPUT 1
#define GPIO_FLOAT 2
#define GPIO_SMT_EN 4
void bflb_gpio_init(struct bflb_device_s *, int, int);
int bflb_gpio_read(struct bflb_device_s *, int);
