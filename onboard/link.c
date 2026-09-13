/*
  link.c - see link.h.
*/

#include "board.h"
#include "bflb_gpio.h"
#include "bflb_uart.h"
#include "bflb_mtimer.h"
#include "link.h"

static struct bflb_device_s *uart;

#define RING 16384
static volatile uint8_t  ring[RING];
static volatile uint32_t ring_wr, ring_rd;

static void link_isr(int irq, void *arg) {
  (void)irq; (void)arg;
  uint32_t st = bflb_uart_get_intstatus(uart);
  if(st & (UART_INTSTS_RX_FIFO | UART_INTSTS_RTO)) {
    while(bflb_uart_rxavailable(uart)) {
      uint8_t b = bflb_uart_getchar(uart);
      uint32_t nxt = (ring_wr + 1) % RING;
      if(nxt != ring_rd) { ring[ring_wr] = b; ring_wr = nxt; }   /* else: lost */
    }
    if(st & UART_INTSTS_RTO) bflb_uart_int_clear(uart, UART_INTCLR_RTO);
  }
}

void link_init(void) {
  struct bflb_device_s *gpio = bflb_device_get_by_name("gpio");
  bflb_gpio_uart_init(gpio, LINK_TX_PIN, GPIO_UART_FUNC_UART1_TX);
  bflb_gpio_uart_init(gpio, LINK_RX_PIN, GPIO_UART_FUNC_UART1_RX);

  struct bflb_uart_config_s cfg = { 0 };
  cfg.baudrate = LINK_BAUD;
  cfg.data_bits = UART_DATA_BITS_8;
  cfg.stop_bits = UART_STOP_BITS_1;
  cfg.parity = UART_PARITY_NONE;
  cfg.flow_ctrl = 0;
  cfg.tx_fifo_threshold = 7;
  cfg.rx_fifo_threshold = 7;
  cfg.bit_order = UART_LSB_FIRST;

  uart = bflb_device_get_by_name("uart1");
  bflb_uart_init(uart, &cfg);
  ring_wr = ring_rd = 0;
  bflb_uart_rxint_mask(uart, false);
  bflb_irq_attach(uart->irq_num, link_isr, NULL);
  bflb_irq_enable(uart->irq_num);
}

int link_avail(void) {
  return (int)((ring_wr + RING - ring_rd) % RING);
}

int link_get(void) {
  if(ring_rd == ring_wr) return -1;
  uint8_t b = ring[ring_rd];
  ring_rd = (ring_rd + 1) % RING;
  return b;
}

int link_get_wait(uint32_t timeout_ms) {
  uint64_t t0 = bflb_mtimer_get_time_ms();
  for(;;) {
    int b = link_get();
    if(b >= 0) return b;
    if(bflb_mtimer_get_time_ms() - t0 > timeout_ms) return -1;
  }
}

void link_put(uint8_t b) {
  bflb_uart_putchar(uart, b);
}

void link_drain(void) {
  ring_rd = ring_wr;
}
