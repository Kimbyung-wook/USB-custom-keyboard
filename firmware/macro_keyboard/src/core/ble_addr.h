#ifndef __MK_BLE_ADDR_H__
#define __MK_BLE_ADDR_H__

#include <stdint.h>

// Derive a BLE device address for a slot and its re-pair generation from a base
// (chip) address. Chip-free so it can be checked off the board (tests/host):
// src/hal/ch573/ble_ch573.c feeds it the chip MAC and feeds the result to the
// stack (via MacAddr[] before CH57X_BLEInit).
//
// Each slot keeps its own address so a host bonded on one slot cannot see or
// reconnect while another slot is active; re-pairing a slot bumps its
// generation, which gives it a brand-new address so the host's old record is
// not reused (as Nordic's nRF Desktop makes a new identity when a bond is
// erased). Properties, for slot < 3:
//   - distinct for every (slot, generation) pair
//   - a valid static random address: the top two bits of the most significant
//     byte (out[5]) are 1, and the remaining 46 bits are not all-0 or all-1
static inline void ble_slot_addr(const uint8_t *base, unsigned slot, unsigned gen, uint8_t *out) {
  unsigned v = slot + 3u * gen; // distinct per (slot, gen) while slot < 3
  uint8_t i;
  for (i = 0; i < 6; i++) {
    out[i] = base[i];
  }
  // Encode v into the two low bytes (XOR keeps the map v -> bytes injective, so
  // different (slot, gen) give different addresses).
  out[0] ^= (uint8_t)(v & 0xFF);
  out[1] ^= (uint8_t)((v >> 8) & 0xFF);
  out[5] = (uint8_t)((base[5] & 0x3F) | 0xC0); // static random: top two bits set
  // Keep the 46-bit random part off the all-0 / all-1 values the spec forbids.
  if (!out[0] && !out[1] && !out[2] && !out[3] && !out[4] && !(out[5] & 0x3F)) {
    out[0] = 0x01;
  }
  if (out[0] == 0xFF && out[1] == 0xFF && out[2] == 0xFF && out[3] == 0xFF && out[4] == 0xFF &&
      (out[5] & 0x3F) == 0x3F) {
    out[0] = 0xFE;
  }
}

#endif
