# test_ch32v203 build: RISC-V GCC (xPack) + WCH CH32V20x EVT SDK.
#   mingw32-make            -> build/test_ch32v203.bin
#   mingw32-make SDK=<dir>  if the SDK is somewhere else
#   mingw32-make flash      -> wchisp flash (only after `wchisp probe` works)
#   mingw32-make APP=1      -> build/app/test_ch32v203-app.bin, linked at 0x08002000
#                              behind our bootloader (firmware/bootloader_ch32v203):
#                              hidboot --force writes it, no BOOT bridge
.RECIPEPREFIX = >

# Tool paths: environment MK_EVT_SDK, MK_RISCV_BIN (folder of riscv-none-elf-gcc,
# else PATH), MK_WCHISP; otherwise these defaults (same in tools/*.ps1)
MK_EVT_SDK ?= C:/Users/user/tools/sdk/EVT
SDK ?= $(MK_EVT_SDK)
EXAM := $(SDK)/EXAM
PERIPH := $(EXAM)/SRC/Peripheral
USBDIR := $(EXAM)/USB/USBFS/DEVICE/CompositeKM/User
LDSCRIPT := $(EXAM)/SRC/Ld/Link.ld

APP ?= 0
ifeq ($(APP),1)
# Same layout as the app (firmware/macro_keyboard/Makefile.ch32v203): flash
# 0x2000..0xEEFF, RAM ends 16 bytes early for the bootloader RAM mark
TARGET := test_ch32v203-app
BUILD := build/app
APP_LD := $(BUILD)/link.ld
else
TARGET := test_ch32v203
BUILD := build
APP_LD :=
endif
PREFIX ?= $(if $(MK_RISCV_BIN),$(MK_RISCV_BIN)/)riscv-none-elf-
CC := $(PREFIX)gcc
OBJCOPY := $(PREFIX)objcopy
SIZE := $(PREFIX)size
WCHISP ?= $(or $(MK_WCHISP),wchisp)

ARCH := -march=rv32imac_zicsr_zifencei -mabi=ilp32 -msmall-data-limit=8
DEFS := -DSYSCLK_FREQ_48MHz_HSI
CFLAGS := $(ARCH) $(DEFS) -Os -g -ffunction-sections -fdata-sections -Wall -Wextra -Wno-unused-parameter
ASFLAGS := $(ARCH) -x assembler-with-cpp
INCS := -I. -I$(EXAM)/SRC/Core -I$(PERIPH)/inc -I$(EXAM)/SRC/Debug -I$(USBDIR)
LDFLAGS := $(ARCH) -T $(or $(APP_LD),$(LDSCRIPT)) -nostartfiles -Wl,--gc-sections -Wl,-Map=$(BUILD)/$(TARGET).map

SRCS_ASM := startup_ch32v20x_D6.S
SRCS_C := test_ch32v203.c ch32v203_hal.c core_riscv.c system_ch32v20x.c \
          ch32v20x_usbfs_device.c usbd_compostie_km.c usbd_desc.c ch32v20x_it.c \
          ch32v20x_gpio.c ch32v20x_rcc.c ch32v20x_misc.c ch32v20x_tim.c \
          ch32v20x_usart.c ch32v20x_dma.c debug.c ch32v20x_exti.c ch32v20x_pwr.c

VPATH := . $(EXAM)/SRC/Startup $(EXAM)/SRC/Core $(EXAM)/SRC/Debug $(USBDIR) $(PERIPH)/src
OBJS := $(addprefix $(BUILD)/,$(SRCS_C:.c=.o) $(SRCS_ASM:.S=.o))

all: $(BUILD)/$(TARGET).bin

$(BUILD)/$(TARGET).elf: $(OBJS) $(LDSCRIPT) $(APP_LD)
> $(CC) $(LDFLAGS) -o $@ $(OBJS) -lc -lgcc
> $(SIZE) $@

$(BUILD)/$(TARGET).bin: $(BUILD)/$(TARGET).elf
> $(OBJCOPY) -O binary $< $@

# Stock GCC does not know the WCH interrupt attribute; "machine" gives the same
# prologue and mret return. Each C file is copied with that replacement first.
$(BUILD)/src/%.c: %.c | $(BUILD)/src
> sed -e 's/WCH-Interrupt-fast/machine/g' -e 's|^#define SYSCLK_FREQ_96MHz_HSE|//&|' -e 's/NVIC_SystemReset();/{ extern void hal_fault_record(void); hal_fault_record(); NVIC_SystemReset(); }/' $< > $@

$(BUILD)/%.o: $(BUILD)/src/%.c | $(BUILD)
> $(CC) $(CFLAGS) $(INCS) -c $< -o $@

# Startup file changes:
# - no csrw 0xbc0: the ISP bootloader writes it (kept from the first jump test)
# - mstatus 0x1888: enter main in machine mode (SDK 0x88 = user mode, where the
#   bootloader's machine CSR writes fault after a jump)
# - boot_check.inc right after the stack setup: RAM mark -> system flash
$(BUILD)/src/%.S: %.S boot_check.inc | $(BUILD)/src
> sed -e '/csrw[[:space:]]*0xbc0/d' -e 's/li t0, 0x88[[:space:]]*$$/li t0, 0x1888/' -e '/la sp, _eusrstack/r boot_check.inc' $< > $@

$(BUILD)/%.o: $(BUILD)/src/%.S | $(BUILD)
> $(CC) $(ASFLAGS) $(INCS) -c $< -o $@

build/app/link.ld: $(LDSCRIPT) Makefile | $(BUILD)
> sed -e 's/ORIGIN = 0x00000000, LENGTH = 64K/ORIGIN = 0x00002000, LENGTH = 52992/' -e 's/LENGTH = 20K/LENGTH = 0x4FF0/' $< > $@
> @grep -q 'ORIGIN = 0x00002000' $@ && grep -q 'LENGTH = 0x4FF0' $@ || { echo "ERROR: link script not relocated"; rm -f $@; exit 1; }

$(BUILD) $(BUILD)/src:
> mkdir -p $@

flash: $(BUILD)/$(TARGET).bin
> @if [ "$(APP)" = 1 ]; then echo "APP=1: write it with hidboot --force (tools\flash.ps1 -Board ... -Bin ... -Force)"; exit 1; fi
> $(WCHISP) flash $<

clean:
> rm -rf $(BUILD)

.PHONY: all flash clean
