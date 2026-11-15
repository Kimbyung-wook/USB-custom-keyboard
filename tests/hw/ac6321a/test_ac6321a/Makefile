# UNVERIFIED DRAFT (2026-11-15): build memo for the AC6321A pin test (JUZH_3B4).
# The sources compile against the SDK (cpu/bd19 flags), the full app link and
# image pack are not done yet. SDK base: public fw-AC63_BT_SDK, platform
# cpu/bd19 (BD19 = AC632N family, AC6321A QFN32).
#
# Idea: the JieLi SDK builds a whole app with its own makefile and toolchain
# (JL clang, C:/JL/pi32/bin/clang.exe, target q32s), then packs it with
# isd_download / isd_config.ini into a firmware image.
# So this file does not compile by itself: it copies the three test sources
# into an SDK app and runs the SDK's own make there.
#   MK_JL_SDK   SDK root (the folder with Makefile, apps/, cpu/, tools/)
#   MK_JL_APP   app folder inside the SDK to put the test into (apps/hid)
#   MK_JL_TARGET  SDK make target (ac632n_hid)
#   MK_JL_TOOLCHAIN  JL toolchain bin folder (installer puts it in C:/JL/pi32/bin)
# The SDK make also needs $(MK_JL_SDK)/tools/utils (mkdir_win, fixbat) on PATH.
#
#   mingw32-make check   show the paths, stop when the SDK is missing
#   mingw32-make copy    copy the sources into $(MK_JL_APP)/pintest/
#   mingw32-make build   copy, then "make $(MK_JL_TARGET)" in the SDK
# The SDK's post-build script (cpu/bd19/tools/download.bat) runs
# isd_download -tonorflash, which writes to a connected chip: build turns it
# off with RUN_POST_SCRIPT. No flash target on purpose (flash only after the
# user's ok, through the ROM UBOOT path).
#
# TODO(SDK):
# - add pintest/test_ch32v203.c and pintest/ac6321a_hal.c to the app's source
#   list (apps/hid/board/bd19/Makefile), with
#   -DTEST_HAL_H='"ac6321a_hal.h"' -DTEST_PINS_H='"ac6321a_pins.h"'
# - drop the app's own app_main()/tasks (ac6321a_hal.c has app_main) or call
#   the test task from it; add "pintest" to task_info_table[]
# - turn off Bluetooth, key, LED and power-off modules of the app so they do
#   not drive the test pins (board_ac6321a_*_cfg.h: TCFG_*_ENABLE 0)
# - isd_config.ini: keep the long-press reset (PA9) and USB update settings
.RECIPEPREFIX = >

MK_JL_SDK ?=
MK_JL_APP ?= $(MK_JL_SDK)/apps/hid
MK_JL_TARGET ?= ac632n_hid
MK_JL_TOOLCHAIN ?= C:/JL/pi32/bin
LOGIC := ../../ch32v203/test_ch32v203
SRCS := $(LOGIC)/test_ch32v203.c ac6321a_hal.c ac6321a_hal.h ac6321a_pins.h
DEST := $(MK_JL_APP)/pintest

.PHONY: check copy build

check:
>@if [ -z "$(MK_JL_SDK)" ] || [ ! -d "$(MK_JL_SDK)" ]; then \
>  echo "MK_JL_SDK is not set or missing (JieLi SDK root)"; exit 1; fi
>@echo "SDK    $(MK_JL_SDK)"
>@echo "app    $(MK_JL_APP)"
>@echo "target $(MK_JL_TARGET)"
>@echo "tools  $(MK_JL_TOOLCHAIN)"

copy: check
>mkdir -p "$(DEST)"
>cp $(SRCS) "$(DEST)/"

build: copy
>PATH="$(MK_JL_TOOLCHAIN):$(MK_JL_SDK)/tools/utils:$$PATH" $(MAKE) -C "$(MK_JL_SDK)" $(MK_JL_TARGET) \
>  RUN_POST_SCRIPT="cmd /c echo POST-SCRIPT-SKIPPED" \
>  EXTRA_CFLAGS="-DTEST_HAL_H='\"ac6321a_hal.h\"' -DTEST_PINS_H='\"ac6321a_pins.h\"'"
