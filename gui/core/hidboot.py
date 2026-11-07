"""CH32V203 own bootloader over HID (docs/BOOTLOADER_CH32V203.md). No Qt.

CLI (from the repo root):
  uv run python -m gui.core.hidboot info
  uv run python -m gui.core.hidboot flash firmware/macro_keyboard/build/ch32v203/macro_keyboard-3X3V_RGB.bin
  uv run python -m gui.core.hidboot run
``--enter`` first sends BOOT (0x06) to a running keyboard app so it restarts
into the bootloader (no key needed).
"""
from __future__ import annotations

import struct
import sys
import time
import zlib
from dataclasses import dataclass
from typing import Callable

from . import protocol as p
from .transport import HidTransport, Transport, TransportError

VID = 0x1209
PID = 0xC55E  # the app is 0xC55D

CMD_HELLO = 0x01
CMD_ERASE = 0x10
CMD_WRITE = 0x11
CMD_CHECK = 0x12
CMD_RUN = 0x13

ST_OK, ST_BAD_CMD, ST_BAD_ARG, ST_BAD_CRC, ST_FLASH_ERR = 0, 1, 2, 3, 5
STATUS_TEXT = {ST_BAD_CMD: "알 수 없는 명령", ST_BAD_ARG: "인수 오류",
               ST_BAD_CRC: "CRC 불일치", ST_FLASH_ERR: "플래시 쓰기 실패"}

TIMEOUT_MS = 1000
ERASE_TIMEOUT_MS = 5000

# Flash layout (src/hal/ch32v203/boot_layout.h)
APP_ADDR = 0x08002000
INFO_ADDR = 0x0800EF00
APP_MAX = INFO_ADDR - APP_ADDR
PAGE_SIZE = 256
INFO_MAGIC = 0x31505041


class BootError(Exception):
    pass


@dataclass
class BootInfo:
    version: tuple[int, int]
    board: str
    uid: bytes
    app_addr: int
    app_max: int
    page_size: int
    app_valid: bool
    write_max: int

    @classmethod
    def parse(cls, r: bytes) -> "BootInfo":
        board = r[5:21].split(b"\x00", 1)[0].decode("ascii", "replace")
        app_addr, app_max = struct.unpack_from("<II", r, 33)
        page = struct.unpack_from("<H", r, 41)[0]
        return cls((r[3], r[4]), board, bytes(r[21:33]), app_addr, app_max,
                   page, bool(r[43]), r[44] or 48)


def find_paths() -> list[bytes]:
    from .transport import _hid
    return [d["path"] for d in _hid().enumerate(VID, PID)
            if d.get("usage_page") == p.USAGE_PAGE]


def chunks(data: bytes, start: int, size: int, page: int = PAGE_SIZE):
    """(address, bytes) pieces of at most ``size`` that never cross a page."""
    off = 0
    while off < len(data):
        addr = start + off
        n = min(size, page - addr % page, len(data) - off)
        yield addr, data[off:off + n]
        off += n


class Bootloader:
    def __init__(self, t: Transport):
        self.t = t
        self._seq = 0
        self.info: BootInfo | None = None

    def close(self) -> None:
        self.t.close()

    def request(self, cmd: int, args: bytes = b"", timeout_ms: int = TIMEOUT_MS) -> bytes:
        self._seq = (self._seq + 1) & 0xFF
        self.t.write(bytes([cmd, self._seq]) + args.ljust(p.REPORT_SIZE - 2, b"\x00"))
        end = time.monotonic() + timeout_ms / 1000
        while True:
            left = int((end - time.monotonic()) * 1000)
            if left <= 0:
                raise BootError(f"응답 없음 (명령 0x{cmd:02X})")
            r = self.t.read(left)
            if r and r[0] == cmd | 0x80 and r[1] == self._seq:
                if r[2] != ST_OK:
                    raise BootError(f"명령 0x{cmd:02X} 실패: {STATUS_TEXT.get(r[2], r[2])}")
                return r

    def hello(self) -> BootInfo:
        self.info = BootInfo.parse(self.request(CMD_HELLO))
        return self.info

    def flash(self, image: bytes, progress: Callable[[int, int], None] | None = None) -> None:
        """Erase, write and check ``image`` (an app .bin linked at 0x08002000)."""
        info = self.info or self.hello()
        if not 8 <= len(image) <= info.app_max:
            raise BootError(f"펌웨어 크기 {len(image)} B (최대 {info.app_max} B)")
        self.request(CMD_ERASE, timeout_ms=ERASE_TIMEOUT_MS)
        done = 0
        for addr, part in chunks(image, info.app_addr, info.write_max, info.page_size):
            self.request(CMD_WRITE, struct.pack("<IBx", addr, len(part)) + part)
            done += len(part)
            if progress:
                progress(done, len(image))
        crc = zlib.crc32(image)
        self.request(CMD_CHECK, struct.pack("<II", len(image), crc))

    def run(self) -> None:
        self.request(CMD_RUN)


def scan() -> list[BootInfo]:
    """HELLO from every bootloader that is plugged in (for the update window)."""
    found = []
    for path in find_paths():
        try:
            bl = Bootloader(HidTransport(path))
        except (TransportError, OSError):
            continue
        try:
            found.append(bl.hello())
        except (BootError, TransportError, OSError):
            pass
        finally:
            bl.close()
    return found


