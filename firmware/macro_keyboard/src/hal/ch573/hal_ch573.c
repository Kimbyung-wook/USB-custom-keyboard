// CH573 implementation of src/hal/hal.h (see hal_ch573.h). Built only for the
// CH573 app (MK_CHIP_CH573 from Makefile.ch573): the Arduino IDE and the
// CH32V203 build skip it.
#if defined(__riscv) && defined(MK_CHIP_CH573)
#include <string.h>
#include "CH57x_common.h"
#include "hal/hal.h"
#include "core/bt_led.h"

// Same numbers as firmware/bootloader_ch573/boot_layout.h (Makefile.ch573
// links the app at BOOT_APP_ADDR and checks the size against it)
#define BOOT_MARK_ENTER 0xB0u

// ---- time: TMR0 every 1 ms ----
static volatile uint32_t ms;

__attribute__((interrupt("WCH-Interrupt-fast"))) __attribute__((section(".highcode")))
void TMR0_IRQHandler(void) {
  TMR0_ClearITFlag(TMR0_3_IT_CYC_END);
  ms++;
}

uint16_t hal_ms(void) { return (uint16_t)ms; }
uint32_t hal_ms32(void) { return ms; }

static void waitMs(uint32_t t) {
  uint32_t t0 = ms;
  while (ms - t0 < t) {
  }
}

// ---- keys: own pin each, pressed = low ----
static const uint8_t keyPins[BOARD_KEYS][2] = {BOARD_KEY_PINS};

hal_sw_t hal_switches(void) {
  hal_sw_t sw = 0;
  uint32_t a = R32_PA_PIN, b = R32_PB_PIN;
  for (uint8_t i = 0; i < BOARD_KEYS; i++) {
    uint32_t pins = keyPins[i][0] ? b : a;
    if (!(pins & (1u << keyPins[i][1]))) {
      sw |= (hal_sw_t)(1u << i);
    }
  }
  return sw;
}

// ---- config storage: Data-Flash 0 .. BLOB_CAPACITY, one 256-byte page in RAM ----
// RAM is short with the BLE stack, so there is no copy of the whole blob:
// reads and writes go through one cached page, written back (erase + write +
// check) when another page is needed or at hal_store_flush(). storage.c
// reads and writes the blob in order, so each page is written at most once.
// The BLE bonding area (BLE_SNV_ADDR, Data-Flash 0x7E00) stays clear of it.
// Erased Data-Flash reads 0xFF: storage.c sees an invalid blob, uses defaults.
#define STORE_ADDR 0
#define STORE_PAGE EEPROM_PAGE_SIZE // 256 B: erase and write unit used here
#if BLOB_CAPACITY % 256 || STORE_ADDR + BLOB_CAPACITY > 0x7000
#error BLOB_CAPACITY: whole 256-byte pages below the BLE area
#endif
static __attribute__((aligned(4))) uint8_t pageBuf[STORE_PAGE];
static __attribute__((aligned(4))) uint8_t checkBuf[16];
static uint32_t pageAt = 0xFFFFFFFFu; // offset of the page in pageBuf
static uint8_t pageDirty, storeFailed;

static void pageWriteBack(void) {
  uint32_t irq;
  if (!pageDirty) {
    return;
  }
  pageDirty = 0;
  // A few ms with interrupts off (as on the CH32V203): the ms clock falls
  // behind a little, USB NAKs meanwhile
  SYS_DisableAllIrq(&irq);
  EEPROM_ERASE(STORE_ADDR + pageAt, STORE_PAGE);
  EEPROM_WRITE(STORE_ADDR + pageAt, pageBuf, STORE_PAGE);
  SYS_RecoverIrq(irq);
  for (uint32_t i = 0; i < STORE_PAGE; i += sizeof checkBuf) {
    EEPROM_READ(STORE_ADDR + pageAt + i, checkBuf, sizeof checkBuf);
    if (memcmp(checkBuf, pageBuf + i, sizeof checkBuf) != 0) {
      storeFailed = 1;
      pageAt = 0xFFFFFFFFu; // read back what is really there next time
      return;
    }
  }
}

