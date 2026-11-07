# test_ch32v203 build: RISC-V GCC (xPack) + WCH CH32V20x EVT SDK.
#   mingw32-make            -> build/test_ch32v203.bin
#   mingw32-make SDK=<dir>  if the SDK is somewhere else
#   mingw32-make flash      -> wchisp flash (only after `wchisp probe` works)
.RECIPEPREFIX = >

SDK ?= C:/Users/user/tools/sdk/EVT
EXAM := $(SDK)/EXAM
PERIPH := $(EXAM)/SRC/Peripheral
USBDIR := $(EXAM)/USB/USBFS/DEVICE/CompositeKM/User
LDSCRIPT := $(EXAM)/SRC/Ld/Link.ld

TARGET := test_ch32v203
BUILD := build
PREFIX ?= riscv-none-elf-
CC := $(PREFIX)gcc
OBJCOPY := $(PREFIX)objcopy
SIZE := $(PREFIX)size
WCHISP ?= wchisp

ARCH := -march=rv32imac_zicsr_zifencei -mabi=ilp32 -msmall-data-limit=8
DEFS := -DSYSCLK_FREQ_48MHz_HSI
CFLAGS := $(ARCH) $(DEFS) -Os -g -ffunction-sections -fdata-sections -Wall -Wextra -Wno-unused-parameter
ASFLAGS := $(ARCH) -x assembler-with-cpp
INCS := -I. -I$(EXAM)/SRC/Core -I$(PERIPH)/inc -I$(EXAM)/SRC/Debug -I$(USBDIR)
LDFLAGS := $(ARCH) -T $(LDSCRIPT) -nostartfiles -Wl,--gc-sections -Wl,-Map=$(BUILD)/$(TARGET).map

SRCS_ASM := startup_ch32v20x_D6.S
SRCS_C := test_ch32v203.c ch32v203_hal.c core_riscv.c system_ch32v20x.c \
          ch32v20x_usbfs_device.c usbd_compostie_km.c usbd_desc.c ch32v20x_it.c \
          ch32v20x_gpio.c ch32v20x_rcc.c ch32v20x_misc.c ch32v20x_tim.c \
          ch32v20x_usart.c ch32v20x_dma.c debug.c ch32v20x_exti.c ch32v20x_pwr.c

VPATH := . $(EXAM)/SRC/Startup $(EXAM)/SRC/Core $(EXAM)/SRC/Debug $(USBDIR) $(PERIPH)/src
OBJS := $(addprefix $(BUILD)/,$(SRCS_C:.c=.o) $(SRCS_ASM:.S=.o))

all: $(BUILD)/$(TARGET).bin

$(BUILD)/$(TARGET).elf: $(OBJS) $(LDSCRIPT)
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

$(BUILD) $(BUILD)/src:
> mkdir -p $@

flash: $(BUILD)/$(TARGET).bin
> $(WCHISP) flash $<

clean:
> rm -rf $(BUILD)

.PHONY: all flash clean
