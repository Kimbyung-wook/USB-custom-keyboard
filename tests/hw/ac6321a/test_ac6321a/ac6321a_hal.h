// UNVERIFIED DRAFT (2026-11-15): compiles against the SDK, never run.
// SDK: public fw-AC63_BT_SDK, platform cpu/bd19 = AC632N (see ac6321a_hal.c).
// Chip layer for the shared pin test logic on the JieLi AC6321A (JUZH_3B4
// keyboard). Same names as tests/hw/ch573/test_ch573/ch573_hal.h; implemented
// in ac6321a_hal.c on top of the JieLi SDK (see Makefile for the build).
//
// Differences from the other chips:
// - The SDK runs an RTOS: the test runs in its own task (ac6321a_hal.c starts
//   it from the SDK's app entry), and the shared loop must give the CPU back
//   now and then (hal_idle, HAL_HAS_IDLE) and keep the watchdog fed.
// - Port 0 = A, 1 = B, PORT_USB1 (ac6321a_pins.h) = the second USB pair as
//   GPIO, typed "PU00" (DP1) and "PU01" (DM1).
#ifndef AC6321A_HAL_H
#define AC6321A_HAL_H

#include <stdint.h>

// The shared logic's main() becomes this task body (ac6321a_hal.c calls it)
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
// test mode: battery and charger input (internal channels, 1/4) and the 10
// ADC-capable pins of the test table, so resistor-ladder keys show up as a
// changing value
#define HAL_HAS_ADC
void hal_adc_report(void);

// Flash identity, typed once at start after "RST": one line
// "FLS size <hex bytes> chip <hex> uid <32 hex>" (ac6321a_hal.c)
#define HAL_HAS_FLASH_INFO
void hal_flash_report(void);

void hal_hid_write(char c); // one key tap, US layout
void hal_hid_print(const char *s);

#endif
