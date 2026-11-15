// UNVERIFIED DRAFT (2026-11-15): links into the SDK app (sdk_setup.sh), never run.
// SDK: public fw-AC63_BT_SDK (C:\Users\user\tools\sdk\fw-AC63_BT_SDK, commit
// fdc018de), platform cpu/bd19 = AC632N family, which the AC6321A belongs to
// (adc_api.h channels match the AC6321A datasheet). Every SDK call goes through
// one of the SDK_* wrappers in the "SDK glue" block: fix them there only.
// "header" = the declaration is in that bd19 header; TODO(SDK) = still a guess.
//
// HAL for the shared pin test logic (test_ch32v203.c) on the JieLi AC6321A.
// The SDK owns main(): its app entry starts a task that runs test_main() (the
// shared logic's main, renamed by ac6321a_hal.h). The shared loop calls
// hal_idle() once per pass, which feeds the watchdog and yields to the RTOS.
#include "ac6321a_hal.h"
#include "ac6321a_pins.h" // PORT_USB1 (the pin table itself is the shared logic's)

// ---- SDK glue: every name of the JieLi SDK is here ---------------------------
#include "asm/gpio.h"     // header: include_lib/driver/cpu/bd19/asm/gpio.h
#include "asm/adc_api.h"  // header: include_lib/driver/cpu/bd19/asm/adc_api.h
#include "asm/power_interface.h" // header: get_reset_source_value()
#include "system/timer.h" // header: sys_timer_get_ms()
#include "asm/cpu.h"
// bd19 asm/cpu.h declares clr_wdt() only inside a macro (cpu.h:44), the
// library has it (apps/hid/app_main.c calls it the same way).
extern void clr_wdt(void);
#include "usb/device/usb_stack.h" // include_lib/driver/device/usb/device: usb_start()
#include "asm/power/p33.h" // header: VCM_DET_EN()
#include "system/boot.h"  // header: boot_info (flash size, chip id from the ROM UBOOT)
#include "os/os_api.h"    // header: os_time_dly(), os_task_create()
#include "system/app_core.h" // header: REGISTER_APPLICATION, struct application
#include "app_action.h"     // apps/hid/include: ACTION_STANDARD_KEYBOARD

// header: IO_GROUP_NUM 16, IO_PORTA_00 = 0, IO_PORTB_00 = 16 (PA0-9, PB0-9,
// PD0-4; no port C). The second USB pair: IO_PORT_DP1, IO_PORT_DM1 = DP1 + 1,
// GPIO after usb1_iomode(1)
static uint32_t SDK_GPIO(uint8_t port, uint8_t bit) {
  if (port == PORT_USB1) {
    return IO_PORT_DP1 + bit; // bit 0 = USB1DP (29), 1 = USB1DM (28)
  }
  return (uint32_t)port * IO_GROUP_NUM + bit;
}

// header: gpio_set_die 1 = digital input (0 = analog), gpio_set_direction 1 = in
static void SDK_gpio_input(uint32_t gpio, uint8_t up, uint8_t down) {
  gpio_set_die(gpio, 1);
  gpio_set_pull_up(gpio, up);
  gpio_set_pull_down(gpio, down);
  gpio_set_direction(gpio, 1);
}

static void SDK_gpio_output(uint32_t gpio) {
  gpio_set_pull_up(gpio, 0);
  gpio_set_pull_down(gpio, 0);
  gpio_set_hd(gpio, 0);  // header; TODO(SDK): 0 = normal drive? (gpio_set_hd0 too)
  gpio_set_direction(gpio, 0);
}

static void SDK_gpio_write(uint32_t gpio, uint8_t high) {
  gpio_set_output_value(gpio, high); // header
}

static uint8_t SDK_gpio_read(uint32_t gpio) {
  return gpio_read(gpio) ? 1 : 0;    // header
}

