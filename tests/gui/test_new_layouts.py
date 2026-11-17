# 요구사항(docs/REQUIREMENTS.md): REQ-GUI-03
"""Layouts beyond 6/8/9 keys: the GUI draws and edits any layout HELLO
reports (keys, rows, knobs).

SAYO_2X6 (12 keys, 2 x 6) and SAYO_2B3 (6 keys, 2 x 3, Bluetooth, power switch
SW1, CH573) are in gui/core/boards.py; boards it does not list yet are made up
here (NEW_2X5).
"""
import pytest

pytest.importorskip("PySide6")

from PySide6.QtWidgets import QApplication, QGridLayout  # noqa: E402

from gui.core import boards, protocol as p  # noqa: E402
from gui.core.transport import MockDevice  # noqa: E402
from gui.ui.app import MainWindow, make_mock_connector  # noqa: E402

FEATURES = p.BOARD_FEATURE_RGB_LED | p.BOARD_FEATURE_LOCK | p.BOARD_FEATURE_AUTO_LOCK

SAYO_2X6 = boards.find("SAYO_2X6")
SAYO_2B3 = boards.find("SAYO_2B3")
# A board boards.py does not list (10 keys, 2 x 5)
NEW_2X5 = boards.BoardSpec(
    "NEW_2X5", "10키 (2x5), 시험용",
    p.Board(keys=10, knobs=0, rows=2, name="NEW_2X5", chip_id=0x19, features=FEATURES),
    boot_config=0, boot_key="A", hidboot=True)


@pytest.fixture(scope="module")
def qapp():
    return QApplication.instance() or QApplication([])


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
    grid = w.device_view.key_grid
    assert isinstance(grid, QGridLayout)
    slot_of = {id(b): s for s, b in enumerate(w.device_view.buttons)}
    return {slot_of[id(grid.itemAt(k).widget())]: grid.getItemPosition(k)[:2]
            for k in range(grid.count())}


@pytest.mark.parametrize("spec,cols", [(SAYO_2X6, 6), (SAYO_2B3, 3), (NEW_2X5, 5)],
                         ids=lambda x: getattr(x, "name", x))
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


def test_switch_between_boards(qapp):
    """Picking boards while disconnected rebuilds the picture each time."""
    w = MainWindow(make_mock_connector(MockDevice()), auto_reconnect=False)
    combo = w.board_combo
    assert [combo.itemData(i) for i in range(combo.count())] == [b.name for b in boards.ALL_BOARDS]
    for name in ("SAYO_2X6", "MK6K1NL", "SAYO_2B3", "SAYO_3X3", "SAYO_2X6"):
        i = combo.findData(name)
        w._on_board_picked(i)
        b = boards.find(name).layout
        assert w.board == b and len(w.device_view.buttons) == b.slot_count
        assert len(w.lock_editor.input_buttons) == b.slot_count
        assert key_cells(w) == {s: divmod(s, b.cols) for s in range(b.keys)}
    w.close()


def test_sayo_2b3_without_rgb(qapp):
    """SAYO_2B3 HELLO features 0x1E: lock, auto-lock, battery, BLE; no RGB."""
    w = window(SAYO_2B3)
    assert w.connect_device()
    assert w.board.chip_id == 0x73 and w.board.features == 0x1E
    for s in (3, 0):
        w.lock_editor.input_buttons[s].click()
    assert w.lock_editor.isEnabled() and w.lock_editor.autolock_spin.isEnabled()
    assert w.lock == [3, 0] and w.save_to_device()
    w.close()


def test_flash_dialog_sayo_2b3(qapp, tmp_path):
    """Key 4 while switching SW1 on (a battery keeps it running unplugged),
    L5 red while waiting, build/ch573/ file, the ROM bootloader first time."""
    from gui.ui.flash_dialog import FirmwareDialog
    assert SAYO_2B3.firmware_file == "ch573/macro_keyboard-SAYO_2B3.bin"
    assert boards.find("SAYO_3X3").firmware_file == "ch32v203/macro_keyboard-SAYO_3X3.bin"
    assert SAYO_2B3.chip.name == "CH573" and SAYO_2B3.chip.flash_size == 446464
    dlg = FirmwareDialog(lambda: "", scan_fn=lambda: [], hid_scan_fn=lambda: [], auto_scan=False,
                         keyboard=SAYO_2B3.layout, hex_path=tmp_path / "none.bin")
    assert dlg.board_combo.currentData() == "SAYO_2B3" and dlg.identified and dlg.window_flash
    assert "업데이트 모드: 키 4를 누른 채 SW1을 켜기" in dlg.board_label.text()
    hint = dlg.hint.text()
    assert "SW1을 껐다가 <b>키 4를 누른 채</b> 다시 SW1을 켜세요" in hint
    assert "L5 빨강 LED가 깜빡입니다" in hint and "보라색" not in hint and "뽑았다가" not in hint
    assert "PB22" in hint and r"tools\flash.ps1 -Board sayo_2b3 -Full" in hint and "6~15초" in hint
    assert SAYO_2B3.flash_text == r"tools\flash.ps1 -Board sayo_2b3 -Full"
    dlg.close()


