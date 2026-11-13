// Chip layer for the shared pin test logic on the CH573 (SAYO_2B3 keyboard).
// Same names as tests/hw/ch32v203/test_ch32v203/ch32v203_hal.h, only the ones
// the logic uses. Implemented in ch573_hal.c on top of the WCH CH573 EVT SDK.
#ifndef CH573_HAL_H
#define CH573_HAL_H

#include <stdint.h>

// Port 0 = A, 1 = B. Bit 0..23
void hal_init(void);       // 60 MHz PLL, TMR0 1 ms tick, USB full speed HID keyboard
uint32_t hal_millis(void); // ms since hal_init
void hal_delay_ms(uint32_t ms);
// Reset cause: 1 pin, 2 power-on, 4 software, 8 watchdog, 32 wake from sleep/shutdown
uint8_t hal_reset_cause(void);
// CPU exception record: none on this chip yet, always returns 0
uint8_t hal_fault_read(uint32_t out[3]);

void hal_gpio_input_pullup(uint8_t port, uint8_t bit);
void hal_gpio_input_pulldown(uint8_t port, uint8_t bit);
void hal_gpio_output(uint8_t port, uint8_t bit); // push-pull, 5 mA
void hal_gpio_write(uint8_t port, uint8_t bit, uint8_t high);
uint8_t hal_gpio_read(uint8_t port, uint8_t bit); // 1 = high

// Analog: types one line "VBAT 3301 PA08 0 PB00 0 PB06 0" (mV) for the ADC test mode.
// VBAT = the chip supply (internal channel); PA8/PB0/PB6 (AIN12/AIN8/AIN9) are the
// ADC pins not used by keys or LEDs on SAYO_2B3, so a battery divider would be one of them.
#define HAL_HAS_ADC
void hal_adc_report(void);

void hal_hid_write(char c); // one key tap, US layout
void hal_hid_print(const char *s);

#endif
