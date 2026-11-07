// Host test driver for firmware/macro_keyboard/src/core/{storage,macro,lock,
// input,led,protocol}.c, built against the host HAL (stub/hal_host.h).
// Usage: host_test <blob.bin> [caps]
//        host_test <blob.bin> lock <inputs>...
//        host_test <blob.bin> tail <blob2.bin>
//        host_test <blob.bin> stall|reset <n>
//        host_test <blob.bin> input <steps>...
//        host_test <blob.bin> proto <request hex>...
//        host_test <blob.bin> torn <blob2.bin> <page> <erased>
// Stages and commits the blob in 56-byte chunks, then types every slot and
// prints the key reports, e.g. "slot 0: 02:0b 00:08 D50 ...".
// "lock": each argument is a sequence of input numbers (0-8) entered on the
// keyboard, "H" for holding the knob (starts a pattern entry), "t" for
// waiting out the entry timeout, "w" for a 6 s pause and "m" for one minute
// without input (auto-lock);
// prints the lock state after each. "tail": commits blob2 over the blob and counts the
// DataFlash bytes past blob2 that are not blank.
// "stall": from the n-th report on the host collects nothing (KVM switched
// away without a reset); "reset": a bus reset after the n-th report. Each
// types slot 0 with a second slot 0 queued, then slot 0 again once the
// host is back.
// "input": runs the whole loop (input, lock, macro, LEDs) 1 ms at a time
// against fake switches and knob lines. Steps: "p<i>"/"r<i>" press/release
// switch i (keys, then the knob switch), "e<AB>" set the knob lines (e.g.
// e10), "w<ms>" wait; a first step "s<AB>" sets the lines at
// power-up. Each step prints one line: "<step>:" then the key reports and
// the LED frames sent, "L<color>.<mask>.<maskColor>" (only changes).
// "proto": runs each request (hex) through src/core/protocol.c and prints
// the response, "res <hex> evt <n>".
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../firmware/macro_keyboard/src/hal/hal.h"
#include "../../firmware/macro_keyboard/src/core/input.h"
#include "../../firmware/macro_keyboard/src/core/led.h"
#include "../../firmware/macro_keyboard/src/core/lock.h"
#include "../../firmware/macro_keyboard/src/core/macro.h"
#include "../../firmware/macro_keyboard/src/core/protocol.h"
#include "../../firmware/macro_keyboard/src/core/storage.h"

static uint8_t eeprom[BLOB_CAPACITY];
static uint32_t now = 0;
static uint8_t keyboardLedStatus = 0;
static hal_sw_t switches = 0;   // bit i = switch i pressed
static uint8_t encoderLines = 3; // A << 1 | B, at rest both high
static int ledLast = -1;        // last LED frame printed

uint16_t hal_ms(void) { return (uint16_t)now; }
hal_sw_t hal_switches(void) { return switches; }
uint8_t hal_encoder(void) { return encoderLines; }
uint8_t kb_caps_lock(void) { return keyboardLedStatus & 0x02; }
void hal_leds(uint8_t color, hal_sw_t mask, uint8_t maskColor) {
  int frame = color << 24 | mask << 8 | maskColor;
  if (frame != ledLast) printf(" L%u.%02x.%u", color, mask, maskColor);
  ledLast = frame;
}
// Power-up color wheel: printed as " C<r>.<g>.<b>" when it changes
void hal_leds_rgb(uint8_t r, uint8_t g, uint8_t b) {
  int frame = 1 << 30 | r << 16 | g << 8 | b;
  if (frame != ledLast) printf(" C%u.%u.%u", r, g, b);
  ledLast = frame;
}

static int pressed = 0; // a key is down and must be released next
static int errors = 0;
static uint32_t lastSend = 0;

uint8_t hal_store_read(blob_pos_t addr) { return addr < BLOB_CAPACITY ? eeprom[addr] : 0; }
void hal_store_write(blob_pos_t addr, uint8_t val) {
  if (addr < BLOB_CAPACITY) eeprom[addr] = val;
}

static int sends = 0;
static int stallAt = 0;  // > 0: reports from this one on are not collected
static int resetAt = 0;  // > 0: bus reset after this report
static uint8_t busy = 0;
static uint8_t ready = 1;

uint8_t kb_ready(void) { return ready; }
uint8_t kb_busy(void) { return busy; }
void kb_cancel(void) {
  printf(" cancel");
  busy = 0;
}
uint8_t kb_send(uint8_t modifiers, uint8_t keycode) {
  sends++;
  if (resetAt && sends == resetAt) ready = 0;
  if (stallAt && sends >= stallAt) {
    printf(" lost"); // never collected: the host does not see it
    busy = 1;
    return 1;
  }
  // One loop iteration = 1 ms here; a long gap means a {DELAY:n} ran
  if (now - lastSend >= 10) printf(" D%u", (unsigned)(now - lastSend));
  lastSend = now;
  if (keycode == 0 && modifiers == 0) {
    if (!pressed) { printf(" !double-release"); errors++; }
    pressed = 0;
  } else {
    if (pressed) { printf(" !no-release"); errors++; }
    printf(" %02x:%02x", modifiers, keycode);
    pressed = 1;
  }
  return 1;
}

