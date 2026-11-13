// CH573 entry point (SAYO_2B3): the sequence in core/app.c plus the wireless
// extras in core/bt_app.c. Built only by Makefile.ch573 (MK_CHIP_CH573).
#if defined(__riscv) && defined(MK_CHIP_CH573)
#include "hal/hal.h"
#include "core/app.h"
#include "core/bt_app.h"
#include "core/input.h"
#include "core/protocol.h"

extern const char firmwareId[];

int main(void) {
  app_setup();
  (void)*(volatile const char *)firmwareId; // keeps the ID string in the image
  bt_setup(hal_ms32());
  while (1) {
#if BTLED_DEMO
    // LED check: the keys set LED states (bt_app.h), only BOOT and the other
    // config commands run; nothing is typed
    now16 = hal_ms();
    if (protocol_poll() == PROTO_EVT_BOOT) {
      hal_enter_bootloader();
    }
#else
    app_loop();
#endif
    if (bt_task(hal_ms32(), hal_switches(), kb_ready())) {
      hal_deep_sleep();
    }
  }
}

#endif
