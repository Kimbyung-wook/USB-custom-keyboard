// Polled USBFS (PB6/PB7) device with one vendor HID interface: 64-byte IN/OUT
// reports on EP1. No interrupts: usb_poll() from the main loop. While a
// transfer flag is pending the controller NAKs (UC_INT_BUSY), so long flash
// operations only delay the host.
#include "usb_hid.h"
#include "ch32v20x.h"
#include "ch32v20x_usb.h"
#include <string.h>

#define VID 0x1209
#define PID 0xC55E // the app is 0xC55D

static const uint8_t dev_desc[18] = {
    18, 1, 0x00, 0x02, 0, 0, 0, 64, VID & 0xFF, VID >> 8, PID & 0xFF, PID >> 8,
    0x00, 0x01, 1, 2, 0, 1};

static const uint8_t rep_desc[] = {
    0x06, 0x00, 0xFF, 0x09, 0x01, 0xA1, 0x01,                         //
    0x09, 0x02, 0x15, 0x00, 0x26, 0xFF, 0x00, 0x75, 0x08, 0x95, 0x40, 0x81, 0x02,
    0x09, 0x03, 0x15, 0x00, 0x26, 0xFF, 0x00, 0x75, 0x08, 0x95, 0x40, 0x91, 0x02,
    0xC0};

static const uint8_t cfg_desc[41] = {
    9, 2, 41, 0, 1, 1, 0, 0x80, 50,                      // config, 100 mA
    9, 4, 0, 0, 2, 3, 0, 0, 0,                           // interface: HID, 2 EPs
    9, 0x21, 0x11, 0x01, 0, 1, 0x22, sizeof(rep_desc), 0, // HID
    7, 5, 0x81, 3, 64, 0, 1,                             // EP1 IN, 1 ms
    7, 5, 0x01, 3, 64, 0, 1};                            // EP1 OUT, 1 ms

static const char *const strings[] = {"MacroKeyboard", "MacroKeyboard Bootloader"};

// EP0 64 B; EP1 OUT at 0, IN at 64 (TX_EN + RX_EN, single buffers)
static __attribute__((aligned(4))) uint8_t ep0[64];
static __attribute__((aligned(4))) uint8_t ep1[128];

static uint8_t req_type, req_code, dev_addr;
static uint8_t rx_ready, tx_busy;
volatile uint8_t usb_stage; // furthest enumeration step seen (DIAG LEDs, main.c)
#define STAGE(n) (usb_stage < (n) ? (usb_stage = (n)) : 0)

static void endp_init(void) {
  USBFSD->UEP4_1_MOD = USBFS_UEP1_RX_EN | USBFS_UEP1_TX_EN;
  USBFSD->UEP0_DMA = (uint32_t)ep0;
  USBFSD->UEP1_DMA = (uint32_t)ep1;
  USBFSD->UEP0_RX_CTRL = USBFS_UEP_R_RES_ACK;
  USBFSD->UEP0_TX_CTRL = USBFS_UEP_T_RES_NAK;
  USBFSD->UEP1_TX_CTRL = USBFS_UEP_T_RES_NAK;
  USBFSD->UEP1_RX_CTRL = USBFS_UEP_R_RES_ACK;
  rx_ready = 0;
  tx_busy = 0;
}

void usb_init(void) {
  RCC->AHBPCENR |= RCC_AHBPeriph_USBFS; // USB clock = PLL 48 MHz / 1 (reset value)
  USBFSH->BASE_CTRL = USBFS_UC_RESET_SIE | USBFS_UC_CLR_ALL;
  for (volatile int i = 0; i < 500; i++) {
  }
  USBFSH->BASE_CTRL = 0;
  endp_init();
  USBFSD->INT_EN = USBFS_UIE_SUSPEND | USBFS_UIE_BUS_RST | USBFS_UIE_TRANSFER;
  USBFSD->BASE_CTRL = USBFS_UC_DEV_PU_EN | USBFS_UC_INT_BUSY | USBFS_UC_DMA_EN;
  USBFSD->UDEV_CTRL = USBFS_UD_PD_DIS | USBFS_UD_PORT_EN;
}

void usb_stop(void) {
  USBFSD->BASE_CTRL = USBFS_UC_RESET_SIE | USBFS_UC_CLR_ALL; // pull-up off
  USBFSD->UDEV_CTRL = 0;
}

