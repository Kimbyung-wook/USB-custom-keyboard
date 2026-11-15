// BLE HID keyboard of the AC6321A app (JUZH_3B4), UNVERIFIED on a chip.
// Built on the SDK's HOGP profile (apps/hid/modules/bt/ble_hogp.c, compiled
// as it is): one bonded host (stage 1, as SAYO_2B3). The link policy (when to
// advertise, pairing, timeout, USB) is chip-free: src/core/bt_app.c.
// main_ac6321a.c passes the SDK's BT events here (hal_ble_on_*).
// TODO(SDK): these calls run in our task (mk), the SDK's own app makes them
// from app_core; check that ble_hid_data_send / ble_module_enable are safe so.
#if defined(MK_CHIP_AC6321A)
#include "system/includes.h"
#include "app_config.h"
#include "btstack/btstack_task.h"
#include "btstack/bluetooth.h"
#include "le_common.h"
#include "gatt_common/le_gatt_common.h"
#include "bt_common.h"
#include "app_comm_bt.h"
#include "user_cfg_id.h"
#include "../hal.h"
#include "../../core/bt_app.h"

#if BOARD_BLE

extern void ble_module_enable(u8 en);
extern int ble_hid_data_send(u8 report_id, u8 *data, u16 len);
extern int ble_hid_is_connected(void);
extern void le_hogp_set_output_callback(void *cb);
extern void le_hogp_set_pair_allow(void);
extern void le_hogp_set_reconnect_adv_cfg(u8 adv_type, u32 adv_timeout);
extern void get_random_number(u8 *ptr, u8 len);
extern void bt_pll_para(u32 osc, u32 sys, u8 low_power, u8 xosc);

#define RPT_ID 1  // keyboard input / LED output (report map below)
#define RPT_LEN 8 // boot keyboard layout

// Keyboard only (the SDK example's first collection): Report ID 1, input 8 B,
// output 1 B (LEDs). The SDK's GATT table has the report handles for IDs 1-6.
static const u8 reportMap[] = {
    0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x85, RPT_ID,
    0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02,
    0x95, 0x01, 0x75, 0x08, 0x81, 0x01,
    0x95, 0x05, 0x75, 0x01, 0x05, 0x08, 0x19, 0x01, 0x29, 0x05, 0x91, 0x02,
    0x95, 0x01, 0x75, 0x03, 0x91, 0x01,
    0x95, 0x06, 0x75, 0x08, 0x15, 0x00, 0x26, 0xFF, 0x00, 0x05, 0x07, 0x19, 0x00, 0x2A, 0xFF, 0x00, 0x81, 0x00,
    0xC0};

static const ble_init_cfg_t bleCfg = {
    .same_address = 0,
    .appearance = BLE_APPEARANCE_HID_KEYBOARD,
    .report_map_size = sizeof(reportMap),
    .report_map = reportMap,
};

// The SDK's bond record (ble_hogp.c struct pair_info_t, VM item
// CFG_BLE_BONDING_REMOTE_INFO without multi_dev_adv_flag): copied here only
// to read pair_flag. TODO(SDK): keep in step with ble_hogp.c
struct mk_pair_info {
  u16 head_tag;
  u8 pair_flag : 2;
  u8 direct_adv_cnt : 6;
  u8 direct_adv_flag : 1;
  u8 peer_address_info[7];
  u16 tail_tag;
};
#define BOND_HEAD 0xB35C
#define BOND_TAIL 0x5CB3

static volatile uint8_t stackOk;  // BT_STATUS_INIT_OK seen
static volatile uint8_t linkSt;   // HAL_BLE_*
static uint8_t advWanted, advPair, enabled;
static volatile uint8_t capsLock;
static uint8_t pending;
static uint8_t report[RPT_LEN];

static void ledOut(u8 *buf, u16 len) {
  if (len) {
    capsLock = buf[len - 1]; // the value only (the report ID is the handle)
  }
}

static void moduleEnable(uint8_t en) {
  enabled = en;
  ble_module_enable(en);
}

// Advertising in the wanted mode: pairing = bond cleared, anyone may connect;
// else the SDK's reconnect advertising to the bonded host (undirected ADV_IND
// as the SDK example's first power-on; TODO(SDK): directed)
static void applyAdv(void) {
  if (!stackOk) {
    return; // hal_ble_on_init_ok() applies it
  }
  if (enabled) {
    moduleEnable(0);
  }
  if (!advWanted) {
    return;
  }
  if (advPair) {
    le_hogp_set_pair_allow();
  }
  le_hogp_set_reconnect_adv_cfg(ADV_IND, 5000);
  moduleEnable(1);
}

