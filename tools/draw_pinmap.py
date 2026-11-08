"""Draw the pin maps and button maps used in docs/board/BOARD_*.md (docs/images/).

pinmap_*.svg: the CH552G (SOP-16) as seen from the top, pins 1-8 down the
left side, 16-9 down the right side, each with what it is wired to.
buttons_*.svg: the keyboard as seen from the top, each key and the knob
with its GPIO, chip pin and slot (and LED, on boards with one per key).
CH32V203 boards: buttons_<board>.svg (key pin, slot, anode, cathode group),
pinmap_<board>.svg (the 48-pin chip with what each pin is wired to)
and leds_<board>.svg (cathode lines x per-key anodes).

Run from the repo root:  uv run python tools\\draw_pinmap.py
"""
from __future__ import annotations

from html import escape
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "docs" / "images"

# Kind of each connection -> (fill, stroke)
COLORS = {
    "key": ("#e3f2fd", "#1e88e5"),
    "boot": ("#fff3e0", "#fb8c00"),
    "knob": ("#e8f5e9", "#43a047"),
    "led": ("#fce4ec", "#d81b60"),
    "usb": ("#ede7f6", "#5e35b1"),
    "power": ("#f5f5f5", "#9e9e9e"),
    "none": ("#ffffff", "#bdbdbd"),
}

# pin -> (GPIO name, connection, kind); the same on both boards
PINS = {
    1: ("P3.2", "키 F · 슬롯 5", "key"),
    2: ("P1.4", "키 E · 슬롯 4", "key"),
    3: ("P1.5", "키 D · 슬롯 3 (부트로더 진입)", "boot"),
    4: ("P1.6", "키 C · 슬롯 2", "key"),
    5: ("P1.7", "키 B · 슬롯 1", "key"),
    6: ("RST", "연결 없음", "none"),
    7: ("P3.1", "노브 인코더 B", "knob"),
    8: ("P3.0", "노브 인코더 A", "knob"),
    9: ("P1.1", "키 A · 슬롯 0", "key"),
    10: ("P3.3", "노브 누르기 · 슬롯 8", "knob"),
    11: ("P3.4", "LED 데이터", "led"),
    12: ("P3.6", "USB D+", "usb"),
    13: ("P3.7", "USB D−", "usb"),
    14: ("GND", "GND", "power"),
    15: ("VCC", "5V (USB)", "power"),
    16: ("V33", "3.3V", "power"),
}

FONT = "Malgun Gothic, Apple SD Gothic Neo, Noto Sans KR, sans-serif"
PITCH = 46                 # distance between pins
TOP = 90                   # first pin row
CHIP_X, CHIP_W = 300, 220  # chip body
BOX_W, BOX_H = 230, 32     # connection boxes
STUB = 18                  # pin length outside the body


def row_y(pin: int) -> int:
    """Pins 1-8 top to bottom on the left, 16-9 top to bottom on the right."""
    index = pin - 1 if pin <= 8 else 16 - pin
    return TOP + index * PITCH


def box(x: float, y: float, text: str, kind: str, w: float = BOX_W) -> str:
    fill, stroke = COLORS[kind]
    return (f'<rect x="{x}" y="{y - BOX_H / 2}" width="{w}" height="{BOX_H}" rx="6" '
            f'fill="{fill}" stroke="{stroke}" stroke-width="1.5"/>'
            f'<text x="{x + w / 2}" y="{y + 5}" text-anchor="middle" font-size="14">{escape(text)}</text>')


