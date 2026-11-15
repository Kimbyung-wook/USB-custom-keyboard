// AC6321A HAL basics (JUZH_3B4), UNVERIFIED on a chip: time, keys (matrix),
// knobs, config store, chip ID, bootloader, kb_* routing (USB / BLE).
// SDK calls as in the pin test (tests/hw/ac6321a/test_ac6321a/ac6321a_hal.c);
// TODO(SDK) = a guess still to check on the chip.
#if defined(MK_CHIP_AC6321A)
#include "asm/gpio.h"            // gpio_set_die/direction/pull_up/output_value, gpio_read
#include "asm/adc_api.h"         // adc_add_sample_ch, adc_get_voltage, AD_CH_VBAT
#include "asm/power/p33.h"       // VCM_DET_EN
#include "asm/power_interface.h" // power_set_soft_poweroff
#include "system/timer.h"        // sys_timer_get_ms
#include "syscfg_id.h"           // syscfg_read/write, CFG_STORE_VM_ONLY_BEGIN
#include "../hal.h"
#include "../../core/battery.h"

extern void clr_wdt(void);            // library (bd19 asm/cpu.h has it in a macro only)
extern void go_mask_usb_updata(void); // apps/common/device/usb/device/msd_upgrade.c
extern int norflash_get_uuid(u8 *buf); // library: the flash's 16-byte unique ID

// ---- time --------------------------------------------------------------------
// TODO(SDK): sys_timer_get_ms() step (1 ms assumed)
uint32_t hal_ms32(void) { return sys_timer_get_ms(); }
uint16_t hal_ms(void) { return (uint16_t)sys_timer_get_ms(); }

// ---- pins --------------------------------------------------------------------
#define PORT_USB1 2
#define NO_PIN 0xFF
typedef struct {
  uint8_t port, bit;
} pin_t;

// SDK GPIO number: IO_PORTA_00 = 0, IO_PORTB_00 = 16 (IO_GROUP_NUM), USB1 =
// IO_PORT_DP1 + bit (bit 0 DP, 1 DM)
static uint32_t gpioNum(pin_t p) {
  if (p.port == PORT_USB1) {
    return IO_PORT_DP1 + p.bit;
  }
  return (uint32_t)p.port * IO_GROUP_NUM + p.bit;
}

static void pinInputPullUp(pin_t p) {
  uint32_t g;
  if (p.port == NO_PIN) {
    return;
  }
  g = gpioNum(p);
  gpio_set_die(g, 1);
  gpio_set_pull_down(g, 0);
  gpio_set_pull_up(g, 1);
  gpio_set_direction(g, 1);
}

// 1 = low (pressed); a missing pin reads released
static uint8_t pinLow(pin_t p) {
  if (p.port == NO_PIN) {
    return 0;
  }
  return gpio_read(gpioNum(p)) ? 0 : 1;
}

static void pinDriveLow(pin_t p) {
  uint32_t g;
  if (p.port == NO_PIN) {
    return;
  }
  g = gpioNum(p);
  gpio_set_pull_up(g, 0);
  gpio_set_output_value(g, 0);
  gpio_set_direction(g, 0);
}

// ---- keys: 3 x 4 matrix, rows driven low one at a time ------------------------
static const pin_t rowPins[BOARD_MATRIX_ROWS] = {BOARD_ROW_PINS};
static const pin_t colPins[BOARD_MATRIX_COLS] = {BOARD_COL_PINS};
static const pin_t knobPins[2 * BOARD_KNOBS] = {BOARD_KNOB_PINS};
static const pin_t knobSwPins[BOARD_KNOBS] = {BOARD_KNOB_SW_PINS};
_Static_assert(BOARD_MATRIX_ROWS * BOARD_MATRIX_COLS == BOARD_KEYS, "matrix = keys");

// Settle time after a row goes low (column pull-ups against the line
// capacitance). TODO(chip): length; the pin test can tell
static void settle(void) {
  volatile uint8_t n;
  for (n = 0; n < 40; n++) {
  }
}

hal_sw_t hal_switches(void) {
  hal_sw_t sw = 0;
  uint8_t r, c;
  for (r = 0; r < BOARD_MATRIX_ROWS; r++) {
    if (rowPins[r].port == NO_PIN) {
      continue;
    }
    pinDriveLow(rowPins[r]);
    settle();
    for (c = 0; c < BOARD_MATRIX_COLS; c++) {
      if (pinLow(colPins[c])) {
        sw |= (hal_sw_t)(1u << (r * BOARD_MATRIX_COLS + c));
      }
    }
    pinInputPullUp(rowPins[r]); // released: back to input, no row fights another
  }
  for (r = 0; r < BOARD_KNOBS; r++) {
    if (pinLow(knobSwPins[r])) {
      sw |= (hal_sw_t)(1u << (BOARD_KEYS + r));
    }
  }
  return sw;
}

// Knob n: A = knobPins[2n], B = knobPins[2n + 1]; a missing pin reads high
uint8_t hal_encoder(void) {
  uint8_t v = 0, n;
  for (n = 0; n < BOARD_KNOBS; n++) {
    v |= (uint8_t)((!pinLow(knobPins[2 * n]) << 1 | !pinLow(knobPins[2 * n + 1])) << (2 * n));
  }
  return v;
}

