"""Build firmware storage/macro code with gcc and check it against the GUI encoder.

Run from the repo root:  uv run python tests\\host\\run.py
"""
from __future__ import annotations

import re
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
SRC = ROOT / "firmware" / "macro_keyboard" / "src"
sys.path.insert(0, str(ROOT))

from gui.core import protocol as p  # noqa: E402

SHIFT, CTRL, ALT, WIN = 0x02, 0x01, 0x04, 0x08


def keys(*pairs: tuple[int, int] | str) -> str:
    """Expected report string: (mods, key) pairs, or 'D50'-style delay markers."""
    out = []
    for item in pairs:
        out.append(item if isinstance(item, str) else f"{item[0]:02x}:{item[1]:02x}")
    return " ".join(out)


def build(out_dir: Path, board: str = "MK6K1N", header: Path | None = None) -> Path:
    """The firmware's functions against the host HAL (stub/hal_host.h); with
    ``header`` a board file outside src/boards (src/config.h BOARD_HEADER)."""
    exe = out_dir / f"host_test_{board}.exe"
    pick = f'-DBOARD_HEADER="{header.as_posix()}"' if header else f"-DBOARD_{board}"
    subprocess.run(
        ["gcc", "-std=c99", "-Wall", "-DHAL_HOST", pick,
         "-I", str(HERE / "stub"), "-I", str(SRC),
         str(HERE / "host_test.c")] + [str(SRC / "core" / f) for f in
                                       ("storage.c", "macro.c", "lock.c", "input.c", "led.c", "protocol.c",
                                        "battery.c")]
        + ["-o", str(exe)],
        check=True)
    return exe


def run(exe: Path, out_dir: Path, blob: bytes, capslock: bool = False,
        extra: list[str] | None = None) -> list[str]:
    path = out_dir / "blob.bin"
    path.write_bytes(blob)
    args = [str(exe), str(path)] + (["caps"] if capslock else []) + (extra or [])
    return subprocess.run(args, check=True, capture_output=True, text=True).stdout.splitlines()


def lock_states(lines: list[str]) -> list[str]:
    """'lock locked flags 3' lines -> ['locked 3', ...]"""
    return [" ".join(line.split()[1::2]) for line in lines if line.startswith("lock ")]


def slot_lines(lines: list[str]) -> dict[int, str]:
    result = {}
    for line in lines:
        if line.startswith("slot "):
            head, _, rest = line.partition(":")
            result[int(head.split()[1])] = rest.strip()
    return result


failures = 0


def check(name: str, got: str, want: str) -> None:
    global failures
    ok = got == want
    failures += not ok
    print(f"{'PASS' if ok else 'FAIL'} {name}" + ("" if ok else f"\n   want: {want}\n   got:  {got}"))


def board_names() -> list[str]:
    """BOARD_NAME of every board file (src/boards/*.h; shared pin files have none)."""
    names = []
    for path in sorted((SRC / "boards").glob("*.h")):
        m = re.search(r'^#define\s+BOARD_NAME\s+"([^"]+)"', path.read_text(encoding="utf-8"), re.M)
        if m:
            names.append(m.group(1))
    return names


TEST_BOARDS = HERE / "boards"   # boards not identified yet (not in src/boards)


def test_board(header: Path) -> tuple[str, p.Board, int]:
    """(name, layout, config capacity) of a tests/host/boards header."""
    sys.path.insert(0, str(ROOT / "tools"))
    import repo_facts
    d = repo_facts.defines(header)
    name = d["BOARD_NAME"].strip('"')
    layout = p.Board(keys=int(d["BOARD_KEYS"]), knobs=int(d["BOARD_KNOBS"]), rows=int(d["BOARD_ROWS"]),
                     name=name, chip_id=repo_facts.number(d["BOARD_CHIP_ID"]))
    return name, layout, 4096 if d.get("BOARD_HAL_CH32V20X") == "1" else 128


# Power-up wheel turning around the keys: key numbers clockwise from key 1,
# written out here so a wrong BOARD_LED_RING in the board file is caught
LED_RING = {"SAYO_3X3": (1, 2, 3, 6, 9, 8, 7, 4), "SAYO_2X4": (1, 2, 3, 4, 8, 7, 6, 5),
            "SAYO_2X6": (1, 2, 3, 4, 5, 6, 12, 11, 10, 9, 8, 7),
            "MK6K1NL": (1, 2, 3, 6, 5, 4), "TEST_RNG": (1, 2, 3, 6, 5, 4)}
# BOARD_LED_RING_CHASE: (ms per lap of a light, lights around the ring)
LED_CHASE = {"SAYO_3X3": (1000, 1), "SAYO_2X4": (1000, 1), "SAYO_2X6": (1000, 2), "MK6K1NL": (1000, 1)}
WHEEL_MS = 3000


def wheel_colors(text: str) -> list[str]:
    """The wheel as "C<r>.<g>.<b>": all LEDs together, or else key 1's LED."""
    return ["C" + c for c in re.findall(r"(?:\bC|\bK0:)(\d+\.\d+\.\d+)", text)]


