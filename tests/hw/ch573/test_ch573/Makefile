# Pin test firmware for the CH573 (SAYO_2B3 keyboard). The test logic is shared
# with the CH32V203 test (../../ch32v203/test_ch32v203/test_ch32v203.c); this
# folder adds the CH573 HAL and pin table.
#   mingw32-make            build build/test_ch573.bin (flash address 0)
#   mingw32-make info       wchisp info (chip, UID, protection), reads only
# Flashing erases the chip: see README.md (needs the user's approval first).
.RECIPEPREFIX = >

MK_CH57X_SDK ?= C:/Users/user/tools/sdk_ch573/EVT
SDK ?= $(MK_CH57X_SDK)
EXAM := $(SDK)/EXAM
DRV := $(EXAM)/SRC/StdPeriphDriver
LDSCRIPT := $(EXAM)/SRC/Ld/Link.ld
LOGIC := ../../ch32v203/test_ch32v203

TARGET := test_ch573
BUILD := build
PREFIX ?= $(if $(MK_RISCV_BIN),$(MK_RISCV_BIN)/)riscv-none-elf-
CC := $(PREFIX)gcc
OBJCOPY := $(PREFIX)objcopy
SIZE := $(PREFIX)size
WCHISP ?= $(or $(MK_WCHISP),wchisp)

ARCH := -march=rv32imac_zicsr_zifencei -mabi=ilp32 -msmall-data-limit=8 -mno-save-restore
DEFS := -DTEST_HAL_H='"ch573_hal.h"' -DTEST_PINS_H='"ch573_pins.h"'
CFLAGS := $(ARCH) $(DEFS) -Os -g -ffunction-sections -fdata-sections -fno-common -Wall -Wextra -Wno-unused-parameter
ASFLAGS := $(ARCH) -x assembler-with-cpp
INCS := -I. -I$(DRV)/inc -I$(EXAM)/SRC/RVMSIS
LDFLAGS := $(ARCH) -T $(LDSCRIPT) -nostartfiles -Wl,--gc-sections -Wl,-Map=$(BUILD)/$(TARGET).map \
           --specs=nano.specs --specs=nosys.specs

SRCS_ASM := startup_CH573.S
SRCS_C := test_ch32v203.c ch573_hal.c core_riscv.c \
          CH57x_clk.c CH57x_gpio.c CH57x_sys.c CH57x_timer0.c CH57x_usbdev.c CH57x_pwr.c CH57x_adc.c

# vpath per type, not VPATH: VPATH would also find the logic folder's own build/src
vpath %.c . $(LOGIC) $(EXAM)/SRC/RVMSIS $(DRV)
vpath %.S $(EXAM)/SRC/Startup
OBJS := $(addprefix $(BUILD)/,$(SRCS_C:.c=.o) $(SRCS_ASM:.S=.o))

all: $(BUILD)/$(TARGET).bin

$(BUILD)/$(TARGET).elf: $(OBJS) $(LDSCRIPT)
> $(CC) $(LDFLAGS) -o $@ $(OBJS) $(DRV)/libISP573.a -lc -lgcc
> $(SIZE) $@

$(BUILD)/$(TARGET).bin: $(BUILD)/$(TARGET).elf
> $(OBJCOPY) -O binary $< $@

# Stock GCC does not know the WCH interrupt attribute; "machine" gives the same
# mret return (no hardware stacking used here)
$(BUILD)/src/%.c: %.c | $(BUILD)/src
> sed -e 's/WCH-Interrupt-fast/machine/g' $< > $@

$(BUILD)/%.o: $(BUILD)/src/%.c | $(BUILD)
> $(CC) $(CFLAGS) $(INCS) -I$(LOGIC) -MMD -MP -c $< -o $@

$(BUILD)/%.o: %.S | $(BUILD)
> $(CC) $(ASFLAGS) -c $< -o $@

$(BUILD) $(BUILD)/src:
> mkdir -p $@

info:
> $(WCHISP) info

clean:
> rm -rf $(BUILD)

-include $(OBJS:.o=.d)

.PHONY: all info clean
.SECONDARY:
