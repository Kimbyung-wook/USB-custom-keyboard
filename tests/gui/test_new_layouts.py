"""Planned boards not yet in gui/core/boards.py: the GUI must draw and edit any
layout HELLO reports (keys, rows, knobs), not only 6/8/9 keys.

BT_2X3 (6 keys, 2 x 3, Bluetooth, no knob) is a working name; its BoardSpec
lives only here until the firmware has a board header
(tests/gui/test_consistency.py compares those with boards.py). SAYO_2X6 (12
keys, 2 x 6) is in boards.py since 2026-11-13; its 12-key tests stay here.
"""
import pytest

pytest.importorskip("PySide6")

from PySide6.QtWidgets import QApplication, QGridLayout  # noqa: E402

from gui.core import boards, protocol as p  # noqa: E402
from gui.core.transport import MockDevice  # noqa: E402
from gui.ui.app import MainWindow, make_mock_connector  # noqa: E402

FEATURES = p.BOARD_FEATURE_RGB_LED | p.BOARD_FEATURE_LOCK | p.BOARD_FEATURE_AUTO_LOCK

SAYO_2X6 = boards.find("SAYO_2X6")
BT_2X3 = boards.BoardSpec(
    "BT_2X3", "6키 (2x3), 노브 없음, 블루투스 (시험용)",
    p.Board(keys=6, knobs=0, rows=2, name="BT_2X3", chip_id=0x19, features=FEATURES),
    boot_config=0, boot_key="A", hidboot=True)
PLANNED = (BT_2X3,)


@pytest.fixture(scope="module")
def qapp():
    return QApplication.instance() or QApplication([])


@pytest.fixture
def planned_boards(monkeypatch):
    """boards.BOARDS with the planned boards added, for this test only."""
    monkeypatch.setattr(boards, "BOARDS", boards.BOARDS + PLANNED)


def window(spec: boards.BoardSpec, **kw) -> MainWindow:
    mock = MockDevice(board=spec.layout, fw=p.SHORT_LOCK_FIRMWARE, **kw)
    w = MainWindow(make_mock_connector(mock), auto_reconnect=False)
    w.mock = mock
    w.dialogs = []
    w.ask = lambda t, m: (w.dialogs.append(("ask", t, m)), True)[1]
    w.inform = lambda t, m: w.dialogs.append(("inform", t, m))
    w.error = lambda t, m: w.dialogs.append(("error", t, m))
    return w


def key_cells(w: MainWindow) -> dict[int, tuple[int, int]]:
    """Slot -> (row, col) of the key buttons on screen."""
    grid = w.device_view._body.layout().itemAt(0).layout()
    assert isinstance(grid, QGridLayout)
    slot_of = {id(b): s for s, b in enumerate(w.device_view.buttons)}
    return {slot_of[id(grid.itemAt(k).widget())]: grid.getItemPosition(k)[:2]
            for k in range(grid.count())}


@pytest.mark.parametrize("spec,cols", [(SAYO_2X6, 6), (BT_2X3, 3)], ids=lambda x: getattr(x, "name", x))
def test_hello_layout_drawn(qapp, spec, cols):
    w = window(spec)
    w.show()
    assert w.connect_device()
    b = w.board
    assert (b.name, b.keys, b.knobs, b.rows, b.cols) == (spec.name, spec.layout.keys, 0, 2, cols)
    assert len(w.slots) == len(w.device_view.buttons) == b.keys
    assert key_cells(w) == {s: divmod(s, cols) for s in range(b.keys)}
    # A board not in boards.py still shows its own name in the board list
    assert w.board_combo.currentData() == spec.name and not w.board_combo.isEnabled()
    # Every key button fits in the window
    qapp.processEvents()
    right = max(btn.mapTo(w, btn.rect().topRight()).x() for btn in w.device_view.buttons)
    assert right < w.width()
    w.close()


