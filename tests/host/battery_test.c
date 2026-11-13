// Host check of src/core/battery.c (tests/host/run.py check_battery).
// battery_test curve        -> "<mv> <percent>" for 2900..4300 mV, 10 mV apart
// battery_test <vdd> ...    -> per VDD reading: "<mv> <percent> <low> <critical>"
// Built with -DBOARD_HEADER naming a header that sets BOARD_BATTERY.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "core/battery.h"

int main(int argc, char **argv) {
  if (argc > 1 && strcmp(argv[1], "curve") == 0) {
    for (unsigned v = 2900; v <= 4300; v += 10)
      printf("%u %u\n", v, battery_percent_of((uint16_t)v));
    return 0;
  }
  for (int i = 1; i < argc; i++) {
    battery_sample((int16_t)atoi(argv[i]));
    printf("%u %u %u %u\n", battery_mv(), battery_percent(), battery_low(), battery_critical());
  }
  return 0;
}
