"""Facts read from the repository's own files with regular expressions.

Shared by tools/gen_docs.py (generated doc regions) and tests/gui
(test_consistency.py, test_docs.py): board files, firmware version,
CH32V203 flash layout, Makefile variables. Nothing here is generated code.
"""
from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FW = ROOT / "firmware" / "macro_keyboard"
SRC = FW / "src"
BL = ROOT / "firmware" / "bootloader_ch32v203"


def text(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def defines(path: Path) -> dict[str, str]:
    """#define NAME value of a header and the headers it includes (same folder).

    A value continued with backslashes is joined into one line."""
    out: dict[str, str] = {}
    src = re.sub(r"\\\r?\n", " ", text(path))
    for inc in re.findall(r'^#include\s+"([^"]+)"', src, re.M):
        if (path.parent / inc).exists():
            out.update(defines(path.parent / inc))
    for name, value in re.findall(r"^#define\s+(\w+)[ \t]+([^\n]*?)\s*(?://.*)?$", src, re.M):
        out[name] = re.sub(r"\s+", " ", value).strip()
    return out


def number(value: str) -> int:
    return int(value.rstrip("uUlL").strip("()"), 0)


def make_var(path: Path, name: str) -> str:
    m = re.search(rf"^{name}\s*[:?]?=\s*(.+?)\s*$", text(path), re.M)
    assert m, f"{name} not in {path.name}"
    return m.group(1)


def board_files() -> dict[str, Path]:
    """BOARD_NAME -> board file (shared pin files have no BOARD_NAME)."""
    out = {}
    for path in sorted((SRC / "boards").glob("*.h")):
        m = re.search(r'^#define\s+BOARD_NAME\s+"([^"]+)"', text(path), re.M)
        if m:
            out[m.group(1)] = path
    return out


def pin_pairs(value: str) -> list[tuple[int, int]]:
    """"{1, 15}, {0, 14}" -> [(1, 15), (0, 14)]: {port, bit} of the CH32V203 board files."""
    return [(int(a), int(b)) for a, b in re.findall(r"\{\s*(\d+)\s*,\s*(\d+)\s*\}", value)]


def gpio(port: int, bit: int) -> str:
    """CH32V203 {port, bit} -> "PB15" (port 0 = A)."""
    return f"P{'ABCDEFG'[port]}{bit}"


def ch55x_pin(value: str) -> str:
    """CH55xduino pin number (port * 10 + bit) -> "P1.5"."""
    n = int(value.split()[0])
    return f"P{n // 10}.{n % 10}"


def fw_version() -> str:
    d = defines(SRC / "config.h")
    return f"{d['FW_MAJOR']}.{d['FW_MINOR']}.{d['FW_PATCH']}"


def bootloader_version() -> str:
    d = defines(BL / "main.c")
    return f"{d['BL_VER_MAJOR']}.{d['BL_VER_MINOR']}"


def protocol_version() -> int:
    return int(defines(SRC / "core" / "protocol.h")["PROTO_VERSION"])


def board_info_version() -> int:
    return int(defines(SRC / "config.h")["BOARD_INFO_VERSION"])


def boot_layout() -> dict[str, int]:
    """Numeric BOOT_* constants of src/hal/ch32v203/boot_layout.h."""
    return {k: number(v) for k, v in defines(SRC / "hal" / "ch32v203" / "boot_layout.h").items()
            if k.startswith("BOOT_") and re.fullmatch(r"(0x[0-9A-Fa-f]+|\d+)u?", v)}


def pyproject_version() -> str:
    return re.search(r'^version = "([^"]+)"', text(ROOT / "pyproject.toml"), re.M).group(1)
