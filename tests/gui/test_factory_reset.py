"""[공장 초기화]: an empty config (no strings, no lock pattern, auto-lock off)
through the usual WRITE/COMMIT, also while locked; then on Bluetooth boards
(HELLO BLE bit) CLEAR_BONDS, after which the keyboard resets and the window
connects again. Mock devices only."""
import json

import pytest

pytest.importorskip("PySide6")

from PySide6.QtWidgets import QApplication  # noqa: E402

from gui.core import boards, protocol as p  # noqa: E402
from gui.core.device import DeviceClient, DeviceError  # noqa: E402
from gui.core.transport import MockDevice, TransportError  # noqa: E402
from gui.ui.app import (MOCK_FW, PAIRING_UNSUPPORTED_TEXT, RESET_LAST_TEXT, RESET_OPTIONS,  # noqa: E402
                        MainWindow, make_mock_connector)

LOCK = [0, 1, 0, 1]
SAYO_2B3 = boards.find("SAYO_2B3")


@pytest.fixture(scope="module")
def qapp():
    return QApplication.instance() or QApplication([])


def window(qapp, board: p.Board, slots, lock=(), autolock=0, pick=1, save_path="", answers=None):
    mock = MockDevice(board=board, fw=MOCK_FW,
                      storage=p.encode_blob(slots, lock, board, autolock=autolock))
    w = MainWindow(make_mock_connector(mock), auto_reconnect=False)
    w.mock = mock
    w.dialogs = []
    w.pick = pick
    w.answers = list(answers) if answers is not None else None   # None: yes to every ask
    w._sleep = lambda s: None

    def ask(t, m):
        w.dialogs.append(("ask", t, m))
        return w.answers.pop(0) if w.answers is not None else True
    w.ask = ask
    w.choose = lambda t, m, o: (w.dialogs.append(("choose", t, m, o)), w.pick)[1]
    w.inform = lambda t, m: w.dialogs.append(("inform", t, m))
    w.error = lambda t, m: w.dialogs.append(("error", t, m))
    w.choose_save_path = lambda: save_path
    return w


def close(w):
    w.baseline = list(w.slots)
    w.close()


def filled(board: p.Board) -> list[str]:
    return [f"secret{i}" if i % 3 != 2 else "" for i in range(board.slot_count)]


def settings_of(board: p.Board):
    lockable = bool(board.features & p.BOARD_FEATURE_LOCK)
    lock = LOCK if lockable else []
    autolock = 5 if lockable and board.features & p.BOARD_FEATURE_AUTO_LOCK else 0
    return lock, autolock


def sent(w) -> list[int]:
    return [r.cmd for r in w.mock.requests]


def assert_factory(w, board):
    stored = w.mock.stored_config()
    assert stored.valid and stored.slots == [""] * board.slot_count
    assert stored.lock == [] and stored.autolock == 0
    assert w.connected and not w.device_locked
    assert w.slots == [""] * board.slot_count and w.lock == [] and w.autolock == 0 and not w.has_unsaved()


def test_only_sayo_2b3_has_the_ble_bit():
    assert SAYO_2B3.bluetooth and SAYO_2B3.layout.features & p.BOARD_FEATURE_BLE
    assert SAYO_2B3.layout.features == 0x1E          # as the firmware's HELLO
    assert not any(s.layout.features & p.BOARD_FEATURE_BLE for s in boards.ALL_BOARDS if s is not SAYO_2B3)


@pytest.mark.parametrize("spec", boards.BOARDS, ids=lambda s: s.name)
def test_reset_unlocked_on_every_board(qapp, spec):
    board = spec.layout
    lock, autolock = settings_of(board)
    slots = filled(board)
    w = window(qapp, board, slots, lock, autolock)
    w.connect_device()
    if lock:
        w.mock.enter_pattern(lock)
        w.check_lock()
    assert w.reset_btn.isEnabled() and w.reset_btn.text() == "공장 초기화"
    assert w.factory_reset()
    kind, title, text, options = [d for d in w.dialogs if d[0] == "choose"][-1]
    assert "공장 상태" in text and "되돌릴 수 없습니다" in text and "JSON" in text
    assert f"키 매핑: {sum(1 for s in slots if s)}개" in text
    assert options == list(RESET_OPTIONS)
    assert ("ask", "공장 초기화 (마지막 확인)", RESET_LAST_TEXT) in w.dialogs      # asked twice
    assert_factory(w, board)
    ble = spec is SAYO_2B3
    assert w.mock.bonds_cleared == (1 if ble else 0)
    assert (p.Cmd.CLEAR_BONDS in sent(w)) == ble
    done = w.dialogs[-1]
    assert done[0] == "inform" and ("페어링도 지웠습니다" in done[2]) == ble
    assert ("블루투스 페어링을 지우고" in text) == ble
    close(w)


def test_ble_order_reset_and_reconnect(qapp):
    """WRITE/COMMIT with the empty config first, CLEAR_BONDS last; the keyboard
    drops off USB after the reply and the window connects again."""
    board = SAYO_2B3.layout
    w = window(qapp, board, filled(board))
    w.connect_device()
    first = w.client
    assert w.factory_reset()
    cmds = sent(w)
    clear = cmds.index(p.Cmd.CLEAR_BONDS)
    assert p.Cmd.COMMIT in cmds[:clear] and p.Cmd.WRITE not in cmds[clear:] and p.Cmd.COMMIT not in cmds[clear:]
    assert cmds[clear + 1] == p.Cmd.HELLO                      # connected again
    assert w.client is not first and first.transport is None    # old client closed
    assert w.connected and w.mock.connected
    close(w)


@pytest.mark.parametrize("spec", [s for s in boards.BOARDS if s.layout.features & p.BOARD_FEATURE_LOCK],
                         ids=lambda s: s.name)
def test_reset_while_locked_reads_nothing(qapp, spec, monkeypatch):
    """Locked: allowed (WRITE/COMMIT and CLEAR_BONDS erase only), but no READ,
    no export and no string on screen; afterwards the keyboard is open and empty."""
    board = spec.layout
    lock, autolock = settings_of(board)
    w = window(qapp, board, filled(board), lock, autolock, pick=0, save_path="should-not-be-used.json")
    w.connect_device()
    assert w.device_locked and w.reset_btn.isEnabled()
    w.choose_save_path = lambda: pytest.fail("no export while locked")
    assert w.factory_reset()
    asks = [d for d in w.dialogs if d[0] == "ask"]
    assert not [d for d in w.dialogs if d[0] == "choose"]               # no JSON offer
    assert "되찾을 수 없습니다" in asks[0][2] and "JSON" not in asks[0][2]
    assert asks[-1][1] == "공장 초기화 (마지막 확인)"
    cmds = sent(w)
    erased = cmds.index(p.Cmd.COMMIT)
    assert p.Cmd.READ not in cmds[:erased]       # only the empty config is read, after reconnecting
    shown = json.dumps([d[2] for d in w.dialogs], ensure_ascii=False)
    assert "secret" not in shown and all("secret" not in s for s in w.slots)
    assert_factory(w, board)
    assert w.mock.bonds_cleared == (1 if spec is SAYO_2B3 else 0)
    close(w)


def test_locked_cancel_leaves_device_locked(qapp):
    for spec in (boards.find("SAYO_3X3"), SAYO_2B3):
        board = spec.layout
        slots = filled(board)
        for answers in ([False], [True, False]):            # first or last question
            w = window(qapp, board, slots, LOCK, answers=answers)
            w.connect_device()
            assert not w.factory_reset()
            stored = w.mock.stored_config()
            assert stored.slots == slots and stored.lock == LOCK and w.device_locked
            assert p.Cmd.WRITE not in sent(w) and w.mock.bonds_cleared == 0
            close(w)


def test_cancel_choice_and_last_question(qapp):
    board = SAYO_2B3.layout
    slots = filled(board)
    for pick, answers in ((2, None), (None, None), (1, [False])):
        w = window(qapp, board, slots, pick=pick, answers=answers)
        w.connect_device()
        assert not w.factory_reset()
        assert w.mock.stored_config().slots == slots and w.mock.bonds_cleared == 0
        close(w)


def test_reset_saves_json_first(qapp, tmp_path):
    board = boards.find("SAYO_2X4").layout
    slots = filled(board)
    path = tmp_path / "before.json"
    w = window(qapp, board, slots, pick=0, save_path=str(path))
    w.connect_device()
    w.set_slot_text(0, "edited on screen")            # the export holds the device's strings
    assert w.factory_reset()
    data = json.dumps(json.loads(path.read_text(encoding="utf-8")))
    assert "secret0" in data and "edited on screen" not in data
    assert "저장하지 않은 화면" in [d for d in w.dialogs if d[0] == "choose"][-1][2]
    assert_factory(w, board)
    close(w)
    w = window(qapp, board, slots, pick=0, save_path="")    # file dialog closed
    w.connect_device()
    assert not w.factory_reset()
    assert w.mock.stored_config().slots == slots
    close(w)


def test_bluetooth_board_on_old_firmware(qapp):
    """SAYO_2B3 whose HELLO has no BLE bit: settings only, CLEAR_BONDS never sent."""
    board = p.Board(keys=6, knobs=0, rows=2, name="SAYO_2B3", chip_id=0x73,
                    features=p.BOARD_FEATURE_LOCK | p.BOARD_FEATURE_BATTERY | p.BOARD_FEATURE_AUTO_LOCK)
    w = window(qapp, board, filled(board))
    w.connect_device()
    assert not w.client.info.supports_ble
    assert w.factory_reset()
    assert PAIRING_UNSUPPORTED_TEXT in [d for d in w.dialogs if d[0] == "choose"][-1][2]
    assert p.Cmd.CLEAR_BONDS not in sent(w)
    assert "페어링은 남아 있습니다" in w.dialogs[-1][2]
    assert_factory(w, board)
    close(w)


def test_clear_bonds_failure_and_no_reconnect(qapp, monkeypatch):
    board = SAYO_2B3.layout
    w = window(qapp, board, filled(board))
    w.connect_device()
    def broken():
        raise DeviceError("no answer")
    monkeypatch.setattr(w.client, "clear_bonds", broken)
    assert not w.factory_reset()
    assert w.dialogs[-1][:2] == ("error", "공장 초기화 실패") and "설정은 지웠지만" in w.dialogs[-1][2]
    assert w.mock.stored_config().slots == [""] * board.slot_count     # settings went first
    close(w)
    # the keyboard does not come back: told to connect by hand
    w = window(qapp, board, filled(board))
    w.connect_device()
    tries = []
    def gone():
        tries.append(1)
        raise TransportError("없음")
    w._connect_fn = gone
    assert w.factory_reset()
    assert w.mock.bonds_cleared == 1 and not w.connected
    assert "[연결]" in w.dialogs[-1][2] and len(tries) == 20
    close(w)


def test_client_clear_bonds():
    ble = MockDevice(board=SAYO_2B3.layout, fw=MOCK_FW)
    c = DeviceClient(ble)
    c.hello()
    c.clear_bonds()
    assert ble.bonds_cleared == 1 and c.transport is None and not ble.connected
    other = MockDevice(board=boards.find("SAYO_3X3").layout, fw=MOCK_FW)
    c = DeviceClient(other)
    c.hello()
    with pytest.raises(DeviceError, match="지원하지 않습니다"):
        c.clear_bonds()
    assert p.Cmd.CLEAR_BONDS not in [r.cmd for r in other.requests]
    # the mock answers BAD_CMD like the firmware on boards without BLE
    r = other._handle(p.Request(p.Cmd.CLEAR_BONDS, 1, b""))
    assert r[2] == p.Status.BAD_CMD and other.bonds_cleared == 0


def test_disconnected(qapp):
    board = boards.find("MK6K1N").layout
    w = window(qapp, board, filled(board))
    assert not w.reset_btn.isEnabled() and not w.factory_reset()
    close(w)
