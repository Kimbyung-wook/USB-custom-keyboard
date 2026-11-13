// CH573 chip layer for the shared pin test (see ch573_hal.h).
// USB: one interface, boot keyboard on EP1 IN, control on EP0. Based on the
// SDK example USB/Device/CompoundDev (keyboard part only).
#include <string.h>
#include "CH57x_common.h"
#include "ch573_hal.h"

// ---- time: TMR0 every 1 ms ----

static volatile uint32_t ms;

__attribute__((interrupt("WCH-Interrupt-fast")))
__attribute__((section(".highcode")))
void TMR0_IRQHandler(void) {
  TMR0_ClearITFlag(TMR0_3_IT_CYC_END);
  ms++;
}

uint32_t hal_millis(void) { return ms; }

void hal_delay_ms(uint32_t t) {
  uint32_t t0 = ms;
  while (ms - t0 < t) {
  }
}

// ---- reset cause, faults ----

static uint8_t resetFlag;

uint8_t hal_reset_cause(void) {
  switch (resetFlag) {
    case RST_FLAG_SW:   return 4;
    case RST_FLAG_RPOR: return 2;
    case RST_FLAG_WTR:  return 8;
    case RST_FLAG_MR:   return 1;
    default:            return 32; // 1xx or 101: power-on or wake during sleep/shutdown
  }
}

uint8_t hal_fault_read(uint32_t out[3]) {
  (void)out;
  return 0;
}

// ---- GPIO ----

static void modeCfg(uint8_t port, uint8_t bit, GPIOModeTypeDef m) {
  if (port == 0) {
    GPIOA_ModeCfg(1u << bit, m);
  } else {
    GPIOB_ModeCfg(1u << bit, m);
  }
}

void hal_gpio_input_pullup(uint8_t port, uint8_t bit) { modeCfg(port, bit, GPIO_ModeIN_PU); }
void hal_gpio_input_pulldown(uint8_t port, uint8_t bit) { modeCfg(port, bit, GPIO_ModeIN_PD); }
void hal_gpio_output(uint8_t port, uint8_t bit) { modeCfg(port, bit, GPIO_ModeOut_PP_5mA); }

void hal_gpio_write(uint8_t port, uint8_t bit, uint8_t high) {
  uint32_t m = 1u << bit;
  if (port == 0) {
    if (high) GPIOA_SetBits(m); else GPIOA_ResetBits(m);
  } else {
    if (high) GPIOB_SetBits(m); else GPIOB_ResetBits(m);
  }
}

uint8_t hal_gpio_read(uint8_t port, uint8_t bit) {
  uint32_t m = 1u << bit;
  return (port == 0 ? GPIOA_ReadPortPin(m) : GPIOB_ReadPortPin(m)) ? 1 : 0;
}

// ---- USB HID keyboard ----

#define EP0_SIZE 64

static const uint8_t devDesc[] = {
  0x12, 0x01, 0x10, 0x01, 0x00, 0x00, 0x00, EP0_SIZE,
  0x3D, 0x41, 0x07, 0x21, // VID 0x413D, PID 0x2107 (SDK example IDs, test only)
  0x00, 0x01, 0x01, 0x02, 0x00, 0x01,
};

static const uint8_t keyRepDesc[] = {
  0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00,
  0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02, 0x95, 0x01, 0x75, 0x08, 0x81, 0x01,
  0x95, 0x03, 0x75, 0x01, 0x05, 0x08, 0x19, 0x01, 0x29, 0x03, 0x91, 0x02, 0x95, 0x05,
  0x75, 0x01, 0x91, 0x01, 0x95, 0x06, 0x75, 0x08, 0x26, 0xFF, 0x00, 0x05, 0x07, 0x19,
  0x00, 0x29, 0x91, 0x81, 0x00, 0xC0,
};

static const uint8_t cfgDesc[] = {
  0x09, 0x02, 0x22, 0x00, 0x01, 0x01, 0x00, 0xA0, 0x32,  // configuration, 1 interface
  0x09, 0x04, 0x00, 0x00, 0x01, 0x03, 0x01, 0x01, 0x00,  // interface 0: HID boot keyboard
  0x09, 0x21, 0x11, 0x01, 0x00, 0x01, 0x22, sizeof(keyRepDesc), 0x00, // HID
  0x07, 0x05, 0x81, 0x03, 0x08, 0x00, 0x0A,              // EP1 IN, 8 bytes, 10 ms
};

