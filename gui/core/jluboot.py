"""JieLi UBOOT (USB download mode) over SCSI, for JUZH_3B4 (AC6956C). No Qt.

In UBOOT the chip is a USB disk (USB 4C4A:3442, SCSI vendor = chip family,
"BD19" on JUZH_3B4, product "UBOOT1.00") that takes JieLi commands as 16-byte
SCSI CDBs: 0xFB = host to device, 0xFC = command only, 0xFD = device to host.
The ROM only has memory commands, so a RAM loader from the JieLi SDK
(cpu/<chip>/tools/<chip>loader.bin) is written and run first; it adds the
flash commands.

Reaching UBOOT, over USB only (ENTRY_METHODS, enter_uboot): the factory
firmware takes 03 EF EF on its vendor HID (514C:8850), ours the BOOT command.
(The USB_KEY adapter in gui/core/jl_usbkey.py is on hold.)

Windows: SCSI pass-through (SPTI, IOCTL_SCSI_PASS_THROUGH_DIRECT) on the disk's
volume with the standard USB storage driver, no driver change. Opening it for
read and write usually needs administrator rights.

Parts of the command layout, device discovery, CRC and SPTI structure follow
jl-uboot-tool (https://github.com/kagaimiq/jl-uboot-tool, jltech/uboot.py,
jltech/crc.py, jltech/cipher.py, scsiio/win32port.py, jldevfind.py):

    MIT License
    Copyright (c) 2023 Andrey Grigoryev (kagaimiq)

    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"), to deal
    in the Software without restriction, including without limitation the rights
    to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
    copies of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in all
    copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
    OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
    SOFTWARE.

Changes from it: SCSI status is checked on Windows (CHECK CONDITION raises),
data-in with data-out raises a real exception, every write is read back, and
crcmod / pyyaml / tqdm are not needed.
"""
from __future__ import annotations

import hashlib
import os
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Callable, Iterable

Progress = Callable[[int, int], None]

# ---------------------------------------------------------------- CRC, cipher

def crc16(data: bytes, crc: int = 0) -> int:
    """JieLi CRC16: poly 0x1021, no reflection (CRC-16/XMODEM with init 0)."""
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) if crc & 0x8000 else crc << 1
            crc &= 0xFFFF
    return crc


# "孟黎我爱你，玉林" in GB2312 (jltech/cipher.py jl_crc_cipher)
_MENGLI = bytes.fromhex("c3cfc0e8ced2b0aec4e3a3acd3f1c1d6")


def mengli_cipher(data: bytes, key: int = 0xFFFFFFFF) -> bytes:
    """XOR scrambler used by the chip key (READ_KEY)."""
    out = bytearray(data)
    crc = crc16((key >> 16).to_bytes(2, "little"), key & 0xFFFF)
    for i in range(len(out)):
        crc = crc16(_MENGLI[i % len(_MENGLI)].to_bytes(1, "little"), crc)
        out[i] ^= crc & 0xFF
    return bytes(out)


# ---------------------------------------------------------------- SCSI

class UbootError(Exception):
    """str(e) is a user-facing Korean message."""


class ScsiError(UbootError):
    pass


class AdminRequired(UbootError):
    pass


class ScsiDevice:
    """One SCSI target. ``execute`` sends a CDB with optional data out, or
    reads ``data_in`` bytes back."""

    path = ""

    def execute(self, cdb: bytes, data_out: bytes | None = None, data_in: int = 0) -> bytes:
        raise NotImplementedError

    def close(self) -> None:
        pass


