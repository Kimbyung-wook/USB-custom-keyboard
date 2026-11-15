# JieLi USB_KEY sender on a Blue Pill (STM32F103C8), bare metal: Arm GCC (xPack).
#   mingw32-make  -> build/jl_usbkey_bluepill.bin (from 0x08000000), .elf, .hex
# Compiler: $(MK_ARM_BIN) if set, else the xPack install below.
# After editing this file run `rm -rf build`.
.RECIPEPREFIX = >

MK_ARM_BIN ?= C:/Users/user/tools/gcc/xpack-arm-none-eabi-gcc-15.2.1-1.1/bin
PREFIX ?= $(MK_ARM_BIN)/arm-none-eabi-
CC := $(PREFIX)gcc
OBJCOPY := $(PREFIX)objcopy
SIZE := $(PREFIX)size

TARGET := jl_usbkey_bluepill
BUILD := build
ARCH := -mcpu=cortex-m3 -mthumb
CFLAGS := $(ARCH) -MMD -MP -Os -g -std=c11 -ffunction-sections -fdata-sections -ffreestanding -Wall -Wextra
LDFLAGS := $(ARCH) -T stm32f103c8.ld -nostdlib -nostartfiles -Wl,--gc-sections -Wl,-Map=$(BUILD)/$(TARGET).map
SRCS := startup.c main.c
OBJS := $(addprefix $(BUILD)/,$(SRCS:.c=.o))

all: $(BUILD)/$(TARGET).bin $(BUILD)/$(TARGET).hex

$(BUILD)/$(TARGET).elf: $(OBJS) stm32f103c8.ld
> $(CC) $(LDFLAGS) -o $@ $(OBJS) -lgcc
> $(SIZE) $@

$(BUILD)/$(TARGET).bin: $(BUILD)/$(TARGET).elf
> $(OBJCOPY) -O binary $< $@

$(BUILD)/$(TARGET).hex: $(BUILD)/$(TARGET).elf
> $(OBJCOPY) -O ihex $< $@

$(BUILD)/%.o: %.c | $(BUILD)
> $(CC) $(CFLAGS) -c $< -o $@

$(BUILD):
> mkdir -p $@

-include $(OBJS:.o=.d)

clean:
> rm -rf $(BUILD)

.PHONY: all clean
