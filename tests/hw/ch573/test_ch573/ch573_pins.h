// Pins tried by the shared test logic (tests/hw/ch32v203/test_ch32v203/test_ch32v203.c)
// on the CH573: every GPIO the register header names (CH573SFR.h), except
// PB10/PB11 (USB D-/D+, this firmware's USB). The 28-pin CH573F may not bond
// out all of them; a missing pin just never changes. Port 0 = A, 1 = B.
// PA10/PA11 are the 32 kHz crystal pins if one is fitted (this firmware uses
// the internal oscillator). PB22 is the ROM bootloader download pin, PB23 the
// reset pin when enabled (off on the SAYO_2B3 board).
#define NUM_PINS 20
static const uint8_t pinPort[NUM_PINS] = {
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // PA4, PA5, PA8-PA15
  1, 1, 1, 1,                   // PB0, PB4, PB6, PB7
  1, 1, 1, 1,                   // PB12-PB15
  1, 1,                         // PB22, PB23
};
static const uint8_t pinBit[NUM_PINS] = {
  4, 5, 8, 9, 10, 11, 12, 13, 14, 15,
  0, 4, 6, 7,
  12, 13, 14, 15,
  22, 23,
};