class Win32Scsi(ScsiDevice):
    """SCSI_PASS_THROUGH_DIRECT on \\\\.\\X: or \\\\.\\HardDiskVolumeN."""

    IOCTL_SCSI_PASS_THROUGH_DIRECT = 0x4D014
    DATA_OUT, DATA_IN, DATA_NONE = 0, 1, 2
    TIMEOUT_S = 5
    SENSE_LEN = 32

    def __init__(self, path: str):
        import ctypes
        from ctypes import wintypes as w
        self._ct, self._w = ctypes, w
        self.path = path
        k32 = ctypes.WinDLL("kernel32", use_last_error=True)
        k32.CreateFileW.restype = w.HANDLE
        k32.CreateFileW.argtypes = [w.LPCWSTR, w.DWORD, w.DWORD, w.LPVOID, w.DWORD, w.DWORD, w.HANDLE]
        k32.DeviceIoControl.argtypes = [w.HANDLE, w.DWORD, w.LPVOID, w.DWORD, w.LPVOID, w.DWORD,
                                        ctypes.POINTER(w.DWORD), w.LPVOID]
        k32.DeviceIoControl.restype = w.BOOL
        k32.CloseHandle.argtypes = [w.HANDLE]
        self._k32 = k32
        GENERIC_READ, GENERIC_WRITE = 0x80000000, 0x40000000
        h = k32.CreateFileW(path, GENERIC_READ | GENERIC_WRITE, 3, None, 3, 0, None)  # share r/w, OPEN_EXISTING
        if h in (None, w.HANDLE(-1).value):
            err = ctypes.get_last_error()
            if err == 5:  # ERROR_ACCESS_DENIED
                raise AdminRequired(f"{path}: 접근 거부. 관리자 권한으로 실행해야 합니다")
            raise ScsiError(f"{path}: 열 수 없습니다 ({ctypes.FormatError(err)})")
        self._h = h

        class SPTD(ctypes.Structure):
            _fields_ = [("Length", w.USHORT), ("ScsiStatus", ctypes.c_ubyte), ("PathId", ctypes.c_ubyte),
                        ("TargetId", ctypes.c_ubyte), ("Lun", ctypes.c_ubyte), ("CdbLength", ctypes.c_ubyte),
                        ("SenseInfoLength", ctypes.c_ubyte), ("DataIn", ctypes.c_ubyte),
                        ("DataTransferLength", w.ULONG), ("TimeOutValue", w.ULONG),
                        ("DataBuffer", ctypes.c_void_p), ("SenseInfoOffset", w.ULONG),
                        ("Cdb", ctypes.c_ubyte * 16)]

        class WithSense(ctypes.Structure):
            _fields_ = [("sptd", SPTD), ("sense", ctypes.c_ubyte * self.SENSE_LEN)]
        self._WithSense = WithSense

    def execute(self, cdb: bytes, data_out: bytes | None = None, data_in: int = 0) -> bytes:
        ct = self._ct
        if data_out is not None and data_in:
            raise ValueError("data out and data in at once are not supported")
        s = self._WithSense()
        d = s.sptd
        d.Length = ct.sizeof(d)
        d.CdbLength = len(cdb)
        d.Cdb[:len(cdb)] = list(cdb)
        d.SenseInfoLength = self.SENSE_LEN
        d.SenseInfoOffset = self._WithSense.sense.offset
        d.TimeOutValue = self.TIMEOUT_S
        if data_out is not None:
            buf = ct.create_string_buffer(bytes(data_out), len(data_out))
            d.DataIn, d.DataTransferLength = self.DATA_OUT, len(data_out)
        elif data_in:
            buf = ct.create_string_buffer(data_in)
            d.DataIn, d.DataTransferLength = self.DATA_IN, data_in
        else:
            buf = None
            d.DataIn, d.DataTransferLength = self.DATA_NONE, 0
        d.DataBuffer = ct.cast(buf, ct.c_void_p) if buf is not None else None
        got = self._w.DWORD()
        ok = self._k32.DeviceIoControl(self._h, self.IOCTL_SCSI_PASS_THROUGH_DIRECT,
                                       ct.byref(s), ct.sizeof(s), ct.byref(s), ct.sizeof(s),
                                       ct.byref(got), None)
        if not ok:
            raise ScsiError(f"SCSI 전송 실패: {ct.FormatError(ct.get_last_error())}")
        if d.ScsiStatus:
            raise ScsiError(f"SCSI 상태 0x{d.ScsiStatus:02X} (명령 {cdb[:2].hex()})")
        return buf.raw[:data_in] if data_in else b""

    def close(self) -> None:
        if getattr(self, "_h", None):
            self._k32.CloseHandle(self._h)
            self._h = None


def is_admin() -> bool:
    if sys.platform != "win32":
        return os.geteuid() == 0 if hasattr(os, "geteuid") else False
    import ctypes
    try:
        return bool(ctypes.windll.shell32.IsUserAnAdmin())
    except OSError:
        return False


@dataclass
class Inquiry:
    vendor: str     # chip family, e.g. "BR23"
    product: str    # "UBOOT1.00" (ROM) or "UBOOT2.00" (loader)
    revision: str

    @property
    def chip(self) -> str:
        return self.vendor.lower()


def inquiry(dev: ScsiDevice) -> Inquiry:
    r = dev.execute(bytes([0x12, 0, 0, 0, 36, 0]), data_in=36)
    text = lambda b: b.decode("ascii", "replace").strip()  # noqa: E731
    return Inquiry(text(r[8:16]), text(r[16:32]), text(r[32:36]))


def candidate_paths() -> list[str]:
    """Windows paths that may be a disk: drive letters, then volumes."""
    paths = [f"\\\\.\\{c}:" for c in "DEFGHIJKLMNOPQRSTUVWXYZ" if os.path.exists(f"{c}:\\")]
    paths += [f"\\\\.\\HarddiskVolume{i}" for i in range(64)]
    return paths


@dataclass
class Found:
    path: str
    inquiry: Inquiry


def find_uboot(paths: Iterable[str] | None = None,
               open_fn: Callable[[str], ScsiDevice] = Win32Scsi) -> list[Found]:
    """Every UBOOT disk. Raises AdminRequired when none was found but some
    disk could not be opened for lack of rights."""
    found, denied = [], False
    for path in candidate_paths() if paths is None else paths:
        try:
            dev = open_fn(path)
        except AdminRequired:
            denied = True
            continue
        except (UbootError, OSError):
            continue
        try:
            q = inquiry(dev)
        except (UbootError, OSError):
            continue
        finally:
            dev.close()
        # The same disk shows up as a drive letter and as a volume: keep the first
        if q.product.startswith("UBOOT") and not any(f.inquiry == q for f in found):
            found.append(Found(path, q))
    if not found and denied:
        raise AdminRequired("UBOOT 장치를 열려면 관리자 권한이 필요합니다. 프로그램을 관리자로 다시 실행하세요")
    return found


def wait_uboot(seconds: float = 20.0, poll_s: float = 0.5, **kw) -> Found:
    end = time.monotonic() + seconds
    while True:
        found = find_uboot(**kw)
        if found:
            return found[0]
        if time.monotonic() >= end:
            raise UbootError(f"UBOOT 장치가 {seconds:.0f}초 안에 나타나지 않았습니다. "
                             "진입 방법, 케이블·포트(USB 2.0 허브 권장), 관리자 권한을 확인하세요")
        time.sleep(poll_s)


# ---------------------------------------------------------------- commands

# ROM (UBOOT1.00) and loader
WRITE_MEMORY = 0xFB06
READ_MEMORY = 0xFD07
JUMP_TO_MEMORY = 0xFB08
# Loader (protocol v2)
ERASE_FLASH_BLOCK = 0xFB00    # 64 KiB
ERASE_FLASH_SECTOR = 0xFB01   # 4 KiB
ERASE_FLASH_CHIP = 0xFB02
READ_STATUS = 0xFC03
WRITE_FLASH = 0xFB04
READ_FLASH = 0xFD05
READ_KEY = 0xFC09
GET_ONLINE_DEVICE = 0xFC0A
READ_ID = 0xFC0B
RUN_APP = 0xFC0C
FLASH_CRC16 = 0xFC0E
GET_USB_BUFF_SIZE = 0xFC14
GET_LOADER_VER = 0xFC15

