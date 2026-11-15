// AC6321A status LEDs for src/core/bt_led.c (JUZH_3B4), UNVERIFIED on a chip.
// The 3 LEDs between the knobs (BOARD_BT_LED_PINS: R, G, B of LED 1, 2, 3).
// Provisional mapping of the BTLED_* bits (TODO(user): confirm):
//   BLUE1/2/3 (BLE slot 1-3) -> LED 1/2/3 blue
//   GREEN (USB cable)        -> LED 2 green
//   RED (battery)            -> LED 3 red
//   WHITE (key blip)         -> LED 1 white (R + G + B)
// A PIN_NONE pin is never touched.
#if defined(MK_CHIP_AC6321A)
#include "asm/gpio.h"
#include "../hal.h"
#include "../../core/bt_led.h"

#ifdef BOARD_BT_LED
#define NO_PIN 0xFF
#define PORT_USB1 2

static const uint8_t ledPin[][2] = {BOARD_BT_LED_PINS};
#define LED_N (sizeof(ledPin) / sizeof(ledPin[0]))
_Static_assert(LED_N == 9, "3 RGB LEDs: R, G, B each");

// Pin index = 3 * LED + colour (0 R, 1 G, 2 B)
#define PIN(led, c) (3 * (led) + (c))

static uint32_t gpioNum(uint8_t i) {
  if (ledPin[i][0] == PORT_USB1) {
    return IO_PORT_DP1 + ledPin[i][1];
  }
  return (uint32_t)ledPin[i][0] * IO_GROUP_NUM + ledPin[i][1];
}

static void pinSet(uint8_t i, uint8_t on) {
  if (ledPin[i][0] == NO_PIN) {
    return;
  }
  gpio_set_output_value(gpioNum(i), on == BOARD_BT_LED_ACTIVE_HIGH);
}

void hal_btled_init(void) {
  uint8_t i;
  for (i = 0; i < LED_N; i++) {
    if (ledPin[i][0] == NO_PIN) {
      continue;
    }
    gpio_set_pull_up(gpioNum(i), 0);
    gpio_set_pull_down(gpioNum(i), 0);
    pinSet(i, 0);
    gpio_set_direction(gpioNum(i), 0);
  }
}

void hal_btled_write(uint8_t bits) {
  uint16_t on = 0; // bit = pin index
  uint8_t i;
  if (bits & BTLED_BLUE1) on |= 1u << PIN(0, 2);
  if (bits & BTLED_BLUE2) on |= 1u << PIN(1, 2);
  if (bits & BTLED_BLUE3) on |= 1u << PIN(2, 2);
  if (bits & BTLED_GREEN) on |= 1u << PIN(1, 1);
  if (bits & BTLED_RED) on |= 1u << PIN(2, 0);
  if (bits & BTLED_WHITE) on |= 7u << PIN(0, 0);
  for (i = 0; i < LED_N; i++) {
    pinSet(i, (on >> i) & 1);
  }
}
#endif
#endif
