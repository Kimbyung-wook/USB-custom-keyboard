// UNVERIFIED DRAFT (2026-11-15): never built, never run (no JieLi SDK yet).
// SDK names: public fw-AC63_BT_SDK, platform cpu/br23 (see ac6956c_hal.c).
// Chip layer for the shared pin test logic on the JieLi AC6956C (JUZH_3B4
// keyboard). Same names as tests/hw/ch573/test_ch573/ch573_hal.h; implemented
// in ac6956c_hal.c on top of the JieLi SDK (see Makefile for the build idea).
//
// Differences from the other chips:
// - The SDK runs an RTOS: the test runs in its own task (ac6956c_hal.c starts
//   it from the SDK's app entry), and the shared loop must give the CPU back
//   now and then (hal_idle, HAL_HAS_IDLE) and keep the watchdog fed.
// - Port 0 = A, 1 = B, 2 = C.
#ifndef AC6956C_HAL_H
#define AC6956C_HAL_H

#include <stdint.h>

// The shared logic's main() becomes this task body (ac6956c_hal.c calls it)
#define main test_main
int test_main(void);

void hal_init(void);       // GPIO clocks, 1 ms count, USB full speed HID keyboard
uint32_t hal_millis(void); // ms since hal_init
void hal_delay_ms(uint32_t ms); // yields to the RTOS for 10 ms and more
// Reset cause: 1 pin, 2 power-on, 4 software, 8 watchdog, 32 wake from sleep (0 = unknown)
uint8_t hal_reset_cause(void);
// CPU exception record: none on this chip, always 0
uint8_t hal_fault_read(uint32_t out[3]);

void hal_gpio_input_pullup(uint8_t port, uint8_t bit);
void hal_gpio_input_pulldown(uint8_t port, uint8_t bit);
void hal_gpio_output(uint8_t port, uint8_t bit); // push-pull, normal drive
void hal_gpio_write(uint8_t port, uint8_t bit, uint8_t high);
uint8_t hal_gpio_read(uint8_t port, uint8_t bit); // 1 = high

// Once per pass of the shared main loop: watchdog, and an RTOS yield every
// few ms so the USB and system tasks run
#define HAL_HAS_IDLE
void hal_idle(void);

// Analog: types one line "VBAT 3701 LDO5V 5012 PA01 3300 ..." (mV) for the ADC
// test mode: battery and charger input (internal channels, 1/4) and the 8
// ADC-capable pins of the test table, so resistor-ladder keys show up as a
// changing value
#define HAL_HAS_ADC
void hal_adc_report(void);

void hal_hid_write(char c); // one key tap, US layout
void hal_hid_print(const char *s);

#endif
