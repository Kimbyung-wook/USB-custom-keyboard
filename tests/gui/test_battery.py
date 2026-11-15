"""BATTERY (0x07): battery boards (HELLO feature bit 0x04, SAYO_2B3) report
the cell; the window shows it under the firmware line and polls every 10 s."""
import struct

import pytest

from gui.core import boards, protocol as p
from gui.core.device import DeviceClient
from gui.core.transport import MockDevice

SAYO_2B3 = boards.find("SAYO_2B3")
ON_BATTERY = p.Battery(p.BATTERY_HAS_READING, 80, 3980, vdd_mv=3975)
CHARGING = p.Battery(p.BATTERY_HAS_READING | p.BATTERY_ON_USB | p.BATTERY_HAS_REST,
                     95, 4150, 62, 3770, 300, 4160)


def test_result_layout_matches_firmware():
    """src/core/battery.c battery_report: 11 bytes, little-endian."""
    raw = p.encode_battery_result(CHARGING)
    assert len(raw) == 11
    assert raw == bytes([0x13, 95]) + struct.pack("<H", 4150) + bytes([62]) + struct.pack("<HHH", 3770, 300, 4160)
    assert p.parse_battery_result(raw + bytes(50)) == CHARGING
    with pytest.raises(ValueError):
        p.parse_battery_result(raw[:10])


def test_board_answer():
    """Seen on the SAYO_2B3 board (2026-11-15), cable in for 10 s."""
    resp = p.decode_response(bytes.fromhex("875a0003604210000000" "0a00b80d").ljust(64, b"\0"))
    assert (resp.cmd, resp.seq, resp.status) == (p.Cmd.BATTERY, 0x5A, p.Status.OK)
    b = p.parse_battery_result(resp.result)
    assert b == p.Battery(0x03, 96, 4162, 0, 0, 10, 3512)
    assert b.on_usb and not b.has_rest
    assert b.text == "🔌 충전 중 (약 96%, 참고값)" and b.level == ""


@pytest.mark.parametrize("battery,text,level", [
    (ON_BATTERY, "🔋 배터리 80%", ""),
    (p.Battery(p.BATTERY_HAS_READING | p.BATTERY_IS_LOW, 15), "🪫 배터리 15% - 낮음", "low"),
    (p.Battery(p.BATTERY_HAS_READING | p.BATTERY_IS_LOW | p.BATTERY_IS_CRITICAL, 3),
     "🪫 배터리 3% - 매우 낮음, 충전하세요", "critical"),
    (CHARGING, "🔌 충전 중 · 연결 전 62% (5분 전)", ""),
    (p.Battery(p.BATTERY_HAS_READING | p.BATTERY_ON_USB, 97), "🔌 충전 중 (약 97%, 참고값)", ""),
    (p.Battery(p.BATTERY_ON_USB), "🔌 충전 중", ""),
    (p.Battery(), "🔋 배터리 측정 중", ""),
    # On the cable the low flags are about the high live value: no warning colour
    (p.Battery(p.BATTERY_HAS_READING | p.BATTERY_ON_USB | p.BATTERY_IS_LOW, 20), "🔌 충전 중 (약 20%, 참고값)", ""),
])
def test_text(battery, text, level):
    assert battery.text == text and battery.level == level


def test_usb_time_text():
    assert [p.usb_time_text(s) for s in (0, 50, 60, 3590, 3600, 3900, 65535)] == [
        "방금", "방금", "1분 전", "59분 전", "1시간 전", "1시간 5분 전", "18시간 12분 넘게 전"]
    assert "칩 VDD 4160 mV" in CHARGING.detail and "높게" in CHARGING.detail


def battery_mock(**kw) -> MockDevice:
    return MockDevice(board=SAYO_2B3.layout, fw=p.SHORT_LOCK_FIRMWARE, battery=ON_BATTERY, **kw)


def test_client_reads_battery():
    mock = battery_mock()
    c = DeviceClient(mock)
    assert c.hello().supports_battery
    assert c.battery() == ON_BATTERY


def test_no_battery_bit_no_request():
    """Boards without the bit (and old firmware) never get BATTERY."""
    mock = MockDevice(board=boards.find("SAYO_3X3").layout, fw=p.SHORT_LOCK_FIRMWARE)
    c = DeviceClient(mock)
    assert not c.hello().supports_battery
    assert c.battery() is None
    assert p.Cmd.BATTERY not in [r.cmd for r in mock.requests]


def test_bad_cmd_means_no_battery():
    mock = battery_mock()
    c = DeviceClient(mock)
    c.hello()
    mock.board_info = boards.find("SAYO_3X3").layout   # now answers BAD_CMD
    assert c.battery() is None


def test_answers_while_locked():
    from gui.core.transport import LOCK_LOCKED
    mock = battery_mock(storage=p.encode_blob([""] * 6, [0, 1], SAYO_2B3.layout))
    c = DeviceClient(mock)
    assert c.hello().locked and mock.lock_state == LOCK_LOCKED
    assert c.battery() == ON_BATTERY


# ------------------------------------------------------------------ window

pytest.importorskip("PySide6")


@pytest.fixture(scope="module")
def qapp():
    from PySide6.QtWidgets import QApplication
    return QApplication.instance() or QApplication([])


def window(mock):
    from gui.ui.app import MainWindow, make_mock_connector
    w = MainWindow(make_mock_connector(mock), auto_reconnect=False)
    w.ask = lambda t, m: True
    w.inform = w.error = lambda t, m: None
    return w


def test_window_shows_and_polls_battery(qapp):
    from gui.ui import app
    mock = battery_mock()
    w = window(mock)
    assert w.connect_device()
    label = w.conn_label.text()
    assert "🔋 배터리 80%" in label and "3980 mV" in w.conn_label.toolTip()
    assert w._battery_timer.isActive() and w._battery_timer.interval() == app.BATTERY_INTERVAL_MS == 10000
    mock.battery = p.Battery(p.BATTERY_HAS_READING | p.BATTERY_IS_CRITICAL | p.BATTERY_IS_LOW, 4)
    w._battery_tick()
    assert "#c62828" in w.conn_label.text() and "4% - 매우 낮음" in w.conn_label.text()
    mock.battery = CHARGING
    w.refresh_status()
    assert "충전 중 · 연결 전 62% (5분 전)" in w.conn_label.text()
    w.disconnect_device()
    assert not w._battery_timer.isActive() and "배터리" not in w.conn_label.text()
    w.close()


def test_window_without_battery(qapp):
    mock = MockDevice(board=boards.find("SAYO_3X3").layout, fw=p.SHORT_LOCK_FIRMWARE)
    w = window(mock)
    assert w.connect_device()
    assert not w._battery_timer.isActive() and w.battery is None
    assert "배터리" not in w.conn_label.text() and "충전" not in w.conn_label.text()
    assert p.Cmd.BATTERY not in [r.cmd for r in mock.requests]
    w.close()


def test_window_battery_unplugged(qapp):
    """The keyboard gone between ticks: disconnected, timer stopped."""
    mock = battery_mock()
    w = window(mock)
    assert w.connect_device()
    mock.connected = False
    w._battery_tick()
    assert not w.connected and not w._battery_timer.isActive()
    w.close()