SECTOR = 0x1000
BLOCK = 0x10000
MEM_CHUNK = 256          # BR23 loaders keep all I/O in a 256-byte buffer
LOADER_BLOCK = 512

# Chips we know (jl-uboot-tool data/usb-loaders.yaml): family -> loader file
# in the JieLi SDK, load address, run argument (target 1 = SPI NOR)
LOADERS = {
    "bd19": ("cpu/bd19/tools/bd19loader.bin", 0x2000, 0x0001),    # JUZH_3B4 (jl-uboot-tool: "unknown")
    "br23": ("cpu/br23/tools/br23loader.bin", 0x12000, 0x0001),
}

# UBOOT on USB (seen on JUZH_3B4, 2026-11-15): "BD19 UBOOT1.00"
UBOOT_VID, UBOOT_PID = 0x4C4A, 0x3442

# The loader's settings block (isd_config.ini [SYS_CFG_PARAM]), written to RAM
# before the jump. Without it the bd19 loader reads garbage RAM as settings
# and stops at the first command (PM, 2026-11-15). 64 B: 16 B mark, zeros to
# offset 34, entries [value length][key NUL][value], 0 to end; MengLi encoded.
CONFIG_ADDR = 0x7A00
CONFIG_MARK = b"UIMMTAG-" + bytes.fromhex("12789BE8CA311B61")
CONFIG_SIZE = 64
CONFIG_ENTRIES_AT = 34


def config_block(entries: dict[str, str]) -> bytes:
    """The plain settings block, e.g. {"SPI": "2_3_0_0"}."""
    body = b"".join(bytes([len(v)]) + k.encode() + b"\x00" + v.encode() for k, v in entries.items())
    block = CONFIG_MARK.ljust(CONFIG_ENTRIES_AT, b"\x00") + body + b"\x00"
    if len(block) > CONFIG_SIZE:
        raise ValueError("설정 블록이 64바이트를 넘습니다")
    return block.ljust(CONFIG_SIZE, b"\x00")


LOADER_CONFIG = {
    "bd19": {"SPI": "2_3_0_0"},     # checked on JUZH_3B4: flash read works
}

# Commands a loader has no handler for: it never answers, the transfer times
# out and the 3 s idle timer resets the chip. Refused before sending
NO_HANDLER = {
    "bd19": frozenset({GET_LOADER_VER, 0xFC0F, 0xFC16}),
}
# Commands the loader has but that must never be sent on this board
# (FB02 erases the whole chip, BTIF at 0x3E000 included)
FORBIDDEN = {
    "bd19": frozenset({ERASE_FLASH_CHIP}),
}
# Commands not checked on the chip yet: left out (their value reads as 0)
UNCHECKED = {
    "bd19": frozenset({READ_KEY}),
}
# The loader resets the chip when no command comes for about 3 s
LOADER_IDLE_S = 2.5
LOADER_IDLE_TEXT = ("로더가 3초 넘게 명령을 받지 않아 칩이 다시 시작했을 수 있습니다. "
                    "[UBOOT로 전환]부터 다시 하세요")
DEFAULT_SDK = r"C:\Users\user\tools\sdk\fw-AC63_BT_SDK"


def loader_path(chip: str, sdk: str | None = None) -> Path:
    if chip not in LOADERS:
        raise UbootError(f"지원하지 않는 칩입니다 ({chip.upper()})")
    return Path(sdk or os.environ.get("MK_JL_SDK") or DEFAULT_SDK) / LOADERS[chip][0]


def cdb(cmd: int, args: bytes = b"") -> bytes:
    c = cmd.to_bytes(2, "big") + args
    return c + b"\xff" * (16 - len(c))


@dataclass
class ChipInfo:
    chip: str           # "BR23"
    product: str        # what INQUIRY said, e.g. "UBOOT1.00" (ROM, loader run since)
    flash_id: int       # JEDEC, e.g. 0x854015
    flash_size: int     # bytes from the JEDEC capacity byte (0: unknown)
    chip_key: int
    online: tuple[int, int]   # (type, id) of the flash the loader talks to

    @property
    def identity(self) -> str:
        """What to record and compare between installs (flash ID + chip key)."""
        return f"{self.chip}:{self.flash_id:06X}:{self.chip_key:04X}"


def jedec_size(flash_id: int) -> int:
    cap = flash_id & 0xFF
    return 1 << cap if 0x10 <= cap <= 0x20 else 0


