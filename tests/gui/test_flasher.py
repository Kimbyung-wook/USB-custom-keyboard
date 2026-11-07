"""Firmware upload helpers (gui/core/flasher.py) and the update dialog."""
import sys
import textwrap
from pathlib import Path

import pytest

from gui.core import boards, chips, flasher

ERASE = "\x1b[2K"
CH552 = chips.find(0x52)
FW_ID = "MKFW1:MK6K1N:CH552:1.3.0"


def make_hex(path: Path, size: int, start: int = 0, fid: str | None = None) -> Path:
    """Intel HEX with `size` bytes of 0x00 from `start`, 16 bytes per record,
    the firmware ID string (if any) at `start`."""
    data = (fid.encode() + bytes(1) if fid else b"").ljust(size, bytes(1))[:size]
    lines = []
    addr = start
    while addr < start + size:
        n = min(16, start + size - addr)
        rec = bytes([n, addr >> 8 & 0xFF, addr & 0xFF, 0]) + data[addr - start:addr - start + n]
        lines.append(":" + (rec + bytes([-sum(rec) & 0xFF])).hex().upper())
        addr += n
    lines.append(":00000001FF")
    path.write_text("\n".join(lines) + "\n", encoding="ascii")
    return path


# ---------------------------------------------------------------- hex file

def test_hex_size_is_highest_address(tmp_path):
    assert flasher.check_hex(make_hex(tmp_path / "a.hex", 11587), CH552).size == 11587
    assert flasher.hex_size(make_hex(tmp_path / "b.hex", 10, start=0x100)) == 0x10A
    image = flasher.read_hex(make_hex(tmp_path / "c.hex", 2, start=4))
    assert image == b"\xff" * 4 + bytes(2)                 # gaps read as erased flash


def test_too_big_hex_rejected(tmp_path):
    path = make_hex(tmp_path / "big.hex", CH552.flash_size + 1)
    with pytest.raises(flasher.FlashError) as e:
        flasher.check_hex(path, CH552)
    assert "너무 큽니다" in str(e.value) and "CH552" in str(e.value)
    assert flasher.check_hex(make_hex(tmp_path / "max.hex", 14336), CH552).size == 14336
    with pytest.raises(flasher.FlashError):                 # CH551 has less flash
        flasher.check_hex(make_hex(tmp_path / "551.hex", 10241), chips.find(0x51))


def test_firmware_id_read_from_hex(tmp_path):
    info = flasher.check_hex(make_hex(tmp_path / "id.hex", 8000, fid=FW_ID), CH552)
    assert info.firmware_id == flasher.FirmwareId("MK6K1N", "CH552", "1.3.0")
    assert flasher.check_hex(make_hex(tmp_path / "old.hex", 8000), CH552).firmware_id is None
    assert flasher.firmware_id(b"xxMKFW1:MK6K1N\0") is None        # incomplete


@pytest.mark.parametrize("text, msg", [
    ("hello\n", "Intel HEX"),
    (":10000000000000000000000000000000000000FF\n:00000001FF\n", "체크섬"),
    (":0100000000FF\n", "EOF"),
    (":00000001FF\n", "데이터가 없습니다"),
])
def test_bad_hex_rejected(tmp_path, text, msg):
    path = tmp_path / "bad.hex"
    path.write_text(text, encoding="ascii")
    with pytest.raises(flasher.FlashError) as e:
        flasher.check_hex(path, CH552)
    assert msg in str(e.value)


def test_missing_file(tmp_path):
    with pytest.raises(flasher.FlashError):
        flasher.check_hex(tmp_path / "none.hex", CH552)


def test_command_matches_flash_ps1(tmp_path):
    spec = boards.find("MK6K1N")
    cmd = flasher.build_command(Path("up.exe"), tmp_path / "fw.hex", spec.chip.name, spec.boot_config, 30)
    assert cmd == ["up.exe", "-r", "30", "-t", "CH552", "-c", "1", str(tmp_path / "fw.hex")]


