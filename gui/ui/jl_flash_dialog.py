"""Firmware update window for JieLi boards (spec.jieli, JUZH_3B4).

Same flow as the other boards, over the keyboard's own USB cable: enter the
ROM's USB download mode (UBOOT disk 4C4A:3442), find it, back up the whole
flash, write, restart. The steps are gui/core/jluboot.py JlFlasher; writing
waits for jluboot.WRITE_READY. Nothing touches a device until a button is
pressed (tests inject fakes).
"""
from __future__ import annotations

from pathlib import Path
from typing import Callable

from PySide6.QtCore import QCoreApplication, Qt, QTimer
from PySide6.QtWidgets import (
    QCheckBox, QComboBox, QDialog, QFileDialog, QHBoxLayout, QLabel, QLineEdit, QPlainTextEdit,
    QProgressBar, QPushButton, QVBoxLayout, QWidget,
)

from ..core import boards
from ..core import jluboot as j

RED, GREEN, GREY, ORANGE = "#c62828", "#2e7d32", "#757575", "#e65100"
BACKUP_DIR = Path.home() / "Documents" / "MacroKeyboard" / "backup"
WAIT_POLL_MS = 1000

EnterApp = Callable[[], str]        # our firmware: BOOT (the main window's prepare_bootloader)
EnterFactory = Callable[[], None]   # factory firmware: 03 EF EF (jluboot.enter_factory)


