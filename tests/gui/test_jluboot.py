"""JieLi UBOOT (gui/core/jluboot.py) and the USB_KEY adapter (gui/core/jl_usbkey.py)
against fakes: nothing is sent to a real device."""
import hashlib

import pytest

from gui.core import jl_usbkey as k
from gui.core import jluboot as j

LOADER = bytes(range(256)) * 5      # 1280 B stand-in for br23loader.bin


def test_crc16_is_xmodem():
    assert j.crc16(b"123456789") == 0x31C3
    assert j.crc16(b"") == 0


def test_mengli_cipher_round_trip():
    data = bytes(range(40))
    assert j.mengli_cipher(j.mengli_cipher(data)) == data != j.mengli_cipher(data)


def test_cdb_layout():
    """2-byte opcode big-endian, address 4 B BE, length 2 B BE, 0xFF padding."""
    c = j.cdb(j.READ_FLASH, j.Uboot._rw_args(0x12345, 0x200))
    assert c == bytes.fromhex("fd05" "00012345" "0200") + b"\xff" * 8 and len(c) == 16


def open_fake(**kw):
    fake = j.FakeUboot(**kw)
    found = j.find_uboot(["\\\\.\\C:", fake.path], open_fn=lambda p: fake if p == fake.path else _no(p))
    assert [f.path for f in found] == [fake.path]
    u, info = j.open_uboot(found[0], open_fn=lambda p: fake, loader=LOADER)
    return fake, u, info


def _no(path):
    raise j.ScsiError(f"{path}: not a disk")


def test_loader_then_chip_info():
    fake, u, info = open_fake()
    # The ROM got the loader in 512-byte pieces at 0x12000 and ran it there
    assert fake.log[:4] == [j.WRITE_MEMORY] * 3 + [j.JUMP_TO_MEMORY]
    assert bytes(fake.ram[0x12000 + i] for i in range(len(LOADER))) == LOADER
    assert u.loader_running and u.chunk == 4096
    assert (info.chip, info.flash_id, info.flash_size, info.chip_key) == ("BR23", 0x854014, 1 << 20, 0x1234)
    assert info.identity == "BR23:854014:1234"
    assert j.identity_problem(info, None) == "" and j.identity_problem(info, info.identity) == ""
    assert "다른 기판" in j.identity_problem(info, "BR23:854014:9999")


def test_flash_commands_need_the_loader():
    u = j.Uboot(j.FakeUboot())
    with pytest.raises(j.UbootError, match="로더"):
        u.read_id()


def test_unknown_chip_refused():
    fake = j.FakeUboot(vendor="BR25")
    with pytest.raises(j.UbootError, match="지원하지 않는 칩"):
        j.open_uboot(j.Found(fake.path, j.inquiry(fake)), open_fn=lambda p: fake, loader=LOADER)
    assert fake.closed


def test_missing_loader_file(tmp_path):
    fake = j.FakeUboot()
    with pytest.raises(j.UbootError, match="로더 파일"):
        j.open_uboot(j.Found(fake.path, j.inquiry(fake)), open_fn=lambda p: fake, sdk=str(tmp_path))
    assert j.loader_path("br23", "X:/sdk").as_posix() == "X:/sdk/cpu/br23/tools/br23loader.bin"


def test_not_uboot_disks_skipped_and_admin_reported():
    usb_stick = j.FakeUboot(product="USB DISK")
    assert j.find_uboot(["a"], open_fn=lambda p: usb_stick) == []

    def denied(path):
        raise j.AdminRequired("denied")
    with pytest.raises(j.AdminRequired, match="관리자"):
        j.find_uboot(["a", "b"], open_fn=denied)


def test_same_disk_twice_found_once():
    fake = j.FakeUboot()
    found = j.find_uboot(["\\\\.\\E:", "\\\\.\\HarddiskVolume7"], open_fn=lambda p: fake)
    assert [f.path for f in found] == ["\\\\.\\E:"]


