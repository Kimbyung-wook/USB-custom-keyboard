# 요구사항(docs/REQUIREMENTS.md): REQ-GUI-06
"""REQ-GUI-06: the JSON backup holds the strings only. The lock pattern and the
auto-lock minutes never go into the file ([JSON 내보내기] and the export before
[공장 초기화]), and a file carrying them does not change the pattern.
Mock devices only."""
import json

import pytest

pytest.importorskip("PySide6")

from PySide6.QtWidgets import QApplication  # noqa: E402

from gui.core import backup, boards, protocol as p  # noqa: E402
from gui.core.transport import MockDevice  # noqa: E402
from gui.ui.app import MOCK_FW, MainWindow, make_mock_connector  # noqa: E402

LOCK = [1, 0, 1, 0, 2]
LOCK_WORDS = ("lock", "pattern", "autolock", "auto_lock", "password", "패턴", "암호")
LOCKABLE = [s for s in boards.BOARDS if s.layout.features & p.BOARD_FEATURE_LOCK]


@pytest.fixture(scope="module")
def qapp():
    return QApplication.instance() or QApplication([])


def window(qapp, board: p.Board, slots, save_path=""):
    mock = MockDevice(board=board, fw=MOCK_FW,
                      storage=p.encode_blob(slots, LOCK, board, autolock=7))
    w = MainWindow(make_mock_connector(mock), auto_reconnect=False)
    w.mock = mock
    w.ask = lambda t, m: True
    w.choose = lambda t, m, o: 0                     # "JSON으로 저장한 뒤 초기화"
    w.inform = w.error = lambda t, m: None
    w.choose_save_path = lambda: save_path
    w._sleep = lambda s: None
    w.connect_device()
    w.mock.enter_pattern(LOCK)
    w.check_lock()
    assert not w.device_locked and w.lock == LOCK and w.autolock == 7
    return w


def close(w):
    w.baseline = list(w.slots)
    w.close()


def keys_of(node) -> set[str]:
    if isinstance(node, dict):
        return set(node) | set().union(*(keys_of(v) for v in node.values()))
    if isinstance(node, list):
        return set().union(*(keys_of(v) for v in node)) if node else set()
    return set()


def assert_strings_only(path, slots):
    raw = path.read_text(encoding="utf-8")
    data = json.loads(raw)
    names = {k.lower() for k in keys_of(data)}
    assert not names & set(LOCK_WORDS), names
    assert not any(word in raw for word in LOCK_WORDS[3:])
    macros = [e["macro"] for e in data["slots"]]
    assert sorted(m for m in macros if m) == sorted(s for s in slots if s)


def strings(board: p.Board) -> list[str]:
    return [f"text {i}" if i % 2 == 0 else "" for i in range(board.slot_count)]


@pytest.mark.parametrize("spec", LOCKABLE, ids=lambda s: s.name)
def test_export_has_no_pattern(qapp, tmp_path, spec):
    board = spec.layout
    slots = strings(board)
    w = window(qapp, board, slots)
    path = tmp_path / "backup.json"
    assert w.export_json(str(path))
    assert_strings_only(path, slots)
    close(w)


@pytest.mark.parametrize("spec", LOCKABLE, ids=lambda s: s.name)
def test_factory_reset_export_has_no_pattern(qapp, tmp_path, spec):
    board = spec.layout
    slots = strings(board)
    path = tmp_path / "before_reset.json"
    w = window(qapp, board, slots, save_path=str(path))
    assert w.factory_reset()
    assert_strings_only(path, slots)
    close(w)


def test_import_ignores_a_pattern_in_the_file(qapp, tmp_path):
    board = boards.find("SAYO_3X3").layout
    w = window(qapp, board, strings(board))
    path = tmp_path / "with_lock.json"
    data = json.loads(backup.to_json(["new"] + [""] * (board.slot_count - 1), board))
    data.update(lock=[0, 1, 2, 3], autolock=1)       # a hand-edited file
    path.write_text(json.dumps(data), encoding="utf-8")
    assert w.import_json(str(path))
    assert w.slots[0] == "new"
    assert w.lock == LOCK and w.autolock == 7       # the pattern stays as on the device
    close(w)
