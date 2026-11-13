// CH573 USB device (PB10/PB11) with one vendor HID interface: 64-byte IN/OUT
// reports on EP2, the same reports as the CH32V203 bootloader (1209:C55E).
// Interrupt-driven, EP0 and endpoint handling copied from the app
// (src/hal/ch573/hal_ch573.c: setup, usbIsr), which Windows starts; the
// earlier polled version enumerated but HidUsb failed to start (code 10,
// 2026-11-13). The handler and what it calls run from RAM (.highcode);
// chip_flash_* turn interrupts off around ROM flash work (USB NAKs meanwhile,
// RB_UC_INT_BUSY). The bootloader leaves by reset (chip_reset), and
// chip_jump_app runs before usb_init, so the app never sees this interrupt.
#include "usb_hid.h"
#include "chip.h"
#include "CH57x_common.h"
#include <string.h>

#define VID 0x1209
#define PID 0xC55E // the app is 0xC55D

#define RAMFUNC __attribute__((section(".highcode")))

// Test build (make BL_USB_COMPOSITE=1): the app's exact descriptors (boot
// keyboard interface 0 on EP1 IN + vendor interface 1 on EP2, serial = board
// name) under 1209:C55E, to tell a descriptor cause from a bootloader one
// (code 10, 2026-11-13). hidboot picks the vendor interface by usage page.
#ifndef BL_USB_COMPOSITE
#define BL_USB_COMPOSITE 0
#endif

static const uint8_t dev_desc[18] = {
    18, 1, 0x00, 0x02, 0, 0, 0, 64, VID & 0xFF, VID >> 8, PID & 0xFF, PID >> 8,
    0x00, 0x01, 1, 2, BL_USB_COMPOSITE ? 3 : 0, 1};

static const uint8_t rep_desc[34] = {
    0x06, 0x00, 0xFF, 0x09, 0x01, 0xA1, 0x01,
    0x09, 0x02, 0x15, 0x00, 0x26, 0xFF, 0x00, 0x75, 0x08, 0x95, 0x40, 0x81, 0x02,
    0x09, 0x03, 0x15, 0x00, 0x26, 0xFF, 0x00, 0x75, 0x08, 0x95, 0x40, 0x91, 0x02,
    0xC0};

#if BL_USB_COMPOSITE
static const uint8_t key_rep_desc[62] = {
    0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00,
    0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02, 0x95, 0x01, 0x75, 0x08, 0x81, 0x01,
    0x95, 0x03, 0x75, 0x01, 0x05, 0x08, 0x19, 0x01, 0x29, 0x03, 0x91, 0x02, 0x95, 0x05,
    0x75, 0x01, 0x91, 0x01, 0x95, 0x06, 0x75, 0x08, 0x26, 0xFF, 0x00, 0x05, 0x07, 0x19,
    0x00, 0x29, 0x91, 0x81, 0x00, 0xC0};

static const uint8_t cfg_desc[66] = {
    0x09, 0x02, 66, 0x00, 0x02, 0x01, 0x00, 0xA0, 0x32, // config: 2 interfaces, 100 mA
    0x09, 0x04, 0x00, 0x00, 0x01, 0x03, 0x01, 0x01, 0x00,
    0x09, 0x21, 0x11, 0x01, 0x00, 0x01, 0x22, sizeof(key_rep_desc), 0x00,
    0x07, 0x05, 0x81, 0x03, 0x08, 0x00, 0x0A,
    0x09, 0x04, 0x01, 0x00, 0x02, 0x03, 0x00, 0x00, 0x00,
    0x09, 0x21, 0x11, 0x01, 0x00, 0x01, 0x22, sizeof(rep_desc), 0x00,
    0x07, 0x05, 0x82, 0x03, 0x40, 0x00, 0x01,
    0x07, 0x05, 0x02, 0x03, 0x40, 0x00, 0x01};
