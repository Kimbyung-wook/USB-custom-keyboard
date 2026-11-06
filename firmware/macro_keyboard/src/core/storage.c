// clang-format off
#include "../hal/hal.h"
#include "protocol.h"
#include "storage.h"
// clang-format on

// RAM copy of the stored blob, and the staging area filled by WRITE. Every
// blob is checked in stage[] (also the one read from DataFlash at start-up)
// and copied to blob[] only when it is taken into use.
static __xdata uint8_t blob[BLOB_CAPACITY];
static __xdata uint8_t stage[BLOB_CAPACITY];
static __xdata blob_pos_t stageLen;

// Macro slots plus the lock pattern (SLOT_LOCK) and settings (SLOT_SETTINGS).
// Slots after these are accepted and ignored (newer GUIs).
#define STORED_SLOTS (SLOT_SETTINGS + 1)
static __xdata blob_pos_t slotOffset[STORED_SLOTS];
static __xdata blob_pos_t slotLength[STORED_SLOTS];

// uint16 lengths in the blob: payload at stage[4..5], each slot before its text.
// With 8-bit positions (CH552, 128 bytes) a high byte other than 0 is invalid.
#if BLOB_CAPACITY > 255
#define U16_AT(p) (stage[p] | ((uint16_t)stage[(p) + 1] << 8))
#define PAYLOAD_OK(total) (BLOB_HEADER_SIZE + U16_AT(4) == (total))
#define STORED_TOTAL() ((blob_pos_t)(BLOB_HEADER_SIZE + U16_AT(4)))
#else
#define PAYLOAD_OK(total) (stage[5] == 0 && BLOB_HEADER_SIZE + stage[4] == (total))
// A payload length over 255 is invalid anyway; 0xFF..0xFF (blank) fails too
#define STORED_TOTAL() (stage[5] ? 0 : BLOB_HEADER_SIZE + stage[4])
#endif

uint16_t crc16_ccitt(__xdata uint8_t *data, __data uint16_t len) {
  // CRC-16/CCITT-FALSE: poly 0x1021, init 0xFFFF, no reflection, no xorout
  __xdata uint8_t *__data p = data; // pointer kept in internal RAM
  __data uint16_t crc = 0xFFFF;
  while (len--) {
    crc ^= (uint16_t)(*p++) << 8;
    for (__data uint8_t bit = 0; bit < 8; bit++) {
      if (crc & 0x8000) {
        crc = (crc << 1) ^ 0x1021;
      } else {
        crc <<= 1;
      }
    }
  }
  return crc;
}

// Check the blob in stage[] and, with fill set, record the slot
// offsets/lengths. Returns 1 when valid. Always parsing the fixed stage[]
// with blob_pos_t positions (8-bit on CH552) keeps this short.
static uint8_t parseStage(__data blob_pos_t totalLen, __data uint8_t fill) {
  __data blob_pos_t pos;
  __data uint8_t count;

  if (totalLen < BLOB_HEADER_SIZE || totalLen > BLOB_CAPACITY) {
    return 0;
  }
  if (stage[0] != 'M' || stage[1] != 'K' || stage[2] != 1) {
    return 0;
  }
  // Payload length (uint16) must make up exactly totalLen
  if (!PAYLOAD_OK(totalLen)) {
    return 0;
  }
  if (crc16_ccitt(stage + BLOB_HEADER_SIZE, totalLen - BLOB_HEADER_SIZE) !=
      (stage[6] | ((uint16_t)stage[7] << 8))) {
    return 0;
  }

  // Walk the slots: each is a uint16 length followed by its text
  count = stage[3];
  pos = BLOB_HEADER_SIZE;
  for (__data uint8_t i = 0; i < count; i++) {
    __data blob_pos_t len;
    if ((blob_pos_t)(pos + 2) > totalLen) {
      return 0;
    }
#if BLOB_CAPACITY > 255
    len = U16_AT(pos);
#else
    len = stage[pos];
    if (stage[pos + 1] != 0) {
      return 0; // longer than any blob
    }
#endif
    pos += 2;
    if (len > (blob_pos_t)(totalLen - pos)) {
      return 0;
    }
    if (i == SLOT_LOCK) {
      // Empty (no lock) or LOCK_MIN_STEPS..LOCK_MAX_STEPS input numbers
      if (len != 0 && (len < LOCK_MIN_STEPS || len > LOCK_MAX_STEPS)) {
        return 0;
      }
#if !(BOARD_FEATURES & BOARD_FEATURE_LOCK)
      // No knob to start a pattern entry: a stored lock could never be opened
      if (len != 0) {
        return 0;
      }
#endif
      for (__data blob_pos_t j = 0; j < len; j++) {
        if (stage[pos + j] >= SLOT_COUNT) {
          return 0;
        }
      }
    }
    if (fill && i < STORED_SLOTS) {
      slotOffset[i] = pos;
      slotLength[i] = len;
    }
    pos += len;
  }
  return pos == totalLen;
}

