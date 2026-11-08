"""Quick check of a freshly flashed keyboard (docs/old/PORT_CH32V203.md "확인할 것").

Run from the repo root with the GUI venv:
  uv run python tools\\check_device.py                 # 1, 3, 8: USB, HELLO, board, chip
  uv run python tools\\check_device.py --write         # 4: write a test config (backs up the old one)
  (replug USB)
  uv run python tools\\check_device.py --verify        # 4: test config survived the replug
  uv run python tools\\check_device.py --restore FILE  # put the backup back

--board NAME picks the expected board (default: the board the keyboard's HELLO
names, looked up in gui/core/boards.py; an unknown one fails; none = no
comparison), --long N puts an
N-character string on key 1 (확인할 것 5: press key 1 in Notepad).
"""
from __future__ import annotations

import argparse
import sys
from dataclasses import replace
from datetime import datetime
from pathlib import Path
from typing import Callable

ROOT = Path(__file__).resolve().parents[1]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from gui.core import backup, boards, chips  # noqa: E402
from gui.core import protocol as p  # noqa: E402
from gui.core.device import DeviceClient, DeviceError  # noqa: E402
from gui.core.transport import HidTransport, Transport, TransportError  # noqa: E402


def test_slots(board: p.Board, long_len: int = 0) -> list[str]:
    """Key i types "k<i+1> "; key 1 types a long counted string when long_len > 0."""
    slots = [f"k{i + 1} " for i in range(board.keys)] + [""] * (board.slot_count - board.keys)
    if long_len > 0:
        text = "".join(f"{n:04d} " for n in range(0, long_len, 5))
        slots[0] = text[:long_len]
    return slots


def check(client: DeviceClient, spec: boards.BoardSpec | None, out: Callable[[str], None],
          auto: bool = False) -> bool:
    """``auto``: compare with the boards.py entry of the board HELLO names."""
    ok = True
    info = client.hello()
    b = info.board
    out(f"[OK] HELLO: 펌웨어 v{info.fw_version}, 프로토콜 v{info.protocol_version}, "
        f"슬롯 {info.slot_count}, 용량 {info.capacity}바이트")
    if info.board_info is None:
        out("[!!] 보드 정보 없음 (6키 기본 보드로 간주)")
        ok = False
    out(f"     보드 {b.name}: {b.summary}, {b.rows}줄, 칩 0x{b.chip_id:02X} ({chips.name_of(b.chip_id)}), "
        f"기능 0x{b.features:02X}, UID {b.uid.hex() or '없음'}")
    if auto and info.board_info is not None:
        spec = boards.find(b.name)
        if spec is None:
            out(f"[!!] 모르는 보드: {b.name} (gui/core/boards.py: {', '.join(s.name for s in boards.BOARDS)})")
            ok = False
    if spec is not None:
        exp = spec.layout
        if boards.same(b.name, spec.name) and b.name != spec.name:
            out(f"[--] 옛 보드 이름 {b.name} (지금 이름 {spec.name}): 펌웨어를 새로 올리면 바뀜")
        # an earlier name of the board counts as its name (gui/core/boards.py aliases)
        if replace(b, name=boards.current_name(b.name)) != exp:
            out(f"[!!] 보드가 {spec.name}와 다름 (기대: {exp.summary}, {exp.rows}줄, 칩 0x{exp.chip_id:02X})")
            ok = False
        else:
            out(f"[OK] 보드 배치가 {spec.name}와 같음")
        # boards.py lists what a board needs; newer firmware may add features
        # (CH552 boards report auto-lock since 1.4.0)
        if b.features & exp.features != exp.features:
            out(f"[!!] 기능 0x{b.features:02X}, 기대 0x{exp.features:02X} 이상")
            ok = False
    cfg = client.read_config()
    if cfg.valid:
        used = sum(1 for s in cfg.slots if s)
        out(f"[OK] 설정 읽기: 유효, 채워진 슬롯 {used}개" + (", 패턴 암호 있음" if cfg.lock else ""))
    else:
        out(f"[--] 설정 읽기: 설정 없음 ({cfg.reason or '빈 저장 영역'}) → 기본 동작")
    return ok


