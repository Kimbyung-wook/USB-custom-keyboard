// CH582F BLE HID dongle: BLE keyboard -> USB HID keyboard.
// Start order: clock, replug counter, 32 kHz clock choice, BLE library,
// central role, USB, then the TMOS loop with a 10 ms app tick.
#include "CH58x_common.h"
#include "CH58xBLE_LIB.h"
#include "board.h"
#include "ble_host.h"
#include "store.h"
#include "usb_hid.h"
#include "core/link.h"
#include "core/rq.h"
#include <string.h>

#define BLE_MEM_SIZE (6 * 1024)
#define CAL_PERIOD_MS 120000
#define EV_TICK 0x0001
#define EV_CAL 0x0002

__attribute__((aligned(4))) static uint32_t ble_mem[BLE_MEM_SIZE / 4];

static uint8_t app;
static uint8_t xt32k;       // 1 = external 32.768 kHz crystal found
static uint32_t now_ms;
static uint8_t replug_done;
static link_t lk;
static rq_t kbq, extq;

// ---- pins -------------------------------------------------------------------
#if BOARD_LED_PIN >= 0 || BOARD_BTN_PIN >= 0
#define PIN_BIT(p) (1u << ((p) & 31))
static void pin_out(int p, uint8_t v) {
    if (p < 32) v ? GPIOA_SetBits(PIN_BIT(p)) : GPIOA_ResetBits(PIN_BIT(p));
    else v ? GPIOB_SetBits(PIN_BIT(p)) : GPIOB_ResetBits(PIN_BIT(p));
}
#endif

static void pins_init(void) {
#if BOARD_LED_PIN >= 0
    pin_out(BOARD_LED_PIN, !BOARD_LED_ON);
    if (BOARD_LED_PIN < 32) GPIOA_ModeCfg(PIN_BIT(BOARD_LED_PIN), GPIO_ModeOut_PP_5mA);
    else GPIOB_ModeCfg(PIN_BIT(BOARD_LED_PIN), GPIO_ModeOut_PP_5mA);
#endif
#if BOARD_BTN_PIN >= 0
    if (BOARD_BTN_PIN < 32) GPIOA_ModeCfg(PIN_BIT(BOARD_BTN_PIN), GPIO_ModeIN_PU);
    else GPIOB_ModeCfg(PIN_BIT(BOARD_BTN_PIN), GPIO_ModeIN_PU);
#endif
}

static uint8_t button_raw(void) {
#if BOARD_BTN_PIN >= 0
    uint32_t v = BOARD_BTN_PIN < 32 ? GPIOA_ReadPortPin(PIN_BIT(BOARD_BTN_PIN))
                                    : GPIOB_ReadPortPin(PIN_BIT(BOARD_BTN_PIN));
    return v == 0;
#else
    return 0;
#endif
}

static void led_set(uint8_t on) {
#if BOARD_LED_PIN >= 0
    pin_out(BOARD_LED_PIN, on ? BOARD_LED_ON : !BOARD_LED_ON);
#else
    (void)on;
#endif
}

// ---- 32 kHz clock -------------------------------------------------------------
// The crystal counts ~3277 RTC ticks in 100 ms; without one it stays near 0
static uint8_t detect_xt32k(void) {
    sys_safe_access_enable();
    R8_CK32K_CONFIG |= RB_CLK_XT32K_PON | RB_CLK_INT32K_PON | RB_CLK_OSC32K_XT;
    sys_safe_access_disable();
    mDelaymS(500); // crystal start-up
    uint32_t a = R32_RTC_CNT_32K;
    mDelaymS(100);
    uint32_t n = R32_RTC_CNT_32K - a;
    return n > 2900 && n < 3700;
}

static void clock32k_init(void) {
    if (xt32k) {
        sys_safe_access_enable();
        R8_CK32K_CONFIG |= RB_CLK_OSC32K_XT | RB_CLK_INT32K_PON | RB_CLK_XT32K_PON;
        sys_safe_access_disable();
    } else {
        sys_safe_access_enable();
        R8_CK32K_CONFIG &= ~(RB_CLK_OSC32K_XT | RB_CLK_XT32K_PON);
        sys_safe_access_disable();
        sys_safe_access_enable();
        R8_CK32K_CONFIG |= RB_CLK_INT32K_PON;
        sys_safe_access_disable();
        LSECFG_Current(LSE_RCur_100);
        Calibration_LSI(Level_64);
    }
    RTC_InitTime(2020, 1, 1, 0, 0, 0);
    TMOS_TimerInit(0);
}

// Same as the SDK HAL (RTC.c): the startup's default handler would hang if the
// library enables the RTC interrupt. BB/LLE interrupts are installed by
// BLE_LibInit as fast vectors (PFIC VTF), not through the vector table.
__INTERRUPT __HIGH_CODE void RTC_IRQHandler(void) {
    R8_RTC_FLAG_CTRL = (RB_RTC_TMR_CLR | RB_RTC_TRIG_CLR);
}

// ---- BLE library ---------------------------------------------------------------
static uint32_t flash_read(uint32_t addr, uint32_t num, uint32_t *buf) {
    EEPROM_READ(addr, buf, num * 4);
    return 0;
}

static uint32_t flash_write(uint32_t addr, uint32_t num, uint32_t *buf) {
    EEPROM_ERASE(addr, num * 4);
    EEPROM_WRITE(addr, buf, num * 4);
    return 0;
}

