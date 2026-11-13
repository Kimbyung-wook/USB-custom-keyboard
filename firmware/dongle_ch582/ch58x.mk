# Shared CH58x (CH582F) build rules: xPack riscv-none-elf-gcc 15 + WCH CH583EVT SDK.
# Used by firmware/dongle_ch582/Makefile and tests/hw/ch582_dongle/Makefile.
#
# The including Makefile sets:
#   TARGET   output name (BUILD/TARGET.bin, .hex, .elf, .map)
#   BUILD    build folder
#   SRCS     own C files (paths)
#   PERIPH   StdPeriphDriver file names (CH58x_xxx.c)
#   BLE=1    link the BLE library (libCH58xBLE.a)
#   INCS     extra -I flags, DEFS extra -D flags
#
# Tool paths: environment MK_CH58X_SDK (folder EVT/EXAM of the CH583EVT SDK),
# MK_RISCV_BIN (folder of riscv-none-elf-gcc), MK_WCHISP; otherwise the defaults below.
.RECIPEPREFIX = >

MK_CH58X_SDK ?= C:/Users/user/tools/sdk_ch583/EVT/EXAM
SDK ?= $(MK_CH58X_SDK)
MK_RISCV_BIN ?= C:/Users/user/tools/gcc/xpack-riscv-none-elf-gcc-15.2.0-1/bin
PREFIX ?= $(MK_RISCV_BIN)/riscv-none-elf-
CC := $(PREFIX)gcc
OBJCOPY := $(PREFIX)objcopy
SIZE := $(PREFIX)size
WCHISP ?= $(or $(MK_WCHISP),C:/Users/user/tools/wchisp/wchisp-win-x64/wchisp.exe)

DRV := $(SDK)/SRC/StdPeriphDriver
HERE_MK := $(dir $(lastword $(MAKEFILE_LIST)))

ARCH := -march=rv32imac_zicsr_zifencei -mabi=ilp32 -msmall-data-limit=8 -mno-save-restore
# ch58x_fix.h replaces the SDK's interrupt attribute "WCH-Interrupt-fast"
# (WCH's own compiler only) with the standard "machine" one, same as the
# sed step of the CH32V203 build, but without copying SDK files
CFLAGS := $(ARCH) -Os -g -ffunction-sections -fdata-sections -fno-common \
          -Wall -Wextra -Wno-unused-parameter -std=gnu11 \
          -include $(HERE_MK)src/ch58x_fix.h $(DEFS) \
          -I$(HERE_MK)src -I$(DRV)/inc -I$(SDK)/SRC/RVMSIS $(if $(BLE),-I$(SDK)/BLE/LIB) $(INCS)
# SDK sources: no -Wextra noise
SDK_CFLAGS := $(filter-out -Wextra,$(CFLAGS)) -Wno-unused-variable -Wno-unused-but-set-variable
ASFLAGS := $(ARCH) -x assembler-with-cpp
LIBS := $(DRV)/libISP583.a $(if $(BLE),$(SDK)/BLE/LIB/libCH58xBLE.a)
LDFLAGS := $(ARCH) -nostartfiles -Wl,--gc-sections -Wl,--print-memory-usage \
           -Wl,-Map=$(BUILD)/$(TARGET).map -T $(SDK)/SRC/Ld/Link.ld \
           --specs=nano.specs --specs=nosys.specs

OWN_OBJS := $(addprefix $(BUILD)/own/,$(notdir $(SRCS:.c=.o)))
SDK_OBJS := $(addprefix $(BUILD)/sdk/,$(PERIPH:.c=.o) core_riscv.o startup_CH583.o)
OBJS := $(OWN_OBJS) $(SDK_OBJS)

vpath %.c $(sort $(dir $(SRCS)))

all: $(BUILD)/$(TARGET).bin

$(BUILD)/own/%.o: %.c | $(BUILD)/own
> $(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/sdk/%.o: $(DRV)/%.c | $(BUILD)/sdk
> $(CC) $(SDK_CFLAGS) -c $< -o $@

$(BUILD)/sdk/core_riscv.o: $(SDK)/SRC/RVMSIS/core_riscv.c | $(BUILD)/sdk
> $(CC) $(SDK_CFLAGS) -c $< -o $@

$(BUILD)/sdk/startup_CH583.o: $(SDK)/SRC/Startup/startup_CH583.S | $(BUILD)/sdk
> $(CC) $(ASFLAGS) -c $< -o $@

$(BUILD)/own $(BUILD)/sdk:
> mkdir -p $@

$(BUILD)/$(TARGET).elf: $(OBJS)
> $(CC) $(LDFLAGS) $(OBJS) $(LIBS) -o $@
> $(SIZE) $@

$(BUILD)/$(TARGET).bin: $(BUILD)/$(TARGET).elf
> $(OBJCOPY) -O binary $< $@
> $(OBJCOPY) -O ihex $< $(BUILD)/$(TARGET).hex

# Writes the chip. Only after the user's approval (README: 장입)
flash: $(BUILD)/$(TARGET).bin
> $(WCHISP) flash $<

clean:
> rm -rf $(BUILD)

.PHONY: all flash clean
-include $(OWN_OBJS:.o=.d)
