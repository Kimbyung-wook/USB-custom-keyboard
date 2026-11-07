# Diagnostic build: blink PC13, no USB. Same toolchain and SDK startup as ../Makefile.
.RECIPEPREFIX = >

# Tool paths: environment MK_EVT_SDK, MK_RISCV_BIN (folder of riscv-none-elf-gcc,
# else PATH), MK_WCHISP; otherwise these defaults (same in tools/*.ps1)
MK_EVT_SDK ?= C:/Users/user/tools/sdk/EVT
SDK ?= $(MK_EVT_SDK)
EXAM := $(SDK)/EXAM
LDSCRIPT := $(EXAM)/SRC/Ld/Link.ld
PREFIX ?= $(if $(MK_RISCV_BIN),$(MK_RISCV_BIN)/)riscv-none-elf-
CC := $(PREFIX)gcc
OBJCOPY := $(PREFIX)objcopy
ARCH := -march=rv32imac_zicsr_zifencei -mabi=ilp32 -msmall-data-limit=8
DEFS := -DSYSCLK_FREQ_48MHz_HSI
CFLAGS := $(ARCH) $(DEFS) -Os -ffunction-sections -fdata-sections -Wall -Wextra
LDFLAGS := $(ARCH) -T $(LDSCRIPT) -nostartfiles -Wl,--gc-sections
INCS := -I$(EXAM)/SRC/Core -I$(EXAM)/SRC/Peripheral/inc -I$(EXAM)/USB/USBFS/DEVICE/CompositeKM/User -I$(EXAM)/SRC/Debug
VPATH := $(EXAM)/SRC/Startup $(EXAM)/SRC/Core $(EXAM)/SRC/Peripheral/src $(EXAM)/USB/USBFS/DEVICE/CompositeKM/User

OBJS := build/diag_blink.o build/startup_ch32v20x_D6.o build/system_ch32v20x.o build/core_riscv.o

all: build/diag_blink.bin

build:
> mkdir -p $@

build/sys/system_ch32v20x.c: system_ch32v20x.c | build
> mkdir -p build/sys
> sed -e 's/WCH-Interrupt-fast/machine/g' -e 's|^#define SYSCLK_FREQ_96MHz_HSE|//&|' $< > $@

build/system_ch32v20x.o: build/sys/system_ch32v20x.c | build
> $(CC) $(CFLAGS) $(INCS) -c $< -o $@

build/%.o: %.c | build
> $(CC) $(CFLAGS) $(INCS) -c $< -o $@

build/%.o: %.S | build
> $(CC) $(ARCH) -x assembler-with-cpp $(INCS) -c $< -o $@

build/diag_blink.elf: $(OBJS)
> $(CC) $(LDFLAGS) -o $@ $(OBJS) -lgcc

build/diag_blink.bin: build/diag_blink.elf
> $(OBJCOPY) -O binary $< $@

.PHONY: all
