"""Exercise PMIC5 configuration ordering, failed writes and unqualified results."""
# SPDX-License-Identifier: AGPL-3.0-only
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from tools import apply_adc5_fix

ROOT = Path(__file__).resolve().parents[1]


class Adc5Tests(unittest.TestCase):
    def test_unknown_baseline_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "pinned Linux"):
            apply_adc5_fix.transform(b"unreviewed ADC source")

    @unittest.skipUnless(shutil.which("cc"), "C compiler required")
    def test_ordering_errors_and_wrong_channel_are_not_valid_samples(self):
        harness = (ROOT / "tests/adc5_config_harness.c").read_text()
        code = (ROOT / "kernel/power/adc5_configure.inc").read_text()
        # A bundled configure/request regression must fail this peripheral model.
        mutation = code.replace(
            "return adc5_write(adc, ADC5_USR_CONV_REQ, &request, 1);",
            "return adc5_write(adc, ADC5_USR_CONV_REQ, &request, 2);")
        with tempfile.TemporaryDirectory() as tmp:
            for name, content, expected in (("actual", code, True), ("bundled", mutation, False)):
                source, binary = Path(tmp) / (name + ".c"), Path(tmp) / name
                source.write_text(harness.replace("/* DRIVER_UNDER_TEST */", content))
                result = subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                                         str(source), "-o", str(binary)], capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stderr)
                result = subprocess.run([str(binary)], capture_output=True, timeout=10)
                self.assertEqual(result.returncode == 0, expected)