class Uboot:
    def __init__(self, dev: ScsiDevice, q: Inquiry | None = None):
        self.dev = dev
        self.q = q or inquiry(dev)
        self.loader_running = self.q.product != "UBOOT1.00"
        self.chunk = 512
        self.clock = time.monotonic       # tests: a fake clock
        self._last = self.clock()

    def close(self) -> None:
        self.dev.close()

    # ----- raw
    def _send(self, cmd: int, c: bytes, data_out: bytes | None = None, data_in: int = 0) -> bytes:
        """Every command goes here: refuses what this chip's loader cannot
        answer and anything after it may have reset for being idle."""
        if self.loader_running:
            if cmd in NO_HANDLER.get(self.q.chip, ()):
                raise UbootError(f"이 칩의 로더에 없는 명령입니다 ({cmd:04X}): 보내면 칩이 다시 시작합니다")
            if cmd in FORBIDDEN.get(self.q.chip, ()):
                raise UbootError(f"이 보드에서 쓰지 않는 명령입니다 ({cmd:04X}): 블루투스 정보(BTIF)까지 지웁니다")
            if self.clock() - self._last > LOADER_IDLE_S:
                self.loader_running = False
                raise UbootError(LOADER_IDLE_TEXT)
        try:
            return self.dev.execute(c, data_out=data_out, data_in=data_in)
        finally:
            self._last = self.clock()

    def _exec(self, cmd: int, args: bytes = b"") -> bytes:
        r = self._send(cmd, cdb(cmd, args), data_in=16)
        if int.from_bytes(r[:2], "big") != cmd:
            raise UbootError(f"UBOOT 응답이 명령과 다릅니다 ({cmd:04X} → {r[:2].hex()})")
        return r[2:]

    def _out(self, cmd: int, args: bytes, data: bytes) -> None:
        self._send(cmd, cdb(cmd, args), data_out=data)

    def _in(self, cmd: int, args: bytes, n: int) -> bytes:
        return self._send(cmd, cdb(cmd, args), data_in=n)

    def keep_alive(self) -> None:
        """A harmless command (online device) so the loader's 3 s timer does not run out."""
        self.online_device()

    def checked(self, cmd: int) -> bool:
        return cmd not in UNCHECKED.get(self.q.chip, ())

    @staticmethod
    def _rw_args(addr: int, n: int) -> bytes:
        return addr.to_bytes(4, "big") + n.to_bytes(2, "big")

    def _need_loader(self) -> None:
        if not self.loader_running:
            raise UbootError("플래시 명령 전에 로더를 실행해야 합니다")

    # ----- memory (ROM and loader)
    def mem_write(self, addr: int, data: bytes) -> None:
        self._out(WRITE_MEMORY, self._rw_args(addr, len(data)) + b"\x00"
                  + crc16(data).to_bytes(2, "little"), data)

    def mem_read(self, addr: int, n: int) -> bytes:
        return self._in(READ_MEMORY, self._rw_args(addr, n), n)

    def mem_jump(self, addr: int, arg: int = 0) -> None:
        self._exec(JUMP_TO_MEMORY, addr.to_bytes(4, "big") + arg.to_bytes(2, "big"))

    def write_config(self, entries: dict[str, str]) -> None:
        """The settings block at CONFIG_ADDR, MengLi encoded; read back to check
        (the ROM reads it back still encoded)."""
        data = mengli_cipher(config_block(entries))
        self.mem_write(CONFIG_ADDR, data)
        if self.mem_read(CONFIG_ADDR, len(data)) != data:
            raise UbootError("로더 설정 블록을 다시 읽은 값이 다릅니다")

    def run_loader(self, blob: bytes, addr: int, arg: int, block: int = LOADER_BLOCK,
                   config: dict[str, str] | None = None) -> None:
        """Write the RAM loader and run it (UBOOT1.00 only). The SDK file is
        already encoded for the ROM's memory write: it goes as it is. The first
        command follows within 3 s (the loader's idle timer)."""
        if self.loader_running:
            return
        for off in range(0, len(blob), block):
            self.mem_write(addr + off, blob[off:off + block])
        if config:
            self.write_config(config)
        self.mem_jump(addr, arg)
        time.sleep(0.2)
        self.loader_running = True
        try:
            self.chunk = self.usb_buffer_size() or 512
        except UbootError:
            self.chunk = 512
        self.chunk = min(self.chunk, 0x8000)

    def start(self, sdk: str | None = None, blob: bytes | None = None) -> None:
        """Run the chip's loader when the ROM is still in charge."""
        if self.loader_running:
            self.chunk = min(self.usb_buffer_size() or 512, 0x8000)
            return
        _, addr, arg = LOADERS.get(self.q.chip, (None, 0, 0))
        if blob is None:
            path = loader_path(self.q.chip, sdk)
            try:
                blob = path.read_bytes()
            except OSError as e:
                raise UbootError(f"로더 파일을 읽을 수 없습니다: {path} ({e}). MK_JL_SDK를 확인하세요") from e
        self.run_loader(blob, addr, arg, config=LOADER_CONFIG.get(self.q.chip))

    # ----- loader
    def usb_buffer_size(self) -> int:
        self._need_loader()
        return int.from_bytes(self._exec(GET_USB_BUFF_SIZE)[:4], "big")

    def read_id(self) -> int:
        self._need_loader()
        return int.from_bytes(self._exec(READ_ID)[:3], "big")

    def chip_key(self) -> int:
        """0 where READ_KEY is not checked on the chip yet (bd19)."""
        self._need_loader()
        if not self.checked(READ_KEY):
            return 0
        r = self._exec(READ_KEY, (0xAC6900).to_bytes(4, "big"))
        return int.from_bytes(mengli_cipher(r[4:6][::-1]), "little")

    def online_device(self) -> tuple[int, int]:
        self._need_loader()
        r = self._exec(GET_ONLINE_DEVICE)
        return r[0], int.from_bytes(r[2:6], "little")

    def info(self) -> ChipInfo:
        fid = self.read_id()
        return ChipInfo(self.q.vendor, self.q.product, fid, jedec_size(fid), self.chip_key(), self.online_device())

    def flash_read(self, addr: int, n: int) -> bytes:
        self._need_loader()
        return self._in(READ_FLASH, self._rw_args(addr, n), n)

    def flash_write(self, addr: int, data: bytes) -> None:
        self._need_loader()
        self._out(WRITE_FLASH, self._rw_args(addr, len(data)) + b"\x00"
                  + crc16(data).to_bytes(2, "little"), data)

    def erase_sector(self, addr: int) -> None:
        self._need_loader()
        self._exec(ERASE_FLASH_SECTOR, addr.to_bytes(4, "big"))

    def erase_block(self, addr: int) -> None:
        self._need_loader()
        self._exec(ERASE_FLASH_BLOCK, addr.to_bytes(4, "big"))

    def flash_crc(self, addr: int, n: int) -> int:
        """FC0E: the loader's CRC16 of flash [addr, addr + n).
        CDB and answer layout are a guess (address 4 B, length 2 B -> CRC 2 B
        big-endian after the echoed command) until the firmware session checks them"""
        self._need_loader()
        return int.from_bytes(self._exec(FLASH_CRC16, self._rw_args(addr, n))[:2], "big")

    def run_app(self, arg: int = 1) -> None:
        """Leave UBOOT and start the firmware (the chip resets)."""
        self._exec(RUN_APP, arg.to_bytes(4, "big"))