def test_board_list():
    spec = boards.find("MK6K1N")
    assert spec.layout.slot_count == 9 and spec.chip_name == "CH552" and spec.chip.supported
    assert spec.boot_config == boards.BOOT_P15 and spec.boot_hint == "키 D를 누른 채"
    assert spec.firmware_file == "macro_keyboard-MK6K1N.hex"
    lit = boards.find("MK6K1NL")
    assert lit.layout.slot_count == 9 and lit.chip_name == "CH552" and lit.boot_hint == "키 D를 누른 채"
    assert lit.firmware_file == "macro_keyboard-MK6K1NL.hex"
    assert boards.find("MK9K0N") is None
    assert all(boards.find(b.name).layout.name == b.name for b in boards.BOARDS)
    assert chips.name_of(0x54) == "CH554" and chips.name_of(0x59) == "CH559"


def test_uploader_from_env(tmp_path, monkeypatch):
    exe = tmp_path / flasher.UPLOADER_NAME
    exe.write_bytes(b"")
    monkeypatch.setenv("MACROKB_UPLOADER", str(exe))
    monkeypatch.setenv("LOCALAPPDATA", str(tmp_path / "nothing"))
    assert flasher.find_uploader() == exe
    monkeypatch.setenv("MACROKB_UPLOADER", str(tmp_path / "missing.exe"))
    with pytest.raises(flasher.FlashError):
        flasher.find_uploader()


# ---------------------------------------------------------------- uploader output

# What vnproch55x printed in a real upload (dots redrawn after an erase code)
REAL_LOG = (
    "usbRertySeconds 60\ntarget: CH552\nconfig bytes: 1\n"
    + "No CH55x USB Found, retry...\n" * 3
    + "CH375 open OK\nMCU ID: 52 11\nFound Device CH552\nBootloader: 2.5.0\n"
    "ID: 7D E2 0F BD\nWrite 11587 bytes from bin file.\n"
    + "".join(f"{ERASE}{'.' * n}" for n in range(1, 51)) + "\nWrite complete!!!\nVerify chip\n"
    + "".join(f"{ERASE}{'.' * n}" for n in range(1, 51)) + "\nVerify complete!!!\n"
    "------------------------------------------------------------------\nReset OK\n"
)


def test_progress_parses_real_log_in_pieces():
    p = flasher.FlashProgress()
    seen = []
    for i in range(0, len(REAL_LOG), 7):     # arrives in arbitrary chunks
        p.feed(REAL_LOG[i:i + 7])
        seen.append((p.stage, p.percent))
    p.finish()
    assert (p.chip, p.bootloader, p.written) == ("CH552", "2.5.0", 11587)
    assert p.succeeded and p.reset and p.stage == flasher.STAGE_DONE and p.percent == 100
    assert p.lines.count("No CH55x USB Found, retry...") == 1
    percents = [pc for _, pc in seen]
    assert percents == sorted(percents)                    # never goes backwards
    assert any(st == flasher.STAGE_WRITING and 0 < pc < 50 for st, pc in seen)
    assert any(st == flasher.STAGE_VERIFYING and 50 < pc < 100 for st, pc in seen)


def test_progress_without_device_fails():
    p = flasher.FlashProgress()
    p.feed("No CH55x USB Found, retry...\n" * 5 + "Can not find CH55x\n")
    p.finish()
    assert p.stage == flasher.STAGE_WAITING and not p.succeeded


# ---------------------------------------------------------------- dialog

pytest.importorskip("PySide6")


@pytest.fixture(scope="module")
def qapp():
    from PySide6.QtWidgets import QApplication
    return QApplication.instance() or QApplication([])


def fake_uploader(tmp_path: Path, log: str, code: int) -> list[str]:
    script = tmp_path / "fake_uploader.py"
    script.write_text(textwrap.dedent(f"""
        import sys
        sys.stdout.write({log!r})
        sys.stdout.flush()
        sys.exit({code})
    """), encoding="utf-8")
    return [sys.executable, str(script)]


def chip(index=0, chip_id=0x52, uid=bytes.fromhex("7DE20FBD")):
    from gui.core.bootloader import ChipInfo
    return ChipInfo(index, chip_id, 0x11, "2.5.0", uid)


def make_dialog(tmp_path, chips, cmd=("unused",), prepare=lambda: "", hex_size=11587,
                fid=FW_ID, **kw):
    from gui.ui.flash_dialog import FirmwareDialog
    dlg = FirmwareDialog(prepare, lambda hex_path: list(cmd),
                         hex_path=make_hex(tmp_path / "fw.hex", hex_size, fid=fid),
                         scan_fn=lambda: list(chips), auto_scan=False, **kw)
    dlg.asked = []
    dlg.confirm = lambda title, text: (dlg.asked.append(text), dlg.answer)[1]
    dlg.answer = True
    return dlg