def write_test(client: DeviceClient, long_len: int, backup_dir: Path,
               out: Callable[[str], None]) -> bool:
    info = client.hello()
    board = info.board
    old = client.read_config()
    if old.valid:
        path = backup_dir / f"check_backup_{datetime.now():%Y%m%d_%H%M%S}.json"
        backup.save(path, old.slots, board)
        out(f"[OK] 기존 설정 백업: {path}")
    slots = test_slots(board, long_len)
    size = client.write_config(slots)
    out(f"[OK] 시험 설정 저장 ({size}바이트): 키 n → 'kn '" +
        (f", 키 1 → {long_len}자" if long_len else ""))
    return verify(client, long_len, out, reread=True)


def verify(client: DeviceClient, long_len: int, out: Callable[[str], None],
           reread: bool = False) -> bool:
    if not reread:
        client.hello()
    board = client.info.board
    cfg = client.read_config()
    want = test_slots(board, long_len)
    if cfg.valid and cfg.slots == want:
        out("[OK] 읽은 설정이 시험 설정과 같음" + ("" if reread else " (재연결 후 유지)"))
        return True
    out("[!!] 읽은 설정이 시험 설정과 다름" + ("" if cfg.valid else f" ({cfg.reason})"))
    return False


def restore(client: DeviceClient, path: Path, out: Callable[[str], None]) -> bool:
    board = client.hello().board
    slots, skipped = backup.load(path, board)
    if skipped:
        out(f"[--] 이 보드에 없는 키는 건너뜀: {', '.join(skipped)}")
    client.write_config(slots)
    out(f"[OK] 백업 복원: {path}")
    return client.read_config().slots == slots


def main(argv: list[str] | None = None,
         connect: Callable[[], Transport] = HidTransport.open_first,
         out: Callable[[str], None] = print) -> int:
    ap = argparse.ArgumentParser(description="장입 직후 키보드 점검")
    ap.add_argument("--board", default=None,
                    help="기대하는 보드 이름 (기본: 키보드가 알린 보드, none = 비교 안 함)")
    g = ap.add_mutually_exclusive_group()
    g.add_argument("--write", action="store_true", help="시험 설정 저장 (기존 설정은 백업)")
    g.add_argument("--verify", action="store_true", help="재연결 후 시험 설정 확인")
    g.add_argument("--restore", type=Path, metavar="FILE", help="백업 파일로 복원")
    ap.add_argument("--long", type=int, default=0, metavar="N", help="키 1에 N자 문자열 (--write/--verify)")
    ap.add_argument("--backup-dir", type=Path, default=Path.cwd())
    args = ap.parse_args(argv)

    spec = None
    auto = args.board is None
    if not auto and args.board.lower() != "none":
        spec = boards.find(args.board)
        if spec is None:
            out(f"알 수 없는 보드: {args.board} (목록: {', '.join(s.name for s in boards.BOARDS)})")
            return 2
    try:
        transport = connect()
    except TransportError as e:
        out(f"[!!] USB: {e}")
        return 1
    out(f"[OK] USB: 장치 찾음 ({transport.description})")
    client = DeviceClient(transport)
    try:
        if args.write:
            ok = write_test(client, args.long, args.backup_dir, out)
            if ok:
                out("→ USB를 뽑았다 꽂은 뒤 --verify" + (f" --long {args.long}" if args.long else "") +
                    " 실행, 메모장에서 키 입력 확인")
        elif args.verify:
            ok = verify(client, args.long, out)
        elif args.restore:
            ok = restore(client, args.restore, out)
        else:
            ok = check(client, spec, out, auto)
    except (DeviceError, TransportError, backup.BackupError, OSError) as e:
        out(f"[!!] {e}")
        ok = False
    finally:
        client.close()
    out("결과: 통과" if ok else "결과: 문제 있음")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