static uint8_t *pageFor(blob_pos_t addr) {
  uint32_t at = addr & ~(uint32_t)(STORE_PAGE - 1);
  if (at != pageAt) {
    pageWriteBack();
    EEPROM_READ(STORE_ADDR + at, pageBuf, STORE_PAGE);
    pageAt = at;
  }
  return pageBuf + (addr & (STORE_PAGE - 1));
}

uint8_t hal_store_read(blob_pos_t addr) { return *pageFor(addr); }

void hal_store_write(blob_pos_t addr, uint8_t value) {
  uint8_t *p = pageFor(addr);
  if (*p != value) {
    *p = value;
    pageDirty = 1;
  }
}

uint8_t hal_store_flush(void) {
  pageWriteBack();
  uint8_t ok = !storeFailed;
  storeFailed = 0;
  return ok;
}

// ---- USB: interface 0 boot keyboard (EP1 IN), interface 1 vendor HID
// (EP2 IN + OUT, 64 B), 1209:C55D: the CH32V203 app's descriptors ----
static const uint8_t devDesc[18] = {
    18, 1, 0x00, 0x02, 0, 0, 0, 64, USB_VID & 0xFF, USB_VID >> 8, USB_PID & 0xFF, USB_PID >> 8,
    FW_MINOR, FW_MAJOR, 1, 2, 3, 1};

static const uint8_t keyRepDesc[62] = {
    0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00,
    0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02, 0x95, 0x01, 0x75, 0x08, 0x81, 0x01,
    0x95, 0x03, 0x75, 0x01, 0x05, 0x08, 0x19, 0x01, 0x29, 0x03, 0x91, 0x02, 0x95, 0x05,
    0x75, 0x01, 0x91, 0x01, 0x95, 0x06, 0x75, 0x08, 0x26, 0xFF, 0x00, 0x05, 0x07, 0x19,
    0x00, 0x29, 0x91, 0x81, 0x00, 0xC0};

static const uint8_t vendorRepDesc[34] = {
    0x06, 0x00, 0xFF, 0x09, 0x01, 0xA1, 0x01,
    0x09, 0x02, 0x15, 0x00, 0x26, 0xFF, 0x00, 0x75, 0x08, 0x95, 0x40, 0x81, 0x02,
    0x09, 0x03, 0x15, 0x00, 0x26, 0xFF, 0x00, 0x75, 0x08, 0x95, 0x40, 0x91, 0x02,
    0xC0};

static const uint8_t cfgDesc[66] = {
    0x09, 0x02, 66, 0x00, 0x02, 0x01, 0x00, 0xA0, 0x32, // config: 2 interfaces, 100 mA
    // interface 0: boot keyboard, EP1 IN 8 B, 10 ms
    0x09, 0x04, 0x00, 0x00, 0x01, 0x03, 0x01, 0x01, 0x00,
    0x09, 0x21, 0x11, 0x01, 0x00, 0x01, 0x22, sizeof(keyRepDesc), 0x00,
    0x07, 0x05, 0x81, 0x03, 0x08, 0x00, 0x0A,
    // interface 1: vendor HID (config GUI), EP2 IN + OUT, 64 B, 1 ms
    0x09, 0x04, 0x01, 0x00, 0x02, 0x03, 0x00, 0x00, 0x00,
    0x09, 0x21, 0x11, 0x01, 0x00, 0x01, 0x22, sizeof(vendorRepDesc), 0x00,
    0x07, 0x05, 0x82, 0x03, 0x40, 0x00, 0x01,
    0x07, 0x05, 0x02, 0x03, 0x40, 0x00, 0x01};
#define HID0_DESC (cfgDesc + 18)
#define HID1_DESC (cfgDesc + 18 + 9 + 7 + 9)