// header: os_time_dly(ticks), not from an ISR. 1 tick = 10 ms (SDK clone:
// FreeRTOSConfig.h configTICK_RATE_HZ 100)
static void SDK_yield_tick(void) {
  os_time_dly(1);
}

// SDK: extern void clr_wdt() in include_lib/driver/cpu/bd19/asm/cpu.h
static void SDK_watchdog(void) {
  clr_wdt();
}

// header (system/timer.h): u32 sys_timer_get_ms(void). TODO(SDK): its step
static uint32_t SDK_ms_now(void) {
  return sys_timer_get_ms();
}

// Busy wait of about 1 ms: no udelay/mdelay in the headers read, so spin on
// the ms count (feeding the watchdog). TODO(SDK): a us delay if the SDK has one
static void SDK_busy_ms(void) {
  uint32_t t0 = sys_timer_get_ms();
  while (sys_timer_get_ms() - t0 < 1) {
    clr_wdt();
  }
}

// header (power_interface.h): u32 get_reset_source_value(void) (cpu.a; the
// power_reset_source_dump() of the same header is not in the bd19 libraries).
// The enum at the top of that header numbers the sources; the value is taken
// as a bit mask of those numbers (TODO(chip): confirm with a known reset).
// 1 pin: P33_PPINR_RST (PA9 long press), P33_VCM_RST; 2 power-on: VDDIO /
// DVDD / PVDD POR; 4 software: MSYS / P11 / P33 soft, exception, assert;
// 8 watchdog: P11_WDT_RST; 32 wake: MSYS_P11_RST_RTC_WAKEUP.
static uint8_t SDK_reset_cause(void) {
  u32 v = get_reset_source_value();
  uint8_t c = 0;
  if (v & (BIT(P33_PPINR_RST) | BIT(P33_VCM_RST))) c |= 1;
  if (v & (BIT(P33_VDDIO_POR_RST) | BIT(MSYS_DVDD_POR_RST) | BIT(P11_PVDD_POR_RST))) c |= 2;
  if (v & (BIT(MSYS_SOFT_RST) | BIT(P11_SOFT_RST) | BIT(P33_SOFT_RST) |
           BIT(P33_EXCEPTION_SOFT_RST) | BIT(P33_ASSERT_SOFT_RST))) c |= 4;
  if (v & BIT(P11_WDT_RST)) c |= 8;
  if (v & BIT(MSYS_P11_RST_RTC_WAKEUP)) c |= 32;
  return c;
}

// USB full speed HID: keyboard (report 1) + vendor (report 3), map below.
// Device 4A4C:4155 ("JL", "UA": SDK descriptor.c), not the factory 514C:8850.
// SDK: the user-HID class of apps/hid/examples/standard_keyboard/usb_hid_devices.c
// (built when CONFIG_APP_STANDARD_KEYBOARD, TCFG_PC_ENABLE and
// TCFG_USB_SLAVE_USER_HID are on: sdk_setup.sh sets them). The SDK's own
// keyboard app starts USB the same way (app_standard_keyboard.c
// stdkb_app_start: set the report map, state callback, usb_start()).
// usb_start() (apps/common/device/usb/device/task_pc.c) opens the device with
// USB_DEVICE_CLASS_CONFIG = HID_CLASS. hid_send_data() returns
// hid_tx_data()'s result, 0 while the host has not opened the device.
// TODO(chip): which value hid_tx_data returns while the endpoint is busy.
extern void usb_start(void);
extern void usb_hid_set_repport_map(const u8 *map, int size);
extern void usb_hid_register_state_callback(void *callback);
extern u32 hid_send_data(const void *p, u32 len);

