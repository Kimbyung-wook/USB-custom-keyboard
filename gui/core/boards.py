"""Boards the GUI knows (docs/BOARD_INFO.md 5-1).

One entry per keyboard product (PCB), named like the firmware's board file
(firmware/macro_keyboard/src/boards/<name>.h). HELLO's board info is what the
keyboard really is; this list is for when no firmware of ours answers, such as
in the bootloader: which uploader settings to use and how to get there.
"""
from __future__ import annotations

from dataclasses import dataclass

from . import chips
from .protocol import BOARD_FEATURE_AUTO_LOCK, BOARD_FEATURE_LOCK, BOARD_FEATURE_RGB_LED, Board

# vnproch55x -c: the bootloader entry pin written with the firmware
BOOT_P15 = 1        # P1.5 pull-down: hold the key on P1.5 while plugging in
BOOT_P36 = 3        # P3.6 (USB D+) pull-up, CH55xDuino's default


@dataclass(frozen=True)
class BoardSpec:
    name: str              # HELLO bytes 24-31, also in the firmware ID
    description: str
    layout: Board          # keys, knobs, rows and chip as HELLO reports them
    boot_config: int       # BOOT_P15 or BOOT_P36
    boot_key: str = ""     # the key on the entry pin, e.g. "D" (BOOT_P15)
    factory_boot: str = "" # how to reach the bootloader on the factory firmware
    boot_text: str = ""    # boot_hint when it is not a key (e.g. a bridge on the board)
    flash_text: str = ""   # how to flash it outside the update window (first install)
    hidboot: bool = False  # our own HID bootloader (gui/core/hidboot.py), .bin files
    # Earlier names of this board, as keyboards flashed before a rename still
    # report them (HELLO, bootloader HELLO, firmware ID). Only here: find() and
    # same() take them, nothing else in the code knows them
    aliases: tuple[str, ...] = ()

    @property
    def chip(self) -> chips.Chip | None:
        return chips.find(self.layout.chip_id)

    @property
    def chip_name(self) -> str:
        return chips.name_of(self.layout.chip_id)

    @property
    def firmware_file(self) -> str:
        """What tools/build_fw.ps1 writes for this board."""
        if self.hidboot:
            return f"ch32v203/macro_keyboard-{self.name}.bin"
        return f"macro_keyboard-{self.name}.hex"

    @property
    def boot_hint(self) -> str:
        """How to plug it in to reach the bootloader, e.g. "키 D를 누른 채"."""
        if self.boot_text:
            return self.boot_text
        if not self.boot_key:
            return "업데이트 모드 단추를 누른 채"
        # L, M, N, R, 1, 3, 6, 7, 8, 0 end in a consonant when read aloud (엘, 엠, 일, 칠, ...)
        return f"키 {self.boot_key}{'을' if self.boot_key[-1] in 'LMNR136780' else '를'} 누른 채"


