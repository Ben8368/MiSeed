"""Exercise the wired fixed-PD thermal callbacks with synthetic hardware."""
# SPDX-License-Identifier: AGPL-3.0-only
import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


@unittest.skipUnless(shutil.which("cc"), "C compiler required")
class KernelThermalTests(unittest.TestCase):
    def test_fixed_pd_stop_failure_ownership_and_temperature_units(self):
        source = (ROOT / "kernel/power/qcom_pm8150b_thermal.c").read_text()
        start = source.index("static int smb5_thermal_get_temp(")
        stop = source.index("static const struct thermal_zone_device_ops")
        harness = (ROOT / "tests/smb5_thermal_harness.c").read_text()
        harness = harness.replace("/* DRIVER_UNDER_TEST */", source[start:stop])
        with tempfile.TemporaryDirectory() as directory:
            entry = Path(directory) / "thermal.c"
            binary = Path(directory) / ("thermal.exe" if os.name == "nt" else "thermal")
            entry.write_text(harness)
            result = subprocess.run(
                ["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                 "-Wno-unused-parameter", str(entry), "-o", str(binary)],
                capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stderr)
