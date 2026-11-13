"""Read the USB event log of the CH573 diagnostic bootloader (BL_USB_DIAG=1).

The diagnostic bootloader saves its log to Data-Flash 0..2047 (the app's
config area) once the host has been quiet for 2 s. Start the app afterwards
(power on without the boot key) and run from the repo root:

    uv run python firmware/bootloader_ch573/diag_read.py [--raw out.bin]

Layout: usb_hid.h USB_LOG_*. Read only; the config area holds the log until
the next config write.
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))

from gui.core.device import DeviceClient  # noqa: E402
from gui.core.transport import HidTransport  # noqa: E402

KINDS = {1: "SETUP", 2: "IN0 done", 3: "OUT0 done", 4: "BUS RESET", 5: "other flag"}
STD = {0: "GET_STATUS", 1: "CLEAR_FEATURE", 3: "SET_FEATURE", 5: "SET_ADDRESS",
       6: "GET_DESCRIPTOR", 7: "SET_DESCRIPTOR", 8: "GET_CONFIGURATION",
       9: "SET_CONFIGURATION", 10: "GET_INTERFACE", 11: "SET_INTERFACE"}
HID = {1: "GET_REPORT", 2: "GET_IDLE", 3: "GET_PROTOCOL", 9: "SET_REPORT", 10: "SET_IDLE",
       11: "SET_PROTOCOL"}
DESC = {1: "device", 2: "config", 3: "string", 6: "qualifier", 7: "other speed", 0x0F: "BOS",
        0x21: "HID", 0x22: "report"}


def describe(s: bytes) -> str:
    rt, rq = s[0], s[1]
    wv, wi, wl = s[2] | s[3] << 8, s[4] | s[5] << 8, s[6] | s[7] << 8
    typ = (rt >> 5) & 3
    name = (STD if typ == 0 else HID if typ == 1 else {}).get(rq, f"req {rq:#04x}")
    extra = ""
    if typ == 0 and rq == 6:
        extra = f" {DESC.get(wv >> 8, hex(wv >> 8))} #{wv & 0xFF}"
    return (f"{s.hex(' ')}  {name}{extra}  type={rt:#04x} wValue={wv:#06x} "
            f"wIndex={wi:#06x} wLength={wl}")


def decode(log: bytes) -> list[str]:
    if log[:6] != b"USBLOG":
        return ["no log (Data-Flash does not start with USBLOG)"]
    n = log[7] | log[8] << 8
    size = log[9] if log[9] in (16, 24) else 16  # 16: diag/diag2 builds (no size byte)
    out = [f"variant {log[6]} ({'empty reply' if log[6] else 'STALL'} for unknown GET_DESCRIPTOR), "
           f"{n} records of {size} B"]
    t0 = None
    for i in range(n):
        r = log[16 + size * i: 16 + size * (i + 1)]
        if len(r) < size:
            break
        if size == 16:  # old: ms time, SETUP bytes read after the reply (wrong except STALL)
            t, unit = r[2] | r[3] << 8, "ms"
            t0 = t if t0 is None else t0
            head = (f"{i:3d} +{(t - t0) & 0xFFFF:5d} ms {KINDS.get(r[0], r[0]):10s} "
                    f"INT_ST={r[4]:02x} MIS_ST={r[5]:02x} UEP0={r[6]:02x} RXLEN={r[7]:2d}")
            setup, fg = r[8:16], r[1]
        else:
            t = int.from_bytes(r[2:6], "little")
            t0 = t if t0 is None else t0
            head = (f"{i:3d} +{(t - t0) / 1000:9.3f} ms {KINDS.get(r[0], r[0]):10s} "
                    f"FG={r[6]:02x}->{r[14]:02x} ST={r[7]:02x} MIS={r[8]:02x} UEP0={r[9]:02x} "
                    f"RXLEN={r[10]:2d} ADDR={r[11]:02x} TLEN={r[12]:2d} isr#{r[13]:3d}")
            setup, fg = r[16:24], r[1]
        if r[0] == 1:
            res = "STALL" if r[1] == 0xFF else f"answered {r[1]} B"
            head += f"  {res:12s} {describe(bytes(setup))}"
        elif r[0] == 5:
            head += f"  INT_FG={fg:02x}"
        out.append(head)
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--raw", type=Path, help="also save the 2048 bytes read")
    args = ap.parse_args()
    client = DeviceClient(HidTransport.open_first())
    try:
        log = client.read_range(0, 2048)
    finally:
        client.close()
    if args.raw:
        args.raw.write_bytes(log)
    print("\n".join(decode(log)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