// Serial = board name, as on the CH32V203 (Windows keeps boards apart)
static const char *const strings[] = {"MacroKeyboard", "Mini Macro Kbd", BOARD_NAME};

// EP0 64 B; EP1 and EP2: OUT at the buffer, IN 64 B after it
static __attribute__((aligned(4))) uint8_t ep0Buf[64];
static __attribute__((aligned(4))) uint8_t ep1Buf[128];
static __attribute__((aligned(4))) uint8_t ep2Buf[128];

uint8_t hal_req[64];
uint8_t hal_res[64];

static volatile uint8_t devConfig, suspended, capsLock;
static volatile uint8_t kbBusy, venBusy, venReq;
static uint8_t reqType, reqCode, devAddr;
static const uint8_t *descPtr;
static uint16_t descLeft;

static void endpInit(void) {
  R8_UEP4_1_MOD = RB_UEP1_RX_EN | RB_UEP1_TX_EN;
  R8_UEP2_3_MOD = RB_UEP2_RX_EN | RB_UEP2_TX_EN;
  R16_UEP0_DMA = (uint16_t)(uint32_t)ep0Buf;
  R16_UEP1_DMA = (uint16_t)(uint32_t)ep1Buf;
  R16_UEP2_DMA = (uint16_t)(uint32_t)ep2Buf;
  R8_UEP0_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK;
  R8_UEP1_CTRL = UEP_R_RES_NAK | UEP_T_RES_NAK | RB_UEP_AUTO_TOG;
  R8_UEP2_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK | RB_UEP_AUTO_TOG;
  kbBusy = venBusy = venReq = 0;
}

static uint8_t stringDesc(uint8_t i) {
  if (i == 0) {
    ep0Buf[0] = 4, ep0Buf[1] = 3, ep0Buf[2] = 0x09, ep0Buf[3] = 0x04;
    return 4;
  }
  const char *s = strings[i - 1];
  uint8_t n = 2;
  while (*s && n < 62) {
    ep0Buf[n++] = (uint8_t)*s++;
    ep0Buf[n++] = 0;
  }
  ep0Buf[0] = n, ep0Buf[1] = 3;
  return n;
}

// Next IN packet of a descriptor (up to 64 B)
static uint8_t descChunk(void) {
  uint8_t n = descLeft > 64 ? 64 : (uint8_t)descLeft;
  memcpy(ep0Buf, descPtr, n);
  descPtr += n;
  descLeft -= n;
  return n;
}

