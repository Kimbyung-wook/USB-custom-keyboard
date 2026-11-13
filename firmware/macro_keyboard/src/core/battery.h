#ifndef __MK_BATTERY_H__
#define __MK_BATTERY_H__

#include "../config.h"

// Battery estimate for boards on a Li-Po cell without a regulator
// (SAYO_2B3: battery -> diode D1 -> VDD33, docs/board/BOARD_SAYO_2X3B_RGB.md 5).
// The chip measures its own supply (VDD, mV) and feeds it here; this code is
// chip-free (tests/host). Only built with BOARD_BATTERY in the board file.
//
// Measure when the radio is quiet (the diode drop grows with current), a few
// to tens of seconds apart: the HAL averages one reading, battery_sample()
// smooths the readings.

#ifdef BOARD_BATTERY

// battery = VDD reading + this. SAYO_2B3: ADC reads about 55 mV high (3.46 V
// multimeter = 3,515-3,519 mV), diode about 711 mV -> +656, rounded to 650
#ifndef BATTERY_OFFSET_MV
#define BATTERY_OFFSET_MV 650
#endif
// Low-battery flag below this, cleared again above it + BATTERY_HYST_MV
#ifndef BATTERY_LOW_MV
#define BATTERY_LOW_MV 3400
#endif
// Critical (chip minimum 2.3 V = battery about 3.0 V): deep sleep candidate
#ifndef BATTERY_CRITICAL_MV
#define BATTERY_CRITICAL_MV 3100
#endif
#ifndef BATTERY_HYST_MV
#define BATTERY_HYST_MV 100
#endif
// Suggested time between readings (the app decides when the radio is idle)
#ifndef BATTERY_SAMPLE_MS
#define BATTERY_SAMPLE_MS 10000
#endif

void battery_sample(int16_t vddMv); // one averaged VDD reading, mV
uint16_t battery_mv(void);          // smoothed battery estimate, 0 before the first reading
uint8_t battery_percent(void);      // 0..100 (Li-Po curve, 3.0 V = 0, 4.2 V = 100), for BLE BAS
uint8_t battery_low(void);          // 1 = below BATTERY_LOW_MV (with hysteresis)
uint8_t battery_critical(void);     // 1 = below BATTERY_CRITICAL_MV (with hysteresis)
uint8_t battery_percent_of(uint16_t mv); // the curve itself

// From the chip HAL (CH573: src/hal/ch573/battery_ch573.c): VDD in mV
int16_t hal_battery_vdd_mv(void);

#endif
#endif
