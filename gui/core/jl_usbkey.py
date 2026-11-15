"""USB_KEY adapter for JieLi boards (JUZH_3B4, AC6956C). No Qt.

ON HOLD (2026-11-15): neither the adapter nor a JieLi dongle can be bought,
so UBOOT is reached over USB only (jluboot.ENTRY_METHODS). Nothing in the GUI
uses this module; it stays for a possible later adapter.

A CH32V203 board running firmware/jl_usbkey_ch32v203 (vendor HID 1209:C560,
"JieLi USB_KEY adapter") sends the JieLi USB_KEY (0x16EF) on the keyboard's
D+/D- while it powers up, so the chip's ROM enters UBOOT (a USB disk; then
gui/core/jluboot.py). Reports are 64 B: [0] command, [1] sequence, [2..]
arguments; the answer is [0] command | 0x80, [1] sequence, [2] status.
"""
from __future__ import annotations

import struct
import time
from dataclasses import dataclass

from . import protocol as p
from .transport import HidTransport, Transport, TransportError

VID = 0x1209
PID = 0xC560
NAME = "JL_USBKEY"

CMD_HELLO = 0x01
CMD_POWER = 0x20
CMD_ROUTE = 0x21
CMD_ENTER = 0x22
CMD_LINES = 0x23

ST_OK, ST_BAD_CMD, ST_BAD_ARG = 0, 1, 2
STATUS_TEXT = {ST_BAD_CMD: "알 수 없는 명령", ST_BAD_ARG: "인수 오류"}

TIMEOUT_MS = 1000
ENTER_TIMEOUT_MS = 3000     # the adapter's default when 0 is sent
CLOCK_AUTO, CLOCK_DP, CLOCK_DM = 0, 1, 2   # ENTER [7]: which line clocks the key


class UsbKeyError(Exception):
    pass


@dataclass
class AdapterInfo:
    version: tuple[int, int]
    name: str
    uid: bytes
    powered: bool       # keyboard VBUS on
    routed_to_key: bool  # keyboard D+/D- on the adapter (not the PC)


@dataclass
class EnterResult:
    acked: bool          # the chip pulled both lines low: UBOOT should follow
    order: int           # the clock line that worked (CLOCK_DP / CLOCK_DM)
    keys: int            # keys sent
    ms: int              # time to the answer (or the time limit)


def find_paths() -> list[bytes]:
    from .transport import _hid
    return [d["path"] for d in _hid().enumerate(VID, PID)
            if d.get("usage_page") == p.USAGE_PAGE]


class Adapter:
    def __init__(self, t: Transport):
        self.t = t
        self._seq = 0

    @classmethod
    def open_first(cls) -> "Adapter":
        try:
            paths = find_paths()
        except TransportError as e:
            raise UsbKeyError(str(e)) from e
        if not paths:
            raise UsbKeyError("진입 어댑터(1209:C560)를 찾을 수 없습니다. 어댑터를 PC에 꽂으세요")
        return cls(HidTransport(paths[0]))

    def close(self) -> None:
        self.t.close()

    def request(self, cmd: int, args: bytes = b"", timeout_ms: int = TIMEOUT_MS) -> bytes:
        self._seq = (self._seq + 1) & 0xFF
        self.t.write((bytes([cmd, self._seq]) + args).ljust(p.REPORT_SIZE, b"\x00"))
        end = time.monotonic() + timeout_ms / 1000
        while True:
            left = int((end - time.monotonic()) * 1000)
            if left <= 0:
                raise UsbKeyError(f"어댑터 응답 없음 (명령 0x{cmd:02X})")
            r = self.t.read(left)
            if r and r[0] == cmd | 0x80 and r[1] == self._seq:
                if r[2] != ST_OK:
                    raise UsbKeyError(f"어댑터 명령 0x{cmd:02X} 실패: {STATUS_TEXT.get(r[2], r[2])}")
                return r

    def hello(self) -> AdapterInfo:
        r = self.request(CMD_HELLO)
        name = bytes(r[5:21]).split(b"\0")[0].decode("ascii", "replace")
        if name != NAME:
            raise UsbKeyError(f"진입 어댑터가 아닙니다 ({name!r})")
        return AdapterInfo((r[3], r[4]), name, bytes(r[21:33]), bool(r[33]), bool(r[34]))

    def power(self, on: bool) -> None:
        self.request(CMD_POWER, bytes([int(on)]))

    def route(self, to_key: bool) -> None:
        """True: keyboard lines on the adapter, False: back to the PC."""
        self.request(CMD_ROUTE, bytes([int(to_key)]))

    def enter(self, off_ms: int = 500, timeout_ms: int = ENTER_TIMEOUT_MS,
              half_us: int = 0, order: int = CLOCK_AUTO) -> EnterResult:
        """Power the keyboard off for ``off_ms`` (0: leave the power alone, the
        user switches it on), then send the key until the chip answers or
        ``timeout_ms`` passes. The lines go back to the PC afterwards."""
        if not (0 <= off_ms <= 0xFFFF and 0 <= timeout_ms <= 0xFFFF and 0 <= half_us <= 100
                and order in (CLOCK_AUTO, CLOCK_DP, CLOCK_DM)):
            raise ValueError("enter: argument out of range")
        r = self.request(CMD_ENTER, struct.pack("<HHBB", off_ms, timeout_ms, half_us, order),
                         timeout_ms=off_ms + (timeout_ms or ENTER_TIMEOUT_MS) + TIMEOUT_MS)
        keys, ms = struct.unpack_from("<HH", r, 5)
        return EnterResult(bool(r[3]), r[4], keys, ms)

    def lines(self) -> tuple[int, int]:
        """D+ and D- levels now."""
        r = self.request(CMD_LINES)
        return r[3], r[4]


