/*
  macro_keyboard: firmware for the 6-key + 1-knob CH552G mini keyboards
  (boards MK6K1N and MK6K1NL, src/boards/, docs/PINMAP.md)

  - Interface 0: HID keyboard. Each key/knob action types the text of its
    slot (macro syntax in docs/PROTOCOL.md).
  - Interface 1: vendor HID for the config GUI (gui/), protocol v2.
  - Config is stored in the 128-byte DataFlash.
  - Lock (README.md "잠금 상태도"): with a lock pattern stored, it starts
    locked and ignores every input. Holding the knob for 2 s starts a
    pattern entry: the pattern unlocks it. On an open keyboard the same hold
    and pattern lock it again (five wrong re-lock tries lock it as well).
  - LEDs: src/core/led.h.
  - Bootloader: hold key D (ROM bootloader, P1.5) or hold the knob while
    plugging in, or send BOOT from the GUI.

  Layers (firmware/macro_keyboard/README.md "코드 구조"): this file only
  calls src/core/app.c, the sequence shared with the CH32V203 build. The
  functions in src/core use the hardware only through src/hal/hal.h; the
  CH55x implementation is src/hal/ch552 (pins from the board file).

  Board options: CH552 Board, 24 MHz (internal) 5V,
                 USB Settings: USER CODE w/ 266B USB ram,
                 Bootloader pin: P1.5 pull-down
  cli board options: usb_settings=user266,bootloader_pin=p15

  USB code in src/hal/ch552/usb is adapted from the CH55xduino
  qmkCompatibleKeyboard example (LGPL-2.1).
*/

#ifndef USER_USB_RAM
#error "This sketch needs to be compiled with a USER USB setting"
#endif

#include "src/core/app.h"

void setup() { app_setup(); }

void loop() { app_loop(); }