def draw(board: str, title: str, led_text: str, chain: list[str] | None) -> str:
    width = 1060 if chain else 820
    height = TOP + 8 * PITCH + 70
    chip_left, chip_right = CHIP_X, CHIP_X + CHIP_W
    chip_top, chip_bottom = TOP - 30, TOP + 7 * PITCH + 30
    left_box_x = chip_left - STUB - 40 - BOX_W
    right_box_x = chip_right + STUB + 40
    parts = [
        f'<rect width="{width}" height="{height}" fill="#ffffff"/>',
        f'<text x="{width / 2}" y="32" text-anchor="middle" font-size="20" font-weight="bold">{escape(title)}</text>',
        # chip body with the pin 1 notch
        f'<rect x="{chip_left}" y="{chip_top}" width="{CHIP_W}" height="{chip_bottom - chip_top}" rx="8" '
        'fill="#37474f" stroke="#263238" stroke-width="2"/>',
        # notch at the top edge; pin 1 is the first pin left of it
        f'<path d="M {chip_left + CHIP_W / 2 - 16} {chip_top} a 16 16 0 0 0 32 0" fill="#ffffff"/>',
        f'<text x="{chip_left + CHIP_W / 2}" y="{(chip_top + chip_bottom) / 2 - 6}" text-anchor="middle" '
        'font-size="22" font-weight="bold" fill="#ffffff">CH552G</text>',
        f'<text x="{chip_left + CHIP_W / 2}" y="{(chip_top + chip_bottom) / 2 + 18}" text-anchor="middle" '
        'font-size="13" fill="#cfd8dc">SOP-16 · 윗면</text>',
    ]
    for pin, (gpio, text, kind) in PINS.items():
        y = row_y(pin)
        left = pin <= 8
        if pin == 11:
            text = led_text
        pin_x = chip_left - STUB if left else chip_right
        parts.append(f'<rect x="{pin_x}" y="{y - 6}" width="{STUB}" height="12" fill="#b0bec5" stroke="#78909c"/>')
        # pin number and name inside the body
        if left:
            parts.append(f'<text x="{chip_left + 10}" y="{y + 5}" font-size="14" fill="#ffffff">'
                         f'<tspan font-weight="bold">{pin}</tspan>  {gpio}</text>')
            line_from, line_to = left_box_x + BOX_W, chip_left - STUB
        else:
            parts.append(f'<text x="{chip_right - 10}" y="{y + 5}" text-anchor="end" font-size="14" fill="#ffffff">'
                         f'{gpio}  <tspan font-weight="bold">{pin}</tspan></text>')
            line_from, line_to = chip_right + STUB, right_box_x
        dash = ' stroke-dasharray="4 4"' if kind == "none" else ""
        parts.append(f'<line x1="{line_from}" y1="{y}" x2="{line_to}" y2="{y}" stroke="{COLORS[kind][1]}" '
                     f'stroke-width="2"{dash}/>')
        parts.append(box(left_box_x if left else right_box_x, y, text, kind))

    if chain:
        # the WS2812 chain, fed from pin 11, one LED per key
        x = right_box_x + BOX_W + 50
        w = 150
        ys = [row_y(16) + i * PITCH for i in range(len(chain))]
        start_y = row_y(11)
        parts.append(f'<path d="M {right_box_x + BOX_W} {start_y} H {x - 25} V {ys[0]} H {x}" fill="none" '
                     f'stroke="{COLORS["led"][1]}" stroke-width="2" marker-end="url(#arrow)"/>')
        for i, (y, text) in enumerate(zip(ys, chain)):
            parts.append(box(x, y, text, "led", w))
            if i:
                parts.append(f'<line x1="{x + w / 2}" y1="{ys[i - 1] + BOX_H / 2}" x2="{x + w / 2}" '
                             f'y2="{y - BOX_H / 2}" stroke="{COLORS["led"][1]}" stroke-width="2" '
                             'marker-end="url(#arrow)"/>')
        parts.append(f'<text x="{x + w / 2}" y="{ys[-1] + 40}" text-anchor="middle" font-size="12" '
                     f'fill="#616161">데이터 순서: 0 → 5</text>')

    legend = [("key", "키"), ("boot", "키 + 부트로더 진입"), ("knob", "노브"), ("led", "LED"),
              ("usb", "USB"), ("power", "전원")]
    lx = 20
    for kind, label in legend:
        fill, stroke = COLORS[kind]
        parts.append(f'<rect x="{lx}" y="{height - 30}" width="16" height="16" rx="3" fill="{fill}" stroke="{stroke}"/>')
        parts.append(f'<text x="{lx + 22}" y="{height - 17}" font-size="13">{label}</text>')
        lx += 46 + sum(14 if ord(c) > 127 else 8 for c in label)   # Hangul is about twice as wide
    parts.append(f'<text x="{width - 20}" y="{height - 17}" text-anchor="end" font-size="12" fill="#757575">'
                 f'{board} · tools/draw_pinmap.py</text>')
    return (f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" '
            f'viewBox="0 0 {width} {height}" font-family="{FONT}" fill="#212121">'
            '<defs><marker id="arrow" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" '
            f'orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="{COLORS["led"][1]}"/></marker></defs>'
            + "".join(parts) + "</svg>\n")


KEY_PINS = {"A": 9, "B": 5, "C": 4, "D": 3, "E": 2, "F": 1}  # key -> chip pin
KEY_W, KEY_H = 150, 112    # key caps in the button map