static const uint8_t langDesc[] = {0x04, 0x03, 0x09, 0x04};
static const uint8_t manuDesc[] = {0x08, 0x03, 'M', 0, 'K', 0, ' ', 0};
static const uint8_t prodDesc[] = {0x14, 0x03, 'T', 0, 'E', 0, 'S', 0, 'T', 0, ' ', 0,
                                   'C', 0, 'H', 0, '5', 0, '7', 0};

__attribute__((aligned(4))) static uint8_t ep0Buf[64 + 64 + 64]; // ep0 + ep4 out/in (unused)
__attribute__((aligned(4))) static uint8_t ep1Buf[64 + 64];      // ep1 out + in
__attribute__((aligned(4))) static uint8_t ep2Buf[64 + 64];      // unused, the driver sets its DMA
__attribute__((aligned(4))) static uint8_t ep3Buf[64 + 64];

static volatile uint8_t devConfig;
static uint8_t setupReq;
static uint16_t setupLen;
static const uint8_t *descPtr;
static uint8_t idleValue, protocolValue = 1;

static void ep0Stall(void) {
  R8_UEP0_CTRL = RB_UEP_R_TOG | RB_UEP_T_TOG | UEP_R_RES_STALL | UEP_T_RES_STALL;
}

static void handleSetup(void) {
  uint8_t len = 0;
  uint8_t err = 0;
  uint8_t type = pSetupReqPak->bRequestType;

  R8_UEP0_CTRL = RB_UEP_R_TOG | RB_UEP_T_TOG | UEP_R_RES_ACK | UEP_T_RES_NAK;
  setupLen = pSetupReqPak->wLength;
  setupReq = pSetupReqPak->bRequest;

  if ((type & USB_REQ_TYP_MASK) == USB_REQ_TYP_CLASS) {
    switch (setupReq) {
      case DEF_USB_SET_IDLE: idleValue = pEP0_DataBuf[3]; break;
      case DEF_USB_SET_REPORT: break; // LED state: ignored
      case DEF_USB_SET_PROTOCOL: protocolValue = pEP0_DataBuf[2]; break;
      case DEF_USB_GET_IDLE: pEP0_DataBuf[0] = idleValue; len = 1; break;
      case DEF_USB_GET_PROTOCOL: pEP0_DataBuf[0] = protocolValue; len = 1; break;
      default: err = 1;
    }
    if (len && setupLen > len) setupLen = len;
  } else if ((type & USB_REQ_TYP_MASK) != USB_REQ_TYP_STANDARD) {
    err = 1;
  } else {
    switch (setupReq) {
      case USB_GET_DESCRIPTOR: {
        uint8_t dlen = 0;
        switch (pSetupReqPak->wValue >> 8) {
          case USB_DESCR_TYP_DEVICE: descPtr = devDesc; dlen = sizeof(devDesc); break;
          case USB_DESCR_TYP_CONFIG: descPtr = cfgDesc; dlen = sizeof(cfgDesc); break;
          case USB_DESCR_TYP_HID: descPtr = &cfgDesc[18]; dlen = 9; break;
          case USB_DESCR_TYP_REPORT: descPtr = keyRepDesc; dlen = sizeof(keyRepDesc); break;
          case USB_DESCR_TYP_STRING:
            switch (pSetupReqPak->wValue & 0xFF) {
              case 0: descPtr = langDesc; dlen = sizeof(langDesc); break;
              case 1: descPtr = manuDesc; dlen = sizeof(manuDesc); break;
              case 2: descPtr = prodDesc; dlen = sizeof(prodDesc); break;
              default: err = 1;
            }
            break;
          default: err = 1;
        }
        if (!err) {
          if (setupLen > dlen) setupLen = dlen;
          len = setupLen >= EP0_SIZE ? EP0_SIZE : setupLen;
          memcpy(pEP0_DataBuf, descPtr, len);
          descPtr += len;
        }
        break;
      }
      case USB_SET_ADDRESS: setupLen = pSetupReqPak->wValue & 0xFF; break;
      case USB_GET_CONFIGURATION:
        pEP0_DataBuf[0] = devConfig;
        if (setupLen > 1) setupLen = 1;
        break;
      case USB_SET_CONFIGURATION: devConfig = pSetupReqPak->wValue & 0xFF; break;
      case USB_GET_INTERFACE:
        pEP0_DataBuf[0] = 0;
        if (setupLen > 1) setupLen = 1;
        break;
      case USB_SET_INTERFACE: break;
      case USB_CLEAR_FEATURE:
        if ((type & USB_REQ_RECIP_MASK) == USB_REQ_RECIP_ENDP && (pSetupReqPak->wIndex & 0xFF) == 0x81) {
          R8_UEP1_CTRL = (R8_UEP1_CTRL & ~(RB_UEP_T_TOG | MASK_UEP_T_RES)) | UEP_T_RES_NAK;
        }
        break;
      case USB_SET_FEATURE: break;
      case USB_GET_STATUS:
        pEP0_DataBuf[0] = 0;
        pEP0_DataBuf[1] = 0;
        if (setupLen > 2) setupLen = 2;
        break;
      default: err = 1;
    }
  }

  if (err) {
    ep0Stall();
    return;
  }
  if (type & 0x80) { // device to host
    len = setupLen > EP0_SIZE ? EP0_SIZE : setupLen;
    setupLen -= len;
  } else {
    len = 0;
  }
  R8_UEP0_T_LEN = len;
  R8_UEP0_CTRL = RB_UEP_R_TOG | RB_UEP_T_TOG | UEP_R_RES_ACK | UEP_T_RES_ACK;
}

