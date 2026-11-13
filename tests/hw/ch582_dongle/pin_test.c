// CH582F dongle pin test: types what it finds as a USB keyboard.
//   start : chip/USB/MAC line, 32 kHz crystal check, pins that read low with
//           pull-up ("PU LOW") and high with pull-down ("PD HIGH")
//   loop  : each GPIO in turn: "<pin> " then 1 s high ("H"), 1 s low ("L").
//           An active-high LED lights on H, an active-low LED on L
//   always: a pin that goes low while not driven = button: "BTN <pin>" / "BTN <pin> up"
//   Scroll Lock on (PC keyboard) = keep repeating the current pin
// Not touched: the USB port the PC uses (PB10/PB11 or PB12/PB13), PB23 (may be RST).
// Pins missing on the package do nothing.
#include "CH58x_common.h"
#include "usb_hid.h"
#include "core/rq.h"

typedef struct {
    uint8_t port; // 0 PA, 1 PB
    uint8_t bit;
} pin_t;

static rq_t q;
static pin_t pins[40];
static uint8_t npins;
static uint8_t stuck[40];   // read low with pull-up at start
static uint8_t btn[40];     // debounced pressed
static uint8_t cnt[40];     // debounce counter
static int8_t driven = -1;  // pin being driven, not scanned

static void pin_name(pin_t p, char *s) {
    s[0] = 'P';
    s[1] = p.port ? 'B' : 'A';
    s[2] = '0' + p.bit / 10;
    s[3] = '0' + p.bit % 10;
    s[4] = 0;
}

static void pin_mode(pin_t p, GPIOModeTypeDef m) {
    if (p.port) GPIOB_ModeCfg(1u << p.bit, m);
    else GPIOA_ModeCfg(1u << p.bit, m);
}

static void pin_set(pin_t p, uint8_t v) {
    if (p.port) { if (v) GPIOB_SetBits(1u << p.bit); else GPIOB_ResetBits(1u << p.bit); }
    else { if (v) GPIOA_SetBits(1u << p.bit); else GPIOA_ResetBits(1u << p.bit); }
}

static uint8_t pin_read(pin_t p) {
    return p.port ? (GPIOB_ReadPortPin(1u << p.bit) != 0) : (GPIOA_ReadPortPin(1u << p.bit) != 0);
}

// ---- typing ----------------------------------------------------------------
static void key_for(char c, uint8_t *mod, uint8_t *key) {
    *mod = 0;
    *key = 0;
    if (c >= 'a' && c <= 'z') *key = 0x04 + c - 'a';
    else if (c >= 'A' && c <= 'Z') *mod = 0x02, *key = 0x04 + c - 'A';
    else if (c >= '1' && c <= '9') *key = 0x1E + c - '1';
    else if (c == '0') *key = 0x27;
    else if (c == '\n') *key = 0x28;
    else if (c == ' ') *key = 0x2C;
    else if (c == '-') *key = 0x2D;
    else if (c == '=') *key = 0x2E;
    else if (c == '.') *key = 0x37;
    else if (c == '/') *key = 0x38;
    else if (c == ':') *mod = 0x02, *key = 0x33;
    else if (c == '*') *mod = 0x02, *key = 0x25;
}

static void pump(void) {
    uint8_t len;
    const uint8_t *r = rq_peek(&q, &len);
    if (r && usb_kb_send(r)) rq_pop(&q);
}

static void scan_buttons(void);

static void wait_ms(uint16_t ms) {
    while (ms--) {
        mDelayuS(1000);
        pump();
        scan_buttons();
    }
}

static void type_char(char c) {
    uint8_t r[8] = {0}, mod, key;
    key_for(c, &mod, &key);
    if (!key) return;
    while (rq_count(&q) > RQ_SLOTS - 4) {
        mDelayuS(1000);
        pump();
    }
    r[0] = mod;
    r[2] = key;
    rq_push(&q, r, 8);
    r[0] = r[2] = 0;
    rq_push(&q, r, 8);
}

static void type_str(const char *s) {
    while (*s) type_char(*s++);
}

static void type_num(uint32_t v) {
    char b[11];
    int i = 10;
    b[i] = 0;
    do {
        b[--i] = '0' + v % 10;
        v /= 10;
    } while (v);
    type_str(b + i);
}

static void type_hex8(uint8_t v) {
    static const char h[] = "0123456789ABCDEF";
    type_char(h[v >> 4]);
    type_char(h[v & 15]);
}

static void type_pin(uint8_t i) {
    char s[5];
    pin_name(pins[i], s);
    type_str(s);
}

