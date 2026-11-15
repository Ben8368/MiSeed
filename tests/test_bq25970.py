"""Test actual BQ25970 backend isolation and failures without hardware."""
# SPDX-License-Identifier: AGPL-3.0-only
import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class Bq25970Tests(unittest.TestCase):
    @unittest.skipUnless(shutil.which("cc"), "C compiler required")
    def test_identity_fault_consumption_configuration_and_adc_bounds(self):
        source = (ROOT / "kernel/power/bq25970.c").read_text()
        source = "\n".join(line for line in source.splitlines()
                           if not line.startswith("#include <linux/")
                           and not line.startswith("EXPORT_SYMBOL_GPL"))
        harness = (ROOT / "tests/bq25970_harness.c").read_text().replace(
            "/* DRIVER_UNDER_TEST */", source)
        with tempfile.TemporaryDirectory() as directory:
            entry = Path(directory) / "bq.c"
            binary = Path(directory) / ("bq.exe" if os.name == "nt" else "bq")
            entry.write_text(harness)
            result = subprocess.run(
                ["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter",
                 "-I", str(ROOT / "kernel/power"), str(entry), "-o", str(binary)],
                capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stderr)
