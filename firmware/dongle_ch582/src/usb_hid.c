// USB HID device on CH58x USB1 and USB2 (same register layout, 0x400 apart).
// Both ports start; the first one the PC configures stays, the other is
// switched off so its pins (PB10/PB11 or PB12/PB13) are free again.
// Endpoint handling follows the SDK examples (USB/Device/CompoundDev, BLE/BLE_USB).
#include "CH58x_common.h"
#include "usb_hid.h"
#include "core/usb_report.h"
#include <string.h>

#define USB_VID 0x1209
#define USB_PID 0xC55F // dongle; the keyboard firmware is 0xC55D, its bootloader 0xC55E
#define EP0_SIZE 64

// Register offsets from the port base (CH583SFR.h R8_USB_xxx / R8_USB2_xxx)
#define O_CTRL 0x00
#define O_DEV_CTRL 0x01
#define O_INT_EN 0x02
#define O_DEV_AD 0x03
#define O_MIS_ST 0x05
#define O_INT_FG 0x06
#define O_INT_ST 0x07
#define O_RX_LEN 0x08
#define O_EP41_MOD 0x0C
#define O_EP23_MOD 0x0D
#define O_EP0_DMA 0x10
#define O_EP1_DMA 0x14
#define O_EP2_DMA 0x18
#define O_EP0_TLEN 0x20
#define O_EP0_CTRL 0x22
#define O_EP1_TLEN 0x24
#define O_EP1_CTRL 0x26
#define O_EP2_TLEN 0x28
#define O_EP2_CTRL 0x2A

typedef struct {
    uint32_t base;
    uint8_t *ep0, *ep1, *ep2; // DMA buffers; IN data of EP1/EP2 at +64
    uint8_t config;
    uint8_t req, dir_in, addr;
    uint16_t left;            // control IN bytes still to send
    const uint8_t *src;       // their source (NULL: already in ep0)
    volatile uint8_t kb_busy, ext_busy;
    uint8_t idle[2], proto[2];
} port_t;

#define R8(p, o) (*(volatile uint8_t *)((p)->base + (o)))
#define R16(p, o) (*(volatile uint16_t *)((p)->base + (o)))

__attribute__((aligned(4))) static uint8_t buf1[64 + 128 + 128];
__attribute__((aligned(4))) static uint8_t buf2[64 + 128 + 128];
static port_t ports[2] = {
    {.base = 0x40008000, .ep0 = buf1, .ep1 = buf1 + 64, .ep2 = buf1 + 192},
    {.base = 0x40008400, .ep0 = buf2, .ep1 = buf2 + 64, .ep2 = buf2 + 192},
};
static port_t *volatile active;
static volatile uint8_t leds, leds_new, suspended;

static const uint8_t dev_desc[18] = {
    18, 1, 0x10, 0x01, 0, 0, 0, EP0_SIZE,
    USB_VID & 0xFF, USB_VID >> 8, USB_PID & 0xFF, USB_PID >> 8,
    0x00, 0x01, // bcdDevice 1.00
    1, 2, 0, 1};

static const uint8_t kb_rep[] = {
    0x05, 0x01, 0x09, 0x06, 0xA1, 0x01,             // Keyboard
    0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00, 0x25, 0x01,
    0x75, 0x01, 0x95, 0x08, 0x81, 0x02,             // modifiers
    0x95, 0x01, 0x75, 0x08, 0x81, 0x01,             // reserved
    0x05, 0x08, 0x19, 0x01, 0x29, 0x05, 0x95, 0x05, 0x75, 0x01, 0x91, 0x02, // LEDs
    0x95, 0x01, 0x75, 0x03, 0x91, 0x01,
    0x05, 0x07, 0x19, 0x00, 0x29, 0xFF, 0x15, 0x00, 0x26, 0xFF, 0x00,
    0x95, 0x06, 0x75, 0x08, 0x81, 0x00,             // keys
    0xC0};

