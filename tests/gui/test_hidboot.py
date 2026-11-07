import struct
import zlib
from pathlib import Path

import pytest

from gui.core import hidboot as h

ROOT = Path(__file__).resolve().parents[2]


def test_chunks_never_cross_a_page():
    data = bytes(range(256)) * 3
    parts = list(h.chunks(data, h.APP_ADDR + 200, 56))
    assert b"".join(d for _, d in parts) == data
    for addr, d in parts:
        assert len(d) <= 56
        assert addr % 256 + len(d) <= 256


def test_hello_parse():
    bl = h.Bootloader(h.MockBootloader())
    info = bl.hello()
    assert info.board == "3X3V_RGB"
    assert info.version == (1, 0)
    assert (info.app_addr, info.app_max, info.page_size) == (h.APP_ADDR, h.APP_MAX, 256)
    assert not info.app_valid
    assert info.write_max == 56


def test_flash_writes_image_and_info():
    dev = h.MockBootloader()
    bl = h.Bootloader(dev)
    image = bytes((i * 7) & 0xFF for i in range(11928))
    seen = []
    bl.flash(image, lambda d, n: seen.append(d))
    assert bytes(dev.flash[:len(image)]) == image
    assert dev.info == (len(image), zlib.crc32(image))
    assert seen[-1] == len(image)
    bl.run()
    assert dev.ran


def test_flash_too_big():
    bl = h.Bootloader(h.MockBootloader())
    with pytest.raises(h.BootError):
        bl.flash(b"\0" * (h.APP_MAX + 1))


def test_flash_error_reported():
    bl = h.Bootloader(h.MockBootloader(flash_error=True))
    with pytest.raises(h.BootError, match="플래시"):
        bl.flash(b"\1" * 100)


def test_write_without_erase_rejected():
    bl = h.Bootloader(h.MockBootloader())
    with pytest.raises(h.BootError, match="인수"):
        bl.request(h.CMD_WRITE, struct.pack("<IBx", h.APP_ADDR, 4) + b"abcd")


def test_layout_matches_firmware_header():
    text = (ROOT / "firmware/macro_keyboard/src/hal/ch32v203/boot_layout.h").read_text()
    for name, value in [("BOOT_APP_ADDR", h.APP_ADDR), ("BOOT_INFO_ADDR", h.INFO_ADDR),
                        ("BOOT_INFO_MAGIC", h.INFO_MAGIC)]:
        assert f"#define {name} 0x{value:08X}u" in text
