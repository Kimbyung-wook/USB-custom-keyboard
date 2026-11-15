// Pins of the JieLi USB_KEY sender on a Blue Pill (STM32F103C8).
// {port, bit}, port 0 = A, 1 = B, 2 = C.
#ifndef JL_BP_PINS_H
#define JL_BP_PINS_H

// Key lines, wired straight to the JUZH_3B4 USB pads (no resistors).
// Push-pull while a key goes out, input (see GAP_RELEASE) between keys,
// floating input when done.
#define PIN_DP {0, 0} // PA0 -> JUZH D+ (chip pin 15)
#define PIN_DM {0, 1} // PA1 -> JUZH D- (chip pin 14)

// On-board LED: PC13, low = on
#define PIN_LED {2, 13}
#define LED_ON 0

// USART1 PA9 (TX) / PA10 (RX) are the ROM serial bootloader's: leave them free.

#endif
