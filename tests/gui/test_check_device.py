"""tools/check_device.py against the mock keyboard."""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))

import check_device as cd  # noqa: E402

from gui.core import boards  # noqa: E402
from gui.core import protocol as p  # noqa: E402
from gui.core.transport import MockDevice, TransportError  # noqa: E402

NINE = boards.find("3X3V_RGB").layout


def run(dev, *args):
    lines = []
    code = cd.main(list(args), connect=lambda: dev, out=lines.append)
    return code, "\n".join(lines)


def test_check_passes_for_expected_board():
    code, text = run(MockDevice(capacity=4096, board=NINE))
    assert code == 0, text
    assert "3X3V_RGB와 같음" in text and "0x19" in text


def test_check_flags_wrong_board():
    code, text = run(MockDevice(board=p.DEFAULT_BOARD), "--board", "3X3V_RGB")   # named board wins
    assert code == 1 and "다름" in text


def test_check_compares_with_the_board_hello_names():
    for spec in boards.BOARDS:
        code, text = run(MockDevice(capacity=4096 if spec.hidboot else 128, board=spec.layout))
        assert code == 0 and f"{spec.name}와 같음" in text, text


def test_check_fails_for_an_unknown_board():
    other = p.Board(keys=4, knobs=0, rows=1, name="MK4K", chip_id=0x52)
    code, text = run(MockDevice(board=other))
    assert code == 1 and "모르는 보드: MK4K" in text


def test_check_flags_missing_features():
    nine = p.Board(keys=9, knobs=0, rows=3, name="3X3V_RGB", chip_id=0x19, features=p.BOARD_FEATURE_LOCK)
    code, text = run(MockDevice(capacity=4096, board=nine))
    assert code == 1 and "기능" in text


def test_check_flags_missing_board_info():
    code, text = run(MockDevice(), "--board", "none")
    assert code == 1 and "보드 정보 없음" in text


def test_no_device():
    def fail():
        raise TransportError("키보드를 찾을 수 없습니다")
    lines = []
    assert cd.main([], connect=fail, out=lines.append) == 1
    assert "찾을 수 없습니다" in lines[0]


def test_write_verify_restore(tmp_path):
    dev = MockDevice(capacity=4096, board=NINE)
    code, text = run(dev, "--write", "--long", "300", "--backup-dir", str(tmp_path))
    assert code == 0, text
    assert "--verify --long 300" in text
    code, text = run(dev, "--verify", "--long", "300")
    assert code == 0 and "재연결 후 유지" in text
    code, _ = run(dev, "--verify")          # without --long the key 1 text differs
    assert code == 1

    # a second write backs up the test config, and restoring brings it back
    run(dev, "--write", "--backup-dir", str(tmp_path))
    saved = sorted(tmp_path.glob("check_backup_*.json"))
    assert saved
    code, text = run(dev, "--restore", str(saved[-1]))
    assert code == 0, text
    code, _ = run(dev, "--verify", "--long", "300")
    assert code == 0


def test_write_too_long_for_capacity(tmp_path):
    # 4096 - header 8 - 9 lengths x2 = 4070 chars at most, the other keys add "kN "
    code, text = run(MockDevice(capacity=4096, board=NINE), "--write", "--long", "4071",
                     "--backup-dir", str(tmp_path))
    assert code == 1 and "결과: 문제 있음" in text
    # Older firmware with the 128-byte blob
    code, text = run(MockDevice(capacity=128, board=NINE), "--write", "--long", "300",
                     "--backup-dir", str(tmp_path))
    assert code == 1 and "결과: 문제 있음" in text


def test_long_string_length():
    slots = cd.test_slots(NINE, 300)
    assert len(slots) == NINE.slot_count and len(slots[0]) == 300
    assert slots[1] == "k2 "
