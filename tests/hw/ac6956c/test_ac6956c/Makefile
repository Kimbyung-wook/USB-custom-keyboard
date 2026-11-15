# UNVERIFIED DRAFT (2026-11-15): build memo for the AC6956C pin test (JUZH_3B4).
# Never run: the JieLi SDK and compiler are not installed (download needs the
# user's approval). SDK base: public fw-AC63_BT_SDK, platform cpu/br23 (same BR23
# family as the AC635N).
#
# Idea: the JieLi SDK builds a whole app with its own makefile and toolchain
# (JL clang, e.g. C:/JL/pi32/bin/clang.exe or /opt/jieli/common/bin/clang),
# then packs it with isd_download / isd_config.ini into a firmware image.
# So this file does not compile by itself: it copies the three test sources
# into an SDK app and runs the SDK's own make there.
#   MK_JL_SDK   SDK root (the folder with Makefile, apps/, cpu/, tools/)
#   MK_JL_APP   app folder inside the SDK to put the test into
#               (TODO: app name; public base fw-AC63_BT_SDK, platform
#               cpu/br23 (AC695N family): apps/hid, "make ac635n_hid")
#   MK_JL_TARGET  SDK make target (TODO: e.g. ac695n_xxx)
#
#   mingw32-make check   show the paths, stop when the SDK is missing
#   mingw32-make copy    copy the sources into $(MK_JL_APP)/pintest/
#   mingw32-make build   copy, then "make $(MK_JL_TARGET)" in the SDK
# No flash target on purpose: flashing (JieLi USB updater / forced upgrade)
# replaces the factory firmware, which must be kept (backup first, user's ok).
#
# TODO(SDK), once the SDK is at hand:
# - add pintest/test_ch32v203.c and pintest/ac6956c_hal.c to the app's source
#   list, with -DTEST_HAL_H='"ac6956c_hal.h"' -DTEST_PINS_H='"ac6956c_pins.h"'
# - drop the app's own app_main()/tasks (ac6956c_hal.c has app_main) or call
#   the test task from it; add "pintest" to task_info_table[]
# - turn off Bluetooth, key, LED and power-off modules of the app so they do
#   not drive the test pins (board_*_cfg.h: TCFG_*_ENABLE 0)
# - isd_config.ini: keep the long-press reset (PB1) and USB update settings
.RECIPEPREFIX = >

MK_JL_SDK ?=
MK_JL_APP ?= $(MK_JL_SDK)/apps/hid
MK_JL_TARGET ?=
LOGIC := ../../ch32v203/test_ch32v203
SRCS := $(LOGIC)/test_ch32v203.c ac6956c_hal.c ac6956c_hal.h ac6956c_pins.h
DEST := $(MK_JL_APP)/pintest

.PHONY: check copy build

check:
>@if [ -z "$(MK_JL_SDK)" ] || [ ! -d "$(MK_JL_SDK)" ]; then \
>  echo "MK_JL_SDK is not set or missing: install the JieLi SDK first (user's approval)"; exit 1; fi
>@if [ -z "$(MK_JL_TARGET)" ]; then echo "MK_JL_TARGET is not set (SDK make target)"; exit 1; fi
>@echo "SDK    $(MK_JL_SDK)"
>@echo "app    $(MK_JL_APP)"
>@echo "target $(MK_JL_TARGET)"

copy: check
>mkdir -p "$(DEST)"
>cp $(SRCS) "$(DEST)/"

build: copy
>$(MAKE) -C "$(MK_JL_SDK)" $(MK_JL_TARGET) \
>  EXTRA_CFLAGS="-DTEST_HAL_H='\"ac6956c_hal.h\"' -DTEST_PINS_H='\"ac6956c_pins.h\"'"