def draw_buttons(board: str, title: str, key_leds: bool) -> str:
    """The keyboard seen from the top: keys A-C over D-F, the knob right of C."""
    width, height = 860, 420
    parts = [
        f'<rect width="{width}" height="{height}" fill="#ffffff"/>',
        f'<text x="{width / 2}" y="32" text-anchor="middle" font-size="20" font-weight="bold">{escape(title)}</text>',
        '<rect x="20" y="56" width="820" height="320" rx="16" fill="#fafafa" stroke="#9e9e9e" stroke-width="2"/>',
    ]
    led_centers = []
    for i, key in enumerate("ABCDEF"):
        pin = KEY_PINS[key]
        gpio, _, kind = PINS[pin]
        fill, stroke = COLORS[kind]
        x = 50 + (i % 3) * (KEY_W + 26)
        y = 80 + (i // 3) * (KEY_H + 24)
        cx = x + KEY_W / 2
        parts.append(f'<rect x="{x}" y="{y}" width="{KEY_W}" height="{KEY_H}" rx="10" fill="{fill}" '
                     f'stroke="{stroke}" stroke-width="2"/>')
        parts.append(f'<text x="{cx}" y="{y + 34}" text-anchor="middle" font-size="26" font-weight="bold">{key}</text>')
        parts.append(f'<text x="{cx}" y="{y + 58}" text-anchor="middle" font-size="14">{gpio} · {pin}번 핀</text>')
        note = "슬롯 3 · 부트로더 진입" if kind == "boot" else f"슬롯 {i}"
        parts.append(f'<text x="{cx}" y="{y + 78}" text-anchor="middle" font-size="12" fill="#616161">{note}</text>')
        if key_leds:
            led_fill, led_stroke = COLORS["led"]
            ly = y + KEY_H - 16
            parts.append(f'<rect x="{cx - 32}" y="{ly - 10}" width="64" height="20" rx="10" fill="{led_fill}" '
                         f'stroke="{led_stroke}" stroke-width="1.5"/>')
            parts.append(f'<text x="{cx}" y="{ly + 5}" text-anchor="middle" font-size="12">LED {i}</text>')
            led_centers.append((cx, ly))

    # the knob, right of key C
    kx, ky, kr = 650, 136, 48
    knob_fill, knob_stroke = COLORS["knob"]
    parts.append(f'<circle cx="{kx}" cy="{ky}" r="{kr}" fill="{knob_fill}" stroke="{knob_stroke}" stroke-width="2"/>')
    parts.append(f'<circle cx="{kx}" cy="{ky}" r="{kr - 14}" fill="#ffffff" stroke="{knob_stroke}" stroke-width="1"/>')
    parts.append(f'<text x="{kx}" y="{ky + 5}" text-anchor="middle" font-size="14" font-weight="bold">노브</text>')
    knob_lines = [
        ("인코더 A", "P3.0 · 8번 핀"),
        ("인코더 B", "P3.1 · 7번 핀"),
        ("누르기", "P3.3 · 10번 핀 · 슬롯 8"),
        ("회전", "시계 슬롯 6 · 반시계 슬롯 7"),
    ]
    for j, (name, value) in enumerate(knob_lines):
        y = ky + kr + 28 + j * 22
        parts.append(f'<text x="{kx - 90}" y="{y}" font-size="13" font-weight="bold" fill="{knob_stroke}">{name}</text>')
        parts.append(f'<text x="{kx - 24}" y="{y}" font-size="13">{escape(value)}</text>')

    led_fill, led_stroke = COLORS["led"]
    if key_leds:
        # the chain: pin 11 feeds LED 0 (under A), then B, C, D, E, F
        sx, sy = 50 + KEY_W / 2 - 32, 80 + KEY_H - 16
        parts.append(f'<text x="44" y="{height - 18}" font-size="13" fill="{led_stroke}">'
                     'LED 체인: P3.4 · 11번 핀 → LED 0 → 1 → 2 → 3 → 4 → 5 (GRB)</text>')
        parts.append(f'<line x1="26" y1="{sy}" x2="{sx}" y2="{sy}" stroke="{led_stroke}" stroke-width="2" '
                     'marker-end="url(#arrow)"/>')
        parts.append(f'<text x="58" y="{sy - 14}" text-anchor="middle" font-size="11" fill="{led_stroke}">P3.4</text>')
        for (x1, y1), (x2, y2) in zip(led_centers, led_centers[1:]):
            if y1 == y2:
                parts.append(f'<line x1="{x1 + 32}" y1="{y1}" x2="{x2 - 32}" y2="{y2}" stroke="{led_stroke}" '
                             'stroke-width="2" marker-end="url(#arrow)"/>')
            else:   # C (end of the top row) through the gap and round the left edge to D
                gap_y = 80 + KEY_H + 12
                parts.append(f'<path d="M {x1} {y1 + 10} V {gap_y} H 36 V {y2} H {x2 - 32}" fill="none" '
                             f'stroke="{led_stroke}" stroke-width="2" stroke-dasharray="5 3" marker-end="url(#arrow)"/>')
    else:
        lx, ly = 650, 316
        parts.append(f'<circle cx="{lx}" cy="{ly}" r="9" fill="{led_fill}" stroke="{led_stroke}" stroke-width="2"/>')
        parts.append(f'<text x="{lx + 18}" y="{ly + 5}" font-size="13">LED · P3.4 · 11번 핀</text>')
        parts.append(f'<text x="44" y="{height - 18}" font-size="13" fill="{led_stroke}">'
                     'LED 1개(WS2812, GRB): 키보드 전체 상태 표시</text>')

    parts.append(f'<text x="{width - 20}" y="{height - 18}" text-anchor="end" font-size="12" fill="#757575">'
                 f'{board} · 윗면 · tools/draw_pinmap.py</text>')
    return (f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" '
            f'viewBox="0 0 {width} {height}" font-family="{FONT}" fill="#212121">'
            '<defs><marker id="arrow" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" '
            f'orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="{led_stroke}"/></marker></defs>'
            + "".join(parts) + "</svg>\n")


# CH32V203 boards (SAYO_3X3, SAYO_2X4): firmware/macro_keyboard/src/boards/<board>.h.
# Per key: (key pin, anode pin, cathode group). Keys in slot order, row-major.
# Cathode groups: name -> (keys, red, green, blue); low = lit. Anode high = lit.
RGB_BOARDS = {
    "SAYO_3X3": {
        "title": "SAYO_3X3", "desc": "9키, 노브 없음, CH32V203C8U6", "cols": 3, "boot": 6,
        "keys": [("PB15", "PB3", 0), ("PA14", "PB4", 0), ("PA4", "PB9", 0),
                 ("PB10", "PB14", 1), ("PA15", "PB5", 1), ("PA3", "PB8", 1),
                 ("PB2", "PB13", 2), ("PB11", "PB12", 2), ("PB1", "PB0", 2)],
        "groups": [("1줄", "1·2·3", "PA11", "PA12", "PA13"),
                   ("2줄", "4·5·6", "PA8", "PA9", "PA10"),
                   ("3줄", "7·8·9", "PA0", "PA1", "PA2")],
    },
    "SAYO_2X4": {
        "title": "SAYO_2X4", "desc": "8키, 노브 없음, CH32V203C8U6", "cols": 4, "boot": 4,
        "keys": [("PA6", "PB14", 0), ("PA15", "PB3", 0), ("PB5", "PB4", 0), ("PB9", "PB8", 2),
                 ("PA5", "PB13", 1), ("PA7", "PB15", 1), ("PB0", "PB12", 1), ("PA4", "PB11", 2)],
        "groups": [("묶음 1", "1·2·3", "PA11", "PA12", "PA13"),
                   ("묶음 2", "5·6·7", "PA8", "PA9", "PA10"),
                   ("묶음 3", "4·8", "PA0", "PA1", "PA2")],
    },
}
LED_RGB = (("빨강", "#e53935"), ("초록", "#43a047"), ("파랑", "#1e88e5"))
GROUP_TINT = ("#fff8e1", "#e0f7fa", "#f3e5f5")   # background per cathode group


def svg_wrap(width: int, height: int, parts: list[str]) -> str:
    led_stroke = COLORS["led"][1]
    return (f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" '
            f'viewBox="0 0 {width} {height}" font-family="{FONT}" fill="#212121">'
            '<defs><marker id="arrow" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" '
            f'orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="{led_stroke}"/></marker></defs>'
            + "".join(parts) + "</svg>\n")


def draw_rgb_buttons(board: str) -> str:
    """CH32V203 keyboard seen from the top: each key with its pin, slot, anode and cathode group."""
    b = RGB_BOARDS[board]
    cols, keys = b["cols"], b["keys"]
    rows = (len(keys) + cols - 1) // cols
    kw, kh, gap = 168, 128, 22
    pad_x, top = 50, 80
    width = max(860, pad_x * 2 + cols * kw + (cols - 1) * gap)
    body_h = rows * kh + (rows - 1) * gap + 48
    height = top + body_h + 40 + 22 * len(b["groups"]) + 30
    parts = [
        f'<rect width="{width}" height="{height}" fill="#ffffff"/>',
        f'<text x="{width / 2}" y="32" text-anchor="middle" font-size="20" font-weight="bold">'
        f'{escape(b["title"])} 키 배치 (윗면, {escape(b["desc"])})</text>',
        f'<rect x="20" y="56" width="{width - 40}" height="{body_h}" rx="16" fill="#fafafa" '
        'stroke="#9e9e9e" stroke-width="2"/>',
    ]
    x0 = (width - (cols * kw + (cols - 1) * gap)) / 2
    for i, (pin, anode, group) in enumerate(keys):
        kind = "boot" if i == b["boot"] else "key"
        fill, stroke = COLORS[kind]
        x = x0 + (i % cols) * (kw + gap)
        y = top + (i // cols) * (kh + gap)
        cx = x + kw / 2
        parts.append(f'<rect x="{x}" y="{y}" width="{kw}" height="{kh}" rx="10" fill="{fill}" '
                     f'stroke="{stroke}" stroke-width="2"/>')
        parts.append(f'<text x="{cx}" y="{y + 32}" text-anchor="middle" font-size="24" font-weight="bold">'
                     f'{chr(65 + i)}</text>')
        parts.append(f'<text x="{x + 12}" y="{y + 24}" font-size="12" fill="#616161">키 {i + 1}</text>')
        parts.append(f'<text x="{cx}" y="{y + 56}" text-anchor="middle" font-size="14">{pin} · 슬롯 {i}</text>')
        note = "부트로더 진입 키" if kind == "boot" else ""
        if note:
            parts.append(f'<text x="{cx}" y="{y + 75}" text-anchor="middle" font-size="12" '
                         f'fill="{stroke}" font-weight="bold">{note}</text>')
        led_fill, led_stroke = COLORS["led"]
        ly = y + kh - 22
        parts.append(f'<rect x="{x + 12}" y="{ly - 11}" width="{kw - 24}" height="22" rx="11" '
                     f'fill="{GROUP_TINT[group]}" stroke="{led_stroke}" stroke-width="1.5"/>')
        parts.append(f'<text x="{cx}" y="{ly + 5}" text-anchor="middle" font-size="12">'
                     f'LED 양극 {anode} · {escape(b["groups"][group][0])}</text>')
    gy = top + body_h + 20
    parts.append(f'<text x="44" y="{gy}" font-size="13" font-weight="bold">LED 음극(낮추면 켜짐, 색마다 핀 1개를 묶음 안의 키가 공유)</text>')
    for j, (name, members, r, g, bl) in enumerate(b["groups"]):
        y = gy + 22 * (j + 1)
        parts.append(f'<rect x="44" y="{y - 12}" width="16" height="16" rx="3" fill="{GROUP_TINT[j]}" '
                     f'stroke="{COLORS["led"][1]}"/>')
        parts.append(f'<text x="68" y="{y + 1}" font-size="13">{escape(name)} (키 {members}): '
                     f'<tspan fill="{LED_RGB[0][1]}">R {r}</tspan> · <tspan fill="{LED_RGB[1][1]}">G {g}</tspan> · '
                     f'<tspan fill="{LED_RGB[2][1]}">B {bl}</tspan></text>')
    parts.append(f'<text x="{width - 20}" y="{height - 14}" text-anchor="end" font-size="12" fill="#757575">'
                 f'{board} · 윗면 · tools/draw_pinmap.py</text>')
    parts.append(f'<text x="44" y="{height - 14}" font-size="12" fill="#616161">'
                 '키: 누르면 낮음(내부 풀업) · 양극: 높이면 켜짐 · USB는 PB6/PB7(USBFS)</text>')
    return svg_wrap(width, height, parts)


def draw_rgb_leds(board: str) -> str:
    """LED wiring as a grid: cathode lines (rows) x per-key anodes (columns)."""
    b = RGB_BOARDS[board]
    keys, groups = b["keys"], b["groups"]
    cw, rh = 64, 34
    left, top = 230, 120
    width = left + cw * len(keys) + 40
    height = top + rh * 3 * len(groups) + 70
    parts = [
        f'<rect width="{width}" height="{height}" fill="#ffffff"/>',
        f'<text x="{width / 2}" y="32" text-anchor="middle" font-size="20" font-weight="bold">'
        f'{escape(b["title"])} LED 배선 (음극 × 키별 양극)</text>',
        f'<text x="{width / 2}" y="56" text-anchor="middle" font-size="13" fill="#616161">'
        '점 = 그 키의 LED 색. 음극(가로)을 낮추고 양극(세로)을 높이면 켜짐</text>',
    ]
    for i, (_, anode, _) in enumerate(keys):
        cx = left + cw * i + cw / 2
        kind = "boot" if i == b["boot"] else "key"
        fill, stroke = COLORS[kind]
        parts.append(f'<rect x="{cx - 26}" y="{top - 46}" width="52" height="22" rx="5" fill="{fill}" stroke="{stroke}"/>')
        parts.append(f'<text x="{cx}" y="{top - 30}" text-anchor="middle" font-size="12" font-weight="bold">'
                     f'키 {i + 1}</text>')
        parts.append(f'<text x="{cx}" y="{top - 8}" text-anchor="middle" font-size="12">{anode}</text>')
        parts.append(f'<line x1="{cx}" y1="{top}" x2="{cx}" y2="{top + rh * 3 * len(groups)}" '
                     f'stroke="{COLORS["led"][1]}" stroke-width="1.5" stroke-dasharray="3 3"/>')
    for gi, (name, members, *pins) in enumerate(groups):
        gy = top + rh * 3 * gi
        parts.append(f'<rect x="20" y="{gy}" width="{width - 40}" height="{rh * 3}" fill="{GROUP_TINT[gi]}" '
                     'stroke="#e0e0e0"/>')
        parts.append(f'<text x="32" y="{gy + rh * 1.5 + 5}" font-size="13" font-weight="bold">'
                     f'{escape(name)}</text>')
        parts.append(f'<text x="32" y="{gy + rh * 1.5 + 22}" font-size="11" fill="#616161">키 {members}</text>')
        for ci, ((cname, color), pin) in enumerate(zip(LED_RGB, pins)):
            y = gy + rh * ci + rh / 2
            parts.append(f'<text x="{left - 14}" y="{y + 5}" text-anchor="end" font-size="13" fill="{color}">'
                         f'{cname} {pin}</text>')
            parts.append(f'<line x1="{left}" y1="{y}" x2="{left + cw * len(keys)}" y2="{y}" stroke="{color}" '
                         'stroke-width="2"/>')
            for ki, (_, _, g) in enumerate(keys):
                if g == gi:
                    cx = left + cw * ki + cw / 2
                    parts.append(f'<circle cx="{cx}" cy="{y}" r="8" fill="{color}" stroke="#ffffff" stroke-width="2"/>')
    parts.append(f'<text x="{width - 20}" y="{height - 14}" text-anchor="end" font-size="12" fill="#757575">'
                 f'{board} · tools/draw_pinmap.py</text>')
    parts.append(f'<text x="32" y="{height - 14}" font-size="12" fill="#616161">'
                 '펌웨어: 색마다 차례로 켜고, 키별 양극으로 밝기 조절(64단계)</text>')
    return svg_wrap(width, height, parts)


# CH32V203C8 pins 1-48: datasheet V2.6 table 3-1-1, one column for LQFP48 and
# QFN48 (checked 2026-11-08). QFN48 (C8U6, these boards) adds pin 0 = the
# exposed pad (VSS).
CH32_PINS = (
    "VBAT", "PC13", "PC14", "PC15", "PD0", "PD1", "NRST", "VSSA", "VDDA", "PA0", "PA1", "PA2",
    "PA3", "PA4", "PA5", "PA6", "PA7", "PB0", "PB1", "PB2", "PB10", "PB11", "VSS", "VDD",
    "PB12", "PB13", "PB14", "PB15", "PA8", "PA9", "PA10", "PA11", "PA12", "PA13", "VSS", "VDD",
    "PA14", "PA15", "PB3", "PB4", "PB5", "PB6", "PB7", "BOOT0", "PB8", "PB9", "VSS", "VDD",
)
CH32_POWER = {"VBAT", "VSSA", "VDDA", "VSS", "VDD"}
CH32_NOTES = {"PA13": "SWDIO", "PA14": "SWCLK", "PA15": "JTDI", "PB2": "BOOT1"}


def ch32_uses(board: str) -> dict[str, tuple[str, str]]:
    """GPIO name -> (what it is wired to, kind) on a CH32V203 board."""
    b = RGB_BOARDS[board]
    uses: dict[str, tuple[str, str]] = {}
    for i, (pin, anode, _) in enumerate(b["keys"]):
        boot = i == b["boot"]
        uses[pin] = (f"키 {i + 1} · 슬롯 {i}" + (" · 진입 키" if boot else ""), "boot" if boot else "key")
        uses[anode] = (f"키 {i + 1} LED 양극", "led")
    for name, _, *pins in b["groups"]:
        for (color, _), pin in zip(LED_RGB, pins):
            uses[pin] = (f"{name} {color} 음극", "led")
    uses["PB6"] = ("USB D− (USBFS)", "usb")
    uses["PB7"] = ("USB D+ (USBFS)", "usb")
    uses["BOOT0"] = ("BOOT 브리지 → ROM 부트로더", "boot")
    uses["NRST"] = ("리셋", "power")
    uses.setdefault("PB2", ("풀다운으로 보임 · 키 아님", "none"))
    return uses


def ch32_label(name: str, uses: dict[str, tuple[str, str]]) -> tuple[str, str]:
    if name in CH32_POWER:
        return ("전원" if name.startswith(("VDD", "VBAT")) else "GND"), "power"
    text, kind = uses.get(name, ("미사용", "none"))
    if name in CH32_NOTES:
        text += f" ({CH32_NOTES[name]})"
    return text, kind


def draw_ch32_pinmap(board: str) -> str:
    """CH32V203C8 (48 pins, 12 a side) from the top with what this board wires to each pin."""
    uses = ch32_uses(board)
    side, pitch, first, stub = 460, 30, 65, 16
    x0, y0 = 320, 380                  # chip body
    gap, bw, bh = 40, 250, 22          # pin -> label box
    width, height = x0 + side + gap + bw + 30, y0 + side + gap + bw + 80
    parts = [
        f'<rect width="{width}" height="{height}" fill="#ffffff"/>',
        f'<text x="{width / 2}" y="32" text-anchor="middle" font-size="20" font-weight="bold">'
        f'{escape(RGB_BOARDS[board]["title"])} 칩 핀맵 (CH32V203C8U6, 윗면)</text>',
        f'<text x="{width / 2}" y="56" text-anchor="middle" font-size="13" fill="#616161">'
        '핀 번호: 자료표 V2.6 표 3-1-1, LQFP48/QFN48 공통. 0번 = 바닥 패드 VSS</text>',
        f'<rect x="{x0}" y="{y0}" width="{side}" height="{side}" rx="10" fill="#37474f" stroke="#263238" stroke-width="2"/>',
        f'<circle cx="{x0 + 22}" cy="{y0 + 22}" r="7" fill="#cfd8dc"/>',
        f'<text x="{x0 + side / 2}" y="{y0 + side / 2 - 8}" text-anchor="middle" font-size="22" font-weight="bold" '
        'fill="#ffffff">CH32V203C8U6</text>',
        f'<text x="{x0 + side / 2}" y="{y0 + side / 2 + 16}" text-anchor="middle" font-size="13" fill="#cfd8dc">'
        'QFN48 · 윗면 · ● = 1번 핀</text>',
    ]
    for n, name in enumerate(CH32_PINS, start=1):
        text, kind = ch32_label(name, uses)
        fill, stroke = COLORS[kind]
        dash = ' stroke-dasharray="4 4"' if kind == "none" else ""
        k = (n - 1) % 12
        label = f'<tspan font-weight="bold">{n}</tspan> {name}'
        if n <= 12:      # left side, top to bottom
            y = y0 + first + k * pitch
            parts.append(f'<rect x="{x0 - stub}" y="{y - 5}" width="{stub}" height="10" fill="#b0bec5" stroke="#78909c"/>')
            parts.append(f'<text x="{x0 + 10}" y="{y + 4}" font-size="11" fill="#ffffff">{label}</text>')
            parts.append(f'<line x1="{x0 - gap}" y1="{y}" x2="{x0 - stub}" y2="{y}" stroke="{stroke}" stroke-width="2"{dash}/>')
            bx, by, tr = x0 - gap - bw, y - bh / 2, ""
        elif n <= 36 and n >= 25:   # right side, bottom to top
            y = y0 + side - first - k * pitch
            parts.append(f'<rect x="{x0 + side}" y="{y - 5}" width="{stub}" height="10" fill="#b0bec5" stroke="#78909c"/>')
            parts.append(f'<text x="{x0 + side - 10}" y="{y + 4}" text-anchor="end" font-size="11" fill="#ffffff">'
                         f'{name} <tspan font-weight="bold">{n}</tspan></text>')
            parts.append(f'<line x1="{x0 + side + stub}" y1="{y}" x2="{x0 + side + gap}" y2="{y}" stroke="{stroke}" '
                         f'stroke-width="2"{dash}/>')
            bx, by, tr = x0 + side + gap, y - bh / 2, ""
        else:            # bottom (13-24, left to right) or top (37-48, right to left); text turned upward
            bottom = n <= 24
            x = x0 + first + k * pitch if bottom else x0 + side - first - k * pitch
            if bottom:
                parts.append(f'<rect x="{x - 5}" y="{y0 + side}" width="10" height="{stub}" fill="#b0bec5" stroke="#78909c"/>')
                parts.append(f'<text transform="translate({x + 4} {y0 + side - 10}) rotate(-90)" font-size="11" '
                             f'fill="#ffffff">{label}</text>')
                parts.append(f'<line x1="{x}" y1="{y0 + side + stub}" x2="{x}" y2="{y0 + side + gap}" stroke="{stroke}" '
                             f'stroke-width="2"{dash}/>')
                tr = f' transform="translate({x} {y0 + side + gap}) rotate(-90)"'
                bx, by = -bw, -bh / 2
            else:
                parts.append(f'<rect x="{x - 5}" y="{y0 - stub}" width="10" height="{stub}" fill="#b0bec5" stroke="#78909c"/>')
                parts.append(f'<text transform="translate({x + 4} {y0 + 10}) rotate(-90)" text-anchor="end" font-size="11" '
                             f'fill="#ffffff">{name} <tspan font-weight="bold">{n}</tspan></text>')
                parts.append(f'<line x1="{x}" y1="{y0 - gap}" x2="{x}" y2="{y0 - stub}" stroke="{stroke}" '
                             f'stroke-width="2"{dash}/>')
                tr = f' transform="translate({x} {y0 - gap}) rotate(-90)"'
                bx, by = 0, -bh / 2
        parts.append(f'<g{tr}><rect x="{bx}" y="{by}" width="{bw}" height="{bh}" rx="5" fill="{fill}" '
                     f'stroke="{stroke}" stroke-width="1.5"/>'
                     f'<text x="{bx + bw / 2}" y="{by + 15}" text-anchor="middle" font-size="12">{escape(text)}</text></g>')
    legend = [("key", "키"), ("boot", "진입 키 · BOOT"), ("led", "LED"), ("usb", "USB"),
              ("power", "전원 · 리셋"), ("none", "미사용")]
    lx = 20
    for kind, label in legend:
        fill, stroke = COLORS[kind]
        parts.append(f'<rect x="{lx}" y="{height - 30}" width="16" height="16" rx="3" fill="{fill}" stroke="{stroke}"/>')
        parts.append(f'<text x="{lx + 22}" y="{height - 17}" font-size="13">{label}</text>')
        lx += 46 + sum(14 if ord(c) > 127 else 8 for c in label)
    parts.append(f'<text x="{width - 20}" y="{height - 17}" text-anchor="end" font-size="12" fill="#757575">'
                 f'{board} · tools/draw_pinmap.py</text>')
    return svg_wrap(width, height, parts)


def main() -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    files = {
        "pinmap_sayo_3x3.svg": draw_ch32_pinmap("SAYO_3X3"),
        "pinmap_sayo_2x4.svg": draw_ch32_pinmap("SAYO_2X4"),
        "buttons_sayo_3x3.svg": draw_rgb_buttons("SAYO_3X3"),
        "leds_sayo_3x3.svg": draw_rgb_leds("SAYO_3X3"),
        "buttons_sayo_2x4.svg": draw_rgb_buttons("SAYO_2X4"),
        "leds_sayo_2x4.svg": draw_rgb_leds("SAYO_2X4"),
        "pinmap_ch552g.svg": draw("MK6K1N · MK6K1NL", "CH552G 핀맵 (두 보드 공통)",
                                  "LED 데이터 (WS2812 체인)", None),
        "pinmap_mk6k1n.svg": draw("MK6K1N", "MK6K1N 핀맵 (6키 + 노브, LED 1개)",
                                  "WS2812 LED 1개 (GRB)", None),
        "pinmap_mk6k1nl.svg": draw("MK6K1NL", "MK6K1NL 핀맵 (6키 + 노브, 키마다 LED)",
                                   "LED 체인 데이터 (GRB) →",
                                   [f"LED {i} · 키 {k} 아래" for i, k in enumerate("ABCDEF")]),
        "buttons_mk6k1n.svg": draw_buttons("MK6K1N", "MK6K1N 버튼 별 맵핑 (윗면)", False),
        "buttons_mk6k1nl.svg": draw_buttons("MK6K1NL", "MK6K1NL 버튼 별 맵핑 (윗면)", True),
    }
    for name, svg in files.items():
        (OUT / name).write_text(svg, encoding="utf-8")
        print(name)


if __name__ == "__main__":
    main()