// Take the valid blob in stage[] as the active config: copy it to blob[]
// and record its slots. An invalid one leaves every slot empty and no lock.
static void loadStage(__data blob_pos_t totalLen) {
  for (__data uint8_t i = 0; i < STORED_SLOTS; i++) {
    slotLength[i] = 0;
  }
  if (!parseStage(totalLen, 1)) {
    // Parsing may have filled some slots before failing
    for (__data uint8_t i = 0; i < STORED_SLOTS; i++) {
      slotLength[i] = 0;
    }
  }
  for (__data blob_pos_t i = 0; i < BLOB_CAPACITY; i++) {
    blob[i] = stage[i];
  }
  stageLen = 0;
}

void storage_init(void) {
  for (__data blob_pos_t i = 0; i < BLOB_CAPACITY; i++) {
    stage[i] = hal_store_read(i);
  }
  loadStage(STORED_TOTAL());
}

uint8_t storage_read_byte(__data blob_pos_t addr) { return hal_store_read(addr); }

__xdata uint8_t *storage_slot_text(__data uint8_t slot) {
  return blob + slotOffset[slot];
}

blob_pos_t storage_slot_len(__data uint8_t slot) {
  if (slot >= SLOT_COUNT) {
    return 0;
  }
  return slotLength[slot];
}

uint8_t storage_lock_len(void) { return (uint8_t)slotLength[SLOT_LOCK]; } // at most LOCK_MAX_STEPS

__xdata uint8_t *storage_lock_steps(void) { return blob + slotOffset[SLOT_LOCK]; }

uint8_t storage_autolock_minutes(void) {
  return slotLength[SLOT_SETTINGS] ? blob[slotOffset[SLOT_SETTINGS]] : 0;
}

uint8_t storage_stage_write(__data uint16_t offset, __data uint8_t len,
                            __xdata uint8_t *data) {
  if (len == 0 || len > PROTO_MAX_CHUNK) {
    return ST_BAD_ARG;
  }
  if (offset + len > BLOB_CAPACITY) {
    return ST_TOO_BIG;
  }
  // A transfer starts at offset 0 and continues without gaps
  if (offset == 0) {
    stageLen = 0;
  } else if (offset != stageLen) {
    return ST_BAD_ARG;
  }
  for (__data uint8_t i = 0; i < len; i++) {
    stage[offset + i] = data[i];
  }
  stageLen = offset + len;
  return ST_OK;
}

uint8_t storage_commit(__data uint16_t totalLen, __data uint16_t crc) {
  if (totalLen > BLOB_CAPACITY) {
    return ST_TOO_BIG;
  }
  if (totalLen < BLOB_HEADER_SIZE || totalLen != stageLen) {
    return ST_BAD_ARG;
  }
  if (!PAYLOAD_OK(totalLen)) {
    return ST_BAD_ARG;
  }
  if ((stage[6] | ((uint16_t)stage[7] << 8)) != crc ||
      crc16_ccitt(stage + BLOB_HEADER_SIZE, totalLen - BLOB_HEADER_SIZE) != crc) {
    return ST_BAD_CRC;
  }
  if (!parseStage((blob_pos_t)totalLen, 0)) {
    return ST_BAD_ARG;
  }

  // The rest is blanked so an old config can never be read back (READ
  // accepts any offset up to the capacity)
  for (__data blob_pos_t i = (blob_pos_t)totalLen; i < BLOB_CAPACITY; i++) {
    stage[i] = 0xFF;
  }
  // Program only the bytes that differ, to spare DataFlash write cycles
  for (__data blob_pos_t i = 0; i < BLOB_CAPACITY; i++) {
    if (hal_store_read(i) != stage[i]) {
      hal_store_write(i, stage[i]);
      if (hal_store_read(i) != stage[i]) {
        storage_init(); // keep using whatever is really stored
        return ST_FLASH_ERR;
      }
    }
  }

  if (!hal_store_flush()) {
    storage_init();
    return ST_FLASH_ERR;
  }
  loadStage((blob_pos_t)totalLen);
  return ST_OK;
}
