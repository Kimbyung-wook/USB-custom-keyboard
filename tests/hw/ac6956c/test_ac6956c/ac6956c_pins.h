// UNVERIFIED DRAFT (2026-11-15): never built, never run. The JieLi SDK and
// compiler are not installed yet (download needs the user's approval).
//
// Pins tried by the shared test logic (tests/hw/ch32v203/test_ch32v203/test_ch32v203.c)
// on the JieLi AC6956C (QFN32 4 x 4, JUZH_3B4 keyboard). Port 0 = A, 1 = B,
// 2 = C: the logic types them as PA00, PB10, PC07.
// Source: AC6956C datasheet, Table 1-1 pin description and Table 2-5 internal
// resistors; ADC channels from the public SDK (fw-AC63_BT_SDK,
// include_lib/driver/cpu/br23/asm/adc_api.h), which match the datasheet table.
// ADC-capable test pins (ac6956c_hal.c adcPins[]): PA1 ch0, PA10 ch3, PB1 ch5,
// PB3 ch6, PB8 ch8, PB10 ch9, PC4 ch0xA, PC5 ch0xD. Other functions (UART,
// IIC ...) only when needed: SDK asm/gpio.h and the datasheet.
//
// Left out (not GPIO, or needed by this firmware):
//   1 VCOM, 2 DACL, 3 DACR, 4 FMIP (audio), 10 VBAT, 11 BT_AVDD, 16 BT_RF,
//   17 BT_OSCI, 18 BT_OSCO, 30 MIC, 31 VDDIO, 32 DACVSS,
//   23 USBDM / 24 USBDP (this firmware's USB; DP is also ADC ch0xC),
//   9 PB5/LDO_IN: a GPIO (high-voltage tolerant, 8 mA) but also the charger
//   input, which may sit on USB 5 V; the shared logic drives every test pin
//   as an output (FIND, ANODE, matrix scan), so it is not tried. Its voltage
//   shows in the ADC mode as LDO5V (internal channel, 1/4).
// Notes per pin:
//   PB1  default pull-up; "Long Press Reset" in the datasheet: holding it low
//        long resets the chip (time set in the SDK/isd_config, UNCONFIRMED).
//        The test drives it low only briefly: matrix scan (ms), FIND (1 s);
//        ANODE drives it high. If the chip resets in FIND on PB1, that is it.
//        Likely the power/boot key.
//   PB0, PB2  high-voltage tolerant GPIO (supplied from RTCVDD), 8 mA drive
//   PB11, PC7 drive strength "/" (not given) in datasheet Table 1-1
//   PC2-PC5   GPIO per Table 2-5 (the "OSC" words beside them in the text copy
//             belong to BT_OSCI/BT_OSCO)
// 12 keys + 2 knobs (A, B, push each) need 18 inputs and only 17 pins exist:
// expect a key matrix ("M PA00 PB01") or resistor-ladder ADC keys (nothing in
// KEYS, a changing value in the ADC mode).
#define NUM_PINS 17
static const uint8_t pinPort[NUM_PINS] = {
  0, 0, 0, 0,             // PA0 (28), PA1 (27), PA9 (26), PA10 (25)
  1, 1, 1, 1,             // PB0 (15), PB1 (14), PB2 (13), PB3 (12)
  1, 1, 1, 1,             // PB8 (8), PB9 (7), PB10 (6), PB11 (5)
  2, 2, 2, 2, 2,          // PC2 (22), PC3 (21), PC4 (20), PC5 (19), PC7 (29)
};
static const uint8_t pinBit[NUM_PINS] = {
  0, 1, 9, 10,
  0, 1, 2, 3,
  8, 9, 10, 11,
  2, 3, 4, 5, 7,
};
