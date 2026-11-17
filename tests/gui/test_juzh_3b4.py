# 요구사항(docs/REQUIREMENTS.md): REQ-GUI-03, REQ-GUI-07
"""JUZH_3B4 (JieLi AC6321A, 12 keys 3 x 4, 2 knobs on the right, BT slot LEDs,
portrait view)
and its update window. Fakes only: no HID report or SCSI command reaches a
real device."""
import pytest

from gui.core import boards, jluboot as j, protocol as p

JUZH = boards.find("JUZH_3B4")
LOADER = bytes(range(256)) * 5


def test_board_definition():
    assert JUZH in boards.PREPARING and JUZH not in boards.BOARDS   # no firmware board file yet
    b = JUZH.layout
    assert (b.keys, b.knobs, b.rows, b.cols, b.slot_count) == (12, 2, 3, 4, 18)
    assert JUZH.jieli and not JUZH.hidboot and JUZH.knob_column and JUZH.rotatable and JUZH.knob_leds == 3
    assert JUZH.chip_name == "AC6321A"
    assert [b.slot_label(s) for s in (12, 14, 15)] == ["노브 1 시계 방향", "노브 1 누르기", "노브 2 시계 방향"]
    assert boards.ALL_BOARDS[-1] is JUZH and boards.find("JUZH_3B4") is JUZH


def test_led_help_slots_and_charging():
    text = JUZH.led_help
    assert "슬롯 1·2·3" in text and "0.2초" in text and "1초" in text and "3초" in text
    assert "충전" in text and "상관없음" in text        # charging LED: hardware only
    assert boards.BT_SLOT_LED_RULES in text


# ------------------------------------------------------------------ flow (core)

def flasher(fake, **kw):
    log = []
    f = j.JlFlasher(open_fn=lambda path: fake, paths=[fake.path], loader=LOADER,
                    log=log.append, wait_s=0, poll_s=0, **kw)
    return f, log


def test_flow_find_open_backup(tmp_path):
    fake = j.FakeUboot(vendor="BD19", flash_size=64 * 1024)
    f, log = flasher(fake)
    found = f.wait()
    assert found.inquiry.vendor == "BD19" and f.stage == j.STAGE_WAIT
    info = f.open()
    assert info.chip == "BD19" and f.uboot.loader_running
    path = f.backup(tmp_path)
    assert path.read_bytes() == bytes(fake.flash) and path.name.startswith("bd19_")
    assert any("백업 저장" in line for line in log)


def test_write_waits_for_the_format(tmp_path):
    fake = j.FakeUboot(vendor="BD19", flash_size=64 * 1024)
    f, _ = flasher(fake)
    assert not j.WRITE_READY
    with pytest.raises(j.UbootError, match="준비 중"):
        f.write(b"\x00" * 100)
    assert j.ERASE_FLASH_BLOCK not in fake.log and j.ERASE_FLASH_SECTOR not in fake.log   # nothing erased


def test_write_and_restart_once_ready(monkeypatch):
    monkeypatch.setattr(j, "WRITE_READY", True)
    fake = bd19()
    f, _ = flasher(fake)
    f.write(b"\x5A" * 5000)
    assert bytes(fake.flash[:5000]) == b"\x5A" * 5000
    with pytest.raises(j.UbootError, match="쓰기 영역"):
        f.write(b"\x00" * (0x30000 + 1))
    f.restart()
    assert fake.ran_app == 1 and f.stage == j.STAGE_DONE and f.uboot is None


def bd19(**kw):
    """JUZH_3B4 as measured: 512 KiB SPI NOR CD7113, loader buffer 0x8000."""
    return j.FakeUboot(vendor="BD19", flash_size=512 * 1024, flash_id=0xCD7113, usb_buffer=0x8000, **kw)


