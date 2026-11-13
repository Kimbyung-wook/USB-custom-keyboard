// CH573 supply reading for src/core/battery.c: internal VBAT channel, -12 dB,
// 16 conversions averaged, the ADC off again afterwards. Same formula as the
// pin test (tests/hw/ch573/test_ch573/ch573_hal.c adcMv). Call it while the
// radio is quiet; it takes well under 1 ms.
// Built only for the CH573 app (MK_CHIP_CH573 from its Makefile; that app
// does not exist yet): the Arduino IDE and the CH32V203 build skip it.
#if defined(__riscv) && defined(MK_CHIP_CH573)
#include "CH57x_common.h"

int16_t hal_battery_vdd_mv(void) {
  int16_t calib;
  uint32_t sum = 0;
  uint8_t n;

  ADC_InterBATSampInit();
  calib = ADC_DataCalib_Rough();
  for (n = 0; n < 16; n++) {
    sum += ADC_ExcutSingleConver();
  }
  R8_ADC_CFG = 0; // ADC off
  return (int16_t)(((int32_t)(sum / 16) + calib) * 1050 / 512 - 3150);
}
#endif