class JlFlashDialog(QDialog):
    def __init__(self, spec: boards.BoardSpec, *, enter_app: EnterApp | None = None,
                 enter_factory: EnterFactory | None = None, flasher: j.JlFlasher | None = None,
                 is_admin: Callable[[], bool] = j.is_admin,
                 relaunch: Callable[[], bool] = j.relaunch_as_admin,
                 our_firmware: bool = False, backup_dir: Path = BACKUP_DIR,
                 parent: QWidget | None = None):
        super().__init__(parent)
        self.setWindowTitle(f"펌웨어 업데이트 · {spec.name}")
        self.resize(640, 560)
        self.spec = spec
        self._enter_app = enter_app
        self._enter_factory = enter_factory or j.enter_factory
        self._is_admin = is_admin
        self._relaunch = relaunch
        self.flasher = flasher or j.JlFlasher(log=self._append)
        if flasher is not None:
            flasher.log = self._append
        self.busy = False
        self.succeeded = False
        self.relaunched = False
        self._wait_left = 0

        title = QLabel(f"{spec.name} · {spec.description}")
        title.setStyleSheet("font-weight: bold;")
        steps = QLabel("순서: ① UBOOT로 전환 → ② UBOOT 장치(USB 저장장치 4C4A:3442) 찾기 → "
                       "③ 전체 백업 → ④ 펌웨어 쓰기 → ⑤ 새 펌웨어 시작. USB 선 하나로 합니다")
        steps.setWordWrap(True)

        # Admin rights (SPTI)
        self.admin_label = QLabel()
        self.admin_label.setWordWrap(True)
        self.admin_btn = QPushButton("관리자로 다시 실행")
        self.admin_btn.clicked.connect(self.relaunch_admin)
        admin_row = QHBoxLayout()
        admin_row.addWidget(self.admin_label, 1)
        admin_row.addWidget(self.admin_btn)

        # 1. Entry
        self.method_combo = QComboBox()
        for key, text in j.ENTRY_METHODS.items():
            self.method_combo.addItem(text, key)
        self.method_combo.setCurrentIndex(
            self.method_combo.findData(j.ENTRY_APP if our_firmware else j.ENTRY_FACTORY))
        self.method_combo.setToolTip("공장 펌웨어: 처음 설치. 우리 펌웨어: 연결된 키보드를 BOOT 명령으로 전환")
        self.enter_btn = QPushButton("UBOOT로 전환")
        self.enter_btn.clicked.connect(self.enter)
        enter_row = QHBoxLayout()
        enter_row.addWidget(QLabel("① 진입 방법:"))
        enter_row.addWidget(self.method_combo, 1)
        enter_row.addWidget(self.enter_btn)

        # 2. Find
        self.device_label = QLabel("UBOOT 장치: 찾지 않음")
        self.device_label.setWordWrap(True)
        self.find_btn = QPushButton("장치 찾기")
        self.find_btn.clicked.connect(self.find)
        find_row = QHBoxLayout()
        find_row.addWidget(QLabel("②"))
        find_row.addWidget(self.device_label, 1)
        find_row.addWidget(self.find_btn)

        # 3. Backup
        self.backup_check = QCheckBox("쓰기 전에 전체 백업 (두 번 읽어 비교, SHA-256)")
        self.backup_check.setChecked(True)
        self.backup_edit = QLineEdit(str(backup_dir))
        self.backup_btn = QPushButton("백업만")
        self.backup_btn.clicked.connect(self.backup_only)
        backup_row = QHBoxLayout()
        backup_row.addWidget(QLabel("③ 백업 폴더:"))
        backup_row.addWidget(self.backup_edit, 1)
        backup_row.addWidget(self.backup_btn)

        # 4. File
        self.path_edit = QLineEdit()
        self.path_edit.setReadOnly(True)
        self.path_edit.setPlaceholderText("펌웨어 파일을 고르세요")
        self.browse_btn = QPushButton("파일 선택…")
        self.browse_btn.clicked.connect(self.browse)
        file_row = QHBoxLayout()
        file_row.addWidget(QLabel("④ 펌웨어:"))
        file_row.addWidget(self.path_edit, 1)
        file_row.addWidget(self.browse_btn)

        self.stage_label = QLabel(j.STAGE_TEXT[j.STAGE_IDLE])
        self.stage_label.setStyleSheet("font-weight: bold;")
        self.stage_label.setWordWrap(True)
        self.bar = QProgressBar()
        self.bar.setRange(0, 100)
        self.log = QPlainTextEdit()
        self.log.setReadOnly(True)
        self.log.setMaximumBlockCount(500)

        self.start_btn = QPushButton("장입")
        self.start_btn.clicked.connect(self.start)
        self.close_btn = QPushButton("닫기")
        self.close_btn.clicked.connect(self.close_dialog)
        buttons = QHBoxLayout()
        buttons.addStretch(1)
        buttons.addWidget(self.start_btn)
        buttons.addWidget(self.close_btn)

        lay = QVBoxLayout(self)
        lay.addWidget(title)
        lay.addWidget(steps)
        lay.addLayout(admin_row)
        lay.addLayout(enter_row)
        lay.addLayout(find_row)
        lay.addWidget(self.backup_check)
        lay.addLayout(backup_row)
        lay.addLayout(file_row)
        lay.addWidget(self.stage_label)
        lay.addWidget(self.bar)
        lay.addWidget(self.log, 1)
        lay.addLayout(buttons)

        # After [UBOOT로 전환]: look for the disk every second, up to wait_s
        self.wait_timer = QTimer(self)
        self.wait_timer.setInterval(WAIT_POLL_MS)
        self.wait_timer.timeout.connect(self._wait_tick)
        self._refresh()

    # ------------------------------------------------------------ state
    @property
    def admin(self) -> bool:
        return self._is_admin()

    @property
    def method(self) -> str:
        return self.method_combo.currentData()

    def _refresh(self) -> None:
        admin = self.admin
        self.admin_label.setText("✔ 관리자 권한으로 실행 중" if admin else f"⚠ {j.ADMIN_TEXT}")
        self.admin_label.setStyleSheet(f"color: {GREEN if admin else ORANGE};")
        self.admin_btn.setVisible(not admin)
        idle = not self.busy
        for w in (self.method_combo, self.enter_btn, self.browse_btn, self.backup_edit, self.backup_check):
            w.setEnabled(idle)
        self.find_btn.setEnabled(idle and admin)
        self.backup_btn.setEnabled(idle and admin and self.flasher.found is not None)
        ready = j.WRITE_READY and bool(self.path_edit.text()) and self.flasher.found is not None
        self.start_btn.setEnabled(idle and admin and ready)
        self.start_btn.setToolTip("" if j.WRITE_READY else j.WRITE_PENDING_TEXT)
        self.close_btn.setEnabled(idle)

    def _append(self, line: str) -> None:
        self.log.appendPlainText(line)

    def _status(self, text: str, color: str = "") -> None:
        self.stage_label.setText(text)
        self.stage_label.setStyleSheet(f"font-weight: bold;{f' color: {color};' if color else ''}")

    def _fail(self, e: Exception) -> None:
        self._append(f"✖ {e}")
        self._status(f"✖ {e}", RED)

    def _progress(self, done: int, total: int) -> None:
        self.bar.setValue(done * 100 // total if total else 0)
        QCoreApplication.processEvents()

    # ------------------------------------------------------------ admin
    def relaunch_admin(self) -> bool:
        try:
            ok = self._relaunch()
        except OSError as e:
            ok = False
            self._append(f"관리자로 다시 실행하지 못했습니다: {e}")
        if ok:
            self.relaunched = True
            self._append("관리자 권한으로 새 창을 열었습니다. 이 창은 닫아도 됩니다")
        else:
            self._status("관리자 권한으로 실행하지 못했습니다 (사용자 계정 컨트롤에서 취소됨?)", ORANGE)
        return ok

    # ------------------------------------------------------------ 1. enter
    def enter(self) -> bool:
        try:
            if self.method == j.ENTRY_APP and self._enter_app is None:
                raise j.UbootError("우리 펌웨어에 연결되어 있지 않습니다. 메인 창에서 먼저 연결하세요")
            self.flasher.found = None
            self.flasher.set_stage(j.STAGE_ENTER)
            if self.method == j.ENTRY_APP:
                self._append(self._enter_app())     # BOOT through the main window's client
            else:
                self._enter_factory()               # 03 EF EF on the vendor HID
        except j.UbootError as e:
            self._fail(e)
            return False
        self._status("UBOOT로 전환했습니다. 장치가 USB 저장장치로 다시 나타나기를 기다립니다", GREY)
        if self.admin:
            self._wait_left = int(self.flasher.wait_s * 1000 // WAIT_POLL_MS)
            self.wait_timer.start()
        else:
            self._status(f"UBOOT로 전환했습니다. {j.ADMIN_TEXT}", ORANGE)
        self._refresh()
        return True

    def _wait_tick(self) -> None:
        self._wait_left -= 1
        if self.find(quiet=True) or self._wait_left <= 0:
            self.wait_timer.stop()
            if self.flasher.found is None:
                self._status(f"UBOOT 장치가 {self.flasher.wait_s:.0f}초 안에 나타나지 않았습니다. "
                             "진입 방법, 케이블·포트를 확인하고 [UBOOT로 전환]을 다시 누르세요", RED)

    # ------------------------------------------------------------ 2. find
    def find(self, quiet: bool = False) -> bool:
        try:
            found = self.flasher.find()
        except j.AdminRequired as e:
            self._fail(e)
            found = []
        except j.UbootError as e:
            if not quiet:
                self._fail(e)
            found = []
        self.flasher.found = found[0] if found else None
        if found:
            q = found[0].inquiry
            self.device_label.setText(f"UBOOT 장치: {q.vendor} {q.product} ({found[0].path})")
            self.device_label.setStyleSheet(f"color: {GREEN};")
            if q.chip not in j.LOADERS:
                self.device_label.setText(self.device_label.text() + f" · 지원하지 않는 칩 {q.vendor}")
                self.device_label.setStyleSheet(f"color: {RED};")
            elif not quiet or self.flasher.stage == j.STAGE_ENTER:
                self._status("✔ UBOOT 장치를 찾았습니다", GREEN)
        else:
            self.device_label.setText("UBOOT 장치: 없음")
            self.device_label.setStyleSheet(f"color: {GREY};")
        self._refresh()
        return bool(found)

    # ------------------------------------------------------------ 3. backup
    def _run(self, job: Callable[[], None]) -> bool:
        self.busy = True
        self.wait_timer.stop()
        self.bar.setValue(0)
        self._refresh()
        try:
            job()
        except (j.UbootError, OSError) as e:
            self.flasher.close()
            self._fail(e)
            return False
        finally:
            self.busy = False
            self._refresh()
        return True

    def backup_only(self) -> bool:
        def job() -> None:
            path = self.flasher.backup(Path(self.backup_edit.text()), self._progress)
            # The loader resets the chip after 3 s without commands: the next
            # job starts again from [UBOOT로 전환]
            self.flasher.close()
            self.flasher.found = None
            self.device_label.setText("UBOOT 장치: 백업 뒤 다시 시작됨 (쓰려면 다시 전환)")
            self._status(f"✔ 백업 완료: {path}. 쓰려면 [UBOOT로 전환]부터 다시 하세요", GREEN)
        return self._run(job)

    # ------------------------------------------------------------ 4. file
    def browse(self) -> None:
        path, _ = QFileDialog.getOpenFileName(self, "펌웨어 파일", "", "펌웨어 (*.bin *.fw *.ufw);;모든 파일 (*)")
        if path:
            self.set_file(path)

    def set_file(self, path: str | Path) -> None:
        self.path_edit.setText(str(path))
        self._refresh()

    # ------------------------------------------------------------ 5. write and restart
    def start(self) -> bool:
        def job() -> None:
            if not j.WRITE_READY:
                raise j.UbootError(j.WRITE_PENDING_TEXT)
            image = Path(self.path_edit.text()).read_bytes()
            if self.backup_check.isChecked():
                self.flasher.backup(Path(self.backup_edit.text()), self._progress)
            self.flasher.write(image, self._progress)
            self.flasher.restart()
            self.succeeded = True
            self.bar.setValue(100)
            self._status(f"✔ 장입 완료 ({len(image)}바이트). 키보드가 새 펌웨어로 다시 시작합니다", GREEN)
        return self._run(job)

    # ------------------------------------------------------------ closing
    def close_dialog(self) -> None:
        if self.busy:
            return
        self.wait_timer.stop()
        self.flasher.close()
        self.accept() if self.succeeded else self.reject()

    def reject(self) -> None:
        if self.busy:     # backup/write run in this thread with processEvents
            return
        self.wait_timer.stop()
        self.flasher.close()
        super().reject()

    def keyPressEvent(self, event) -> None:
        if event.key() == Qt.Key_Escape and self.busy:
            return
        super().keyPressEvent(event)
