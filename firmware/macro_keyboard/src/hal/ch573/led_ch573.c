// CH573 status LED pins for src/core/bt_led.c (SAYO_2B3, docs/board/BOARD_SAYO_2X3B_RGB.md 4):
// high = on, unused = output low (a pull-up lights the LED dimly).
// Pins: BOARD_BT_LED_PINS of the board file, in BTLED_* bit order, port A only.
// Built only for the CH573 app (MK_CHIP_CH573 from its Makefile; that app
// does not exist yet): the Arduino IDE and the CH32V203 build skip it.
#if defined(__riscv) && defined(MK_CHIP_CH573)
#include "CH57x_common.h"
#include "hal/hal.h"

static const uint8_t ledPin[][2] = {BOARD_BT_LED_PINS};
#define LED_N (sizeof(ledPin) / sizeof(ledPin[0]))
_Static_assert(LED_N == 6, "six status LEDs (BTLED_* bits)");
static uint32_t ledAll;

void hal_btled_init(void) {
  uint8_t i;
  ledAll = 0;
  for (i = 0; i < LED_N; i++) {
    ledAll |= 1ul << ledPin[i][1]; // port A (ledPin[i][0] == 0)
  }
  GPIOA_ResetBits(ledAll);
  GPIOA_ModeCfg(ledAll, GPIO_ModeOut_PP_5mA);
}

void hal_btled_write(uint8_t bits) {
  uint32_t on = 0;
  uint8_t i;
  for (i = 0; i < LED_N; i++) {
    if (bits & (1u << i)) {
      on |= 1ul << ledPin[i][1];
    }
  }
  GPIOA_ResetBits(ledAll & ~on);
  GPIOA_SetBits(on);
}
#endif