// Config channel: one request at a time (runProto)
uint8_t hal_req[64], hal_res[64];
static uint8_t hasRequest = 0;
uint8_t vendor_has_request(void) { return hasRequest; }
uint8_t vendor_in_busy(void) { return 0; }
void vendor_send(void) { hasRequest = 0; }
uint8_t hal_chip_uid(uint8_t i) { return (uint8_t)(0xA0 + i); }

// Each argument is a request in hex (up to 64 bytes, zero padded); prints
// "res <64 bytes hex> evt <PROTO_EVT_*>" per request
static void runProto(int count, char **reqs) {
  lock_init();
  for (int i = 0; i < count; i++) {
    const char *h = reqs[i];
    memset(hal_req, 0, sizeof(hal_req));
    for (size_t j = 0; j < sizeof(hal_req) && h[2 * j] && h[2 * j + 1]; j++) {
      char byte[3] = {h[2 * j], h[2 * j + 1], 0};
      hal_req[j] = (uint8_t)strtol(byte, NULL, 16);
    }
    hasRequest = 1;
    uint8_t evt = protocol_poll();
    if (hasRequest) { printf("!no-response\n"); errors++; }
    printf("res ");
    for (size_t j = 0; j < sizeof(hal_res); j++) printf("%02x", hal_res[j]);
    printf(" evt %u\n", evt);
  }
}

static size_t readFile(const char *path, uint8_t *buf, size_t size) {
  FILE *f = fopen(path, "rb");
  size_t n;
  if (!f) {
    fprintf(stderr, "cannot open %s\n", path);
    exit(2);
  }
  n = fread(buf, 1, size, f);
  fclose(f);
  return n;
}

// WRITE in 56-byte chunks, then COMMIT; prints both statuses
static uint8_t writeBlob(uint8_t *blob, size_t n) {
  uint8_t st = ST_OK;
  for (size_t off = 0; off < n && st == ST_OK; off += PROTO_MAX_CHUNK) {
    uint8_t len = (uint8_t)((n - off) > PROTO_MAX_CHUNK ? PROTO_MAX_CHUNK : n - off);
    st = storage_stage_write((uint16_t)off, len, blob + off);
  }
  printf("write status %u\n", st);
  if (st == ST_OK) {
    st = storage_commit((uint16_t)n, blob[6] | (blob[7] << 8));
    printf("commit status %u\n", st);
  }
  return st;
}

static void runLock(int count, char **inputs) {
  static const char *names[] = {"open", "locked", "blocked", "unlocking", "relock"};
  lock_init();
  printf("lock %s flags %u\n", names[lock_state()], lock_flags());
  for (int i = 0; i < count; i++) {
    for (const char *s = inputs[i]; *s; s++) {
      now += 100;
      if (*s == 'H') {
        lock_start_entry(); // the knob was held for LOCK_HOLD_MS
      } else if (*s == 't') {
        now += LOCK_UNLOCK_TIMEOUT_MS;
        lock_task();
      } else if (*s == 'w') {
        now += 6000;
        lock_task();
      } else if (*s == 'm') {
        for (int sec = 0; sec < 60; sec++) {
          now += 1000;
          lock_task();
        }
      } else {
        lock_activity(); // as input_action() in src/core/input.c
        lock_input((uint8_t)(*s - '0'));
      }
    }
    printf("lock %s flags %u\n", names[lock_state()], lock_flags());
  }
}

// Runs the queued slots until playback ends (at most 10 s)
static void play(void) {
  uint32_t start = now;
  lastSend = now;
  while (macro_busy() && now - start < 10000) {
    macro_task();
    now++;
  }
  if (macro_busy()) { printf(" !still-busy"); errors++; }
}

static void runStall(int reset, int n) {
  if (reset) resetAt = n; else stallAt = n;
  printf("slot 0:");
  macro_enqueue(0);
  macro_enqueue(0);
  play();
  printf("\n");
  // The host is back
  stallAt = 0;
  resetAt = 0;
  busy = 0;
  ready = 1;
  pressed = 0;
  printf("slot 1:");
  macro_enqueue(0);
  play();
  if (pressed) { printf(" !left-pressed"); errors++; }
  printf("\n");
  printf("errors %d\n", errors);
}

