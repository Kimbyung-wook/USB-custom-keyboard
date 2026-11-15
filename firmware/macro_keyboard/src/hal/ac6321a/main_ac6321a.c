// AC6321A entry (JUZH_3B4), UNVERIFIED on a chip. The SDK owns main():
// app_main starts the app "standard_keyboard" (CONFIG_APP_STANDARD_KEYBOARD,
// jl_build_ac6321a.sh); this file registers it in place of the SDK's
// app_standard_keyboard.c. On start it brings up USB and the BLE stack and
// runs the keyboard sequence (core/app.c + core/bt_app.c) in its own RTOS
// task; the app's event handler passes the BT events to ble_ac6321a.c.
#if defined(MK_CHIP_AC6321A)
#include "system/app_core.h"
#include "system/includes.h"
#include "app_config.h"
#include "app_action.h"
#include "os/os_api.h"
#include "btstack/btstack_task.h"
#include "btstack/bluetooth.h"
#include "btstack/avctp_user.h"
#include "bt_common.h"
#include "app_comm_bt.h"
#include "../hal.h"
#include "../../core/app.h"
#include "../../core/bt_app.h"
#include "../../core/macro.h"

extern const char firmwareId[];

// Our task: below the BT tasks (btstack 3, btctrler 4), as app_core (1).
// TODO(SDK): stack size (words) once the build shows the core's stack use
#define MK_TASK_PRIO 1
#define MK_TASK_STACK 1024

static volatile uint8_t mkActive = 1;

static void mkTask(void *p) {
  (void)p;
  app_setup();
  (void)*(volatile const char *)firmwareId; // keeps the ID string in the image
  bt_setup(hal_ms32(), hal_switches());
  while (1) {
    if (hal_uboot_request) {
      hal_enter_bootloader(); // factory 03 EF EF (usb_ac6321a.c)
    }
#if BOARD_BLE
    hal_ble_poll();
#endif
    app_loop();
    if (bt_task(hal_ms32(), hal_switches(), hal_usb_ready())) {
      hal_deep_sleep();
    }
    hal_idle();
    // Give the other tasks their turn (10 ms tick) unless a macro is typing
    // and the next report can go now. TODO(chip): typing speed over BLE
    if (!(macro_busy() && !kb_busy())) {
      os_time_dly(1);
    }
  }
}

static void mkStart(void) {
  clk_set("sys", BT_NORMAL_HZ);
  hal_init();
  hal_usb_start();
#if BOARD_BLE
  hal_ble_init();
#endif
  os_task_create(mkTask, NULL, MK_TASK_PRIO, MK_TASK_STACK, 0, "mk");
}

static int mkStateMachine(struct application *app, enum app_state state, struct intent *it) {
  (void)app;
  if (state == APP_STA_START && it && it->action == ACTION_STANDARD_KEYBOARD) {
    mkStart();
  }
  return 0;
}

#if BOARD_BLE
static void btConStatus(struct bt_event *bt) {
  if (bt->event == BT_STATUS_INIT_OK) {
    hal_ble_on_init_ok();
  } else {
    bt_comm_ble_status_event_handler(bt);
  }
}
#endif

static int mkEventHandler(struct application *app, struct sys_event *event) {
  (void)app;
  switch (event->type) {
#if BOARD_BLE
  case SYS_BT_EVENT:
    if ((u32)event->arg == SYS_BT_EVENT_TYPE_CON_STATUS) {
      btConStatus(&event->u.bt);
    } else if ((u32)event->arg == SYS_BT_EVENT_TYPE_HCI_STATUS) {
      bt_comm_ble_hci_event_handler(&event->u.bt);
    } else if ((u32)event->arg == SYS_BT_EVENT_BLE_STATUS) {
      hal_ble_on_status(event->u.bt.event);
    }
    return 0;
#endif
  default:
    return FALSE;
  }
}

// Low power: the SDK may sleep only when every target is idle. Our loop polls
// the keys, so never idle for now. TODO(battery): idle between key scans
static u8 mkIdleQuery(void) { return !mkActive; }

REGISTER_LP_TARGET(mk_lp_target) = {
    .name = "mk",
    .is_idle = mkIdleQuery,
};

static const struct application_operation mkOps = {
    .state_machine = mkStateMachine,
    .event_handler = mkEventHandler,
};

REGISTER_APPLICATION(app_mk) = {
    .name = "standard_keyboard",
    .action = ACTION_STANDARD_KEYBOARD,
    .ops = &mkOps,
    .state = APP_STA_DESTROY,
};

#endif