// String descriptor built from ASCII into ep0; returns its length
static uint8_t string_desc(uint8_t i) {
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

static void setup(void) {
  const USB_SETUP_REQ *r = (const USB_SETUP_REQ *)ep0;
  uint16_t want = r->wLength;
  uint8_t value_lo = r->wValue & 0xFF, value_hi = r->wValue >> 8;
  const uint8_t *src = 0;
  int len = 0; // -1 = stall
  req_type = r->bRequestType;
  req_code = r->bRequest;

  if ((req_type & USB_REQ_TYP_MASK) == USB_REQ_TYP_STANDARD) {
    switch (req_code) {
    case USB_GET_DESCRIPTOR:
      switch (value_hi) {
      case USB_DESCR_TYP_DEVICE: src = dev_desc, len = sizeof dev_desc; break;
      case USB_DESCR_TYP_CONFIG: src = cfg_desc, len = sizeof cfg_desc; break;
      case USB_DESCR_TYP_HID: src = cfg_desc + 18, len = 9; break;
      case USB_DESCR_TYP_REPORT: src = rep_desc, len = sizeof rep_desc; break;
      case USB_DESCR_TYP_STRING:
        len = value_lo <= 2 ? string_desc(value_lo) : -1;
        break;
      default: len = -1;
      }
      break;
    case USB_SET_ADDRESS: dev_addr = value_lo; break;
    case USB_GET_CONFIGURATION: ep0[0] = 1, len = 1; break;
    case USB_SET_CONFIGURATION: STAGE(5); break;
    case USB_SET_INTERFACE:
    case USB_CLEAR_FEATURE: break;
    case USB_GET_INTERFACE: ep0[0] = 0, len = 1; break;
    case USB_GET_STATUS: ep0[0] = 0, ep0[1] = 0, len = 2; break;
    default: len = -1;
    }
  } else if ((req_type & USB_REQ_TYP_MASK) == USB_REQ_TYP_CLASS &&
             (req_code == HID_SET_IDLE || req_code == HID_SET_PROTOCOL)) {
    // accepted, ignored
  } else {
    len = -1;
  }

  if (len < 0) {
    USBFSD->UEP0_TX_CTRL = USBFS_UEP_T_TOG | USBFS_UEP_T_RES_STALL;
    USBFSD->UEP0_RX_CTRL = USBFS_UEP_R_TOG | USBFS_UEP_R_RES_STALL;
    return;
  }
  if (len > want)
    len = want;
  if (len > 64)
    len = 64; // every descriptor here fits one packet
  if (src)
    memcpy(ep0, src, len);
  // IN request: data stage (or ZLP status); OUT without data: ZLP status
  USBFSD->UEP0_TX_LEN = (req_type & 0x80) ? len : 0;
  USBFSD->UEP0_TX_CTRL = USBFS_UEP_T_TOG | USBFS_UEP_T_RES_ACK;
}

void usb_poll(void) {
  uint8_t fg = USBFSD->INT_FG;
  if (fg & USBFS_UIF_TRANSFER) {
    uint8_t st = USBFSD->INT_ST;
    STAGE(2);
    // SETUP by the token alone, as the SDK does (the endpoint bits are not
    // checked for it)
    uint8_t key = (st & USBFS_UIS_TOKEN_MASK) == USBFS_UIS_TOKEN_SETUP
                      ? USBFS_UIS_TOKEN_SETUP
                      : st & (USBFS_UIS_TOKEN_MASK | USBFS_UIS_ENDP_MASK);
    switch (key) {
    case USBFS_UIS_TOKEN_SETUP:
      USBFSD->UEP0_TX_CTRL = USBFS_UEP_T_TOG | USBFS_UEP_T_RES_NAK;
      USBFSD->UEP0_RX_CTRL = USBFS_UEP_R_TOG | USBFS_UEP_R_RES_NAK;
      STAGE(3);
      setup();
      break;
    case USBFS_UIS_TOKEN_IN | 0:
      if ((req_type & USB_REQ_TYP_MASK) == USB_REQ_TYP_STANDARD &&
          req_code == USB_SET_ADDRESS) {
        USBFSD->DEV_ADDR = (USBFSD->DEV_ADDR & USBFS_UDA_GP_BIT) | dev_addr;
        STAGE(4);
      }
      // data stage sent: accept the status OUT
      USBFSD->UEP0_TX_CTRL = USBFS_UEP_T_RES_NAK;
      USBFSD->UEP0_RX_CTRL = USBFS_UEP_R_TOG | USBFS_UEP_R_RES_ACK;
      break;
    case USBFS_UIS_TOKEN_OUT | 0:
      USBFSD->UEP0_RX_CTRL = USBFS_UEP_R_RES_ACK; // status done, wait for SETUP
      break;
    case USBFS_UIS_TOKEN_OUT | 1:
      if (st & USBFS_UIS_TOG_OK) {
        USBFSD->UEP1_RX_CTRL ^= USBFS_UEP_R_TOG;
        // NAK until usb_recv() takes the report
        USBFSD->UEP1_RX_CTRL = (USBFSD->UEP1_RX_CTRL & ~USBFS_UEP_R_RES_MASK) | USBFS_UEP_R_RES_NAK;
        rx_ready = 1;
      }
      break;
    case USBFS_UIS_TOKEN_IN | 1:
      USBFSD->UEP1_TX_CTRL = (USBFSD->UEP1_TX_CTRL & ~USBFS_UEP_T_RES_MASK) | USBFS_UEP_T_RES_NAK;
      USBFSD->UEP1_TX_CTRL ^= USBFS_UEP_T_TOG;
      tx_busy = 0;
      break;
    default:
      break;
    }
    USBFSD->INT_FG = USBFS_UIF_TRANSFER;
  } else if (fg & USBFS_UIF_BUS_RST) {
    STAGE(1);
    dev_addr = 0;
    USBFSD->DEV_ADDR = 0;
    endp_init();
    USBFSD->INT_FG = USBFS_UIF_BUS_RST;
  } else if (fg) {
    USBFSD->INT_FG = fg; // suspend and others: nothing to do
  }
}

int usb_recv(uint8_t *buf) {
  if (!rx_ready)
    return 0;
  memcpy(buf, ep1, 64);
  rx_ready = 0;
  USBFSD->UEP1_RX_CTRL = (USBFSD->UEP1_RX_CTRL & ~USBFS_UEP_R_RES_MASK) | USBFS_UEP_R_RES_ACK;
  return 1;
}

int usb_tx_busy(void) { return tx_busy; }

void usb_send(const uint8_t *buf) {
  while (tx_busy)
    usb_poll();
  memcpy(ep1 + 64, buf, 64);
  tx_busy = 1;
  USBFSD->UEP1_TX_LEN = 64;
  USBFSD->UEP1_TX_CTRL = (USBFSD->UEP1_TX_CTRL & ~USBFS_UEP_T_RES_MASK) | USBFS_UEP_T_RES_ACK;
}
