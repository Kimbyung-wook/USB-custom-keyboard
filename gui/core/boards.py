"""Boards the GUI knows (docs/BOARD_INFO.md 5-1).

One entry per keyboard product (PCB), named like the firmware's board file
(firmware/macro_keyboard/src/boards/<name>.h). HELLO's board info is what the
keyboard really is; this list is for when no firmware of ours answers, such as
in the bootloader: which uploader settings to use and how to get there.
"""
from __future__ import annotations

from dataclasses import dataclass

from . import chips
from .protocol import (BOARD_FEATURE_AUTO_LOCK, BOARD_FEATURE_BATTERY, BOARD_FEATURE_BLE, BOARD_FEATURE_LOCK,
                       BOARD_FEATURE_RGB_LED, Board)

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
    # Power switch to turn off and on instead of unplugging (a battery keeps the
    # board running when the cable is pulled), e.g. "SW1"
    power_switch: str = ""
    # Bluetooth: the keyboard keeps pairings (bonds, slot hosts) outside the
    # config blob; a factory reset clears them too
    bluetooth: bool = False
    # What the board shows while our bootloader waits (hidboot boards)
    boot_led: str = "LED가 보라색입니다"
    # Flashed through the JieLi ROM's USB download mode (gui/core/jluboot.py)
    # instead of a WCH bootloader
    jieli: bool = False
    chip_text: str = ""    # chip name when chips.py has no entry for it (yet)
    # Knobs in a column right of the keys, one above the other (else right, side by side)
    knob_column: bool = False
    # The board is also used turned 90 degrees counter-clockwise: the window
    # offers a portrait view (same slots, only the picture turns)
    rotatable: bool = False
    # LEDs the GUI explains: (name, what it shows). Hardware-only LEDs (charging) are left out
    leds: tuple[tuple[str, str], ...] = ()
    # Status LEDs drawn between the knobs (knob_column boards), e.g. the BT slots 1-3
    knob_leds: int = 0

    @property
    def chip(self) -> chips.Chip | None:
        return chips.find(self.layout.chip_id)

    @property
    def chip_name(self) -> str:
        return self.chip_text or chips.name_of(self.layout.chip_id)

    @property
    def firmware_file(self) -> str:
        """What tools/build_fw.ps1 (or the chip's Makefile) writes for this board."""
        if self.hidboot:    # build/<chip>/, e.g. build/ch32v203/, build/ch573/
            return f"{self.chip_name.lower()}/macro_keyboard-{self.name}.bin"
        return f"macro_keyboard-{self.name}.hex"

    @property
    def led_help(self) -> str:
        """The LED explanation, one "name: meaning" line each."""
        return "\n".join(f"{name}: {text}" for name, text in self.leds)

    @property
    def boot_hint(self) -> str:
        """How to plug it in to reach the bootloader, e.g. "키 D를 누른 채"."""
        if self.boot_text:
            return self.boot_text
        if not self.boot_key:
            return "업데이트 모드 단추를 누른 채"
        return f"키 {self.boot_key}{_object(self.boot_key)} 누른 채"

    def power_on(self, ending: str) -> str:
        """The plug-in (or switch-on) verb with ending "기", "고", "면" or
        "세요", e.g. "꽂으면", "SW1을 켜면"."""
        if self.power_switch:
            verb = {"기": "켜기", "고": "켜고", "면": "켜면", "세요": "켜세요"}[ending]
            return f"{self.power_switch}{_object(self.power_switch)} {verb}"
        return {"기": "꽂기", "고": "꽂고", "면": "꽂으면", "세요": "꽂으세요"}[ending]

    @property
    def power_off(self) -> str:
        if self.power_switch:
            return f"{self.power_switch}{_object(self.power_switch)} 껐다가"
        return "키보드를 뽑았다가"

    def enter_boot(self, ending: str) -> str:
        """How to reach the bootloader, e.g. "키 4를 누른 채 SW1을 켜기"."""
        return f"{self.boot_hint} {self.power_on(ending)}"

    def reenter_boot(self, ending: str) -> str:
        """e.g. "키보드를 뽑았다가 키 7을 누른 채 다시 꽂으세요"."""
        return f"{self.power_off} {self.boot_hint} 다시 {self.power_on(ending)}"