def test_backup_reads_twice_and_hashes(tmp_path):
    fake, u, info = open_fake(flash_size=64 * 1024)
    seen = []
    b = j.backup(u, info.flash_size, lambda d, t: seen.append((d, t)))
    assert b.data == bytes(fake.flash) and b.sha256 == hashlib.sha256(b.data).hexdigest()
    assert seen[-1] == (2 * info.flash_size, 2 * info.flash_size)
    assert fake.log.count(j.READ_FLASH) == 2 * 64 * 1024 // 4096
    path = j.save_backup(b, tmp_path / "backup", info)
    assert path.read_bytes() == b.data and path.name.startswith("br23_854010_")
    assert path.with_suffix(".sha256").read_text().split()[0] == b.sha256


def test_backup_mismatch_stops():
    fake, u, info = open_fake(flash_size=64 * 1024)
    fake.flaky_reads = 1
    with pytest.raises(j.UbootError, match="두 번 읽은 값이 다릅니다"):
        j.backup(u, info.flash_size)


def test_erase_ranges():
    assert j.erase_ranges(0, 0x10000) == [(0, 0x10000)]
    assert j.erase_ranges(0x1000, 0x10000) == [(0x1000 * i, 0x1000) for i in range(1, 17)]
    assert j.erase_ranges(0, 0x10001) == [(0, 0x10000), (0x10000, 0x1000)]
    assert j.erase_ranges(0x20000, 1) == [(0x20000, 0x1000)]


def test_write_image_erases_writes_and_verifies():
    fake, u, info = open_fake(flash_size=256 * 1024)
    image = bytes((i * 13) & 0xFF for i in range(70000))
    seen = []
    j.write_image(u, 0, image, lambda d, t: seen.append((d, t)))
    assert bytes(fake.flash[:len(image)]) == image
    assert fake.flash[len(image):0x12000] == b"\xff" * (0x12000 - len(image))   # erased to the 4 KiB end
    assert fake.flash[0x12000:0x13000] == b"\xA5" * 0x1000                      # untouched after it
    assert seen[-1] == (2 * len(image), 2 * len(image))
    assert fake.log.count(j.ERASE_FLASH_BLOCK) == 1 and fake.log.count(j.ERASE_FLASH_SECTOR) == 2


def test_write_image_verify_catches_bad_read():
    fake, u, info = open_fake(flash_size=64 * 1024)
    fake.flaky_reads = 1
    with pytest.raises(j.UbootError, match="확인 읽기"):
        j.write_image(u, 0, b"\x00" * 5000)
    with pytest.raises(j.UbootError, match="4 KiB"):
        j.write_image(u, 0x800, b"\x00")


def test_run_app():
    fake, u, info = open_fake()
    u.run_app()
    assert fake.ran_app == 1


def test_wait_uboot_times_out():
    with pytest.raises(j.UbootError, match="나타나지 않았습니다"):
        j.wait_uboot(0, paths=[], open_fn=_no)


# ------------------------------------------------------------------ BD19 (JUZH_3B4) and entry

def test_bd19_loader_backup_and_write(tmp_path):
    """JUZH_3B4 answers "BD19 UBOOT1.00": bd19loader.bin goes to 0x2000."""
    assert j.LOADERS["bd19"][1] == 0x2000 and (j.UBOOT_VID, j.UBOOT_PID) == (0x4C4A, 0x3442)
    fake, u, info = open_fake(vendor="BD19", flash_size=64 * 1024)
    assert bytes(fake.ram[0x2000 + i] for i in range(len(LOADER))) == LOADER
    assert u.loader_running and info.identity.startswith("BD19:")
    b = j.backup(u, info.flash_size)
    assert j.save_backup(b, tmp_path, info).name.startswith("bd19_")
    j.write_image(u, 0, b"\x5A" * 5000)
    assert bytes(fake.flash[:5000]) == b"\x5A" * 5000
    assert j.loader_path("bd19", "X:/sdk").as_posix() == "X:/sdk/cpu/bd19/tools/bd19loader.bin"