// Two top-level collections, so Windows gives the vendor one its own HID
// device that a program can open (the keyboard one is the system's):
//   report 1 keyboard: in 8 B (boot layout), out 1 B (LEDs)
//   report 3 vendor (usage page 0xFF00): out 64 B, the factory firmware's
//     "03 EF EF" way to the ROM UBOOT (gui/core/jluboot.py FACTORY_ENTER
//     writes ID + 64 B with hidapi = interrupt OUT, 16 B packets)
// The SDK ignores SET_REPORT on EP0 (hid_recv_output_report only prints it):
// output reports must come on the interrupt OUT endpoint (hidapi write /
// WriteFile, not HidD_SetOutputReport).
#define KBD_REPORT_ID 1
#define VENDOR_REPORT_ID 3
static const u8 kbdReportMap[] = {
  0x05, 0x01, 0x09, 0x06, 0xA1, 0x01,       // Generic Desktop, Keyboard, Application
  0x85, KBD_REPORT_ID,
  0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7,       // modifiers E0-E7
  0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02,
  0x95, 0x01, 0x75, 0x08, 0x81, 0x01,       // reserved byte
  0x95, 0x06, 0x75, 0x08, 0x15, 0x00, 0x25, 0x65,
  0x05, 0x07, 0x19, 0x00, 0x29, 0x65, 0x81, 0x00, // 6 key codes
  0x95, 0x05, 0x75, 0x01, 0x05, 0x08, 0x19, 0x01, 0x29, 0x05, 0x91, 0x02, // LEDs out
  0x95, 0x01, 0x75, 0x03, 0x91, 0x01,       // LED padding
  0xC0,
  0x06, 0x00, 0xFF, 0x09, 0x01, 0xA1, 0x01, // vendor page 0xFF00, usage 1, Application
  0x85, VENDOR_REPORT_ID,
  0x15, 0x00, 0x26, 0xFF, 0x00, 0x75, 0x08, 0x95, 0x40, // 64 bytes 0-255
  0x09, 0x01, 0x91, 0x02,                   // output
  0xC0,
};

static volatile uint8_t usbConnected;

// state 1 = configured by the host (app_standard_keyboard.c: "usb connect sucesss")
static void SDK_usb_state(u8 state) {
  usbConnected = (state == 1);
}

// Ways back to the ROM UBOOT without a button (the test firmware has no BOOT
// command): vendor report "03 EF EF" (as the factory firmware), or the host
// toggles Scroll Lock BOOT_TOGGLES times within
// BOOT_WINDOW_MS (keyboard LED output report, bit 2; a GUI can do it with
// SendInput, no driver). Then go_mask_usb_updata()
// (apps/common/device/usb/device/msd_upgrade.c: nvram_set_boot_state(USB soft
// key) and restart, same as the factory 03 EF EF) from the test task.
extern void usb_hid_set_output_callback(void *cb);
extern void go_mask_usb_updata(void);
#define BOOT_TOGGLES 6
#define BOOT_WINDOW_MS 3000
static volatile uint8_t bootRequest;
static uint8_t lastLeds, toggles;
static uint32_t firstToggleMs;

// called by usb_hid_devices.c hid_rx_data() with one OUT packet (up to 16 B):
// "01 LEDs" (keyboard) or a 16 B piece of the 65 B vendor report: the first
// piece starts "03 EF EF", the others (zeros) are dropped
static void SDK_usb_out(u8 *buf, u16 len) {
  uint32_t now;
  uint8_t leds;
  if (len >= 3 && buf[0] == VENDOR_REPORT_ID && buf[1] == 0xEF && buf[2] == 0xEF) {
    bootRequest = 1; // factory-compatible way back to UBOOT
    return;
  }
  if (len < 2 || buf[0] != KBD_REPORT_ID) return;
  leds = buf[1];
  if (!((leds ^ lastLeds) & 0x04)) {
    lastLeds = leds;
    return;
  }
  lastLeds = leds;
  now = sys_timer_get_ms();
  if (!toggles || now - firstToggleMs > BOOT_WINDOW_MS) {
    toggles = 0;
    firstToggleMs = now;
  }
  if (++toggles >= BOOT_TOGGLES) {
    bootRequest = 1;
  }
}

static void SDK_boot_check(void) {
  if (bootRequest) {
    go_mask_usb_updata(); // does not return
  }
}

