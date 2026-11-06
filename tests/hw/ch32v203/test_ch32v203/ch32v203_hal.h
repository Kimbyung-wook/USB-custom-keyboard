// Chip layer for test_ch32v203. Implemented in ch32v203_hal.c on top of the
// WCH CH32V20x SDK (EVT). The portable test code only uses these names.
#ifndef CH32V203_HAL_H
#define CH32V203_HAL_H

#include <stdint.h>

// Port 0 = A, 1 = B, 2 = C
void hal_init(void);                // 48 MHz clock, TIM2 time base, USB full speed HID
uint32_t hal_millis(void);          // ms since hal_init (TIM2; call at least every 65 ms)
void hal_delay_ms(uint32_t ms);
void hal_reset(void);               // restart the chip
void hal_jump_bootloader(void);     // experiment: start the ISP bootloader from the application
// Reset cause flags (cleared on read): 1 pin, 2 power-on, 4 software (fault), 8 IWDG, 16 WWDG, 32 low power
uint8_t hal_reset_cause(void);
// Last CPU exception before a fault reset: out = mcause, mepc, mtval. Returns 1 once, then clears.
uint8_t hal_fault_read(uint32_t out[3]);

void hal_gpio_input_pullup(uint8_t port, uint8_t bit);
void hal_gpio_output(uint8_t port, uint8_t bit);
void hal_gpio_write(uint8_t port, uint8_t bit, uint8_t high);
uint8_t hal_gpio_read(uint8_t port, uint8_t bit); // 1 = high

// WS2812 (GRB bytes), timed by the chip, interrupts off during the send
void hal_ws2812_show(uint8_t port, uint8_t bit, const uint8_t *grb, uint16_t len);

void hal_hid_write(char c); // one key tap, US layout
void hal_hid_print(const char *s);

#endif