BOARDS = (
    BoardSpec("MK6K1N", "6키 + 노브 1개 (CH552G)",
              Board(keys=6, knobs=1, rows=2, name="MK6K1N", chip_id=0x52),
              boot_config=BOOT_P15, boot_key="D",
              factory_boot="키 E를 누른 채 꽂으세요"),
    # Same pins as MK6K1N, with an LED under every key (tests/hw/mk6k1nl/test_mk6k1nl)
    BoardSpec("MK6K1NL", "6키 + 노브 1개, 키마다 LED (CH552G)",
              Board(keys=6, knobs=1, rows=2, name="MK6K1NL", chip_id=0x52),
              boot_config=BOOT_P15, boot_key="D",
              factory_boot="키로는 들어가지 않습니다. 칩 12번(D+)과 16번(3V3) 핀 사이에 "
                           "1.5~2kΩ 저항을 댄 채 꽂으세요 (docs/TROUBLESHOOTING.md 2-3)"),
    # CH32V203C8U6, 9 keys, no knob (docs/board/BOARD_SAYO_3X3V_RGB.md). Our own HID bootloader
    # (docs/BOOTLOADER_CH32V203.md): key 7 at power-up, or BOOT from the app. It goes
    # on once through the ROM bootloader (BOOT bridge, wchisp: flash_text).
    # Without a knob, holding any key starts the lock pattern entry.
    BoardSpec("SAYO_3X3", "9키 (3x3), 노브 없음, 키별 RGB LED (CH32V203)",
              Board(keys=9, knobs=0, rows=3, name="SAYO_3X3", chip_id=0x19,
                    features=BOARD_FEATURE_RGB_LED | BOARD_FEATURE_LOCK | BOARD_FEATURE_AUTO_LOCK),
              boot_config=0, boot_key="7", hidboot=True,
              factory_boot="부트로더가 아직 없으면(공장 펌웨어 또는 처음) BOOT 브리지(BOOT1 아래 패드 2개)를 "
                           r"이은 채 꽂고 PC에서 tools\flash.ps1 -Board sayo_3x3 -Full을 실행하세요 (wchisp)",
              flash_text=r"tools\flash.ps1 -Board sayo_3x3 -Full",
              aliases=("3X3V_RGB",)),   # until firmware 1.7.0 and bootloader 1.0 (2026-11-08)
    # CH32V203C8U6, 8 keys (2 x 4), no knob, same scheme as SAYO_3X3; boot key 5
    # (bottom left, PA5; docs/board/BOARD_SAYO_2X4V_RGB.md). Pins: firmware/macro_keyboard/src/boards/sayo_2x4.h
    BoardSpec("SAYO_2X4", "8키 (2x4), 노브 없음, 키별 RGB LED (CH32V203)",
              Board(keys=8, knobs=0, rows=2, name="SAYO_2X4", chip_id=0x19,
                    features=BOARD_FEATURE_RGB_LED | BOARD_FEATURE_LOCK | BOARD_FEATURE_AUTO_LOCK),
              boot_config=0, boot_key="5", hidboot=True,
              factory_boot="부트로더가 아직 없으면(공장 펌웨어 또는 처음) BOOT 브리지(BOOT1 아래 패드 2개)를 "
                           r"이은 채 꽂고 PC에서 tools\flash.ps1 -Board sayo_2x4 -Full을 실행하세요 (wchisp)",
              flash_text=r"tools\flash.ps1 -Board sayo_2x4 -Full",
              aliases=("2X4V_RGB",)),   # until firmware 1.7.0 and bootloader 1.0 (2026-11-08)
    # CH32V203C8U6, 12 keys (2 x 6), no knob, same scheme as SAYO_3X3; boot key 7
    # (bottom left, PA7; docs/board/BOARD_SAYO_2X6.md). Name provisional.
    # Pins: firmware/macro_keyboard/src/boards/sayo_2x6.h
    BoardSpec("SAYO_2X6", "12키 (2x6), 노브 없음, 키별 RGB LED (CH32V203)",
              Board(keys=12, knobs=0, rows=2, name="SAYO_2X6", chip_id=0x19,
                    features=BOARD_FEATURE_RGB_LED | BOARD_FEATURE_LOCK | BOARD_FEATURE_AUTO_LOCK),
              boot_config=0, boot_key="7", hidboot=True,
              factory_boot="부트로더가 아직 없으면(공장 펌웨어 또는 처음) BOOT 브리지를 "
                           r"이은 채 꽂고 PC에서 tools\flash.ps1 -Board sayo_2x6 -Full을 실행하세요 (wchisp)",
              flash_text=r"tools\flash.ps1 -Board sayo_2x6 -Full"),
)


def find(name: str) -> BoardSpec | None:
    """The board called ``name`` now or under an earlier name (aliases)."""
    for spec in BOARDS:
        if spec.name == name or name in spec.aliases:
            return spec
    return None


def same(a: str, b: str) -> bool:
    """``a`` and ``b`` name the same board (one may be an earlier name)."""
    if a == b:
        return True
    spec = find(a)
    return spec is not None and spec is find(b)


def current_name(name: str) -> str:
    """The board's name now (an earlier name -> the current one; unknown stays)."""
    spec = find(name)
    return spec.name if spec else name


def display_name(name: str) -> str:
    """How the GUI shows a board name: the current one, plus the earlier name
    the keyboard (or file) still reports, e.g. "SAYO_3X3 (옛 이름 3X3V_RGB)"."""
    now = current_name(name)
    return now if now == name else f"{now} (옛 이름 {name})"
