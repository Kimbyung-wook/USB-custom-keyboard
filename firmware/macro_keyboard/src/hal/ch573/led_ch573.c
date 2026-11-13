// CH573 status LED pins for src/core/bt_led.c (SAYO_2B3, docs/board/BOARD_SAYO_2X3B_RGB.md 4):
// high = on, unused = output low (a pull-up lights the LED dimly).
// Bit order of BTLED_*: L3 PA4, L4 PA5, L5 PA15, L6 PA14, L7 PA13, L8 PA12.
// Built only for the CH573 app (MK_CHIP_CH573 from its Makefile; that app
// does not exist yet): the Arduino IDE and the CH32V203 build skip it.
#if defined(__riscv) && defined(MK_CHIP_CH573)
#include "CH57x_common.h"

static const uint32_t ledPin[6] = {GPIO_Pin_4, GPIO_Pin_5, GPIO_Pin_15, GPIO_Pin_14, GPIO_Pin_13, GPIO_Pin_12};
#define LED_ALL (GPIO_Pin_4 | GPIO_Pin_5 | GPIO_Pin_15 | GPIO_Pin_14 | GPIO_Pin_13 | GPIO_Pin_12)

void hal_btled_init(void) {
  GPIOA_ResetBits(LED_ALL);
  GPIOA_ModeCfg(LED_ALL, GPIO_ModeOut_PP_5mA);
}

void hal_btled_write(uint8_t bits) {
  uint32_t on = 0;
  uint8_t i;
  for (i = 0; i < 6; i++) {
    if (bits & (1u << i)) {
      on |= ledPin[i];
    }
  }
  GPIOA_ResetBits(LED_ALL & ~on);
  GPIOA_SetBits(on);
}
#endif