# ---------------------------------------------------------------- procedures

def read_flash(u: Uboot, addr: int, n: int, progress: Progress | None = None) -> bytes:
    out = bytearray()
    while len(out) < n:
        k = min(u.chunk, n - len(out))
        part = u.flash_read(addr + len(out), k)
        if len(part) != k:
            raise UbootError(f"플래시 읽기가 짧습니다 (0x{addr + len(out):06X}, {len(part)}/{k} B)")
        out += part
        if progress:
            progress(len(out), n)
    return bytes(out)


@dataclass
class Backup:
    data: bytes
    sha256: str
    path: Path | None = None


def backup(u: Uboot, size: int, progress: Progress | None = None) -> Backup:
    """The whole flash read twice; both reads must match."""
    if size <= 0:
        raise UbootError("플래시 용량을 알 수 없습니다 (JEDEC ID). 백업할 크기를 정하세요")
    def half(base: int) -> Progress | None:
        return (lambda d, t: progress(base + d, 2 * t)) if progress else None
    first = read_flash(u, 0, size, half(0))
    second = read_flash(u, 0, size, half(size))
    if first != second:
        bad = next(i for i in range(size) if first[i] != second[i])
        raise UbootError(f"백업을 두 번 읽은 값이 다릅니다 (0x{bad:06X}부터). 케이블·허브를 확인하고 다시 하세요")
    return Backup(first, hashlib.sha256(first).hexdigest())


def save_backup(b: Backup, folder: Path, info: ChipInfo) -> Path:
    """<folder>/<chip>_<flash id>_<date>.bin and .sha256 next to it."""
    folder.mkdir(parents=True, exist_ok=True)
    stamp = time.strftime("%Y%m%d-%H%M%S")
    path = folder / f"{info.chip.lower()}_{info.flash_id:06X}_{stamp}.bin"
    path.write_bytes(b.data)
    path.with_suffix(".sha256").write_text(f"{b.sha256}  {path.name}\n", encoding="ascii")
    b.path = path
    return path


def erase_ranges(addr: int, n: int) -> list[tuple[int, int]]:
    """(address, size) erases covering [addr, addr + n): 64 KiB blocks where
    aligned, 4 KiB sectors elsewhere."""
    a, end = addr & ~(SECTOR - 1), (addr + n + SECTOR - 1) & ~(SECTOR - 1)
    out = []
    while a < end:
        size = BLOCK if a % BLOCK == 0 and end - a >= BLOCK else SECTOR
        out.append((a, size))
        a += size
    return out


def write_image(u: Uboot, addr: int, data: bytes, progress: Progress | None = None,
                verify: bool = True) -> None:
    """Erase, write in the loader's chunk size, then read everything back."""
    if addr % SECTOR:
        raise UbootError(f"쓰기 주소는 4 KiB 단위여야 합니다 (0x{addr:X})")
    steps = len(data) * (2 if verify else 1)
    for a, size in erase_ranges(addr, len(data)):
        (u.erase_block if size == BLOCK else u.erase_sector)(a)
    done = 0
    for off in range(0, len(data), u.chunk):
        part = data[off:off + u.chunk]
        u.flash_write(addr + off, part)
        done += len(part)
        if progress:
            progress(done, steps)
    if verify:
        back = read_flash(u, addr, len(data),
                          (lambda d, t: progress(len(data) + d, steps)) if progress else None)
        if back != data:
            bad = next(i for i in range(len(data)) if back[i] != data[i])
            raise UbootError(f"확인 읽기가 다릅니다 (0x{addr + bad:06X}). 다시 쓰세요 (UBOOT는 ROM이라 다시 들어갈 수 있습니다)")


@dataclass(frozen=True)
class WritePlan:
    """How a board's flash is rewritten.
    [0, erase_end) is erased in 64 KiB blocks (FB00) whatever the image size,
    the image must fit in it and nothing after it is touched (BTIF, settings).
    ``piece``: the most bytes per FB04 (4 KiB multiples, from address 0).
    ``crc_each``: FC0E after every piece. A restore writes the backup's [0, erase_end)"""
    erase_end: int
    piece: int = 0x8000
    crc_each: bool = True


WRITE_PLANS = {
    # JUZH_3B4: jl_isd.bin (0x15000 B) from isd_download, written as it is.
    # 0x3E000 BTIF stays
    "bd19": WritePlan(erase_end=0x30000),
}


def write_planned(u: Uboot, plan: WritePlan, data: bytes, progress: Progress | None = None) -> None:
    """Erase [0, erase_end) in blocks, write from 0 in pieces with a CRC per
    piece, then read everything back."""
    if len(data) > plan.erase_end:
        raise UbootError(f"이미지({len(data)}바이트)가 쓰기 영역(0x{plan.erase_end:X} = {plan.erase_end}바이트)보다 큽니다")
    if plan.erase_end % BLOCK:
        raise UbootError(f"지우기 영역은 64 KiB 단위여야 합니다 (0x{plan.erase_end:X})")
    piece = min(plan.piece, u.chunk)
    piece -= piece % SECTOR
    if piece <= 0:
        raise UbootError(f"로더 버퍼({u.chunk} B)가 4 KiB보다 작아 쓸 수 없습니다")
    steps = 2 * len(data)
    for a in range(0, plan.erase_end, BLOCK):
        u.erase_block(a)
    done = 0
    for off in range(0, len(data), piece):
        part = data[off:off + piece]
        u.flash_write(off, part)
        if plan.crc_each:
            got = u.flash_crc(off, len(part))
            if got != crc16(part):
                raise UbootError(f"조각 CRC가 다릅니다 (0x{off:06X}, 칩 {got:04X} / 파일 {crc16(part):04X}). "
                                 "다시 쓰거나 백업으로 되돌리세요")
        done += len(part)
        if progress:
            progress(done, steps)
    back = read_flash(u, 0, len(data), (lambda d, t: progress(len(data) + d, steps)) if progress else None)
    if back != data:
        bad = next(i for i in range(len(data)) if back[i] != data[i])
        raise UbootError(f"확인 읽기가 다릅니다 (0x{bad:06X}). 다시 쓰세요 (UBOOT는 ROM이라 다시 들어갈 수 있습니다)")


# ---------------------------------------------------------------- entering UBOOT

# How a keyboard is sent into UBOOT over its own USB cable (no adapter, no dongle)
ENTRY_FACTORY = "factory"   # factory or test firmware: vendor HID report 3 = EF EF
ENTRY_APP = "app"           # our firmware: protocol BOOT (0x06)
ENTRY_METHODS = {
    ENTRY_FACTORY: "공장·시험 펌웨어 (벤더 HID 명령 03 EF EF)",
    ENTRY_APP: "우리 펌웨어 (BOOT 명령)",
}
FACTORY_VID, FACTORY_PID = 0x514C, 0x8850
# Firmware that takes 03 EF EF on its vendor HID: the factory one, and our
# AC6321A pin test firmware (SDK default IDs "JL"/"UA", tests/hw/ac6321a)
ENTRY_HIDS = ((FACTORY_VID, FACTORY_PID), (0x4A4C, 0x4155))
FACTORY_USAGE_PAGE = 0xFF00
FACTORY_REPORT_ID = 0x03
FACTORY_ENTER = bytes([FACTORY_REPORT_ID, 0xEF, 0xEF]).ljust(65, b"\x00")   # report ID + 64 B


def factory_paths() -> list[bytes]:
    from .transport import _hid
    return [d["path"] for vid, pid in ENTRY_HIDS for d in _hid().enumerate(vid, pid)
            if d.get("usage_page") == FACTORY_USAGE_PAGE]


def enter_factory(open_path: Callable[[bytes], object] | None = None,
                  paths: list[bytes] | None = None) -> None:
    """Send 03 EF EF to the factory firmware; it drops off USB and comes back
    as the UBOOT disk. ``open_path`` returns an object with ``write(bytes)``
    and ``close()`` (hidapi device; tests pass a fake)."""
    from .transport import TransportError, _hid
    try:
        paths = factory_paths() if paths is None else paths
    except TransportError as e:
        raise UbootError(str(e)) from e
    if not paths:
        ids = ", ".join(f"{v:04X}:{p_:04X}" for v, p_ in ENTRY_HIDS)
        raise UbootError(f"공장·시험 펌웨어 키보드({ids})를 찾을 수 없습니다. "
                         "USB를 꽂고 전원을 켜세요")
    if open_path is None:
        def open_path(path: bytes):
            dev = _hid().device()
            dev.open_path(path)
            return dev
    try:
        dev = open_path(paths[0])
    except (OSError, IOError) as e:
        raise UbootError(f"공장 펌웨어 키보드를 열 수 없습니다: {e}") from e
    try:
        n = dev.write(FACTORY_ENTER)
    except (OSError, IOError, ValueError) as e:
        raise UbootError(f"진입 명령을 보내지 못했습니다: {e}") from e
    finally:
        try:
            dev.close()
        except Exception:
            pass
    if n is not None and n < 0:
        raise UbootError("진입 명령을 보내지 못했습니다 (연결이 끊겼을 수 있습니다)")


def enter_app(client) -> None:
    """Our firmware: BOOT (gui/core/device.py DeviceClient). It restarts into UBOOT."""
    client.reboot_to_bootloader()


def enter_uboot(method: str, *, client=None, **factory_kw) -> None:
    if method == ENTRY_FACTORY:
        enter_factory(**factory_kw)
    elif method == ENTRY_APP:
        if client is None:
            raise UbootError("우리 펌웨어에 연결되어 있지 않습니다")
        enter_app(client)
    else:
        raise UbootError(f"모르는 진입 방법입니다: {method}")


def open_uboot(found: Found, open_fn: Callable[[str], ScsiDevice] = Win32Scsi,
               sdk: str | None = None, loader: bytes | None = None) -> tuple[Uboot, ChipInfo]:
    """Open the disk, run the loader if needed, read the chip and flash IDs."""
    u = Uboot(open_fn(found.path), found.inquiry)
    try:
        if u.q.chip not in LOADERS:
            raise UbootError(f"지원하지 않는 칩입니다 ({u.q.vendor}). 지원: "
                             + ", ".join(c.upper() for c in LOADERS))
        u.start(sdk, loader)
        return u, u.info()
    except BaseException:
        u.close()
        raise


def identity_problem(info: ChipInfo, known: str | None) -> str:
    """"" when ``known`` (recorded at the first install) is unset or matches."""
    if not known or known == info.identity:
        return ""
    return f"다른 기판입니다: 기록 {known}, 지금 {info.identity}. 보드를 확인하세요"


# ---------------------------------------------------------------- admin rights

ADMIN_TEXT = ("UBOOT 장치(USB 저장장치)에 명령을 보내려면 Windows 관리자 권한이 필요합니다. "
              "[관리자로 다시 실행]을 누르고 사용자 계정 컨트롤 창에서 [예]를 누르세요")