def run_dialog(qapp, tmp_path, log, code, hex_size=11587):
    cmd = fake_uploader(tmp_path, log, code)
    dlg = make_dialog(tmp_path, [chip()], cmd, hex_size=hex_size)
    assert dlg.process is None                             # choosing the file does not flash
    assert dlg.start_btn.isEnabled()
    assert dlg.start()
    assert dlg.process.waitForFinished(10000)
    qapp.processEvents()
    return dlg


def test_dialog_upload_success(qapp, tmp_path):
    dlg = run_dialog(qapp, tmp_path, REAL_LOG, 0)
    assert dlg.succeeded and dlg.bar.value() == 100
    assert "장입 완료" in dlg.stage_label.text()
    assert "Found Device CH552" in dlg.log.toPlainText()
    assert dlg.close_btn.text() == "닫기"


def test_dialog_upload_timeout(qapp, tmp_path):
    dlg = run_dialog(qapp, tmp_path, "No CH55x USB Found, retry...\n" * 3, 1)
    assert not dlg.succeeded
    assert "찾지 못했습니다" in dlg.stage_label.text()
    assert dlg.start_btn.isEnabled()                       # can try again


def test_dialog_refuses_oversized_hex(qapp, tmp_path):
    dlg = make_dialog(tmp_path, [chip()], hex_size=CH552.flash_size + 16)
    assert not dlg.start_btn.isEnabled() and not dlg.start()
    assert "너무 큽니다" in dlg.size_label.text()


def test_dialog_lists_chips_and_waits_for_one(qapp, tmp_path):
    chips = []
    dlg = make_dialog(tmp_path, chips)
    assert dlg.chip_table.rowCount() == 0
    assert not dlg.start_btn.isEnabled() and not dlg.start()   # no chip yet
    assert "칩이 없습니다" in dlg.chip_status.text()

    chips.append(chip())
    dlg.scan()
    row = [dlg.chip_table.item(0, c).text() for c in range(4)]
    assert row == ["1", "CH552", "7D-E2-0F-BD", "2.5.0"]
    assert dlg.start_btn.isEnabled() and dlg.process is None
    assert "ID 7D-E2-0F-BD" in dlg.log.toPlainText()

    chips.append(chip(1, uid=bytes.fromhex("01020304")))
    dlg.scan()
    assert dlg.chip_table.rowCount() == 2
    assert dlg.chip_table.item(1, 2).text() == "01-02-03-04"
    assert not dlg.start_btn.isEnabled()                       # which one is unclear
    assert "2개" in dlg.chip_status.text()


def test_dialog_refuses_other_chip(qapp, tmp_path):
    dlg = make_dialog(tmp_path, [chip(chip_id=0x54)])
    assert dlg.chip_table.item(0, 1).text() == "CH554"
    assert not dlg.start_btn.isEnabled()
    assert "CH554" in dlg.chip_status.text()


def test_dialog_shows_firmware_id_and_board(qapp, tmp_path):
    dlg = make_dialog(tmp_path, [chip()])
    assert "MK6K1N용 (CH552) v1.3.0" in dlg.size_label.text() and "✔" in dlg.size_label.text()
    assert "키 D를 누른 채" in dlg.board_label.text() and "키 D를 누른 채" in dlg.hint.text()
    assert dlg.hex_warning == "" and dlg.start_btn.isEnabled()


def test_dialog_refuses_firmware_for_other_chip(qapp, tmp_path):
    dlg = make_dialog(tmp_path, [chip()], fid="MKFW1:MK9K0N:CH573:1.3.0")
    assert dlg.hex_size == 0 and not dlg.start_btn.isEnabled()
    assert "CH573용 펌웨어" in dlg.size_label.text()


@pytest.mark.parametrize("fid, warning", [
    (None, "식별 정보가 없는"),
    ("MKFW1:MK8K1N:CH552:1.3.0", "다른 키보드(MK8K1N)용"),
])
def test_dialog_asks_before_unchecked_firmware(qapp, tmp_path, fid, warning):
    dlg = make_dialog(tmp_path, [chip()], fid=fid)
    assert warning in dlg.size_label.text() and "⚠" in dlg.size_label.text()
    assert dlg.start_btn.isEnabled()
    dlg.answer = False
    assert not dlg.start() and dlg.process is None           # declined: nothing runs
    assert len(dlg.asked) == 1 and warning in dlg.asked[0]