def wheel_rgb(t: int) -> str:
    """led.c wheel(): the color at t ms within a turn."""
    seg, rest = divmod(t, WHEEL_MS // 6)
    up = rest * 40 // (WHEEL_MS // 6)
    down = 40 - up
    return "%d.%d.%d" % [(40, up, 0), (down, 40, 0), (0, 40, up), (0, down, 40), (up, 0, 40), (40, 0, down)][seg]


def check_chase(exe: Path, out: Path, blob: bytes, name: str, key_count: int) -> None:
    """Every LED at every ms of the test: each light fades from one ring LED to the next."""
    ring = LED_RING[name]
    lap, lights = LED_CHASE[name]
    step = lap // len(ring)
    gap = len(ring) // lights

    def dimmed(rgb: str, level: int) -> str:
        return ".".join(str(int(c) * level >> 5) for c in rgb.split("."))

    # now = 1 after "start"; the last wheel frame is at 5992 ms (one frame per 8 ms)
    lines = run(exe, out, blob, extra=["input", *["w1"] * 5998])[3:]
    colors: dict[int, str] = {}
    at: dict[int, dict[int, str]] = {}
    laps: list[int] = []
    wrong = []
    for i, line in enumerate(lines):
        colors.update((int(k), c) for k, c in re.findall(r"\bK(\d+):(\d+\.\d+\.\d+)", line))
        now = i + 1
        frame = now & ~7 if now >= 8 else 1
        rgb = wheel_rgb(frame % WHEEL_MS)
        cur, frac = divmod(frame % lap, step)
        cur %= len(ring)                                        # 1000 // 6: the lap's last ms
        level = frac * (8192 // step) >> 8
        want = {k: rgb for k in range(key_count)}               # keys off the ring: always lit
        want.update({key - 1: "0.0.0" for key in ring})
        for j in range(lights):
            c = cur + j * gap
            want[ring[c % len(ring)] - 1] = dimmed(rgb, 32 - level)
            want[ring[(c + 1) % len(ring)] - 1] = dimmed(rgb, level)
        if colors != want:
            wrong.append(now)
        at[now] = dict(colors)
        bright = max(range(len(ring)), key=lambda p: sum(int(c) for c in colors[ring[p] - 1].split("."))) % gap
        if not laps or laps[-1] != bright:
            laps.append(bright)
    check(f"{name}: {lights} light(s) fade between ring LEDs, the others off, at every ms", wrong[:5], [])
    check(f"{name}: the brightest LED steps clockwise", laps, [p % gap for p in range(len(laps))])
    check(f"{name}: 6 laps in 6 s", len(laps) - len(ring) * 6 in (0, 1), True)
    # End of a lap: last position -> first, through the last frame before the boundary
    first, second, last = ring[0] - 1, ring[1] - 1, ring[-1] - 1
    before = at[992]
    check(f"{name}: lap end: last and first LED both lit, the next one off",
          (before[last] != "0.0.0", before[first] != "0.0.0", before[second]), (True, True, "0.0.0"))
    check(f"{name}: lap boundary: first LED full, next and last off",
          (at[1000][first], at[1000][second], at[1000][last]), (wheel_rgb(1000), "0.0.0", "0.0.0"))


def check_rainbow(exe: Path, out: Path, blob: bytes, name: str, key_count: int) -> None:
    """Every LED's color at known times: red moves one key clockwise per 1/8 turn."""
    ring = LED_RING[name]
    step = WHEEL_MS // len(ring)
    # now = 1 after "start"; then every 1/8 turn for a whole turn, and a time between steps
    waits = [step - 1] + [step] * len(ring) + [1234]
    lines = run(exe, out, blob, extra=["input", *[f"w{w}" for w in waits]])[3:]
    colors: dict[int, str] = {}
    now = 1
    for i, line in enumerate(lines):
        colors.update((int(k), c) for k, c in re.findall(r"\bK(\d+):(\d+\.\d+\.\d+)", line))
        if i == 0:
            continue
        now += waits[i - 1]
        t = now % WHEEL_MS
        want = {k: wheel_rgb(t) for k in range(key_count)}     # keys off the ring: plain wheel
        for p, key in enumerate(ring):
            want[key - 1] = wheel_rgb((t - p * step) % WHEEL_MS)
        check(f"{name}: wheel around the keys at {now} ms", colors, want)
        if i < len(ring):
            check(f"{name}: red on key {ring[i]} at {now} ms", colors[ring[i] - 1], "40.0.0")


def check_ring(exe: Path, out: Path, blob: bytes, name: str, key_count: int) -> None:
    (check_chase if name in LED_CHASE else check_rainbow)(exe, out, blob, name, key_count)
    if name == "MK6K1NL":    # WS2812: a frame keeps interrupts off; one only after a color change
        frames = " ".join(run(exe, out, blob, extra=["input", "w6000"])[3:]).split().count("F")
        check(f"{name}: at most a frame per 8 ms", 400 < frames <= 751, True)

    text = run(exe, out, blob, extra=["input", "w1000", "p0", "w30", "r0", "w300"])[3:]
    during = " ".join(text)
    check(f"{name}: a key types while the wheel turns, no key LED frame",
          (" ".join(re.findall(r"\b[0-9a-f]{2}:[0-9a-f]{2}\b", during)), re.findall(r"\bL\d", during)),
          (keys((0, 0x04)), []))
    check(f"{name}: the wheel keeps turning while the key is down",
          len(re.findall(r"\bK\d+:", text[3])) > 0, True)  # the "w30" step


def check_board(out: Path, name: str, header: Path | None = None) -> None:
    """HELLO, READ, WRITE, COMMIT through the firmware's protocol.c, parsed by
    gui.core.protocol and compared with gui.core.boards (or, for a test board,
    its header); keys, the lock pattern and LEDs."""
    from gui.core import boards, version
    if header:
        name, layout, capacity = test_board(header)
    else:
        spec = boards.find(name)
        check(f"{name}: board file known to gui/core/boards.py", spec is not None, True)
        if spec is None:
            return
        layout, capacity = spec.layout, 4096 if spec.hidboot else 128
    exe = build(out, name, header)
    letters = [chr(ord("a") + i) for i in range(layout.slot_count)]
    old = p.encode_blob(letters, board=layout)
    new = p.encode_blob(["x" * 70] + letters[1:], board=layout)  # more than one chunk
    reqs = [p.encode_request(p.Cmd.HELLO, 1),
            p.encode_request(p.Cmd.READ, 2, p.read_args(0, p.MAX_CHUNK))]
    chunks = range(0, len(new), p.MAX_CHUNK)
    reqs += [p.encode_request(p.Cmd.WRITE, 3, p.write_args(o, new[o:o + p.MAX_CHUNK])) for o in chunks]
    reqs.append(p.encode_request(p.Cmd.COMMIT, 4, p.commit_args(len(new), p.crc16_ccitt_false(new[8:]))))
    reqs += [p.encode_request(p.Cmd.READ, 5, p.read_args(o, min(p.MAX_CHUNK, len(new) - o))) for o in chunks]
    lines = [line.split() for line in run(exe, out, old, extra=["proto"] + [r.hex() for r in reqs])
             if line.startswith("res ")]
    res = [p.decode_response(bytes.fromhex(line[1])) for line in lines]
    events = [int(line[3]) for line in lines]
    check(f"{name}: every request answered OK, cmd and seq echoed",
          [(r.cmd, r.seq, r.status) for r in res], [(q[0], q[1], 0) for q in reqs])

    hello = p.parse_hello_result(res[0].result)
    check(f"{name}: HELLO version = gui/core/version.py",
          f"{hello.fw_major}.{hello.fw_minor}.{hello.fw_patch}", version.VERSION)
    check(f"{name}: HELLO protocol, slots, capacity",
          (hello.protocol_version, hello.slot_count, hello.capacity),
          (p.PROTOCOL_VERSION, layout.slot_count, capacity))
    board = hello.board_info
    check(f"{name}: HELLO board info = gui/core/boards.py",
          (board, board and board.name, board and board.chip_id), (layout, name, layout.chip_id))
    check(f"{name}: HELLO features cover boards.py, with auto-lock, UID",
          (board and board.features & layout.features == layout.features,
           hello.supports_autolock, board and board.uid),
          (True, True, bytes([0xA0, 0xA1, 0xA2, 0xA3])))
    # CMD_BATTERY (0x07): answered on a battery board (HELLO feature 0x04),
    # ST_BAD_CMD elsewhere, as from firmware older than the command
    bat = [bytes.fromhex(line.split()[1]) for line in run(exe, out, old, extra=["proto", "0706"])
           if line.startswith("res ")]
    has_bat = bool(board and board.features & 0x04)
    check(f"{name}: BATTERY answered only with the battery feature bit",
          (bat[0][0], bat[0][1], bat[0][2]), (0x87, 6, 0 if has_bat else 1))
    # 56 bytes: the blob, then erased bytes
    check(f"{name}: READ returns the blob", p.parse_read_result(res[1].result),
          (0, old.ljust(p.MAX_CHUNK, b"\xff")[:p.MAX_CHUNK]))
    check(f"{name}: COMMIT reports a saved config", events[2 + len(chunks)], 2)  # PROTO_EVT_SAVED
    check(f"{name}: READ after COMMIT returns the new blob",
          b"".join(p.parse_read_result(r.result)[1] for r in res[3 + len(chunks):]), new)

    blob = p.encode_blob(letters, board=layout)
    for i in range(layout.keys):
        text = " ".join(run(exe, out, blob, extra=["input", f"p{i}", "w30", f"r{i}", "w300"])[3:])
        check(f"{name}: key {i + 1} types slot {i}",
              " ".join(re.findall(r"\b[0-9a-f]{2}:[0-9a-f]{2}\b", text)), keys((0, 0x04 + i)))
    # A board without LED pins yet (no BOARD_FEATURE_RGB_LED, e.g. SAYO_2X6
    # before its LED test) has no power-up wheel or key LEDs to check
    led = bool(layout.features & p.BOARD_FEATURE_RGB_LED)
    if led:
        text = " ".join(run(exe, out, blob, extra=["input", "w6500"])[3:])
        wheel = wheel_colors(text)
        check(f"{name}: color wheel 2 rounds, then off, then blue",
              (sum(1 for a, b in zip([""] + wheel, wheel) if b == "C40.0.0" and a != b),
               re.findall(r"L\d\.[0-9a-f]{2,3}\.\d", text)), (2, ["L0.00.0", "L1.00.0"]))
    if name in LED_RING:
        check_ring(exe, out, blob, name, layout.keys)

    if layout.knobs:
        return
    # No knob: holding any key starts the pattern entry. Pattern: last key, first key
    last = layout.keys - 1
    locked = p.encode_blob(letters, [last, 0], board=layout)
    unlock = ("p1", "w2100", "r1", "w50", f"p{last}", "w30", f"r{last}", "w30", "p0", "w30", "r0", "w300")

    def typed(*steps: str) -> str:
        text = " ".join(run(exe, out, locked, extra=["input", *steps])[3:])
        return " ".join(re.findall(r"\b[0-9a-f]{2}:[0-9a-f]{2}\b", text))
    check(f"{name}: locked keys type nothing", typed("p2", "w30", "r2", f"p{last}", "w30", f"r{last}", "w300"), "")
    check(f"{name}: hold + pattern (key {last + 1}, key 1) opens, then key {last + 1} types",
          typed(*unlock, f"p{last}", "w30", f"r{last}", "w300"), keys((0, 0x04 + last)))
    check(f"{name}: wrong pattern stays locked",
          typed("p1", "w2100", "r1", "w50", "p0", "w30", "r0", "w30", f"p{last}", "w30", f"r{last}", "w300",
                "p2", "w30", "r2", "w300"), "")
    if not led:
        return
    # the key LED of the pressed key only (mask of 12 keys is 3 hex digits)
    text = " ".join(run(exe, out, blob, extra=["input", "w6500", f"p{last}", "w30", f"r{last}", "w300"])[3:])
    check(f"{name}: key {last + 1} lights only its LED",
          re.findall(r"L\d\.[0-9a-f]{2,3}\.\d", text)[2:],
          [f"L0.{1 << last:02x}.2", "L1.00.0"])


BAT_CURVE = [(3000, 0), (3450, 5), (3600, 10), (3670, 20), (3710, 30), (3750, 40), (3800, 50),
             (3860, 60), (3920, 70), (4000, 80), (4100, 90), (4200, 100)]


def bat_percent(v: int) -> int:
    """Li-Po curve of src/core/battery.c: linear between points, rounded down."""
    if v <= BAT_CURVE[0][0]:
        return 0
    for (v0, p0), (v1, p1) in zip(BAT_CURVE, BAT_CURVE[1:]):
        if v < v1:
            return p0 + (v - v0) * (p1 - p0) // (v1 - v0)
    return 100


def check_battery(out: Path) -> None:
    """src/core/battery.c (SAYO_2B3 battery estimate) against a Python model:
    the curve, smoothing, offset, low/critical flags with hysteresis."""
    header = out / "test_battery.h"
    header.write_text('#define BOARD_NAME "TEST_BAT"\n#define BOARD_BATTERY 1\n')
    exe = out / "battery_test.exe"
    subprocess.run(["gcc", "-std=c99", "-Wall", "-Werror", f'-DBOARD_HEADER="{header.as_posix()}"',
                    "-I", str(SRC), str(HERE / "battery_test.c"), str(SRC / "core" / "battery.c"),
                    "-o", str(exe)], check=True)
    curve = subprocess.run([str(exe), "curve"], capture_output=True, text=True, check=True).stdout
    want = "".join(f"{v} {bat_percent(v)}\n" for v in range(2900, 4301, 10))
    check("battery: Li-Po curve 3.0 V = 0 %, 4.2 V = 100 %, monotonic", curve, want)
    check("battery: curve ends", (bat_percent(3000), bat_percent(4200), bat_percent(3800)), (0, 100, 50))

    # VDD readings: full, slow fall through low (3400) and critical (3100), back up
    vdd = [3517] + list(range(3500, 2300, -40)) + list(range(2300, 3100, 40))
    mv, low, crit, lines = 0, 0, 0, []
    for r in vdd:
        v = min(max(r + 650, 0), 6000)
        mv = v if mv == 0 else mv + int((v - mv) / 4)  # C division truncates toward 0
        low = (mv <= 3400 + 100) if low else (mv < 3400)
        crit = (mv <= 3100 + 100) if crit else (mv < 3100)
        lines.append(f"{mv} {bat_percent(mv)} {int(low)} {int(crit)}")
    got = subprocess.run([str(exe)] + [str(r) for r in vdd], capture_output=True, text=True,
                         check=True).stdout.splitlines()
    check("battery: smoothing, +650 mV offset, low/critical hysteresis", got, lines)
    check("battery: first reading 3517 mV = 4167 mV battery", got[0], f"4167 {bat_percent(4167)} 0 0")
    flags = [ln.split()[2:] for ln in got]
    check("battery: low and critical both set and cleared", (["1", "1"] in flags, flags[-1]),
          (True, ["0", "0"]))


BT_LED_WANT = {  # bits: 01 white, 02 green, 04 red, 08/10/20 blue slot 1/2/3
    "pair_0": "10", "pair_1100": "00", "pair_1200": "10",
    "reconn_2500": "10", "reconn_3500": "00", "reconn_4000": "10",
    "conn_7999": "10", "conn_8000": "00",
    "key_9029": "01", "key_9030": "00", "key_off": "00",
    "dongle_2999": "02", "dongle_3000": "00", "usb_60000": "02",
    "combo_fires": "01", "batt80": "38", "batt50": "18", "batt10": "08",
    "batt_end": "00", "batt_end2": "08",
    "low_0": "04", "low_49": "04", "low_50": "00", "low_5000": "04",
    "crit_6000": "04", "crit_6100": "00", "crit_6200": "04", "crit_6599": "00",
    "sleep_before": "00", "crit_6600": "00", "sleep_after": "01",
}


def check_bt_led(out: Path) -> None:
    """src/core/bt_led.c (SAYO_2B3 status LEDs): blink timings, slots, dongle/USB green,
    battery check (key 1 + 6), low/critical red, the 3-LED limit."""
    header = out / "test_bt_led.h"
    header.write_text('#define BOARD_NAME "TEST_LED"\n#define BOARD_BT_LED 1\n')
    exe = out / "bt_led_test.exe"
    subprocess.run(["gcc", "-std=c99", "-Wall", "-Werror", f'-DBOARD_HEADER="{header.as_posix()}"',
                    "-I", str(SRC), str(HERE / "bt_led_test.c"), str(SRC / "core" / "bt_led.c"),
                    "-o", str(exe)], check=True)
    got = dict(ln.split() for ln in subprocess.run([str(exe)], capture_output=True, text=True,
                                                   check=True).stdout.splitlines())
    for group in ("pair", "reconn", "conn", "key", "dongle", "usb", "combo", "batt", "low", "crit", "sleep"):
        names = [n for n in BT_LED_WANT if n.startswith(group + "_") or n == group]
        check(f"bt_led: {group}", {n: got.get(n) for n in names}, {n: BT_LED_WANT[n] for n in names})


BT_APP_WANT = {  # "<LED bits> <sleep>" or a plain value; bits as BT_LED_WANT
    0: {"usb_off": "00 0", "usb_on": "02 0", "key_down": "03 0", "key_after": "02 0",
        "combo_early": "02 0", "combo_fired": "38 0", "combo_end": "02 0",
        "low_soon": "00 0", "low_red_ms": "50", "crit_usb": "02 0", "crit_batt": "00 1"},
    1: {"demo_start": "0a 0", "demo_slot2": "12 0", "demo_slot_end": "02 0",
        # dongle <-> USB: green only, no slot blue (board check 2026-11-13)
        "demo_pair_seen": "12", "demo_dongle_seen": "02", "demo_dongle_end": "00 0",
        "demo_usb_back_seen": "02", "demo_usb_back": "02 0", "demo_batt1": "1", "demo_batt2": "2",
        "demo_crit_done": "02 0", "demo_batt_after": "0", "demo_combo": "38 0",
        "demo_combo_batt": "0", "demo_combo_slot": "02 0"},
    # BOARD_BLE (SAYO_2B3 BLE stage 1); hal_ble_* counts, then pair (1 = to
    # anyone) and state (0 idle, 1 advertising, 2 link, 3 ready)
    "ble": {"ble_wait_usb": "starts 0 stops 0 forgets 0 pair 9 state 0",
            "ble_reconnect": "starts 1 stops 0 forgets 0 pair 0 state 1",
            "ble_reconnect_seen": "08", "ble_connected": "08 0", "ble_connected_end": "00 0",
            "ble_usb": "starts 1 stops 1 forgets 0 pair 0 state 0", "ble_usb_led": "02 0",
            "ble_usb_out": "starts 2 stops 1 forgets 0 pair 0 state 1",
            "ble_adv_59s": "starts 2 stops 1 forgets 0 pair 0 state 1",
            "ble_adv_timeout": "starts 2 stops 2 forgets 0 pair 0 state 0", "ble_adv_off_led": "00 0",
            "ble_key_restart": "starts 3 stops 2 forgets 0 pair 0 state 1",
            "ble_no_timeout_linking": "starts 3 stops 2 forgets 0 pair 0 state 2",
            "ble_link_lost": "starts 4 stops 2 forgets 0 pair 0 state 1",
            "ble_pair": "starts 1 stops 0 forgets 1 pair 1 state 1", "ble_pair_seen": "08",
            "ble_pair_then_lost": "starts 2 stops 0 forgets 1 pair 0 state 1",
            "ble_unbonded": "starts 1 stops 0 forgets 0 pair 1 state 1",
            "ble_usb_boot": "starts 0 stops 1 forgets 0 pair 1 state 0"},
}


def check_bt_app(out: Path) -> None:
    """src/core/bt_app.c (SAYO_2B3 stage 1): USB green, key blip, key 1 + 6 battery
    level, low/critical from the smoothed reading, sleep only off the cable; the
    LED check build (BTLED_DEMO=1) where keys set the BLE/dongle states; the BLE
    link (BOARD_BLE)."""
    header = out / "test_bt_app.h"
    header.write_text('#define BOARD_NAME "TEST_APP"\n#define BOARD_BT_LED 1\n#define BOARD_BATTERY 1\n')
    for demo, want in BT_APP_WANT.items():
        exe = out / f"bt_app_test{demo}.exe"
        ble = ["-DBOARD_BLE=1"] if demo == "ble" else []
        subprocess.run(["gcc", "-std=c99", "-Wall", "-Werror", f'-DBOARD_HEADER="{header.as_posix()}"',
                        f"-DBTLED_DEMO={1 if demo == 1 else 0}", *ble, "-I", str(SRC),
                        str(HERE / "bt_app_test.c"),
                        *(str(SRC / "core" / f) for f in ("bt_app.c", "bt_led.c", "battery.c")),
                        "-o", str(exe)], check=True)
        got = dict(ln.split(" ", 1) for ln in subprocess.run(
            [str(exe)], capture_output=True, text=True, check=True).stdout.splitlines())
        check(f"bt_app{' ' + str(demo) if demo else ''}: LED states", got, want)



def check_battery_cmd(out: Path) -> None:
    """CMD_BATTERY through protocol.c and battery.c (tests/host/boards/test_bat.h):
    flags, live and rest (before the cable) estimate, seconds on the cable, last VDD."""
    header = TEST_BOARDS / "test_bat.h"
    exe = build(out, "TEST_BAT", header)
    blob = p.encode_blob([""] * 6, board=test_board(header)[1])

    def query(*steps: str) -> list[tuple]:
        rows = []
        for line in run(exe, out, blob, extra=["bat", *steps]):
            if line.startswith("res "):
                b = bytes.fromhex(line.split()[1])
                u16 = lambda i: b[i] | b[i + 1] << 8  # noqa: E731
                rows.append((b[0], b[2], b[3], b[4], u16(5), b[7], u16(8), u16(10), u16(12)))
        return rows

    def est(readings: list[int]) -> int:   # battery.c smoothing, +650 mV, no charge offset
        mv = 0
        for r in readings:
            v = r + 650
            mv = v if mv == 0 else mv + int((v - mv) / 4)
        return mv
    live = est([3517, 3517, 3600, 3600])
    check("BATTERY: no reading yet", query("q"), [(0x87, 0, 0, 0, 0, 0, 0, 0, 0)])
    check("BATTERY: on battery, then cable after 2 readings (rest kept, 20 s), cable out",
          query("3517", "q", "3517", "u1", "3600", "3600", "q", "u0", "q"),
          [(0x87, 0, 0x01, bat_percent(4167), 4167, 0, 0, 0, 3517),
           (0x87, 0, 0x13, bat_percent(live), live, bat_percent(4167), 4167, 20, 3600),
           (0x87, 0, 0x01, bat_percent(live), live, 0, 0, 0, 3600)])
    check("BATTERY: power-up on the cable has no rest value",
          query("3517", "u1", "3600", "q"),
          [(0x87, 0, 0x03, bat_percent(est([3517, 3600])), est([3517, 3600]), 0, 0, 10, 3600)])
    check("BATTERY: low and critical flags",
          [r[2] for r in query("2400", "q")], [0x0D])


def check_staging(out: Path) -> None:
    """WRITE without COMMIT: with one RAM buffer (BOARD_STORE_ONE_BUFFER,
    SAYO_2B3) the slots type nothing until a COMMIT and a refused COMMIT reads
    the stored blob back; the lock pattern and auto-lock never change. With two
    buffers staging leaves the active config alone."""
    two = TEST_BOARDS / "test_2x3.h"
    one = out / "test_1buf.h"
    one.write_text(two.read_text(encoding="utf-8").replace('"TEST_2X3"', '"TEST_1BF"')
                   + "#define BOARD_STORE_ONE_BUFFER 1\n", encoding="utf-8")
    check_board(out, "", one)  # the whole config channel, keys and lock
    _, layout, _ = test_board(two)
    stored = p.encode_blob(["abc"] + [""] * (layout.slot_count - 1), [5, 0], board=layout, autolock=7)
    other = p.encode_blob(["x" * 40] + [""] * (layout.slot_count - 1), [1, 2, 3], board=layout, autolock=1)
    (out / "other.bin").write_bytes(other)
    for name, header, staged in (("two buffers", two, 3), ("one buffer", one, 0)):
        exe = build(out, "STAGE1" if header == one else "STAGE2", header)
        lines = run(exe, out, stored, extra=["stage", str(out / "other.bin")])[3:]
        check(f"staging, {name}", lines,
              ["write status 0", f"staged slot0 len {staged} lock 5 0 auto 7",
               f"commit status {p.Status.BAD_ARG}", "refused slot0 len 3 lock 5 0 auto 7"])


def check_bt_boot_key() -> None:
    """SAYO_2B3: the bootloader's boot key pin (firmware/bootloader_ch573/boards)
    is the app's BOARD_BOOT_KEY key; key 4 = PA9, bottom row left (2026-11-13)."""
    boot = (ROOT / "firmware" / "bootloader_ch573" / "boards" / "sayo_2b3.h").read_text(encoding="utf-8")
    app = (SRC / "hal" / "ch573" / "sayo_2b3.h").read_text(encoding="utf-8")
    pin = re.search(r"^#define BOARD_BOOT_KEY_PIN (\d+), (\d+)", boot, re.M)
    key = int(re.search(r"^#define BOARD_BOOT_KEY (\d+)", app, re.M).group(1))
    pins = re.findall(r"\{(\d+), (\d+)\}", re.search(r"^#define BOARD_KEY_PINS (.*)$", app, re.M).group(1))
    check("sayo_2b3 boot key: bootloader pin = app key", str(pins[key]), str(pin.groups()))
    check("sayo_2b3 boot key: key 4 (PA9)", f"{key + 1} {pin.groups()}", "4 ('0', '9')")


def main() -> int:
    with tempfile.TemporaryDirectory() as tmp:
        out = Path(tmp)
        exe = build(out)

        slots = [
            "Hello, World!{ENTER}",
            "{CTRL+C}{ctrl+shift+esc}{ALT+F4}{WIN+r}",
            "{{x}}",
            "a{DELAY:50}b",
            "{CTRL++}",
            "ab\ncd",
            "{UP}",
            "{DOWN}",
            "",
        ]
        lines = run(exe, out, p.encode_blob(slots))
        check("blank DataFlash reads as empty", lines[0], "blank slot0 len 0")
        check("write status", lines[1], "write status 0")
        check("commit status", lines[2], "commit status 0")
        got = slot_lines(lines)
        check("plain text + ENTER", got[0], keys(
            (SHIFT, 0x0b), (0, 0x08), (0, 0x0f), (0, 0x0f), (0, 0x12), (0, 0x36), (0, 0x2c),
            (SHIFT, 0x1a), (0, 0x12), (0, 0x15), (0, 0x0f), (0, 0x07), (SHIFT, 0x1e), (0, 0x28)))
        check("combos", got[1], keys((CTRL, 0x06), (CTRL | SHIFT, 0x29), (ALT, 0x3d), (WIN, 0x15)))
        check("brace escapes", got[2], keys((SHIFT, 0x2f), (0, 0x1b), (SHIFT, 0x30)))
        delay = got[3].split()
        check("delay waits ~50 ms", " ".join(x for x in delay if not x.startswith("D")),
              keys((0, 0x04), (0, 0x05)))
        d = [int(x[1:]) for x in delay if x.startswith("D")]
        check("delay length", str(bool(d) and 50 <= d[0] <= 53), "True")
        check("CTRL++", got[4], keys((CTRL | SHIFT, 0x2e)))
        check("newline is ENTER", got[5], keys((0, 0x04), (0, 0x05), (0, 0x28), (0, 0x06), (0, 0x07)))
        check("knob UP", got[6], keys((0, 0x52)))
        check("knob DOWN", got[7], keys((0, 0x51)))
        check("empty slot", got[8], "(empty)")
        check("press/release pairing", lines[-1], "errors 0")

        lines = run(exe, out, p.encode_blob(["aB"] + [""] * 8), capslock=True)
        check("caps lock inverts shift for letters", slot_lines(lines)[0], keys((SHIFT, 0x04), (0, 0x05)))

        # Largest blob that fits: header 8 + 9 lengths 18 + 102 chars = 128
        full = ["x" * 102] + [""] * 8
        lines = run(exe, out, p.encode_blob(full))
        check("128-byte blob commits", lines[2], "commit status 0")
        check("128-byte blob types 102 chars", str(len(slot_lines(lines)[0].split())), "102")

        lines = run(exe, out, p.encode_blob(["x" * 103] + [""] * 8))
        check("129-byte blob rejected (TOO_BIG)", lines[1], f"write status {p.Status.TOO_BIG}")

        bad = bytearray(p.encode_blob(slots))
        bad[-1] ^= 0xFF  # corrupt payload, header CRC unchanged
        lines = run(exe, out, bytes(bad))
        check("corrupt payload rejected (BAD_CRC)", lines[2], f"commit status {p.Status.BAD_CRC}")

        # Lock pattern A B C knob-CW (slot 9 of the blob)
        short = ["Hi{ENTER}", "{CTRL+C}"] + [""] * 7
        lock = [0, 1, 2, 6]
        locked_blob = p.encode_blob(short, lock)
        # H = knob held LOCK_HOLD_MS (starts a pattern entry), t = entry timeout
        lines = run(exe, out, locked_blob, extra=["lock", "0126", "H", "0126"])
        check("lock blob commits", lines[2], "commit status 0")
        check("locked ignores inputs until the knob hold, then the pattern opens",
              " | ".join(lock_states(lines)), "locked 3 | locked 3 | unlocking 3 | open 1")

        lines = run(exe, out, locked_blob, extra=["lock", "H0124", "H0126"])
        check("wrong try goes back to locked, next try opens", " | ".join(lock_states(lines)[1:]),
              "locked 3 | open 1")

        lines = run(exe, out, locked_blob, extra=["lock"] + ["H1111"] * 5 + ["H0126"])
        states = lock_states(lines)
        check("5 wrong tries block", states[5], "blocked 7")
        check("blocked ignores the hold and the right pattern", states[6], "blocked 7")

        lines = run(exe, out, locked_blob, extra=["lock"] + ["H01t"] * 6 + ["H0126"])
        states = lock_states(lines)
        check("unfinished entries time out without counting", states[6], "locked 3")
        check("...and the pattern still opens after them", states[7], "open 1")

        lines = run(exe, out, locked_blob, extra=["lock", "H0126012"])
        check("extra inputs after opening are ignored", lock_states(lines)[1], "open 1")

        lines = run(exe, out, locked_blob, extra=["lock", "H0126", "H", "0126", "H0126"])
        check("re-lock: hold + pattern locks again, then unlocks again",
              " | ".join(lock_states(lines)[1:]), "open 1 | relock 1 | locked 3 | open 1")

        lines = run(exe, out, locked_blob, extra=["lock", "H0126", "H0124", "H01t"])
        check("re-lock: wrong pattern or timeout stays open",
              " | ".join(lock_states(lines)[1:]), "open 1 | open 1 | open 1")

        lines = run(exe, out, locked_blob, extra=["lock", "H0126"] + ["H1111"] * 5 + ["H0126"])
        states = lock_states(lines)
        check("re-lock: 4 wrong tries stay open", states[5], "open 1")
        check("re-lock: 5th wrong try locks anyway", states[6], "locked 3")
        check("...and it unlocks normally afterwards", states[7], "open 1")

        lines = run(exe, out, locked_blob,
                    extra=["lock", "H0126"] + ["H1111"] * 5 + ["H1111"] * 4 + ["H0126"])
        check("auto-lock gives 5 fresh unlock tries (4 wrong, then right)",
              lock_states(lines)[-1], "open 1")

        lines = run(exe, out, locked_blob,
                    extra=["lock", "H0126"] + ["H1111"] * 4 + ["H0126", "H0126"] + ["H1111"] * 4)
        check("re-lock failures reset after a successful re-lock and unlock",
              lock_states(lines)[-1], "open 1")

        lines = run(exe, out, locked_blob, extra=["lock", "H01w26", "H01w26"])
        # Both entries wait 10 s, so the pattern still finishes after the pause
        check("6 s pause: unlock and re-lock entries (10 s) go on",
              " | ".join(lock_states(lines)[1:]), "open 1 | locked 3")

        # Auto-lock (settings slot): m = one minute without input
        auto_blob = p.encode_blob(short, lock, autolock=2)
        lines = run(exe, out, auto_blob, extra=["lock", "H0126", "m", "m"])
        check("auto-lock blob commits", lines[2], "commit status 0")
        check("auto-lock: 2 idle minutes lock an open keyboard",
              " | ".join(lock_states(lines)[1:]), "open 1 | open 1 | locked 3")
        lines = run(exe, out, auto_blob, extra=["lock", "H0126", "m", "5", "m", "m"])
        check("auto-lock: an input starts the time over",
              " | ".join(lock_states(lines)[1:]), "open 1 | open 1 | open 1 | open 1 | locked 3")
        lines = run(exe, out, auto_blob, extra=["lock", "H0126", "mm", "H1111"] + ["H1111"] * 3 + ["H0126"])
        check("auto-lock gives 5 fresh unlock tries", lock_states(lines)[-1], "open 1")
        lines = run(exe, out, locked_blob, extra=["lock", "H0126", "mmmmm"])
        check("no settings slot: never auto-locks", lock_states(lines)[-1], "open 1")
        got = slot_lines(run(exe, out, auto_blob))
        check("slots still type next to the settings slot", got[1], keys((CTRL, 0x06)))

        lines = run(exe, out, p.encode_blob(short), extra=["lock", "H"])
        check("no pattern starts open and the hold does nothing",
              " | ".join(lock_states(lines)), "open 0 | open 0")

        got = slot_lines(run(exe, out, locked_blob))
        check("slots still type next to the lock slot", f"{got[0]} | {got[1]} | {got[8]}",
              keys((SHIFT, 0x0b), (0, 0x0c), (0, 0x28)) + " | " + keys((CTRL, 0x06)) + " | (empty)")

        # 2-step pattern (1.6.0; 4 steps at least before)
        lines = run(exe, out, p.encode_blob(short, [3, 6]), extra=["lock", "H36", "H", "36", "H3"])
        check("2-step lock commits", lines[2], "commit status 0")
        check("2-step lock: unlock, re-lock, a 1-step try stays locked",
              " | ".join(lock_states(lines)[1:]), "open 1 | relock 1 | locked 3 | unlocking 3")

        for name, bad_lock in (("1-step lock", [0]), ("9-step lock", [0] * 9),
                               ("lock step 9", [0, 1, 2, 9])):
            # Built by hand: encode_blob refuses invalid patterns
            payload = bytearray()
            for s in short:
                payload += len(s).to_bytes(2, "little") + s.encode()
            payload += len(bad_lock).to_bytes(2, "little") + bytes(bad_lock)
            blob = (b"MK" + bytes([1, 10]) + len(payload).to_bytes(2, "little")
                    + p.crc16_ccitt_false(bytes(payload)).to_bytes(2, "little") + payload)
            lines = run(exe, out, blob)
            check(f"{name} rejected (BAD_ARG)", lines[2], f"commit status {p.Status.BAD_ARG}")

        # A smaller config blanks what the old one left behind
        small = out / "small.bin"
        small.write_bytes(p.encode_blob([""] * 9))
        lines = run(exe, out, p.encode_blob(full), extra=["tail", str(small)])
        check("old bytes past a new blob are blanked", lines[-1], "tail dirty 0")

        # The host stops collecting reports (KVM switched away) or resets the
        # bus while typing: the rest and the queued second copy are dropped,
        # and typing works again once the host is back
        hi = p.encode_blob(["Hi!"] + [""] * 8)
        hi_keys = keys((SHIFT, 0x0b), (0, 0x0c), (SHIFT, 0x1e))
        for n, typed in ((1, ""), (3, "02:0b "), (4, "02:0b 00:0c ")):
            got = slot_lines(run(exe, out, hi, extra=["stall", str(n)]))
            check(f"stall at report {n}: dropped after the timeout", got[0],
                  f"{typed}lost cancel lost cancel")
            check(f"stall at report {n}: types again", got[1], hi_keys)
        got = slot_lines(run(exe, out, hi, extra=["reset", "3"]))
        check("bus reset: the rest is dropped", got[0], keys((SHIFT, 0x0b), (0, 0x0c)))
        check("bus reset: types again", got[1], hi_keys)

        # Keys, knob and LEDs (src/core/input.c, src/core/led.c) through the host HAL,
        # on both boards. Slots type a..i: key C = 06, CW = 0a, CCW = 0b.
        letters = p.encode_blob(list("abcdefghi"))
        locked = p.encode_blob(list("abcdefghi"), [3, 6])     # D, then knob CW
        for board, exe_b in (("MK6K1N", exe), ("MK6K1NL", build(out, "MK6K1NL"))):
            key_leds = board == "MK6K1NL"

            def typed(blob: bytes, *steps: str) -> tuple[str, str]:
                """(key reports, LED frames) of a run of input steps, after the
                power-up color wheel (6.3 s, BOARD_LED_SELF_TEST) and its off frame."""
                text = " ".join(run(exe_b, out, blob, extra=["input", "w6500", *steps])[3:])
                leds = re.findall(r"L\d\.[0-9a-f]{2}\.\d", text)
                return (" ".join(re.findall(r"\b[0-9a-f]{2}:[0-9a-f]{2}\b", text)),
                        " ".join(leds[1:] if leds[:1] == ["L0.00.0"] else leds))

            text = " ".join(run(exe_b, out, letters, extra=["input", "w6500"])[3:])
            check(f"{board}: power-up color wheel, then off, then blue",
                  " ".join(re.findall(r"L\d\.[0-9a-f]{2}\.\d", text)), "L0.00.0 L1.00.0")
            wheel = wheel_colors(text)
            check(f"{board}: color wheel starts red and passes green and blue",
                  (wheel[:1], "C0.40.0" in wheel, "C0.0.40" in wheel), (["C40.0.0"], True, True))
            check(f"{board}: color wheel goes round 2 times",
                  sum(1 for a, b in zip([""] + wheel, wheel) if b == "C40.0.0" and a != b), 2)
            text = " ".join(run(exe_b, out, letters, extra=["input", "p2", "w30", "r2", "w300"])[3:])
            check(f"{board}: a key types during the wheel, no key LED frame",
                  (" ".join(re.findall(r"\b[0-9a-f]{2}:[0-9a-f]{2}\b", text)),
                   re.findall(r"L\d\.[0-9a-f]{2}\.\d", text)), (keys((0, 0x06)), []))
            reports, leds = typed(letters, "p2", "w30", "r2", "w300")
            check(f"{board}: key C types its slot", reports, keys((0, 0x06)))
            check(f"{board}: key C lights green, then blue again", leds,
                  "L1.00.0 L0.04.2 L1.00.0" if key_leds else "L1.00.0 L2.00.0 L1.00.0")
            # Held 1 s: green from the press on, blue only after the release
            steps = run(exe_b, out, letters, extra=["input", "w6500", "p2", "w1000", "r2", "w30"])[3:]
            held = {line.split(":", 1)[0]: re.findall(r"L\d\.[0-9a-f]{2}\.\d", line)
                    for line in steps}
            check(f"{board}: a held key stays green until its release",
                  (held["w1000"], held["w30"]),
                  (["L0.04.2"] if key_leds else ["L2.00.0"], ["L1.00.0"]))
            reports, leds = typed(letters, "e10", "e00", "e01", "e11", "w300")
            check(f"{board}: a clockwise detent types slot 6", reports, keys((0, 0x0a)))
            check(f"{board}: a knob action lights every key LED", leds,
                  "L1.00.0 L0.3f.2 L1.00.0" if key_leds else "L1.00.0 L2.00.0 L1.00.0")
            check(f"{board}: a counter-clockwise detent types slot 7",
                  typed(letters, "e01", "e00", "e10", "e11", "w300")[0], keys((0, 0x0b)))
            check(f"{board}: a detent with a missed sample still counts",
                  typed(letters, "e10", "e00", "e11", "w300")[0], keys((0, 0x0a)))
            check(f"{board}: knob off a detent (10) chattering to 11 types nothing",
                  typed(letters, "s10", "e11", "e10", "e11", "e10", "e11", "w300")[0], "")
            check(f"{board}: a 10 ms bounce is not a key press",
                  typed(letters, "p1", "w10", "r1", "w300")[0], "")
            reports, leds = typed(locked, "p6", "w2100", "r6", "w50", "p3", "w30", "r3", "w300",
                                  "e10", "e00", "e01", "e11", "w300")
            check(f"{board}: pattern entry types nothing and opens", reports, "")
            check(f"{board}: pattern entry LEDs (locked, red, pressed key dark, open)", leds,
                  "L5.00.0 L0.00.0 L5.00.0 L5.08.0 L5.00.0 L1.00.0" if key_leds
                  else "L5.00.0 L0.00.0 L5.00.0 L1.00.0")

        # SAYO_3X3 (CH32V203): 9 keys, no knob, LED per key (mask 9 bits)
        exe9 = build(out, "SAYO_3X3")

        def typed9(blob: bytes, *steps: str) -> tuple[str, str, list[str]]:
            text = " ".join(run(exe9, out, blob, extra=["input", *steps])[3:])
            return (" ".join(re.findall(r"\b[0-9a-f]{2}:[0-9a-f]{2}\b", text)),
                    " ".join(re.findall(r"L\d\.[0-9a-f]{2,3}\.\d", text)),
                    wheel_colors(text))

        lines = run(exe9, out, letters)
        check("SAYO_3X3: 9-slot blob commits", lines[2], "commit status 0")
        for i in range(9):
            check(f"SAYO_3X3: key {i + 1} types slot {i}",
                  typed9(letters, f"p{i}", "w30", f"r{i}", "w300")[0], keys((0, 0x04 + i)))
        check("SAYO_3X3: key 9 press lights only key 9 green, then blue again",
              typed9(letters, "w6500", "p8", "w30", "r8", "w300")[1].split(" ", 1)[1],
              "L1.00.0 L0.100.2 L1.00.0")
        check("SAYO_3X3: key 1 press lights only key 1 green",
              typed9(letters, "w6500", "p0", "w30", "r0", "w300")[1].split(" ", 1)[1],
              "L1.00.0 L0.01.2 L1.00.0")
        _, power_leds, wheel = typed9(letters, "w6500")
        check("SAYO_3X3: power-up color wheel, then off, then blue",
              power_leds, "L0.00.0 L1.00.0")
        check("SAYO_3X3: color wheel starts red and passes green and blue",
              (wheel[:1], "C0.40.0" in wheel, "C0.0.40" in wheel), (["C40.0.0"], True, True))
        check("SAYO_3X3: color wheel goes round 2 times",
              sum(1 for a, b in zip([""] + wheel, wheel) if b == "C40.0.0" and a != b), 2)
        check("SAYO_3X3: two keys at once type both",
              typed9(letters, "p0", "w30", "p8", "w30", "r0", "r8", "w300")[0],
              keys((0, 0x04), (0, 0x0c)))
        check("SAYO_3X3: a 10 ms bounce is not a key press",
              typed9(letters, "p4", "w10", "r4", "w300")[0], "")
        # No knob: holding any key starts the pattern entry (pattern D, G)
        locked9 = p.encode_blob(list("abcdefghi"), [3, 6])
        lines = run(exe9, out, locked9)
        check("SAYO_3X3: a lock pattern commits", lines[2], "commit status 0")
        unlock = ("p4", "w2100", "r4", "w50", "p3", "w30", "r3", "w30", "p6", "w30", "r6", "w300")
        check("SAYO_3X3: locked keys type nothing",
              typed9(locked9, "p0", "w30", "r0", "p3", "w30", "r3", "p6", "w30", "r6", "w300")[0], "")
        check("SAYO_3X3: key hold + pattern opens, then a key types",
              typed9(locked9, *unlock, "p0", "w30", "r0", "w300")[0], keys((0, 0x04)))
        check("SAYO_3X3: with a pattern, a key types on press (before release)",
              typed9(locked9, *unlock, "p0", "w300")[0], keys((0, 0x04)))
        check("SAYO_3X3: with a pattern, rolled keys type both in order",
              typed9(locked9, *unlock, "p0", "w30", "p8", "w30", "r0", "r8", "w300")[0],
              keys((0, 0x04), (0, 0x0c)))
        check("SAYO_3X3: wrong pattern stays locked",
              typed9(locked9, "p4", "w2100", "r4", "w50", "p3", "w30", "r3", "w30", "p5", "w30", "r5",
                     "w300", "p0", "w30", "r0", "w300")[0], "")
        check("SAYO_3X3: re-lock with key hold + pattern (held key types once)",
              typed9(locked9, *unlock, *unlock, "p0", "w30", "r0", "w300")[0], keys((0, 0x08)))
        lines = run(exe9, out, p.encode_blob(short, autolock=2), extra=["lock", "mmm"])
        check("SAYO_3X3: settings slot without a lock never locks",
              lock_states(lines)[-1], "open 0")
        lines = run(exe9, out, p.encode_blob(full))
        check("SAYO_3X3: 128-byte blob commits", lines[2], "commit status 0")

        # CH32V203 capacity 4096: header 8 + 9 lengths 18 + 4070 chars
        def long_text(n: int) -> str:
            return "".join(chr(ord("a") + i % 26) for i in range(n))
        big = p.encode_blob([long_text(4070)] + [""] * 8)
        check("SAYO_3X3: largest blob is 4096 bytes", str(len(big)), "4096")
        lines = run(exe9, out, big)
        check("SAYO_3X3: 4096-byte blob commits", lines[2], "commit status 0")
        check("SAYO_3X3: 4096-byte blob types 4070 chars",
              str(len(slot_lines(lines)[0].split())), "4070")
        lines = run(exe9, out, p.encode_blob([long_text(4071)] + [""] * 8))
        check("SAYO_3X3: 4097-byte blob rejected (TOO_BIG)", lines[1],
              f"write status {p.Status.TOO_BIG}")
        lines = run(exe9, out, p.encode_blob([long_text(300), "x" * 300] + list("cdefghi")))
        check("SAYO_3X3: 300-char slots commit", lines[2], "commit status 0")
        check("SAYO_3X3: 300-char slot types 300 chars",
              str(len(slot_lines(lines)[1].split())), "300")

        # Power lost while hal_store_flush (CH32V) rewrites the pages of a new
        # config: the next power-up must find the CRC wrong and start blank,
        # never type a mix of the old and new texts
        old_cfg = p.encode_blob(list("abcdefghi"))                          # page 0
        new_cfg = p.encode_blob([long_text(600), "y" * 600] + list("cdefghi"))  # pages 0-4
        new_path = out / "new_cfg.bin"
        new_path.write_bytes(new_cfg)
        last = (len(new_cfg) - 1) // 256
        for page in range(last + 1):
            for erased in (1, 0):
                if page == last and not erased:
                    continue          # that is the whole new config
                lines = run(exe9, out, old_cfg, extra=["torn", str(new_path), str(page), str(erased)])
                slots = slot_lines(lines)
                check(f"SAYO_3X3: power lost at page {page} ({'erased' if erased else 'written'}) "
                      "starts blank",
                      ([line for line in lines if line.startswith("torn ")],
                       set(slots.values()), lines[-1]),
                      ([f"torn page {page} erased {erased}: slot0 len 0"], {"(empty)"}, "errors 0"))
        lines = run(exe9, out, old_cfg, extra=["torn", str(new_path), str(last + 1), "0"])
        check("SAYO_3X3: a finished flush types the new config",
              (len(slot_lines(lines)[0].split()), len(slot_lines(lines)[1].split())), (600, 600))

        # Every board file: the config channel (src/core/protocol.c) read with
        # the GUI's parser, every key and the power-up color wheel
        for name in board_names():
            check_board(out, name)
        for header in sorted(TEST_BOARDS.glob("*.h")):
            check_board(out, "", header)

        check_battery(out)
        check_battery_cmd(out)
        check_bt_led(out)
        check_bt_app(out)
        check_bt_boot_key()
        check_staging(out)

    print("all passed" if failures == 0 else f"{failures} failed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
