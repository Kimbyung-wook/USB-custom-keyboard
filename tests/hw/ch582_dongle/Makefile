# CH582F dongle pin test: mingw32-make -> build/pin_test.bin
# Shared rules and tool paths: firmware/dongle_ch582/ch58x.mk (MK_CH58X_SDK, MK_RISCV_BIN, MK_WCHISP)
DONGLE := ../../../firmware/dongle_ch582
TARGET := pin_test
BUILD := build
SRCS := pin_test.c $(DONGLE)/src/usb_hid.c $(DONGLE)/src/core/rq.c
PERIPH := CH58x_clk.c CH58x_gpio.c CH58x_sys.c CH58x_pwr.c
include $(DONGLE)/ch58x.mk