def test_dialog_unknown_board_flashes_the_picked_board(qapp, tmp_path):
    from gui.core import protocol as p
    dlg = make_dialog(tmp_path, [chip()], keyboard=p.Board(keys=9, knobs=0, rows=3, name="MK9K0N"))
    assert not dlg.identified and "목록에 없는 보드(MK9K0N)" in dlg.board_label.text()
    assert dlg.spec.name == "MK6K1N" and dlg.start_btn.isEnabled()
    dlg.answer = False
    assert not dlg.start() and "식별하지 못했습니다" in dlg.asked[0]


def test_dialog_unidentified_keyboard_picks_board(qapp, tmp_path, monkeypatch):
    from gui.ui import flash_dialog
    monkeypatch.setattr(flash_dialog, "BUILD_DIR", tmp_path)
    make_hex(tmp_path / "macro_keyboard-MK6K1NL.hex", 100, fid="MKFW1:MK6K1NL:CH552:1.5.0")
    dlg = make_dialog(tmp_path, [chip()], identified=False)    # factory firmware, 1.2.x, nothing
    dlg.auto_hex = True
    assert "식별할 수 없습니다" in dlg.board_label.text() and "키 E" in dlg.hint.text()
    dlg.board_combo.setCurrentIndex([b.name for b in boards.BOARDS].index("MK6K1NL"))
    assert dlg.spec.name == "MK6K1NL" and "저항" in dlg.hint.text()
    assert dlg.path_edit.text().endswith("macro_keyboard-MK6K1NL.hex") and dlg.hex_warning == ""
    dlg.answer = False
    assert not dlg.start() and "MK6K1NL" in dlg.asked[0]       # asks once, then flashes on yes


def test_dialog_identified_keyboard_asks_for_another_board(qapp, tmp_path):
    dlg = make_dialog(tmp_path, [chip()])                       # MK6K1N said so in HELLO
    assert dlg.identified and dlg.spec.name == "MK6K1N" and not dlg.other_board
    dlg.board_combo.setCurrentIndex([b.name for b in boards.BOARDS].index("MK6K1NL"))
    assert dlg.other_board and "다른 보드를 골랐습니다" in dlg.board_label.text()
    dlg.answer = False
    assert not dlg.start() and "MK6K1N인데 MK6K1NL" in dlg.asked[0]


def test_dialog_marks_the_keyboard_that_was_connected(qapp, tmp_path):
    from gui.core import protocol as p
    keyboard = p.Board(uid=bytes.fromhex("7DE20FBD"))
    dlg = make_dialog(tmp_path, [chip()], keyboard=keyboard)
    assert dlg.chip_table.item(0, 2).text() == "7D-E2-0F-BD (방금 연결된 키보드)"
    assert "방금 연결되어 있던 키보드" in dlg.chip_status.text()
    other = make_dialog(tmp_path, [chip(uid=bytes.fromhex("01020304"))], keyboard=keyboard)
    assert other.chip_table.item(0, 2).text() == "01-02-03-04" and other.start_btn.isEnabled()


def test_dialog_default_command_uses_board_list(qapp, tmp_path, monkeypatch):
    from gui.ui.flash_dialog import FirmwareDialog
    monkeypatch.setattr(flasher, "find_uploader", lambda: Path("up.exe"))
    dlg = FirmwareDialog(lambda: "", hex_path=make_hex(tmp_path / "fw.hex", 100, fid=FW_ID),
                         scan_fn=lambda: [], auto_scan=False)
    cmd = dlg._command_fn(tmp_path / "fw.hex")
    assert cmd[cmd.index("-t") + 1] == "CH552" and cmd[cmd.index("-c") + 1] == "1"


def test_dialog_boot_button_runs_prepare(qapp, tmp_path):
    calls = []
    dlg = make_dialog(tmp_path, [], prepare=lambda: calls.append(1) or "업데이트 모드로 바꿨습니다")
    dlg.boot_btn.click()
    assert calls == [1] and "업데이트 모드로 바꿨습니다" in dlg.log.toPlainText()
    assert dlg.process is None                                 # still waits for [장입]


