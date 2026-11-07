#ifndef __MK_BOOT_LAYOUT_H__
#define __MK_BOOT_LAYOUT_H__

// Flash layout shared by the CH32V203 app and its own bootloader
// (firmware/bootloader_ch32v203, docs/BOOTLOADER_CH32V203.md).
// Flash is also mapped at 0x00000000; these are the 0x08000000 addresses.

#define BOOT_FLASH_BASE 0x08000000u
#define BOOT_LOADER_SIZE 0x2000u               // 8 KB
#define BOOT_APP_ADDR 0x08002000u              // app vector table / _start
#define BOOT_INFO_ADDR 0x0800EF00u             // app info page (256 B)
#define BOOT_APP_MAX (BOOT_INFO_ADDR - BOOT_APP_ADDR) // 0xCF00 = 52992 B
#define BOOT_STORE_ADDR 0x0800F000u            // config store, kept by updates
#define BOOT_PAGE_SIZE 256u                    // fast-program page

// App info at BOOT_INFO_ADDR, written by the bootloader after CHECK
#define BOOT_INFO_MAGIC 0x31505041u // "APP1"
typedef struct {
  unsigned long magic;  // BOOT_INFO_MAGIC
  unsigned long length; // app bytes from BOOT_APP_ADDR
  unsigned long crc32;  // zlib CRC32 of those bytes
  unsigned long check;  // ~magic ^ length ^ crc32
} boot_info_t;

// RAM mark: last 16 bytes of RAM (both linkers end the stack below it)
#define BOOT_MARK_ADDR 0x20004FF0u
#define BOOT_MARK_ENTER 0xB0075EEDu // app BOOT command: stay in the bootloader

#endif
