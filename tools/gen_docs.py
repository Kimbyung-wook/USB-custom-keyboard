"""Fill the generated regions of the Markdown docs from the code.

  uv run python tools/gen_docs.py           # rewrite the regions
  uv run python tools/gen_docs.py --check   # exit 1 and print a diff when one is stale
  uv run python tools/gen_docs.py --list    # region names and the files that have them

A region is the text between two markers; only that text is replaced:

  <!-- gen:boards -->
  (generated)
  <!-- /gen:boards -->

Regions: boards, versions, chips, pins-<BOARD> (e.g. pins-SAYO_3X3). A doc
without markers is never touched; the markers are put in by hand.
Sources: gui/core/boards.py, gui/core/chips.py, gui/core/version.py and,
through tools/repo_facts.py, the board files, src/config.h, the protocol
header, the bootloader's BL_VER and pyproject.toml.
"""
from __future__ import annotations

import difflib
import re
import sys
from pathlib import Path
from typing import Callable

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent))

import repo_facts as rf  # noqa: E402
from gui.core import boards, chips, version  # noqa: E402

REGION = re.compile(r"(<!-- gen:([\w-]+) -->)(.*?)(<!-- /gen:\2 -->)", re.S)
SKIP_DIRS = {".git", ".venv", "build", "dist", "__pycache__", ".pytest_cache", "node_modules"}


def table(head: list[str], rows: list[list[str]]) -> str:
    lines = ["| " + " | ".join(head) + " |", "|" + "|".join("---" for _ in head) + "|"]
    lines += ["| " + " | ".join(r) + " |" for r in rows]
    return "\n".join(lines)


def code(s: str) -> str:
    return f"`{s}`" if s else "-"


# ---- regions ----------------------------------------------------------------

def led_text(d: dict[str, str]) -> str:
    count = int(d["BOARD_LED_COUNT"])
    if d.get("BOARD_HAL_CH32V20X") == "1":
        return f"키마다 RGB LED {count}개 (PWM)"
    if d.get("BOARD_KEY_LEDS") == "1":
        return f"키마다 WS2812 {count}개"
    return f"WS2812 (체인 {count}자리에 같은 색)"


def gen_boards() -> str:
    files = rf.board_files()
    rows = []
    for spec in boards.BOARDS:
        d = rf.defines(files[spec.name])
        b = spec.layout
        first = code(spec.flash_text) if spec.hidboot else spec.factory_boot
        rows.append([f"{spec.name}", spec.chip_name, f"{b.keys} / {b.knobs} / {b.rows}",
                     led_text(d), f"키 {spec.boot_key}" if spec.boot_key else "-", first,
                     code(f"firmware/macro_keyboard/build/{spec.firmware_file}")])
    return table(["보드", "칩", "키 / 노브 / 줄", "LED", "업데이트 진입", "처음 설치", "펌웨어 파일"], rows)


def gen_versions() -> str:
    rows = [
        ["펌웨어", rf.fw_version(), code("firmware/macro_keyboard/src/config.h")],
        ["GUI", version.VERSION, code("gui/core/version.py")],
        ["Python 프로젝트", rf.pyproject_version(), code("pyproject.toml")],
        ["CH32V203 부트로더", rf.bootloader_version(), code("firmware/bootloader_ch32v203/main.c")],
        ["설정 프로토콜", str(rf.protocol_version()), code("firmware/macro_keyboard/src/core/protocol.h")],
        ["보드 정보 (HELLO)", str(rf.board_info_version()), code("firmware/macro_keyboard/src/config.h")],
    ]
    return table(["항목", "버전", "정의한 곳"], rows)


def gen_chips() -> str:
    rows = [[f"0x{c.chip_id:02X}", c.name, f"{c.flash_size:,} B", "예" if c.supported else "아니오",
             c.note or "-"] for c in chips.CHIPS.values()]
    return table(["칩 ID", "칩", "펌웨어 영역", "지원", "비고"], rows)