def relaunch_as_admin(argv: list[str] | None = None, shell_execute=None) -> bool:
    """Start this program again elevated (Windows asks the user: UAC). True when
    it started; the caller then closes this one. ``shell_execute``: tests."""
    if sys.platform != "win32" and shell_execute is None:
        return False
    import subprocess
    argv = list(sys.argv if argv is None else argv)
    # Built program (PyInstaller): the .exe itself. From source: python gui/main.py …
    args = argv[1:] if getattr(sys, "frozen", False) else [os.path.abspath(argv[0])] + argv[1:]
    params = subprocess.list2cmdline(args)
    exe = sys.executable
    if shell_execute is None:
        import ctypes
        shell_execute = ctypes.windll.shell32.ShellExecuteW
    return int(shell_execute(None, "runas", exe, params, os.getcwd(), 1)) > 32


# ---------------------------------------------------------------- the update, step by step

# Writing the firmware is not ready: the flash image and how isd_download lays
# it out (uboot.boot, app.bin, cfg, keys) are being worked out by the firmware
# session, and the RAM loader has not run on the board yet. Until then
# JlFlasher.write refuses, so nothing is erased.
WRITE_READY = False
WRITE_PENDING_TEXT = ("펌웨어 쓰기는 아직 준비 중입니다 (플래시 이미지 형식·로더 확인 중). "
                      "진입·장치 찾기·백업까지만 할 수 있습니다")

STAGE_IDLE, STAGE_ENTER, STAGE_WAIT, STAGE_OPEN, STAGE_BACKUP, STAGE_WRITE, STAGE_RESTART, STAGE_DONE = range(8)
STAGE_TEXT = {
    STAGE_IDLE: "대기",
    STAGE_ENTER: "UBOOT로 전환 중",
    STAGE_WAIT: "UBOOT 장치 찾는 중",
    STAGE_OPEN: "로더 실행·칩 확인 중",
    STAGE_BACKUP: "백업 중 (두 번 읽어 비교)",
    STAGE_WRITE: "지우기·쓰기·확인 중",
    STAGE_RESTART: "새 펌웨어 시작",
    STAGE_DONE: "완료",
}


class JlFlasher:
    """The JieLi update in the order the window runs it: enter -> wait for the
    UBOOT disk -> loader and chip info -> backup -> write -> restart. Every
    device access goes through the injected functions (tests: FakeUboot)."""

    def __init__(self, *, open_fn: Callable[[str], ScsiDevice] = Win32Scsi,
                 paths: list[str] | None = None, sdk: str | None = None,
                 loader: bytes | None = None, log: Callable[[str], None] | None = None,
                 wait_s: float = 20.0, poll_s: float = 0.5):
        self.open_fn, self.paths, self.sdk, self.loader = open_fn, paths, sdk, loader
        self.log = log or (lambda line: None)
        self.wait_s, self.poll_s = wait_s, poll_s
        self.stage = STAGE_IDLE
        self.found: Found | None = None
        self.uboot: Uboot | None = None
        self.info: ChipInfo | None = None
        self.backup_path: Path | None = None

    def set_stage(self, stage: int) -> None:
        self.stage = stage
        self.log(STAGE_TEXT[stage])

    def find(self) -> list[Found]:
        return find_uboot(self.paths, open_fn=self.open_fn)

    def enter(self, method: str, **kw) -> None:
        self.set_stage(STAGE_ENTER)
        enter_uboot(method, **kw)

    def wait(self) -> Found:
        self.set_stage(STAGE_WAIT)
        self.found = wait_uboot(self.wait_s, self.poll_s, paths=self.paths, open_fn=self.open_fn)
        q = self.found.inquiry
        self.log(f"UBOOT 장치: {q.vendor} {q.product} ({self.found.path})")
        return self.found

    def open(self) -> ChipInfo:
        if self.found is None:
            self.wait()
        self.set_stage(STAGE_OPEN)
        self.uboot, self.info = open_uboot(self.found, self.open_fn, self.sdk, self.loader)
        i = self.info
        self.log(f"칩 {i.chip} · 플래시 ID {i.flash_id:06X} ({i.flash_size // 1024} KB) · 칩 키 {i.chip_key:04X}")
        return i

    def backup(self, folder: Path, progress: Progress | None = None) -> Path:
        if self.uboot is None:
            self.open()
        self.set_stage(STAGE_BACKUP)
        b = backup(self.uboot, self.info.flash_size, progress)
        self.backup_path = save_backup(b, Path(folder), self.info)
        self.log(f"백업 저장: {self.backup_path} (SHA-256 {b.sha256[:16]}…)")
        return self.backup_path

    def write(self, image: bytes, progress: Progress | None = None, addr: int = 0) -> None:
        if not WRITE_READY:
            raise UbootError(WRITE_PENDING_TEXT)
        if self.uboot is None:
            self.open()
        if self.info.flash_size and addr + len(image) > self.info.flash_size:
            raise UbootError(f"이미지({len(image)}바이트)가 플래시({self.info.flash_size}바이트)보다 큽니다")
        self.set_stage(STAGE_WRITE)
        plan = WRITE_PLANS.get(self.info.chip.lower())
        if plan is None:
            write_image(self.uboot, addr, image, progress)
        elif addr:
            raise UbootError(f"이 보드는 0번지부터만 씁니다 (0x{addr:X})")
        else:
            write_planned(self.uboot, plan, image, progress)

    def restore(self, backup_file: Path, progress: Progress | None = None) -> None:
        """Put a backup back: its [0, erase_end) with the same erase/write/check."""
        data = Path(backup_file).read_bytes()
        if self.uboot is None:
            self.open()
        plan = WRITE_PLANS.get(self.info.chip.lower())
        if plan is None:
            raise UbootError("이 칩은 되돌리기 범위가 정해지지 않았습니다")
        if self.info.flash_size and len(data) != self.info.flash_size:
            raise UbootError(f"백업 크기({len(data)}바이트)가 플래시({self.info.flash_size}바이트)와 다릅니다")
        self.write(data[:plan.erase_end], progress)

    def restart(self) -> None:
        self.set_stage(STAGE_RESTART)
        if self.uboot is not None:
            self.uboot.run_app()
        self.close()
        self.set_stage(STAGE_DONE)

    def close(self) -> None:
        if self.uboot is not None:
            self.uboot.close()
            self.uboot = None


