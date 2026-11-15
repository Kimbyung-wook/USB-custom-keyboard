// UNVERIFIED DRAFT (2026-11-15): compiled against the SDK, never run.
//
// Pins tried by the shared test logic (tests/hw/ch32v203/test_ch32v203/test_ch32v203.c)
// on the JieLi AC6321A (BD19 family = AC632N, QFN32 4 x 4, JUZH_3B4 keyboard).
// Source: AC6321A datasheet V1.3 pin table (SDK doc/datasheet/AC632N/AC6321A),
// ADC channels from the public SDK (fw-AC63_BT_SDK,
// include_lib/driver/cpu/bd19/asm/adc_api.h), which match the datasheet.
// Alternate functions also checked in "AC6321A_TOP V2.0 IO功能列表.pdf" (same
// SDK folder; its table extracts garbled, so pin numbers and MCLR follow the
// datasheet): SDTAP debug port options on PA8, USB0, PB1, PB3 (unused here),
// Q-decoder1 (knob decoder) on PB2 / PB3 side: a knob may sit on those pins.
//
// Port 0 = A, 1 = B, PORT_USB1 = the second USB pair used as GPIO: the logic
// types it as "PU00" (USB1DP, pin 29) and "PU01" (USB1DM, pin 28).
// ADC-capable test pins (ac6321a_hal.c adcPins[]): PA1 0, PA3 1, PA5 2, PA7 3,
// PA8 4, USB1DP 5, USB1DM 6, PB1 7, PB4 9, PB6 0xC.
//
// Left out (not GPIO, or needed by this firmware):
//   2 SW (DC-DC), 3 VBAT, 4 LDOIN/PP0 (charger 5 V input), 5 VDDIO,
//   6 BTAVDD, 8 BT_RF, 9 BTOSCI, 10 BTOSCO, PAD GND,
//   14 USB0DM / 15 USB0DP (this firmware's USB),
//   7 PA9: default pull-up, "Long Press Reset" (holding it low long resets
//      the chip): the shared logic drives every test pin (FIND 1 s low,
//      matrix scan), so it is not tried. Likely the power/boot key; check
//      it by hand (or in the ADC mode: not in adcPins either, ADC8).
// Notes per pin:
//   High-voltage tolerant (datasheet "(High Voltage)"): PB0, PA0, PB7, PB5, PB3
//   PB2: SDK FAQ #37 (doc/FAQ): changing PB2 on the AC632X trips the VCM
//      reset; hal_init calls VCM_DET_EN(0) first. PB2 held low at power-on
//      keeps the chip from starting (FAQ): no LED or transistor base there,
//      a key there only if not held while powering on
//   Default pull-up: PB2, PB1. PB3 also MCLR, PB1 also LVD (low-voltage
//   detect) in the datasheet: the SDK leaves both as plain GPIO unless enabled
//   PB8 / PB9: 32K_OSCO / 32K_OSCI (32.768 kHz crystal, if any is fitted:
//   the board has none known, UNCONFIRMED; a crystal there shows as a stuck
//   or noisy pin)
//   USB1DP / USB1DM: internal pull-down by default; GPIO after usb1_iomode(1)
// 12 keys + 2 knobs (A, B, push each) need 18 inputs and 21 pins are tried:
// a key matrix ("M PA00 PB01") or resistor-ladder ADC keys (nothing in KEYS,
// a changing value in the ADC mode) is still possible.
#define PORT_USB1 20 // 'A' + 20 = 'U'
#define NUM_PINS 21
static const uint8_t pinPort[NUM_PINS] = {
  0, 0, 0, 0, 0, 0, 0, 0, 0, // PA0 (21), PA1 (20), PA2 (19), PA3 (18), PA4 (17), PA5 (16), PA6 (13), PA7 (12), PA8 (11)
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, // PB0 (1), PB1 (32), PB2 (31), PB3 (30), PB4 (27), PB5 (26), PB6 (25), PB7 (24), PB8 (23), PB9 (22)
  PORT_USB1, PORT_USB1,  // USB1DP (29), USB1DM (28)
};
static const uint8_t pinBit[NUM_PINS] = {
  0, 1, 2, 3, 4, 5, 6, 7, 8,
  0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
  0, 1,
};