// The firmware's loop() without USB, 1 ms per pass
static void runLoop(uint32_t ms) {
  for (uint32_t i = 0; i < ms; i++) {
    now++;
    now16 = hal_ms();
    input_task();
    lock_task();
    macro_task();
    led_task();
  }
}

static void runInput(int count, char **steps) {
  int first = 0;
  if (count > 0 && steps[0][0] == 's') {      // knob lines at power-up
    encoderLines = (uint8_t)strtol(steps[0] + 1, NULL, 2);
    first = 1;
  }
  lock_init();
  input_init();
  printf("start:");
  runLoop(1);
  printf("\n");
  for (int i = first; i < count; i++) {
    const char *s = steps[i];
    uint32_t wait = 1;
    printf("%s:", s);
    lastSend = now;
    if (s[0] == 'p') switches |= (hal_sw_t)(1u << atoi(s + 1));
    else if (s[0] == 'r') switches &= (hal_sw_t)~(1u << atoi(s + 1));
    else if (s[0] == 'e') encoderLines = (uint8_t)strtol(s + 1, NULL, 2);
    else if (s[0] == 'w') wait = (uint32_t)atoi(s + 1);
    runLoop(wait);
    printf("\n");
  }
}

int main(int argc, char **argv) {
  static uint8_t blob[8192]; // larger than any capacity (TOO_BIG tests)
  size_t n;

  if (argc < 2) {
    fprintf(stderr, "usage: host_test blob.bin [caps | lock <inputs>... | tail blob2.bin]\n");
    return 2;
  }
  n = readFile(argv[1], blob, sizeof(blob));
  if (argc > 2 && !strcmp(argv[2], "caps")) keyboardLedStatus = 0x02;

  memset(eeprom, 0xFF, sizeof(eeprom)); // blank DataFlash
  storage_init();
  printf("blank slot0 len %u\n", storage_slot_len(0));

  if (writeBlob(blob, n) != ST_OK) return 0;

  // Reload from "DataFlash" as after a power cycle
  storage_init();

  if (argc > 2 && !strcmp(argv[2], "lock")) {
    runLock(argc - 3, argv + 3);
    return 0;
  }
  if (argc > 2 && !strcmp(argv[2], "proto")) {
    runProto(argc - 3, argv + 3);
    return 0;
  }
  if (argc > 2 && !strcmp(argv[2], "input")) {
    runInput(argc - 3, argv + 3);
    return 0;
  }
  if (argc > 3 && (!strcmp(argv[2], "stall") || !strcmp(argv[2], "reset"))) {
    runStall(!strcmp(argv[2], "reset"), atoi(argv[3]));
    return 0;
  }
  if (argc > 5 && !strcmp(argv[2], "torn")) {
    // Power lost in the CH32V hal_store_flush of blob2: it erases and programs
    // the changed 256-byte pages from the lowest up. Pages before <page> hold
    // blob2, <page> is erased (erase=1, reads 0xE339E339) or already blob2
    // (erase=0), the pages after it still hold the old blob. Then a power-up.
    static uint8_t old[BLOB_CAPACITY], blob2[8192];
    size_t n2 = readFile(argv[3], blob2, sizeof(blob2));
    size_t page = (size_t)atoi(argv[4]) * 256, erased = (size_t)atoi(argv[5]);
    memcpy(old, eeprom, sizeof(old));
    writeBlob(blob2, n2);
    if (page < sizeof(eeprom)) {
      size_t keep = page + 256; // the old blob from the next page on
      if (erased) {
        for (size_t i = page; i < page + 256 && i < sizeof(eeprom); i++) eeprom[i] = i & 1 ? 0xE3 : 0x39;
      }
      if (keep < sizeof(eeprom)) memcpy(eeprom + keep, old + keep, sizeof(eeprom) - keep);
    }
    storage_init();
    printf("torn page %d erased %d: slot0 len %u\n", atoi(argv[4]), (int)erased, storage_slot_len(0));
    argc = 2; // type every slot below
  }
  if (argc > 3 && !strcmp(argv[2], "tail")) {
    static uint8_t blob2[8192];
    size_t n2 = readFile(argv[3], blob2, sizeof(blob2));
    int dirty = 0;
    writeBlob(blob2, n2);
    for (size_t i = n2; i < sizeof(eeprom); i++) dirty += eeprom[i] != 0xFF;
    printf("tail dirty %d\n", dirty);
    return 0;
  }

  for (uint8_t slot = 0; slot < SLOT_COUNT; slot++) {
    printf("slot %u:", slot);
    if (!macro_enqueue(slot)) {
      printf(" (empty)\n");
      continue;
    }
    lastSend = now;
    while (macro_busy()) {
      macro_task();
      now++;
    }
    if (pressed) { printf(" !left-pressed"); errors++; }
    printf("\n");
  }
  printf("errors %d\n", errors);
  return 0;
}
