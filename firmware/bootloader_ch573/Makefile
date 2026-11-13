# CH573 bootloader (boot_layout.h) and apps linked for it, from the SDK
# (MK_CH57X_SDK, CH573EVT). Nothing here flashes unless asked:
#   mingw32-make               build/bootloader-$(BOARD).bin (<= BOOT_LOADER_SIZE, flash 0)
#   mingw32-make test-app      pin test firmware (tests/hw/ch573/test_ch573) at BOOT_APP_ADDR
#   mingw32-make ble-app       SDK example BLE/HID_Keyboard at BOOT_APP_ADDR
#   mingw32-make full APP=test|ble   bootloader + app info + app (first install)
#   mingw32-make flash APP=test|ble  wchisp flash of that image (ROM bootloader: PB22 low + SW1)
# Apps for this bootloader need only ORIGIN = BOOT_APP_ADDR in the link script
# (build/app.ld); the SDK start-up code stays as it is.
# After editing this file delete the build folder.
.RECIPEPREFIX = >

MK_CH57X_SDK ?= C:/Users/user/tools/sdk_ch573/EVT
SDK ?= $(MK_CH57X_SDK)
EXAM := $(SDK)/EXAM
DRV := $(EXAM)/SRC/StdPeriphDriver
BLE := $(EXAM)/BLE
BOARD ?= SAYO_2B3
APP ?= test
TESTDIR := ../../tests/hw/ch573/test_ch573

BUILD := build
PREFIX ?= $(if $(MK_RISCV_BIN),$(MK_RISCV_BIN)/)riscv-none-elf-
CC := $(PREFIX)gcc
OBJCOPY := $(PREFIX)objcopy
SIZE := $(PREFIX)size
WCHISP ?= $(or $(MK_WCHISP),wchisp)
PYTHON ?= python

# Layout numbers from boot_layout.h
layout = $(shell sed -n 's/^\#define $(1) \(0x[0-9A-Fa-f]*\)u.*/\1/p' boot_layout.h)
BL_SIZE := $(call layout,BOOT_LOADER_SIZE)
APP_ADDR := $(call layout,BOOT_APP_ADDR)
FLASH_SIZE := $(call layout,BOOT_FLASH_SIZE)
APP_LEN := $(shell printf '0x%X' $$(($(FLASH_SIZE) - $(APP_ADDR))))

ARCH := -march=rv32imac_zicsr_zifencei -mabi=ilp32 -msmall-data-limit=8 -mno-save-restore
WARN := -Wall -Wextra -Wno-unused-parameter
CBASE := $(ARCH) -Os -g -ffunction-sections -fdata-sections -fno-common -MMD -MP
ASFLAGS := $(ARCH) -x assembler-with-cpp
LDBASE := $(ARCH) -nostartfiles -Wl,--gc-sections --specs=nano.specs --specs=nosys.specs
INC_DRV := -I$(DRV)/inc -I$(EXAM)/SRC/RVMSIS

# ---- bootloader ----
BOARD_FILE := boards/$(shell echo $(BOARD) | tr A-Z a-z).h
BL := bootloader-$(BOARD)
BL_OBJS := $(addprefix $(BUILD)/bl-$(BOARD)/,main.o chip_ch57x.o usb_hid.o CH57x_sys.o CH57x_clk.o startup_CH573.o)
# Test build: make BUILD=build/comp BL_USB_COMPOSITE=1 (usb_hid.c: the app's descriptors)
BL_USB_COMPOSITE ?= 0
# Diagnostic build: make BUILD=build/diag BL_USB_DIAG=1 (USB stages on the LEDs)
BL_USB_DIAG ?= 0
CFLAGS_BL := $(CBASE) $(WARN) -include $(BOARD_FILE) -I. $(INC_DRV) -DBL_USB_COMPOSITE=$(BL_USB_COMPOSITE) -DBL_USB_DIAG=$(BL_USB_DIAG)

# ---- pin test app: its own Makefile, with our link script and build folder ----
TEST_BUILD := $(abspath $(BUILD))/test

# ---- BLE HID keyboard example (SDK sources as they are) ----
KBD := $(BLE)/HID_Keyboard
BLE_C := hidkbd.c hidkbd_main.c battservice.c devinfoservice.c hiddev.c hidkbdservice.c \
         scanparamservice.c KEY.c LED.c MCU.c RTC.c SLEEP.c core_riscv.c \
         $(notdir $(filter-out %usbhostBase.c %usbhostClass.c,$(wildcard $(DRV)/CH57x_*.c)))
BLE_OBJS := $(addprefix $(BUILD)/ble/,$(BLE_C:.c=.o) startup_CH573.o)
CFLAGS_BLE := $(CBASE) -w -I$(KBD)/APP/include -I$(KBD)/Profile/include -I$(BLE)/HAL/include \
              -I$(BLE)/LIB $(INC_DRV)

vpath %.c . $(EXAM)/SRC/RVMSIS $(DRV) $(KBD)/APP $(KBD)/Profile $(BLE)/HAL
vpath %.S $(EXAM)/SRC/Startup

