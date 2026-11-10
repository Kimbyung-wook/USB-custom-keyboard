// CH55x implementation of src/hal/hal.h: switches, knob lines, LEDs,
// bootloader. Pins come from the board file (src/boards).

// clang-format off
#include <WS2812.h>
#include "../hal.h"
// clang-format on

// Switches in slot order (keys, then the knob switch): port bit of each
#define NUM_SWITCHES (BOARD_KEYS + 1)
#if NUM_SWITCHES > 8
#error hal_switches() returns at most 8 switches
#endif
static __code uint8_t switchBits[NUM_SWITCHES] = {BOARD_SWITCH_BITS};

void hal_init(void) {
  // Same registers as pinMode: inputs with pull-up are open drain + pull-up,
  // the LED pin push-pull output, driven low
  P1_MOD_OC |= P1_INPUTS;
  P1_DIR_PU |= P1_INPUTS;
  P3_MOD_OC = (P3_MOD_OC | P3_INPUTS) & ~PIN_BIT(PIN_LED);
  P3_DIR_PU |= P3_INPUTS | PIN_BIT(PIN_LED);
  P3 &= ~PIN_BIT(PIN_LED);
  delay(20); // pull-ups settle before input_init reads the knob lines
}

uint8_t hal_switches(void) {
  __data uint8_t pressed = 0;
  __data uint8_t bit = 1;
  for (__data uint8_t i = 0; i < NUM_SWITCHES; i++) {
    if (!((i < BOARD_P1_SWITCHES ? P1 : P3) & switchBits[i])) {
      pressed |= bit;
    }
    bit <<= 1;
  }
  return pressed;
}

uint8_t hal_encoder(void) {
  __data uint8_t p = P3; // A = P3.0, B = P3.1
  return ((p & 1) << 1) | ((p >> 1) & 1);
}

// LEDs: the chain on P3.4, GRB order
#define NUM_LEDS BOARD_LED_COUNT
static __xdata uint8_t ledData[NUM_LEDS * 3];

#if !BOARD_KEY_LEDS || (BOARD_LED_SELF_TEST && !defined(BOARD_LED_RING))
static void fillLeds(uint8_t r, uint8_t g, uint8_t b) {
  // Walked with a pointer (set_pixel_for_GRB_LED multiplies i * 3)
  __xdata uint8_t *p = ledData;
  for (uint8_t i = 0; i < NUM_LEDS; i++) {
    *p++ = g;
    *p++ = r;
    *p++ = b;
  }
}
#endif

#if BOARD_LED_SELF_TEST && defined(BOARD_LED_RING)
// The wheel turning around the keys (src/core/led.c): a color per LED kept in
// ledData, palette units as they are. led_task runs every loop but the colors
// change about every 12 ms: a frame goes out only after a change, as it keeps
// interrupts off (6 LEDs: about 0.18 ms), the same as one hal_leds_rgb frame
static __idata uint8_t ledsChanged;
#define WHEEL_FORGET()

void hal_led_key_rgb(uint8_t key, uint8_t r, uint8_t g, uint8_t b) {
  __xdata uint8_t *p = ledData + (uint8_t)(key * 3);
  if (p[0] != g || p[1] != r || p[2] != b) {
    p[0] = g;
    p[1] = r;
    p[2] = b;
    ledsChanged = 1;
  }
}

void hal_leds_show(void) {
  if (ledsChanged) {
    ledsChanged = 0;
    neopixel_show_P3_4(ledData, NUM_LEDS * 3);
  }
}
#elif BOARD_LED_SELF_TEST
// The last hal_leds_rgb color; wheelR = 0xff (never a color) after any other frame
static __idata uint8_t wheelR = 0xff, wheelG, wheelB;
#define WHEEL_FORGET() (wheelR = 0xff)

// Every LED in one R, G, B (palette units 0..40, sent as they are): the
// power-up color wheel (src/core/led.c). The wheel changes about every 12 ms,
// but led_task calls this every 1 ms loop: the same color is not sent again,
// as a frame keeps interrupts off (about 0.25 ms for 8 LEDs)
void hal_leds_rgb(uint8_t r, uint8_t g, uint8_t b) {
  if (r == wheelR && g == wheelG && b == wheelB) {
    return;
  }
  wheelR = r;
  wheelG = g;
  wheelB = b;
  fillLeds(r, g, b);
  neopixel_show_P3_4(ledData, NUM_LEDS * 3);
}
#else
#define WHEEL_FORGET()
#endif

#if BOARD_KEY_LEDS
void hal_leds(uint8_t color, uint8_t mask, uint8_t maskColor) {
  WHEEL_FORGET();
  // Walked with a pointer (set_pixel_for_GRB_LED multiplies i * 3)
  __xdata uint8_t *p = ledData;
  for (uint8_t i = 0; i < NUM_LEDS; i++) {
    __code uint8_t *rgb = hal_palette[mask & 1 ? maskColor : color];
    mask >>= 1;
    *p++ = rgb[1];
    *p++ = rgb[0];
    *p++ = rgb[2];
  }
  neopixel_show_P3_4(ledData, NUM_LEDS * 3);
}
#else
void hal_leds_all(uint8_t color) {
  WHEEL_FORGET();
  fillLeds(hal_palette[color][0], hal_palette[color][1], hal_palette[color][2]);
  neopixel_show_P3_4(ledData, NUM_LEDS * 3);
}
#endif

void hal_enter_bootloader(void) {
  USB_CTRL = 0;
  EA = 0; // Disabling all interrupts is required.
  TMOD = 0;
  delayMicroseconds(50000);
  delayMicroseconds(50000);

  __asm__("lcall #0x3800"); // Jump to bootloader code

  while (1)
    ;
}
