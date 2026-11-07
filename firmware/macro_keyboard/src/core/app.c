// Firmware main sequence: init, then one pass of every task per loop
#include "../hal/hal.h"
#include "app.h"
#include "input.h"
#include "led.h"
#include "lock.h"
#include "macro.h"
#include "protocol.h"
#include "storage.h"

static void enterBootloader(void) {
  led_boot();
  hal_enter_bootloader();
}

void app_setup(void) {
  hal_init();
  storage_init();
  lock_init();
  input_init();
  hal_usb_start();
  now16 = hal_ms();
  led_task();
}

void app_loop(void) {
  uint8_t event;

  now16 = hal_ms();
  event = protocol_poll();

  if (event == PROTO_EVT_BOOT) {
    // Give the host a moment to collect the OK response
    uint16_t start = hal_ms();
    while (vendor_in_busy() && (uint16_t)(hal_ms() - start) < 100)
      ;
    enterBootloader();
  } else if (event == PROTO_EVT_SAVED) {
    led_saved();
  }

  input_task();
  lock_task();
  macro_task();
  led_task();
}
