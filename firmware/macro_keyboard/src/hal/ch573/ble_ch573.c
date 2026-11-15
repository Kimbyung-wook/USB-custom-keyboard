// BLE HID keyboard (HID over GATT) of the CH573 app, SAYO_2B3 stage 1: one
// bonded host. Built on the WCH BLE library and the SDK's HID_Keyboard
// profile files (EXAM/BLE: HAL/MCU.c, RTC.c; Profile/hiddev.c,
// hidkbdservice.c, battservice.c, devinfoservice.c, scanparamservice.c),
// compiled from the SDK as they are (Makefile.ch573). This file replaces the
// example's APP/hidkbd.c. The link policy (when to advertise, pairing,
// timeout, USB) is chip-free: src/core/bt_app.c.
//
// Compiled with the SDK's BLE include path first (Makefile.ch573 BLE_INCS):
// "CONFIG.h" is the SDK's HAL/include/config.h, not src/config.h.
#if defined(__riscv) && defined(MK_CHIP_CH573)
#include "CONFIG.h"
#include "HAL.h"
#include "battservice.h"
#include "devinfoservice.h"
#include "hiddev.h"
#include "hidkbdservice.h"
#include "hal/hal.h"
#include "core/battery.h"
#include "core/bt_app.h"

#if BOARD_BLE

__attribute__((aligned(4))) uint32_t MEM_BUF[BLE_MEMHEAP_SIZE / 4]; // BLE heap (config.h)

#define RPT_LEN 8 // boot keyboard input report

// Connection parameters asked for once a host is connected (1.25 ms units):
// 7.5 .. 15 ms, no latency, 5 s supervision timeout (10 ms units)
#define CONN_MIN 6
#define CONN_MAX 12
#define CONN_LATENCY 0
#define CONN_TIMEOUT 500
#define PARAM_UPDATE_DELAY 1600 // 1 s (625 us units), as hosts settle first

// Advertising intervals (625 us units): pairing 30..50 ms, reconnecting 20..30 ms
#define ADV_PAIR_MIN 48
#define ADV_PAIR_MAX 80
#define ADV_RECONN_MIN 32
#define ADV_RECONN_MAX 48

// A link that is not encrypted after this long is dropped (a host that
// connected but cannot pair, e.g. while reconnecting, keeps nobody waiting)
#define SECURE_TIMEOUT_MS 30000u

#define EVT_PARAM_UPDATE 0x0001

static uint8_t taskId = INVALID_TASK_ID;
static gapRole_States_t gapState = GAPROLE_INIT;
static uint16_t connHandle = GAP_CONNHANDLE_INIT;
static uint32_t linkAt;
static uint8_t advWanted, advPair, capsLock;
static uint8_t pending; // a report waiting for a free BLE buffer
static uint8_t report[RPT_LEN];

// The name (BOARD_NAME, 8 characters at most) is copied in by hal_ble_init
#define NAME_LEN (sizeof(BOARD_NAME) - 1)
static uint8_t scanRspData[] = {
    1 + NAME_LEN, GAP_ADTYPE_LOCAL_NAME_COMPLETE,
    0, 0, 0, 0, 0, 0, 0, 0,
    5, GAP_ADTYPE_SLAVE_CONN_INTERVAL_RANGE,
    LO_UINT16(CONN_MIN), HI_UINT16(CONN_MIN), LO_UINT16(CONN_MAX), HI_UINT16(CONN_MAX),
    5, GAP_ADTYPE_16BIT_MORE,
    LO_UINT16(HID_SERV_UUID), HI_UINT16(HID_SERV_UUID), LO_UINT16(BATT_SERV_UUID), HI_UINT16(BATT_SERV_UUID),
    2, GAP_ADTYPE_POWER_LEVEL, 0}; // 0 dBm
_Static_assert(NAME_LEN == 8, "scanRspData holds 8 name characters");

static uint8_t advertData[] = {
    2, GAP_ADTYPE_FLAGS, GAP_ADTYPE_FLAGS_GENERAL | GAP_ADTYPE_FLAGS_BREDR_NOT_SUPPORTED,
    3, GAP_ADTYPE_APPEARANCE, LO_UINT16(GAP_APPEARE_HID_KEYBOARD), HI_UINT16(GAP_APPEARE_HID_KEYBOARD)};

static uint8_t deviceName[GAP_DEVICE_NAME_LEN] = BOARD_NAME;

static hidDevCfg_t hidCfg = {0, HID_FEATURE_FLAGS}; // no idle timeout: bt_app.c decides

static void setAdvEnabled(uint8_t on) { GAPRole_SetParameter(GAPROLE_ADVERT_ENABLED, sizeof on, &on); }

// Advertising with the parameters of the current mode (advPair)
static void advStart(void) {
  uint8_t policy = GAP_FILTER_POLICY_ALL;
  uint8_t mode = advPair ? GAPBOND_PAIRING_MODE_WAIT_FOR_REQ : GAPBOND_PAIRING_MODE_NO_PAIRING;
  GAPBondMgr_SetParameter(GAPBOND_PERI_PAIRING_MODE, sizeof mode, &mode);
  GAPRole_SetParameter(GAPROLE_ADV_FILTER_POLICY, sizeof policy, &policy);
  GAP_SetParamValue(TGAP_DISC_ADV_INT_MIN, advPair ? ADV_PAIR_MIN : ADV_RECONN_MIN);
  GAP_SetParamValue(TGAP_DISC_ADV_INT_MAX, advPair ? ADV_PAIR_MAX : ADV_RECONN_MAX);
  setAdvEnabled(TRUE);
}

static void stateCB(gapRole_States_t newState, gapRoleEvent_t *pEvent) {
  if (newState == GAPROLE_CONNECTED_ADV) {
    newState = GAPROLE_CONNECTED; // advertising goes on only until bt_app.c stops it
  }
  switch (newState) {
  case GAPROLE_CONNECTED:
    if (gapState == GAPROLE_CONNECTED) {
      break;
    }
    connHandle = ((gapEstLinkReqEvent_t *)pEvent)->connectionHandle;
    linkAt = hal_ms32();
    pending = 0;
    tmos_start_task(taskId, EVT_PARAM_UPDATE, PARAM_UPDATE_DELAY);
    break;
  case GAPROLE_WAITING: // advertising ended or link gone
    connHandle = GAP_CONNHANDLE_INIT;
    capsLock = 0;
    pending = 0;
    if (advWanted) {
      advStart(); // also after a mode change (hal_ble_start while advertising)
    }
    break;
  default:
    break;
  }
  gapState = newState;
}

// Reports from the host (LED output report) and reads of ours
static uint8_t reportCB(uint8_t id, uint8_t type, uint16_t uuid, uint8_t oper, uint16_t *pLen,
                        uint8_t *pData) {
  uint8_t status = SUCCESS;
  if (oper == HID_DEV_OPER_WRITE) {
    if (uuid == REPORT_UUID && type == HID_REPORT_TYPE_OUTPUT) {
      if (*pLen != 1) {
        return ATT_ERR_INVALID_VALUE_SIZE;
      }
      capsLock = pData[0]; // 0x02 = Caps Lock
    }
    status = Hid_SetParameter(id, type, uuid, *pLen, pData);
  } else if (oper == HID_DEV_OPER_READ) {
    status = Hid_GetParameter(id, type, uuid, pLen, pData);
  }
  return status;
}

static void evtCB(uint8_t evt) { (void)evt; }

static hidDevCB_t hidCBs = {reportCB, evtCB, NULL, stateCB};

static uint16_t processEvent(uint8_t task, uint16_t events) {
  (void)task;
  if (events & SYS_EVENT_MSG) {
    uint8_t *msg = tmos_msg_receive(taskId);
    if (msg) {
      tmos_msg_deallocate(msg);
    }
    return events ^ SYS_EVENT_MSG;
  }
  if (events & EVT_PARAM_UPDATE) {
    if (gapState == GAPROLE_CONNECTED) {
      GAPRole_PeripheralConnParamUpdateReq(connHandle, CONN_MIN, CONN_MAX, CONN_LATENCY, CONN_TIMEOUT,
                                           taskId);
    }
    return events ^ EVT_PARAM_UPDATE;
  }
  return 0;
}

// Battery service: the level is src/core/battery.c's (the library's own
// "measurement" is a fixed number between the limits below)
static uint8_t battLevel(uint16_t adc) {
  (void)adc;
  return battery_percent();
}