def test_bd19_write_order(monkeypatch):
    """Erase 64 KiB blocks 0x00000, 0x10000, 0x20000 (BTIF at 0x3E000 stays,
    no chip erase), FB04 from 0 in 0x8000 pieces with FC0E after each,
    then FD05 of the whole image."""
    monkeypatch.setattr(j, "WRITE_READY", True)
    fake = bd19()
    fake.flash[0x3E000:0x3F000] = b"\xB7" * 0x1000           # BTIF
    image = bytes((i * 7 + (i >> 9)) & 0xFF for i in range(0x15000))   # jl_isd.bin size
    f, _ = flasher(fake)
    f.open()
    start = len(fake.log)
    seen = []
    f.write(image, lambda d, t: seen.append((d, t)))
    sent = fake.log[start:]
    assert fake.erases == [0x00000, 0x10000, 0x20000]
    assert j.ERASE_FLASH_CHIP not in fake.log and j.ERASE_FLASH_SECTOR not in sent
    assert fake.writes == [(0, 0x8000), (0x8000, 0x8000), (0x10000, 0x5000)]
    assert all(a % 0x1000 == 0 for a, _ in fake.writes)
    w = [c for c in sent if c in (j.WRITE_FLASH, j.FLASH_CRC16)]
    assert w == [j.WRITE_FLASH, j.FLASH_CRC16] * 3                     # a CRC after every piece
    assert sent[-1] == j.READ_FLASH and sent.index(j.READ_FLASH) > sent.index(j.FLASH_CRC16)
    assert bytes(fake.flash[:0x15000]) == image
    assert fake.flash[0x15000:0x30000] == b"\xff" * (0x30000 - 0x15000)    # rest of the area erased
    assert fake.flash[0x30000:0x3E000] == b"\xA5" * (0x3E000 - 0x30000)    # untouched
    assert fake.flash[0x3E000:0x3F000] == b"\xB7" * 0x1000                 # BTIF kept
    assert seen[-1] == (2 * len(image), 2 * len(image))
    with pytest.raises(j.UbootError, match="0번지"):
        f.write(image, addr=0x1000)


def test_bd19_piece_crc_mismatch_stops(monkeypatch):
    monkeypatch.setattr(j, "WRITE_READY", True)
    fake = bd19()
    f, _ = flasher(fake)
    f.open()
    orig = fake.execute
    def bad_crc(c, data_out=None, data_in=0):
        r = orig(c, data_out=data_out, data_in=data_in)
        return r[:2] + bytes([r[2] ^ 0xFF]) + r[3:] if c[:2] == b"\xFC\x0E" else r
    fake.execute = bad_crc
    with pytest.raises(j.UbootError, match="조각 CRC"):
        f.write(b"\x11" * 0x9000)
    assert fake.writes == [(0, 0x8000)]                       # stopped after the first piece


def test_bd19_forbidden_and_no_handler_commands():
    fake = bd19()
    f, _ = flasher(fake)
    f.open()
    for cmd in (j.ERASE_FLASH_CHIP, j.GET_LOADER_VER, 0xFC0F, 0xFC16):
        with pytest.raises(j.UbootError, match="명령입니다"):
            f.uboot._exec(cmd)
        assert cmd not in fake.log


def test_bd19_backup_twice_in_32k_reads_and_restore(tmp_path, monkeypatch):
    fake = bd19()
    fake.flash[:] = bytes((i * 31 + (i >> 12)) & 0xFF for i in range(len(fake.flash)))
    original = bytes(fake.flash)
    f, _ = flasher(fake)
    f.open()
    start = len(fake.log)
    path = f.backup(tmp_path)
    reads = fake.log[start:]
    assert reads == [j.READ_FLASH] * 32                       # 512 KiB / 32 KiB, twice
    assert path.read_bytes() == original and f.info.flash_size == 512 * 1024
    with pytest.raises(j.UbootError, match="준비 중"):         # restore writes too: gated
        f.restore(path)
    monkeypatch.setattr(j, "WRITE_READY", True)
    f.write(b"\x00" * 0x15000)                                # a new firmware
    f.restore(path)
    assert bytes(fake.flash[:0x30000]) == original[:0x30000]
    assert bytes(fake.flash[0x30000:]) == original[0x30000:]  # never erased
    short = tmp_path / "short.bin"
    short.write_bytes(original[:0x1000])
    with pytest.raises(j.UbootError, match="백업 크기"):
        f.restore(short)


