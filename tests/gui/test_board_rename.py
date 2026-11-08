"""Boards renamed (2026-11-08: 3X3V_RGB -> SAYO_3X3, 2X4V_RGB -> SAYO_2X4).

Keyboards flashed before the rename (bootloader 1.0, firmware 1.7.0) report
the earlier name. gui/core/boards.py aliases make them the same board: the
GUI, check_device and the hidboot board check take them without --force.
"""
from __future__ import annotations

import sys
from pathlib import Path

import pytest

from gui.core import boards, hidboot
from gui.core import protocol as p
from gui.core.transport import MockDevice

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
import check_device as cd  # noqa: E402

RENAMED = [("3X3V_RGB", "SAYO_3X3"), ("2X4V_RGB", "SAYO_2X4")]


@pytest.mark.parametrize("old, new", RENAMED)
def test_earlier_name_finds_the_board(old, new):
    assert boards.find(old) is boards.find(new) is not None
    assert boards.same(old, new) and boards.same(new, old) and boards.current_name(old) == new
    assert boards.find(new).name == new


def test_other_boards_stay_apart():
    assert not boards.same("3X3V_RGB", "SAYO_2X4") and not boards.same("SAYO_3X3", "2X4V_RGB")
    assert not boards.same("3X3V_RGB", "NOPE") and boards.current_name("NOPE") == "NOPE"
    assert boards.same("NOPE", "NOPE")   # an unknown board is still itself


def app(board: str, size: int = 2000) -> bytes:
    fid = f"MKFW1:{board}:CH32V203:1.7.0\0".encode()
    return (fid + bytes((i * 7) & 0xFF for i in range(size)))[:size]


@pytest.mark.parametrize("bootloader, image_board", [
    ("3X3V_RGB", "SAYO_3X3"),   # bootloader 1.0 on the keyboard, new app: no BOOT bridge, no --force
    ("SAYO_3X3", "3X3V_RGB"),   # new bootloader, an app built before the rename
    ("2X4V_RGB", "SAYO_2X4"),
])
def test_hidboot_takes_the_earlier_name(bootloader, image_board):
    dev = hidboot.MockBootloader(board=bootloader)
    image = app(image_board)
    hidboot.Bootloader(dev).flash(image)
    assert dev.info is not None


def test_hidboot_still_refuses_another_board():
    dev = hidboot.MockBootloader(board="3X3V_RGB")
    with pytest.raises(hidboot.BootError, match="SAYO_2X4"):
        hidboot.Bootloader(dev).flash(app("SAYO_2X4"))
    assert not dev.erased


@pytest.mark.parametrize("old, new", RENAMED)
def test_check_device_takes_the_earlier_name(old, new):
    spec = boards.find(new)
    layout = p.Board(keys=spec.layout.keys, knobs=0, rows=spec.layout.rows, name=old,
                     chip_id=0x19, features=spec.layout.features)
    lines: list[str] = []
    code = cd.main([], connect=lambda: MockDevice(capacity=4096, board=layout), out=lines.append)
    text = "\n".join(lines)
    assert code == 0, text
    assert f"옛 보드 이름 {old}" in text and f"{new}와 같음" in text


def test_main_window_lists_an_old_keyboard_under_its_new_name():
    from PySide6.QtWidgets import QApplication
    from gui.ui.app import MainWindow, make_mock_connector
    _app = QApplication.instance() or QApplication([])  # noqa: F841
    old = p.Board(keys=9, knobs=0, rows=3, name="3X3V_RGB", chip_id=0x19,
                  features=boards.find("SAYO_3X3").layout.features)
    w = MainWindow(make_mock_connector(MockDevice(capacity=4096, board=old)), auto_reconnect=False)
    assert w.connect_device()
    names = [w.board_combo.itemData(i) for i in range(w.board_combo.count())]
    assert w.board_combo.currentData() == "SAYO_3X3" and "3X3V_RGB" not in names
    assert "키 7" in w.boot_hint
    w.close()


def test_flash_dialog_takes_an_old_keyboard_and_bootloader(tmp_path):
    from PySide6.QtWidgets import QApplication
    from test_flasher import make_bin, make_hid_dialog
    _app = QApplication.instance() or QApplication([])  # noqa: F841
    old = p.Board(keys=9, knobs=0, rows=3, name="3X3V_RGB", chip_id=0x19)
    dev = hidboot.MockBootloader(board="3X3V_RGB")
    dlg = make_hid_dialog(tmp_path, dev, keyboard=old,
                          path=make_bin(tmp_path / "new.bin", 2000, fid="MKFW1:SAYO_3X3:CH32V203:1.7.0"))
    assert dlg.spec.name == "SAYO_3X3" and not dlg.other_board and dlg.hex_warning == ""
    dlg.confirm_force = lambda text: pytest.fail("asked to force the same board")
    assert dlg.start_btn.isEnabled() and dlg.start() and dlg.succeeded and dev.ran


# ---- what the GUI shows: the current name, with the earlier one beside it

@pytest.mark.parametrize("old, new", RENAMED)
def test_display_name(old, new):
    assert boards.display_name(new) == new
    assert boards.display_name(old) == f"{new} (옛 이름 {old})"
    assert boards.display_name("SAYO_2X6") == "SAYO_2X6"     # not in boards.py: as is


def test_main_window_shows_an_old_keyboard_by_its_new_name():
    from PySide6.QtWidgets import QApplication
    from gui.ui.app import MainWindow, make_mock_connector
    _app = QApplication.instance() or QApplication([])  # noqa: F841
    old = p.Board(keys=8, knobs=0, rows=2, name="2X4V_RGB", chip_id=0x19,
                  features=boards.find("SAYO_2X4").layout.features)
    w = MainWindow(make_mock_connector(MockDevice(capacity=4096, board=old)), auto_reconnect=False)
    assert w.connect_device()
    assert "SAYO_2X4 (옛 이름 2X4V_RGB) (키 8)" in w.conn_label.text()
    assert "SAYO_2X4 (키 8)" in w.windowTitle() and "2X4V_RGB" not in w.windowTitle()
    names = [w.board_combo.itemData(i) for i in range(w.board_combo.count())]
    assert names == [b.name for b in boards.BOARDS]               # listed once, new name
    w.close()


def test_flash_dialog_shows_new_name_and_still_warns_for_another_board(tmp_path):
    from PySide6.QtWidgets import QApplication
    from test_flasher import make_bin, make_hid_dialog
    _app = QApplication.instance() or QApplication([])  # noqa: F841
    old = p.Board(keys=9, knobs=0, rows=3, name="3X3V_RGB", chip_id=0x19)
    dev = hidboot.MockBootloader(board="3X3V_RGB")
    dlg = make_hid_dialog(tmp_path, dev, keyboard=old,
                          path=make_bin(tmp_path / "old.bin", 2000, fid="MKFW1:3X3V_RGB:CH32V203:1.7.0"))
    names = [dlg.board_combo.itemData(i) for i in range(dlg.board_combo.count())]
    assert names == [b.name for b in boards.BOARDS]
    assert "연결되어 있던 키보드: SAYO_3X3 (옛 이름 3X3V_RGB)" in dlg.board_label.text()
    assert "SAYO_3X3 (옛 이름 3X3V_RGB)용" in dlg.size_label.text()   # a file built before the rename
    assert dlg.hex_warning == "" and not dlg.other_board

    # Another board picked: warned, and [장입] stays off since the bootloader
    # (old name) says it is the 9-key board
    dlg.board_combo.setCurrentIndex(dlg.board_combo.findData("SAYO_2X4"))
    assert dlg.other_board and "⚠ 연결되어 있던 키보드는 SAYO_3X3 (옛 이름 3X3V_RGB)" in dlg.board_label.text()
    assert dlg.target is None and not dlg.start_btn.isEnabled() and not dlg.start()
    assert "부트로더가 SAYO_3X3 (옛 이름 3X3V_RGB) 보드라고" in dlg._chip_problem()

    # Back on the right board with an 8-key file: asked to force, no means no
    dlg.board_combo.setCurrentIndex(dlg.board_combo.findData("SAYO_3X3"))
    dlg.set_hex(make_bin(tmp_path / "eight.bin", 2000, fid="MKFW1:2X4V_RGB:CH32V203:1.7.0"))
    assert dlg.hex_warning == "다른 키보드(SAYO_2X4 (옛 이름 2X4V_RGB))용 펌웨어"
    forced = []
    dlg.confirm = lambda title, text: pytest.fail("asked the plain confirm instead of force")
    dlg.confirm_force = lambda text: (forced.append(text), False)[1]
    assert dlg.start_btn.isEnabled() and not dlg.start() and not dev.ran
    assert forced and "SAYO_3X3 부트로더는 이 파일을 받지 않습니다" in forced[0]
