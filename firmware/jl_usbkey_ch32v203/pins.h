// Pins of the JieLi USB_KEY adapter (any CH32V203C8 board; PB6/PB7 are its
// own USB to the PC, PA13/PA14 stay SWD). {port, bit}, port 0 = A, 1 = B, 2 = C.
#ifndef JL_USBKEY_PINS_H
#define JL_USBKEY_PINS_H
#include <stdint.h>

typedef struct {
  uint8_t port, bit;
} pin_t;

// Key lines, open drain, each with an external pull-up of 4.7 kOhm or lower
// to 3.3 V. Wired to the analog switch's "key" port, D+ and D- side.
#define PIN_DP {0, 0} // PA0 -> target D+ (via switch)
#define PIN_DM {0, 1} // PA1 -> target D- (via switch)
// Analog switch select (TS3USB221 / FSUSB42 style 2:1 USB switch): which
// port the target's D+/D- reach. SEL_TO_KEY = level that picks this board.
#define PIN_SEL {0, 2} // PA2
#define SEL_TO_KEY 1
// Target power (its USB VBUS through a high-side switch: load switch EN, or
// N-MOSFET driving a P-MOSFET gate). PWR_ON = level that turns it on.
#define PIN_PWR {0, 3} // PA3
#define PWR_ON 1

#endif
