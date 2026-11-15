// AC6321A USB device (JUZH_3B4), UNVERIFIED on a chip. The SDK's USB stack
// (apps/common/device/usb, TCFG_PC_ENABLE) runs EP0 and the standard
// requests; this file is its HID class (in place of the SDK's one-interface
// apps/hid/examples/standard_keyboard/usb_hid_devices.c) and its device
// descriptor file (in place of apps/common/device/usb/device/descriptor.c,
// VID 4A4C). jl_build_ac6321a.sh leaves both SDK files out of the build.
//   interface 0: boot keyboard, EP HID_EP_IN (8 B IN, 10 ms); Caps Lock from
//                SET_REPORT on EP0
//   interface 1: vendor HID (docs/PROTOCOL.md), EP HID_EP_IN_2 IN + OUT, 64 B
// Same report descriptors, VID/PID and strings as the CH32V203 / CH573 apps.
// The SDK calls hid_register() and hid_desc_config() for HID_CLASS
// (usb_device.c usb_device_mode, USB_DEVICE_CLASS_CONFIG).
#if defined(MK_CHIP_AC6321A)
#include "os/os_api.h"
#include "app_config.h"
#include "usb/device/usb_stack.h"
#include "usb/device/hid.h"
#include "usb_config.h"
#include "../hal.h"

#define KB_EP HID_EP_IN      // 2 (usb_std_class_def.h)
#define VEN_EP HID_EP_IN_2   // 3
#define KB_SIZE 8
#define VEN_SIZE 64
// USB counts as gone when the frame number (SOF, every 1 ms) stops this long:
// cable out or host asleep (the SDK posts no event for it in this app)
#define SOF_TIMEOUT_MS 20

// ---- descriptors (as hal_ch573.c) --------------------------------------------
static const u8 devDesc[18] = {
    18, 1, 0x00, 0x02, 0, 0, 0, EP0_SETUP_LEN, USB_VID & 0xFF, USB_VID >> 8,
    USB_PID & 0xFF, USB_PID >> 8, FW_MINOR, FW_MAJOR, 1, 2, 3, 1};

static const u8 keyRepDesc[62] = {
    0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00,
    0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02, 0x95, 0x01, 0x75, 0x08, 0x81, 0x01,
    0x95, 0x03, 0x75, 0x01, 0x05, 0x08, 0x19, 0x01, 0x29, 0x03, 0x91, 0x02, 0x95, 0x05,
    0x75, 0x01, 0x91, 0x01, 0x95, 0x06, 0x75, 0x08, 0x26, 0xFF, 0x00, 0x05, 0x07, 0x19,
    0x00, 0x29, 0x91, 0x81, 0x00, 0xC0};

static const u8 vendorRepDesc[34] = {
    0x06, 0x00, 0xFF, 0x09, 0x01, 0xA1, 0x01,
    0x09, 0x02, 0x15, 0x00, 0x26, 0xFF, 0x00, 0x75, 0x08, 0x95, 0x40, 0x81, 0x02,
    0x09, 0x03, 0x15, 0x00, 0x26, 0xFF, 0x00, 0x75, 0x08, 0x95, 0x40, 0x91, 0x02,
    0xC0};

// The two interfaces as hid_desc_config() appends them (interface numbers
// patched in there). bmAttributes / power: the SDK's sConfigDescriptor below.
static const u8 itfDesc[] = {
    // interface 0: boot keyboard, EP IN 8 B, 10 ms
    0x09, 0x04, 0x00, 0x00, 0x01, 0x03, 0x01, 0x01, 0x00,
    0x09, 0x21, 0x11, 0x01, 0x00, 0x01, 0x22, sizeof(keyRepDesc), 0x00,
    0x07, 0x05, 0x80 | KB_EP, 0x03, KB_SIZE, 0x00, 0x0A,
    // interface 1: vendor HID (config GUI), EP IN + OUT, 64 B, 1 ms
    0x09, 0x04, 0x01, 0x00, 0x02, 0x03, 0x00, 0x00, 0x00,
    0x09, 0x21, 0x11, 0x01, 0x00, 0x01, 0x22, sizeof(vendorRepDesc), 0x00,
    0x07, 0x05, 0x80 | VEN_EP, 0x03, VEN_SIZE, 0x00, 0x01,
    0x07, 0x05, VEN_EP, 0x03, VEN_SIZE, 0x00, 0x01};
