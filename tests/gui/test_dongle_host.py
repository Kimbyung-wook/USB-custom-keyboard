"""Runs the dongle core PC test (firmware/dongle_ch582/test/run.py) so `uv run pytest` covers it; needs gcc."""
from __future__ import annotations

import os
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

RUN = Path(__file__).resolve().parents[2] / "firmware" / "dongle_ch582" / "test" / "run.py"


@pytest.mark.skipif(shutil.which("gcc") is None and not os.environ.get("MK_HOST_GCC"),
                    reason="gcc not on PATH (or MK_HOST_GCC)")
def test_dongle_host():
    r = subprocess.run([sys.executable, str(RUN)], capture_output=True, text=True,
                       encoding="utf-8", errors="replace")
    failed = [line for line in r.stdout.splitlines() if not line.startswith("OK")]
    assert r.returncode == 0, "\n".join(failed) + r.stderr