def gen_pins(name: str) -> str:
    spec = boards.find(name)    # an earlier board name (aliases) still works
    files = rf.board_files()
    if spec is None or spec.name not in files:
        raise KeyError(f"pins-{name}: no such board")
    name = spec.name
    d = rf.defines(files[name])
    if d.get("BOARD_HAL_CH32V20X") == "1":
        keys = rf.pin_pairs(d["BOARD_KEY_PINS"])
        anodes = rf.pin_pairs(d["BOARD_KEY_LED_PINS"])
        boot = int(d["BOARD_BOOT_KEY"])
        rows = [[f"{i + 1}" + (" (업데이트 진입)" if i == boot else ""), rf.gpio(*k), rf.gpio(*a)]
                for i, (k, a) in enumerate(zip(keys, anodes))]
        out = table(["키", "키 GPIO (누르면 Low)", "LED 양극 (High = 켜짐)"], rows)
        cath = rf.pin_pairs(d["BOARD_LED_PINS"])
        groups = [cath[i:i + 3] for i in range(0, len(cath), 3)]
        crow = [[f"{g + 1}", *(rf.gpio(*p) for p in grp)] for g, grp in enumerate(groups)]
        return out + "\n\nLED 음극 (Low = 켜짐, 묶음마다 공통)\n\n" + table(["묶음", "R", "G", "B"], crow)
    letters = sorted(k[len("PIN_KEY_"):] for k in d if k.startswith("PIN_KEY_"))
    chain = d.get("BOARD_KEY_LEDS") == "1"
    rows = [[f"{x}" + (" (업데이트 진입)" if x == spec.boot_key else ""), rf.ch55x_pin(d[f"PIN_KEY_{x}"]),
             str(i) if chain else "-"] for i, x in enumerate(letters)]
    out = table(["키", "핀", "LED 체인 위치"], rows)
    other = [["노브 누르기", rf.ch55x_pin(d["PIN_KNOB_SW"])], ["노브 A", rf.ch55x_pin(d["PIN_ENC_A"])],
             ["노브 B", rf.ch55x_pin(d["PIN_ENC_B"])],
             ["LED (WS2812, GRB)", rf.ch55x_pin(d["PIN_LED"]) + f", {d['BOARD_LED_COUNT']}자리"]]
    return out + "\n\n" + table(["기능", "핀"], other)


def generator(name: str) -> Callable[[], str]:
    fixed = {"boards": gen_boards, "versions": gen_versions, "chips": gen_chips}
    if name in fixed:
        return fixed[name]
    if name.startswith("pins-"):
        return lambda: gen_pins(name[len("pins-"):])
    raise KeyError(f"unknown region gen:{name}")


def region_names() -> list[str]:
    return ["boards", "versions", "chips"] + [f"pins-{b.name}" for b in boards.BOARDS]


# ---- files ------------------------------------------------------------------

def md_files(root: Path = rf.ROOT) -> list[Path]:
    out = []
    for path in sorted(root.rglob("*.md")):
        if not SKIP_DIRS.intersection(path.relative_to(root).parts):
            out.append(path)
    return out


def render(src: str, nl: str = "\n") -> tuple[str, list[str]]:
    """The doc with every region regenerated, and the region names found."""
    names = []

    def fill(m: re.Match) -> str:
        names.append(m.group(2))
        body = generator(m.group(2))().replace("\n", nl)
        return f"{m.group(1)}{nl}{body}{nl}{m.group(4)}"

    return REGION.sub(fill, src), names


def process(write: bool, out=print) -> tuple[int, list[str]]:
    """(regions found, files that differ); rewrites them when ``write``."""
    count, stale = 0, []
    for path in md_files():
        raw = path.read_bytes().decode("utf-8")
        nl = "\r\n" if "\r\n" in raw else "\n"
        new, names = render(raw, nl)
        count += len(names)
        if new != raw:
            rel = path.relative_to(rf.ROOT).as_posix()
            stale.append(rel)
            if write:
                path.write_bytes(new.encode("utf-8"))
                out(f"updated {rel} ({', '.join(names)})")
            else:
                out("".join(difflib.unified_diff(raw.splitlines(True), new.splitlines(True),
                                                 f"a/{rel}", f"b/{rel}")))
    return count, stale


def main(argv: list[str]) -> int:
    if "--list" in argv:
        for path in md_files():
            names = [m.group(2) for m in REGION.finditer(path.read_text(encoding="utf-8"))]
            if names:
                print(f"{path.relative_to(rf.ROOT).as_posix()}: {', '.join(names)}")
        print("available:", ", ".join(region_names()))
        return 0
    check = "--check" in argv
    try:
        count, stale = process(write=not check)
    except KeyError as e:
        print(f"error: {e.args[0]}")
        return 2
    if count == 0:
        print("warning: no gen:<name> regions in any doc yet")
    elif check and stale:
        print(f"stale: {', '.join(stale)} (run: uv run python tools/gen_docs.py)")
        return 1
    else:
        print(f"{count} regions{' up to date' if check else ''}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