// ---- config store: RAM copy, saved in VM items -------------------------------
// The SDK's VM (syscfg_read / syscfg_write, its own flash area with wear
// handling) holds the blob as STORE_ITEMS items of STORE_CHUNK bytes, item
// ids from STORE_ID (VM-only range 50-99 of syscfg_id.h; 51 is the SDK's).
// TODO(SDK): largest VM item (256 B assumed) and the VM size
// (VM_MAX_SIZE_CONFIG 16 KB in the board Makefile)
#define STORE_CHUNK 256
#define STORE_ITEMS (BLOB_CAPACITY / STORE_CHUNK)
#define STORE_ID (CFG_STORE_VM_ONLY_BEGIN + 10)
_Static_assert(BLOB_CAPACITY % STORE_CHUNK == 0, "whole items");
_Static_assert(STORE_ID + STORE_ITEMS - 1 <= CFG_STORE_VM_ONLY_END, "VM-only ids");
static uint8_t store[BLOB_CAPACITY];
static uint8_t storeDirty[STORE_ITEMS];

static void storeLoad(void) {
  uint8_t i;
  for (i = 0; i < STORE_ITEMS; i++) {
    if (syscfg_read(STORE_ID + i, store + i * STORE_CHUNK, STORE_CHUNK) != STORE_CHUNK) {
      memset(store + i * STORE_CHUNK, 0xFF, STORE_CHUNK); // never written: blank
    }
    storeDirty[i] = 0;
  }
}

uint8_t hal_store_read(blob_pos_t addr) { return store[addr]; }

void hal_store_write(blob_pos_t addr, uint8_t value) {
  if (store[addr] != value) {
    store[addr] = value;
    storeDirty[addr / STORE_CHUNK] = 1;
  }
}

uint8_t hal_store_flush(void) {
  uint8_t i, ok = 1;
  for (i = 0; i < STORE_ITEMS; i++) {
    if (storeDirty[i]) {
      clr_wdt();
      if (syscfg_write(STORE_ID + i, store + i * STORE_CHUNK, STORE_CHUNK) == STORE_CHUNK) {
        storeDirty[i] = 0;
      } else {
        ok = 0;
      }
    }
  }
  return ok;
}

// ---- chip ID, battery, power -----------------------------------------------------
static uint8_t uid[16];

uint8_t hal_chip_uid(uint8_t i) { return uid[i & 15]; }

// Supply in mV: the internal VBAT channel reads 1/4 (adc_api.h), in mV
// (app_power_manage.c). TODO(SDK): sampled in the background after
// adc_add_sample_ch(), as the SDK's power manager does
int16_t hal_battery_vdd_mv(void) { return (int16_t)(adc_get_voltage(AD_CH_VBAT) * 4); }

void hal_deep_sleep(void) {
  power_set_soft_poweroff(); // back with the power switch / key wake-up
  while (1) {
    clr_wdt();
  }
}

// ROM UBOOT (USB disk 4C4A:3442), as the factory 03 EF EF: the SDK marks a
// USB update in NVRAM and restarts (msd_upgrade.c). Does not return.
void hal_enter_bootloader(void) {
  go_mask_usb_updata();
  while (1) {
  }
}

void hal_idle(void) { clr_wdt(); }

void hal_init(void) {
  uint8_t i;
  // Changing PB2 trips the VCM reset unless this is off first (SDK FAQ #37,
  // pin test); PB3 is MCLR. Before any pin is touched.
  VCM_DET_EN(0);
  usb1_iomode(1); // USB1DP / USB1DM as GPIO (key / knob / LED candidates)
  for (i = 0; i < BOARD_MATRIX_ROWS; i++) {
    pinInputPullUp(rowPins[i]);
  }
  for (i = 0; i < BOARD_MATRIX_COLS; i++) {
    pinInputPullUp(colPins[i]);
  }
  for (i = 0; i < 2 * BOARD_KNOBS; i++) {
    pinInputPullUp(knobPins[i]);
  }
  for (i = 0; i < BOARD_KNOBS; i++) {
    pinInputPullUp(knobSwPins[i]);
  }
  memset(uid, 0, sizeof(uid));
  norflash_get_uuid(uid);
  adc_add_sample_ch(AD_CH_VBAT);
  storeLoad();
}

// ---- keyboard: USB while configured, else BLE ---------------------------------
#if BOARD_BLE
uint8_t kb_ready(void) { return hal_usb_ready() || hal_ble_ready(); }
uint8_t kb_busy(void) { return hal_usb_ready() ? hal_usb_kb_busy() : hal_ble_busy(); }
uint8_t kb_caps_lock(void) { return hal_usb_ready() ? hal_usb_caps_lock() : hal_ble_caps_lock(); }
uint8_t kb_send(uint8_t modifiers, uint8_t keycode) {
  return hal_usb_ready() ? hal_usb_kb_send(modifiers, keycode) : hal_ble_send(modifiers, keycode);
}
void kb_cancel(void) {
  hal_usb_kb_cancel();
  hal_ble_cancel();
}
#else
uint8_t kb_ready(void) { return hal_usb_ready(); }
uint8_t kb_busy(void) { return hal_usb_kb_busy(); }
uint8_t kb_caps_lock(void) { return hal_usb_caps_lock(); }
uint8_t kb_send(uint8_t modifiers, uint8_t keycode) { return hal_usb_kb_send(modifiers, keycode); }
void kb_cancel(void) { hal_usb_kb_cancel(); }
#endif
#endif