#define VEN_HID_DESC (cfg_desc + 18 + 9 + 7 + 9)
#define N_STRINGS 3
static const char *const strings[] = {"MacroKeyboard", "MacroKeyboard Bootloader", BOARD_NAME};
#else
// Interface as the app's interface 1 (vendor HID, EP2 IN + OUT, 1 ms)
static const uint8_t cfg_desc[41] = {
    9, 2, 41, 0, 1, 1, 0, 0x80, 50,                      // config, 100 mA
    9, 4, 0, 0, 2, 3, 0, 0, 0,                           // interface: HID, 2 EPs
    9, 0x21, 0x11, 0x01, 0, 1, 0x22, sizeof(rep_desc), 0, // HID
    7, 5, 0x82, 3, 64, 0, 1,                             // EP2 IN, 1 ms
    7, 5, 0x02, 3, 64, 0, 1};                            // EP2 OUT, 1 ms
#define VEN_HID_DESC (cfg_desc + 18)
#define N_STRINGS 2
static const char *const strings[] = {"MacroKeyboard", "MacroKeyboard Bootloader"};
#endif

// EP0 64 B; EP2 OUT at the buffer, IN 64 B after it
static __attribute__((aligned(4))) uint8_t ep0[64];
#if BL_USB_COMPOSITE
static __attribute__((aligned(4))) uint8_t ep1[128]; // keyboard IN: never sends
#endif
static __attribute__((aligned(4))) uint8_t ep2[128];
static uint8_t rx_buf[64]; // OUT report copied in the handler

static volatile uint8_t dev_config, rx_ready, tx_busy;

// Diagnostic build (make BL_USB_DIAG=1): USB stages reached, shown on the
// status LEDs by main.c (usb_hid.h USB_DIAG_*)
#if BL_USB_DIAG
volatile uint8_t usb_diag;
#define DIAG(bit) (usb_diag |= (bit))
// Event log (usb_hid.h USB_LOG_*), saved to Data-Flash 0 by main.c; the app
// reads it back with CMD_READ (firmware/bootloader_ch573/diag_read.py)
uint8_t usb_log[USB_LOG_SIZE];
volatile uint16_t usb_log_n;    // records so far
volatile uint32_t usb_log_ms;   // time of the last one
uint8_t usb_variant;            // 1 = unknown GET_DESCRIPTOR: empty reply, not STALL
static uint8_t isr_fg, isr_count; // INT_FG at handler entry, handler entries
static uint8_t setup_copy[8];     // SETUP packet, copied before the reply overwrites ep0
RAMFUNC static void log_event(uint8_t kind, uint8_t res) {
  if (usb_log_n >= USB_LOG_MAX)
    return;
  uint8_t *r = usb_log + 16 + USB_LOG_REC * usb_log_n;
  uint32_t ms = chip_ms();
  uint32_t t = ms * 1000 + R32_TMR0_COUNT / (FREQ_SYS / 1000000); // us
  r[0] = kind, r[1] = res;
  r[2] = (uint8_t)t, r[3] = (uint8_t)(t >> 8), r[4] = (uint8_t)(t >> 16), r[5] = (uint8_t)(t >> 24);
  r[6] = isr_fg, r[7] = R8_USB_INT_ST, r[8] = R8_USB_MIS_ST, r[9] = R8_UEP0_CTRL;
  r[10] = R8_USB_RX_LEN, r[11] = R8_USB_DEV_AD, r[12] = R8_UEP0_T_LEN, r[13] = isr_count;
  r[14] = R8_USB_INT_FG, r[15] = 0;
  for (int i = 0; i < 8; i++)
    r[16 + i] = kind == USB_LOG_SETUP ? setup_copy[i] : 0;
  usb_log_n++;
  usb_log_ms = ms;
}
#define LOG(kind, res) log_event(kind, res)
#else
#define DIAG(bit) ((void)0)
#define LOG(kind, res) ((void)0)
#endif
static uint8_t req_type, req_code, dev_addr;