# ---------------------------------------------------------------- fake device

class FakeUboot(ScsiDevice):
    """A chip in UBOOT for tests (BR23 by default, BD19 on JUZH_3B4): ROM memory
    commands until a loader is written to the chip's LOADERS address and run, then the flash commands (``flash``, JEDEC
    ``flash_id``). ``flaky_reads``: corrupt that many flash reads."""

    def __init__(self, flash_size: int = 1 << 20, flash_id: int | None = None,
                 vendor: str = "BR23", product: str = "UBOOT1.00", path: str = "\\\\.\\E:",
                 usb_buffer: int = 4096, chip_key: int = 0x1234):
        self.path = path
        self.vendor, self.product = vendor, product
        self.flash = bytearray(b"\xA5" * flash_size)   # the factory firmware
        # JEDEC: 0x85 maker, 0x40 type, capacity = log2(size)
        self.flash_id = flash_id if flash_id is not None else 0x854000 | (flash_size.bit_length() - 1)
        self.ram: dict[int, int] = {}
        self.loader = product != "UBOOT1.00"
        self.usb_buffer = usb_buffer
        self.key = chip_key
        self.flaky_reads = 0
        self.ran_app: int | None = None
        self.log: list[int] = []
        self.closed = False
        self.config: bytes | None = None   # the decoded settings block seen at the jump
        self.stuck = False
        self.writes: list[tuple[int, int]] = []   # (address, size) of every FB04
        self.erases: list[int] = []

    def _resp(self, cmd: int, body: bytes = b"") -> bytes:
        return (cmd.to_bytes(2, "big") + body).ljust(16, b"\x00")

    def execute(self, c: bytes, data_out: bytes | None = None, data_in: int = 0) -> bytes:
        if c[0] == 0x12:
            return (bytes(8) + self.vendor.encode().ljust(8) + self.product.encode().ljust(16)
                    + b"1.00")[:data_in]
        cmd = int.from_bytes(c[:2], "big")
        self.log.append(cmd)
        addr, n = int.from_bytes(c[2:6], "big"), int.from_bytes(c[6:8], "big")
        if cmd == WRITE_MEMORY:
            if crc16(data_out) != int.from_bytes(c[9:11], "little") or n != len(data_out):
                raise ScsiError("CRC")
            for i, b in enumerate(data_out):
                self.ram[addr + i] = b
            return b""
        if cmd == READ_MEMORY:
            return bytes(self.ram.get(addr + i, 0) for i in range(n))
        if cmd == JUMP_TO_MEMORY:
            chip = self.vendor.lower()
            at = LOADERS.get(chip, (None, 0x12000))[1]
            if addr == at and self.ram.get(at) is not None:
                self.loader, self.product = True, "UBOOT2.00"
                if chip in LOADER_CONFIG:
                    # bd19: the settings block, else garbage settings and no answers
                    raw = bytes(self.ram.get(CONFIG_ADDR + i, 0) for i in range(CONFIG_SIZE))
                    self.config = mengli_cipher(raw)
                    self.stuck = not self.config.startswith(CONFIG_MARK)
            return self._resp(cmd)
        if not self.loader:
            raise ScsiError("ROM has no such command")
        if self.stuck or cmd in NO_HANDLER.get(self.vendor.lower(), ()):
            self.stuck = True     # no answer: the transfer times out, the chip resets
            raise ScsiError(f"{cmd:04X}: 응답 없음 (시간 초과)")
        if cmd == GET_USB_BUFF_SIZE:
            return self._resp(cmd, self.usb_buffer.to_bytes(4, "big"))
        if cmd == READ_ID:
            return self._resp(cmd, self.flash_id.to_bytes(3, "big"))
        if cmd == READ_KEY:
            raw = mengli_cipher(self.key.to_bytes(2, "little"))[::-1]
            return self._resp(cmd, bytes(4) + raw)
        if cmd == GET_ONLINE_DEVICE:
            return self._resp(cmd, bytes([1, 0]) + self.flash_id.to_bytes(4, "little"))
        if cmd in (ERASE_FLASH_SECTOR, ERASE_FLASH_BLOCK):
            size = SECTOR if cmd == ERASE_FLASH_SECTOR else BLOCK
            if addr % size or addr + size > len(self.flash):
                raise ScsiError("unaligned erase or past the flash end")
            self.flash[addr:addr + size] = b"\xff" * size
            self.erases.append(addr)
            return self._resp(cmd)
        if cmd == WRITE_FLASH:
            if crc16(data_out) != int.from_bytes(c[9:11], "little") or n != len(data_out) or n > self.usb_buffer:
                raise ScsiError("bad write")
            if addr + n > len(self.flash):
                raise ScsiError("write past the flash end")
            self.writes.append((addr, n))
            for i, b in enumerate(data_out):
                self.flash[addr + i] &= b      # NOR: only clears bits
            return b""
        if cmd == READ_FLASH:
            data = bytes(self.flash[addr:addr + n])
            if self.flaky_reads:
                self.flaky_reads -= 1
                data = bytes([data[0] ^ 1]) + data[1:]
            return data
        if cmd == FLASH_CRC16:
            return self._resp(cmd, crc16(bytes(self.flash[addr:addr + n])).to_bytes(2, "big"))
        if cmd == RUN_APP:
            self.ran_app = addr
            return self._resp(cmd)
        raise ScsiError(f"unknown {cmd:04X}")

    def close(self) -> None:
        self.closed = True
