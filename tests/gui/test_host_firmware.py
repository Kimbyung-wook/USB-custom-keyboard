"""Runs the firmware host test (tests/host/run.py) so `uv run pytest` covers it; needs gcc."""
from __future__ import annotations

import shutil
import subprocess
import sys
from pathlib import Path

import pytest

RUN = Path(__file__).resolve().parents[1] / "host" / "run.py"


@pytest.mark.skipif(shutil.which("gcc") is None, reason="gcc not on PATH")
def test_host_firmware():
    r = subprocess.run([sys.executable, str(RUN)], capture_output=True, text=True,
                       encoding="utf-8", errors="replace")
    failed = [line for line in r.stdout.splitlines() if not line.startswith("PASS")]
    assert r.returncode == 0, "\n".join(failed) + r.stderr