all: $(BUILD)/$(BL).bin
test-app: $(BUILD)/test_ch573-app.bin
ble-app: $(BUILD)/hid_keyboard-app.bin

APPBIN_test := $(BUILD)/test_ch573-app.bin
APPBIN_ble := $(BUILD)/hid_keyboard-app.bin
FULL := $(BUILD)/full-$(BOARD)-$(APP).bin

# Link scripts from the SDK's: bootloader at 0 (BOOT_LOADER_SIZE); app at BOOT_APP_ADDR
$(BUILD)/bl.ld: $(EXAM)/SRC/Ld/Link.ld | $(BUILD)
> sed -e 's/LENGTH = 448K/LENGTH = $(BL_SIZE)/' $< > $@
> grep -q 'LENGTH = $(BL_SIZE)' $@
$(BUILD)/app.ld: $(EXAM)/SRC/Ld/Link.ld | $(BUILD)
> sed -e 's/ORIGIN = 0x00000000, LENGTH = 448K/ORIGIN = $(APP_ADDR), LENGTH = $(APP_LEN)/' $< > $@
> grep -q 'ORIGIN = $(APP_ADDR)' $@

$(BUILD)/$(BL).elf: $(BL_OBJS) $(BUILD)/bl.ld
> $(CC) $(LDBASE) -T $(BUILD)/bl.ld -Wl,-Map=$(BUILD)/$(BL).map -o $@ $(BL_OBJS) $(DRV)/libISP573.a -lc -lgcc
> $(SIZE) $@
$(BUILD)/$(BL).bin: $(BUILD)/$(BL).elf
> $(OBJCOPY) -O binary $< $@
> @n=$$(wc -c < $@); if [ $$n -gt $$(($(BL_SIZE))) ]; then echo "ERROR: bootloader $$n bytes > $(BL_SIZE)"; rm -f $@; exit 1; fi

# Pin test: its own Makefile decides the sources (it may change)
$(BUILD)/test_ch573-app.bin: $(BUILD)/app.ld FORCE
> $(MAKE) -C $(TESTDIR) BUILD=$(TEST_BUILD) LDSCRIPT=$(abspath $(BUILD))/app.ld
> cp $(TEST_BUILD)/test_ch573.bin $@

$(BUILD)/hid_keyboard-app.elf: $(BLE_OBJS) $(BUILD)/app.ld
> $(CC) $(LDBASE) -T $(BUILD)/app.ld -Wl,-Map=$(BUILD)/hid_keyboard-app.map -o $@ $(BLE_OBJS) \
    $(BLE)/LIB/libCH57xBLE.a $(DRV)/libISP573.a -lc -lgcc -lm
> $(SIZE) $@
$(BUILD)/hid_keyboard-app.bin: $(BUILD)/hid_keyboard-app.elf
> $(OBJCOPY) -O binary $< $@

# C: WCH interrupt attribute -> machine (same mret, software stacking)
$(BUILD)/pp/%.c: %.c | $(BUILD)/pp
> sed -e 's/WCH-Interrupt-fast/machine/g' $< > $@
# Bootloader start-up: mstatus MPP = 3, so its main runs in machine mode (as
# the SDK's IAP example). Apps keep the SDK start-up: chip_jump_app sets MPP.
$(BUILD)/pp/%.S: %.S | $(BUILD)/pp
> sed -e 's/li t0, 0x88[[:space:]]*$$/li t0, 0x1888/' $< > $@
> grep -q 'li t0, 0x1888' $@

$(BUILD)/bl-$(BOARD)/%.o: $(BUILD)/pp/%.c $(BOARD_FILE) | $(BUILD)/bl-$(BOARD)
> $(CC) $(CFLAGS_BL) -c $< -o $@
$(BUILD)/bl-$(BOARD)/%.o: $(BUILD)/pp/%.S | $(BUILD)/bl-$(BOARD)
> $(CC) $(ASFLAGS) -c $< -o $@
$(BUILD)/ble/%.o: $(BUILD)/pp/%.c | $(BUILD)/ble
> $(CC) $(CFLAGS_BLE) -c $< -o $@
$(BUILD)/ble/%.o: %.S | $(BUILD)/ble
> $(CC) $(ASFLAGS) -c $< -o $@

$(BUILD) $(BUILD)/pp $(BUILD)/bl-$(BOARD) $(BUILD)/ble:
> mkdir -p $@

full: $(FULL)
$(FULL): $(BUILD)/$(BL).bin $(APPBIN_$(APP)) mkfull.py boot_layout.h
> $(PYTHON) mkfull.py $(BUILD)/$(BL).bin $(APPBIN_$(APP)) $@

flash: $(FULL)
> $(WCHISP) flash $(FULL)

-include $(wildcard $(BUILD)/bl-*/*.d $(BUILD)/ble/*.d)

clean:
> rm -rf $(BUILD)

FORCE:
.PHONY: all test-app ble-app full flash clean FORCE
.SECONDARY:
