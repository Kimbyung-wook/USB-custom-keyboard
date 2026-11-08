"""Numbers and lists written in more than one place agree.

Read with regular expressions only (nothing is generated): the CH32V203 flash
layout (src/hal/ch32v203/boot_layout.h) against the Makefiles, mkfull.py, the
GUI and tools/flash.ps1; the board files (src/boards/*.h) against
gui/core/boards.py and the tools' board lists; the firmware version against
gui/core/version.py and pyproject.toml.
"""
from __future__ import annotations

import importlib.util
import re
import sys
from pathlib import Path

import pytest

from gui.core import boards, chips, hidboot, version

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
from repo_facts import BL, FW, ROOT, SRC, board_files, defines, make_var, number, text  # noqa: E402


# ---- (a) CH32V203 flash layout --------------------------------------------

LAYOUT = {k: v for k, v in defines(SRC / "hal" / "ch32v203" / "boot_layout.h").items()
          if k.startswith("BOOT_") and re.fullmatch(r"(0x[0-9A-Fa-f]+|\d+)u?", v)}
BASE = number(LAYOUT["BOOT_FLASH_BASE"])
LOADER = number(LAYOUT["BOOT_LOADER_SIZE"])
APP = number(LAYOUT["BOOT_APP_ADDR"])
INFO = number(LAYOUT["BOOT_INFO_ADDR"])
STORE = number(LAYOUT["BOOT_STORE_ADDR"])
PAGE = number(LAYOUT["BOOT_PAGE_SIZE"])
MAGIC = number(LAYOUT["BOOT_INFO_MAGIC"])
MARK = number(LAYOUT["BOOT_MARK_ADDR"])
FLASH_SIZE = 0x10000  # CH32V203C8: 64 KB
RAM_BASE = 0x20000000


def test_layout_itself():
    assert APP == BASE + LOADER
    assert APP < INFO < STORE < BASE + FLASH_SIZE
    assert INFO + PAGE <= STORE


def test_app_makefile():
    mk = FW / "Makefile.ch32v203"
    assert int(make_var(mk, "IMAGE_MAX")) == INFO - APP
    assert int(make_var(mk, "STORE_SIZE")) == BASE + FLASH_SIZE - STORE
    origin, length = re.search(r"ORIGIN = (0x[0-9A-Fa-f]+), LENGTH = (\d+)/'", text(mk)).groups()
    assert (int(origin, 16), int(length)) == (APP - BASE, INFO - APP)
    ram = re.search(r"LENGTH = 20K/LENGTH = (0x[0-9A-Fa-f]+)/", text(mk)).group(1)
    assert RAM_BASE + int(ram, 16) == MARK


def test_bootloader_makefile():
    mk = BL / "Makefile"
    assert int(make_var(mk, "BL_MAX")) == LOADER
    flash = re.search(r"LENGTH = 64K/LENGTH = (\d+)K/", text(mk)).group(1)
    assert int(flash) * 1024 == LOADER
    ram = re.search(r"LENGTH = 20K/LENGTH = (0x[0-9A-Fa-f]+)/", text(mk)).group(1)
    assert RAM_BASE + int(ram, 16) == MARK


def test_mkfull():
    spec = importlib.util.spec_from_file_location("mkfull", BL / "mkfull.py")
    mkfull = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mkfull)
    assert (mkfull.LOADER_SIZE, mkfull.INFO_OFF, mkfull.MAGIC) == (LOADER, INFO - BASE, MAGIC)


def test_gui():
    assert (hidboot.APP_ADDR, hidboot.INFO_ADDR, hidboot.PAGE_SIZE, hidboot.INFO_MAGIC) == \
        (APP, INFO, PAGE, MAGIC)
    ch32 = [c for c in chips.CHIPS.values() if c.name == "CH32V203"]
    assert [c.flash_size for c in ch32] == [INFO - APP]


def test_flash_ps1():
    src = text(ROOT / "tools" / "flash.ps1")
    # the bootloader size limit, and the full image length named in the comments
    assert [int(v, 16) for v in re.findall(r"-gt (0x[0-9A-Fa-f]+)", src)] == [LOADER]
    for v in re.findall(r"0xEF10|0x[0-9A-Fa-f]{4}(?= B\b)", src):
        assert int(v, 16) == INFO - BASE + 16


def test_config_capacity():
    cfg = text(SRC / "config.h")
    m = re.search(r"#if BOARD_HAL_CH32V20X\s*#define BLOB_CAPACITY (\d+)", cfg)
    assert m and int(m.group(1)) == BASE + FLASH_SIZE - STORE


# ---- (b) board files against gui/core/boards.py -----------------------------

BOARD_FILES = board_files()


def test_same_board_list():
    assert sorted(BOARD_FILES) == sorted(b.name for b in boards.BOARDS)
    for name, path in BOARD_FILES.items():
        assert path.stem == name.lower()


@pytest.mark.parametrize("name", sorted(BOARD_FILES))
def test_board_file_matches_gui(name):
    d = defines(BOARD_FILES[name])
    spec = boards.find(name)
    got = (int(d["BOARD_KEYS"]), int(d["BOARD_KNOBS"]), int(d["BOARD_ROWS"]),
           number(d["BOARD_CHIP_ID"]))
    assert got == (spec.layout.keys, spec.layout.knobs, spec.layout.rows, spec.layout.chip_id)
    assert len(name) <= 8
    hid = d.get("BOARD_HAL_CH32V20X") == "1"
    assert spec.hidboot == hid
    if "BOARD_BOOT_KEY" in d:
        assert spec.boot_key == str(int(d["BOARD_BOOT_KEY"]) + 1)
    else:
        assert not hid, "a board with our bootloader needs BOARD_BOOT_KEY"