def test_dialog_scan_error_shown_once(qapp, tmp_path):
    from gui.ui.flash_dialog import FirmwareDialog

    def broken():
        raise flasher.FlashError("WCH 드라이버가 없습니다")
    dlg = FirmwareDialog(lambda: "", lambda p: ["unused"], scan_fn=broken, auto_scan=False,
                         hex_path=make_hex(tmp_path / "fw.hex", 100))
    dlg.scan()
    assert dlg.log.toPlainText().count("WCH 드라이버가 없습니다") == 1
    assert "WCH 드라이버" in dlg.chip_status.text() and not dlg.start_btn.isEnabled()


def test_main_window_boots_open_device_before_upload(qapp, tmp_path):
    from gui.core import protocol as p
    from gui.ui.app import MainWindow, make_mock_connector
    from gui.core.transport import MockDevice
    mock = MockDevice(capacity=128, storage=p.encode_blob(["secret"] + [""] * 8))
    w = MainWindow(make_mock_connector(mock), auto_reconnect=False)
    w.connect_device()
    hint = w.prepare_bootloader()
    assert mock.in_bootloader and not w.connected
    assert "업데이트 모드로 바꿨습니다" in hint
    assert w.slots == [""] * 9                             # nothing left on screen
    w.close()


def test_main_window_locked_device_needs_key_d(qapp, tmp_path):
    from gui.core import protocol as p
    from gui.ui.app import MainWindow, make_mock_connector
    from gui.core.transport import MockDevice
    mock = MockDevice(capacity=128, storage=p.encode_blob([""] * 9, [0, 1, 2, 3]))
    w = MainWindow(make_mock_connector(mock), auto_reconnect=False)
    w.connect_device()
    hint = w.prepare_bootloader()
    assert not mock.in_bootloader                          # BOOT is refused while locked
    assert "키 D" in hint and "잠겨" in hint
    w.close()


NINE_ID = "MKFW1:3X3V_RGB:CH32V203:1.4.0"


def make_bin(path: Path, size: int, fid: str | None = NINE_ID) -> Path:
    data = (fid.encode() + bytes(1) if fid else b"") + bytes((i * 7) & 0xFF for i in range(size))
    path.write_bytes(data[:size])
    return path


def make_hid_dialog(tmp_path, dev=None, board="3X3V_RGB", path=None, **kw):
    """Update window for the 9-key board with a MockBootloader plugged in (dev=None: none)."""
    from gui.core import hidboot
    from gui.ui.flash_dialog import FirmwareDialog
    nine = boards.find("3X3V_RGB")

    def scan():
        if dev is None:
            return []
        return [hidboot.Bootloader(dev).hello()]

    def flash(image, progress, force=False):
        bl = hidboot.Bootloader(dev)
        bl.flash(image, progress, force)
        bl.run()

    dlg = FirmwareDialog(lambda: "", lambda p: ["unused"], keyboard=nine.layout,
                         hex_path=path or make_bin(tmp_path / "fw.bin", 11928),
                         auto_scan=False, hid_scan_fn=scan, hid_flash_fn=flash, **kw)
    dlg.confirm = lambda title, text: True
    return dlg


def test_dialog_ch32v203_flashes_through_own_bootloader(qapp, tmp_path):
    from gui.core import hidboot
    dev = hidboot.MockBootloader()
    dlg = make_hid_dialog(tmp_path, dev)
    assert dlg.spec.hidboot and dlg.window_flash and dlg.boot_btn.isEnabled()
    assert "키 7을 누른 채" in dlg.hint.text() and "보라색" in dlg.hint.text()
    assert r"tools\flash.ps1 -Board 3x3v_rgb -Full" in dlg.hint.text()   # first install
    assert "✔" in dlg.size_label.text() and "52992" in dlg.size_label.text()
    assert dlg.chip_table.rowCount() == 1 and dlg.chip_table.item(0, 3).text() == "자체 1.0"
    assert dlg.start_btn.isEnabled()

    assert dlg.start() and dlg.succeeded
    image = (tmp_path / "fw.bin").read_bytes()
    assert bytes(dev.flash[:len(image)]) == image and dev.ran
    assert dlg.bar.value() == 100 and "장입 완료" in dlg.stage_label.text()
    assert not dlg.running


def test_dialog_ch32v203_without_bootloader_or_with_hex(qapp, tmp_path):
    dlg = make_hid_dialog(tmp_path, None)
    assert "업데이트 모드인 칩이 없습니다" in dlg.chip_status.text()
    assert not dlg.start_btn.isEnabled()

    dlg.set_hex(make_hex(tmp_path / "fw.hex", 100, fid=NINE_ID))
    assert ".bin" in dlg.size_label.text() and dlg.hex is None

    # picking a CH552 board brings the vnproch55x flow back
    dlg.board_combo.setCurrentIndex(0)
    assert not dlg.spec.hidboot and "[업데이트 모드로 전환]" in dlg.hint.text()


@pytest.mark.parametrize("fid, warning", [
    (None, "식별 정보가 없는"),
    ("MKFW1:2X4V_RGB:CH32V203:1.7.0", "다른 키보드(2X4V_RGB)용"),
])
def test_dialog_ch32v203_wrong_file_needs_forced_consent(qapp, tmp_path, fid, warning):
    from gui.core import hidboot
    dev = hidboot.MockBootloader()
    dlg = make_hid_dialog(tmp_path, dev, path=make_bin(tmp_path / "x.bin", 2000, fid=fid))
    assert warning in dlg.size_label.text() and "강제 동의" in dlg.size_label.text()
    asked = []
    dlg.confirm_force = lambda text: asked.append(text) or False   # [취소]
    assert not dlg.start() and asked and warning in asked[0]
    assert not dev.erased and dev.writes == 0                      # nothing touched

    dlg.confirm_force = lambda text: True                          # [강제 장입]
    assert dlg.start() and dlg.succeeded and dev.ran
    image = (tmp_path / "x.bin").read_bytes()
    assert bytes(dev.flash[:len(image)]) == image


def test_dialog_ch32v203_blocks_reentry_while_flashing(qapp, tmp_path):
    """Clicks and closing during the processEvents of hidboot flashing do nothing."""
    from gui.core import hidboot
    dev = hidboot.MockBootloader()
    seen = []

    def flash(image, progress, force=False):
        bl = hidboot.Bootloader(dev)
        bl.flash(image, progress, force)
        # what a user could do between two progress updates
        dlg.start_btn.click()
        dlg.close_btn.click()
        dlg.close()
        dlg.reject()
        seen.append((dlg.start(), dlg.isVisible(), dlg.start_btn.isEnabled(),
                     dlg.board_combo.isEnabled(), dlg.browse_btn.isEnabled(), dev.writes))
        bl.run()

    dlg = make_hid_dialog(tmp_path, dev)
    dlg._hid_flash_fn = flash
    dlg.show()
    assert dlg.start() and dlg.succeeded
    started, visible, start_on, combo_on, browse_on, writes = seen[0]
    assert (started, visible, start_on, combo_on, browse_on) == (False, True, False, False, False)
    assert dev.ran and writes == dev.writes          # no second flash started
    assert dlg.isVisible() and dlg.start_btn.isEnabled() and dlg.board_combo.isEnabled()
    dlg.close_btn.click()                            # after it: closes normally
    assert not dlg.isVisible()


def test_dialog_ch32v203_right_file_asks_no_force(qapp, tmp_path):
    from gui.core import hidboot
    dlg = make_hid_dialog(tmp_path, hidboot.MockBootloader())
    dlg.confirm_force = lambda text: pytest.fail("asked to force a matching file")
    assert dlg.start() and dlg.succeeded


def test_dialog_ch32v203_other_board_and_flash_error(qapp, tmp_path):
    from gui.core import hidboot
    dlg = make_hid_dialog(tmp_path, hidboot.MockBootloader(board="2X4V_RGB"))
    assert "2X4V_RGB" in dlg.chip_status.text() and not dlg.start_btn.isEnabled()

    dlg = make_hid_dialog(tmp_path, hidboot.MockBootloader(flash_error=True))
    assert not dlg.start() and not dlg.succeeded
    assert "플래시" in dlg.stage_label.text() and "키 7을 누른 채" in dlg.stage_label.text()
    assert dlg.start_btn.isEnabled()                      # try again


@pytest.mark.parametrize("name", [b.name for b in boards.BOARDS])
def test_flash_dialog_lists_every_board(qapp, tmp_path, name):
    """Every board in boards.BOARDS is in the update window's list; the
    connected keyboard's board is picked."""
    dlg = make_dialog(tmp_path, [], keyboard=boards.find(name).layout)
    assert [dlg.board_combo.itemData(i) for i in range(dlg.board_combo.count())] == \
        [b.name for b in boards.BOARDS]
    assert dlg.board_combo.currentData() == name
    dlg.close()