RAMFUNC static void endp_init(void) {
#if BL_USB_COMPOSITE
  R8_UEP4_1_MOD = RB_UEP1_RX_EN | RB_UEP1_TX_EN;
  R16_UEP1_DMA = (uint16_t)(uint32_t)ep1;
  R8_UEP1_CTRL = UEP_R_RES_NAK | UEP_T_RES_NAK | RB_UEP_AUTO_TOG;
#endif
  R8_UEP2_3_MOD = RB_UEP2_RX_EN | RB_UEP2_TX_EN;
  R16_UEP0_DMA = (uint16_t)(uint32_t)ep0;
  R16_UEP2_DMA = (uint16_t)(uint32_t)ep2;
  R8_UEP0_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK;
  R8_UEP2_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK | RB_UEP_AUTO_TOG;
  rx_ready = 0;
  tx_busy = 0;
}

void usb_init(void) {
  R8_USB_CTRL = 0;
  endp_init();
  R8_USB_DEV_AD = 0;
  R8_USB_CTRL = RB_UC_DEV_PU_EN | RB_UC_INT_BUSY | RB_UC_DMA_EN;
  R16_PIN_ANALOG_IE |= RB_PIN_USB_IE | RB_PIN_USB_DP_PU;
  R8_USB_INT_FG = 0xFF;
  R8_UDEV_CTRL = RB_UD_PD_DIS | RB_UD_PORT_EN;
  R8_USB_INT_EN = RB_UIE_SUSPEND | RB_UIE_BUS_RST | RB_UIE_TRANSFER;
  PFIC_EnableIRQ(USB_IRQn);
}

void usb_stop(void) {
  PFIC_DisableIRQ(USB_IRQn);
  R8_USB_INT_EN = 0;
  R16_PIN_ANALOG_IE &= ~(RB_PIN_USB_IE | RB_PIN_USB_DP_PU); // pull-up off
  R8_USB_CTRL = RB_UC_RESET_SIE | RB_UC_CLR_ALL;
  R8_UDEV_CTRL = 0;
}

// String descriptor built from ASCII into ep0; returns its length
RAMFUNC static uint8_t string_desc(uint8_t i) {
  if (i == 0) {
    ep0[0] = 4, ep0[1] = 3, ep0[2] = 0x09, ep0[3] = 0x04;
    return 4;
  }
  const char *s = strings[i - 1];
  uint8_t n = 2;
  while (*s && n < 62) {
    ep0[n++] = (uint8_t)*s++;
    ep0[n++] = 0;
  }
  ep0[0] = n, ep0[1] = 3;
  return n;
}

// Byte loop kept as a loop (not a call to memcpy in flash)
__attribute__((optimize("no-tree-loop-distribute-patterns"))) RAMFUNC static void copy64(uint8_t *d, const uint8_t *s, uint8_t n) {
  while (n--)
    *d++ = *s++;
}