def flash_first(image: bytes, progress: Callable[[int, int], None] | None = None) -> None:
    """Flash and start the app on the one bootloader that is plugged in."""
    bl = open_first()
    try:
        bl.flash(image, progress)
        bl.run()
    finally:
        bl.close()


def enter_from_app(wait_s: float = 5.0) -> None:
    """Send BOOT to the running keyboard app, then wait for the bootloader."""
    t = HidTransport.open_first()
    try:
        t.write(bytes([p.Cmd.BOOT, 1]).ljust(p.REPORT_SIZE, b"\x00"))
        t.read(TIMEOUT_MS)
    finally:
        t.close()
    wait_for_bootloader(wait_s)


def wait_for_bootloader(wait_s: float) -> bytes:
    end = time.monotonic() + wait_s
    while time.monotonic() < end:
        paths = find_paths()
        if paths:
            return paths[0]
        time.sleep(0.2)
    raise BootError("부트로더를 찾을 수 없습니다 (키 7을 누른 채 USB를 꽂으세요)")


def open_first(wait_s: float = 0.0) -> Bootloader:
    paths = find_paths() if wait_s <= 0 else [wait_for_bootloader(wait_s)]
    if not paths:
        raise BootError("부트로더를 찾을 수 없습니다 (키 7을 누른 채 USB를 꽂으세요)")
    return Bootloader(HidTransport(paths[0]))


# ------------------------------------------------------------------ mock

class MockBootloader(Transport):
    """Simulates firmware/bootloader_ch32v203/main.c for tests."""

    def __init__(self, board: str = "3X3V_RGB", flash_error: bool = False):
        self.board = board
        self.flash_error = flash_error
        self.flash = bytearray(b"\xff" * APP_MAX)
        self.info: tuple[int, int] | None = None  # (length, crc)
        self.erased = False
        self.ran = False
        self.writes = 0
        self._out: list[bytes] = []

    def _reply(self, rq: bytes, status: int, body: bytes = b"") -> None:
        self._out.append((bytes([rq[0] | 0x80, rq[1], status]) + body).ljust(64, b"\x00"))

    def write(self, report: bytes) -> None:
        rq = bytes(report)
        cmd = rq[0]
        if cmd == CMD_HELLO:
            body = bytes([1, 0]) + self.board.encode().ljust(16, b"\x00") + bytes(range(12))
            body += struct.pack("<IIHBB", APP_ADDR, APP_MAX, PAGE_SIZE, self.info is not None, 56)
            self._reply(rq, ST_OK, body)
        elif cmd == CMD_ERASE:
            self.flash[:] = b"\xff" * APP_MAX
            self.info, self.erased = None, not self.flash_error
            self._reply(rq, ST_OK if self.erased else ST_FLASH_ERR)
        elif cmd == CMD_WRITE:
            addr, n = struct.unpack_from("<IB", rq, 2)
            ok = (self.erased and 0 < n <= 56 and addr >= APP_ADDR
                  and addr + n <= INFO_ADDR and addr % PAGE_SIZE + n <= PAGE_SIZE)
            if ok:
                self.flash[addr - APP_ADDR:addr - APP_ADDR + n] = rq[8:8 + n]
                self.writes += 1
            self._reply(rq, ST_OK if ok else ST_BAD_ARG)
        elif cmd == CMD_CHECK:
            length, crc = struct.unpack_from("<II", rq, 2)
            if not 8 <= length <= APP_MAX:
                return self._reply(rq, ST_BAD_ARG)
            got = zlib.crc32(bytes(self.flash[:length]))
            if got != crc:
                return self._reply(rq, ST_BAD_CRC, struct.pack("<I", got))
            self.info, self.erased = (length, crc), False
            self._reply(rq, ST_OK, struct.pack("<I", got))
        elif cmd == CMD_RUN:
            self.ran = True
            self._reply(rq, ST_OK)
        else:
            self._reply(rq, ST_BAD_CMD)

    def read(self, timeout_ms: int) -> bytes | None:
        return self._out.pop(0) if self._out else None

    def close(self) -> None:
        pass

    @property
    def description(self) -> str:
        return "mock bootloader"


# ------------------------------------------------------------------ CLI

def main(argv: list[str]) -> int:
    args = [a for a in argv if a != "--enter"]
    if not args or args[0] not in ("info", "flash", "run") or (args[0] == "flash") != (len(args) == 2):
        print(__doc__)
        return 2
    try:
        if "--enter" in argv:
            enter_from_app()
        bl = open_first(wait_s=3.0)
        try:
            info = bl.hello()
            print(f"부트로더 {info.version[0]}.{info.version[1]}, 보드 {info.board}, "
                  f"UID {info.uid.hex('-').upper()}, 앱 {'있음' if info.app_valid else '없음'}")
            if args[0] == "flash":
                image = open(args[1], "rb").read()
                bl.flash(image, lambda d, n: print(f"\r쓰는 중 {d * 100 // n}%", end=""))
                print(f"\n확인 완료 ({len(image)} B, CRC32 {zlib.crc32(image):08X})")
            if args[0] in ("flash", "run"):
                bl.run()
                print("앱 실행")
        finally:
            bl.close()
    except (BootError, TransportError, OSError) as e:
        print(f"오류: {e}")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