def test_wait_times_out():
    f = j.JlFlasher(open_fn=lambda p_: (_ for _ in ()).throw(j.ScsiError("no")), paths=["x"], wait_s=0)
    with pytest.raises(j.UbootError, match="나타나지 않았습니다"):
        f.wait()


def test_relaunch_as_admin_asks_windows():
    calls = []

    def shell(hwnd, verb, exe, params, cwd, show):
        calls.append((verb, exe, params))
        return 42
    assert j.relaunch_as_admin(["gui/main.py", "--mock"], shell_execute=shell)
    verb, exe, params = calls[0]
    assert verb == "runas" and params.endswith("main.py --mock")
    assert not j.relaunch_as_admin(["gui/main.py"], shell_execute=lambda *a: 5)   # UAC cancelled


# ------------------------------------------------------------------ window

pytest.importorskip("PySide6")


@pytest.fixture(scope="module")
def qapp():
    from PySide6.QtWidgets import QApplication
    return QApplication.instance() or QApplication([])


def main_window(tmp_path):
    from PySide6.QtCore import QSettings
    from gui.core.transport import MockDevice
    from gui.ui.app import MainWindow, make_mock_connector
    settings = QSettings(str(tmp_path / "gui.ini"), QSettings.IniFormat)
    w = MainWindow(make_mock_connector(MockDevice()), auto_reconnect=False, settings=settings)
    w.ask = lambda title, text: True       # e.g. closing with unsaved edits: no modal box
    return w


def pos(dv, widget):
    return widget.mapTo(dv, widget.rect().topLeft())


def test_picture_knobs_right_with_slot_leds(qapp, tmp_path):
    from PySide6.QtWidgets import QWidget
    w = main_window(tmp_path)
    i = w.board_combo.findData("JUZH_3B4")
    assert i >= 0 and "슬롯" in w.board_combo.itemData(i, 3)        # tooltip role
    w._on_board_picked(i)
    dv = w.device_view
    assert len(dv.buttons) == 18 and not dv.turned
    assert w.portrait_btn.isVisibleTo(w) and not w.portrait_btn.isChecked()
    # keys 3 x 4, the leftmost thing on the board
    cells = {dv.key_grid.getItemPosition(k)[:2] for k in range(dv.key_grid.count())}
    assert cells == {(r, c) for r in range(3) for c in range(4)}
    assert dv._body.layout().itemAt(0).layout() is dv.key_grid
    w.show()
    qapp.processEvents()
    key_d, knob1_cw, knob2_cw = dv.buttons[3], dv.buttons[12], dv.buttons[15]
    # knob column right of the keys: knob 1, the LEDs one above the other, knob 2
    assert pos(dv, knob1_cw).x() > pos(dv, key_d).x() and pos(dv, knob2_cw).x() > pos(dv, key_d).x()
    l1, l2, l3 = (pos(dv, led) for led in dv.status_leds)
    assert pos(dv, knob1_cw).y() < l1.y() < l2.y() < l3.y() < pos(dv, knob2_cw).y()
    assert l1.x() == l2.x() == l3.x()
    assert [led.text() for led in dv.status_leds] == ["1", "2", "3"]
    assert all("블루투스" in led.toolTip() for led in dv.status_leds)
    assert all(isinstance(led, QWidget) and led not in dv.buttons for led in dv.status_leds)
    # other boards keep the knobs side by side, no LEDs, no portrait button
    w._on_board_picked(w.board_combo.findData("MK6K1NL"))
    assert w.device_view.status_leds == [] and not w.portrait_btn.isVisibleTo(w)
    assert w.device_view._body.layout().itemAt(0).layout() is w.device_view.key_grid
    w.close()