#define ITF0_HID (itfDesc + 9)
#define ITF1 (9 + 9 + 7)
#define ITF1_HID (itfDesc + ITF1 + 9)

static const u8 sConfigDescriptor[] = {
    USB_DT_CONFIG_SIZE, USB_DT_CONFIG, 0, 0, // total length: the SDK fills it in
    0,    // interfaces: the SDK counts them
    0x01, 0x00,
    0xA0, // bus powered, remote wakeup
    0x32, // 100 mA
};

// Serial = board name, as on the CH32V203 (Windows keeps boards apart)
static void putString(u8 *ptr, const char *s) {
  u8 n = 2;
  while (*s && n < 62) {
    ptr[n++] = (u8)*s++;
    ptr[n++] = 0;
  }
  ptr[0] = n;
  ptr[1] = 3;
}

// descriptor.c API (include_lib usb/device/descriptor.h)
void get_device_descriptor(u8 *ptr) { memcpy(ptr, devDesc, sizeof(devDesc)); }
void get_language_str(u8 *ptr) {
  ptr[0] = 4, ptr[1] = 3, ptr[2] = 0x09, ptr[3] = 0x04;
}
void get_manufacture_str(u8 *ptr) { putString(ptr, "MacroKeyboard"); }
void get_product_str(u8 *ptr) { putString(ptr, "Mini Macro Kbd"); }
void get_iserialnumber_str(u8 *ptr) { putString(ptr, BOARD_NAME); }
const u8 *usb_get_config_desc(void) { return sConfigDescriptor; }
const u8 *usb_get_string_desc(u32 id) {
  (void)id;
  return NULL;
}
// Asked by the SDK's OTA code (CONFIG_UPDATA_ENABLE): VID, PID, version and
// three 18-byte strings
void get_device_info_to_ota(void *parm_priv) {
  u8 *p = parm_priv;
  memcpy(p, devDesc + 8, 6);
  memset(p + 6, 0, 3 * 18);
}

// ---- state ---------------------------------------------------------------------
static struct usb_device_t *usbDev;
static u8 kbItf, venItf;
static u8 *kbInDma, *venInDma, *venOutDma;
static volatile u8 configured, capsLock;
static volatile u8 kbBusy, venBusy, venReq;
static u16 lastFrame;
static uint32_t lastSofMs;

uint8_t hal_req[64];
uint8_t hal_res[64];

// IN done (TX interrupt of that endpoint)
static void kbInDone(struct usb_device_t *dev, u32 ep) {
  (void)dev, (void)ep;
  kbBusy = 0;
}

static void venInDone(struct usb_device_t *dev, u32 ep) {
  (void)dev, (void)ep;
  venBusy = 0;
}

// The factory firmware's UBOOT command, 03 EF EF + zeros (its vendor HID,
// report ID 3). Our vendor report has no ID, so a host reaches this only by
// writing report 0 with 03 EF EF as data. As our protocol it would be WRITE
// with length 0 (BAD_ARG), so taking it as UBOOT costs nothing. The GUI uses
// BOOT (0x06) on our firmware.
static u8 isFactoryUboot(void) {
  u8 i;
  if (hal_req[0] != 0x03 || hal_req[1] != 0xEF || hal_req[2] != 0xEF) {
    return 0;
  }
  for (i = 3; i < VEN_SIZE; i++) {
    if (hal_req[i]) {
      return 0;
    }
  }
  return 1;
}

volatile uint8_t hal_uboot_request;