void hal_ble_init(void) {
  u8 addr[6];
  btstack_ble_start_before_init(&bleCfg, 0);
  if (syscfg_read(CFG_BLE_ADDRESS_BEGIN, addr, 6) != 6) {
    get_random_number(addr, 6);
    syscfg_write(CFG_BLE_ADDRESS_BEGIN, addr, 6);
  }
  le_controller_set_mac(addr);
  bt_pll_para(TCFG_CLOCK_OSC_HZ, clk_get("sys"), 0, 0);
  btstack_init(); // BT_STATUS_INIT_OK follows (hal_ble_on_init_ok)
}

// BT_STATUS_INIT_OK (main_ac6321a.c event_handler, app_core task)
void hal_ble_on_init_ok(void) {
  le_hogp_set_output_callback(ledOut);
  le_hogp_set_reconnect_adv_cfg(ADV_IND, 5000);
  btstack_ble_start_after_init(0); // starts advertising
  enabled = 1;
  stackOk = 1;
  applyAdv();
}

// SYS_BT_EVENT_BLE_STATUS: ble_state_e or BLE_PRIV_*
void hal_ble_on_status(uint8_t st) {
  switch (st) {
  case BLE_ST_ADV:
    linkSt = HAL_BLE_ADV;
    break;
  case BLE_ST_CONNECT:
    linkSt = HAL_BLE_LINK;
    break;
  case BLE_PRIV_PAIR_ENCRYPTION_CHANGE:
    if (ble_hid_is_connected()) {
      linkSt = HAL_BLE_READY;
      advWanted = 0; // the host is here: no advertising after it leaves (bt_app.c decides)
    }
    break;
  case BLE_ST_IDLE:
  case BLE_ST_DISCONN:
  case BLE_ST_CONNECT_FAIL:
  case BLE_ST_INIT_OK:
    linkSt = HAL_BLE_IDLE;
    pending = 0;
    break;
  default:
    break;
  }
}

void hal_ble_start(uint8_t pair) {
  advWanted = 1;
  advPair = pair;
  applyAdv();
}

void hal_ble_stop(void) {
  advWanted = 0;
  pending = 0;
  if (stackOk && enabled) {
    moduleEnable(0); // drops the link and stops advertising
  }
}

uint8_t hal_ble_state(void) {
  if (linkSt >= HAL_BLE_LINK && !ble_hid_is_connected()) {
    return HAL_BLE_IDLE;
  }
  return linkSt;
}

uint8_t hal_ble_bonded(void) {
  struct mk_pair_info p;
  if (syscfg_read(CFG_BLE_BONDING_REMOTE_INFO, (u8 *)&p, sizeof(p)) != sizeof(p)) {
    return 0;
  }
  return p.head_tag == BOND_HEAD && p.tail_tag == BOND_TAIL && p.pair_flag;
}

// Before the stack is up (bt_setup at power-on) the record is cleared
// directly; le_hogp_set_pair_allow() also clears the SDK's copy in RAM
void hal_ble_forget(void) {
  struct mk_pair_info p;
  if (stackOk) {
    le_hogp_set_pair_allow();
    return;
  }
  memset(&p, 0, sizeof(p));
  p.head_tag = BOND_HEAD;
  p.tail_tag = BOND_TAIL;
  syscfg_write(CFG_BLE_BONDING_REMOTE_INFO, (u8 *)&p, sizeof(p));
}

static void flush(void) {
  if (pending && hal_ble_state() == HAL_BLE_READY && ble_hid_data_send(RPT_ID, report, RPT_LEN) == 0) {
    pending = 0;
  }
}

void hal_ble_poll(void) { flush(); }

uint8_t hal_ble_ready(void) { return hal_ble_state() == HAL_BLE_READY; }
uint8_t hal_ble_busy(void) { return pending; }
void hal_ble_cancel(void) { pending = 0; }
uint8_t hal_ble_caps_lock(void) { return capsLock & 0x02; }

uint8_t hal_ble_send(uint8_t modifiers, uint8_t keycode) {
  if (pending) {
    return 0;
  }
  memset(report, 0, sizeof(report));
  report[0] = modifiers;
  report[2] = keycode;
  pending = 1;
  flush();
  return 1;
}

#endif // BOARD_BLE
#endif