def test_portrait_view_turns_the_picture_only(qapp, tmp_path):
    w = main_window(tmp_path)
    w._on_board_picked(w.board_combo.findData("JUZH_3B4"))
    w.set_slot_text(5, "{ENTER}")
    w.portrait_btn.click()                        # [세로 보기]
    dv = w.device_view
    assert dv.turned and w.portrait_btn.isChecked() and w.portrait_btn.text() == "가로 보기"
    assert len(dv.buttons) == 18 and w.slots[5] == "{ENTER}"   # same slots, same settings
    # keys 4 rows x 3 columns: turned counter-clockwise, D (row 0, col 3) is top left,
    # A (row 0, col 0) bottom left, L (row 2, col 3) top right
    at = {dv.key_grid.getItemPosition(dv.key_grid.indexOf(dv.buttons[s]))[:2]: s for s in range(12)}
    assert set(at) == {(r, c) for r in range(4) for c in range(3)}
    assert (at[(0, 0)], at[(3, 0)], at[(0, 2)], at[(3, 2)]) == (3, 0, 11, 8)
    w.show()
    qapp.processEvents()
    key_top = pos(dv, dv.buttons[3])
    k1, k2 = pos(dv, dv.buttons[12]), pos(dv, dv.buttons[15])
    l1, l2, l3 = (pos(dv, led) for led in dv.status_leds)
    # knobs on top: knob 1 left, knob 2 right, the LEDs in a row between them
    assert k1.y() < key_top.y() and k2.y() < key_top.y()
    assert k1.x() < l1.x() < l2.x() < l3.x() < k2.x() and l1.y() == l2.y() == l3.y()
    # knob buttons keep their physical meaning (clockwise is still clockwise)
    assert dv.buttons[12].toolTip().startswith("노브 1 시계 방향") and "시계" in dv.buttons[12].text()
    assert dv.buttons[5].text().startswith("F")                  # texts drawn again after turning
    w.close()


def test_portrait_view_is_remembered_per_board(qapp, tmp_path):
    w = main_window(tmp_path)
    w._on_board_picked(w.board_combo.findData("JUZH_3B4"))
    w.portrait_btn.click()
    w._on_board_picked(w.board_combo.findData("SAYO_2X6"))      # not rotatable: landscape
    assert not w.device_view.turned and not w.portrait_btn.isVisibleTo(w)
    w._on_board_picked(w.board_combo.findData("JUZH_3B4"))      # back: portrait again
    assert w.device_view.turned and w.portrait_btn.isChecked()
    w.close()
    w2 = main_window(tmp_path)                                    # next start, same settings file
    w2._on_board_picked(w2.board_combo.findData("JUZH_3B4"))
    assert w2.device_view.turned
    w2.portrait_btn.click()                                       # back to landscape
    assert not w2.device_view.turned and w2.portrait_btn.text() == "세로 보기"
    w2.close()
    assert main_window(tmp_path).settings.value("view/JUZH_3B4/portrait") == "false"


class FakeHid:
    def __init__(self):
        self.sent, self.closed = [], False

    def write(self, data):
        self.sent.append(bytes(data))
        return len(data)

    def close(self):
        self.closed = True


def jl_dialog(fake=None, admin=True, **kw):
    from gui.ui.jl_flash_dialog import JlFlashDialog
    fake = fake or bd19()
    f = j.JlFlasher(open_fn=lambda path: fake, paths=[fake.path], loader=LOADER, wait_s=0, poll_s=0)
    hid = FakeHid()
    relaunched = []
    dlg = JlFlashDialog(JUZH, flasher=f, is_admin=lambda: admin,
                        relaunch=lambda: relaunched.append(1) or True,
                        enter_factory=lambda: j.enter_factory(paths=[b"p"], open_path=lambda p_: hid), **kw)
    return dlg, fake, hid, relaunched


