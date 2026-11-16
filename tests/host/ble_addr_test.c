// Host check of src/core/ble_addr.h (tests/host/run.py check_ble_addr): the
// per-slot, per-generation BLE address derivation. Chip-free. Prints booleans
// the runner compares to 1.
#include <stdio.h>
#include <string.h>
#include "core/ble_addr.h"

#define SLOTS 3
#define GENS 8

int main(void) {
  // A chip MAC with awkward bytes (low byte 0xFF, so the +slot/gen wraps)
  const uint8_t base[6] = {0xFF, 0x00, 0x33, 0x44, 0x55, 0x12};
  uint8_t addr[SLOTS][GENS][6];
  int distinct = 1, staticBits = 1, nondegen = 1, slotsDiffer = 1, regen = 1;
  unsigned s, g, s2, g2, i;

  for (s = 0; s < SLOTS; s++) {
    for (g = 0; g < GENS; g++) {
      ble_slot_addr(base, s, g, addr[s][g]);
    }
  }

  // Every (slot, generation) address is distinct
  for (s = 0; s < SLOTS; s++) {
    for (g = 0; g < GENS; g++) {
      for (s2 = 0; s2 < SLOTS; s2++) {
        for (g2 = 0; g2 < GENS; g2++) {
          if ((s != s2 || g != g2) && memcmp(addr[s][g], addr[s2][g2], 6) == 0) {
            distinct = 0;
          }
        }
      }
    }
  }

  for (s = 0; s < SLOTS; s++) {
    for (g = 0; g < GENS; g++) {
      uint8_t *a = addr[s][g];
      int allZero = (a[5] & 0x3F) == 0, allOne = (a[5] & 0x3F) == 0x3F;
      if ((a[5] & 0xC0) != 0xC0) {
        staticBits = 0; // top two bits of the MSB must be 1
      }
      for (i = 0; i < 5; i++) {
        allZero = allZero && a[i] == 0x00;
        allOne = allOne && a[i] == 0xFF;
      }
      if (allZero || allOne) {
        nondegen = 0;
      }
    }
  }

  // Slots 0..2 at the same generation differ from each other
  for (g = 0; g < GENS; g++) {
    if (!memcmp(addr[0][g], addr[1][g], 6) || !memcmp(addr[0][g], addr[2][g], 6) ||
        !memcmp(addr[1][g], addr[2][g], 6)) {
      slotsDiffer = 0;
    }
  }

  // A re-pair (generation + 1) changes the slot's address
  for (s = 0; s < SLOTS; s++) {
    for (g = 0; g + 1 < GENS; g++) {
      if (!memcmp(addr[s][g], addr[s][g + 1], 6)) {
        regen = 0;
      }
    }
  }

  printf("distinct %d\n", distinct);
  printf("static_top_bits %d\n", staticBits);
  printf("not_degenerate %d\n", nondegen);
  printf("slots_differ %d\n", slotsDiffer);
  printf("regen_changes %d\n", regen);
  return 0;
}