static void lsi_cal(void) { Calibration_LSI(Level_64); }

static uint16_t temp_value(void) {
    uint8_t tkey = R8_TKEY_CFG, sensor = R8_TEM_SENSOR, ch = R8_ADC_CHANNEL, cfg = R8_ADC_CFG;
    ADC_InterTSSampInit();
    R8_ADC_CONVERT |= RB_ADC_START;
    while (R8_ADC_CONVERT & RB_ADC_START);
    uint16_t v = R16_ADC_DATA;
    R8_TEM_SENSOR = sensor;
    R8_ADC_CHANNEL = ch;
    R8_ADC_CFG = cfg;
    R8_TKEY_CFG = tkey;
    return v;
}

static void ble_init(void) {
    bleConfig_t cfg;
    SysTick_Config(SysTick_LOAD_RELOAD_Msk);
    PFIC_DisableIRQ(SysTick_IRQn);
    memset(&cfg, 0, sizeof(cfg));
    cfg.MEMAddr = (uint32_t)ble_mem;
    cfg.MEMLen = BLE_MEM_SIZE;
    cfg.BufMaxLen = 27;
    cfg.BufNumber = 5;
    cfg.TxNumEvent = 1;
    cfg.TxPower = LL_TX_POWEER_4_DBM;
    cfg.SNVAddr = 0x77E00 - FLASH_ROM_MAX_SIZE; // bond records, data flash 0x7E00
    cfg.SNVBlock = 256;
    cfg.SNVNum = 1;
    cfg.readFlashCB = flash_read;
    cfg.writeFlashCB = flash_write;
    cfg.SelRTCClock = xt32k ? 0 : 1;
    cfg.ConnectNumber = (0 & 3) | (1 << 2); // no peripheral, 1 central link
    cfg.srandCB = SYS_GetSysTickCnt;
    cfg.tsCB = temp_value;
    if (!xt32k) cfg.rcCB = lsi_cal;
    GetMACAddress(cfg.MacAddr);
    if (BLE_LibInit(&cfg)) while (1);
}

// ---- app ------------------------------------------------------------------------
static void emit(void *ctx, uint8_t iface, const uint8_t *r, uint8_t len) {
    (void)ctx;
    rq_push(iface == CONV_IF_KB ? &kbq : &extq, r, len);
}

static void on_link_event(uint8_t ev) { link_event(&lk, ev); }

static uint8_t btn_cnt, btn_state;

static void tick(void) {
    now_ms += 10;
    uint8_t raw = button_raw(); // 30 ms debounce
    if (raw != btn_state) {
        if (++btn_cnt >= 3) {
            btn_state = raw;
            btn_cnt = 0;
        }
    } else btn_cnt = 0;

    link_tick(&lk, now_ms, btn_state);
    if (link_take(&lk, LKA_ERASE)) bh_erase_all();
    link_take(&lk, LKA_SAVE); // ble_host saves the new bond itself
    bh_poll(link_target(&lk));
    led_set(link_led(&lk));
    if (usb_leds_take()) bh_set_leds(usb_leds());
    if (!replug_done && now_ms >= 3000) { // stayed plugged in: not a quick replug
        replug_done = 1;
        store_replug_reset();
    }
}

static tmosEvents app_task(tmosTaskID id, tmosEvents ev) {
    if (ev & SYS_EVENT_MSG) {
        uint8_t *m = tmos_msg_receive(app);
        if (m) tmos_msg_deallocate(m);
        return ev ^ SYS_EVENT_MSG;
    }
    if (ev & EV_TICK) {
        tmos_start_task(app, EV_TICK, MS1_TO_SYSTEM_TIME(10));
        tick();
        return ev ^ EV_TICK;
    }
    if (ev & EV_CAL) { // RF and clock calibration (< 10 ms)
        BLE_RegInit();
        if (!xt32k) lsi_cal();
        tmos_start_task(app, EV_CAL, MS1_TO_SYSTEM_TIME(CAL_PERIOD_MS));
        return ev ^ EV_CAL;
    }
    return 0;
}

static void usb_pump(void) {
    if (!usb_hid_ready()) return;
    uint8_t len;
    const uint8_t *r;
    if (!usb_kb_busy() && (r = rq_peek(&kbq, &len)) && usb_kb_send(r)) rq_pop(&kbq);
    if (!usb_ext_busy() && (r = rq_peek(&extq, &len)) && usb_ext_send(r, len)) rq_pop(&extq);
}

__HIGH_CODE __attribute__((noinline)) static void loop(void) {
    for (;;) {
        TMOS_SystemProcess();
        usb_pump();
    }
}

int main(void) {
    SetSysClock(CLK_SOURCE_PLL_60MHz);
    pins_init();
    led_set(1);
    uint8_t replugs = store_replug_count();
    rq_init(&kbq);
    rq_init(&extq);
    usb_hid_init(BOARD_PRODUCT);
    xt32k = detect_xt32k();
    ble_init();
    clock32k_init();
    app = TMOS_ProcessEventRegister(app_task);
    GAPRole_CentralInit();
    bh_init(emit, on_link_event);
    link_init(&lk, bh_has_bond(), replugs, 0);
    tmos_set_event(app, EV_TICK);
    tmos_start_task(app, EV_CAL, MS1_TO_SYSTEM_TIME(500));
    loop();
}