RAMFUNC static void setup(void) {
  const USB_SETUP_REQ *r = (const USB_SETUP_REQ *)ep0;
  uint16_t want = r->wLength;
  uint8_t lo = r->wValue & 0xFF, hi = r->wValue >> 8, idx = r->wIndex & 0xFF;
  const uint8_t *src = 0;
  int len = 0; // -1 = stall

  req_type = r->bRequestType;
  req_code = r->bRequest;
  if ((req_type & USB_REQ_TYP_MASK) == USB_REQ_TYP_STANDARD) {
    switch (req_code) {
    case USB_GET_DESCRIPTOR:
      switch (hi) {
      case USB_DESCR_TYP_DEVICE: src = dev_desc, len = sizeof dev_desc; break;
      case USB_DESCR_TYP_CONFIG: src = cfg_desc, len = sizeof cfg_desc; break;
#if BL_USB_COMPOSITE
      case USB_DESCR_TYP_HID: src = idx ? VEN_HID_DESC : cfg_desc + 18, len = 9; break;
      case USB_DESCR_TYP_REPORT:
        if (idx)
          src = rep_desc, len = sizeof rep_desc, DIAG(USB_DIAG_REPORT);
        else
          src = key_rep_desc, len = sizeof key_rep_desc;
        break;
#else
      case USB_DESCR_TYP_HID: src = VEN_HID_DESC, len = 9; break;
      case USB_DESCR_TYP_REPORT: src = rep_desc, len = sizeof rep_desc, DIAG(USB_DIAG_REPORT); break;
#endif
      case USB_DESCR_TYP_STRING: len = lo <= N_STRINGS ? string_desc(lo) : -1; break;
      default: len = -1;
      }
      // Unknown descriptor (device qualifier, other speed, BOS, string past
      // the last): empty reply, not STALL. On this board Windows asks for the
      // qualifier right after the strings; after that STALL the next request
      // (device descriptor) never completed its IN stage and the host gave
      // up (code 10). The empty reply enumerated (diag build, 2026-11-13).
#if BL_USB_DIAG
      if (len < 0 && !usb_variant) // diag variant 0: the old STALL, to compare
        break;
#endif
      if (len < 0)
        len = 0;
      break;
    case USB_SET_ADDRESS: dev_addr = lo; break;
    case USB_GET_CONFIGURATION: ep0[0] = dev_config, len = 1; break;
    case USB_SET_CONFIGURATION: dev_config = lo, DIAG(USB_DIAG_CONFIG); break;
    case USB_SET_INTERFACE: break;
    case USB_GET_INTERFACE: ep0[0] = 0, len = 1; break;
    case USB_CLEAR_FEATURE: // endpoint halt: back to DATA0
      if ((req_type & USB_REQ_RECIP_MASK) == USB_REQ_RECIP_ENDP) {
        if (idx == 0x81)
          R8_UEP1_CTRL = (R8_UEP1_CTRL & ~(RB_UEP_T_TOG | MASK_UEP_T_RES)) | UEP_T_RES_NAK;
        else if (idx == 0x82)
          R8_UEP2_CTRL = (R8_UEP2_CTRL & ~(RB_UEP_T_TOG | MASK_UEP_T_RES)) | UEP_T_RES_NAK, tx_busy = 0;
        else if (idx == 0x02)
          R8_UEP2_CTRL = (R8_UEP2_CTRL & ~RB_UEP_R_TOG);
      }
      break;
    case USB_SET_FEATURE: break;
    case USB_GET_STATUS: ep0[0] = 0, ep0[1] = 0, len = 2; break;
    default: len = -1;
    }
  } else if ((req_type & USB_REQ_TYP_MASK) == USB_REQ_TYP_CLASS) {
    switch (req_code) { // HID class: accepted, ignored
    case HID_SET_IDLE:
    case HID_SET_PROTOCOL:
    case HID_SET_REPORT: // data stage (EP0 OUT) dropped
      break;
    case HID_GET_IDLE: ep0[0] = 0, len = 1; break;
    case HID_GET_PROTOCOL: ep0[0] = 1, len = 1; break;
    default: len = -1;
    }
  } else {
    len = -1;
  }

  if (len < 0) {
    R8_UEP0_CTRL = RB_UEP_R_TOG | RB_UEP_T_TOG | UEP_R_RES_STALL | UEP_T_RES_STALL;
    DIAG(USB_DIAG_STALL);
    LOG(USB_LOG_SETUP, 0xFF);
    return;
  }
  if (len > want)
    len = want;
  if (len > 64)
    len = 64; // every descriptor here fits one packet
  if (src)
    copy64(ep0, src, (uint8_t)len);
  // IN request: data stage; OUT: ZLP status (after the data stage, if any)
  R8_UEP0_T_LEN = (req_type & 0x80) ? (uint8_t)len : 0;
  R8_UEP0_CTRL = RB_UEP_R_TOG | RB_UEP_T_TOG | UEP_R_RES_ACK | UEP_T_RES_ACK;
  LOG(USB_LOG_SETUP, (uint8_t)len);
}

