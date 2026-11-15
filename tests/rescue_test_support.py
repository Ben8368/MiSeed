"""Linux synthetic USB diagnostics; never run PID 1 or touch a USB gadget."""

# SPDX-License-Identifier: AGPL-3.0-only

import errno
import os
import select
import shutil
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
from tools.source_layout import RESCUE_FILES


def rescue_includes():
    return "\n".join('#include "' + (ROOT / "kernel" / name).as_posix() + '"'
                     for name in RESCUE_FILES)


@unittest.skipUnless(sys.platform == "linux" and shutil.which("gcc"),
                     "Linux GCC and pseudo-terminals required")
class RescueFixture(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.build.cleanup)
        root = Path(cls.build.name)
        harness = root / "harness.c"
        harness_text = (ROOT / "tests/rescue_harness.c").read_text().replace(
            '#include "@SOURCE@"', rescue_includes()).replace(
            '#include "@THERMAL_HELPERS@"', '#include "' +
            (ROOT / "tests/rescue_thermal_harness.c").as_posix() + '"')
        harness.write_text(harness_text)
        cls.program = root / "harness"
        result = subprocess.run(["gcc", "-O2", "-Wall", "-Wextra", "-Werror",
                                 str(harness), "-o", str(cls.program)], capture_output=True, text=True)
        if result.returncode:
            raise AssertionError(result.stderr)


    def read_until(self, fd, expected, timeout=6):
        received = b""
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if not select.select([fd], [], [], 0.1)[0]:
                continue
            try:
                data = os.read(fd, 4096)
            except OSError as error:
                if error.errno != errno.EIO:
                    raise
                time.sleep(0.01)
                continue
            received += data
            if expected in received:
                return received
        self.fail(f"console did not emit {expected!r}: {received[-500:]!r}")


    def run_log(self, command, env=None):
        result = subprocess.run([self.program, command], env=env, capture_output=True,
                                text=True, timeout=6)
        self.assertEqual(result.returncode, 0, result.stderr)
        return result.stdout
