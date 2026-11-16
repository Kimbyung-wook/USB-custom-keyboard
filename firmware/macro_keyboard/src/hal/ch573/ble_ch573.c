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
#include "core/ble_addr.h"
#include "core/bt_app.h"
#include "core/bt_led.h"

#if BOARD_BLE

__attribute__((aligned(4))) uint32_t MEM_BUF[BLE_MEMHEAP_SIZE / 4]; // BLE heap (config.h)

#define RPT_LEN 8 // boot keyboard input report

// ---- three BLE slots: one bonded host each, kept across power-off ----
// A Data-Flash page clear of the config blob (<= 0x7000) and the BLE bonding
// SNV (BLE_SNV_ADDR, 0x7E00). Each slot remembers its host's identity address
// so reconnecting can whitelist that host alone (only the current slot's host
// may connect). The bond keys stay in the stack's SNV. Writes are rare (a new
// host, or a slot change), so one erase+write of the page is fine.
#define BLE_SLOT_COUNT 3
#define SLOT_STORE_ADDR 0x7000
#define SLOT_MAGIC 0x3254 // bumped from 0x3253 when gen[] was added

// ---- BLE bonding store (SNV) layout in Data-Flash ----
// The stack keeps one bond per SNV block, and the SDK leaves SNVNum at its
// default of 1, so only one host could stay bonded and a second pairing evicted
// the first. We enlarge it (SNVNum below, applied in __wrap_CH57X_BLEInit) so
// all three slots keep their bond, and place the region at the very top of
// Data-Flash, clear of the config blob (0 .. BLOB_CAPACITY) and the one-page
// slot store at SLOT_STORE_ADDR. (The SDK default sat at 0x7E00; moving it
// abandons any bond stored there, so hosts re-pair once after this update.)
#define BLE_SNV_BLOCK EEPROM_PAGE_SIZE // 256 B, one Data-Flash page per block
#define BLE_SNV_NUM 4                  // up to 4 bonds kept (3 slots + margin)
#define BLE_SNV_OFFSET (DATA_FLASH_SIZE - BLE_SNV_BLOCK * BLE_SNV_NUM)
_Static_assert(BLOB_CAPACITY <= SLOT_STORE_ADDR, "config blob overlaps the slot store");
_Static_assert(SLOT_STORE_ADDR + EEPROM_PAGE_SIZE <= BLE_SNV_OFFSET,
               "slot store page overlaps the BLE SNV region");
_Static_assert(BLE_SNV_OFFSET + BLE_SNV_BLOCK * BLE_SNV_NUM <= DATA_FLASH_SIZE,
               "BLE SNV region runs past the end of Data-Flash");
_Static_assert(BLE_SNV_OFFSET % EEPROM_PAGE_SIZE == 0, "BLE SNV region not page aligned");
typedef struct {
  uint8_t valid;
  uint8_t addrType;
  uint8_t addr[B_ADDR_LEN];
} slotHost_t;
typedef struct {
  uint16_t magic;
  uint8_t cur;
  uint8_t rsv;
  slotHost_t host[BLE_SLOT_COUNT];
  uint8_t gen[BLE_SLOT_COUNT]; // re-pair generation: bumped to make a new address
} slotStore_t;
static slotStore_t slots;
static uint8_t pendValid, pendAddrType, pendAddr[B_ADDR_LEN]; // connected host, until stored
static uint8_t hostDirty; // a host was recorded in RAM, flush to flash on disconnect

static void slotLoad(void) {
  EEPROM_READ(SLOT_STORE_ADDR, &slots, sizeof slots);
  if (slots.magic != SLOT_MAGIC || slots.cur >= BLE_SLOT_COUNT) {
    tmos_memset(&slots, 0, sizeof slots);
    slots.magic = SLOT_MAGIC;
  }
}
static void slotSave(void) {
  uint32_t irq;
  SYS_DisableAllIrq(&irq);
  EEPROM_ERASE(SLOT_STORE_ADDR, EEPROM_PAGE_SIZE);
  EEPROM_WRITE(SLOT_STORE_ADDR, &slots, sizeof slots);
  SYS_RecoverIrq(irq);
}
static uint8_t addrEq(const uint8_t *a, const uint8_t *b) {
  uint8_t i;
  for (i = 0; i < B_ADDR_LEN; i++) {
    if (a[i] != b[i]) {
      return 0;
    }
  }
  return 1;
}

