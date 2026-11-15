// Timing and behaviour of the JieLi USB_KEY sender (Blue Pill).
// Logic follows ../jl_usbkey_ch32v203/main.c (ENTER, order 0 = auto).
#ifndef JL_BP_CONFIG_H
#define JL_BP_CONFIG_H

#define USB_KEY 0x16EF   // 16-bit key, MSB first
#define HALF_US 10u      // half clock period in us (10 -> 50 kHz)
#define SEND_MS 10000u   // send keys for this long after reset
#define AUTO_SWITCH 8    // swap clock line (D+ <-> D-) every 8 keys
#define FIRST_CLK 1      // 1: D+ clock first, 2: D- clock first

// Gap after each key = the CH32V203 code's ack window (3 ms, ack = both
// lines low for >= 300 us).
#define ACK_WAIT_US 3000u
#define ACK_MIN_US 300u

// Lines during the gap:
//   1: released (input with internal pull-up): no fight if the chip pulls
//      the lines low to answer, ack can be sensed. The PC's 15 kOhm
//      pull-downs (and the chip's own) beat the ~40 kOhm pull-up, so the
//      lines may read low anyway: a sensed ack is a hint only.
//   0: driven high (idle high like the CH32V203 board's 4.7 kOhm pull-ups),
//      no ack sensing; fights the chip if it does answer (both push-pull).
#define GAP_RELEASE 1

// 1: stop at the first sensed ack (CH32V203 behaviour). 0: keep sending the
// whole SEND_MS and show the ack on the LED only (default, see above).
#define ACK_STOPS 0

// LED after sending: no ack sensed = slow blink (BLINK_MS on, BLINK_MS off);
// ack sensed = double flash every 2 s.
#define BLINK_MS 500u

#endif