static const uint8_t ext_rep[] = {
    // Consumer: 2 usages, 16 bit each
    0x05, 0x0C, 0x09, 0x01, 0xA1, 0x01, 0x85, USB_ID_CONSUMER,
    0x15, 0x00, 0x26, 0xFF, 0x03, 0x19, 0x00, 0x2A, 0xFF, 0x03,
    0x75, 0x10, 0x95, USB_CONSUMER_SLOTS, 0x81, 0x00, 0xC0,
    // System control: power, sleep, wake
    0x05, 0x01, 0x09, 0x80, 0xA1, 0x01, 0x85, USB_ID_SYSTEM,
    0x19, 0x81, 0x29, 0x83, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x03, 0x81, 0x02,
    0x95, 0x05, 0x81, 0x01, 0xC0,
    // Mouse: 8 buttons, x, y (16 bit), wheel, pan (8 bit)
    0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x85, USB_ID_MOUSE, 0x09, 0x01, 0xA1, 0x00,
    0x05, 0x09, 0x19, 0x01, 0x29, 0x08, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02,
    0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x16, 0x01, 0x80, 0x26, 0xFF, 0x7F,
    0x75, 0x10, 0x95, 0x02, 0x81, 0x06,
    0x09, 0x38, 0x15, 0x81, 0x25, 0x7F, 0x75, 0x08, 0x95, 0x01, 0x81, 0x06,
    0x05, 0x0C, 0x0A, 0x38, 0x02, 0x95, 0x01, 0x81, 0x06,
    0xC0, 0xC0};

#define CFG_LEN (9 + 2 * (9 + 9 + 7))
static const uint8_t cfg_desc[CFG_LEN] = {
    9, 2, CFG_LEN, 0, 2, 1, 0, 0x80, 50,                    // 100 mA, no remote wakeup
    9, 4, 0, 0, 1, 3, 1, 1, 0,                               // interface 0: boot keyboard
    9, 0x21, 0x11, 0x01, 0, 1, 0x22, sizeof(kb_rep), 0,
    7, 5, 0x81, 3, USB_KB_LEN, 0, 1,                         // EP1 IN, 1 ms
    9, 4, 1, 0, 1, 3, 0, 0, 0,                               // interface 1: report IDs
    9, 0x21, 0x11, 0x01, 0, 1, 0x22, sizeof(ext_rep), 0,
    7, 5, 0x82, 3, USB_EXT_MAX, 0, 1};                       // EP2 IN, 1 ms

static const uint8_t lang_desc[4] = {4, 3, 0x09, 0x04};
static uint8_t manu_desc[2 + 2 * 16];
static uint8_t prod_desc[2 + 2 * 24];

static void make_string(uint8_t *d, uint8_t max, const char *s) {
    uint8_t n = 0;
    while (s[n] && n < max) {
        d[2 + 2 * n] = (uint8_t)s[n];
        d[3 + 2 * n] = 0;
        n++;
    }
    d[0] = 2 + 2 * n;
    d[1] = 3;
}

static void port_start(port_t *p, uint8_t second) {
    R8(p, O_CTRL) = 0;
    R8(p, O_EP41_MOD) = RB_UEP1_RX_EN | RB_UEP1_TX_EN;
    R8(p, O_EP23_MOD) = RB_UEP2_RX_EN | RB_UEP2_TX_EN;
    R16(p, O_EP0_DMA) = (uint16_t)(uint32_t)p->ep0;
    R16(p, O_EP1_DMA) = (uint16_t)(uint32_t)p->ep1;
    R16(p, O_EP2_DMA) = (uint16_t)(uint32_t)p->ep2;
    R8(p, O_EP0_CTRL) = UEP_R_RES_ACK | UEP_T_RES_NAK;
    R8(p, O_EP1_CTRL) = UEP_R_RES_ACK | UEP_T_RES_NAK | RB_UEP_AUTO_TOG;
    R8(p, O_EP2_CTRL) = UEP_R_RES_ACK | UEP_T_RES_NAK | RB_UEP_AUTO_TOG;
    R8(p, O_DEV_AD) = 0;
    R8(p, O_CTRL) = RB_UC_DEV_PU_EN | RB_UC_INT_BUSY | RB_UC_DMA_EN;
    R16_PIN_ANALOG_IE |= second ? (RB_PIN_USB2_IE | RB_PIN_USB2_DP_PU) : (RB_PIN_USB_IE | RB_PIN_USB_DP_PU);
    R8(p, O_INT_FG) = 0xFF;
    R8(p, O_DEV_CTRL) = RB_UD_PD_DIS | RB_UD_PORT_EN;
    R8(p, O_INT_EN) = RB_UIE_SUSPEND | RB_UIE_BUS_RST | RB_UIE_TRANSFER;
}

