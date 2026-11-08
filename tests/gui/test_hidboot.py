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
    assert info.board == "SAYO_3X3"
    assert info.version == (1, 0)
    assert (info.app_addr, info.app_max, info.page_size) == (h.APP_ADDR, h.APP_MAX, 256)
    assert not info.app_valid
    assert info.write_max == 56


NINE_ID = b"MKFW1:SAYO_3X3:CH32V203:1.7.0\0"


def app(size: int, fid: bytes | None = NINE_ID) -> bytes:
    """An app image of ``size`` bytes carrying ``fid`` (None: no firmware ID)."""
    return ((fid or b"") + bytes((i * 7) & 0xFF for i in range(size)))[:size]


def test_flash_writes_image_and_info():
    dev = h.MockBootloader()
    bl = h.Bootloader(dev)
    image = app(11928)
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
        bl.flash(app(100))


@pytest.mark.parametrize("fid, force, problem", [
    (NINE_ID, False, None),                                   # same board
    (b"MKFW1:SAYO_2X4:CH32V203:1.7.0\0", False, "SAYO_2X4용"),  # another board
    (None, False, "식별 정보"),                                 # test firmware, no ID
    (b"MKFW1:SAYO_2X4:CH32V203:1.7.0\0", True, None),          # forced
    (None, True, None),
])
def test_flash_checks_the_board(fid, force, problem):
    dev = h.MockBootloader(board="SAYO_3X3")
    bl = h.Bootloader(dev)
    image = app(2000, fid)
    if problem:
        with pytest.raises(h.BootError, match=problem) as e:
            bl.flash(image, force=force)
        assert "--force" in str(e.value) and "-Force" in str(e.value)
        assert not dev.erased and dev.writes == 0 and dev.info is None   # refused before ERASE
    else:
        bl.flash(image, force=force)
        assert dev.info == (len(image), zlib.crc32(image))


@pytest.mark.parametrize("force", [False, True])
def test_other_bootloader_major_refused_even_forced(force):
    dev = h.MockBootloader(version=(2, 0))
    with pytest.raises(h.BootError, match="부트로더 2.0"):
        h.Bootloader(dev).flash(app(500), force=force)
    assert not dev.erased and dev.writes == 0


@pytest.mark.parametrize("reported, used", [(0, h.WRITE_MAX), (32, 32), (56, 56), (200, h.WRITE_MAX)])
def test_write_max(reported, used):
    info = h.Bootloader(h.MockBootloader(write_max=reported)).hello()
    assert info.write_max == used and h.WRITE_MAX == 56


def test_cli_force_flag(tmp_path, monkeypatch):
    dev = h.MockBootloader()
    monkeypatch.setattr(h, "open_first", lambda wait_s=0.0: h.Bootloader(dev))
    path = tmp_path / "test.bin"
    path.write_bytes(app(500, None))
    assert h.main(["flash", str(path)]) == 1 and dev.info is None
    assert h.main(["flash", str(path), "--force"]) == 0 and dev.info is not None and dev.ran


def test_write_without_erase_rejected():
    bl = h.Bootloader(h.MockBootloader())
    with pytest.raises(h.BootError, match="인수"):
        bl.request(h.CMD_WRITE, struct.pack("<IBx", h.APP_ADDR, 4) + b"abcd")


def test_layout_matches_firmware_header():
    text = (ROOT / "firmware/macro_keyboard/src/hal/ch32v203/boot_layout.h").read_text()
    for name, value in [("BOOT_APP_ADDR", h.APP_ADDR), ("BOOT_INFO_ADDR", h.INFO_ADDR),
                        ("BOOT_INFO_MAGIC", h.INFO_MAGIC)]:
        assert f"#define {name} 0x{value:08X}u" in text