def enter_failed_text(r: EnterResult) -> str:
    return ("키보드가 응답하지 않았습니다 (키 {0}번, {1} ms). 확인할 것: 키보드 배터리·전원 스위치"
            "(USB를 꺼도 배터리로 켜져 있으면 안 됨), D+/D- 풀업 4.7 kΩ, 배선, "
            "키보드 쪽 PC 연결은 따로 둔 포트나 USB 2.0 허브").format(r.keys, r.ms)


# ------------------------------------------------------------------ mock

class MockAdapter(Transport):
    """Simulates firmware/jl_usbkey_ch32v203/main.c for tests. ``ack_after``:
    the key count after which the chip answers (None: never); ``on_ack`` runs
    when it does (e.g. a fake UBOOT disk appears)."""

    def __init__(self, ack_after: int | None = 40, order: int = CLOCK_DP, on_ack=None):
        self.ack_after = ack_after
        self.ack_order = order
        self.on_ack = on_ack
        self.powered = True
        self.routed = False
        self.enters: list[tuple[int, int, int, int]] = []
        self._out: list[bytes] = []

    def _reply(self, rq: bytes, status: int, body: bytes = b"") -> None:
        self._out.append((bytes([rq[0] | 0x80, rq[1], status]) + body).ljust(64, b"\x00"))

    def write(self, report: bytes) -> None:
        rq = bytes(report)
        cmd = rq[0]
        if cmd == CMD_HELLO:
            body = bytes([0, 1]) + NAME.encode().ljust(16, b"\0") + bytes(range(12))
            self._reply(rq, ST_OK, body + bytes([self.powered, self.routed]))
        elif cmd in (CMD_POWER, CMD_ROUTE):
            if rq[2] > 1:
                return self._reply(rq, ST_BAD_ARG)
            if cmd == CMD_POWER:
                self.powered = bool(rq[2])
            else:
                self.routed = bool(rq[2])
            self._reply(rq, ST_OK)
        elif cmd == CMD_ENTER:
            off, limit, half, order = struct.unpack_from("<HHBB", rq, 2)
            if order > 2 or half > 100:
                return self._reply(rq, ST_BAD_ARG)
            self.enters.append((off, limit, half, order))
            acked = self.ack_after is not None and order in (CLOCK_AUTO, self.ack_order)
            keys = self.ack_after if acked else (limit or ENTER_TIMEOUT_MS) // 2
            self.powered, self.routed = True, False
            if acked and self.on_ack:
                self.on_ack()
            self._reply(rq, ST_OK, bytes([acked, self.ack_order if acked else order])
                        + struct.pack("<HH", keys, 120 if acked else (limit or ENTER_TIMEOUT_MS)))
        elif cmd == CMD_LINES:
            self._reply(rq, ST_OK, bytes([1, 1]))
        else:
            self._reply(rq, ST_BAD_CMD)

    def read(self, timeout_ms: int) -> bytes | None:
        return self._out.pop(0) if self._out else None

    def close(self) -> None:
        pass

    @property
    def description(self) -> str:
        return "mock USB_KEY adapter"