// OUT from the config GUI: one 64-byte request; NAKed (not read) while the
// last one is still waiting. TODO(SDK): whether an unread OUT packet stays
// NAKed until read, or must be read here and dropped
static void venOut(struct usb_device_t *dev, u32 ep) {
  const usb_dev id = usb_device2id(dev);
  if (venReq) {
    return;
  }
  if (usb_g_intr_read(id, ep, hal_req, VEN_SIZE, 0) == VEN_SIZE) {
    if (isFactoryUboot()) {
      hal_uboot_request = 1; // main loop: hal_enter_bootloader()
    } else {
      venReq = 1;
    }
  }
}

static void endpointInit(struct usb_device_t *dev) {
  const usb_dev id = usb_device2id(dev);
  // TODO(SDK): the 4th argument (interrupt enable) on an IN endpoint, needed
  // for the done callbacks; the SDK's own HID class passes 0 there
  usb_g_ep_config(id, KB_EP | USB_DIR_IN, USB_ENDPOINT_XFER_INT, 1, kbInDma, KB_SIZE);
  usb_g_set_intr_hander(id, KB_EP | USB_DIR_IN, kbInDone);
  usb_enable_ep(id, KB_EP);
  usb_g_ep_config(id, VEN_EP | USB_DIR_IN, USB_ENDPOINT_XFER_INT, 1, venInDma, VEN_SIZE);
  usb_g_set_intr_hander(id, VEN_EP | USB_DIR_IN, venInDone);
  usb_g_ep_config(id, VEN_EP | USB_DIR_OUT, USB_ENDPOINT_XFER_INT, 1, venOutDma, VEN_SIZE);
  usb_g_set_intr_hander(id, VEN_EP | USB_DIR_OUT, venOut);
  usb_enable_ep(id, VEN_EP);
  kbBusy = venBusy = venReq = 0;
}

// SET_REPORT on interface 0: the LED byte
static u32 kbSetReport(struct usb_device_t *dev, struct usb_ctrlrequest *setup) {
  u8 buf[8];
  usb_read_ep0(usb_device2id(dev), buf, MIN(sizeof(buf), setup->wLength));
  capsLock = buf[0] & 0x02;
  return USB_EP0_STAGE_SETUP;
}

static u32 itfHandler(struct usb_device_t *dev, struct usb_ctrlrequest *req) {
  u8 *payload;
  u8 itf;
  if (req == (void *)-1) {
    return 0;
  }
  usbDev = dev;
  itf = (u8)req->wIndex;
  payload = usb_get_setup_buffer(dev);
  switch (req->bRequestType & USB_TYPE_MASK) {
  case USB_TYPE_STANDARD:
    if (req->bRequest == USB_REQ_GET_DESCRIPTOR) {
      switch (HIBYTE(req->wValue)) {
      case USB_HID_DT_HID:
        usb_set_data_payload(dev, req, itf == kbItf ? ITF0_HID : ITF1_HID, 9);
        break;
      case USB_HID_DT_REPORT:
        // The host reads the report descriptors after SET_CONFIGURATION:
        // from here on the keyboard counts as configured (as the SDK's class)
        if (itf == kbItf) {
          usb_set_data_payload(dev, req, keyRepDesc, sizeof(keyRepDesc));
        } else {
          usb_set_data_payload(dev, req, vendorRepDesc, sizeof(vendorRepDesc));
        }
        configured = 1;
        lastSofMs = hal_ms32();
        break;
      default:
        usb_set_setup_phase(dev, USB_EP0_SET_STALL);
        break;
      }
    } else if (req->bRequest == USB_REQ_GET_INTERFACE && req->wLength) {
      payload[0] = 0;
      usb_set_data_payload(dev, req, payload, 1);
    } else {
      usb_set_setup_phase(dev, USB_EP0_SET_STALL);
    }
    break;
  case USB_TYPE_CLASS:
    switch (req->bRequest) {
    case USB_REQ_SET_IDLE:
      usb_set_setup_phase(dev, USB_EP0_STAGE_SETUP);
      break;
    case USB_REQ_GET_IDLE:
      payload[0] = 0;
      usb_set_data_payload(dev, req, payload, 1);
      break;
    case USB_REQ_SET_REPORT:
      if (itf == kbItf) {
        usb_set_setup_recv(dev, kbSetReport);
      } else {
        usb_set_setup_phase(dev, USB_EP0_SET_STALL);
      }
      break;
    default:
      usb_set_setup_phase(dev, USB_EP0_SET_STALL);
      break;
    }
    break;
  default:
    usb_set_setup_phase(dev, USB_EP0_SET_STALL);
    break;
  }
  return 0;
}