// A host has a fixed (identity) address we can whitelist and match against a
// bond when it is public, or a static random address (top two bits of the MSB
// are 0b11). A resolvable or non-resolvable private address (privacy hosts such
// as phones) changes over time, so it can be neither whitelisted nor matched.
static uint8_t hostHasFixedAddr(const slotHost_t *h) {
  return h->addrType == ADDRTYPE_PUBLIC || (h->addr[B_ADDR_LEN - 1] & 0xC0) == 0xC0;
}

// ---- a different keyboard address per slot ----
// Each slot advertises its own address so a host bonded on one slot cannot see
// or reconnect while another slot is active. The address is a STATIC RANDOM one
// (ble_addr.h: top two bits of the MSB are 1) and is applied with
// GAP_ConfigDeviceAddr(ADDRTYPE_STATIC, addr) in GAPROLE_STARTED, exactly as the
// SDK example (hidkbd.c) does -- that sets the bytes AND the advertised
// own-address type to static together. (The earlier attempt put the bytes in the
// BLE_MAC path, which advertised them as a PUBLIC address; a static-random byte
// pattern sent as public broke pairing and input. An even earlier attempt called
// GAP_ConfigDeviceAddr at the wrong time -- while advertising/connected, which
// returns bleIncorrectMode -- not in GAPROLE_STARTED.) The address is fixed at
// start, so switching slots resets the chip (hal_reset) and STARTED runs again
// for the new slot. SLOT_ADDR=0 (A/B test build) skips all of this and keeps the
// chip's public address.
#if SLOT_ADDR
static void applySlotAddr(void) {
  uint8_t base[B_ADDR_LEN], addr[B_ADDR_LEN];
  GAPRole_GetParameter(GAPROLE_BD_ADDR, base); // the chip (public) address
  ble_slot_addr(base, slots.cur, slots.gen[slots.cur], addr);
  GAP_ConfigDeviceAddr(ADDRTYPE_STATIC, addr);
}
#endif

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
#define EVT_SECURITY_REQ 0x0002
// After a bonded reconnect, ask the host to start encryption with the stored
// keys. Some hosts (seen with Windows here) do not re-encrypt a bonded link on
// their own, so the link stays unencrypted, hidDevConnSecure never turns TRUE
// and HID reports are blocked (battery notifications, which skip that check,
// still work). A short delay lets the link settle first (625 us units).
#define SECURITY_REQ_DELAY 480 // 0.3 s

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
  // Pairing mode by case (below): a fixed-address host reconnect stays
  // WAIT_FOR_REQ with a whitelist of that one host (so a new device still cannot
  // reach this slot, and the bonded host re-encrypts); a privacy host reconnect
  // has no whitelist, so it uses NO_PAIRING to keep new devices from pairing in;
  // first pairing / re-pair (advPair) is WAIT_FOR_REQ with filter ALL. The
  // bonded host re-encrypts from its stored keys regardless of the pairing mode,
  // prompted by the security request in stateCB.
  uint8_t mode = GAPBOND_PAIRING_MODE_WAIT_FOR_REQ;
  slotHost_t *h = &slots.host[slots.cur];
  if (!advPair && h->valid) {
    if (hostHasFixedAddr(h)) {
      // reconnecting to a fixed-address host: only it may scan and connect
      LL_ClearWhiteList();
      LL_AddWhiteListDevice(h->addrType, h->addr);
      policy = GAP_FILTER_POLICY_WHITE;
    } else {
      // The host uses a resolvable private address that changes, so it cannot be
      // whitelisted. Accept any connection (filter ALL) but allow no new pairing,
      // since this slot is already bonded: the bonded host re-encrypts with its
      // stored keys (security request in stateCB), a new device cannot pair in,
      // and a known other-slot host that grabs this open advertising is dropped
      // on connect (stateCB). This is the one case where a second privacy host
      // bonded on another slot cannot be told apart by address.
      mode = GAPBOND_PAIRING_MODE_NO_PAIRING;
    }
  }
  GAPBondMgr_SetParameter(GAPBOND_PERI_PAIRING_MODE, sizeof mode, &mode);
  GAPRole_SetParameter(GAPROLE_ADV_FILTER_POLICY, sizeof policy, &policy);
  GAP_SetParamValue(TGAP_DISC_ADV_INT_MIN, advPair ? ADV_PAIR_MIN : ADV_RECONN_MIN);
  GAP_SetParamValue(TGAP_DISC_ADV_INT_MAX, advPair ? ADV_PAIR_MAX : ADV_RECONN_MAX);
  setAdvEnabled(TRUE);
}

