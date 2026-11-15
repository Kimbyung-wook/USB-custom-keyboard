#!/usr/bin/env bash
# AC6321A build of the macro keyboard (board JUZH_3B4), UNVERIFIED on a chip.
# The firmware is a JieLi SDK app (fw-AC63_BT_SDK, cpu/bd19, ac632n_hid):
# the SDK owns main(), its USB stack and the BLE stack; src/hal/ac6321a
# registers the app and runs the keyboard sequence (src/core) as one task.
# Never writes to a chip: the SDK's post-build script (download.bat ->
# isd_download -tonorflash, which flashes a connected chip) is turned off.
#
#   bash jl_build_ac6321a.sh          (or: mingw32-make -f Makefile.ac6321a)
#
#   MK_JL_SDK        SDK clone, left untouched (default C:/Users/user/tools/sdk/fw-AC63_BT_SDK)
#   MK_JL_WORK       work tree made from it (default C:/Users/user/tools/jieli/mk_sdk)
#   MK_JL_TOOLCHAIN  JL toolchain bin folder (default C:/JL/pi32/bin)
#   BLE=0            USB only (no BLE stack calls; the BLE library still links)
#
# Work tree = the SDK parts a bd19 build reads (apps/common, apps/hid,
# cpu/bd19, include_lib, tools), copied once. On every run the config files
# and the board Makefile are copied fresh from the SDK and patched (same
# patch each time), and src/ is copied to apps/hid/mk/src:
#   board_config.h      CONFIG_BOARD_AC6321A_STAND_KEYBOARD
#   ..._keyboard_cfg.h  USB device (PC) on; debug UART0 off (its TX is USB0 DP);
#                       SDK matrix keys and external MCU off (our scan);
#                       BLE as BLE= says
#   app_config.h        CONFIG_APP_STANDARD_KEYBOARD (app name the SDK starts)
#   Makefile            our sources in; the SDK's keyboard app, its one-
#                       interface USB HID class (usb_hid_devices.c) and its
#                       device descriptor (descriptor.c, VID 4A4C) out: our
#                       usb_ac6321a.c gives both HID interfaces and 1209:C55D
# Output: build/ac6321a/macro_keyboard-JUZH_3B4.elf and .bin (application
# only: the flash image with uboot, config and partitions is isd_download's).
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
SDK=${MK_JL_SDK:-C:/Users/user/tools/sdk/fw-AC63_BT_SDK}
WORK=${MK_JL_WORK:-C:/Users/user/tools/jieli/mk_sdk}
TC=${MK_JL_TOOLCHAIN:-C:/JL/pi32/bin}
BLE=${BLE:-1}
BOARD=apps/hid/board/bd19
CFG=$BOARD/board_ac6321a_stand_keyboard_cfg.h
APPCFG=apps/hid/include/app_config.h
OUT=$HERE/build/ac6321a
NAME=macro_keyboard-JUZH_3B4

