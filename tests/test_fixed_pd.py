"""Compile actual fixed-PD/profile control against fault-injecting transports."""
# SPDX-License-Identifier: AGPL-3.0-only
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


@unittest.skipUnless(shutil.which("cc"), "C compiler required")
class FixedPdTests(unittest.TestCase):
    def compile_run(self, harness, source):
        text = (ROOT / "tests" / harness).read_text()
        code = (ROOT / "kernel/power" / source).read_text()
        code = code.replace('#include "qcom_pm8150b_internal.h"', '')
        with tempfile.TemporaryDirectory() as directory:
            entry = Path(directory) / "test.c"
            binary = Path(directory) / ("test.exe" if os.name == "nt" else "test")
            entry.write_text(text.replace("/* DRIVER_UNDER_TEST */", code))
            result = subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                                     "-Wno-unused-parameter", "-I", str(ROOT / "kernel/power"),
                                     str(entry), "-o", str(binary)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stderr)

    def test_fixed_pd_ordering_contract_ceilings_failures_and_thermal_recovery(self):
        self.compile_run("fixed_pd_harness.c", "qcom_pm8150b_pd.c")

    def test_tcpm_profiles_busy_detach_defaults_and_no_pps_interference(self):
        self.compile_run("tcpm_profile_harness.c", "tcpm_fixed_profile.inc")