static void SDK_usb_start(void) {
  usbConnected = 0;
  usb_hid_set_output_callback(SDK_usb_out);
  usb_hid_register_state_callback(SDK_usb_state);
  usb_hid_set_repport_map(kbdReportMap, sizeof(kbdReportMap));
  usb_start();
}

static uint8_t SDK_usb_ready(void) {
  return usbConnected;
}

static uint8_t SDK_usb_send(const uint8_t report[8]) {
  uint8_t r[9];
  r[0] = KBD_REPORT_ID;
  memcpy(r + 1, report, 8);
  return hid_send_data(r, 9) != 0;
}

// ADC. header (adc_api.h): u32 adc_add_sample_ch(u32 ch), u32 adc_get_voltage(u32 ch);
// AD_CH_VBAT = 1/4 VBAT, AD_CH_LDO5V = 1/4 LDO5V (charger input), AD_CH_LDOREF = VBG.
// adc_get_voltage returns mV (SDK apps/hid/modules/power/app_power_manage.c:
// battery = adc_get_voltage(AD_CH_VBAT) * 4 / 10 in 10 mV steps).
// TODO(SDK): whether the system already called adc_init() and samples the
// added channels in the background (adc_get_voltage reads the last sample).
static uint32_t SDK_adc_mv(uint32_t ch) {
  return adc_get_voltage(ch);
}

// Test pins with an ADC channel (header channel names = datasheet ADC0..12;
// DP0/DM0 = this firmware's USB and PA9 = long press reset are left out)
static const struct { uint8_t port, bit; uint32_t ch; } adcPins[] = {
  {0, 1, AD_CH_PA1},          // PA1 (20), ADC0
  {0, 3, AD_CH_PA3},          // PA3 (18), ADC1
  {0, 5, AD_CH_PA5},          // PA5 (16), ADC2
  {0, 7, AD_CH_PA7},          // PA7 (12), ADC3
  {0, 8, AD_CH_PA8},          // PA8 (11), ADC4
  {PORT_USB1, 0, AD_CH_DP1},  // USB1DP (29), ADC5
  {PORT_USB1, 1, AD_CH_DM1},  // USB1DM (28), ADC6
  {1, 1, AD_CH_PB1},          // PB1 (32), ADC7
  {1, 4, AD_CH_PB4},          // PB4 (27), ADC9
  {1, 6, AD_CH_PB6},          // PB6 (25), ADC12
};
#define ADC_PINS (sizeof(adcPins) / sizeof(adcPins[0]))

static void SDK_adc_init(void) {
  uint8_t i;
  adc_add_sample_ch(AD_CH_VBAT);
  adc_add_sample_ch(AD_CH_LDO5V);
  for (i = 0; i < ADC_PINS; i++) {
    adc_add_sample_ch(adcPins[i].ch);
  }
}

// App entry. The SDK's app_main() (apps/hid/app_main.c) starts the app named
// by app_config.h: sdk_setup.sh turns on CONFIG_APP_STANDARD_KEYBOARD and
// leaves out the SDK's app_standard_keyboard.c, so this file registers the app
// of that name and starts the test task from it (same shape as
// apps/hid/examples/idle/app_idle.c).
// header (os_api.h): int os_task_create(void (*task)(void *), void *arg, u8 prio,
// u32 stksize, int qsize, const char *name). Stack in 32-bit words like the
// task_info_table[] entries of app_main.c (TODO(chip): confirm).
static void test_task(void *priv) {
  (void)priv;
  test_main();
}

static int pintest_state_machine(struct application *app, enum app_state state,
                                 struct intent *it) {
  (void)app;
  if (state == APP_STA_START && it && it->action == ACTION_STANDARD_KEYBOARD) {
    os_task_create(test_task, 0, 1, 1024, 0, "pintest");
  }
  return 0;
}

static int pintest_event_handler(struct application *app, struct sys_event *event) {
  (void)app;
  (void)event;
  return 0; // keys, Bluetooth, devices: nothing to do
}