static void usbIsr(void) {
  uint8_t fg = R8_USB_INT_FG;

  if (fg & RB_UIF_TRANSFER) {
    if ((R8_USB_INT_ST & MASK_UIS_TOKEN) != MASK_UIS_TOKEN) {
      switch (R8_USB_INT_ST & (MASK_UIS_TOKEN | MASK_UIS_ENDP)) {
        case UIS_TOKEN_IN:
          if (setupReq == USB_GET_DESCRIPTOR) {
            uint8_t len = setupLen >= EP0_SIZE ? EP0_SIZE : setupLen;
            memcpy(pEP0_DataBuf, descPtr, len);
            setupLen -= len;
            descPtr += len;
            R8_UEP0_T_LEN = len;
            R8_UEP0_CTRL ^= RB_UEP_T_TOG;
          } else if (setupReq == USB_SET_ADDRESS) {
            R8_USB_DEV_AD = (R8_USB_DEV_AD & RB_UDA_GP_BIT) | setupLen;
            R8_UEP0_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK;
          } else {
            R8_UEP0_T_LEN = 0;
            R8_UEP0_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK;
          }
          break;
        case UIS_TOKEN_OUT:
          break; // SET_REPORT data (keyboard LEDs): ignored
        case UIS_TOKEN_IN | 1:
          R8_UEP1_CTRL ^= RB_UEP_T_TOG;
          R8_UEP1_CTRL = (R8_UEP1_CTRL & ~MASK_UEP_T_RES) | UEP_T_RES_NAK;
          break;
        default:
          break;
      }
      R8_USB_INT_FG = RB_UIF_TRANSFER;
    }
    if (R8_USB_INT_ST & RB_UIS_SETUP_ACT) {
      handleSetup();
      R8_USB_INT_FG = RB_UIF_TRANSFER;
    }
  } else if (fg & RB_UIF_BUS_RST) {
    R8_USB_DEV_AD = 0;
    devConfig = 0;
    R8_UEP0_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK;
    R8_UEP1_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK;
    R8_USB_INT_FG = RB_UIF_BUS_RST;
  } else {
    R8_USB_INT_FG = fg; // suspend and the rest: just clear
  }
}

__attribute__((interrupt("WCH-Interrupt-fast")))
__attribute__((section(".highcode")))
void USB_IRQHandler(void) {
  usbIsr();
}

#define HID_MOD_SHIFT 0x02

// One 8-byte report on EP1 IN. Waits up to 50 ms for the endpoint, then drops it.
static void usbSend(const uint8_t *report) {
  uint32_t t0 = ms;

  if (!devConfig) {
    return;
  }
  while ((R8_UEP1_CTRL & MASK_UEP_T_RES) == UEP_T_RES_ACK) { // previous report not taken yet
    if (ms - t0 > 50) {
      return;
    }
  }
  memcpy(pEP1_IN_DataBuf, report, 8);
  DevEP1_IN_Deal(8);
}

