"""Join bootloader + app info + app into one image for the first install (wchisp).

python mkfull.py bootloader.bin app.bin full.bin
Layout from boot_layout.h (next to this file): bootloader at 0, app info at
BOOT_INFO_ADDR, app at BOOT_APP_ADDR. The image ends with the app (no padding
up to the end of flash).
"""

import pathlib
import re
import struct
import sys
import zlib

MAGIC = 0x31505041  # "APP1"


def layout() -> dict[str, int]:
    text = (pathlib.Path(__file__).parent / "boot_layout.h").read_text()
    names = ("BOOT_LOADER_SIZE", "BOOT_INFO_ADDR", "BOOT_APP_ADDR", "BOOT_FLASH_SIZE")
    return {n: int(re.search(rf"#define {n} (0x[0-9A-Fa-f]+)u", text).group(1), 16) for n in names}


def app_info(app: bytes) -> bytes:
    crc = zlib.crc32(app)
    check = (~MAGIC ^ len(app) ^ crc) & 0xFFFFFFFF
    return struct.pack("<4I", MAGIC, len(app), crc, check)


def main(loader_path: str, app_path: str, out_path: str) -> None:
    lay = layout()
    loader = open(loader_path, "rb").read()
    app = open(app_path, "rb").read()
    app_max = lay["BOOT_FLASH_SIZE"] - lay["BOOT_APP_ADDR"]
    if len(loader) > lay["BOOT_LOADER_SIZE"]:
        sys.exit(f"bootloader {len(loader)} B > {lay['BOOT_LOADER_SIZE']}")
    if not 8 <= len(app) <= app_max:
        sys.exit(f"app {len(app)} B outside 8..{app_max}")
    image = loader.ljust(lay["BOOT_INFO_ADDR"], b"\xff") + app_info(app)
    image = image.ljust(lay["BOOT_APP_ADDR"], b"\xff") + app
    open(out_path, "wb").write(image)
    print(f"{out_path}: {len(image)} B (bootloader {len(loader)}, app {len(app)})")


if __name__ == "__main__":
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    main(*sys.argv[1:])
