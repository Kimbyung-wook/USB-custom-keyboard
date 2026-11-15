// Battery estimate from the chip's supply reading (battery.h). Empty on
// boards without BOARD_BATTERY (the Arduino IDE compiles every file in src).
#include "battery.h"

#ifdef BOARD_BATTERY

static uint16_t mv;       // smoothed estimate, 0 = no reading yet
static uint8_t low, critical;
static uint8_t onUsb, offSamples;   // readings off the cable since the last time on it
static uint16_t usbSamples;         // readings on the cable since it came
static uint16_t restMv, lastVdd;    // estimate before the cable (0 = none), last reading

// Li-Po 1S rest voltage -> charge, rough curve; linear between points
static const uint16_t curveMv[] = {3000, 3450, 3600, 3670, 3710, 3750, 3800, 3860, 3920, 4000, 4100, 4200};
static const uint8_t curvePct[] = {0, 5, 10, 20, 30, 40, 50, 60, 70, 80, 90, 100};
#define CURVE_N (sizeof(curveMv) / sizeof(curveMv[0]))

uint8_t battery_percent_of(uint16_t v) {
  uint8_t i;
  if (v <= curveMv[0]) {
    return 0;
  }
  for (i = 1; i < CURVE_N; i++) {
    if (v < curveMv[i]) {
      uint16_t span = curveMv[i] - curveMv[i - 1];
      uint8_t step = curvePct[i] - curvePct[i - 1];
      return curvePct[i - 1] + (uint8_t)((uint32_t)(v - curveMv[i - 1]) * step / span);
    }
  }
  return 100;
}

static uint8_t below(uint8_t flag, uint16_t limit) {
  if (flag) {
    return mv <= limit + BATTERY_HYST_MV;
  }
  return mv < limit;
}

void battery_usb(uint8_t on) {
  if (on && !onUsb) {
    restMv = offSamples >= BATTERY_REST_SAMPLES ? mv : 0;
    usbSamples = 0;
  }
  if (!on) {
    usbSamples = 0;
  } else {
    offSamples = 0;
  }
  onUsb = on ? 1 : 0;
}

void battery_sample(int16_t vddMv) {
  int32_t v = (int32_t)vddMv + BATTERY_OFFSET_MV - (onUsb ? BATTERY_CHARGE_MV : 0);
  lastVdd = vddMv > 0 ? (uint16_t)vddMv : 0;
  if (onUsb) {
    if (usbSamples < 0xFFFF) {
      usbSamples++;
    }
  } else if (offSamples < 0xFF) {
    offSamples++;
  }
  if (v < 0) {
    v = 0;
  } else if (v > 6000) {
    v = 6000; // a bad reading
  }
  if (!mv) {
    mv = (uint16_t)v; // first reading
  } else {
    mv = (uint16_t)((int32_t)mv + (v - (int32_t)mv) / 4); // smoothing over about 4 readings
  }
  low = below(low, BATTERY_LOW_MV);
  critical = below(critical, BATTERY_CRITICAL_MV);
}

uint16_t battery_mv(void) { return mv; }
uint8_t battery_percent(void) { return battery_percent_of(mv); }
uint8_t battery_low(void) { return mv && low; }
uint8_t battery_critical(void) { return mv && critical; }

void battery_report(uint8_t *out) {
  uint32_t secs = (uint32_t)usbSamples * (BATTERY_SAMPLE_MS / 1000);
  uint8_t i;
  for (i = 0; i < BATTERY_REPORT_SIZE; i++) {
    out[i] = 0;
  }
  if (secs > 0xFFFF) {
    secs = 0xFFFF;
  }
  out[0] = (mv ? BATTERY_HAS_READING : 0) | (onUsb ? BATTERY_ON_USB : 0) |
           (battery_low() ? BATTERY_IS_LOW : 0) | (battery_critical() ? BATTERY_IS_CRITICAL : 0) |
           (onUsb && restMv ? BATTERY_HAS_REST : 0);
  if (mv) {
    out[1] = battery_percent_of(mv);
    out[2] = mv & 0xFF;
    out[3] = mv >> 8;
  }
  if (out[0] & BATTERY_HAS_REST) {
    out[4] = battery_percent_of(restMv);
    out[5] = restMv & 0xFF;
    out[6] = restMv >> 8;
  }
  if (onUsb) {
    out[7] = secs & 0xFF;
    out[8] = secs >> 8;
  }
  out[9] = lastVdd & 0xFF;
  out[10] = lastVdd >> 8;
}

#endif
