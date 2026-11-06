"""Draw the pin maps and button maps used in docs/PINMAP.md (docs/images/).

pinmap_*.svg: the CH552G (SOP-16) as seen from the top, pins 1-8 down the
left side, 16-9 down the right side, each with what it is wired to.
buttons_*.svg: the keyboard as seen from the top, each key and the knob
with its GPIO, chip pin and slot (and LED, on boards with one per key).

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


def main() -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    files = {
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