static void port_stop(port_t *p, uint8_t second) {
    PFIC_DisableIRQ(second ? USB2_IRQn : USB_IRQn);
    R8(p, O_INT_EN) = 0;
    R8(p, O_CTRL) = 0;      // pull-up off
    R8(p, O_DEV_CTRL) = 0;  // port I/O off
    R16_PIN_ANALOG_IE &= ~(second ? (RB_PIN_USB2_IE | RB_PIN_USB2_DP_PU) : (RB_PIN_USB_IE | RB_PIN_USB_DP_PU));
}

void usb_hid_init(const char *product) {
    make_string(manu_desc, 16, "MacroKeyboard");
    make_string(prod_desc, 24, product);
    port_start(&ports[0], 0);
    port_start(&ports[1], 1);
    PFIC_EnableIRQ(USB_IRQn);
    PFIC_EnableIRQ(USB2_IRQn);
}

uint8_t usb_hid_ready(void) { port_t *p = active; return p && p->config; }
uint8_t usb_hid_port(void) { return active == &ports[0] ? 1 : active == &ports[1] ? 2 : 0; }
uint8_t usb_suspended(void) { return suspended; }
uint8_t usb_leds(void) { return leds; }

uint8_t usb_leds_take(void) {
    uint8_t n = leds_new;
    leds_new = 0;
    return n;
}

uint8_t usb_kb_busy(void) {
    port_t *p = active;
    return !p || !p->config || p->kb_busy;
}

uint8_t usb_ext_busy(void) {
    port_t *p = active;
    return !p || !p->config || p->ext_busy;
}

uint8_t usb_kb_send(const uint8_t *r) {
    port_t *p = active;
    if (!p || !p->config || p->kb_busy) return 0;
    memcpy(p->ep1 + 64, r, USB_KB_LEN);
    R8(p, O_EP1_TLEN) = USB_KB_LEN;
    p->kb_busy = 1;
    R8(p, O_EP1_CTRL) = (R8(p, O_EP1_CTRL) & ~MASK_UEP_T_RES) | UEP_T_RES_ACK;
    return 1;
}

uint8_t usb_ext_send(const uint8_t *r, uint8_t len) {
    port_t *p = active;
    if (!p || !p->config || p->ext_busy || len > USB_EXT_MAX) return 0;
    memcpy(p->ep2 + 64, r, len);
    R8(p, O_EP2_TLEN) = len;
    p->ext_busy = 1;
    R8(p, O_EP2_CTRL) = (R8(p, O_EP2_CTRL) & ~MASK_UEP_T_RES) | UEP_T_RES_ACK;
    return 1;
}

static uint8_t report_len(uint8_t intf, uint8_t id) {
    if (intf == 0) return USB_KB_LEN;
    return id == USB_ID_CONSUMER ? USB_CONSUMER_LEN : id == USB_ID_MOUSE ? USB_MOUSE_LEN
         : id == USB_ID_SYSTEM ? USB_SYSTEM_LEN : 0;
}

static void ep0_chunk(port_t *p) {
    uint8_t n = p->left > EP0_SIZE ? EP0_SIZE : (uint8_t)p->left;
    if (p->src) {
        memcpy(p->ep0, p->src, n);
        p->src += n;
    }
    p->left -= n;
    R8(p, O_EP0_TLEN) = n;
}

static void ep_reset(port_t *p) {
    R8(p, O_EP1_CTRL) = UEP_R_RES_ACK | UEP_T_RES_NAK | RB_UEP_AUTO_TOG;
    R8(p, O_EP2_CTRL) = UEP_R_RES_ACK | UEP_T_RES_NAK | RB_UEP_AUTO_TOG;
    p->kb_busy = p->ext_busy = 0;
}

__HIGH_CODE static void do_setup(port_t *p) {
    uint8_t type = p->ep0[0], req = p->ep0[1];
    uint16_t value = p->ep0[2] | (p->ep0[3] << 8);
    uint16_t index = p->ep0[4] | (p->ep0[5] << 8);
    uint16_t want = p->ep0[6] | (p->ep0[7] << 8);
    uint8_t intf = index & 0xFF;
    const uint8_t *src = 0;
    uint16_t len = 0;
    uint8_t err = 0;

    R8(p, O_EP0_CTRL) = RB_UEP_R_TOG | RB_UEP_T_TOG | UEP_R_RES_ACK | UEP_T_RES_NAK;
    p->req = req;
    p->dir_in = type & 0x80;
    if ((type & USB_REQ_TYP_MASK) == USB_REQ_TYP_CLASS) {
        if (intf > 1) {
            err = 1;
        } else if (req == 0x0A) { // SET_IDLE
            p->idle[intf] = value >> 8;
        } else if (req == 0x0B) { // SET_PROTOCOL
            p->proto[intf] = value & 0xFF;
        } else if (req == 0x02) { // GET_IDLE
            p->ep0[0] = p->idle[intf];
            len = 1;
        } else if (req == 0x03) { // GET_PROTOCOL
            p->ep0[0] = p->proto[intf];
            len = 1;
        } else if (req == 0x01) { // GET_REPORT: all released
            len = report_len(intf, value & 0xFF);
            memset(p->ep0, 0, len);
            if (intf == 1) p->ep0[0] = value & 0xFF;
            err = len == 0;
        } else if (req != 0x09) { // SET_REPORT data comes in the OUT stage
            err = 1;
        }
    } else if ((type & USB_REQ_TYP_MASK) != USB_REQ_TYP_STANDARD) {
        err = 1;
    } else {
        switch (req) {
        case USB_GET_DESCRIPTOR:
            switch (value >> 8) {
            case USB_DESCR_TYP_DEVICE: src = dev_desc; len = sizeof(dev_desc); break;
            case USB_DESCR_TYP_CONFIG: src = cfg_desc; len = sizeof(cfg_desc); break;
            case USB_DESCR_TYP_STRING:
                switch (value & 0xFF) {
                case 0: src = lang_desc; break;
                case 1: src = manu_desc; break;
                case 2: src = prod_desc; break;
                default: err = 1;
                }
                if (src) len = src[0];
                break;
            case USB_DESCR_TYP_HID:
                if (intf > 1) err = 1;
                else src = cfg_desc + (intf ? 9 + 25 + 9 : 9 + 9), len = 9;
                break;
            case USB_DESCR_TYP_REPORT:
                if (intf == 0) src = kb_rep, len = sizeof(kb_rep);
                else if (intf == 1) src = ext_rep, len = sizeof(ext_rep);
                else err = 1;
                break;
            default: err = 1;
            }
            break;
        case USB_SET_ADDRESS: p->addr = value & 0x7F; break;
        case USB_GET_CONFIGURATION: p->ep0[0] = p->config; len = 1; break;
        case USB_SET_CONFIGURATION:
            p->config = value & 0xFF;
            ep_reset(p);
            if (p->config && !active) {
                port_t *other = p == &ports[0] ? &ports[1] : &ports[0];
                port_stop(other, other == &ports[1]);
                active = p;
            }
            break;
        case USB_GET_INTERFACE: p->ep0[0] = 0; len = 1; break;
        case USB_SET_INTERFACE: break;
        case USB_CLEAR_FEATURE:
            if ((type & USB_REQ_RECIP_MASK) == USB_REQ_RECIP_ENDP) {
                if ((index & 0xFF) == 0x81) {
                    R8(p, O_EP1_CTRL) = (R8(p, O_EP1_CTRL) & ~(RB_UEP_T_TOG | MASK_UEP_T_RES)) | UEP_T_RES_NAK;
                    p->kb_busy = 0;
                } else if ((index & 0xFF) == 0x82) {
                    R8(p, O_EP2_CTRL) = (R8(p, O_EP2_CTRL) & ~(RB_UEP_T_TOG | MASK_UEP_T_RES)) | UEP_T_RES_NAK;
                    p->ext_busy = 0;
                }
            }
            break;
        case USB_SET_FEATURE: break;
        case USB_GET_STATUS: p->ep0[0] = 0; p->ep0[1] = 0; len = 2; break;
        default: err = 1;
        }
    }
    if (err) {
        R8(p, O_EP0_CTRL) = RB_UEP_R_TOG | RB_UEP_T_TOG | UEP_R_RES_STALL | UEP_T_RES_STALL;
        return;
    }
    if (p->dir_in) {
        p->left = len < want ? len : want;
        p->src = src;
        ep0_chunk(p);
    } else {
        R8(p, O_EP0_TLEN) = 0; // status stage
    }
    R8(p, O_EP0_CTRL) = RB_UEP_R_TOG | RB_UEP_T_TOG | UEP_R_RES_ACK | UEP_T_RES_ACK;
}

__HIGH_CODE static void usb_isr(port_t *p) {
    uint8_t fg = R8(p, O_INT_FG);
    if (fg & RB_UIF_TRANSFER) {
        uint8_t st = R8(p, O_INT_ST);
        if ((st & MASK_UIS_TOKEN) != MASK_UIS_TOKEN) {
            switch (st & (MASK_UIS_TOKEN | MASK_UIS_ENDP)) {
            case UIS_TOKEN_IN:
                if (p->dir_in) {
                    ep0_chunk(p);
                    R8(p, O_EP0_CTRL) ^= RB_UEP_T_TOG;
                } else {
                    if (p->req == USB_SET_ADDRESS)
                        R8(p, O_DEV_AD) = (R8(p, O_DEV_AD) & RB_UDA_GP_BIT) | p->addr;
                    R8(p, O_EP0_TLEN) = 0;
                    R8(p, O_EP0_CTRL) = UEP_R_RES_ACK | UEP_T_RES_NAK;
                }
                break;
            case UIS_TOKEN_OUT:
                if (!p->dir_in && p->req == 0x09 && R8(p, O_RX_LEN) >= 1) {
                    leds = p->ep0[0];
                    leds_new = 1;
                }
                break;
            case UIS_TOKEN_IN | 1:
                R8(p, O_EP1_CTRL) = (R8(p, O_EP1_CTRL) & ~MASK_UEP_T_RES) | UEP_T_RES_NAK;
                p->kb_busy = 0;
                break;
            case UIS_TOKEN_IN | 2:
                R8(p, O_EP2_CTRL) = (R8(p, O_EP2_CTRL) & ~MASK_UEP_T_RES) | UEP_T_RES_NAK;
                p->ext_busy = 0;
                break;
            default:
                break;
            }
            R8(p, O_INT_FG) = RB_UIF_TRANSFER;
        }
        if (R8(p, O_INT_ST) & RB_UIS_SETUP_ACT) {
            do_setup(p);
            R8(p, O_INT_FG) = RB_UIF_TRANSFER;
        }
    } else if (fg & RB_UIF_BUS_RST) {
        R8(p, O_DEV_AD) = 0;
        R8(p, O_EP0_CTRL) = UEP_R_RES_ACK | UEP_T_RES_NAK;
        ep_reset(p);
        p->config = 0;
        R8(p, O_INT_FG) = RB_UIF_BUS_RST;
    } else if (fg & RB_UIF_SUSPEND) {
        suspended = (R8(p, O_MIS_ST) & RB_UMS_SUSPEND) ? 1 : 0;
        R8(p, O_INT_FG) = RB_UIF_SUSPEND;
    } else {
        R8(p, O_INT_FG) = fg;
    }
}

__INTERRUPT __HIGH_CODE void USB_IRQHandler(void) { usb_isr(&ports[0]); }
__INTERRUPT __HIGH_CODE void USB2_IRQHandler(void) { usb_isr(&ports[1]); }