def test_all_12_keys_edit_save_readback(qapp):
    w = window(SAYO_2X6)
    assert w.connect_device()
    names = [w.board.key_name(s) for s in range(12)]
    assert names == list("ABCDEFGHIJKL")
    for s in range(12):
        w.device_view.buttons[s].click()
        assert w.current == s and w.editor.title().startswith(f"키 {names[s]}")
        w.editor.edit.setPlainText(f"key{names[s]}{{ENTER}}")
        assert w.is_modified(s) and w.device_view.buttons[s].text().startswith(f"{names[s]} *")
    assert w.save_to_device() and not w.has_unsaved()
    assert w.mock.stored_config().slots == [f"key{n}{{ENTER}}" for n in names]
    w.set_slot_text(11, "scratch")
    assert w.read_from_device()
    assert w.slots[11] == "keyL{ENTER}"
    w.baseline = list(w.slots)
    w.close()


def test_lock_pattern_uses_all_12_keys(qapp):
    w = window(SAYO_2X6)
    assert w.connect_device()
    ed = w.lock_editor
    assert [b.text() for b in ed.input_buttons] == list("ABCDEFGHIJKL")
    for s in (11, 0, 6, 5, 10, 1, 7, 4):          # 8 steps, the maximum
        ed.input_buttons[s].click()
    assert w.lock == [11, 0, 6, 5, 10, 1, 7, 4]
    assert ed.display.text() == "L → A → G → F → K → B → H → E"
    assert w.save_btn.isEnabled()
    assert w.save_to_device()
    assert w.mock.stored_config().lock == [11, 0, 6, 5, 10, 1, 7, 4]
    # Without a knob, holding any key starts the pattern entry
    assert "아무 키" in ed.help_text()
    w.close()


def test_capacity_counts_12_slots(qapp):
    w = window(SAYO_2X6, capacity=128)
    assert w.connect_device()
    fixed = p.blob_size([""] * 12)
    assert w.blob_size() == fixed and fixed > p.blob_size([""] * 9)
    assert f"{128 - fixed}자 더 입력 가능 ({fixed} / 128 바이트)" in w.capacity_bar.format()
    assert "12개 슬롯" in w.capacity_bar.toolTip()
    w.set_slot_text(11, "x" * (128 - fixed))
    assert "0자 더 입력 가능" in w.capacity_bar.format() and w.save_btn.isEnabled()
    w.set_slot_text(0, "y")
    assert "1자 초과!" in w.capacity_bar.format() and not w.save_btn.isEnabled()
    w.baseline = list(w.slots)
    w.close()


def test_switch_between_planned_and_known_boards(qapp, planned_boards):
    """Picking boards while disconnected rebuilds the picture each time."""
    w = MainWindow(make_mock_connector(MockDevice()), auto_reconnect=False)
    combo = w.board_combo
    assert [combo.itemData(i) for i in range(combo.count())] == [b.name for b in boards.BOARDS]
    for name in ("SAYO_2X6", "MK6K1N", "BT_2X3", "SAYO_3X3", "SAYO_2X6"):
        i = combo.findData(name)
        w._on_board_picked(i)
        b = boards.find(name).layout
        assert w.board == b and len(w.device_view.buttons) == b.slot_count
        assert len(w.lock_editor.input_buttons) == b.slot_count
        assert key_cells(w) == {s: divmod(s, b.cols) for s in range(b.keys)}
    w.close()


@pytest.mark.parametrize("spec", PLANNED, ids=lambda s: s.name)
def test_flash_dialog_offers_planned_board(qapp, planned_boards, spec):
    from gui.ui.flash_dialog import FirmwareDialog
    dlg = FirmwareDialog(lambda: "", scan_fn=lambda: [], hid_scan_fn=lambda: [], auto_scan=False,
                         keyboard=spec.layout)
    assert dlg.board_combo.currentData() == spec.name and dlg.identified
    assert "키 A를 누른 채" in dlg.board_label.text()
    dlg.close()


def test_flash_dialog_unknown_board_not_guessed(qapp):
    """Connected to a board boards.py does not list yet: the window says it
    cannot tell which board it is instead of quietly picking one."""
    from gui.ui.flash_dialog import FirmwareDialog
    dlg = FirmwareDialog(lambda: "", scan_fn=lambda: [], hid_scan_fn=lambda: [], auto_scan=False,
                         keyboard=BT_2X3.layout)
    assert not dlg.identified
    dlg.close()
