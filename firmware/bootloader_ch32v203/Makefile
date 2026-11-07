# CH32V203 bootloader (docs/BOOTLOADER_CH32V203.md): RISC-V GCC (xPack) + WCH EVT SDK.
#   mingw32-make              -> build/bootloader-3X3V_RGB.bin (<= 8 KB)
#   mingw32-make full         -> also builds the app and build/full-3X3V_RGB.bin
#                                (bootloader + app + app info) for the first install
#   mingw32-make flash        -> wchisp flash of the full image (BOOT bridge connected)
# After editing this file run `rm -rf build`.
.RECIPEPREFIX = >

# Tool paths: environment MK_EVT_SDK, MK_RISCV_BIN (folder of riscv-none-elf-gcc,
# else PATH), MK_WCHISP; otherwise these defaults (same in tools/*.ps1)
MK_EVT_SDK ?= C:/Users/user/tools/sdk/EVT
SDK ?= $(MK_EVT_SDK)
EXAM := $(SDK)/EXAM
PERIPH := $(EXAM)/SRC/Peripheral
BOARD ?= 3X3V_RGB
BL_MAX := 8192
APPDIR := ../macro_keyboard
APPBIN := $(APPDIR)/build/ch32v203/macro_keyboard-$(BOARD).bin

TARGET := bootloader-$(BOARD)
BUILD := build
PREFIX ?= $(if $(MK_RISCV_BIN),$(MK_RISCV_BIN)/)riscv-none-elf-
CC := $(PREFIX)gcc
OBJCOPY := $(PREFIX)objcopy
SIZE := $(PREFIX)size
WCHISP ?= $(or $(MK_WCHISP),wchisp)
PYTHON ?= python

ARCH := -march=rv32imac_zicsr_zifencei -mabi=ilp32 -msmall-data-limit=8
# Pins, name and boot key from the app's board file (src/boards/<board>.h)
BOARD_FILE := $(APPDIR)/src/boards/$(shell echo $(BOARD) | tr A-Z a-z).h
DEFS := -DSYSCLK_FREQ_48MHz_HSI -include $(BOARD_FILE)
# DIAG=1: LEDs show how far USB enumeration got (main.c)
ifeq ($(DIAG),1)
DEFS += -DDIAG
endif
CFLAGS := $(ARCH) $(DEFS) -MMD -MP -Os -g -ffunction-sections -fdata-sections -Wall -Wextra -Wno-unused-parameter
ASFLAGS := $(ARCH) -x assembler-with-cpp
USERD := $(EXAM)/USB/USBFS/DEVICE/CompositeKM/User # system_ch32v20x
INCS := -I. -I$(APPDIR)/src/hal/ch32v203 -I$(EXAM)/SRC/Core -I$(EXAM)/SRC/Debug -I$(PERIPH)/inc -I$(USERD)
LDSCRIPT := $(BUILD)/link.ld
LDFLAGS := $(ARCH) -T $(LDSCRIPT) -nostartfiles -Wl,--gc-sections -Wl,-Map=$(BUILD)/$(TARGET).map

SRCS_ASM := startup_ch32v20x_D6.S
SRCS_C := main.c usb_hid.c system_ch32v20x.c ch32v20x_flash.c
VPATH := . $(EXAM)/SRC/Startup $(PERIPH)/src $(USERD)
# Objects per board (and DIAG): the board file is compiled in
OBJDIR := $(BUILD)/obj-$(BOARD)$(if $(filter 1,$(DIAG)),-diag)
OBJS := $(addprefix $(OBJDIR)/,$(SRCS_C:.c=.o) $(SRCS_ASM:.S=.o))

all: $(BUILD)/$(TARGET).bin

# Flash 8 KB; RAM ends 16 bytes early: the RAM mark (boot_layout.h) sits above the stack
$(LDSCRIPT): $(EXAM)/SRC/Ld/Link.ld | $(BUILD)
> sed -e 's/LENGTH = 64K/LENGTH = 8K/' -e 's/LENGTH = 20K/LENGTH = 0x4FF0/' $< > $@

$(BUILD)/$(TARGET).elf: $(OBJS) $(LDSCRIPT)
> $(CC) $(LDFLAGS) -o $@ $(OBJS) -lc -lgcc
> $(SIZE) $@

$(BUILD)/$(TARGET).bin: $(BUILD)/$(TARGET).elf
> $(OBJCOPY) -O binary $< $@
> @n=$$(wc -c < $@); if [ $$n -gt $(BL_MAX) ]; then echo "ERROR: bootloader $$n bytes > $(BL_MAX)"; rm -f $@; exit 1; fi

# SDK sources: WCH interrupt attribute -> machine, 96 MHz HSE define off
$(BUILD)/src/%.c: %.c | $(BUILD)/src
> sed -e 's/WCH-Interrupt-fast/machine/g' -e 's|^#define SYSCLK_FREQ_96MHz_HSE|//&|' $< > $@

$(OBJDIR)/%.o: $(BUILD)/src/%.c $(BOARD_FILE) | $(OBJDIR)
> $(CC) $(CFLAGS) $(INCS) -c $< -o $@

# No CSR 0xbc0 write (write-once); main in machine mode (mstatus MPP = 3) so the
# app's start-up code may write machine CSRs after the jump
$(BUILD)/src/%.S: %.S | $(BUILD)/src
> sed -e '/csrw[[:space:]]*0xbc0/d' -e 's/li t0, 0x88[[:space:]]*$$/li t0, 0x1888/' $< > $@

$(OBJDIR)/%.o: $(BUILD)/src/%.S | $(OBJDIR)
> $(CC) $(ASFLAGS) $(INCS) -c $< -o $@

$(BUILD) $(BUILD)/src $(OBJDIR):
> mkdir -p $@

app:
> $(MAKE) -C $(APPDIR) -f Makefile.ch32v203 BOARD=$(BOARD) EXTRA_DEFS="$(EXTRA_DEFS)"

full: $(BUILD)/$(TARGET).bin app
> $(PYTHON) mkfull.py $(BUILD)/$(TARGET).bin $(APPBIN) $(BUILD)/full-$(BOARD).bin

flash: full
> $(WCHISP) flash $(BUILD)/full-$(BOARD).bin

# Header dependencies (-MMD -MP): boot_layout.h and the board file rebuild their objects
-include $(OBJS:.o=.d)

clean:
> rm -rf $(BUILD)

.PHONY: all app full flash clean
.PRECIOUS: $(BUILD)/src/%.c $(BUILD)/src/%.S