static void setup(void) {
  const USB_SETUP_REQ *r = (const USB_SETUP_REQ *)ep0Buf;
  uint16_t want = r->wLength;
  uint8_t lo = r->wValue & 0xFF, hi = r->wValue >> 8, idx = r->wIndex & 0xFF;
  const uint8_t *src = 0;
  int len = 0; // -1 = stall

  reqType = r->bRequestType;
  reqCode = r->bRequest;
  descLeft = 0;
  if ((reqType & USB_REQ_TYP_MASK) == USB_REQ_TYP_STANDARD) {
    switch (reqCode) {
    case USB_GET_DESCRIPTOR:
      switch (hi) {
      case USB_DESCR_TYP_DEVICE: src = devDesc, len = sizeof devDesc; break;
      case USB_DESCR_TYP_CONFIG: src = cfgDesc, len = sizeof cfgDesc; break;
      case USB_DESCR_TYP_HID: src = idx ? HID1_DESC : HID0_DESC, len = 9; break;
      case USB_DESCR_TYP_REPORT:
        if (idx) {
          src = vendorRepDesc, len = sizeof vendorRepDesc;
        } else {
          src = keyRepDesc, len = sizeof keyRepDesc;
        }
        break;
      case USB_DESCR_TYP_STRING: len = lo <= 3 ? stringDesc(lo) : -1; break;
      default: len = -1;
      }
      break;
    case USB_SET_ADDRESS: devAddr = lo; break;
    case USB_GET_CONFIGURATION: ep0Buf[0] = devConfig, len = 1; break;
    case USB_SET_CONFIGURATION: devConfig = lo; break;
    case USB_SET_INTERFACE: break;
    case USB_GET_INTERFACE: ep0Buf[0] = 0, len = 1; break;
    case USB_CLEAR_FEATURE: // endpoint halt: back to DATA0
      if ((reqType & USB_REQ_RECIP_MASK) == USB_REQ_RECIP_ENDP) {
        if (idx == 0x81) {
          R8_UEP1_CTRL = (R8_UEP1_CTRL & ~(RB_UEP_T_TOG | MASK_UEP_T_RES)) | UEP_T_RES_NAK, kbBusy = 0;
        } else if (idx == 0x82) {
          R8_UEP2_CTRL = (R8_UEP2_CTRL & ~(RB_UEP_T_TOG | MASK_UEP_T_RES)) | UEP_T_RES_NAK, venBusy = 0;
        } else if (idx == 0x02) {
          R8_UEP2_CTRL = (R8_UEP2_CTRL & ~RB_UEP_R_TOG);
        }
      }
      break;
    case USB_SET_FEATURE: break;
    case USB_GET_STATUS: ep0Buf[0] = 0, ep0Buf[1] = 0, len = 2; break;
    default: len = -1;
    }
  } else if ((reqType & USB_REQ_TYP_MASK) == USB_REQ_TYP_CLASS) {
    switch (reqCode) {
    case HID_SET_IDLE:
    case HID_SET_PROTOCOL:
    case HID_SET_REPORT: // keyboard LEDs: the data stage (EP0 OUT) carries them
      break;
    case HID_GET_IDLE: ep0Buf[0] = 0, len = 1; break;
    case HID_GET_PROTOCOL: ep0Buf[0] = 1, len = 1; break;
    default: len = -1;
    }
  } else {
    len = -1;
  }

  if (len < 0) {
    R8_UEP0_CTRL = RB_UEP_R_TOG | RB_UEP_T_TOG | UEP_R_RES_STALL | UEP_T_RES_STALL;
    return;
  }
  if (len > want) {
    len = want;
  }
  if (src) {
    descPtr = src;
    descLeft = (uint16_t)len;
    len = descChunk();
  } else if (len > 64) {
    len = 64;
  }
  // IN request: data stage; OUT: ZLP status (after the data stage, if any)
  R8_UEP0_T_LEN = (reqType & 0x80) ? (uint8_t)len : 0;
  R8_UEP0_CTRL = RB_UEP_R_TOG | RB_UEP_T_TOG | UEP_R_RES_ACK | UEP_T_RES_ACK;
}

__attribute__((noinline)) static void usbIsr(void) {
  uint8_t fg = R8_USB_INT_FG;
  if (fg & RB_UIF_TRANSFER) {
    uint8_t st = R8_USB_INT_ST;
    if ((st & MASK_UIS_TOKEN) != MASK_UIS_TOKEN) {
      switch (st & (MASK_UIS_TOKEN | MASK_UIS_ENDP)) {
      case UIS_TOKEN_IN | 0:
        if (descLeft) { // next packet of a long descriptor
          R8_UEP0_T_LEN = descChunk();
          R8_UEP0_CTRL ^= RB_UEP_T_TOG;
          break;
        }
        if ((reqType & USB_REQ_TYP_MASK) == USB_REQ_TYP_STANDARD && reqCode == USB_SET_ADDRESS) {
          R8_USB_DEV_AD = (R8_USB_DEV_AD & RB_UDA_GP_BIT) | devAddr;
        }
        reqCode = 0xFF;
        R8_UEP0_T_LEN = 0;
        R8_UEP0_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK;
        break;
      case UIS_TOKEN_OUT | 0:
        if ((reqType & USB_REQ_TYP_MASK) == USB_REQ_TYP_CLASS && reqCode == HID_SET_REPORT &&
            R8_USB_RX_LEN) {
          capsLock = ep0Buf[0]; // LED bits: 0x02 = Caps Lock; ZLP status already armed
        } else {
          R8_UEP0_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK; // status done
        }
        break;
      case UIS_TOKEN_IN | 1:
        R8_UEP1_CTRL = (R8_UEP1_CTRL & ~MASK_UEP_T_RES) | UEP_T_RES_NAK;
        kbBusy = 0;
        break;
      case UIS_TOKEN_OUT | 2:
        if (st & RB_UIS_TOG_OK) {
          // NAK until vendor_send() answers (protocol.c reads HAL_REQ)
          R8_UEP2_CTRL = (R8_UEP2_CTRL & ~MASK_UEP_R_RES) | UEP_R_RES_NAK;
          memcpy(hal_req, ep2Buf, 64);
          venReq = 1;
        }
        break;
      case UIS_TOKEN_IN | 2:
        R8_UEP2_CTRL = (R8_UEP2_CTRL & ~MASK_UEP_T_RES) | UEP_T_RES_NAK;
        venBusy = 0;
        break;
      default:
        break;
      }
      R8_USB_INT_FG = RB_UIF_TRANSFER;
    }
    if (R8_USB_INT_ST & RB_UIS_SETUP_ACT) {
      setup();
      R8_USB_INT_FG = RB_UIF_TRANSFER;
    }
  } else if (fg & RB_UIF_BUS_RST) {
    devAddr = 0;
    devConfig = 0;
    R8_USB_DEV_AD = 0;
    endpInit();
    R8_USB_INT_FG = RB_UIF_BUS_RST;
  } else if (fg & RB_UIF_SUSPEND) {
    // Cable out (on battery) or host asleep: no keyboard until resumed
    suspended = (R8_USB_MIS_ST & RB_UMS_SUSPEND) != 0;
    R8_USB_INT_FG = RB_UIF_SUSPEND;
  } else if (fg) {
    R8_USB_INT_FG = fg;
  }
}

// The vector table (in RAM) reaches only .highcode, so the handler stays
// there but the work (usbIsr) is in flash: RAM goes to the BLE stack
__attribute__((interrupt("WCH-Interrupt-fast"))) __attribute__((section(".highcode")))
void USB_IRQHandler(void) {
  usbIsr();
}

uint8_t hal_usb_ready(void) { return devConfig && !suspended; }

static uint8_t usbSend(uint8_t modifiers, uint8_t keycode) {
  if (kbBusy) {
    return 0;
  }
  uint8_t *in = ep1Buf + 64;
  memset(in, 0, 8);
  in[0] = modifiers;
  in[2] = keycode;
  kbBusy = 1;
  R8_UEP1_T_LEN = 8;
  R8_UEP1_CTRL = (R8_UEP1_CTRL & ~MASK_UEP_T_RES) | UEP_T_RES_ACK;
  return 1;
}

static void usbCancel(void) {
  R8_UEP1_CTRL = (R8_UEP1_CTRL & ~MASK_UEP_T_RES) | UEP_T_RES_NAK;
  kbBusy = 0;
}

// Keyboard reports: over USB while it is configured, else over BLE
// (src/core/bt_app.c drops the BLE link while USB is up)
#if BOARD_BLE
uint8_t kb_ready(void) { return hal_usb_ready() || hal_ble_ready(); }
uint8_t kb_busy(void) { return hal_usb_ready() ? kbBusy : hal_ble_busy(); }
uint8_t kb_caps_lock(void) { return hal_usb_ready() ? capsLock & 0x02 : hal_ble_caps_lock(); }
uint8_t kb_send(uint8_t modifiers, uint8_t keycode) {
  return hal_usb_ready() ? usbSend(modifiers, keycode) : hal_ble_send(modifiers, keycode);
}
void kb_cancel(void) {
  usbCancel();
  hal_ble_cancel();
}
#else
uint8_t kb_ready(void) { return hal_usb_ready(); }
uint8_t kb_busy(void) { return kbBusy; }
uint8_t kb_caps_lock(void) { return capsLock & 0x02; }
uint8_t kb_send(uint8_t modifiers, uint8_t keycode) { return usbSend(modifiers, keycode); }
void kb_cancel(void) { usbCancel(); }
#endif