#if BLE_DIAG
static volatile uint8_t diagSendRc = 0xFF; // last real HID key report result (flush)
static volatile uint8_t diagEncrReached;   // link became encrypted since this connect
static volatile uint8_t diagSecReqSent;    // a peripheral security request was sent since connect
#endif

// True if the just-connected device is a different slot's known fixed-address
// host. A privacy-host slot advertises to anyone (filter ALL), so a bonded host
// from another slot can grab it; drop such a connection so the slot stays for
// its own host. (Only fixed addresses can be told apart this way; a second
// privacy host cannot.)
static uint8_t connFromOtherSlot(void) {
  uint8_t s;
  for (s = 0; s < BLE_SLOT_COUNT; s++) {
    if (s != slots.cur && slots.host[s].valid && slots.host[s].addrType == pendAddrType &&
        addrEq(slots.host[s].addr, pendAddr)) {
      return 1;
    }
  }
  return 0;
}

static void stateCB(gapRole_States_t newState, gapRoleEvent_t *pEvent) {
  if (newState == GAPROLE_CONNECTED_ADV) {
    newState = GAPROLE_CONNECTED;
  }
  switch (newState) {
#if SLOT_ADDR
  case GAPROLE_STARTED:
    applySlotAddr(); // set this slot's static random address (before advertising)
    break;
#endif
  case GAPROLE_CONNECTED:
    if (gapState == GAPROLE_CONNECTED) {
      break;
    }
    connHandle = ((gapEstLinkReqEvent_t *)pEvent)->connectionHandle;
    pendAddrType = ((gapEstLinkReqEvent_t *)pEvent)->devAddrType;
    tmos_memcpy(pendAddr, ((gapEstLinkReqEvent_t *)pEvent)->devAddr, B_ADDR_LEN);
    if (connFromOtherSlot()) {
      // another slot's host reached this slot's open advertising: drop it and
      // keep advertising for this slot's own host
      pendValid = 0;
      GAPRole_TerminateLink(connHandle);
      break;
    }
    pendValid = 1; // store it in the slot once the link is encrypted (bonded)
    linkAt = hal_ms32();
    pending = 0;
#if BLE_DIAG
    diagEncrReached = 0;
    diagSecReqSent = 0;
#endif
    tmos_start_task(taskId, EVT_PARAM_UPDATE, PARAM_UPDATE_DELAY);
    // Reconnecting to this slot's bonded host: prompt it to re-encrypt with the
    // stored keys (EVT_SECURITY_REQ below). Not on a first pairing (no bond yet):
    // the host drives that, and a request then would be redundant.
    if (slots.host[slots.cur].valid) {
      tmos_start_task(taskId, EVT_SECURITY_REQ, SECURITY_REQ_DELAY);
    }
    break;
  case GAPROLE_WAITING: // advertising ended or link gone
    connHandle = GAP_CONNHANDLE_INIT;
    capsLock = 0;
    pending = 0;
    if (hostDirty) {
      slotSave(); // the link is down: safe to write flash now (not mid-GATT)
      hostDirty = 0;
    }
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
  if (events & EVT_SECURITY_REQ) {
    // Still connected but not yet encrypted: ask the bonded host to start
    // encryption. If it already did (secure), skip. hidDevPairStateCB then
    // sets hidDevConnSecure on GAPBOND_PAIRING_STATE_COMPLETE and HID unblocks.
    if (gapState == GAPROLE_CONNECTED && !linkDB_State(connHandle, LINK_ENCRYPTED)) {
      GAPBondMgr_PeriSecurityReq(connHandle);
#if BLE_DIAG
      diagSecReqSent = 1;
#endif
    }
    return events ^ EVT_SECURITY_REQ;
  }
  return 0;
}

// Battery service: the level is src/core/battery.c's (the library's own
// "measurement" is a fixed number between the limits below)
static uint8_t battLevel(uint16_t adc) {
  (void)adc;
  return battery_percent();
}

// The SDK builds the BLE config in CH57X_BLEInit (EXAM/BLE/HAL/MCU.c) and never
// sets SNVNum, leaving it at the library default of 1 (one bond). We cannot edit
// the SDK, so the linker redirects CH57X_BLEInit to this wrapper
// (-Wl,--wrap=CH57X_BLEInit in Makefile.ch573): same setup as the SDK's, plus
// the SNV fields (block size, count, relocated address). The SDK's flash
// callbacks (Lib_*_Flash, still compiled from MCU.c) erase exactly the span the
// library writes, page-granular, so they already cover the larger region.
extern uint32_t Lib_Read_Flash(uint32_t addr, uint32_t num, uint32_t *pBuf);
extern uint32_t Lib_Write_Flash(uint32_t addr, uint32_t num, uint32_t *pBuf);

void __wrap_CH57X_BLEInit(void) {
  uint8_t i, mac[6];
  bleConfig_t cfg;
  SysTick_Config(SysTick_LOAD_RELOAD_Msk);
  PFIC_DisableIRQ(SysTick_IRQn);
  tmos_memset(&cfg, 0, sizeof cfg);
  cfg.MEMAddr = (uint32_t)MEM_BUF;
  cfg.MEMLen = (uint32_t)BLE_MEMHEAP_SIZE;
  cfg.BufMaxLen = (uint32_t)BLE_BUFF_MAX_LEN;
  cfg.BufNumber = (uint32_t)BLE_BUFF_NUM;
  cfg.TxNumEvent = (uint32_t)BLE_TX_NUM_EVENT;
  cfg.TxPower = (uint32_t)BLE_TX_POWER;
  cfg.SNVAddr = (uint32_t)BLE_SNV_OFFSET; // relocated multi-bond SNV (above)
  cfg.SNVBlock = BLE_SNV_BLOCK;
  cfg.SNVNum = BLE_SNV_NUM;
  cfg.readFlashCB = Lib_Read_Flash;
  cfg.writeFlashCB = Lib_Write_Flash;
#if (CLK_OSC32K)
  cfg.SelRTCClock = (uint32_t)CLK_OSC32K;
#endif
  cfg.ConnectNumber = (PERIPHERAL_MAX_CONNECTION & 3) | (CENTRAL_MAX_CONNECTION << 2);
  cfg.srandCB = SYS_GetSysTickCnt;
#if (defined(TEM_SAMPLE) && (TEM_SAMPLE == TRUE))
  cfg.tsCB = HAL_GetInterTempValue; // calibrate RF/RC on temperature change
#if (CLK_OSC32K)
  cfg.rcCB = Lib_Calibration_LSI;
#endif
#endif
  GetMACAddress(mac);
  for (i = 0; i < 6; i++) {
    cfg.MacAddr[i] = mac[i]; // the chip's public address
  }
  if (BLE_LibInit(&cfg) != SUCCESS) {
    while (1) { // library init failed (bad config): stop
    }
  }
}

// ---- drop slots whose bond the stack no longer has ----
// The stack has no "is this address bonded?" call, so read the bond records
// straight from NV (tmos_snv_read of each main record) and compare addresses.
#define BLE_BOND_SCAN 8 // bond record indices to check (well over BLE_SNV_NUM)
static uint8_t hostStillBonded(const slotHost_t *h) {
  uint8_t idx;
  gapBondRec_t rec;
  // Only a fixed address can be matched against a bond record; a privacy host's
  // stored address differs from the bond's identity, so leave such a slot alone.
  if (!hostHasFixedAddr(h)) {
    return 1;
  }
  for (idx = 0; idx < BLE_BOND_SCAN; idx++) {
    if (tmos_snv_read(mainRecordNvID(idx), sizeof rec, &rec) != SUCCESS) {
      continue; // no bond stored at this index
    }
    if (addrEq(h->addr, rec.publicAddr) || addrEq(h->addr, rec.reconnectAddr)) {
      return 1;
    }
  }
  return 0;
}

// At startup, forget any slot whose host has no matching bond left in NV (the
// stack can evict a bond when its limit is reached). Such a slot would keep
// advertising to a whitelist of one unreachable host and never connect or pair;
// clearing host.valid makes it advertise in pairing mode so a new host can bond.
static void reconcileSlots(void) {
  uint8_t s, changed = 0;
  for (s = 0; s < BLE_SLOT_COUNT; s++) {
    if (slots.host[s].valid && !hostStillBonded(&slots.host[s])) {
      slots.host[s].valid = 0;
      changed = 1;
    }
  }
  if (changed) {
    slotSave();
  }
}

void hal_ble_init(void) {
  slotLoad(); // the current slot and each slot's host, kept across power-off
  // The device address is the chip's (public) at init; with SLOT_ADDR the
  // stateCB sets this slot's static random address in GAPROLE_STARTED.
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
  // NV is live now: drop any slot whose bond the stack no longer holds, so it
  // advertises for a new pairing instead of being stuck on an unreachable host.
  reconcileSlots();
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

uint8_t hal_ble_bonded(void) { return slots.host[slots.cur].valid; }

uint8_t hal_ble_slot(void) { return slots.cur; }

void hal_ble_select_slot(uint8_t s) {
  if (s >= BLE_SLOT_COUNT || s == slots.cur) {
    return;
  }
  // The slot address is applied once in GAPROLE_STARTED, so a slot change means
  // re-init: save the slot and reset. After the reset STARTED runs applySlotAddr
  // for the new slot. hal_reset does not return.
  slots.cur = s;
  pendValid = 0;
  slotSave();
  hal_reset();
}

// Re-pair the current slot: forget its host (the other slots keep their bonds),
// bump its generation so it comes back with a brand-new address, then reset so
// the new address takes effect (it is fixed at CH57X_BLEInit). Does not return
// when it resets. The "no host" early return means a slot with nothing bonded
// is already fresh -- and it stops a reset loop when key 5 is held at power-on
// (after the first reset the host is cleared, so the next boot does nothing).
void hal_ble_forget(void) {
  slotHost_t *h = &slots.host[slots.cur];
  uint8_t a[1 + B_ADDR_LEN]; // ERASE_SINGLEBOND: address type then address
  pendValid = 0;
  if (!h->valid) {
    return;
  }
  a[0] = h->addrType;
  tmos_memcpy(a + 1, h->addr, B_ADDR_LEN);
  GAPBondMgr_SetParameter(GAPBOND_ERASE_SINGLEBOND, sizeof a, a);
  LL_RemoveWhiteListDevice(h->addrType, h->addr);
  h->valid = 0;
  slots.gen[slots.cur]++;
  slotSave();
  hal_reset();
}

// CMD_CLEAR_BONDS: erase every slot's pairing. Wipes all SNV bonds, the stored
// hosts and the whitelist, and bumps every slot's generation so the old
// addresses do not come back (not zeroed -- a host's old record would otherwise
// match again). Returns to slot 0, then resets. Does not return.
void hal_ble_factory_reset(void) {
  uint8_t s;
  GAPBondMgr_SetParameter(GAPBOND_ERASE_ALLBONDS, 0, NULL);
  LL_ClearWhiteList();
  for (s = 0; s < BLE_SLOT_COUNT; s++) {
    slots.host[s].valid = 0;
    slots.gen[s]++;
  }
  slots.cur = 0;
  pendValid = 0;
  slotSave();
  hal_reset();
}

#if BLE_DIAG
// Encode on the LEDs (bt_app.c, DIAG build) the result of the LAST REAL key
// report that flush() tried to send (diagSendRc), without sending anything of
// its own. An earlier version probed with a zero report, but that extra report
// contended with the real key report's buffer and showed a false failure even
// when input worked; this just reports what the real send returned.
// Physical LEDs after the blue-pin reversal (sayo_2b3.h): BLUE1 -> L8,
// BLUE2 -> L7, BLUE3 -> L6.
uint8_t hal_ble_diag_bits(void) {
  uint8_t bits = 0, rc = diagSendRc;
  if (gapState == GAPROLE_CONNECTED) {
    bits |= BTLED_WHITE; // L3: connected (our GAPRole view)
    if (linkDB_State(connHandle, LINK_ENCRYPTED)) {
      bits |= BTLED_GREEN; // L4: link encrypted
    }
  }
  if (rc != SUCCESS && rc != 0xFF) {
    bits |= BTLED_RED; // L5: the last real key report failed to send
  }
  if (rc == SUCCESS) {
    bits |= BTLED_BLUE3; // L6: the report went out (HID input working)
  } else if (rc == bleNoResources) {
    bits |= BTLED_BLUE2; // L7: CCCD notify off (host not subscribed / no buffer)
  } else if (rc == bleNotReady) {
    bits |= BTLED_BLUE1; // L8: not connected or not secure
  }
  // 0xFF (no key sent yet) or any other code: no blue, only L3/L4/L5 above
  return bits;
}
// Second DIAG phase: how many bonds the stack holds in SNV (GAPBOND_BOND_COUNT)
// and two latches since this connect. The SNV keeps very few bonds (SNVNum), so
// a reconnect that shows 0 bonds means our keys were lost (overwritten by a
// later pairing, or erased) -- the host then has no key to encrypt with.
//   L3/L4/L5 (white/green/red) = bond count as 3-bit binary (bit0/1/2), 0..7
//   L6 blue = the link reached encryption at least once since this connect
//   L7 blue = a peripheral security request was sent since this connect
uint8_t hal_ble_diag_bond(void) {
  uint8_t n = 0, bits = 0;
  GAPBondMgr_GetParameter(GAPBOND_BOND_COUNT, &n);
  if (n & 1) bits |= BTLED_WHITE;
  if (n & 2) bits |= BTLED_GREEN;
  if (n & 4) bits |= BTLED_RED;
  if (diagEncrReached) bits |= BTLED_BLUE3; // L6
  if (diagSecReqSent) bits |= BTLED_BLUE2;  // L7
  return bits;
}
#endif

static void flush(void) {
  if (pending && hal_ble_state() == HAL_BLE_READY) {
    uint8_t rc = HidDev_Report(HID_RPT_ID_KEY_IN, HID_REPORT_TYPE_INPUT, RPT_LEN, report);
#if BLE_DIAG
    diagSendRc = rc; // the real key report's result, for the DIAG LEDs
#endif
    if (rc == SUCCESS) {
      pending = 0;
    }
  }
}

void hal_ble_poll(void) {
  TMOS_SystemProcess();
  if (gapState == GAPROLE_CONNECTED) {
    if (linkDB_State(connHandle, LINK_ENCRYPTED)) {
#if BLE_DIAG
      diagEncrReached = 1; // the link reached encryption at least once this connect
#endif
      advWanted = 0; // the host is here: no advertising after it leaves (bt_app.c decides)
      if (pendValid) {
        // Bonded now: remember this host for the current slot (whitelist on the
        // next reconnect). Only update RAM here and flush to flash on disconnect
        // -- slotSave() turns off all interrupts for a few ms (EEPROM erase +
        // write), which, run now, collides with the host's GATT discovery and
        // CCCD (notify-enable) writes and leaves notifications off, so no key
        // report ever goes out. Mark it dirty instead.
        slotHost_t *h = &slots.host[slots.cur];
        if (!h->valid || h->addrType != pendAddrType || !addrEq(h->addr, pendAddr)) {
          h->valid = 1;
          h->addrType = pendAddrType;
          tmos_memcpy(h->addr, pendAddr, B_ADDR_LEN);
          hostDirty = 1;
        }
        pendValid = 0;
      }
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
