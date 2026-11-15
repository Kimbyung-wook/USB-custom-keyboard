# UNVERIFIED DRAFT (2026-11-15): build entry for the AC6321A pin test (JUZH_3B4).
# Builds and links as a JieLi SDK app (cpu/bd19, ac632n_hid) and packs
# build/app.bin; never run on a chip. The work is in jl_build.sh (bash):
# it copies the needed SDK parts to a work tree (the SDK clone stays as it
# is), patches the board / app config there, adds the test sources, runs the
# SDK make with its post-build script off (that script runs isd_download
# -tonorflash, which writes to a connected chip) and packs app.bin.
#   MK_JL_SDK        SDK clone (default C:/Users/user/tools/sdk/fw-AC63_BT_SDK)
#   MK_JL_WORK       work tree (default C:/Users/user/tools/jieli/pintest_sdk)
#   MK_JL_TOOLCHAIN  JL toolchain bin (default C:/JL/pi32/bin)
#
#   mingw32-make build   bash jl_build.sh
# No flash target on purpose: app.bin is not a flash image (isd_download adds
# uboot, config and partitions), and flashing waits for the user's ok.
.RECIPEPREFIX = >

.PHONY: build

build:
>bash jl_build.sh