RAMFUNC static void usb_isr(void) {
  uint8_t fg = R8_USB_INT_FG;
#if BL_USB_DIAG
  isr_fg = fg, isr_count++;
#endif
  if (fg & RB_UIF_TRANSFER) {
    uint8_t st = R8_USB_INT_ST;
    if ((st & MASK_UIS_TOKEN) != MASK_UIS_TOKEN) {
      switch (st & (MASK_UIS_TOKEN | MASK_UIS_ENDP)) {
      case UIS_TOKEN_IN | 0:
        LOG(USB_LOG_IN0, 0);
        if ((req_type & USB_REQ_TYP_MASK) == USB_REQ_TYP_STANDARD && req_code == USB_SET_ADDRESS)
          R8_USB_DEV_AD = (R8_USB_DEV_AD & RB_UDA_GP_BIT) | dev_addr, DIAG(USB_DIAG_ADDRESS);
        req_code = 0xFF;
        R8_UEP0_T_LEN = 0;
        R8_UEP0_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK;
        break;
      case UIS_TOKEN_OUT | 0:
        LOG(USB_LOG_OUT0, 0);
        if (!((req_type & USB_REQ_TYP_MASK) == USB_REQ_TYP_CLASS && req_code == HID_SET_REPORT &&
              R8_USB_RX_LEN)) // SET_REPORT data: its ZLP status is armed already
          R8_UEP0_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK; // status done
        break;
      case UIS_TOKEN_IN | 1:
        R8_UEP1_CTRL = (R8_UEP1_CTRL & ~MASK_UEP_T_RES) | UEP_T_RES_NAK;
        break;
      case UIS_TOKEN_OUT | 2:
        if (st & RB_UIS_TOG_OK) {
          // NAK until usb_recv() takes the report (toggle: RB_UEP_AUTO_TOG)
          R8_UEP2_CTRL = (R8_UEP2_CTRL & ~MASK_UEP_R_RES) | UEP_R_RES_NAK;
          copy64(rx_buf, ep2, 64);
          rx_ready = 1;
        }
        break;
      case UIS_TOKEN_IN | 2:
        R8_UEP2_CTRL = (R8_UEP2_CTRL & ~MASK_UEP_T_RES) | UEP_T_RES_NAK;
        tx_busy = 0;
        break;
      default:
        break;
      }
      R8_USB_INT_FG = RB_UIF_TRANSFER;
    }
    if (R8_USB_INT_ST & RB_UIS_SETUP_ACT) {
      DIAG(USB_DIAG_SETUP);
#if BL_USB_DIAG
      for (int i = 0; i < 8; i++)
        setup_copy[i] = ep0[i];
#endif
      setup();
      R8_USB_INT_FG = RB_UIF_TRANSFER;
    }
  } else if (fg & RB_UIF_BUS_RST) {
    LOG(USB_LOG_RESET, 0);
    dev_addr = 0;
    dev_config = 0;
    R8_USB_DEV_AD = 0;
    endp_init();
    R8_USB_INT_FG = RB_UIF_BUS_RST;
  } else if (fg) {
    LOG(USB_LOG_OTHER, fg);
    R8_USB_INT_FG = fg; // suspend and others: nothing to do
  }
}

__attribute__((interrupt("WCH-Interrupt-fast"))) RAMFUNC void USB_IRQHandler(void) {
  usb_isr();
}

int usb_recv(uint8_t *buf) {
  if (!rx_ready)
    return 0;
  memcpy(buf, rx_buf, 64);
  rx_ready = 0;
  R8_UEP2_CTRL = (R8_UEP2_CTRL & ~MASK_UEP_R_RES) | UEP_R_RES_ACK;
  return 1;
}

int usb_tx_busy(void) { return tx_busy; }

void usb_send(const uint8_t *buf) {
  while (tx_busy) {
  }
  memcpy(ep2 + 64, buf, 64);
  tx_busy = 1;
  R8_UEP2_T_LEN = 64;
  R8_UEP2_CTRL = (R8_UEP2_CTRL & ~MASK_UEP_T_RES) | UEP_T_RES_ACK;
}