def _object(word: str) -> str:
    """을/를 after a key or switch name read aloud: L, M, N, R, 1, 3, 6, 7, 8, 0
    end in a consonant (엘, 엠, 일, 칠, ...)."""
    return "을" if word[-1] in "LMNR136780" else "를"


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
    # CH32V203C8U6, 9 keys, no knob (docs/board/BOARD_SAYO_3X3_RGB.md). Our own HID bootloader
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
    # (bottom left, PA5; docs/board/BOARD_SAYO_2X4_RGB.md). Pins: firmware/macro_keyboard/src/boards/sayo_2x4.h
    BoardSpec("SAYO_2X4", "8키 (2x4), 노브 없음, 키별 RGB LED (CH32V203)",
              Board(keys=8, knobs=0, rows=2, name="SAYO_2X4", chip_id=0x19,
                    features=BOARD_FEATURE_RGB_LED | BOARD_FEATURE_LOCK | BOARD_FEATURE_AUTO_LOCK),
              boot_config=0, boot_key="5", hidboot=True,
              factory_boot="부트로더가 아직 없으면(공장 펌웨어 또는 처음) BOOT 브리지(BOOT1 아래 패드 2개)를 "
                           r"이은 채 꽂고 PC에서 tools\flash.ps1 -Board sayo_2x4 -Full을 실행하세요 (wchisp)",
              flash_text=r"tools\flash.ps1 -Board sayo_2x4 -Full",
              aliases=("2X4V_RGB",)),   # until firmware 1.7.0 and bootloader 1.0 (2026-11-08)
    # CH32V203C8U6, 12 keys (2 x 6), no knob, same scheme as SAYO_3X3; boot key 7
    # (bottom left, PA7). PCB marking 2x6_RGB (docs/board/BOARD_SAYO_2X6_RGB.md).
    # Pins: firmware/macro_keyboard/src/boards/sayo_2x6.h
    BoardSpec("SAYO_2X6", "12키 (2x6), 노브 없음, 키별 RGB LED (CH32V203)",
              Board(keys=12, knobs=0, rows=2, name="SAYO_2X6", chip_id=0x19,
                    features=BOARD_FEATURE_RGB_LED | BOARD_FEATURE_LOCK | BOARD_FEATURE_AUTO_LOCK),
              boot_config=0, boot_key="7", hidboot=True,
              factory_boot="부트로더가 아직 없으면(공장 펌웨어 또는 처음) BOOT 브리지를 "
                           r"이은 채 꽂고 PC에서 tools\flash.ps1 -Board sayo_2x6 -Full을 실행하세요 (wchisp)",
              flash_text=r"tools\flash.ps1 -Board sayo_2x6 -Full"),
    # CH573F, 6 keys (2 x 3), no knob, Bluetooth with a battery and power switch
    # SW1 (docs/board/BOARD_SAYO_2X3B_RGB.md). Our HID bootloader for CH573
    # (docs/BOOTLOADER_CH573.md, same commands as CH32V203): key 4 (bottom left,
    # PA9) while switching SW1 on, or BOOT from the app; L5 red blinks there.
    # First install through the ROM bootloader (PB22 to GND, wchisp, -full.bin).
    # Pins: firmware/macro_keyboard/src/hal/ch573/sayo_2b3.h. Settings over USB only
    BoardSpec("SAYO_2B3", "6키 (2x3), 노브 없음, 블루투스 (CH573)",
              Board(keys=6, knobs=0, rows=2, name="SAYO_2B3", chip_id=0x73,
                    # HELLO 0x1E: the firmware adds AUTO_LOCK to lock boards, BATTERY from BOARD_BATTERY, BLE from BOARD_BLE
                    features=BOARD_FEATURE_LOCK | BOARD_FEATURE_BATTERY | BOARD_FEATURE_AUTO_LOCK
                    | BOARD_FEATURE_BLE),
              boot_config=0, boot_key="4", hidboot=True, power_switch="SW1", bluetooth=True,
              boot_led="L5 빨강 LED가 깜빡입니다",
              # The ROM bootloader leaves after about 6-15 s without traffic: start
              # flash.ps1 first (it waits for the chip), then enter
              factory_boot="부트로더가 아직 없으면(공장 펌웨어 또는 처음) PC에서 먼저 "
                           r"tools\flash.ps1 -Board sayo_2b3 -Full을 실행하고, 안내가 나오면 SW1을 끄고 "
                           "USB를 꽂은 채 PB22를 GND에 대고 SW1을 켠 뒤 바로 Enter를 누르세요. "
                           "ROM 부트로더는 통신이 없으면 6~15초 뒤 빠져나갑니다 (wchisp, docs/BOOTLOADER_CH573.md)",
              flash_text=r"tools\flash.ps1 -Board sayo_2b3 -Full",
              aliases=("BT_2X3",)),   # CH573 bootloader before the rename (2026-11-13)
)