def test_dialog_factory_entry_find_and_backup(qapp, tmp_path):
    dlg, fake, hid, _ = jl_dialog(backup_dir=tmp_path)
    assert dlg.method == j.ENTRY_FACTORY and not dlg.admin_btn.isVisibleTo(dlg)
    assert not dlg.start_btn.isEnabled() and "준비 중" in dlg.start_btn.toolTip()
    assert dlg.enter()
    assert hid.sent == [j.FACTORY_ENTER]
    assert dlg.find() and "BD19 UBOOT1.00" in dlg.device_label.text()
    assert dlg.backup_btn.isEnabled()
    assert dlg.backup_only()
    assert list(tmp_path.glob("bd19_*.bin")) and "백업 완료" in dlg.stage_label.text()
    assert dlg.flasher.found is None and "다시" in dlg.stage_label.text()   # the loader resets when idle
    dlg.set_file(tmp_path / "fw.bin")
    assert not dlg.start_btn.isEnabled()                       # writing not ready
    assert not dlg.start() and "준비 중" in dlg.stage_label.text()
    assert j.ERASE_FLASH_SECTOR not in fake.log
    dlg.close_dialog()


def test_dialog_our_firmware_uses_boot(qapp):
    booted = []
    dlg, *_ = jl_dialog(enter_app=lambda: booted.append(1) or "키보드를 업데이트 모드로 바꿨습니다",
                        our_firmware=True)
    assert dlg.method == j.ENTRY_APP and dlg.enter() and booted == [1]
    dlg.close_dialog()
    dlg, *_ = jl_dialog()
    dlg.method_combo.setCurrentIndex(dlg.method_combo.findData(j.ENTRY_APP))
    assert not dlg.enter() and "연결되어 있지 않습니다" in dlg.stage_label.text()
    dlg.close_dialog()


def test_dialog_without_admin(qapp):
    dlg, fake, hid, relaunched = jl_dialog(admin=False)
    assert dlg.admin_btn.isVisibleTo(dlg) and "관리자" in dlg.admin_label.text()
    assert not dlg.find_btn.isEnabled()
    assert dlg.enter() and "관리자" in dlg.stage_label.text() and not dlg.wait_timer.isActive()
    assert dlg.relaunch_admin() and relaunched == [1] and dlg.relaunched
    dlg.close_dialog()


def test_dialog_write_when_ready(qapp, tmp_path, monkeypatch):
    monkeypatch.setattr(j, "WRITE_READY", True)
    dlg, fake, hid, _ = jl_dialog(backup_dir=tmp_path)
    image = tmp_path / "fw.bin"
    image.write_bytes(b"\x33" * 6000)
    assert dlg.find()
    dlg.set_file(image)
    assert dlg.start_btn.isEnabled() and dlg.start()
    assert bytes(fake.flash[:6000]) == b"\x33" * 6000 and fake.ran_app == 1
    assert list(tmp_path.glob("bd19_*.bin"))                   # backed up first
    assert dlg.succeeded and dlg.bar.value() == 100


def test_main_window_opens_the_jieli_window(qapp):
    from gui.core.transport import MockDevice
    from gui.ui.app import MainWindow, make_mock_connector
    from gui.ui.jl_flash_dialog import JlFlashDialog
    mock = MockDevice(board=p.Board(keys=12, knobs=2, rows=3, name="JUZH_3B4", chip_id=0,
                                    features=JUZH.layout.features))
    w = MainWindow(make_mock_connector(mock), auto_reconnect=False)
    assert w.connect_device()
    assert "슬롯" in w.conn_label.toolTip()
    dlg = w.make_jl_dialog(JUZH, flasher=j.JlFlasher(paths=[]), is_admin=lambda: True)
    assert isinstance(dlg, JlFlashDialog) and dlg.method == j.ENTRY_APP
    dlg.close_dialog()
    w.close()
