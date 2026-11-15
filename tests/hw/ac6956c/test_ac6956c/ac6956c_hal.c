// UNVERIFIED DRAFT (2026-11-15): never built, never run. No JieLi SDK or
// compiler is installed yet (download needs the user's approval).
// SDK names follow the public fw-AC63_BT_SDK master, platform cpu/br23
// (include_lib/driver/cpu/br23/asm/*.h, include_lib/system/*.h), read on the
// web only; br23 is taken as the AC695N/AC6956C family (adc_api.h channels
// match the AC6956C datasheet table). Every SDK call goes through one of the
// SDK_* wrappers in the "SDK glue" block: fix them there only. "header" = the
// declaration was seen in that header; TODO(SDK) = still a guess.
//
// HAL for the shared pin test logic (test_ch32v203.c) on the JieLi AC6956C.
// The SDK owns main(): its app entry starts a task that runs test_main() (the
// shared logic's main, renamed by ac6956c_hal.h). The shared loop calls
// hal_idle() once per pass, which feeds the watchdog and yields to the RTOS.
#include "ac6956c_hal.h"

// ---- SDK glue: every name of the JieLi SDK is here ---------------------------
// TODO(SDK): include paths as the SDK's include list (-I) resolves them
#include "asm/gpio.h"     // header: include_lib/driver/cpu/br23/asm/gpio.h
#include "asm/adc_api.h"  // header: include_lib/driver/cpu/br23/asm/adc_api.h
#include "asm/power_interface.h" // header: power_reset_source_dump()
#include "system/timer.h" // header: sys_timer_get_ms()
#include "os/os_api.h"    // header: os_time_dly(), os_task_create()
#include "device/usb/device/hid.h" // header: hid_send_data() (says AC632N, shared)

// header: IO_GROUP_NUM 16, IO_PORTA_00 = 0, IO_PORTB_00 = 16, IO_PORTC_00 = 32
#define SDK_GPIO(port, bit) ((uint32_t)(port) * IO_GROUP_NUM + (bit))

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

// header: os_time_dly(ticks), not from an ISR. TODO(SDK): 1 tick = 10 ms?
static void SDK_yield_tick(void) {
  os_time_dly(1);
}

// TODO(SDK): clr_wdt() not seen in the br23 headers read (used in SDK apps)
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

// header (power_interface.h): u8 power_reset_source_dump(void), which prints
// and returns the source; the meaning of its value (P3_RST_SRC register 0x12
// in p33.h) is not in the headers. TODO(SDK): map it to 1 pin, 2 power-on,
// 4 software, 8 watchdog (PB1 long press = pin). Until then 0 ("?").
static uint8_t SDK_reset_cause(void) {
  (void)power_reset_source_dump(); // the SDK prints it on its debug UART
  return 0;
}

// USB full speed HID keyboard, 8-byte boot report.
// header (hid.h): u32 hid_send_data(const void *p, u32 len); u32 hid_register(usb_id);
// u32 hid_desc_config(usb_id, u8 *ptr, u32 *cur_itf_num) writes the interface
// and report descriptor (built with the USAGE_PAGE/USAGE/... macros of hid.h).
// TODO(SDK): how the device starts (usb_start / usb_device_mode not found:
// apps/common/usb/usb_config.h is not in the public tree), the class mask
// for HID only, a keyboard report descriptor (the SDK's default is likely
// consumer control), and hid_send_data's return value (0 = sent? length?).
static void SDK_usb_start(void) {
  // TODO(SDK): start USB device with the HID class only
}

static uint8_t SDK_usb_ready(void) {
  return 1; // TODO(SDK): configured by the host?
}

static uint8_t SDK_usb_send(const uint8_t report[8]) {
  return hid_send_data(report, 8) != 0; // TODO(SDK): return value meaning
}

// ADC. header (adc_api.h): u32 adc_add_sample_ch(u32 ch), u32 adc_get_voltage(u32 ch);
// AD_CH_VBAT = 1/4 VBAT, AD_CH_LDO5V = 1/4 LDO5V (charger input), AD_CH_LDOREF = VBG.
// TODO(SDK): adc_get_voltage unit (mV assumed), and whether the system already
// called adc_init() and samples the channels in the background.
static uint32_t SDK_adc_mv(uint32_t ch) {
  return adc_get_voltage(ch);
}

// Test pins with an ADC channel (header channel names, AC6956C pins only;
// AD_CH_DP = USBDP is this firmware's USB and left out)
static const struct { uint8_t port, bit; uint32_t ch; } adcPins[] = {
  {0, 1, AD_CH_PA1},   // PA1 (27)
  {0, 10, AD_CH_PA10}, // PA10 (25)
  {1, 1, AD_CH_PB1},   // PB1 (14), long press reset pin
  {1, 3, AD_CH_PB3},   // PB3 (12)
  {1, 8, AD_CH_PB8},   // PB8 (8)
  {1, 10, AD_CH_PB10}, // PB10 (6)
  {2, 4, AD_CH_PC4},   // PC4 (20)
  {2, 5, AD_CH_PC5},   // PC5 (19)
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

// header (os_api.h): int os_task_create(void (*task)(void *), void *arg, u8 prio,
// u32 stksize, int qsize, const char *name). TODO(SDK): prio and stack units,
// and where the app starts its tasks (app_main / task_info_table[]): this
// app_main replaces the SDK app's own one.
static void test_task(void *priv) {
  (void)priv;
  test_main();
}

void app_main(void) {
  os_task_create(test_task, 0, 1, 1024, 0, "pintest"); // TODO(SDK): prio, stack
}
// ---- end of SDK glue ---------------------------------------------------------

static uint32_t startMs, lastYield;

void hal_init(void) {
  startMs = SDK_ms_now();
  lastYield = 0;
  SDK_usb_start();
  SDK_adc_init();
}

uint32_t hal_millis(void) {
  return SDK_ms_now() - startMs;
}

void hal_idle(void) {
  SDK_watchdog();
  if (hal_millis() - lastYield >= 10) {
    lastYield = hal_millis();
    SDK_yield_tick();
  }
}

void hal_delay_ms(uint32_t ms) {
  while (ms >= 10) {
    SDK_watchdog();
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
