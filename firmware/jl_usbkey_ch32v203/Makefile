# JieLi USB_KEY adapter on a CH32V203C8 (main.c): RISC-V GCC (xPack) + WCH EVT SDK.
#   mingw32-make          -> build/jl_usbkey.bin, a whole-chip image at 0x08000000
#                            (no bootloader of ours: flashed with wchisp over the
#                            BOOT bridge, like a first install; needs the user's ok)
# USB code is the bootloader's (../bootloader_ch32v203/usb_hid.c) with PID 0xC560.
# After editing this file run `rm -rf build`.
.RECIPEPREFIX = >

MK_EVT_SDK ?= C:/Users/user/tools/sdk/EVT
SDK ?= $(MK_EVT_SDK)
EXAM := $(SDK)/EXAM
PERIPH := $(EXAM)/SRC/Peripheral
BLDIR := ../bootloader_ch32v203

TARGET := jl_usbkey
BUILD := build
PREFIX ?= $(if $(MK_RISCV_BIN),$(MK_RISCV_BIN)/)riscv-none-elf-
CC := $(PREFIX)gcc
OBJCOPY := $(PREFIX)objcopy
SIZE := $(PREFIX)size

ARCH := -march=rv32imac_zicsr_zifencei -mabi=ilp32 -msmall-data-limit=8
DEFS := -DSYSCLK_FREQ_48MHz_HSI -DUSB_PID=0xC560 '-DUSB_PRODUCT="JieLi USB_KEY adapter"'
CFLAGS := $(ARCH) $(DEFS) -MMD -MP -Os -g -ffunction-sections -fdata-sections -Wall -Wextra -Wno-unused-parameter
ASFLAGS := $(ARCH) -x assembler-with-cpp
USERD := $(EXAM)/USB/USBFS/DEVICE/CompositeKM/User # system_ch32v20x
INCS := -I. -I$(BLDIR) -I$(EXAM)/SRC/Core -I$(EXAM)/SRC/Debug -I$(PERIPH)/inc -I$(USERD)
LDSCRIPT := $(EXAM)/SRC/Ld/Link.ld
LDFLAGS := $(ARCH) -T $(LDSCRIPT) -nostartfiles -Wl,--gc-sections -Wl,-Map=$(BUILD)/$(TARGET).map

SRCS_ASM := startup_ch32v20x_D6.S
SRCS_C := main.c usb_hid.c system_ch32v20x.c
# vpath per type, not VPATH: VPATH would also find the bootloader's own build/src
vpath %.c . $(BLDIR) $(PERIPH)/src $(USERD)
vpath %.S $(EXAM)/SRC/Startup
OBJDIR := $(BUILD)/obj
OBJS := $(addprefix $(OBJDIR)/,$(SRCS_C:.c=.o) $(SRCS_ASM:.S=.o))

all: $(BUILD)/$(TARGET).bin

$(BUILD)/$(TARGET).elf: $(OBJS)
> $(CC) $(LDFLAGS) -o $@ $(OBJS) -lc -lgcc
> $(SIZE) $@

$(BUILD)/$(TARGET).bin: $(BUILD)/$(TARGET).elf
> $(OBJCOPY) -O binary $< $@

# SDK sources: WCH interrupt attribute -> machine, 96 MHz HSE define off (as the bootloader)
$(BUILD)/src/%.c: %.c | $(BUILD)/src
> sed -e 's/WCH-Interrupt-fast/machine/g' -e 's|^#define SYSCLK_FREQ_96MHz_HSE|//&|' $< > $@

$(OBJDIR)/%.o: $(BUILD)/src/%.c | $(OBJDIR)
> $(CC) $(CFLAGS) $(INCS) -c $< -o $@

$(BUILD)/src/%.S: %.S | $(BUILD)/src
> cp $< $@

$(OBJDIR)/%.o: $(BUILD)/src/%.S | $(OBJDIR)
> $(CC) $(ASFLAGS) $(INCS) -c $< -o $@

$(BUILD) $(BUILD)/src $(OBJDIR):
> mkdir -p $@

-include $(OBJS:.o=.d)

clean:
> rm -rf $(BUILD)

.PHONY: all clean
.PRECIOUS: $(BUILD)/src/%.c $(BUILD)/src/%.S