# Bluetooth slot LEDs, the same rules as SAYO_2B3 (docs/board/BOARD_SAYO_2X3B_RGB.md, LED table)
BT_SLOT_LED_RULES = ("페어링 대기 = 빠르게 깜빡임(0.2초), 재연결 중 = 느리게 깜빡임(1초), "
                     "연결됨 = 3초 켜진 뒤 꺼짐, 슬롯 바꿈 = 그 슬롯이 1초 켜짐. "
                     "광고는 60초 뒤 멈추고 LED도 꺼짐")


# Boards the GUI draws and flashes before the firmware has a board file for
# them (tests/gui/test_consistency.py checks BOARDS against the board files).
# A board moves into BOARDS with its board file.
PREPARING = (
    # JieLi AC6321A (family BD19), 12 keys (3 x 4) and 2 knobs in a column on the
    # right with the BT slot LEDs between them; also used turned 90 degrees
    # counter-clockwise (knobs on top, keys 4 x 3 below), Bluetooth (docs/board/BOARD_KEY12_JUZHEN_V01.md). No WCH bootloader:
    # the ROM's USB download mode (UBOOT disk 4C4A:3442), reached over the same
    # USB cable: 03 EF EF on the factory firmware's vendor HID, BOOT on ours.
    # HELLO chip ID not decided yet (0: "unknown")
    BoardSpec("JUZH_3B4", "12키 (3x4) + 노브 2개, 블루투스 (AC6321A)",
              Board(keys=12, knobs=2, rows=3, name="JUZH_3B4", chip_id=0,
                    features=BOARD_FEATURE_LOCK | BOARD_FEATURE_AUTO_LOCK),
              boot_config=0, jieli=True, bluetooth=True, chip_text="AC6321A", knob_column=True, knob_leds=3, rotatable=True,
              boot_text="USB를 꽂은 채 GUI의 [UBOOT로 전환]으로",
              factory_boot="공장 펌웨어 상태면 USB를 꽂고 업데이트 창에서 진입 방법 "
                           "'공장 펌웨어'로 [UBOOT로 전환]을 누르세요 (벤더 HID 명령 03 EF EF)",
              leds=(("노브 사이 LED 1·2·3", "블루투스 연결 슬롯 1·2·3. " + BT_SLOT_LED_RULES),
                    ("충전 LED", "충전 회로가 직접 켜는 표시라 GUI·펌웨어와 상관없음"))),
)

ALL_BOARDS = BOARDS + PREPARING


# Earlier names of boards not in BOARDS yet: earlier name -> current name.
# A board's aliases move into its BoardSpec when it is added to BOARDS.
PENDING_ALIASES: dict[str, str] = {}


def find(name: str) -> BoardSpec | None:
    """The board called ``name`` now or under an earlier name (aliases)."""
    name = PENDING_ALIASES.get(name, name)
    for spec in ALL_BOARDS:
        if spec.name == name or name in spec.aliases:
            return spec
    return None


def same(a: str, b: str) -> bool:
    """``a`` and ``b`` name the same board (one may be an earlier name)."""
    return current_name(a) == current_name(b)


def current_name(name: str) -> str:
    """The board's name now (an earlier name -> the current one; unknown stays)."""
    spec = find(name)
    return spec.name if spec else PENDING_ALIASES.get(name, name)


def display_name(name: str) -> str:
    """How the GUI shows a board name: the current one, plus the earlier name
    the keyboard (or file) still reports, e.g. "SAYO_3X3 (옛 이름 3X3V_RGB)"."""
    now = current_name(name)
    return now if now == name else f"{now} (옛 이름 {name})"