// Bus reset: endpoints again, not configured until the host asks again
static void itfReset(struct usb_device_t *dev, u32 itf) {
  usbDev = dev;
  configured = 0;
  if (itf == kbItf) {
    endpointInit(dev);
  }
}

// ---- SDK class hooks (usb_device.c) --------------------------------------------
u32 hid_register(const usb_dev usb_id) {
  kbInDma = usb_alloc_ep_dmabuffer(usb_id, KB_EP | USB_DIR_IN, KB_SIZE);
  venInDma = usb_alloc_ep_dmabuffer(usb_id, VEN_EP | USB_DIR_IN, VEN_SIZE);
  venOutDma = usb_alloc_ep_dmabuffer(usb_id, VEN_EP | USB_DIR_OUT, VEN_SIZE);
  return 0;
}

void hid_release(const usb_dev usb_id) { (void)usb_id; }

u32 hid_desc_config(const usb_dev usb_id, u8 *ptr, u32 *cur_itf_num) {
  memcpy(ptr, itfDesc, sizeof(itfDesc));
  kbItf = (u8)*cur_itf_num;
  venItf = kbItf + 1;
  ptr[2] = kbItf;
  ptr[ITF1 + 2] = venItf;
  if (usb_set_interface_hander(usb_id, kbItf, itfHandler) != kbItf ||
      usb_set_interface_hander(usb_id, venItf, itfHandler) != venItf) {
    ASSERT(0, "hid set interface_hander fail");
  }
  usb_set_reset_hander(usb_id, kbItf, itfReset);
  usb_set_reset_hander(usb_id, venItf, itfReset);
  *cur_itf_num = venItf + 1;
  return sizeof(itfDesc);
}

// ---- HAL ---------------------------------------------------------------------
extern void usb_start(void); // apps/common/device/usb/device/task_pc.c

void hal_usb_start(void) {
  configured = capsLock = 0;
  usb_start();
}

uint8_t hal_usb_ready(void) {
  if (!configured || !usbDev) {
    return 0;
  }
  u16 frame = usb_read_sofframe(usb_device2id(usbDev));
  uint32_t now = hal_ms32();
  if (frame != lastFrame) {
    lastFrame = frame;
    lastSofMs = now;
  }
  return now - lastSofMs < SOF_TIMEOUT_MS;
}

uint8_t hal_usb_caps_lock(void) { return capsLock; }

uint8_t hal_usb_kb_busy(void) { return kbBusy; }

uint8_t hal_usb_kb_send(uint8_t modifiers, uint8_t keycode) {
  u8 r[KB_SIZE] = {0};
  if (!hal_usb_ready() || kbBusy) {
    return 0;
  }
  r[0] = modifiers;
  r[2] = keycode;
  kbBusy = 1;
  if (!usb_g_intr_write(usb_device2id(usbDev), KB_EP, r, KB_SIZE)) {
    kbBusy = 0; // TODO(SDK): what usb_g_intr_write returns when it cannot send
    return 0;
  }
  return 1;
}

// A report written to the endpoint cannot be taken back here: let it go
void hal_usb_kb_cancel(void) {}

uint8_t vendor_has_request(void) { return venReq; }
uint8_t vendor_in_busy(void) { return venBusy; }

// Send HAL_RES, then take the next request
void vendor_send(void) {
  if (usbDev && configured) {
    venBusy = 1;
    if (!usb_g_intr_write(usb_device2id(usbDev), VEN_EP, hal_res, VEN_SIZE)) {
      venBusy = 0;
    }
  }
  venReq = 0;
}
#endif
