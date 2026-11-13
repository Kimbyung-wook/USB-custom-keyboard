// Dongle board pins: the SAYO_2B3 dongle has no LED and no button (confirmed),
// so both stay -1 (none) and only the quick-replug gestures work.
// Other boards: pin number PA0..PA15 = 0..15, PB0..PB23 = 32..55.
#ifndef BOARD_H
#define BOARD_H

#define PIN_PA(n) (n)
#define PIN_PB(n) (32 + (n))

#ifndef BOARD_LED_PIN
#define BOARD_LED_PIN (-1)  // status LED
#endif
#ifndef BOARD_LED_ON
#define BOARD_LED_ON 1      // level that lights the LED
#endif
#ifndef BOARD_BTN_PIN
#define BOARD_BTN_PIN (-1)  // pairing button (pulled up, active low)
#endif

#define BOARD_PRODUCT "BT Dongle"

#endif