static const struct application_operation pintest_ops = {
  .state_machine = pintest_state_machine,
  .event_handler = pintest_event_handler,
};

REGISTER_APPLICATION(app_pintest) = {
  .name = "standard_keyboard",
  .action = ACTION_STANDARD_KEYBOARD,
  .ops = &pintest_ops,
  .state = APP_STA_DESTROY,
};
// ---- end of SDK glue ---------------------------------------------------------

static uint32_t startMs, lastYield;

void hal_init(void) {
  startMs = SDK_ms_now();
  lastYield = 0;
  // SDK FAQ (doc/FAQ/AC630X软件问题整理.pdf #37): on the AC632X changing PB2
  // trips the VCM reset unless VCM_DET_EN(0) is called first (the br25 HID
  // boards do the same). The test drives PB2 and PB3 (datasheet: PB3 MCLR),
  // so turn it off before any pin is touched.
  VCM_DET_EN(0);
  usb1_iomode(1); // header: the second USB pair as GPIO (test pins PU00, PU01)
  SDK_usb_start();
  SDK_adc_init();
}

uint32_t hal_millis(void) {
  return SDK_ms_now() - startMs;
}

void hal_idle(void) {
  SDK_watchdog();
  SDK_boot_check(); // Scroll Lock x6: back to the ROM UBOOT
  if (hal_millis() - lastYield >= 10) {
    lastYield = hal_millis();
    SDK_yield_tick();
  }
}

void hal_delay_ms(uint32_t ms) {
  while (ms >= 10) {
    SDK_watchdog();
    SDK_boot_check();
    SDK_yield_tick();
    ms -= 10;
  }
  while (ms--) {
    SDK_busy_ms();
  }
  SDK_watchdog();
}

uint8_t hal_reset_cause(void) {
  return SDK_reset_cause();
}

uint8_t hal_fault_read(uint32_t out[3]) {
  (void)out;
  return 0;
}

void hal_gpio_input_pullup(uint8_t port, uint8_t bit) {
  SDK_gpio_input(SDK_GPIO(port, bit), 1, 0);
}

void hal_gpio_input_pulldown(uint8_t port, uint8_t bit) {
  SDK_gpio_input(SDK_GPIO(port, bit), 0, 1);
}

void hal_gpio_output(uint8_t port, uint8_t bit) {
  SDK_gpio_output(SDK_GPIO(port, bit));
}

void hal_gpio_write(uint8_t port, uint8_t bit, uint8_t high) {
  SDK_gpio_write(SDK_GPIO(port, bit), high);
}

uint8_t hal_gpio_read(uint8_t port, uint8_t bit) {
  return SDK_gpio_read(SDK_GPIO(port, bit));
}

// ---- ADC ---------------------------------------------------------------------
static void putNum(uint32_t v) {
  char buf[11];
  uint8_t n = 0;
  do {
    buf[n++] = (char)('0' + v % 10);
    v /= 10;
  } while (v && n < sizeof(buf));
  while (n) {
    hal_hid_write(buf[--n]);
  }
}

static void put2Digits(uint8_t v) {
  hal_hid_write((char)('0' + v / 10));
  hal_hid_write((char)('0' + v % 10));
}

// "VBAT 3701 LDO5V 0 PA01 3300 ..." in mV. VBAT and LDO5V are read as 1/4
// (header), so times 4. Pins stay digital inputs with their pull-up (the
// shared logic set them so): a resistor-ladder key pulls the value down.
// TODO(SDK): whether the ADC reads a pin with gpio_set_die(gpio, 1); if not,
// the ADC mode must set die 0 (analog) on these pins and back afterwards.
void hal_adc_report(void) {
  uint8_t i;
  hal_hid_print("VBAT ");
  putNum(SDK_adc_mv(AD_CH_VBAT) * 4);
  hal_hid_print(" LDO5V ");
  putNum(SDK_adc_mv(AD_CH_LDO5V) * 4);
  for (i = 0; i < ADC_PINS; i++) {
    hal_hid_write(' ');
    hal_hid_write('P');
    hal_hid_write((char)('A' + adcPins[i].port));
    put2Digits(adcPins[i].bit);
    hal_hid_write(' ');
    putNum(SDK_adc_mv(adcPins[i].ch));
  }
  hal_hid_write('\n');
}

