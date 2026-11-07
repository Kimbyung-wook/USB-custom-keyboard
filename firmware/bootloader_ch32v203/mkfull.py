"""Join bootloader + app + app info into one image for the first install (wchisp).

python mkfull.py bootloader.bin app.bin full.bin
Layout: src/hal/ch32v203/boot_layout.h (offsets from the flash start).
"""

import struct
import sys
import zlib

LOADER_SIZE = 0x2000
INFO_OFF = 0xEF00
APP_MAX = INFO_OFF - LOADER_SIZE
MAGIC = 0x31505041  # "APP1"


def app_info(app: bytes) -> bytes:
    crc = zlib.crc32(app)
    check = (~MAGIC ^ len(app) ^ crc) & 0xFFFFFFFF
    return struct.pack("<4I", MAGIC, len(app), crc, check)


def main(loader_path: str, app_path: str, out_path: str) -> None:
    loader = open(loader_path, "rb").read()
    app = open(app_path, "rb").read()
    if len(loader) > LOADER_SIZE:
        sys.exit(f"bootloader {len(loader)} B > {LOADER_SIZE}")
    if not 8 <= len(app) <= APP_MAX:
        sys.exit(f"app {len(app)} B outside 8..{APP_MAX}")
    image = loader.ljust(LOADER_SIZE, b"\xff") + app
    image = image.ljust(INFO_OFF, b"\xff") + app_info(app)
    open(out_path, "wb").write(image)
    print(f"{out_path}: {len(image)} B (bootloader {len(loader)}, app {len(app)})")


if __name__ == "__main__":
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    main(*sys.argv[1:])