def test_config_h_selects_every_board():
    cfg = text(SRC / "config.h")
    picked = dict(re.findall(r'defined\(BOARD_(\w+)\)\s*#include "boards/(\w+)\.h"', cfg))
    assert picked == {name: name.lower() for name in BOARD_FILES}


# ---- (c) the tools' board lists -------------------------------------------

def validate_set(src: str, param: str) -> list[str]:
    m = re.search(rf"\[ValidateSet\(([^)]*)\)\]\s*(?:\[string\])?\s*\${param}\b", src)
    assert m, f"no ValidateSet for ${param}"
    return re.findall(r"'([^']*)'", m.group(1))


def test_build_fw_boards():
    src = text(ROOT / "tools" / "build_fw.ps1")
    assert sorted(validate_set(src, "Board")) == sorted(n.lower() for n in BOARD_FILES)
    chips_of = dict(re.findall(r"^\s*'?(\w+)'?\s*=\s*'(ch552|ch32v203)'", src, re.M))
    for name, path in BOARD_FILES.items():
        d = defines(path)
        assert chips_of[name.lower()] == ("ch32v203" if d.get("BOARD_HAL_CH32V20X") == "1" else "ch552")


def test_flash_ps1_boards():
    src = text(ROOT / "tools" / "flash.ps1")
    hid = sorted(b.name.lower() for b in boards.BOARDS if b.hidboot)
    assert sorted(v for v in validate_set(src, "Board") if v) == hid
    uids = re.search(r"\$knownUid = @\{([^}]*)\}", src).group(1)
    assert sorted(re.findall(r'"(\w+)"\s*=', uids)) == hid
    # the boot key comes from the board file, not a table
    assert "BOARD_BOOT_KEY" in src


# ---- earlier board names ------------------------------------------------------

def test_board_aliases_only_in_boards_py():
    """An earlier board name (BoardSpec.aliases) is written in gui/core/boards.py
    only (tests and Markdown may name it); nothing else may depend on it."""
    aliases = [a for b in boards.BOARDS for a in b.aliases]
    current = [b.name for b in boards.BOARDS]
    assert len(set(aliases)) == len(aliases) and not set(aliases) & set(current)
    for b in boards.BOARDS:
        assert all(len(a) <= 8 for a in b.aliases)
    code = [path for path in ROOT.rglob("*")
            if path.is_file() and (path.suffix in (".py", ".c", ".h", ".ps1", ".S", ".inc")
                                   or path.name.startswith("Makefile"))
            and not {".git", ".venv", "build", "dist", "__pycache__"} & set(path.relative_to(ROOT).parts)
            and path.relative_to(ROOT).parts[0] != "tests"
            and path != ROOT / "gui" / "core" / "boards.py"]
    for path in code:
        src = text(path)
        for alias in aliases:
            assert alias not in src and alias.lower() not in src, f"{alias} in {path.relative_to(ROOT)}"


# ---- tool paths -------------------------------------------------------------

TOOL_FILES = [FW / "Makefile.ch32v203", BL / "Makefile",
              ROOT / "tests" / "hw" / "ch32v203" / "test_ch32v203" / "Makefile",
              ROOT / "tests" / "hw" / "ch32v203" / "test_ch32v203" / "diag" / "Makefile",
              ROOT / "tools" / "build_fw.ps1", ROOT / "tools" / "flash.ps1"]
TOOL_PATH = re.compile(r"C:[/\\]Users[/\\]user[/\\]tools[/\\][^'\"\s)]+")


def test_tool_default_paths_agree():
    """Every default under C:/Users/user/tools names the same folder per tool,
    and each file lets the environment (MK_EVT_SDK, MK_RISCV_BIN, MK_WCHISP) win."""
    seen: dict[str, set[str]] = {}
    for path in TOOL_FILES:
        src = text(path)
        for found in TOOL_PATH.findall(src):
            p_ = found.replace("\\", "/")
            tool = p_.split("/")[4]                 # sdk, gcc, wchisp
            seen.setdefault(tool, set()).add(p_)
        if path.suffix == ".ps1":
            for var in re.findall(r"\$env:(MK_\w+)", src):
                assert var in ("MK_EVT_SDK", "MK_RISCV_BIN", "MK_WCHISP")
        else:
            assert "MK_EVT_SDK ?=" in src and "SDK ?= $(MK_EVT_SDK)" in src and "MK_RISCV_BIN" in src
    assert set(seen) == {"sdk", "gcc", "wchisp"}
    assert all(len(v) == 1 for v in seen.values()), seen
    ps = text(ROOT / "tools" / "build_fw.ps1") + text(ROOT / "tools" / "flash.ps1")
    assert all(f"$env:{v}" in ps for v in ("MK_EVT_SDK", "MK_RISCV_BIN", "MK_WCHISP"))


# ---- (d) version ------------------------------------------------------------

def test_firmware_version_is_gui_version():
    d = defines(SRC / "config.h")
    fw = f"{d['FW_MAJOR']}.{d['FW_MINOR']}.{d['FW_PATCH']}"
    assert fw == version.VERSION
    py = re.search(r'^version = "([^"]+)"', text(ROOT / "pyproject.toml"), re.M).group(1)
    assert py == version.VERSION