class FakeHid:
    def __init__(self, n=65, fail=False):
        self.sent, self.closed, self.n, self.fail = [], False, n, fail

    def write(self, data):
        if self.fail:
            raise OSError("gone")
        self.sent.append(bytes(data))
        return self.n

    def close(self):
        self.closed = True


def test_factory_entry_sends_03_ef_ef():
    hid = FakeHid()
    opened = []
    j.enter_uboot(j.ENTRY_FACTORY, paths=[b"p1", b"p2"], open_path=lambda p: opened.append(p) or hid)
    assert opened == [b"p1"] and hid.closed
    assert hid.sent == [bytes([3, 0xEF, 0xEF]) + bytes(62)] and len(hid.sent[0]) == 65


def test_factory_entry_errors():
    with pytest.raises(j.UbootError, match="514C:8850"):
        j.enter_factory(paths=[])
    bad = FakeHid(fail=True)
    with pytest.raises(j.UbootError, match="보내지 못했습니다"):
        j.enter_factory(paths=[b"p"], open_path=lambda p: bad)
    assert bad.closed
    with pytest.raises(j.UbootError, match="보내지 못했습니다"):
        j.enter_factory(paths=[b"p"], open_path=lambda p: FakeHid(n=-1))


def test_app_entry_uses_boot_command():
    class Client:
        booted = 0

        def reboot_to_bootloader(self):
            self.booted += 1
    c = Client()
    j.enter_uboot(j.ENTRY_APP, client=c)
    assert c.booted == 1
    with pytest.raises(j.UbootError, match="연결"):
        j.enter_uboot(j.ENTRY_APP)
    with pytest.raises(j.UbootError, match="모르는"):
        j.enter_uboot("usbkey")
    assert set(j.ENTRY_METHODS) == {j.ENTRY_FACTORY, j.ENTRY_APP}


# ------------------------------------------------------------------ adapter (on hold)

def test_adapter_hello_power_route():
    m = k.MockAdapter()
    a = k.Adapter(m)
    info = a.hello()
    assert info.name == "JL_USBKEY" and info.version == (0, 1) and len(info.uid) == 12
    a.power(False)
    a.route(True)
    assert a.hello().powered is False and a.hello().routed_to_key is True
    assert a.lines() == (1, 1)


def test_adapter_enter_acks_and_uboot_appears():
    fake = j.FakeUboot()
    disks = []
    m = k.MockAdapter(ack_after=40, on_ack=lambda: disks.append(fake.path))
    r = k.Adapter(m).enter(off_ms=500, timeout_ms=3000)
    assert r == k.EnterResult(True, k.CLOCK_DP, 40, 120)
    assert m.enters == [(500, 3000, 0, k.CLOCK_AUTO)] and not m.routed
    found = j.find_uboot(disks, open_fn=lambda p: fake)
    assert found and found[0].inquiry.product == "UBOOT1.00"


def test_adapter_enter_no_answer():
    m = k.MockAdapter(ack_after=None)
    r = k.Adapter(m).enter(off_ms=0)
    assert not r.acked and r.ms == k.ENTER_TIMEOUT_MS
    assert "배터리" in k.enter_failed_text(r)


def test_adapter_bad_args():
    a = k.Adapter(k.MockAdapter())
    with pytest.raises(ValueError):
        a.enter(order=3)
    with pytest.raises(k.UsbKeyError, match="인수 오류"):
        a.request(k.CMD_POWER, bytes([2]))
    with pytest.raises(k.UsbKeyError, match="알 수 없는 명령"):
        a.request(0x55)


def test_backup_needs_a_size():
    fake, u, info = open_fake(flash_size=64 * 1024, flash_id=0x8540FF)
    assert info.flash_size == 0
    with pytest.raises(j.UbootError, match="용량"):
        j.backup(u, info.flash_size)


@pytest.mark.skipif(__import__("sys").platform != "win32", reason="Windows SPTI")
def test_win32_open_missing_path():
    """No device is touched: a path that does not exist fails to open."""
    with pytest.raises(j.ScsiError) as e:
        j.Win32Scsi(r"\\.\MacroKeyboardNoSuchDisk")
    assert not isinstance(e.value, j.AdminRequired)