[ -d "$SDK/cpu/bd19" ] || { echo "MK_JL_SDK: no cpu/bd19 in $SDK"; exit 1; }
[ -x "$TC/clang.exe" ] || [ -x "$TC/clang" ] || { echo "MK_JL_TOOLCHAIN: no clang in $TC"; exit 1; }
case "$WORK" in "$SDK"|"$SDK"/*) echo "MK_JL_WORK must not be inside the SDK clone"; exit 1;; esac

if [ ! -d "$WORK/cpu/bd19" ]; then
  echo "== copy SDK parts to $WORK"
  mkdir -p "$WORK/apps" "$WORK/cpu"
  cp -r "$SDK/apps/common" "$SDK/apps/hid" "$WORK/apps/"
  cp -r "$SDK/cpu/bd19" "$WORK/cpu/"
  cp -r "$SDK/include_lib" "$SDK/tools" "$WORK/"
  cp "$SDK/Makefile" "$SDK/LICENSE" "$WORK/"
fi

echo "== patch config (BLE=$BLE)"
for f in $BOARD/board_config.h $CFG $APPCFG $BOARD/Makefile; do
  cp "$SDK/$f" "$WORK/$f"
done
# replace "#define NAME <old>" by "#define NAME <new>" and fail when absent
setdef() {
  local file=$WORK/$1 name=$2 val=$3
  grep -Eq "^[[:space:]]*#define[[:space:]]+$name[[:space:]]" "$file" || { echo "missing $name in $1"; exit 1; }
  sed -i -E "s|^([[:space:]]*#define[[:space:]]+$name)[[:space:]]+[^/]*|\1 $val |" "$file"
}
sed -i -E 's|^#define CONFIG_BOARD_AC632N_DEMO|// #define CONFIG_BOARD_AC632N_DEMO|; s|^// #define CONFIG_BOARD_AC6321A_STAND_KEYBOARD|#define CONFIG_BOARD_AC6321A_STAND_KEYBOARD|' "$WORK/$BOARD/board_config.h"
grep -q '^#define CONFIG_BOARD_AC6321A_STAND_KEYBOARD' "$WORK/$BOARD/board_config.h" || { echo "board_config.h patch failed"; exit 1; }
setdef $CFG TCFG_UART0_ENABLE DISABLE_THIS_MOUDLE
setdef $CFG TCFG_PC_ENABLE ENABLE_THIS_MOUDLE
setdef $CFG TCFG_MATRIX_KEY_ENABLE DISABLE_THIS_MOUDLE
setdef $CFG TCFG_EX_MCU_ENABLE DISABLE_THIS_MOUDLE
setdef $CFG TCFG_USER_BLE_ENABLE "$BLE"
setdef $APPCFG CONFIG_APP_KEYBOARD 0
setdef $APPCFG CONFIG_APP_STANDARD_KEYBOARD 1

echo "== sources"
rm -rf "$WORK/apps/hid/mk"
mkdir -p "$WORK/apps/hid/mk"
cp -r "$HERE/src" "$WORK/apps/hid/mk/"
rm -rf "$WORK/apps/hid/mk/src/hal/ch552" "$WORK/apps/hid/mk/src/hal/ch32v203" "$WORK/apps/hid/mk/src/hal/ch573"
SRCS=""
for f in "$WORK"/apps/hid/mk/src/core/*.c "$WORK"/apps/hid/mk/src/hal/ac6321a/*.c; do
  SRCS="$SRCS\t../../../../apps/hid/mk/src/${f#$WORK/apps/hid/mk/src/} \\\\\n"
done
SRCS=${SRCS%'\n'} # no blank line after the last one (it would end the list)
MK=$WORK/$BOARD/Makefile
for line in examples/standard_keyboard/app_standard_keyboard.c examples/standard_keyboard/usb_hid_devices.c \
            device/usb/device/descriptor.c; do
  grep -q "$line" "$MK" || { echo "Makefile: $line not found"; exit 1; }
done
sed -i -e '/examples\/standard_keyboard\/usb_hid_devices\.c/d' -e '/apps\/common\/device\/usb\/device\/descriptor\.c/d' "$MK"
sed -i "s|^[[:space:]]*\.\./\.\./\.\./\.\./apps/hid/examples/standard_keyboard/app_standard_keyboard\.c \\\\|$SRCS|" "$MK"
grep -q 'apps/hid/mk/src/hal/ac6321a/' "$MK" || { echo "Makefile: sources not added"; exit 1; }
# our defines next to the SDK's (build date for HELLO, as tools/build_fw.ps1)
DATE_DEFS="-DFW_BUILD_YEAR=$(date +%Y) -DFW_BUILD_MONTH=$((10#$(date +%m))) -DFW_BUILD_DAY=$((10#$(date +%d))) -DFW_BUILD_HOUR=$((10#$(date +%H))) -DFW_BUILD_MINUTE=$((10#$(date +%M)))"
grep -q '^[[:space:]]*-DUSB_PC_NO_APP_MODE \\' "$MK" || { echo "Makefile: DEFINES anchor not found"; exit 1; }
sed -i "s|^\([[:space:]]*\)-DUSB_PC_NO_APP_MODE \\\\|\1-DUSB_PC_NO_APP_MODE \\\\\n\1-DMK_CHIP_AC6321A -DBOARD_JUZH_3B4 -DMK_BLE=$BLE -DBLOB_CAPACITY=2048 $DATE_DEFS \\\\|" "$MK"

echo "== build"
export PATH="$(cygpath -u "$TC"):$(cygpath -u "$WORK/tools/utils"):$PATH"
cd "$WORK/$BOARD"
mingw32-make -j8 RUN_POST_SCRIPT="cmd /c echo POST-SCRIPT-SKIPPED"

echo "== pack app.bin (the objcopy part of cpu/bd19/tools/download.bat)"
cd "$WORK/cpu/bd19/tools"
OC=$TC/llvm-objcopy.exe
for s in text data data_code overlay_aec overlay_aac overlay_aptx common; do
  "$OC" -O binary -j ".$s" sdk.elf "${s#overlay_}.bin"
done
banks=()
for i in $(seq 0 20); do
  "$OC" -O binary -j ".overlay_bank$i" sdk.elf "bank$i.bin"
  banks+=("bank$i.bin" 0x0)
done
./lz4_packet.exe -dict text.bin -input common.bin 0 "${banks[@]}" -o bank.bin
cat text.bin data.bin data_code.bin aec.bin aac.bin bank.bin aptx.bin > app.bin
rm -f bank*.bin common.bin text.bin data.bin aac.bin aec.bin aptx.bin data_code.bin
mkdir -p "$OUT"
cp sdk.elf "$OUT/$NAME.elf"
cp app.bin "$OUT/$NAME.bin"
ls -l "$OUT"
echo "OK: $OUT/$NAME.bin (application only, not flashed)"