def test_flash_dialog_takes_sayo_2b3_bootloader_under_its_old_name(qapp, tmp_path):
    from gui.core import hidboot
    from test_flasher import make_bin, make_hid_dialog
    dev = hidboot.MockBootloader(board="BT_2X3")
    dlg = make_hid_dialog(tmp_path, dev, keyboard=SAYO_2B3.layout,
                          path=make_bin(tmp_path / "fw.bin", 2000, fid="MKFW1:SAYO_2B3:CH573:1.7.0"))
    assert dlg.spec.name == "SAYO_2B3" and dlg.hex_warning == "" and dlg._chip_problem() is None
    assert "SAYO_2B3용 (CH573)" in dlg.size_label.text()
    dlg.confirm_force = lambda text: pytest.fail("asked to force the same board")
    assert dlg.start() and dlg.succeeded and dev.ran


def test_main_window_sayo_2b3_boot_text(qapp):
    """Without a connection the main window tells to switch SW1 off and on."""
    w = MainWindow(make_mock_connector(MockDevice()), auto_reconnect=False)
    w._on_board_picked(w.board_combo.findData("SAYO_2B3"))
    assert w.reenter_boot == "SW1을 껐다가 키 4를 누른 채 다시 SW1을 켜세요"
    w._on_board_picked(w.board_combo.findData("SAYO_3X3"))
    assert w.reenter_boot == "키보드를 뽑았다가 키 7을 누른 채 다시 꽂으세요"
    w.close()


def test_flash_dialog_unknown_board_not_guessed(qapp):
    """Connected to a board boards.py does not list: the window says it
    cannot tell which board it is instead of quietly picking one."""
    from gui.ui.flash_dialog import FirmwareDialog
    dlg = FirmwareDialog(lambda: "", scan_fn=lambda: [], hid_scan_fn=lambda: [], auto_scan=False,
                         keyboard=NEW_2X5.layout)
    assert not dlg.identified
    dlg.close()


def test_autolock_note_blames_the_board(qapp):
    """Firmware 1.6.0+ without the AUTO_LOCK bit: the board, not the firmware version."""
    lock_only = boards.BoardSpec(
        "NEW_2X3", "6키, 시험용",
        p.Board(keys=6, knobs=0, rows=2, name="NEW_2X3", chip_id=0x19, features=p.BOARD_FEATURE_LOCK),
        boot_config=0, boot_key="A", hidboot=True)
    w = window(lock_only)
    assert w.connect_device()
    w.lock_editor.input_buttons[0].click()
    note = w.lock_editor.autolock_note.text()
    assert note == "이 보드는 자동 잠금을 지원하지 않습니다" and "1.4.0" not in note
    w.lock_baseline = list(w.lock)
    w.close()


CH573_APP = 446464      # docs/BOOTLOADER_CH573.md: app 0x3000 ~ 0x6FFFF


@pytest.mark.parametrize("size", [121 * 1024, CH573_APP])
def test_flash_dialog_sayo_2b3_full_app_area(qapp, tmp_path, monkeypatch, size):
    """The BLE app (~121 KB) up to the whole CH573 app area goes through the
    window: no size limit below 446,464 B, progress to 100 %, and CHECK
    (CRC over the whole app in the bootloader) gets more than 1 s."""
    from gui.core import hidboot
    from test_flasher import make_bin, make_hid_dialog
    timeouts = {}
    request = hidboot.Bootloader.request

    def spy(self, cmd, args=b"", timeout_ms=hidboot.TIMEOUT_MS):
        timeouts[cmd] = max(timeouts.get(cmd, 0), timeout_ms)
        return request(self, cmd, args, timeout_ms)
    monkeypatch.setattr(hidboot.Bootloader, "request", spy)
    dev = hidboot.MockBootloader(board="SAYO_2B3", app_addr=0x3000, app_max=CH573_APP)
    dlg = make_hid_dialog(tmp_path, dev, keyboard=SAYO_2B3.layout,
                          path=make_bin(tmp_path / "fw.bin", size, fid="MKFW1:SAYO_2B3:CH573:1.7.0"))
    assert dlg.hex_warning == "" and dlg._chip_problem() is None
    seen = []
    dlg.bar.valueChanged.connect(seen.append)
    assert dlg.start() and dlg.succeeded and dev.ran
    assert dev.info[0] == size and bytes(dev.flash[:size]) == (tmp_path / "fw.bin").read_bytes()
    assert seen[-1] == 100 and len(set(seen)) > 50
    assert timeouts[hidboot.CMD_CHECK] >= 1000 + size // 100
    assert hidboot.check_timeout_ms(CH573_APP) == 5464


def test_sayo_2b3_image_over_app_area_refused(qapp, tmp_path):
    from gui.core import flasher, hidboot
    from test_flasher import make_bin
    path = make_bin(tmp_path / "big.bin", CH573_APP + 1, fid="MKFW1:SAYO_2B3:CH573:1.7.0")
    assert flasher.check_bin(make_bin(tmp_path / "max.bin", CH573_APP), SAYO_2B3.chip).size == CH573_APP
    with pytest.raises(flasher.FlashError, match="너무 큽니다"):
        flasher.check_bin(path, SAYO_2B3.chip)
    image = path.read_bytes()
    dev = hidboot.MockBootloader(board="SAYO_2B3", app_addr=0x3000, app_max=CH573_APP)
    with pytest.raises(hidboot.BootError, match="최대 446464 B"):
        hidboot.Bootloader(dev).flash(image)
    assert dev.writes == 0
