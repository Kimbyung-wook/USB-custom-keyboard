"""Build and run the dongle core PC tests: python run.py (needs gcc)."""
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
CORE = HERE.parent / "src" / "core"
SRCS = ["hid_desc.c", "hid_conv.c", "hogp.c", "link.c", "rq.c"]


def find_gcc():
    for c in (os.environ.get("MK_HOST_GCC"), shutil.which("gcc"), "C:/MinGW/bin/gcc.exe"):
        if c and Path(c).exists():
            return c
    sys.exit("gcc not found (set MK_HOST_GCC)")


def main():
    gcc = find_gcc()
    with tempfile.TemporaryDirectory() as tmp:
        out = Path(tmp) / "dongle_host_test.exe"
        cmd = [gcc, "-std=c99", "-Wall", "-Wextra", "-Werror", "-O1", "-I", str(CORE),
               str(HERE / "host_test.c"), *[str(CORE / s) for s in SRCS], "-o", str(out)]
        r = subprocess.run(cmd)
        if r.returncode:
            return r.returncode
        return subprocess.run([str(out)]).returncode


if __name__ == "__main__":
    sys.exit(main())