void hal_ble_init(void) {
  CH57X_BLEInit();
  HAL_Init(); // RTC (TMOS clock), LSI + RF calibration every BLE_CALIBRATION_PERIOD
  GAPRole_PeripheralInit();
  HidDev_Init();
  taskId = TMOS_ProcessEventRegister(processEvent);

  setAdvEnabled(FALSE); // bt_app.c starts it
  tmos_memcpy(scanRspData + 2, BOARD_NAME, NAME_LEN);
  GAPRole_SetParameter(GAPROLE_ADVERT_DATA, sizeof advertData, advertData);
  GAPRole_SetParameter(GAPROLE_SCAN_RSP_DATA, sizeof scanRspData, scanRspData);
  GGS_SetParameter(GGS_DEVICE_NAME_ATT, GAP_DEVICE_NAME_LEN, deviceName);
  {
    uint32_t passkey = 0;
    uint8_t mode = GAPBOND_PAIRING_MODE_NO_PAIRING, mitm = FALSE;
    uint8_t ioCap = GAPBOND_IO_CAP_NO_INPUT_NO_OUTPUT, bonding = TRUE;
    GAPBondMgr_SetParameter(GAPBOND_PERI_DEFAULT_PASSCODE, sizeof passkey, &passkey);
    GAPBondMgr_SetParameter(GAPBOND_PERI_PAIRING_MODE, sizeof mode, &mode);
    GAPBondMgr_SetParameter(GAPBOND_PERI_MITM_PROTECTION, sizeof mitm, &mitm);
    GAPBondMgr_SetParameter(GAPBOND_PERI_IO_CAPABILITIES, sizeof ioCap, &ioCap);
    GAPBondMgr_SetParameter(GAPBOND_PERI_BONDING_ENABLED, sizeof bonding, &bonding);
  }
  {
    uint8_t critical = 6; // %
    Batt_SetParameter(BATT_PARAM_CRITICAL_LEVEL, sizeof critical, &critical);
    Batt_Setup(0, 0, 0xFFFF, NULL, NULL, battLevel);
  }
  Hid_AddService();
  HidDev_Register(&hidCfg, &hidCBs);
}

void hal_ble_start(uint8_t pair) {
  advWanted = 1;
  advPair = pair;
  if (gapState == GAPROLE_ADVERTISING) {
    setAdvEnabled(FALSE); // stateCB (waiting) starts it again in the new mode
  } else if (gapState != GAPROLE_CONNECTED) {
    advStart();
  }
}

void hal_ble_stop(void) {
  advWanted = 0;
  pending = 0;
  if (gapState == GAPROLE_CONNECTED) {
    GAPRole_TerminateLink(connHandle);
  }
  setAdvEnabled(FALSE);
}

uint8_t hal_ble_state(void) {
  if (gapState == GAPROLE_CONNECTED) {
    return linkDB_State(connHandle, LINK_ENCRYPTED) ? HAL_BLE_READY : HAL_BLE_LINK;
  }
  return gapState == GAPROLE_ADVERTISING ? HAL_BLE_ADV : HAL_BLE_IDLE;
}

uint8_t hal_ble_bonded(void) {
  uint8_t n = 0;
  GAPBondMgr_GetParameter(GAPBOND_BOND_COUNT, &n);
  return n != 0;
}

void hal_ble_forget(void) { GAPBondMgr_SetParameter(GAPBOND_ERASE_ALLBONDS, 0, NULL); }

static void flush(void) {
  if (pending && hal_ble_state() == HAL_BLE_READY &&
      HidDev_Report(HID_RPT_ID_KEY_IN, HID_REPORT_TYPE_INPUT, RPT_LEN, report) == SUCCESS) {
    pending = 0;
  }
}

void hal_ble_poll(void) {
  TMOS_SystemProcess();
  if (gapState == GAPROLE_CONNECTED) {
    if (linkDB_State(connHandle, LINK_ENCRYPTED)) {
      advWanted = 0; // the host is here: no advertising after it leaves (bt_app.c decides)
    } else if (hal_ms32() - linkAt >= SECURE_TIMEOUT_MS) {
      linkAt = hal_ms32();
      GAPRole_TerminateLink(connHandle);
    }
  }
  flush();
}

uint8_t hal_ble_ready(void) { return hal_ble_state() == HAL_BLE_READY; }
uint8_t hal_ble_busy(void) { return pending; }
void hal_ble_cancel(void) { pending = 0; }
uint8_t hal_ble_caps_lock(void) { return capsLock & 0x02; }

uint8_t hal_ble_send(uint8_t modifiers, uint8_t keycode) {
  if (pending) {
    return 0;
  }
  report[0] = modifiers;
  report[1] = 0;
  report[2] = keycode;
  report[3] = report[4] = report[5] = report[6] = report[7] = 0;
  pending = 1;
  flush();
  return 1;
}

#endif // BOARD_BLE
#endif