// ---- Flash identity (no UBOOT read: the bd19 RAM loader resets the chip) ----
// Library function (sdk.elf .data_code, no SDK header): copies the 16-byte
// unique ID that the SDK read from the internal flash at start (cmd 0x4B) and
// returns 0. Seen in the disassembly of the stock ac632n_hid build, UNVERIFIED
// on the chip.
extern int norflash_get_uuid(u8 *buf);

static void putHex8(uint8_t v) {
  static const char d[] = "0123456789abcdef";
  hal_hid_write(d[v >> 4]);
  hal_hid_write(d[v & 15]);
}

static void putHex32(uint32_t v) {
  int8_t i;
  for (i = 24; i >= 0; i -= 8) {
    putHex8((uint8_t)(v >> i));
  }
}

// "FLS size 00080000 chip 1234 uid 0011...ff": flash size in bytes and chip
// id as the ROM UBOOT handed them over (boot_info, system/boot.h), and the
// flash unique ID. The JEDEC ID (cmd 0x9F) sits in a library-local variable
// (flash_info + 0x1c in sdk.elf), not reachable from here: the size is its
// capacity byte (size = 1 << byte).
void hal_flash_report(void) {
  uint8_t uid[16];
  uint8_t i;
  hal_hid_print("FLS size ");
  putHex32(boot_info.flash_size);
  hal_hid_print(" chip ");
  putHex8((uint8_t)(boot_info.chip_id >> 8));
  putHex8((uint8_t)boot_info.chip_id);
  hal_hid_print(" uid ");
  for (i = 0; i < 16; i++) {
    uid[i] = 0;
  }
  norflash_get_uuid(uid);
  for (i = 0; i < 16; i++) {
    putHex8(uid[i]);
  }
  hal_hid_write('\n');
}

// ---- USB HID keyboard (US layout, same table idea as the CH32V203 test) -------
static uint8_t keyCode(char c, uint8_t *shift) {
  *shift = 0;
  if (c >= 'a' && c <= 'z') return (uint8_t)(0x04 + c - 'a');
  if (c >= 'A' && c <= 'Z') { *shift = 1; return (uint8_t)(0x04 + c - 'A'); }
  if (c >= '1' && c <= '9') return (uint8_t)(0x1E + c - '1');
  switch (c) {
  case '0': return 0x27;
  case '\n': return 0x28;
  case ' ': return 0x2C;
  case '-': return 0x2D;
  case '=': return 0x2E;
  case '.': return 0x37;
  case ',': return 0x36;
  case '/': return 0x38;
  case ':': *shift = 1; return 0x33;
  case '?': *shift = 1; return 0x38;
  default: return 0;
  }
}

static void sendReport(const uint8_t r[8]) {
  uint32_t t0 = hal_millis();
  if (!SDK_usb_ready()) {
    return; // no host: drop the text, the test keeps running
  }
  while (!SDK_usb_send(r)) { // endpoint busy: let the USB task run
    hal_idle();
    if (hal_millis() - t0 > 100) {
      return;
    }
  }
  hal_delay_ms(2); // TODO(SDK): wait for the IN transfer instead
}

void hal_hid_write(char c) {
  uint8_t shift;
  uint8_t r[8] = {0};
  uint8_t k = keyCode(c, &shift);
  if (!k) {
    return;
  }
  r[0] = shift ? 0x02 : 0x00;
  r[2] = k;
  sendReport(r);
  r[0] = 0;
  r[2] = 0;
  sendReport(r);
}

void hal_hid_print(const char *s) {
  while (*s) {
    hal_hid_write(*s++);
  }
}