// ASCII to HID usage (US layout). 0 if the character has no key.
static uint8_t keyFor(char c, uint8_t *mod) {
  *mod = 0;
  if (c >= 'a' && c <= 'z') return (uint8_t)(0x04 + (c - 'a'));
  if (c >= 'A' && c <= 'Z') {
    *mod = HID_MOD_SHIFT;
    return (uint8_t)(0x04 + (c - 'A'));
  }
  if (c >= '1' && c <= '9') return (uint8_t)(0x1E + (c - '1'));
  if (c == '0') return 0x27;
  switch (c) {
    case '\n': return 0x28;
    case ' ':  return 0x2C;
    case '-':  return 0x2D;
    case '.':  return 0x37;
    case ':':  *mod = HID_MOD_SHIFT; return 0x33;
    case '?':  *mod = HID_MOD_SHIFT; return 0x38;
    default:   return 0;
  }
}

void hal_hid_write(char c) {
  uint8_t report[8];
  uint8_t mod;
  uint8_t key = keyFor(c, &mod);

  if (!key) {
    return;
  }
  memset(report, 0, sizeof(report));
  report[0] = mod;
  report[2] = key;
  usbSend(report);
  memset(report, 0, sizeof(report));
  usbSend(report);
}

void hal_hid_print(const char *s) {
  while (*s) {
    hal_hid_write(*s++);
  }
}

// ---- ADC ----

// -12 dB gain (PGA 1/4): Vin = (code / 512 - 3) * 1.05 V, range about 0..5.2 V
static int32_t adcMv(void) {
  int16_t calib = ADC_DataCalib_Rough();
  uint32_t sum = 0;

  for (uint8_t n = 0; n < 16; n++) {
    sum += ADC_ExcutSingleConver();
  }
  return ((int32_t)(sum / 16) + calib) * 1050 / 512 - 3150;
}

static void typeNum(int32_t v) {
  char buf[12];
  uint8_t n = 0;

  if (v < 0) {
    hal_hid_write('-');
    v = -v;
  }
  do {
    buf[n++] = (char)('0' + v % 10);
    v /= 10;
  } while (v && n < sizeof(buf));
  while (n) {
    hal_hid_write(buf[--n]);
  }
}

static const struct { uint8_t port, bit, ch; const char *name; } adcPins[] = {
  {0, 8, CH_EXTIN_12, " PA08 "},
  {1, 0, CH_EXTIN_8, " PB00 "},
  {1, 6, CH_EXTIN_9, " PB06 "},
};

void hal_adc_report(void) {
  int32_t mv[1 + sizeof(adcPins) / sizeof(adcPins[0])];

  // Measure everything first, then type (typing takes a while)
  ADC_InterBATSampInit();
  mv[0] = adcMv();
  for (uint8_t i = 0; i < sizeof(adcPins) / sizeof(adcPins[0]); i++) {
    modeCfg(adcPins[i].port, adcPins[i].bit, GPIO_ModeIN_Floating);
    ADC_ExtSingleChSampInit(SampleFreq_3_2, ADC_PGA_1_4);
    ADC_ChannelCfg(adcPins[i].ch);
    hal_delay_ms(5); // let the pin settle without the pull-up
    mv[1 + i] = adcMv();
    modeCfg(adcPins[i].port, adcPins[i].bit, GPIO_ModeIN_PU); // back to a test pin
  }
  R8_ADC_CFG = 0; // ADC off

  hal_hid_print("VBAT ");
  typeNum(mv[0]);
  for (uint8_t i = 0; i < sizeof(adcPins) / sizeof(adcPins[0]); i++) {
    hal_hid_print(adcPins[i].name);
    typeNum(mv[1 + i]);
  }
  hal_hid_write('\n');
}

// ---- start ----

void hal_init(void) {
  resetFlag = R8_RESET_STATUS & RB_RESET_FLAG;
  SetSysClock(CLK_SOURCE_PLL_60MHz);

  TMR0_TimerInit(FREQ_SYS / 1000);
  TMR0_ITCfg(ENABLE, TMR0_3_IT_CYC_END);
  PFIC_EnableIRQ(TMR0_IRQn);

  pEP0_RAM_Addr = ep0Buf;
  pEP1_RAM_Addr = ep1Buf;
  pEP2_RAM_Addr = ep2Buf;
  pEP3_RAM_Addr = ep3Buf;
  USB_DeviceInit();
  R8_UEP1_CTRL &= ~RB_UEP_AUTO_TOG; // the IN handler toggles by hand
  PFIC_EnableIRQ(USB_IRQn);
}
