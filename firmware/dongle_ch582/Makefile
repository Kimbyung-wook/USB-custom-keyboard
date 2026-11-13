# CH582F BLE HID dongle: mingw32-make -> build/dongle.bin
# Pins: mingw32-make DEFS="-DBOARD_LED_PIN=... -DBOARD_BTN_PIN=..." (src/board.h)
# Shared rules and tool paths: ch58x.mk (MK_CH58X_SDK, MK_RISCV_BIN, MK_WCHISP)
TARGET := dongle
BUILD := build
BLE := 1
SRCS := src/main.c src/ble_host.c src/store.c src/usb_hid.c \
        src/core/hid_desc.c src/core/hid_conv.c src/core/hogp.c src/core/link.c src/core/rq.c
PERIPH := CH58x_clk.c CH58x_gpio.c CH58x_sys.c CH58x_pwr.c CH58x_adc.c
include ch58x.mk