// ---- buttons ----------------------------------------------------------------
static void scan_buttons(void) {
    static uint8_t div;
    if (++div < 10) return; // every 10 ms
    div = 0;
    for (uint8_t i = 0; i < npins; i++) {
        if ((int8_t)i == driven || stuck[i]) continue;
        uint8_t low = !pin_read(pins[i]);
        if (low != btn[i]) {
            if (++cnt[i] >= 3) {
                btn[i] = low;
                cnt[i] = 0;
                type_str(" BTN ");
                type_pin(i);
                type_str(low ? "\n" : " up\n");
            }
        } else {
            cnt[i] = 0;
        }
    }
}

// ---- 32 kHz crystal: RTC ticks in 100 ms with the crystal selected ---------
static uint32_t rtc_ticks_100ms(uint8_t xt) {
    sys_safe_access_enable();
    if (xt) R8_CK32K_CONFIG |= RB_CLK_XT32K_PON | RB_CLK_INT32K_PON | RB_CLK_OSC32K_XT;
    else R8_CK32K_CONFIG = (R8_CK32K_CONFIG & ~RB_CLK_OSC32K_XT) | RB_CLK_INT32K_PON;
    sys_safe_access_disable();
    wait_ms(xt ? 500 : 20); // crystal start-up
    uint32_t a = R32_RTC_CNT_32K;
    wait_ms(100);
    return R32_RTC_CNT_32K - a;
}

int main(void) {
    SetSysClock(CLK_SOURCE_PLL_60MHz);
    uint8_t rst = R8_RESET_STATUS & RB_RESET_FLAG;
    rq_init(&q);
    usb_hid_init("CH582 Pin Test");
    while (!usb_hid_ready()) mDelayuS(1000);
    mDelaymS(1500); // let the PC open the keyboard

    uint8_t usb = usb_hid_port();
    for (uint8_t port = 0; port < 2; port++) {
        for (uint8_t bit = 0; bit < (port ? 24 : 16); bit++) {
            if (port == 1 && bit == 23) continue;                           // RST
            if (port == 1 && usb == 1 && (bit == 10 || bit == 11)) continue; // USB1
            if (port == 1 && usb == 2 && (bit == 12 || bit == 13)) continue; // USB2
            pins[npins].port = port;
            pins[npins].bit = bit;
            npins++;
        }
    }

    type_str("\nCH582 PIN TEST USB");
    type_num(usb);
    type_str(" RST");
    type_num(rst);
    uint8_t mac[6];
    GetMACAddress(mac);
    type_str(" MAC ");
    for (int8_t i = 5; i >= 0; i--) {
        type_hex8(mac[i]);
        if (i) type_char(':');
    }
    type_str("\n");

    uint32_t rc = rtc_ticks_100ms(0);
    uint32_t xt = rtc_ticks_100ms(1);
    rtc_ticks_100ms(0); // back to RC
    type_str("RC32K ");
    type_num(rc);
    type_str(" X32K ");
    type_num(xt);
    type_str(xt > 2000 ? " OK\n" : " NONE\n");

    for (uint8_t i = 0; i < npins; i++) pin_mode(pins[i], GPIO_ModeIN_PU);
    wait_ms(20);
    type_str("PU LOW:");
    for (uint8_t i = 0; i < npins; i++)
        if (!pin_read(pins[i])) {
            stuck[i] = 1;
            type_char(' ');
            type_pin(i);
        }
    type_str("\n");
    for (uint8_t i = 0; i < npins; i++) pin_mode(pins[i], GPIO_ModeIN_PD);
    wait_ms(20);
    type_str("PD HIGH:");
    for (uint8_t i = 0; i < npins; i++)
        if (pin_read(pins[i])) {
            type_char(' ');
            type_pin(i);
        }
    type_str("\n");
    for (uint8_t i = 0; i < npins; i++) pin_mode(pins[i], GPIO_ModeIN_PU);
    wait_ms(20);

    for (uint8_t i = 0;;) {
        driven = i;
        type_pin(i);
        if (stuck[i]) type_char('*');
        type_char(' ');
        pin_set(pins[i], 1);
        pin_mode(pins[i], GPIO_ModeOut_PP_5mA);
        type_char('H');
        wait_ms(1000);
        pin_set(pins[i], 0);
        type_char('L');
        wait_ms(1000);
        pin_mode(pins[i], GPIO_ModeIN_PU);
        type_char('\n');
        wait_ms(30);
        driven = -1;
        if (!(usb_leds() & 0x04)) { // Scroll Lock off: next pin
            if (++i >= npins) {
                i = 0;
                type_str("LOOP\n");
            }
        }
    }
}
