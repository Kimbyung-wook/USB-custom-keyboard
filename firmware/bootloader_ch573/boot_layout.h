#ifndef __MK_BOOT_LAYOUT_CH57X_H__
#define __MK_BOOT_LAYOUT_CH57X_H__

// Flash layout shared by the CH573 bootloader (firmware/bootloader_ch573) and
// the apps linked for it. Code flash starts at 0 and erases in 4 KB blocks.
//   0x00000-0x01FFF  bootloader (8 KB; about 4.6 KB used)
//   0x02000-0x02FFF  app info (own erase block, first 16 B used)
//   0x03000-0x6FFFF  app (_start at 0x3000)
// Data EEPROM (32 KB, separate) is not touched: the app's config store and the
// BLE bonding area (BLE_SNV_ADDR, EEPROM 0x7E00) live there.
// Makefile and mkfull.py read the numbers from this file.

#define BOOT_LOADER_SIZE 0x2000u
#define BOOT_INFO_ADDR 0x2000u
#define BOOT_APP_ADDR 0x3000u
#define BOOT_FLASH_SIZE 0x70000u                         // CH573: 448 KB
#define BOOT_APP_MAX (BOOT_FLASH_SIZE - BOOT_APP_ADDR)   // 0x6D000 = 446464 B
#define BOOT_PAGE_SIZE 256u                              // write buffer
#define BOOT_BLOCK_SIZE 4096u                            // erase block

// App info at BOOT_INFO_ADDR, written by the bootloader after CHECK (same as
// the CH32V203 bootloader)
#define BOOT_INFO_MAGIC 0x31505041u // "APP1"
typedef struct {
  unsigned long magic;  // BOOT_INFO_MAGIC
  unsigned long length; // app bytes from BOOT_APP_ADDR
  unsigned long crc32;  // zlib CRC32 of those bytes
  unsigned long check;  // ~magic ^ length ^ crc32
} boot_info_t;

// Enter mark: R8_GLOB_RESET_KEEP (0x40001047) keeps its value through a
// software reset, power-on clears it. The app's BOOT command writes this
// value, detaches USB, waits 200 ms and resets (SYS_ResetExecute).
#define BOOT_MARK_ENTER 0xB0u

#endif