uint8_t vendor_has_request(void) { return venReq; }
uint8_t vendor_in_busy(void) { return venBusy; }

// Send the response, then accept the next request
void vendor_send(void) {
  memcpy(ep2Buf + 64, hal_res, 64);
  venBusy = 1;
  venReq = 0;
  R8_UEP2_T_LEN = 64;
  R8_UEP2_CTRL = (R8_UEP2_CTRL & ~(MASK_UEP_T_RES | MASK_UEP_R_RES)) | UEP_T_RES_ACK | UEP_R_RES_ACK;
}

static void usbStop(void) {
  PFIC_DisableIRQ(USB_IRQn);
  R16_PIN_ANALOG_IE &= ~(RB_PIN_USB_IE | RB_PIN_USB_DP_PU); // pull-up off: host sees a detach
  R8_USB_CTRL = RB_UC_RESET_SIE | RB_UC_CLR_ALL;
  R8_UDEV_CTRL = 0;
}

void hal_usb_start(void) {
  R8_USB_CTRL = 0;
  endpInit();
  R8_USB_DEV_AD = 0;
  R8_USB_CTRL = RB_UC_DEV_PU_EN | RB_UC_INT_BUSY | RB_UC_DMA_EN;
  R16_PIN_ANALOG_IE |= RB_PIN_USB_IE | RB_PIN_USB_DP_PU;
  R8_USB_INT_FG = 0xFF;
  R8_UDEV_CTRL = RB_UD_PD_DIS | RB_UD_PORT_EN;
  R8_USB_INT_EN = RB_UIE_SUSPEND | RB_UIE_BUS_RST | RB_UIE_TRANSFER;
  PFIC_EnableIRQ(USB_IRQn);
}

// ---- system ----
static __attribute__((aligned(4))) uint8_t uid[8];

void hal_init(void) {
  SetSysClock(CLK_SOURCE_PLL_60MHz);

  TMR0_TimerInit(FREQ_SYS / 1000);
  TMR0_ITCfg(ENABLE, TMR0_3_IT_CYC_END);
  PFIC_EnableIRQ(TMR0_IRQn);

  // Status LEDs dark first (outputs low; a pull-up would light them dimly)
  hal_btled_init();
  for (uint8_t i = 0; i < BOARD_KEYS; i++) {
    uint32_t m = 1u << keyPins[i][1];
    if (keyPins[i][0]) {
      GPIOB_ModeCfg(m, GPIO_ModeIN_PU);
    } else {
      GPIOA_ModeCfg(m, GPIO_ModeIN_PU);
    }
  }
  // Pins with nothing found on them (pin test): pulled up, not floating
  GPIOA_ModeCfg(GPIO_Pin_8, GPIO_ModeIN_PU);
  GPIOB_ModeCfg(GPIO_Pin_7 | GPIO_Pin_12 | GPIO_Pin_15 | GPIO_Pin_22 | GPIO_Pin_23, GPIO_ModeIN_PU);

  FLASH_EEPROM_CMD(CMD_GET_UNIQUE_ID, 0, uid, 0);
}

uint8_t hal_chip_uid(uint8_t i) { return uid[i & 7]; }

// BOOT command: leave the enter mark (kept through a software reset) for
// firmware/bootloader_ch573, detach long enough for the host to notice, reset
void hal_enter_bootloader(void) {
  waitMs(300); // let the reply go out
  usbStop();
  hal_btled_write(0);
  waitMs(200);
  R8_GLOB_RESET_KEEP = BOOT_MARK_ENTER;
  SYS_ResetExecute();
  while (1) {
  }
}

void hal_deep_sleep(void) {
  hal_btled_write(0);
  usbStop();
  LowPower_Shutdown(0); // wakes only by power-on (switch SW1)
  while (1) {
  }
}

#endif
